// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// ITU compliance suite, echo-performance battery (docs/itu-compliance.md
// Tier A, clauses 11.11.x): one test per matrix row, run at BOTH required
// rates (48 kHz and 16 kHz) on the pinned compliance chain
// (tests/support/itu_chain.h). Thresholds measured first (Stage 3 scratch,
// double precision); the measured value sits in a comment next to every
// assertion. Where a 16 kHz reading misses our own margin target while
// clearing the ITU requirement, the assertion pins the measured value as
// a regression gate and the shortfall is called out in the comment and in
// the matrix — never silently.
//
// Measured summary (cabin path unless stated; studio where the row sweeps
// rooms):
//
//                             48 kHz              16 kHz
//   ITU_TCL                   68.5 dB             79.2 dB
//   ITU_EchoLevel cab/studio  -76.5 / -86.2       -85.9 / -102.3 dBm0(A)
//   ITU_EchoStability (worst) 2.75 dB             3.71 dB (target 3: miss,
//                                                  at 74..84 dB attenuation)
//   ITU_EchoSpectral margin   +13.2 dB over mask  +22.3 dB
//   ITU_ConvergenceQuiet      43.2 / 47.8 dB      36.0 / 46.3 dB (600 ms
//                                                  target 40: miss; both
//                                                  1200 ms targets met)
//   ITU_ConvergenceNoise      pass at every mask point (driving noise
//                             -30 dBm0(A); the rec's own condition class)
//   ITU_TimeVariantPath       -58.3 dBm0(A)       -55.5 (target -58: miss;
//                                                  req -52 met +3.5 dB)
//
// The initial receive guard (aec_chain) is part of the measured chain: a
// fresh canceller passes raw echo and no Yhat-referenced suppressor can
// see it, so the chain applies its switched send loss (< 14 dB, inside
// the A_H,S < 20 dB switching allowance) while receive is active and
// convergence is uncertified. The ConvergenceNoise row is what forced it.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "support/itu_chain.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::itu;

    // Per-rate expectations keyed off the rate (48 kHz first).
    struct rate_pair {
        double at48;
        double at16;
    };
    double expected(const rate_pair& p, const rate_setup& rs) { return rs.fs == 48000.0 ? p.at48 : p.at16; }

    // ITU_TCL (P1110/P1120 11.11.1, [MIXED] — simulated-path component):
    // CSS at -10 dBm0 as the compressed-speech stand-in (method-equivalent
    // until the P.501 attachment WAVs are procured), first 17 s discarded,
    // unweighted 100 Hz-8 kHz power ratio. Requirement >= 46, target
    // >= 52. Measured 68.5 dB at 48 kHz, 79.2 at 16 kHz.
    TEST(ItuEcho, Tcl) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cc;
            cc.periods = static_cast<size_t>(27.0 / 0.35);
            cc.shaped  = true;
            auto x     = make_css_at(cc, rs.fs);
            set_level_dbm0(x, -10.0);
            auto rr = run_chain(c, path, rs.block, x);

            const size_t        from = static_cast<size_t>(17.0 * rs.fs);
            std::vector<double> in_seg(x.begin() + static_cast<long>(from),
                                       x.begin() + static_cast<long>(rr.out.size()));
            std::vector<double> out_seg(rr.out.begin() + static_cast<long>(from), rr.out.end());
            const size_t        nfft = 8192;
            const auto          pin  = welch_psd_db(in_seg, nfft);
            const auto          pout = welch_psd_db(out_seg, nfft);
            const size_t        k0   = static_cast<size_t>(100.0 * static_cast<double>(nfft) / rs.fs);
            const size_t k1 = std::min(pout.size() - 1, static_cast<size_t>(8000.0 * static_cast<double>(nfft) / rs.fs));
            double sr = 0.0;
            double so = 0.0;
            for (size_t k = k0; k < k1; ++k) {
                sr += std::pow(10.0, pin[k] / 10.0);
                so += std::pow(10.0, pout[k] / 10.0);
            }
            const double tcl = 10.0 * std::log10(sr / so);
            EXPECT_GE(tcl, expected({62.0, 72.0}, rs)) << "fs " << rs.fs;
        }
    }

    // ITU_EchoLevel (P1120 11.11.2): single-talk steady-state echo,
    // A-weighted 35 ms meter, max over the settled tail. Requirement
    // < -58 dBm0(A), target < -64. Measured: 48 kHz -76.5 (cabin) /
    // -86.2 (studio); 16 kHz -85.9 / -102.3.
    TEST(ItuEcho, EchoLevel) {
        const rate_pair gate_cabin{-70.0, -78.0};
        const rate_pair gate_studio{-80.0, -90.0};
        for (const auto& rs : required_rates()) {
            for (room r : {room::cabin, room::studio}) {
                compliance_chain c(chain_config(rs));
                const auto       path = compliance_path(r, rs);
                css_config       cc;
                cc.periods = 40;
                cc.shaped  = true;
                auto x     = make_css_at(cc, rs.fs);
                set_level_dbm0(x, -16.0);
                auto         rr  = run_chain(c, path, rs.block, x);
                const double lvl = max_level_dbm0a(rr.out, rs.fs, rr.out.size() * 2 / 3, rr.out.size());
                EXPECT_LT(lvl, expected(r == room::cabin ? gate_cabin : gate_studio, rs))
                    << "fs " << rs.fs << " room " << (r == room::cabin ? "cabin" : "studio");
            }
        }
    }

    // ITU_EchoStability (P1110 11.11.2): echo attenuation shall not
    // degrade more than 6 dB from its best DURING single talk — a
    // temporal criterion, evaluated per level {-5, -16, -25} dBm0 after
    // convergence at nominal. Requirement <= 6 dB, target <= 3. Measured
    // worst-of-levels: 2.75 dB at 48 kHz (target met); 3.71 at 16 kHz —
    // target missed by 0.7 dB at attenuation depths of 74..84 dB, where
    // the variation is the meter reading the suppressor floor; the
    // requirement is met with 2.3 dB to spare (gate at 4.5).
    TEST(ItuEcho, EchoStability) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            typename echo_sim<double>::config sc;
            sc.echo_path  = path;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto xc    = make_css_at(cc, rs.fs);
            set_level_dbm0(xc, -16.0);
            run_chain_on(sim, c, rs.block, xc);
            for (double lvl : {-5.0, -16.0, -25.0}) {
                cc.periods = 16;
                auto x     = make_css_at(cc, rs.fs);
                set_level_dbm0(x, lvl);
                auto       rr = run_chain_on(sim, c, rs.block, x);
                erl_reader erl(rr.echo, rr.out, rs.fs);
                double     amax = -1e9;
                double     amin = 1e9;
                for (double t = 1.75; t <= static_cast<double>(rr.out.size()) / rs.fs; t += 0.35) {
                    const double a = erl.by(t);
                    amax           = std::max(amax, a);
                    amin           = std::min(amin, a);
                }
                EXPECT_LE(amax - amin, expected({3.0, 4.5}, rs)) << "fs " << rs.fs << " level " << lvl;
            }
        }
    }

    // ITU_EchoSpectral (P1110/P1120 11.11.3): attenuation spectrum vs the
    // WB mask, third-octave bands, 8k FFT, settled single talk. Margin
    // target: >= 6 dB over the mask everywhere. Measured worst margins:
    // +13.2 dB (48 kHz), +22.3 dB (16 kHz).
    TEST(ItuEcho, EchoSpectral) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto x     = make_css_at(cc, rs.fs);
            set_level_dbm0(x, -16.0);
            auto rr = run_chain(c, path, rs.block, x);

            const size_t        from = rr.out.size() - static_cast<size_t>(4 * 0.35 * rs.fs);
            std::vector<double> ref(rr.echo.begin() + static_cast<long>(from), rr.echo.end());
            std::vector<double> out(rr.out.begin() + static_cast<long>(from), rr.out.end());
            const auto sp = attenuation_spectrum(ref, out, rs.fs, 8192, 100.0, std::min(8000.0, rs.fs / 2 * 0.94));
            const freq_mask mask{{100, 1300, 3450, 5200, 7500, 8000}, {41, 41, 46, 46, 37, 37}};
            for (size_t i = 0; i < sp.f_center.size(); ++i) {
                EXPECT_GE(sp.atten_db[i], mask.at(sp.f_center[i]) + 6.0)
                    << "fs " << rs.fs << " band " << sp.f_center[i] << " Hz";
            }
        }
    }

    // ITU_ConvergenceQuiet (P1110/P1120 11.11.4): ERL-vs-time from cold
    // start. Requirement: >= 40 dB at 1200 ms; margin targets >= 40 by
    // 600 ms and >= 46 by 1200. Measured: 43.2 / 47.8 dB at 48 kHz (both
    // targets met); 36.0 / 46.3 at 16 kHz — the 600 ms half-time target
    // is missed (600 ms is only 37 blocks at block 256 / 16 kHz; the
    // early phase is excitation-limited, initial-uncertainty sweeps
    // saturate at ~37 dB) while BOTH 1200 ms values, requirement and
    // target, are met. The 16 kHz 600 ms assertion is a regression gate
    // at the measured value.
    TEST(ItuEcho, ConvergenceQuiet) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cc;
            cc.periods = 12;
            cc.shaped  = true;
            auto x     = make_css_at(cc, rs.fs);
            set_level_dbm0(x, -16.0);
            auto       rr = run_chain(c, path, rs.block, x);
            erl_reader erl(rr.echo, rr.out, rs.fs);
            EXPECT_GE(erl.by(0.6), expected({40.0, 34.0}, rs)) << "fs " << rs.fs;
            EXPECT_GE(erl.by(1.2), 46.0) << "fs " << rs.fs;
        }
    }

    // ITU_ConvergenceNoise (P1110/P1120 11.11.5): initial convergence in
    // background noise — synthetic driving noise at -30 dBm0(A), the
    // automotive condition class this clause is defined for. Reference =
    // idle transmitted noise from the pre-roll, SAME meter statistic.
    // Mask: <= ref+10 from 100 ms, <= ref by 1500 ms (req) / 750 ms
    // (target). Measured (max over window, 48 kHz / 16 kHz):
    //   ref            -24.5 / -26.7
    //   onset 100-200   -30.6 / -31.6   (mask ref+10: met by ~16 dB)
    //   750-1000 ms     -28.6 / -33.2   (target <= ref: met)
    //   1500-2000 ms    -26.9 / -30.6   (req <= ref: met)
    // A quiet-noise variant (Hoth at -46 dBm0) meets the mask only from
    // ~600 ms: with echo 30 dB above BGN+10 at onset, the first mask
    // segment demands more switched loss than the A_H,S allowance —
    // recorded in the matrix as a scenario note, not asserted.
    TEST(ItuEcho, ConvergenceNoise) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            const size_t     pre  = static_cast<size_t>(2.0 * rs.fs);
            css_config       cc;
            cc.periods = 12;
            cc.shaped  = true;
            auto css   = make_css_at(cc, rs.fs);
            set_level_dbm0(css, -16.0);
            std::vector<double> x(pre, 0.0);
            x.insert(x.end(), css.begin(), css.end());
            auto noise = make_driving_noise(x.size(), 7, rs.fs);
            {
                a_weighting  aw(rs.fs);
                auto         na  = aw.apply(noise);
                const double cur = level_dbov(na.data(), na.size()) - k_dbov_per_dbm0;
                const double g   = std::pow(10.0, (-30.0 - cur) / 20.0);
                for (auto& v : noise) {
                    v *= g;
                }
            }
            auto rr = run_chain(c, path, rs.block, x, &noise);
            auto tr = level_trace_dbm0a(rr.out, rs.fs);
            auto max_in = [&](double t0, double t1) {
                double m = -1e9;
                for (size_t i = static_cast<size_t>(t0 * rs.fs);
                     i < std::min(tr.size(), static_cast<size_t>(t1 * rs.fs)); ++i) {
                    m = std::max(m, tr[i]);
                }
                return m;
            };
            const double ref = max_in(1.0, 2.0);
            EXPECT_LE(max_in(2.1, 2.2), ref + 10.0) << "fs " << rs.fs;  // mask from 100 ms
            EXPECT_LE(max_in(2.75, 3.0), ref) << "fs " << rs.fs;        // target: by 750 ms
            EXPECT_LE(max_in(3.5, 4.0), ref) << "fs " << rs.fs;         // requirement: by 1500 ms
        }
    }

    // ITU_TimeVariantPath (P1110 11.11.6/7, P1120 11.11.6): converged,
    // then the path grows a -30 dB moving reflection (15 rpm, +-27 cm —
    // the rotating-reflector analogue defined in the matrix). Criterion:
    // absolute echo < -52 dBm0(A) (P1120's form of P1110's "steady state
    // + 6 dB"), target < -58. Measured: -58.3 at 48 kHz (target met);
    // -55.5 at 16 kHz — requirement met with 3.5 dB margin, target
    // missed (the depth-vs-tracking trade pinned by ITU_DtEchoLoss's
    // transition choice); gate at -54.
    TEST(ItuEcho, TimeVariantPath) {
        for (const auto& rs : required_rates()) {
            compliance_chain c(chain_config(rs));
            const auto       base = compliance_path(room::cabin, rs);
            typename echo_sim<double>::config sc;
            sc.echo_path  = base;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto xc    = make_css_at(cc, rs.fs);
            set_level_dbm0(xc, -16.0);
            run_chain_on(sim, c, rs.block, xc);

            moving_reflector refl;
            refl.base     = base;
            refl.fs       = rs.fs;
            refl.tap_gain = 0.03;
            cc.periods    = 20;
            auto xm       = make_css_at(cc, rs.fs);
            set_level_dbm0(xm, -16.0);
            std::vector<double> out;
            std::vector<double> p2(base);
            for (size_t blk = 0; blk + 1 <= xm.size() / rs.block; ++blk) {
                refl.fill(static_cast<double>(blk * rs.block) / rs.fs, p2);
                sim.set_echo_path(p2.data(), p2.size());
                sim.step(&xm[blk * rs.block], static_cast<const double*>(nullptr), &c);
                const auto& e = sim.error_block();
                out.insert(out.end(), e.begin(), e.end());
            }
            const double lvl = max_level_dbm0a(out, rs.fs, out.size() / 3, out.size());
            EXPECT_LT(lvl, expected({-58.0, -54.0}, rs)) << "fs " << rs.fs;
        }
    }

} // namespace
