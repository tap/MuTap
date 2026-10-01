// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The safety layer's per-mic howl detector (mutap/howl_detector.h): the
// mechanics, on synthetic signals, in float and double, plus the float
// canary both emulated selections run. The acoustic claims (latency on
// loop-born howls, false trips per material, the faust-icc side by side)
// are host-only: tests/test_howl_detector_host.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/howl_detector.h"
#include "support/closed_loop.h"
#include "support/rooms.h"

namespace {

    using tap::mu::howl_detector;
    using tap::mu::howl_trigger;

    constexpr double k_fs    = 48000.0;
    constexpr size_t k_block = 64;

    /// Deterministic uniform noise in [-1, 1) (a 64-bit LCG: the same stream
    /// on every target).
    class lcg_noise {
      public:
        explicit lcg_noise(std::uint64_t seed)
            : m_state(seed) {}
        double next() {
            m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>(m_state >> 11) * 0x1.0p-52 - 1.0;
        }

      private:
        std::uint64_t m_state;
    };

    /// A tone at `hz` whose level grows at `db_per_s` from `start_db` (re a
    /// unit-amplitude sine), over white noise at `noise_rms`, from n = 0.
    /// Generated in double with a rotator (no per-sample libm call).
    template <typename Sample>
    std::vector<Sample> growing_tone(double hz, double start_db, double db_per_s, double noise_rms, size_t n,
                                     std::uint64_t seed) {
        std::vector<Sample> x(n);
        lcg_noise           gen(seed);
        const double        step = 2.0 * std::numbers::pi * hz / k_fs;
        const double        c    = std::cos(step);
        const double        s    = std::sin(step);
        const double        g    = std::pow(10.0, db_per_s / 20.0 / k_fs);
        double              re   = std::pow(10.0, start_db / 20.0);
        double              im   = 0.0;
        for (size_t i = 0; i < n; ++i) {
            x[i]            = static_cast<Sample>(im + noise_rms * std::sqrt(3.0) * gen.next());
            const double r2 = (re * c - im * s) * g;
            const double i2 = (re * s + im * c) * g;
            re              = r2;
            im              = i2;
        }
        return x;
    }

    template <typename Sample>
    class howl_detector_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(howl_detector_test, sample_types);

} // namespace

namespace {

    template <typename Sample>
    using det = howl_detector<Sample>;

    template <typename Sample>
    typename det<Sample>::config default_config() {
        return typename det<Sample>::config{};
    }

    /// Feed x block by block; return the first block whose verdict fired
    /// (-1: none) and the trigger of that block.
    template <typename Sample>
    std::pair<long, howl_trigger> first_verdict(det<Sample>& d, const std::vector<Sample>& x) {
        for (size_t i = 0; i + k_block <= x.size(); i += k_block) {
            d.process_block(&x[i], k_block);
            if (d.verdict()) {
                return {static_cast<long>(i / k_block), d.trigger()};
            }
        }
        return {-1L, howl_trigger::none};
    }

    // The bank's geometry: log-spaced centres from f_lo_hz to f_hi_hz, as
    // faust-icc's logBandFreq.
    TYPED_TEST(howl_detector_test, BandsAreLogSpacedOverTheRange) {
        det<TypeParam> d(default_config<TypeParam>());
        ASSERT_EQ(d.bands(), 32U);
        EXPECT_NEAR(static_cast<double>(d.band_hz(0)), 150.0, 1e-3);
        EXPECT_NEAR(static_cast<double>(d.band_hz(31)), 16000.0, 1e-2);
        const double step = std::pow(16000.0 / 150.0, 1.0 / 31.0);
        for (size_t b = 1; b < 32; ++b) {
            EXPECT_NEAR(static_cast<double>(d.band_hz(b) / d.band_hz(b - 1)), step, 1e-5) << b;
        }
    }

