/// @file spectral_rig.h
/// @brief tap::mu::spectral_reverb as decorrelated_loop's forward stage, on
///        the reverb suites' protocol, and the per-bin measurements that
///        test the shaping hypothesis.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Shared by test_spectral_reverb_host.cpp (the gated rows),
// test_spectral_reverb_sweep.cpp (the MUTAP_SLOW sweep) and karaoke_ramp_dump
// (the audible rows), so a number in
// docs/reverb-afc.md's "Spectral reverb" section means the same measurement
// wherever it came from. Host-only, double precision (a property of the
// algorithm, not of a target's arithmetic).
//
// THE MEASUREMENT is support/karaoke_asg.h's, as the Dattorro rows ran it
// (test_reverb_stage_sweep.cpp), with one addition the shaping needs: the
// canceller converges with the reverb in the loop (flat: a shaped reverb has
// nothing to shape from yet), then the reverb is shaped ONCE from the
// converged canceller's impulse response F_hat (copy_impulse_response(),
// reshape_from_impulse_response(); a no-op in flat mode) and the shape is
// held through every probe of the bisection (each probe starts the reverb's
// tail from silence; reset() keeps the shape). That is the branch's
// protocol (commit 8abe958, "converge, then shape from what the canceller
// has identified"). The reverb's one block of latency comes out of the
// forward delay (decorrelated_loop's forward_stage), so the loop's total
// delay is S1's 480 or S3's 960 samples, as in the plates' rows.
//
// THE HYPOTHESIS (HANDOFF item 11): shaping up to shape_max puts long,
// loud bins where |F_hat| is weakest, which may be where the estimate is
// worst. At the moment of shaping, per bin k of the reverb's grid (65 bins,
// 375 Hz apart at block 64; folding a response onto the 128-point grid
// samples its transfer function exactly there):
//   misalignment  m_k = |F_k - F_hat_k|            (F the true band-limited path)
//   relative      m_k / |F_k|
//   decay time    T_k = rt60 * ln(a_flat) / ln(a_k) (the bin's rt60, s)
//   tail energy   E_k = w_k^2 / (1 - a_k^2)        (the bin's one-pole power gain per hop)
//   peak gain     g_k = (1 - w) + w_k / (1 - a_k)  (the bin's gain at its centre)
// and per run: Spearman's rho of T_k against m_k and against m_k / |F_k|
// (E_k and g_k rank as T_k does: all three rise with the allowance), the
// share of sum_k E_k in the quarter of the bins with the largest m_k (16 of
// 65; a flat reverb's share is 16 / 65 = 0.246), and a residual-loop
// indicator: 20 log10 of max_k m_k g_k over the flat reverb's max_k m_k g
// (same rt60 and wet), positive when the shape makes the worst bin of the
// residual loop hotter. An indicator on the hop grid, not the loop's
// frequency response.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

#include "closed_loop.h"
#include "decorrelated_loop.h"
#include "karaoke_asg.h"
#include "mutap/fft.h"
#include "mutap/spectral_reverb.h"

namespace mutap_test::spectral {

    namespace kk = karaoke;

    using reverb = tap::mu::spectral_reverb<double>;

    /// One spectral reverb configuration. shape_max 0 is the flat mode.
    struct params {
        double rt60      = 1.0;
        double wet       = 0.30;
        double shape_max = 0.0; ///< 0: flat (spectral_shaping::flat); else from_path with this ceiling
    };

    inline reverb::config config_of(const params& p) {
        reverb::config c;
        c.block_size  = kk::k_block;
        c.sample_rate = kk::k_fs;
        c.rt60        = p.rt60;
        c.wet         = p.wet;
        if (p.shape_max > 0.0) {
            c.shaping   = tap::mu::spectral_shaping::from_path;
            c.shape_max = p.shape_max;
            c.shape_min = std::min(c.shape_min, p.shape_max);
        }
        return c;
    }

