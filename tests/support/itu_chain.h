// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// The pinned compliance chain and shared scenario machinery for the Stage 3
// ITU compliance suite (docs/itu-compliance.md). Every row-test builds on
// this header so the whole suite measures ONE configuration:
//
//   mutap::aec_chain<double> — raw partitioned FD-Kalman canceller
//   (transition 0.9998, initial uncertainty 10; the measured AEC sweet
//   spot, see test_postfilter.cpp) + residual suppressor at defaults.
//
// Rates per the matrix's sample-rate policy: 48 kHz (block 256, 2048 taps
// ~ 43 ms of path) and 16 kHz (block 256, 1024 taps = 64 ms) are REQUIRED;
// the 16 kHz echo paths are the 48 kHz fixture RIRs converted with the
// P.501 NOTE 2 resampler. Rooms: the image-source car cabin (RT60 66.6 ms,
// the automotive recs' scenario) and the studio (the general hands-free
// scenario); both unit-energy so levels stay on the calibration plane.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "../fixtures/rir_cabin.h"
#include "../fixtures/rir_studio.h"
#include "echo_scenario.h"
#include "itu_levels.h"
#include "itu_signals.h"
#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"

namespace mutap_test::itu {

    using compliance_chain = mutap::aec_chain<double>;

    /// One required operating rate and the chain geometry pinned for it.
    struct rate_setup {
        double fs;
        size_t block;
        size_t taps;
    };
    /// 16 kHz runs block 256 like 48 kHz — NOT a scaled-down 128. Measured
    /// (Stage 3 scratch): the partitioned Kalman's convergence collapses
    /// with block 128 at 16 kHz (ERL 8.9 dB by 600 ms vs 22.5 at block
    /// 256, which then beats even the 48 kHz reference by 1200 ms:
    /// 47.1 vs 43.6). Filter span stays 64 ms (4 partitions).
    inline rate_setup setup_48k() {
        return {48000.0, 256, 2048};
    }
    inline rate_setup setup_16k() {
        return {16000.0, 256, 1024};
    }
    inline std::vector<rate_setup> required_rates() {
        return {setup_48k(), setup_16k()};
    }

    inline compliance_chain::config chain_config(const rate_setup& rs) {
        compliance_chain::config cfg;
        cfg.canceller.block_size          = rs.block;
        cfg.canceller.partitions          = rs.taps / rs.block;
        cfg.canceller.initial_uncertainty = 10;
        // ALL per-block constants — canceller and suppressor — are
        // rescaled to keep the same PHYSICAL time constants; blocks at
        // 16 kHz last 3x longer (16 ms vs 5.33), so rescale every
        // constant to keep the same PHYSICAL time constants the Stage 2
        // measurements calibrated (a' = a^(block_s / ref_block_s)),
        // and the floor window to the same seconds.
        const double ratio = (static_cast<double>(rs.block) / rs.fs) / (256.0 / 48000.0);
        // Canceller: transition (state decay per block) and the noise-PSD
        // smoothing follow block duration — unscaled, the 16 kHz Kalman's
        // observation-noise tracker reacted 3x slower in wall time and
        // double talk dragged its filter (hangover recovery measured
        // 16.4 dB bare where 48 kHz reads 42.6).
        cfg.canceller.transition =
            0.9998; // NOT rescaled: rescaling traded TimeVariantPath to the wire (-52.3 vs req -52) for hangover
        cfg.canceller.noise_smoothing = std::pow(0.9, ratio);
        auto& pf                      = cfg.postfilter;
        pf.leakage_smoothing          = std::pow(pf.leakage_smoothing, ratio);
        pf.gain_attack                = std::pow(pf.gain_attack, ratio);
        pf.gain_release               = std::pow(pf.gain_release, ratio);
        pf.floor_smoothing            = std::pow(pf.floor_smoothing, ratio);
        pf.floor_window = std::max<size_t>(8, static_cast<size_t>(static_cast<double>(pf.floor_window) / ratio));
        // Low-band suppression cap at 300 Hz (see postfilter.h): protect
        // voice fundamentals where no analysis resolution can separate
        // them from echo; the canceller owns low-frequency echo.
        const size_t n_analysis    = pf.analysis_blocks * rs.block;
        pf.low_band_bins           = static_cast<size_t>(300.0 * static_cast<double>(n_analysis) / rs.fs) + 1;
        pf.low_band_certify_blocks = std::max<size_t>(8, static_cast<size_t>(56.0 / ratio));
        // Comfort-noise floor bias, calibrated per rate: at 16 ms blocks
        // the minimum-statistics window holds 3x fewer meter samples and
        // the minima bias deeper (measured -2.8 dB comfort-noise step
        // tracking at bias 4, against G.168's +-2 requirement; 5.6
        // restores the 48 kHz calibration).
        if (rs.fs != 48000.0) {
            pf.floor_bias = 5.6;
        }
        return cfg;
    }

