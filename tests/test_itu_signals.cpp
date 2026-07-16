// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Validation of the ITU signal layer (docs/itu-compliance.md, Stage 1):
// the generated signals must match the properties P.501 publishes and
// the instruments must read known signals correctly, or every number the
// compliance suite later produces is untrustworthy. Thresholds follow
// the house workflow — measured first, asserted with margin:
//
//   CSS ST period 15435 samples exact; voiced/PN RMS ratio 1.0000/1.0003;
//   PN crest 10.97 dB (spec 11 +- 1); PN bin-power stddev 0.000 dB;
//   level calibration error 0.00 dB; DT noise crest 12.09 (spec 12 +- 1);
//   shaped-PN crest 9.82 dB; A-weighting 100 Hz -19.18 / 1 kHz 0.00 /
//   10 kHz -1.76 dB (analog nominals -19.1 / 0 / -2.5; the 10 kHz point
//   documents the prewarped-bilinear residual, IEC class-1 tolerance is
//   +2.6/-3.6 dB there); NB band-limit 0.0 dB at 3.4 kHz, -85.2 dB at
//   4 kHz, -106.5 at 5 kHz; AM-FM comb separation 94.3 dB with the
//   spec's exact band edges (guard 0 — an 8 Hz guard collapses it to
//   19 dB by reaching across the plans' 10 Hz interleave gaps); Hoth
//   octave decline 100-200 -> 4k-8k measured 18.1 dB; driving-noise LF
//   dominance 27.6 dB; cabin fixture RT60 66.6 ms (G.167 car figure:
//   ~60 ms typical); P.56 meter on sparse bursts: activity 0.312 with
//   level delta +5.06 dB (= 10 log10(1/activity), exact), and on CSS
//   activity ~1 (the 101 ms pause is inside P.56's 200 ms hangover —
//   the G.168 active-part corrections are the separate arithmetic
//   constants +1.49/+1.66 dB, reproduced by our exact segment sizes).

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_cabin.h"
#include "support/itu_levels.h"
#include "support/itu_signals.h"

namespace {

    using namespace mutap_test;

    TEST(ItuCss, SingleTalkStructureIsSpecExact) {
        itu::css_config cfg;
        cfg.periods    = 4;
        const auto css = itu::make_css(cfg);
        ASSERT_EQ(css.size(), 4U * 15435U) << "350 ms at 44.1 kHz, sample-exact";

        // Segment levels: voiced and PN parts RMS-matched (P.501 requires
        // equal levels), pause silent.
        const double vr = itu::rms_of(css.data(), 2144);
        const double pr = itu::rms_of(css.data() + 2144, 8820);
        EXPECT_NEAR(20.0 * std::log10(vr / pr), 0.0, 0.05) << "measured 0.003 dB";
        EXPECT_EQ(itu::rms_of(css.data() + 10964, 4471), 0.0);

        // Polarity inversion between periods (offset-free long sequence).
        for (size_t i = 0; i < 2144; ++i) {
            ASSERT_EQ(css[i], -css[15435 + i]);
        }
    }

    TEST(ItuCss, PnSegmentCrestAndFlatness) {
        const auto css = itu::make_css({});
        EXPECT_NEAR(itu::crest_factor_db(css.data() + 2144, 8820), 11.0, 1.0)
            << "P.501 spec 11 +- 1 dB; measured 10.97";

        // Flat magnitude by construction: every FFT bin of the 8192-point
        // block has identical power (measured stddev 0.000 dB).
        mutap::real_fft     fft(8192);
        std::vector<double> b(css.begin() + 2144, css.begin() + 2144 + 8192);
        fft.forward_inplace(b.data());
        double sum  = 0.0;
        double sum2 = 0.0;
        int    n    = 0;
        for (size_t k = 16; k < 3700; ++k) {
            const double d = 10.0 * std::log10(b[2 * k] * b[2 * k] + b[2 * k + 1] * b[2 * k + 1]);
            sum += d;
            sum2 += d * d;
            ++n;
        }
        const double mean = sum / n;
        EXPECT_LT(std::sqrt(std::max(0.0, sum2 / n - mean * mean)), 0.1);
    }

    TEST(ItuCss, DoubleTalkStructureAndCrest) {
        itu::css_config cfg;
        cfg.kind       = itu::css_kind::double_talk;
        cfg.periods    = 4;
        const auto css = itu::make_css(cfg);
        ASSERT_EQ(css.size(), 4U * 17640U) << "400 ms at 44.1 kHz, sample-exact";
        EXPECT_NEAR(itu::crest_factor_db(css.data() + 3206, 8820), 12.0, 1.0)
            << "P.501 spec 12 +- 1 dB; measured 12.09";
    }

