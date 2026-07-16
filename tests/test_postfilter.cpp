// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Stage 2 of the ITU compliance plan (docs/itu-compliance.md): the residual
// suppressor + comfort noise (postfilter.h) and the aec_chain it composes
// with the linear canceller. Every threshold here was measured first in the
// scratch harness and is asserted with margin; the matrix rows each test
// pre-proves are named in its comment (the full multi-rate compliance suite
// is Stage 3 — these pin the chain's headline behavior on the golden model).
//
// Chain under test: RAW partitioned frequency-domain Kalman canceller
// (transition 0.9998 and initial_uncertainty 10 — the AEC sweet spot
// measured in the Stage 2 sweeps: deeper steady state than the 0.9995 AFC
// default lifts the worst double-talk echo-loss band ~5 dB while 0.99995
// costs double-talk transparency and tracking, and the larger P(0) buys
// ~4 dB of early convergence, 46.9 vs 45.2 dB ERL by 1200 ms) followed by
// the residual suppressor at its defaults. Note the chain deliberately
// does NOT use pem_afc: open-loop AEC has an exogenous far end, and PEM's
// predictor refit floors misalignment near -20 dB where the raw Kalman
// core reaches -75 dBm0(A) bare (measured; the full story is in
// postfilter.h's class comment).
//
// Measured (this file's exact protocols and chain config, double
// precision, block 256, 2048-tap unit-energy paths at 48 kHz unless
// stated):
//
//   single-talk max residual, A-weighted 35 ms meter, CSS at -16 dBm0:
//     kalman+pf cabin  -79.9 dBm0(A)   kalman+pf studio -88.9
//     nlms+pf   cabin  -65.0           kalman 16 kHz cabin -69.1
//   double-talk (AM-FM orthogonal pair, P.501 Table 7-6):
//     send atten (send -1.7 dBPa):   cabin 1.05 dB   studio 0.93
//     echo loss worst band 200-6950 Hz (send -25.7 dBPa):
//                                    cabin 38.0 dB (6660 Hz)   studio 34.1 (270 Hz)
//   convergence from cold start (CSS -16 dBm0, 35 ms meters):
//     ERL 43.2 dB by 600 ms, 46.9 by 1200, 55.3 by 2000, 60.6 by 5000
//   comfort noise (Hoth at -46 dBm0 near end, CSS -16 dBm0 far end):
//     level delta -1.20 dB; band spectrum deviation <= 1.69 dB;
//     noise pumping 3.3 dB; DT-onset build-up 20.9 ms (gain_release
//     0.85; 0.9 measured 26.0 ms against the switching rows' 25 ms
//     margin target, which is what set the default)
//
// Matrix margin targets asserted: ST residual < -64 dBm0(A)
// (ITU_EchoLevel), DT send atten <= 1.5 dB (ITU_DtSendAtten /
// ITU_DtSentSpeech), DT echo loss >= 33 dB per band (ITU_DtEchoLoss),
// ERL >= 40 dB by 600 ms and >= 46 dB by 1200 ms (ITU_ConvergenceQuiet),
// comfort noise +1/-2.5 dB level and half-mask spectrum
// (ITU_ComfortNoiseLevel/Spectrum), pumping <= 5 dB (ITU_NoisePump*),
// build-up <= 25 ms (switching rows).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_cabin.h"
#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"
#include "support/echo_scenario.h"
#include "support/itu_levels.h"
#include "support/itu_signals.h"

namespace {

    using namespace mutap_test;

    using chain_kalman = mutap::aec_chain<double>;
    using chain_nlms   = mutap::aec_chain<double, mutap::partitioned_fdaf<double>>;

    constexpr size_t k_block = 256;
    constexpr size_t k_taps  = 2048;
    constexpr double k_fs    = 48000.0;

    std::vector<double> unit_energy(std::vector<double> f) {
        double e = 0.0;
        for (double v : f) {
            e += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(e);
        }
        return f;
    }

    std::vector<double> cabin_path() {
        return unit_energy({fixtures::k_rir_cabin, fixtures::k_rir_cabin + k_taps});
    }
    std::vector<double> studio_path() {
        return unit_energy({fixtures::k_rir_studio, fixtures::k_rir_studio + k_taps});
    }