    // A steady tone is prominent but does not grow: no verdict on any path in
    // 10 s (a 1 kHz tone at -20 dB re 1 over white noise at -60 dB). Its
    // prominence is what a pure line reaches in this bank: 20.05 dB (float
    // and double), the 2nd-order skirts of the other 31 bands setting the
    // mean it is measured over (1 kHz sits between the 942 and 1085 Hz
    // bands).
    TYPED_TEST(howl_detector_test, SteadyToneIsProminentButNotAHowl) {
        det<TypeParam> d(default_config<TypeParam>());
        const auto     x        = growing_tone<TypeParam>(1000.0, -20.0, 0.0, 1e-3, 480000, 11);
        TypeParam      max_prom = 0;
        TypeParam      max_rise = 0;
        bool           any      = false;
        for (size_t i = 0; i < x.size(); i += k_block) {
            d.process_block(&x[i], k_block);
            max_prom = std::max(max_prom, d.prominence_db());
            max_rise = std::max(max_rise, d.rise_db());
            any      = any || d.verdict();
        }
        std::printf("steady tone: max prominence %.2f dB, max rise %.3f dB, peak %.1f Hz\n",
                    static_cast<double>(max_prom), static_cast<double>(max_rise), static_cast<double>(d.peak_hz()));
        EXPECT_FALSE(any);
        EXPECT_GT(max_prom, TypeParam(19)); // measured 20.05
        EXPECT_LT(max_rise, d.cfg().rise_db);
    }

    // The growth path: a tone growing at a steady rate under white noise
    // (-30 dB re 1), from -50 dB, is caught by the growth path alone (level
    // catch off), sooner the faster it grows. At the default 10 ms loop
    // period, 60 dB/s is 0.6 dB per pass (twice growth_db_per_pass); a pure
    // tone's prominence tops out at 20.5 dB in this bank, so it qualifies
    // (P = 18) only from about 10 dB over the noise.
    TYPED_TEST(howl_detector_test, GrowingToneTripsOnTheGrowthPath) {
        struct row {
            double db_per_s;
            long   max_block; // measured + margin
        };
        // measured (float and double): blocks 673 / 403 / 72 (897.3 / 537.3 /
        // 96.0 ms), each with the line at 914.4 Hz; limits ~10 % over
        const row rows[] = {{60.0, 740}, {100.0, 445}, {600.0, 80}};
        for (const row& r : rows) {
            auto cfg        = default_config<TypeParam>();
            cfg.level_catch = false;
            det<TypeParam> d(cfg);
            const auto     x            = growing_tone<TypeParam>(1006.84, -50.0, r.db_per_s, 0.0316, 48000 * 6, 1);
            const auto [first, trigger] = first_verdict(d, x);
            std::printf("growing tone at %.0f dB/s: growth verdict at block %ld (%.1f ms), line %.1f Hz\n", r.db_per_s,
                        first, static_cast<double>(first) * 64.0 / 48.0, static_cast<double>(d.line_hz()));
            ASSERT_GE(first, 0) << r.db_per_s;
            EXPECT_EQ(trigger, howl_trigger::growth) << r.db_per_s;
            EXPECT_LE(first, r.max_block) << r.db_per_s;
        }
    }

    // The ceiling: a block at ceiling_db re 1 trips it whatever its
    // spectrum (white noise at ceiling + 1 dB, after 1 s at -40 dB).
    TYPED_TEST(howl_detector_test, CeilingTripsOnLevelAlone) {
        det<TypeParam> d(default_config<TypeParam>());
        const double   up = std::pow(10.0, (static_cast<double>(d.cfg().ceiling_db) + 1.0) / 20.0);
        auto           x  = growing_tone<TypeParam>(1000.0, -200.0, 0.0, 0.01, 96000, 2);
        for (size_t i = 48000; i < x.size(); ++i) {
            x[i] = static_cast<TypeParam>(static_cast<double>(x[i]) * up / 0.01);
        }
        const auto [first, trigger] = first_verdict(d, x);
        EXPECT_EQ(first, 750);
        EXPECT_EQ(trigger, howl_trigger::ceiling);
        EXPECT_TRUE(d.tripped());
    }