    TEST(ItuCss, LevelCalibrationIsExact) {
        auto css = itu::make_css({});
        itu::set_level_dbm0(css, -16.0);
        EXPECT_NEAR(itu::rms_to_dbm0(itu::rms_of(css.data(), css.size())), -16.0, 0.01);
        // G.168's active-part corrections fall out of the exact segment
        // sizes: 10 log10(15435/10964) and 10 log10(17640/12026).
        EXPECT_NEAR(10.0 * std::log10(15435.0 / 10964.0), 1.485, 0.01);
        EXPECT_NEAR(10.0 * std::log10(17640.0 / 12026.0), 1.664, 0.01);
    }

    TEST(ItuLevels, AWeightingSpotGains) {
        const itu::a_weighting aw(itu::k_fs);
        const auto             db = [&](double f) { return 20.0 * std::log10(aw.magnitude_at(f, itu::k_fs)); };
        EXPECT_NEAR(db(100.0), -19.1, 0.5) << "measured -19.18";
        EXPECT_NEAR(db(1000.0), 0.0, 0.05);
        // Prewarped-bilinear residual at 10 kHz: measured -1.76 vs the
        // analog -2.5; IEC 61672 class-1 tolerance there is +2.6/-3.6.
        EXPECT_NEAR(db(10000.0), -2.5, 2.0);
    }