    enum class room { cabin, studio };

    /// Unit-energy echo path for a room at a required rate (16 kHz paths
    /// go through the NOTE 2 resampler and keep the full RT inside the
    /// 64 ms tap budget).
    inline std::vector<double> compliance_path(room r, const rate_setup& rs) {
        const float*        rir  = (r == room::cabin) ? fixtures::k_rir_cabin : fixtures::k_rir_studio;
        const size_t        base = 4096;
        std::vector<double> p(rir, rir + base);
        if (rs.fs != 48000.0) {
            p = resample(p, 48000.0, rs.fs, 0);
        }
        p.resize(rs.taps);
        double e = 0.0;
        for (double v : p) {
            e += v * v;
        }
        for (auto& v : p) {
            v /= std::sqrt(e);
        }
        return p;
    }

    /// Drive a processor over far-end x (and optional near-end v, same
    /// length); returns the send output and the raw echo-at-mic trace.
    struct chain_run {
        std::vector<double> out;  ///< chain send output e
        std::vector<double> echo; ///< echo component at the mic (sim truth)
    };

    /// Core runner on a caller-owned sim (for scenarios that mutate the
    /// path or chain mid-run: time-variant paths, freeze phases, level
    /// switches).
    template <typename Proc>
    chain_run run_chain_on(echo_sim<double>& sim, Proc& p, size_t block, const std::vector<double>& x,
                           const std::vector<double>* v = nullptr) {
        chain_run r;
        r.out.reserve(x.size());
        r.echo.reserve(x.size());
        for (size_t blk = 0; blk + 1 <= x.size() / block; ++blk) {
            sim.step(&x[blk * block], v ? &(*v)[blk * block] : static_cast<const double*>(nullptr), &p);
            const auto& e = sim.error_block();
            r.out.insert(r.out.end(), e.begin(), e.end());
            const auto& d = sim.echo_block();
            r.echo.insert(r.echo.end(), d.begin(), d.end());
        }
        return r;
    }

    /// Convenience runner on a fresh sim over a fixed path.
    template <typename Proc>
    chain_run run_chain(Proc& p, const std::vector<double>& path, size_t block, const std::vector<double>& x,
                        const std::vector<double>* v = nullptr) {
        typename echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = block;
        echo_sim<double> sim(sc);
        return run_chain_on(sim, p, block, x, v);
    }

    // ------------------------------------------------------------- meters

    /// A-weighted 35 ms level trace of a buffer, dBm0(A).
    inline std::vector<double> level_trace_dbm0a(const std::vector<double>& x, double fs, double tau = 0.035) {
        a_weighting     aw(fs);
        auto            xa = aw.apply(x);
        exp_level_meter m(fs, tau);
        return m.trace_dbm0(xa);
    }

    /// Max of an A-weighted level trace over [from, to) samples, skipping
    /// nothing — callers position the window.
    inline double max_level_dbm0a(const std::vector<double>& x, double fs, size_t from, size_t to) {
        auto tr = level_trace_dbm0a(
            std::vector<double>(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(to)), fs);
        return *std::max_element(tr.begin() + static_cast<long>(0.1 * fs), tr.end());
    }