    // A trip holds hold_s after its cause, and confidence() follows the
    // verdict up over hold_s and down over 4 hold_s.
    TYPED_TEST(howl_detector_test, TripHoldsForHoldSeconds) {
        det<TypeParam> d(default_config<TypeParam>());
        const double   up = std::pow(10.0, (static_cast<double>(d.cfg().ceiling_db) + 3.0) / 20.0);
        auto           x  = growing_tone<TypeParam>(1000.0, -200.0, 0.0, 0.01, 48000, 4);
        for (size_t i = 0; i < 6400; ++i) {
            x[i] = static_cast<TypeParam>(static_cast<double>(x[i]) * up / 0.01); // 100 loud blocks
        }
        const auto hold         = static_cast<long>(std::lround(static_cast<double>(d.cfg().hold_s) * 750.0));
        long       last_tripped = -1;
        TypeParam  conf_at_end  = 0;
        for (size_t i = 0; i < x.size(); i += k_block) {
            d.process_block(&x[i], k_block);
            if (d.tripped()) {
                last_tripped = static_cast<long>(i / k_block);
            }
            if (i / k_block == 99) {
                conf_at_end = d.confidence();
            }
        }
        EXPECT_EQ(last_tripped, 99 + hold);
        // 100 blocks of verdict through a 0.2 s attack: 1 - exp(-100 / 150)
        EXPECT_NEAR(static_cast<double>(conf_at_end), 1.0 - std::exp(-100.0 / 150.0), 1e-3);
        EXPECT_LT(d.confidence(), conf_at_end);
    }

    // Harmonic readouts: a voiced note (the harness's 300 Hz impulse train
    // through its AR(4)) carries partners at 2f / 3f; a pure tone's partner
    // bands hold only the bank's skirts. Measured (float and double): voiced
    // -8.16 dB (peak 1436.8 Hz), tone -21.29 dB (peak 1063.1 Hz); f/2, f/3:
    // -11.55 and -19.82 dB. A readout, not a gate (see the header).
    TYPED_TEST(howl_detector_test, HarmonicReadoutsSeparateANoteFromATone) {
        const auto     voiced = mutap_test::voiced_near_end<TypeParam>(48000 * 2, 1, 160);
        const auto     tone   = growing_tone<TypeParam>(1006.84, -10.0, 0.0, 1e-4, 48000 * 2, 1);
        det<TypeParam> dv(default_config<TypeParam>());
        det<TypeParam> dt(default_config<TypeParam>());
        dv.process_block(voiced.data(), voiced.size());
        dt.process_block(tone.data(), tone.size());
        std::printf("harmonic ratio: voiced %.2f dB (peak %.1f Hz), tone %.2f dB (peak %.1f Hz); "
                    "subharmonic: voiced %.2f, tone %.2f dB\n",
                    static_cast<double>(dv.harmonic_ratio_db()), static_cast<double>(dv.peak_hz()),
                    static_cast<double>(dt.harmonic_ratio_db()), static_cast<double>(dt.peak_hz()),
                    static_cast<double>(dv.subharmonic_ratio_db()), static_cast<double>(dt.subharmonic_ratio_db()));
        EXPECT_GT(dv.harmonic_ratio_db(), dt.harmonic_ratio_db() + 6) << "measured 13.13 dB apart";
    }

    // The detector ticks every block_size samples of input whatever the
    // caller's partition: feeding the same signal in pieces of 1, 7 and 448
    // samples leaves every readout identical to feeding whole blocks,
    // compared after each multiple of 448 samples (7 blocks, where every
    // partition has just completed a tick).
    TYPED_TEST(howl_detector_test, BlockPartitionDoesNotChangeTheReadouts) {
        const auto x = growing_tone<TypeParam>(1000.0, -40.0, 60.0, 0.05, 48000 * 3, 7);
        struct snapshot {
            TypeParam prom, growth, rise, level, power, harm;
            bool      tripped;
            bool      operator==(const snapshot&) const = default;
        };
        auto run = [&](size_t piece) {
            det<TypeParam>        d(default_config<TypeParam>());
            std::vector<snapshot> out;
            size_t                done = 0;
            while (done < x.size()) {
                const size_t n = std::min(piece, x.size() - done);
                d.process_block(&x[done], n);
                done += n;
                if (done % 448 == 0) {
                    out.push_back({d.prominence_db(), d.growth_db_per_s(), d.rise_db(), d.level_db(), d.power_db(),
                                   d.harmonic_ratio_db(), d.tripped()});
                }
            }
            return out;
        };
        const auto ref = run(64);
        ASSERT_EQ(ref.size(), x.size() / 448);
        for (const size_t piece : {size_t{1}, size_t{7}, size_t{448}}) {
            EXPECT_TRUE(run(piece) == ref) << "piece " << piece;
        }
    }

