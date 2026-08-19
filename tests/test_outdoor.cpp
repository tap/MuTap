// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Outdoor close-range AEC scenario: fixture validation + the measured
// BASELINE of the certified compliance chain (tests/support/itu_chain.h
// geometry and preset, untouched) against the scenario the ITU battery
// does not cover — negative ERL, a distorting loudspeaker, wind, mic
// overload (tests/support/outdoor_scenario.h has the model). House
// workflow: every gate pins a measured value with margin; the measured
// number sits in a comment next to the assertion. Double golden model
// only at this stage — the float32 parity pass comes with the departure
// work, as it did for the compliance battery.
//
// Measured summary (far end shaped CSS at -10 dBm0; suppression = true
// echo suppression from 4 s; residual = A-weighted 35 ms max over the
// 6 s.. tail, dBm0(A)):
//
//   Linear ERL sweep              48 kHz                  16 kHz
//     erl   supp  resid  by1.2s   |  supp  resid  by1.2s
//       0   60.7  -65.3   56.2    |  53.7  -54.2   47.4
//     -10   59.0  -55.5   55.2    |  58.2  -52.2   44.6
//     -20   58.8  -46.5   51.1    |  58.6  -42.6   40.1
//     -30   60.2  -35.5   37.5    |  56.1  -29.9   31.9
//   -> The linear problem is EASY here (short rigid path, no tail):
//      suppression is flat in ERL and convergence holds to erl -30.
//      What negative ERL does is spend the budget: 58 dB of suppression
//      on a +20 dBm0 echo still leaves -35..-30 dBm0(A) in the send
//      path — above every compliance mask, without any nonlinearity.
//
//   Loudspeaker distortion (erl -20): raw fdkf supp / chain supp / resid
//     drive        48 kHz              16 kHz
//     linear   50.2 / 58.8 / -46.5 |  53.7 / 58.6 / -42.6
//     mild     25.6 / 29.8 / -18.2 |  25.9 / 28.1 / -17.4
//     moderate 15.6 / 19.3 / -10.3 |  15.9 / 17.5 /  -8.7
//     severe   10.2 / 20.6 / -11.4 |  10.6 / 12.1 /  -6.8
//   -> The headline: ~1 % THD (mild) costs ~25 dB of linear
//      cancellation, and the coherence suppressor adds only ~2..4 dB on
//      top — the residual's energy is at harmonics of the reference,
//      bands where the linear echo estimate has nothing to correlate
//      against. THIS is the outdoor close-range problem, exactly as
//      scoped: the multi-branch (nonlinear-basis) canceller and the
//      device-trained nn_suppressor are the two candidate responses,
//      and these rows are the numbers they must move.
//
//   Permanent double talk (erl -20, near end CSS-DT at -30 dBm0, i.e.
//   40 dB below the echo at the mic): supp / send delta (out - near, dB)
//     linear    39.0 / -1.4 (48k)   38.1 / -0.5 (16k)
//     moderate  19.2 / +20.1        17.6 / +20.1
//   -> Linear: echo pulled down TO the near end's level with the near
//      end intact — the Kalman core's Psi_s machinery survives the
//      always-double-talk regime without a DTD, as designed. With
//      distortion the send path is echo junk 20 dB ABOVE the talker.
//      (The delta metric includes the suppressor's action on the near
//      end itself, so it is a conservative read.)
//
//   Wind (erl -10 linear, wind at -25 dBm0): ERL by 1.2 s 31.1 (48k) /
//   33.5 (16k) vs 55.2/44.6 in quiet; steady suppression reads 25.1/22.3
//   dB — a floor, not the true value: the true-residual instrument
//   charges the chain's noise-side processing of the wind (comfort fill
//   delta) to the echo account. No divergence, adaptation slows as the
//   noise tracker absorbs the buffeting — the designed behavior.
//
//   Mic clipping (erl -20 linear, full-scale ADC, echo at +10 dBm0):
//   52 % of echo samples beyond full scale; suppression 26.6 (48k) /
//   24.5 (16k) vs 58.8/58.6 unclipped. A ~32 dB loss with no
//   nonlinearity in the speaker at all — the case for input gain
//   staging plus a clip guard (freeze adaptation on saturated blocks,
//   the narrowband-guard discipline) in the departure work.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "support/itu_chain.h"
#include "support/outdoor_scenario.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::outdoor;
    using mutap_test::itu::chain_config;
    using mutap_test::itu::max_level_dbm0a;
    using mutap_test::itu::rate_setup;
    using mutap_test::itu::required_rates;

    constexpr double k_far_level_dbm0 = -10.0; ///< scenario far-end operating level

    // Shaped single-talk CSS at the operating level, `seconds` long.
    std::vector<double> far_end(const rate_setup& rs, double seconds) {
        itu::css_config cc;
        cc.periods = static_cast<size_t>(seconds / 0.35);
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, rs.fs);
        itu::set_level_dbm0(x, k_far_level_dbm0);
        return x;
    }

    echo_sim<double> make_sim(const std::vector<double>& path, size_t block, double mic_clip = 0.0) {
        echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = block;
        sc.mic_clip   = mic_clip;
        return echo_sim<double>(sc);
    }

    size_t blocks_at(double seconds, const rate_setup& rs) {
        return static_cast<size_t>(seconds * rs.fs) / rs.block;
    }

    /// Gate table row: one value per (rate, sweep index).
    double at(const rate_setup& rs, std::initializer_list<double> at48, std::initializer_list<double> at16, size_t i) {
        return rs.fs == 48000.0 ? *(at48.begin() + static_cast<long>(i)) : *(at16.begin() + static_cast<long>(i));
    }

    // ------------------------------------------------------------- fixture

    TEST(OutdoorFixture, PathAnatomy) {
        for (const auto& rs : required_rates()) {
            const auto p = make_outdoor_path(rs.fs, rs.taps, 0.0);

            double total = 0.0;
            for (const double v : p) {
                total += v * v;
            }
            EXPECT_NEAR(total, 1.0, 1e-9) << "fs " << rs.fs;

            // All support inside ~7.8 ms (bulk 1 ms + coupling + ground
            // bounce at ~6.9 ms + spike half-width): outdoors has no tail.
            double late = 0.0;
            for (size_t i = static_cast<size_t>(0.0078 * rs.fs); i < p.size(); ++i) {
                late += p[i] * p[i];
            }
            EXPECT_LT(late, 1e-9) << "fs " << rs.fs;

            // The direct+structure region owns essentially all the energy;
            // the ground bounce is physically ~40 dB down (1/r).
            double early = 0.0;
            for (size_t i = 0; i < std::min(p.size(), static_cast<size_t>(0.004 * rs.fs)); ++i) {
                early += p[i] * p[i];
            }
            EXPECT_GT(early, 0.999) << "fs " << rs.fs;
        }
    }

    // ERL calibration: broadband echo-at-mic level = reference level -
    // erl_db, by construction. Measured -10.01 at erl 0 and +9.99 at
    // erl -20, both rates.
    TEST(OutdoorFixture, ErlCalibration) {
        for (const auto& rs : required_rates()) {
            for (const double erl : {0.0, -20.0}) {
                const auto                       p = make_outdoor_path(rs.fs, rs.taps, erl);
                std::mt19937                     gen(97);
                std::normal_distribution<double> dist(0.0, 1.0);
                std::vector<double>              x(static_cast<size_t>(3.0 * rs.fs));
                for (auto& v : x) {
                    v = dist(gen);
                }
                itu::set_level_dbm0(x, k_far_level_dbm0);
                auto   sim = make_sim(p, rs.block);
                auto   r   = run_outdoor(sim, static_cast<tap::mu::partitioned_fdkf<double>*>(nullptr), x, x, nullptr,
                                         blocks_at(0.5, rs));
                double sq  = 0.0;
                size_t n   = 0;
                for (size_t i = static_cast<size_t>(0.5 * rs.fs); i < r.echo.size(); ++i, ++n) {
                    sq += r.echo[i] * r.echo[i];
                }
                const double echo_dbm0 = itu::rms_to_dbm0(std::sqrt(sq / static_cast<double>(n)));
                EXPECT_NEAR(echo_dbm0, k_far_level_dbm0 - erl, 0.75) << "fs " << rs.fs << " erl " << erl;
            }
        }
    }

    // The severity presets' measured THD at the -10 dBm0 operating level
    // (1 kHz): mild -40.4 dB (~1 %), moderate -27.1 (~4.4 %), severe
    // -17.8 (~12.9 %). Rate-independent (memoryless model, on-bin sine).
    TEST(OutdoorFixture, SpeakerDriveThd) {
        for (const auto& rs : required_rates()) {
            EXPECT_NEAR(drive_thd_db(k_drive_mild, k_far_level_dbm0, rs.fs), -40.4, 1.0) << "fs " << rs.fs;
            EXPECT_NEAR(drive_thd_db(k_drive_moderate, k_far_level_dbm0, rs.fs), -27.1, 1.0) << "fs " << rs.fs;
            EXPECT_NEAR(drive_thd_db(k_drive_severe, k_far_level_dbm0, rs.fs), -17.8, 1.0) << "fs " << rs.fs;
        }
    }

    // Wind instrument: LF dominance (band 20..100 Hz over 1..4 kHz,
    // measured 49.2 dB at 48 kHz / 47.7 at 16 kHz), gust nonstationarity
    // (1 s RMS wander over 12 s, measured 22.2 / 9.9 dB — the two rates
    // draw different sequences from the same seed), determinism.
    TEST(OutdoorFixture, WindCharacter) {
        for (const auto& rs : required_rates()) {
            const size_t n = static_cast<size_t>(12.0 * rs.fs);
            auto         w = make_wind_noise(n, 7, rs.fs);
            itu::set_level_dbm0(w, -25.0);

            auto w2 = make_wind_noise(n, 7, rs.fs);
            itu::set_level_dbm0(w2, -25.0);
            EXPECT_EQ(w, w2);

            const size_t nfft = 8192;
            const auto   psd  = itu::welch_psd_db(w, nfft);
            auto         band = [&](double f0, double f1) {
                const size_t k0 = static_cast<size_t>(f0 * static_cast<double>(nfft) / rs.fs);
                const size_t k1 = std::min(psd.size() - 1, static_cast<size_t>(f1 * static_cast<double>(nfft) / rs.fs));
                double       acc = 0.0;
                for (size_t k = std::max<size_t>(1, k0); k < k1; ++k) {
                    acc += std::pow(10.0, psd[k] / 10.0);
                }
                return 10.0 * std::log10(acc / static_cast<double>(k1 - k0));
            };
            EXPECT_GT(band(20.0, 100.0) - band(1000.0, 4000.0), 40.0) << "fs " << rs.fs;

            const size_t        win = static_cast<size_t>(1.0 * rs.fs);
            std::vector<double> lv;
            for (size_t off = 0; off + win <= w.size(); off += win / 2) {
                double sq = 0.0;
                for (size_t i = 0; i < win; ++i) {
                    sq += w[off + i] * w[off + i];
                }
                lv.push_back(10.0 * std::log10(sq / static_cast<double>(win)));
            }
            const double wander = *std::max_element(lv.begin(), lv.end()) - *std::min_element(lv.begin(), lv.end());
            EXPECT_GT(wander, rs.fs == 48000.0 ? 18.0 : 7.5) << "fs " << rs.fs;
        }
    }

    // ------------------------------------------------------------ baseline

    // Negative-ERL sweep on the LINEAR path. Measured table in the file
    // banner; gates are measured-minus-margin (suppression, convergence)
    // and measured-plus-margin (residual level) so erosion fails, while
    // the departure work improving these passes untouched.
    TEST(OutdoorBaseline, LinearErlSweep) {
        const std::initializer_list<double> erls = {0.0, -10.0, -20.0, -30.0};
        size_t                              i    = 0;
        for (const auto& rs : required_rates()) {
            i = 0;
            for (const double erl : erls) {
                tap::mu::aec_chain<double> chain(chain_config<double>(rs));
                const auto                 p   = make_outdoor_path(rs.fs, rs.taps, erl);
                const auto                 x   = far_end(rs, 8.4);
                auto                       sim = make_sim(p, rs.block);
                auto                       r   = run_outdoor(sim, &chain, x, x, nullptr, blocks_at(4.0, rs));
                ASSERT_TRUE(r.finite) << "fs " << rs.fs << " erl " << erl;

                // Steady true suppression, measured 58.8..60.7 (48k) /
                // 53.7..58.6 (16k) — flat in ERL: the linear close-range
                // problem does not get harder as ERL goes negative.
                EXPECT_GE(r.suppression_db, at(rs, {57.0, 56.0, 55.0, 57.0}, {50.0, 55.0, 55.0, 53.0}, i))
                    << "fs " << rs.fs << " erl " << erl;

                // Absolute send residual, measured -65.3/-55.5/-46.5/-35.5
                // (48k), -54.2/-52.2/-42.6/-29.9 (16k): the budget problem
                // in one row — at erl -30 the residual clears every
                // conversational level despite ~58 dB of suppression.
                const double residual_tail =
                    max_level_dbm0a(r.residual, rs.fs, static_cast<size_t>(6.0 * rs.fs), r.residual.size());
                EXPECT_LE(residual_tail, at(rs, {-62.0, -52.0, -43.0, -32.0}, {-51.0, -49.0, -39.0, -26.0}, i))
                    << "fs " << rs.fs << " erl " << erl;

                // Convergence: best mic-vs-out inside the window before
                // 1.2 s (the P.1110 convergence clock), measured
                // 56.2/55.2/51.1/37.5 (48k), 47.4/44.6/40.1/31.9 (16k).
                std::vector<double>   mic(r.echo); // single talk: mic IS the echo
                const itu::erl_reader rd(mic, r.out, rs.fs);
                EXPECT_GE(rd.by(1.2), at(rs, {52.0, 51.0, 47.0, 33.0}, {43.0, 40.0, 36.0, 28.0}, i))
                    << "fs " << rs.fs << " erl " << erl;
                ++i;
            }
        }
    }

    // The transducer-distortion sweep at erl -20 — the headline table
    // (banner): mild (~1 % THD) costs the linear stage ~25 dB and the
    // coherence suppressor recovers only ~2..4 dB. Gates are lower
    // bounds so the multi-branch canceller / nn_suppressor work that
    // moves these rows up passes; erosion fails.
    TEST(OutdoorBaseline, NonlinearDrive) {
        const std::initializer_list<double> drives = {0.0, k_drive_mild, k_drive_moderate, k_drive_severe};
        size_t                              i      = 0;
        for (const auto& rs : required_rates()) {
            i = 0;
            for (const double drive : drives) {
                const auto    p = make_outdoor_path(rs.fs, rs.taps, -20.0);
                const auto    x = far_end(rs, 8.4);
                speaker_drive sp{drive};
                const auto    xd = sp.apply(x);

                tap::mu::partitioned_fdkf<double> raw(chain_config<double>(rs).canceller);
                auto                              sim_r = make_sim(p, rs.block);
                auto                              rr    = run_outdoor(sim_r, &raw, x, xd, nullptr, blocks_at(4.0, rs));

                tap::mu::aec_chain<double> chain(chain_config<double>(rs));
                auto                       sim_c = make_sim(p, rs.block);
                auto                       rc    = run_outdoor(sim_c, &chain, x, xd, nullptr, blocks_at(4.0, rs));
                ASSERT_TRUE(rr.finite && rc.finite) << "fs " << rs.fs << " drive " << drive;

                // Raw linear canceller, measured 50.2/25.6/15.6/10.2 (48k),
                // 53.7/25.9/15.9/10.6 (16k): the linear ceiling against a
                // distorting speaker.
                EXPECT_GE(rr.suppression_db, at(rs, {47.0, 22.0, 13.0, 8.0}, {50.0, 23.0, 13.0, 8.0}, i))
                    << "fs " << rs.fs << " drive " << drive;

                // Full chain, measured 58.8/29.8/19.3/20.6 (48k),
                // 58.6/28.1/17.5/12.1 (16k).
                EXPECT_GE(rc.suppression_db, at(rs, {55.0, 26.0, 16.0, 17.0}, {55.0, 25.0, 14.0, 9.0}, i))
                    << "fs " << rs.fs << " drive " << drive;

                // Send residual, measured -46.5/-18.2/-10.3/-11.4 (48k),
                // -42.6/-17.4/-8.7/-6.8 (16k) dBm0(A): at moderate drive
                // the send path carries conversation-level echo junk.
                const double residual_tail =
                    max_level_dbm0a(rc.residual, rs.fs, static_cast<size_t>(6.0 * rs.fs), rc.residual.size());
                EXPECT_LE(residual_tail, at(rs, {-43.0, -15.0, -7.0, -8.0}, {-39.0, -14.0, -5.0, -3.5}, i))
                    << "fs " << rs.fs << " drive " << drive;
                ++i;
            }
        }
    }

    // The permanent-double-talk regime: near end 40 dB BELOW the echo at
    // the mic, for the whole run. Linear: the Kalman core's model-based
    // robustness holds without any DTD (send delta ~0: echo brought down
    // to the talker's level, talker intact). Distorted: the send path is
    // residual echo 20 dB above the talker — the duplex cost of the
    // uncompensated nonlinearity. The delta metric charges the
    // suppressor's action on the near end itself, so it reads
    // conservative.
    TEST(OutdoorBaseline, PermanentDoubleTalk) {
        const std::initializer_list<double> drives = {0.0, k_drive_moderate};
        size_t                              i      = 0;
        for (const auto& rs : required_rates()) {
            i = 0;
            for (const double drive : drives) {
                const auto    p = make_outdoor_path(rs.fs, rs.taps, -20.0);
                const auto    x = far_end(rs, 8.4);
                speaker_drive sp{drive};
                const auto    xd = sp.apply(x);

                itu::css_config nc;
                nc.kind    = itu::css_kind::double_talk;
                nc.periods = static_cast<size_t>(8.4 / 0.4) + 1;
                nc.seed    = 977;
                auto v     = itu::make_css_at(nc, rs.fs);
                itu::set_level_dbm0(v, -30.0);
                v.resize(x.size(), 0.0);

                tap::mu::aec_chain<double> chain(chain_config<double>(rs));
                auto                       sim = make_sim(p, rs.block);
                auto                       r   = run_outdoor(sim, &chain, x, xd, &v, blocks_at(4.0, rs));
                ASSERT_TRUE(r.finite) << "fs " << rs.fs << " drive " << drive;

                // Suppression under permanent DT, measured 39.0/19.2 (48k),
                // 38.1/17.6 (16k).
                EXPECT_GE(r.suppression_db, at(rs, {36.0, 16.0}, {35.0, 14.5}, i))
                    << "fs " << rs.fs << " drive " << drive;

                // Send delta out-vs-near over the settled tail, measured
                // -1.4/+20.1 (48k), -0.5/+20.1 (16k) dB.
                const size_t t0       = static_cast<size_t>(6.0 * rs.fs);
                const double out_lvl  = max_level_dbm0a(r.out, rs.fs, t0, r.out.size());
                const double near_lvl = max_level_dbm0a(v, rs.fs, t0, r.out.size());
                EXPECT_LE(out_lvl - near_lvl, at(rs, {2.0, 23.0}, {2.5, 23.0}, i))
                    << "fs " << rs.fs << " drive " << drive;
                ++i;
            }
        }
    }

    // Wind at the mic while converging (erl -10, linear speaker): the
    // noise tracker absorbs the buffeting and adaptation slows but never
    // diverges. ERL by 1.2 s measured 31.1 (48k) / 33.5 (16k), vs
    // 55.2/44.6 in quiet. Steady suppression reads 25.1/22.3 dB — a
    // FLOOR, not the true value: the true-residual instrument charges
    // the chain's noise-side processing of the wind (comfort fill delta)
    // to the echo account.
    TEST(OutdoorBaseline, WindAdaptation) {
        for (const auto& rs : required_rates()) {
            const auto p = make_outdoor_path(rs.fs, rs.taps, -10.0);
            const auto x = far_end(rs, 8.4);
            auto       w = make_wind_noise(x.size(), 7, rs.fs);
            itu::set_level_dbm0(w, -25.0);

            tap::mu::aec_chain<double> chain(chain_config<double>(rs));
            auto                       sim = make_sim(p, rs.block);
            auto                       r   = run_outdoor(sim, &chain, x, x, &w, blocks_at(4.0, rs));
            ASSERT_TRUE(r.finite) << "fs " << rs.fs;

            std::vector<double> mic(r.echo.size());
            for (size_t i = 0; i < mic.size(); ++i) {
                mic[i] = r.echo[i] + w[i];
            }
            const itu::erl_reader rd(mic, r.out, rs.fs);
            EXPECT_GE(rd.by(1.2), rs.fs == 48000.0 ? 27.0 : 29.0) << "fs " << rs.fs;
            EXPECT_GE(r.suppression_db, rs.fs == 48000.0 ? 21.0 : 18.0) << "fs " << rs.fs;
        }
    }

    // Mic overload (erl -20 linear, full-scale ADC): the echo rides at
    // +10 dBm0, so ~52 % of echo samples land beyond full scale (the
    // fraction is pinned to keep the operating point honest), and
    // suppression collapses 58.8 -> 26.6 (48k) / 58.6 -> 24.5 (16k) with
    // NO speaker nonlinearity at all. The case for gain staging plus a
    // clip guard in the departure work.
    TEST(OutdoorBaseline, MicClipping) {
        for (const auto& rs : required_rates()) {
            const auto p = make_outdoor_path(rs.fs, rs.taps, -20.0);
            const auto x = far_end(rs, 8.4);

            tap::mu::aec_chain<double> chain(chain_config<double>(rs));
            auto                       sim = make_sim(p, rs.block, 1.0); // full-scale ADC
            auto                       r   = run_outdoor(sim, &chain, x, x, nullptr, blocks_at(4.0, rs));
            ASSERT_TRUE(r.finite) << "fs " << rs.fs;

            double clipped = 0.0;
            for (const double d : r.echo) {
                if (std::abs(d) > 1.0) {
                    clipped += 1.0;
                }
            }
            const double pct = 100.0 * clipped / static_cast<double>(r.echo.size());
            EXPECT_GT(pct, 45.0) << "fs " << rs.fs; // measured 52.2 / 52.9
            EXPECT_LT(pct, 60.0) << "fs " << rs.fs;
            EXPECT_GE(r.suppression_db, rs.fs == 48000.0 ? 23.0 : 21.0) << "fs " << rs.fs;
        }
    }

} // namespace