    template <typename Chain>
    typename Chain::config aec_config(size_t block = k_block, size_t taps = k_taps) {
        typename Chain::config cfg;
        cfg.canceller.block_size = block;
        cfg.canceller.partitions = taps / block;
        if constexpr (requires { cfg.canceller.transition; }) {
            cfg.canceller.transition          = 0.9998;
            cfg.canceller.initial_uncertainty = 10;
        }
        return cfg;
    }

    /// Max of the A-weighted 35 ms level trace over [from, to).
    double max_level_dbm0a(const std::vector<double>& x, double fs, size_t from, size_t to) {
        itu::a_weighting aw(fs);
        auto xa = aw.apply(std::vector<double>(x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(to)));
        itu::exp_level_meter m(fs, 0.035);
        const auto           tr = m.trace_dbm0(xa);
        return *std::max_element(tr.begin() + static_cast<long>(0.1 * fs), tr.end());
    }

    /// Single-talk protocol: CSS at -16 dBm0 through the path, no near
    /// end; returns max A-weighted residual level over the last third.
    template <typename Proc>
    double single_talk_residual(Proc& p, const std::vector<double>& path, double fs, size_t block) {
        typename echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = block;
        echo_sim<double> sim(sc);

        itu::css_config cc;
        cc.periods = 60;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, fs);
        itu::set_level_dbm0(x, -16.0);