    /// Spearman's rank correlation (average ranks for ties).
    inline double spearman(const std::vector<double>& x, const std::vector<double>& y) {
        auto ranks = [](const std::vector<double>& v) {
            std::vector<size_t> idx(v.size());
            std::iota(idx.begin(), idx.end(), size_t{0});
            std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return v[a] < v[b]; });
            std::vector<double> r(v.size());
            for (size_t i = 0; i < idx.size();) {
                size_t j = i;
                while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]]) {
                    ++j;
                }
                const double avg = 0.5 * static_cast<double>(i + j);
                for (size_t k = i; k <= j; ++k) {
                    r[idx[k]] = avg;
                }
                i = j + 1;
            }
            return r;
        };
        const auto rx  = ranks(x);
        const auto ry  = ranks(y);
        const auto n   = static_cast<double>(x.size());
        const auto mx  = std::accumulate(rx.begin(), rx.end(), 0.0) / n;
        const auto my  = std::accumulate(ry.begin(), ry.end(), 0.0) / n;
        double     sxy = 0.0;
        double     sxx = 0.0;
        double     syy = 0.0;
        for (size_t i = 0; i < rx.size(); ++i) {
            sxy += (rx[i] - mx) * (ry[i] - my);
            sxx += (rx[i] - mx) * (rx[i] - mx);
            syy += (ry[i] - my) * (ry[i] - my);
        }
        return (sxx > 0.0 && syy > 0.0) ? sxy / std::sqrt(sxx * syy) : std::nan("");
    }

    /// A response folded onto an n-point grid and transformed: the transfer
    /// function at k * 2 pi / n, k = 0 .. n / 2, as (re, im) pairs.
    inline std::vector<std::pair<double, double>> folded_spectrum(const std::vector<double>& h, size_t n) {
        std::vector<double> buf(n, 0.0);
        for (size_t i = 0; i < h.size(); ++i) {
            buf[i % n] += h[i];
        }
        tap::mu::real_fft fft(n);
        fft.forward_inplace(buf.data());
        std::vector<std::pair<double, double>> out(n / 2 + 1);
        out[0]     = {buf[0], 0.0};
        out[n / 2] = {buf[1], 0.0};
        for (size_t k = 1; k < n / 2; ++k) {
            out[k] = {buf[2 * k], buf[2 * k + 1]};
        }
        return out;
    }

    /// The per-bin picture at the moment of shaping (see the file comment).
    struct bin_stats {
        std::vector<double> mis; ///< m_k
        std::vector<double> rel; ///< m_k / |F_k|
        std::vector<double> t60; ///< T_k, s
        std::vector<double> e;   ///< E_k
        double              rho_t_mis   = 0.0;
        double              rho_t_rel   = 0.0;
        double              share_top   = 0.0; ///< share of sum E_k in the top quarter of m_k
        double              peak_rel_db = 0.0; ///< max m_k g_k, shaped over flat, dB
        double              misalign_db = 0.0; ///< 10 log10(sum m_k^2 / sum |F_k|^2) on the grid
    };

    inline bin_stats per_bin(const std::vector<double>& path, const std::vector<double>& f_hat, const reverb& rev) {
        const size_t n  = rev.window_size();
        const auto   F  = folded_spectrum(path, n);
        const auto   Fh = folded_spectrum(f_hat, n);
        bin_stats    s;
        const double a_flat = [&] {
            const double hops = rev.rt60() * kk::k_fs / static_cast<double>(rev.block_size());
            return std::pow(10.0, -3.0 / std::max(hops, 1.0));
        }();
        const double w_flat = rev.wet();
        const double g_flat = (1.0 - w_flat) + w_flat / (1.0 - a_flat);
        double       num    = 0.0;
        double       den    = 0.0;
        double       peak_s = 0.0;
        double       peak_f = 0.0;
        for (size_t k = 0; k < rev.bins(); ++k) {
            const double dr = F[k].first - Fh[k].first;
            const double di = F[k].second - Fh[k].second;
            const double m  = std::hypot(dr, di);
            const double f  = std::hypot(F[k].first, F[k].second);
            const double a  = rev.bin_decay(k);
            const double w  = rev.bin_wet(k);
            s.mis.push_back(m);
            s.rel.push_back(m / std::max(f, 1e-300));
            s.t60.push_back(rev.rt60() * std::log(a_flat) / std::log(a));
            s.e.push_back(w * w / (1.0 - a * a));
            peak_s = std::max(peak_s, m * ((1.0 - w_flat) + w / (1.0 - a)));
            peak_f = std::max(peak_f, m * g_flat);
            num += m * m;
            den += f * f;
        }
        s.misalign_db = 10.0 * std::log10(num / den);
        s.peak_rel_db = 20.0 * std::log10(peak_s / peak_f);
        s.rho_t_mis   = spearman(s.t60, s.mis);
        s.rho_t_rel   = spearman(s.t60, s.rel);
        std::vector<size_t> idx(s.mis.size());
        std::iota(idx.begin(), idx.end(), size_t{0});
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return s.mis[a] > s.mis[b]; });
        const size_t top   = s.mis.size() / 4; // 16 of 65
        double       e_top = 0.0;
        for (size_t i = 0; i < top; ++i) {
            e_top += s.e[idx[i]];
        }
        s.share_top = e_top / std::accumulate(s.e.begin(), s.e.end(), 0.0);
        return s;
    }

    /// The stage's broadband level change for white input, dB: output over
    /// input energy of `seconds` of white noise (the first second skipped),
    /// through a copy of `rev` (its shape as it stands).
    inline double level_db(const reverb& rev, double seconds = 20.0) {
        reverb       r = rev;
        const size_t b = r.block_size();
        r.reset();
        const auto          x = white_near_end<double>(static_cast<size_t>(seconds * kk::k_fs) / b * b, 4242);
        std::vector<double> y(x.size());
        for (size_t i = 0; i < x.size(); i += b) {
            r.process_block(&x[i], &y[i], b);
        }
        const auto skip = static_cast<size_t>(kk::k_fs);
        double     ex   = 0.0;
        double     ey   = 0.0;
        for (size_t i = skip; i < x.size(); ++i) {
            ex += x[i - r.latency()] * x[i - r.latency()];
            ey += y[i] * y[i];
        }
        return 10.0 * std::log10(ey / ex);
    }

    /// The reverb's all-wet impulse response (wet 1, flat): its return r,
    /// `seconds` long, the impulse at the first sample of a hop.
    inline std::vector<double> wet_impulse_response(double rt60, double seconds) {
        params p;
        p.rt60 = rt60;
        p.wet  = 1.0;
        reverb              r(config_of(p));
        const size_t        b = r.block_size();
        const size_t        n = static_cast<size_t>(seconds * kk::k_fs) / b * b;
        std::vector<double> x(n, 0.0);
        std::vector<double> y(n, 0.0);
        x[0] = 1.0;
        for (size_t i = 0; i < n; i += b) {
            r.process_block(&x[i], &y[i], b);
        }
        return {y.begin() + static_cast<std::ptrdiff_t>(r.latency()), y.end()};
    }

    /// One run: converge, shape, bisect.
    struct run {
        double    exact_db    = 0.0;
        double    chain_db    = 0.0;                                      ///< with the canceller
        double    open_rev_db = std::numeric_limits<double>::quiet_NaN(); ///< no canceller, the same reverb
        double    level       = 0.0;                                      ///< level_db() of the reverb as shaped
        bool      shaped      = false;                                    ///< reshape took effect
        bin_stats bins;
    };

    /// Bisected limits on `path` (see the file comment): with `chain`, of the
    /// canceller + spectral reverb `prm`; with `open_reverb`, of the open loop
    /// (no canceller) with the same reverb, as shaped, over [exact + open_lo,
    /// exact + open_hi]. The canceller converges (and the reverb is shaped)
    /// either way.
    inline run measure(const std::vector<double>& path, kk::material mat, const kk::protocol& p, unsigned seed,
                       const params& prm, bool chain, bool open_reverb, double open_lo = -40.0, double open_hi = 10.0) {
        run r;
        r.exact_db         = exact_msg_db(path, p.delay);
        const auto v_conv  = kk::near_end(mat, p.converge_blocks, seed);
        const auto v_probe = kk::near_end(mat, p.probe_blocks, seed + 10);
        reverb     rev(config_of(prm));
        kk::setup  s;
        s.mat          = mat;
        s.stage        = forward_stage<double>::of(&rev);
        const auto cfg = kk::loop_config(path, s, p.delay, nullptr);

        kk::kalman_afc::config c;
        c.fdaf.block_size = kk::k_block;
        c.fdaf.partitions = kk::k_parts;
        kk::kalman_afc      afc(c);
        std::vector<double> ir(afc.filter_length(), 0.0);
        // The open loop alone of a flat reverb needs no canceller (nothing to
        // shape from): F_hat stays zero and the per-bin misalignment is |F|.
        if (chain || prm.shape_max > 0.0) {
            kk::converge(afc, cfg, r.exact_db, p, v_conv);
            afc.copy_impulse_response(ir.data());
        }
        r.shaped = rev.reshape_from_impulse_response(ir.data(), ir.size());
        r.bins   = per_bin(path, ir, rev);
        r.level  = level_db(rev);

        r.chain_db = std::numeric_limits<double>::quiet_NaN();
        if (chain) {
            r.chain_db =
                decorrelated_msg_db(cfg, &afc, v_probe, r.exact_db + p.chain_lo, r.exact_db + p.chain_hi, p.tol_db);
        }
        if (open_reverb) {
            r.open_rev_db = decorrelated_msg_db(cfg, static_cast<const kk::naive_core*>(nullptr), v_probe,
                                                r.exact_db + open_lo, r.exact_db + open_hi, p.tol_db);
        }
        return r;
    }

} // namespace mutap_test::spectral