    /// ERL(t) between two UNWEIGHTED 35 ms level traces, read as the best
    /// value inside the preceding window (an instantaneous read lands
    /// wherever the CSS pause falls and measures meter decay, not echo).
    class erl_reader {
      public:
        erl_reader(const std::vector<double>& mic, const std::vector<double>& out, double fs, double window_s = 0.35)
            : m_fs(fs)
            , m_win(window_s) {
            exp_level_meter m_y(fs, 0.035);
            exp_level_meter m_e(fs, 0.035);
            m_try = m_y.trace_dbm0(mic);
            m_tre = m_e.trace_dbm0(out);
        }
        double by(double t_s) const {
            const size_t i0   = static_cast<size_t>(std::max(0.0, t_s - m_win) * m_fs);
            const size_t i1   = std::min(m_try.size(), static_cast<size_t>(t_s * m_fs));
            double       best = -1e9;
            for (size_t i = i0; i < i1; ++i) {
                best = std::max(best, m_try[i] - m_tre[i]);
            }
            return best;
        }

      private:
        double              m_fs;
        double              m_win;
        std::vector<double> m_try;
        std::vector<double> m_tre;
    };

    /// Welch PSD in dB (Hann, 50% overlap, n power of two), bins 0..n/2.
    inline std::vector<double> welch_psd_db(const std::vector<double>& x, size_t n) {
        mutap::basic_real_fft<double> fft(n);
        std::vector<double>           psd(n / 2 + 1, 0.0);
        std::vector<double>           buf(n);
        std::vector<double>           win(n);
        for (size_t i = 0; i < n; ++i) {
            win[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
        }
        size_t frames = 0;
        for (size_t off = 0; off + n <= x.size(); off += n / 2, ++frames) {
            for (size_t i = 0; i < n; ++i) {
                buf[i] = win[i] * x[off + i];
            }
            fft.forward_inplace(buf.data());
            psd[0] += buf[0] * buf[0];
            psd[n / 2] += buf[1] * buf[1];
            for (size_t k = 1; k < n / 2; ++k) {
                psd[k] += buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1];
            }
        }
        for (auto& v : psd) {
            v = 10.0 * std::log10(v / static_cast<double>(frames) + 1e-30);
        }
        return psd;
    }

    /// Piecewise-linear mask over frequency (Hz -> dB), interpolated on a
    /// log-frequency axis the way the recs draw their masks.
    struct freq_mask {
        std::vector<double> f;
        std::vector<double> db;
        double              at(double hz) const {
            if (hz <= f.front()) {
                return db.front();
            }
            if (hz >= f.back()) {
                return db.back();
            }
            for (size_t i = 1; i < f.size(); ++i) {
                if (hz <= f[i]) {
                    const double a = (std::log(hz) - std::log(f[i - 1])) / (std::log(f[i]) - std::log(f[i - 1]));
                    return db[i - 1] + a * (db[i] - db[i - 1]);
                }
            }
            return db.back();
        }
    };

    /// Band-average attenuation spectrum out-vs-reference in dB over
    /// [f0, f1] third-octave-spaced bands: attenuation(f) = ref(f) - out(f).
    struct band_atten {
        std::vector<double> f_center;
        std::vector<double> atten_db;
    };
    inline band_atten attenuation_spectrum(const std::vector<double>& ref, const std::vector<double>& out, double fs,
                                           size_t nfft, double f0, double f1) {
        const auto pr = welch_psd_db(ref, nfft);
        const auto po = welch_psd_db(out, nfft);
        band_atten r;
        for (double fc = f0; fc <= f1 * 1.0001; fc *= std::pow(2.0, 1.0 / 3.0)) {
            const double lo = fc / std::pow(2.0, 1.0 / 6.0);
            const double hi = fc * std::pow(2.0, 1.0 / 6.0);
            const size_t k0 = static_cast<size_t>(lo * static_cast<double>(nfft) / fs);
            const size_t k1 = std::min(po.size() - 1, static_cast<size_t>(hi * static_cast<double>(nfft) / fs));
            if (k1 <= k0) {
                continue;
            }
            double sr = 0.0;
            double so = 0.0;
            for (size_t k = k0; k < k1; ++k) {
                sr += std::pow(10.0, pr[k] / 10.0);
                so += std::pow(10.0, po[k] / 10.0);
            }
            r.f_center.push_back(fc);
            r.atten_db.push_back(10.0 * std::log10(sr / so));
        }
        return r;
    }

} // namespace mutap_test::itu