        std::vector<double> out;
        out.reserve(x.size());
        for (size_t blk = 0; blk + 1 <= x.size() / block; ++blk) {
            sim.step(&x[blk * block], static_cast<const double*>(nullptr), &p);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
        }
        return max_level_dbm0a(out, fs, out.size() * 2 / 3, out.size());
    }

    /// Double-talk protocol: converge on CSS, then AM-FM orthogonal pair —
    /// far end on the receive plan at -16 dBm0 through the path, near end
    /// on the send plan at send_dbpa. Returns the settled second half.
    struct dt_run {
        std::vector<double> out;
        std::vector<double> x;
        std::vector<double> v;
        size_t              n = 0;
    };

    template <typename Proc>
    dt_run run_double_talk(Proc& p, const std::vector<double>& path, double fs, double send_dbpa) {
        typename echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = k_block;
        echo_sim<double> sim(sc);

        itu::css_config cc;
        cc.periods = 30;
        cc.shaped  = true;
        auto xc    = itu::make_css_at(cc, fs);
        itu::set_level_dbm0(xc, -16.0);
        for (size_t blk = 0; blk + 1 <= xc.size() / k_block; ++blk) {
            sim.step(&xc[blk * k_block], static_cast<const double*>(nullptr), &p);
        }

        dt_run r;
        r.n = static_cast<size_t>(8 * fs);
        r.x = itu::make_amfm(itu::amfm_receive_plan(), r.n, fs);
        itu::set_level_dbm0(r.x, -16.0);
        r.v             = itu::make_amfm(itu::amfm_send_plan(), r.n, fs);
        const double vg = itu::dbpa_to_rms(send_dbpa) / itu::rms_of(r.v.data(), r.v.size());
        for (auto& s : r.v) {
            s *= vg;
        }
        r.out.reserve(r.n);
        for (size_t blk = 0; blk + 1 <= r.n / k_block; ++blk) {
            sim.step(&r.x[blk * k_block], &r.v[blk * k_block], &p);
            const auto& e = sim.error_block();
            r.out.insert(r.out.end(), e.begin(), e.end());
        }
        return r;
    }

    std::vector<double> settled_half(const std::vector<double>& s, size_t n) {
        return {s.begin() + static_cast<long>(n / 2), s.begin() + static_cast<long>(n)};
    }

    // ---------------------------------------------------------------- tests

    // ITU_EchoLevel margin target: < -64 dBm0(A) single-talk residual.
    // Measured: cabin -79.9, studio -88.9 dBm0(A).
    TEST(ItuChain, SingleTalkResidualKalman) {
        {
            chain_kalman c(aec_config<chain_kalman>());
            EXPECT_LT(single_talk_residual(c, cabin_path(), k_fs, k_block), -72.0);
        }
        {
            chain_kalman c(aec_config<chain_kalman>());
            EXPECT_LT(single_talk_residual(c, studio_path(), k_fs, k_block), -80.0);
        }
    }

    // The NLMS-core chain also meets the margin target (measured -65.0).
    TEST(ItuChain, SingleTalkResidualNlmsCore) {
        chain_nlms c(aec_config<chain_nlms>());
        EXPECT_LT(single_talk_residual(c, cabin_path(), k_fs, k_block), -64.0);
    }

    // 16 kHz is a REQUIRED operating rate (matrix sample-rate policy).
    // Cabin path resampled with the NOTE 2 resampler; measured -69.1.
    TEST(ItuChain, SingleTalkResidualAt16k) {
        const double        fs16 = 16000.0;
        std::vector<double> cab48(fixtures::k_rir_cabin, fixtures::k_rir_cabin + 4096);
        auto                cab16 = itu::resample(cab48, static_cast<double>(fixtures::k_rir_cabin_fs), fs16, 0);
        cab16.resize(1024); // 64 ms covers the cabin RT60
        chain_kalman c(aec_config<chain_kalman>(128, 1024));
        EXPECT_LT(single_talk_residual(c, unit_energy(cab16), fs16, 128), -64.0);
    }

    // ITU_DtSendAtten / ITU_DtSentSpeech margin target: <= 1.5 dB near-end
    // attenuation during double talk, send at -1.7 dBPa. Measured: cabin
    // 1.05, studio 0.93 dB (bare canceller -0.41: the postfilter's whole
    // cost is ~1.4 dB, spent at the comb bins the analysis cannot split).
    TEST(ItuChain, DoubleTalkSendAttenuation) {
        const auto sp = itu::amfm_send_plan();
        for (const auto& path : {cabin_path(), studio_path()}) {
            chain_kalman c(aec_config<chain_kalman>());
            const auto   r  = run_double_talk(c, path, k_fs, -1.7);
            const auto   vh = settled_half(r.v, r.n);
            const auto   oh = settled_half(r.out, r.n);
            const double atten =
                itu::comb_band_level_db(vh, sp, 0.0, k_fs) - itu::comb_band_level_db(oh, sp, 0.0, k_fs);
            EXPECT_LE(atten, 1.5);
        }
    }

    // ITU_DtEchoLoss margin target: >= 33 dB echo loss during double talk
    // in EACH receive band 200-6950 Hz, send at -25.7 dBPa. Measured worst
    // bands: cabin 38.0 dB (6660 Hz), studio 34.1 dB (the 270 Hz line the
    // gain constraint cannot notch selectively — the canceller carries it).
    TEST(ItuChain, DoubleTalkEchoLossPerBand) {
        const auto   rp               = itu::amfm_receive_plan();
        const double worst_expected[] = {35.0, 33.0}; // cabin, studio
        size_t       room             = 0;
        for (const auto& path : {cabin_path(), studio_path()}) {
            chain_kalman c(aec_config<chain_kalman>());
            const auto   r     = run_double_talk(c, path, k_fs, -25.7);
            const auto   xh    = settled_half(r.x, r.n);
            const auto   oh    = settled_half(r.out, r.n);
            double       worst = 1e9;
            for (size_t b = 0; b < rp.f0.size(); ++b) {
                if (rp.f0[b] < 200.0 || rp.f0[b] > 6950.0) {
                    continue;
                }
                itu::amfm_plan one;
                one.f0.push_back(rp.f0[b]);
                one.df.push_back(rp.df[b]);
                const double loss =
                    itu::comb_band_level_db(xh, one, 0.0, k_fs) - itu::comb_band_level_db(oh, one, 0.0, k_fs);
                worst = std::min(worst, loss);
            }
            EXPECT_GE(worst, worst_expected[room++]);
        }
    }

    // ITU_ConvergenceQuiet margin targets: ERL >= 40 dB by 600 ms and
    // >= 46 dB by 1200 ms from cold start. Measured 43.2 / 46.9 dB.
    TEST(ItuChain, ConvergenceFromColdStart) {
        chain_kalman                      c(aec_config<chain_kalman>());
        typename echo_sim<double>::config sc;
        sc.echo_path  = cabin_path();
        sc.block_size = k_block;
        echo_sim<double> sim(sc);

        itu::css_config cc;
        cc.periods = 15;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, k_fs);
        itu::set_level_dbm0(x, -16.0);

        std::vector<double> out;
        std::vector<double> mic;
        for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
            sim.step(&x[blk * k_block], static_cast<const double*>(nullptr), &c);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
            const auto& y = sim.echo_block();
            mic.insert(mic.end(), y.begin(), y.end());
        }
        itu::exp_level_meter m_y(k_fs, 0.035);
        itu::exp_level_meter m_e(k_fs, 0.035);
        const auto           tr_y = m_y.trace_dbm0(mic);
        const auto           tr_e = m_e.trace_dbm0(out);
        // ERL by t: best reading within the preceding CSS period (350 ms)
        // — an instantaneous read lands wherever the CSS pause happens to
        // fall and measures meter decay, not echo.
        const auto erl_by = [&](double t) {
            const size_t i0   = static_cast<size_t>((t - 0.35) * k_fs);
            const size_t i1   = static_cast<size_t>(t * k_fs);
            double       best = -1e9;
            for (size_t i = i0; i < i1; ++i) {
                best = std::max(best, tr_y[i] - tr_e[i]);
            }
            return best;
        };
        EXPECT_GE(erl_by(0.6), 40.0);
        EXPECT_GE(erl_by(1.2), 46.0);
    }

    // ITU_ComfortNoiseLevel (+1/-2.5 dB target), ITU_ComfortNoiseSpectrum
    // (half-mask: +-6 dB 200-800 Hz, +-5 to 2 kHz, +-3 above) and
    // ITU_NoisePump* (<= 5 dB target): Hoth noise at -46 dBm0 in the near
    // end, far-end CSS bursts. Measured: level delta -1.20 dB, worst band
    // deviation 1.69 dB, pumping 3.3 dB.
    TEST(ItuChain, ComfortNoiseMatchesFloor) {
        chain_kalman                      c(aec_config<chain_kalman>());
        typename echo_sim<double>::config sc;
        sc.echo_path  = cabin_path();
        sc.block_size = k_block;
        echo_sim<double> sim(sc);

        const size_t n_half = static_cast<size_t>(10 * k_fs);
        auto         noise  = itu::make_hoth_noise(2 * n_half, 7, k_fs);
        itu::set_level_dbm0(noise, -46.0);
        itu::css_config cc;
        cc.periods = 28;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, k_fs);
        itu::set_level_dbm0(x, -16.0);
        x.resize(2 * n_half, 0.0); // second half: far end silent

        std::vector<double> out;
        for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
            sim.step(&x[blk * k_block], &noise[blk * k_block], &c);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
        }
        const auto seg = [&](double t0, double t1) {
            return std::vector<double>(out.begin() + static_cast<long>(t0 * k_fs),
                                       out.begin() + static_cast<long>(t1 * k_fs));
        };
        auto talk  = seg(6.0, 9.5);   // far end active: suppressed + comfort fill
        auto quiet = seg(16.0, 19.5); // far end silent: the true noise floor

        // Level match, A-weighted.
        itu::a_weighting aw(k_fs);
        auto             ta = aw.apply(talk);
        auto             qa = aw.apply(quiet);
        const double     lt = itu::level_dbov(ta.data(), ta.size()) - itu::k_dbov_per_dbm0;
        const double     lq = itu::level_dbov(qa.data(), qa.size()) - itu::k_dbov_per_dbm0;
        EXPECT_LE(lt - lq, 1.0);
        EXPECT_GE(lt - lq, -2.5);

        // Spectral match: Welch band levels, half-mask bounds.
        const auto band_levels = [&](const std::vector<double>& s) {
            const size_t                  n = 8192;
            mutap::basic_real_fft<double> fft(n);
            std::vector<double>           psd(n / 2 + 1, 0.0);
            std::vector<double>           buf(n);
            std::vector<double>           win(n);
            for (size_t i = 0; i < n; ++i) {
                win[i] =
                    0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
            }
            size_t frames = 0;
            for (size_t off = 0; off + n <= s.size(); off += n / 2, ++frames) {
                for (size_t i = 0; i < n; ++i) {
                    buf[i] = win[i] * s[off + i];
                }
                fft.forward_inplace(buf.data());
                psd[0] += buf[0] * buf[0];
                psd[n / 2] += buf[1] * buf[1];
                for (size_t k = 1; k < n / 2; ++k) {
                    psd[k] += buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1];
                }
            }
            const double        edges[] = {200, 400, 800, 1600, 3150, 6300, 8000};
            std::vector<double> bands;
            for (size_t b = 0; b + 1 < 7; ++b) {
                double       sum = 0.0;
                const size_t k0  = static_cast<size_t>(edges[b] * n / k_fs);
                const size_t k1  = static_cast<size_t>(edges[b + 1] * n / k_fs);
                for (size_t k = k0; k < k1; ++k) {
                    sum += psd[k];
                }
                bands.push_back(10.0 * std::log10(sum / static_cast<double>(frames) + 1e-30));
            }
            return bands;
        };
        const auto   bt     = band_levels(talk);
        const auto   bq     = band_levels(quiet);
        const double mask[] = {6.0, 6.0, 5.0, 3.0, 3.0, 3.0};
        for (size_t b = 0; b < 6; ++b) {
            EXPECT_LE(std::abs(bt[b] - bq[b]), mask[b]) << "band " << b;
        }

        // Noise pumping over the far-end-active segment.
        itu::exp_level_meter m(k_fs, 0.035);
        const auto           tr   = m.trace_dbm0(ta);
        const double         vmax = *std::max_element(tr.begin() + 1680, tr.end());
        const double         vmin = *std::min_element(tr.begin() + 1680, tr.end());
        EXPECT_LE(vmax - vmin, 5.0);
    }

    // Switching build-up target <= 25 ms: near-end onset mid-double-talk
    // reaches within 3 dB of its settled send level in 20.9 ms (measured).
    TEST(ItuChain, NearEndBuildUpTime) {
        chain_kalman                      c(aec_config<chain_kalman>());
        typename echo_sim<double>::config sc;
        sc.echo_path  = cabin_path();
        sc.block_size = k_block;
        echo_sim<double> sim(sc);

        itu::css_config cc;
        cc.periods = 30;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, k_fs);
        itu::set_level_dbm0(x, -16.0);

        itu::css_config cd;
        cd.periods = 30;
        cd.kind    = itu::css_kind::double_talk;
        cd.shaped  = true;
        auto v     = itu::make_css_at(cd, k_fs);
        v.resize(x.size(), 0.0);
        const double vg = itu::dbpa_to_rms(-1.7) / itu::rms_of(v.data(), v.size() / 2);
        // Onset at 2/3 of the run — the settled-level window below needs
        // 2 s of trace after it. (An onset at 5/6 left only 1.75 s and the
        // window read past the trace: ASan aborted, MSVC compared against
        // a garbage median. The other platforms' passes were luck.)
        const size_t        t0 = (x.size() / k_block * 2 / 3) * k_block;
        std::vector<double> vv(x.size(), 0.0);
        for (size_t i = t0; i < x.size(); ++i) {
            vv[i] = vg * v[i - t0];
        }
        std::vector<double> out;
        for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
            sim.step(&x[blk * k_block], &vv[blk * k_block], &c);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
        }
        itu::a_weighting     aw(k_fs);
        auto                 seg = aw.apply(std::vector<double>(out.begin() + static_cast<long>(t0) - 4800, out.end()));
        itu::exp_level_meter m(k_fs, 0.005);
        const auto           tr = m.trace_dbm0(seg);
        ASSERT_GE(tr.size(), 4800 + static_cast<size_t>(2 * k_fs));
        std::vector<double> settled(tr.begin() + 4800 + static_cast<long>(k_fs),
                                    tr.begin() + 4800 + static_cast<long>(2 * k_fs));
        std::nth_element(settled.begin(), settled.begin() + static_cast<long>(settled.size() / 2), settled.end());
        const double target = settled[settled.size() / 2];
        size_t       t_hit  = tr.size();
        for (size_t i = 4800; i < tr.size(); ++i) {
            if (tr[i] >= target - 3.0) {
                t_hit = i - 4800;
                break;
            }
        }
        EXPECT_LE(1000.0 * static_cast<double>(t_hit) / k_fs, 25.0);
    }

    // The pem_afc 3-argument surface still composes (the closed-loop AFC
    // heritage path through aec_chain's echo_estimate_block() branch).
    TEST(ItuChain, PemAfcCancellerStillComposes) {
        using chain_pem = mutap::aec_chain<double, mutap::pem_afc<double>>;
        chain_pem::config cfg;
        cfg.canceller.fdaf.block_size = k_block;
        cfg.canceller.fdaf.partitions = k_taps / k_block;
        chain_pem c(cfg);
        // Composition smoke test, not a depth claim: the PEM structure's
        // predictor refit floors misalignment near -20 dB (the reason the
        // AEC chain defaults to the raw core). Measured 16.5 dB below the
        // raw echo, converged.
        typename echo_sim<double>::config sc;
        sc.echo_path  = cabin_path();
        sc.block_size = k_block;
        echo_sim<double> sim(sc);
        itu::css_config  cc;
        cc.periods = 20;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, k_fs);
        itu::set_level_dbm0(x, -16.0);
        std::vector<double> out;
        std::vector<double> mic;
        for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
            sim.step(&x[blk * k_block], static_cast<const double*>(nullptr), &c);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
            const auto& y = sim.echo_block();
            mic.insert(mic.end(), y.begin(), y.end());
        }
        const size_t from = out.size() * 2 / 3;
        const double le   = itu::level_dbov(out.data() + from, out.size() - from);
        const double ly   = itu::level_dbov(mic.data() + from, mic.size() - from);
        EXPECT_LT(le, ly - 12.0);
    }

    // reset() restores the initial state exactly, comfort-noise PRNG
    // included: two identical runs produce identical output.
    TEST(ItuChain, ResetRestoresDeterminism) {
        chain_kalman c(aec_config<chain_kalman>());
        auto         run_once = [&] {
            typename echo_sim<double>::config sc;
            sc.echo_path  = cabin_path();
            sc.block_size = k_block;
            echo_sim<double> sim(sc);
            itu::css_config  cc;
            cc.periods = 4;
            cc.shaped  = true;
            auto x     = itu::make_css_at(cc, k_fs);
            itu::set_level_dbm0(x, -16.0);
            std::vector<double> out;
            for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
                sim.step(&x[blk * k_block], static_cast<const double*>(nullptr), &c);
                const auto& e = sim.error_block();
                out.insert(out.end(), e.begin(), e.end());
            }
            return out;
        };
        const auto first = run_once();
        c.reset();
        const auto second = run_once();
        ASSERT_EQ(first.size(), second.size());
        for (size_t i = 0; i < first.size(); ++i) {
            ASSERT_EQ(first[i], second[i]) << "diverged at sample " << i;
        }
    }

    TEST(PostFilterConfigValidation, RejectsBadConfigs) {
        using pf = mutap::residual_suppressor<double>;
        pf::config good;
        EXPECT_NO_THROW(pf{good});
        {
            auto c       = good;
            c.block_size = 100; // not a power of two
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
        {
            auto c            = good;
            c.analysis_blocks = 2; // < 4
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
        {
            auto c               = good;
            c.max_suppression_db = 0.0;
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
        {
            auto c             = good;
            c.over_subtraction = 0.5; // < 1
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
        {
            auto c              = good;
            c.leakage_smoothing = 1.0; // not in [0, 1)
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
        {
            auto c         = good;
            c.floor_window = 0;
            EXPECT_THROW(pf{c}, std::invalid_argument);
        }
    }

    TEST(PostFilterRtContract, ProcessingPathIsNoexcept) {
        using pf = mutap::residual_suppressor<double>;
        static_assert(noexcept(std::declval<pf&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<pf&>().reset()));
        static_assert(noexcept(std::declval<const pf&>().gain_at(0)));
        static_assert(noexcept(std::declval<const pf&>().coherence_at(0)));
        static_assert(noexcept(std::declval<const pf&>().leakage_at(0)));
        static_assert(noexcept(std::declval<chain_kalman&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<chain_kalman&>().reset()));
        SUCCEED();
    }

} // namespace