    // Digital silence after a signal: the resonators' bias and the
    // envelopes' floor keep every state normal (a denormal envelope would
    // read below -400 dB here: the floor is 1e-20), and every readout stays
    // finite.
    TYPED_TEST(howl_detector_test, SilenceAfterSignalStaysNormal) {
        det<TypeParam> d(default_config<TypeParam>());
        const auto     x = growing_tone<TypeParam>(1000.0, -10.0, 0.0, 0.1, 48000, 3);
        d.process_block(x.data(), x.size());
        const std::vector<TypeParam> zeros(48000, TypeParam(0));
        for (int s = 0; s < 10; ++s) {
            d.process_block(zeros.data(), zeros.size());
        }
        for (size_t b = 0; b < d.bands(); ++b) {
            const TypeParam l = d.band_level_db(b);
            EXPECT_TRUE(std::isfinite(l)) << b;
            EXPECT_GE(l, TypeParam(-400.5)) << b;
        }
        EXPECT_TRUE(std::isfinite(d.prominence_db()));
        EXPECT_TRUE(std::isfinite(d.growth_db_per_s()));
        EXPECT_TRUE(std::isfinite(d.level_db()));
        EXPECT_TRUE(std::isfinite(d.power_db()));
        EXPECT_FALSE(d.tripped());
    }

    // reset() returns the detector to its constructed state: the same input
    // afterwards gives the same readouts.
    TYPED_TEST(howl_detector_test, ResetRestoresTheConstructedState) {
        const auto     x = growing_tone<TypeParam>(1500.0, -40.0, 300.0, 0.05, 48000, 5);
        det<TypeParam> fresh(default_config<TypeParam>());
        det<TypeParam> used(default_config<TypeParam>());
        used.process_block(x.data(), x.size());
        used.reset();
        EXPECT_FALSE(used.tripped());
        EXPECT_EQ(used.rise_db(), TypeParam(0));
        fresh.process_block(x.data(), x.size());
        used.process_block(x.data(), x.size());
        EXPECT_EQ(used.prominence_db(), fresh.prominence_db());
        EXPECT_EQ(used.rise_db(), fresh.rise_db());
        EXPECT_EQ(used.level_db(), fresh.level_db());
        EXPECT_EQ(used.tripped(), fresh.tripped());
    }