    TEST(ItuSignals, NarrowbandLimiterMeetsMask) {
        auto probe = [&](double f) {
            std::vector<double> s(static_cast<size_t>(itu::k_fs));
            for (size_t i = 0; i < s.size(); ++i) {
                s[i] = std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / itu::k_fs);
            }
            const auto y = itu::band_limit(s, itu::bandwidth::narrowband);
            return 20.0
                   * std::log10(itu::rms_of(y.data() + 8000, y.size() - 16000)
                                / itu::rms_of(s.data() + 8000, s.size() - 16000));
        };
        EXPECT_GT(probe(3400.0), -1.0) << "passband (measured 0.0 dB)";
        EXPECT_LT(probe(4000.0), -70.0) << "P.501 Table 7-7 cutoff (measured -85.2 dB)";
        EXPECT_LT(probe(5000.0), -80.0) << "measured -106.5 dB";
    }

    // THE PROPERTY THE DOUBLE-TALK ECHO TESTS STAND ON: the send and
    // receive AM-FM plans are orthogonal under the spec's own comb
    // analysis. Measured separation 94.3 dB with exact band edges — the
    // measurable echo-loss floor is far beyond the >= 33 dB margin
    // targets. (Guard discipline matters: +8 Hz of guard collapses this
    // to 19 dB via the send-250/receive-270 adjacency.)
    TEST(ItuSignals, AmFmPlansAreOrthogonal) {
        const auto   sp    = itu::amfm_send_plan();
        const auto   rp    = itu::amfm_receive_plan();
        const auto   snd   = itu::make_amfm(sp, static_cast<size_t>(4 * itu::k_fs));
        const double own   = itu::comb_band_level_db(snd, sp);
        const double cross = itu::comb_band_level_db(snd, rp);
        EXPECT_GT(own - cross, 60.0) << "measured 94.3 dB";
    }

    TEST(ItuSignals, NoiseFieldsHaveTheirSpectra) {
        auto octave_db = [&](const std::vector<double>& x, double f0, double f1) {
            constexpr size_t    n_fft = 65536;
            mutap::real_fft     fft(n_fft);
            std::vector<double> b(x.begin(), x.begin() + n_fft);
            fft.forward_inplace(b.data());
            double acc = 0.0;
            for (size_t k = static_cast<size_t>(f0 * n_fft / itu::k_fs);
                 k < static_cast<size_t>(f1 * n_fft / itu::k_fs); ++k) {
                acc += b[2 * k] * b[2 * k] + b[2 * k + 1] * b[2 * k + 1];
            }
            return 10.0 * std::log10(acc);
        };
        const auto   hoth  = itu::make_hoth_noise(static_cast<size_t>(4 * itu::k_fs), 7);
        const double h_lo  = octave_db(hoth, 100.0, 200.0);
        const double h_mid = octave_db(hoth, 800.0, 1600.0);
        const double h_hi  = octave_db(hoth, 4000.0, 8000.0);
        EXPECT_GT(h_lo, h_mid);
        EXPECT_GT(h_mid, h_hi);
        EXPECT_NEAR(h_lo - h_hi, 18.1, 4.0) << "Hoth decline, measured 18.1 dB";

        const auto drv = itu::make_driving_noise(static_cast<size_t>(4 * itu::k_fs), 7);
        EXPECT_GT(octave_db(drv, 60.0, 120.0) - octave_db(drv, 4000.0, 8000.0), 20.0)
            << "LF dominance, measured 27.6 dB";
    }

    // The cabin fixture is the automotive echo path of the compliance
    // suite; G.167 5.2.3.1 gives the car figure as RT ~60 ms typical.
    TEST(ItuSignals, CabinFixtureReverbTime) {
        const float*        r = fixtures::k_rir_cabin;
        const size_t        n = fixtures::k_rir_cabin_taps;
        std::vector<double> edc(n);
        double              acc = 0.0;
        for (size_t i = n; i-- > 0;) {
            acc += static_cast<double>(r[i]) * static_cast<double>(r[i]);
            edc[i] = acc;
        }
        double t5  = 0.0;
        double t35 = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double d = 10.0 * std::log10(edc[i] / edc[0]);
            if (t5 == 0.0 && d <= -5.0) {
                t5 = static_cast<double>(i) / 48000.0;
            }
            if (t35 == 0.0 && d <= -35.0) {
                t35 = static_cast<double>(i) / 48000.0;
                break;
            }
        }
        const double rt60_ms = 2.0 * (t35 - t5) * 1000.0;
        EXPECT_GT(rt60_ms, 50.0) << "measured 66.6 ms";
        EXPECT_LT(rt60_ms, 80.0) << "measured 66.6 ms";
    }

    TEST(ItuLevels, ActiveSpeechLevelMeter) {
        // Sparse bursts: 0.5 s voiced + 2.0 s silence. P.56 activity =
        // (burst + 200 ms hangover + envelope tail) / period; measured
        // 0.312 with the level delta the definition demands.
        std::vector<double> x;
        for (int b = 0; b < 4; ++b) {
            for (size_t i = 0; i < static_cast<size_t>(0.5 * itu::k_fs); ++i) {
                x.push_back(static_cast<double>(itu::k_css_voiced_st[i % 134]) / 3000.0);
            }
            x.insert(x.end(), static_cast<size_t>(2.0 * itu::k_fs), 0.0);
        }
        double       act = 0.0;
        const double asl = itu::active_speech_level_dbm0(x, itu::k_fs, &act);
        const double tot = itu::rms_to_dbm0(itu::rms_of(x.data(), x.size()));
        EXPECT_NEAR(act, 0.312, 0.05);
        EXPECT_NEAR(asl - tot, 10.0 * std::log10(1.0 / act), 0.1) << "definitionally consistent";

        // On CSS the 101 ms pause sits inside the 200 ms hangover, so
        // activity is ~1 and ASL ~ total level (measured -15.93 at -16).
        auto css = itu::make_css({});
        itu::set_level_dbm0(css, -16.0);
        double       css_act = 0.0;
        const double css_asl = itu::active_speech_level_dbm0(css, itu::k_fs, &css_act);
        EXPECT_GT(css_act, 0.9);
        EXPECT_NEAR(css_asl, -16.0, 0.5) << "measured -15.93 dBm0";
    }

    TEST(ItuSignals, ActivationSequenceGeometry) {
        std::vector<size_t> starts;
        const auto          seq = itu::make_activation_sequence(5, -40.0, &starts);
        ASSERT_EQ(starts.size(), 5U);
        EXPECT_EQ(starts[1] - starts[0], static_cast<size_t>(itu::k_fs)) << "500 ms token + 500 ms pause";
        // +1 dB per step.
        const size_t tn = static_cast<size_t>(0.5 * itu::k_fs);
        const double l0 = 20.0 * std::log10(itu::rms_of(seq.data() + starts[0], tn));
        const double l4 = 20.0 * std::log10(itu::rms_of(seq.data() + starts[4], tn));
        EXPECT_NEAR(l4 - l0, 4.0, 0.01);
    }

    TEST(ItuSignals, MovingReflectorPathStaysSane) {
        itu::moving_reflector mr;
        mr.base.assign(256, 0.0);
        mr.base[32] = 1.0;
        std::vector<double> path;
        double              tap_min = 1e9;
        double              tap_max = -1e9;
        for (double t = 0.0; t < 4.0; t += 0.05) {
            mr.fill(t, path);
            ASSERT_EQ(path.size(), mr.base.size());
            EXPECT_EQ(path[32], 1.0) << "base path untouched";
            double extra = 0.0;
            for (size_t i = 0; i < path.size(); ++i) {
                if (i != 32) {
                    extra += path[i];
                }
            }
            tap_min = std::min(tap_min, extra);
            tap_max = std::max(tap_max, extra);
        }
        EXPECT_NEAR(tap_max, mr.tap_gain, 0.01) << "reflector reaches full strength";
        EXPECT_LT(tap_min, 0.01) << "and vanishes at the rotation nulls";
    }

} // namespace