    // CANARY (both emulated selections; one seed, checking that the target's
    // float arithmetic tracks the host's double): a loop-born howl in a
    // band-limited 256-tap random_decaying_rir room (seed 7) at S1, the dry
    // loop 1 dB over its exact_msg_db (-6.1224 dB, measured on the host: the
    // 2^20-point analysis does not fit the M55's heap, so the gain is written
    // down), the voiced near end (seed 1). The loop runs in double; the float
    // and double detectors see the same residual. Measured on the host: the
    // 40 dB rule at block 453, the growth path at block 396 in both
    // precisions (76 ms first; the line at 1235.9 Hz), the integrated rises
    // within 0.0000 dB (printed to 4 places).
    TEST(HowlDetectorCrossPrecision, FloatTracksDoubleOnALoopBornHowl) {
        const auto path = mutap_test::band_limited(mutap_test::random_decaying_rir<double>(256, 7));
        mutap_test::closed_loop_sim<double>::config lc;
        lc.feedback_path   = path;
        lc.block_size      = k_block;
        lc.forward_delay   = 480;
        lc.forward_gain_db = -6.1224 + 1.0;
        mutap_test::closed_loop_sim<double> sim(lc);
        const auto                          v = mutap_test::voiced_near_end<double>(k_block * 1000, 1, 160);
        const howl_detector<double>::config cd;
        const howl_detector<float>::config  cf;
        howl_detector<double>               dd(cd);
        howl_detector<float>                df(cf);
        std::vector<float>                  ef(k_block);
        long                                rule          = -1;
        long                                gd            = -1;
        long                                gf            = -1;
        double                              max_rise_diff = 0.0;
        for (size_t b = 0; b < 1000 && (rule < 0 || gd < 0 || gf < 0); ++b) {
            const double r = sim.step(&v[b * k_block], static_cast<tap::mu::partitioned_fdaf<double>*>(nullptr));
            const auto&  e = sim.error_block();
            dd.process_block(e.data(), k_block);
            for (size_t i = 0; i < k_block; ++i) {
                ef[i] = static_cast<float>(e[i]);
            }
            df.process_block(ef.data(), k_block);
            max_rise_diff = std::max(max_rise_diff, std::abs(static_cast<double>(df.rise_db()) - dd.rise_db()));
            if (rule < 0 && r >= 100.0) {
                rule = static_cast<long>(b);
            }
            if (gd < 0 && dd.rise_db() >= cd.rise_db) {
                gd = static_cast<long>(b);
            }
            if (gf < 0 && df.rise_db() >= cf.rise_db) {
                gf = static_cast<long>(b);
            }
        }
        std::printf("canary: 40 dB rule at block %ld, growth path at block %ld (double) / %ld (float), "
                    "max |rise float - double| %.4f dB\n",
                    rule, gd, gf, max_rise_diff);
        ASSERT_GE(rule, 0);
        ASSERT_GE(gd, 0);
        ASSERT_GE(gf, 0);
        EXPECT_LT(gd, rule) << "measured 396 against 453";
        EXPECT_LT(gf, rule) << "measured 396 against 453";
        EXPECT_LE(std::abs(gf - gd), 2) << "measured equal";
        EXPECT_LT(max_rise_diff, 0.5) << "measured 0.0000 dB";
    }

    TEST(HowlDetectorConfigValidation, RejectsBadConfigs) {
        using cfg_t     = howl_detector<float>::config;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        auto        bad = [](auto mutate) {
            cfg_t c;
            mutate(c);
            return c;
        };
        EXPECT_NO_THROW(howl_detector<float>(cfg_t{}));
        EXPECT_NO_THROW(howl_detector<double>(howl_detector<double>::config{}));
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.sample_rate = 0.0; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.sample_rate = 1e300; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.block_size = 0; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.bands = 2; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.f_lo_hz = 0.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.f_hi_hz = 100.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.f_hi_hz = 24000.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([&](cfg_t& c) { c.q = nan; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.attack_s = 0.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.release_s = -1.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.loop_period_s = 0.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.program_s = 0.0F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.hold_s = -0.1F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([](cfg_t& c) { c.growth_window_s = 0.003F; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([&](cfg_t& c) { c.prominence_db = inf; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([&](cfg_t& c) { c.rise_db = nan; })), std::invalid_argument);
        EXPECT_THROW(howl_detector<float>(bad([&](cfg_t& c) { c.ceiling_db = nan; })), std::invalid_argument);
    }

    TEST(HowlDetectorRtContract, PostConstructionEntryPointsAreNoexcept) {
        using df = howl_detector<float>;
        using dd = howl_detector<double>;
        static_assert(noexcept(std::declval<df&>().process_block(nullptr, 0)));
        static_assert(noexcept(std::declval<df&>().reset()));
        static_assert(noexcept(std::declval<const df&>().tripped()));
        static_assert(noexcept(std::declval<const df&>().confidence()));
        static_assert(noexcept(std::declval<const df&>().band_level_db(0)));
        static_assert(noexcept(std::declval<const df&>().harmonic_ratio_db()));
        static_assert(noexcept(std::declval<dd&>().process_block(nullptr, 0)));
        static_assert(noexcept(std::declval<dd&>().reset()));
        static_assert(noexcept(std::declval<const dd&>().growth_db_per_pass()));
        SUCCEED();
    }

} // namespace
