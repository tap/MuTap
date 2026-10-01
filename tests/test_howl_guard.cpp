// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The safety layer's guard (mutap/howl_guard.h) and its afc_chain hook: the
// state machine, the ramps, the strikes and latch, the attribution and the
// bus stage, driven tick by tick with synthetic residuals and synthetic
// verdicts, in float and double. Every expectation here is a transition
// time in ticks or an exact gain the policy defines (a tick is one 64-sample
// block at 48 kHz: 750 ticks per second), not a measured number; the live
// claims are host-only: tests/test_howl_guard_host.cpp. Both emulated
// selections run the float suite (howl_guard_test/0) and the float
// validation, contract and chain-hook tests.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/afc_chain.h"
#include "mutap/fd_kalman.h"
#include "mutap/howl_guard.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"

namespace {

    using tap::mu::guard_policy;
    using tap::mu::guard_state;
    using tap::mu::howl_guard;

    constexpr size_t k_block = 64;
    constexpr double k_fs    = 48000.0;
    // Policy times in ticks at the defaults (750 ticks per second).
    constexpr long k_release = 1125; ///< release_hold_s 1.5
    constexpr long k_trip    = 225;  ///< trip_hold_s 0.3
    constexpr long k_quiet   = 750;  ///< quiet_s 1.0
    constexpr long k_timeout = 7500; ///< arming_timeout_s 10
    constexpr long k_rearm   = 3750; ///< rearm_timeout_s 5
    constexpr long k_ramp_up = 150;  ///< release_ramp_ms 200 = 9600 samples
    constexpr long k_probate = 7500; ///< probation_s 10
    constexpr long k_window  = 42;   ///< attack_ms + 50 ms = 41.25 ticks, rounded up

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

    /// What a mic's residual carries on one tick.
    enum class sig {
        quiet, ///< white noise at -60 dB re 1
        loud,  ///< a 1 kHz tone at amplitude 2 (+3 dB re 1: over the default -6 dB ceiling)
        tone   ///< the same tone at `tone_amp`
    };

    /// Drives a guard tick by tick: per mic a residual block and a verdict.
    template <typename Sample>
    class driver {
      public:
        explicit driver(const typename howl_guard<Sample>::config& cfg)
            : g(cfg)
            , m_noise(cfg.microphones, lcg_noise(7))
            , m_phase(cfg.microphones, 0.0)
            , m_x(k_block) {
            for (size_t m = 0; m < cfg.microphones; ++m) {
                m_noise[m] = lcg_noise(7 + m);
            }
        }

        /// One tick for every mic: `kinds[m]`, `ok[m]`.
        void tick(const std::vector<sig>& kinds, const std::vector<bool>& ok, double a_override = -1.0) {
            for (size_t m = 0; m < g.microphones(); ++m) {
                fill(m, kinds[m]);
                const Sample a = a_override >= 0.0 ? static_cast<Sample>(a_override)
                                                   : (ok[m] ? Sample(1e-3) : Sample(0.1)); // -30 / -10 dB
                const Sample d = ok[m] ? Sample(0.5) : Sample(1);                          // -3.01 / 0 dB
                g.analyze(m, m_x.data(), a, d);
            }
            g.update();
            ++ticks;
        }
        /// One tick on a single-mic guard.
        void tick1(sig kind, bool ok) { tick({kind}, {ok}); }
        /// One quiet tick on a single-mic guard with D and A' given in dB.
        void tick_db(double d_db, double a_db) {
            fill(0, sig::quiet);
            g.analyze(0, m_x.data(), static_cast<Sample>(std::pow(10.0, a_db / 10.0)),
                      static_cast<Sample>(std::pow(10.0, d_db / 10.0)));
            g.update();
            ++ticks;
        }
        /// n ticks of the same input on a single-mic guard; returns the
        /// first tick (counted from this call) at which the state became
        /// `until`, or -1.
        long run1(long n, sig kind, bool ok, std::optional<guard_state> until = std::nullopt) {
            for (long i = 0; i < n; ++i) {
                tick1(kind, ok);
                if (until.has_value() && g.state(0) == *until) {
                    return i;
                }
            }
            return -1;
        }

        howl_guard<Sample> g;
        long               ticks    = 0;
        double             tone_amp = 2.0;

      private:
        void fill(size_t m, sig kind) {
            const double w = 2.0 * std::numbers::pi * 1000.0 / k_fs;
            for (size_t i = 0; i < k_block; ++i) {
                double x = 1e-3 * m_noise[m].next();
                if (kind != sig::quiet) {
                    x = (kind == sig::loud ? 2.0 : tone_amp) * std::sin(m_phase[m]);
                }
                m_phase[m] += w;
                m_x[i] = static_cast<Sample>(x);
            }
        }

        std::vector<lcg_noise> m_noise;
        std::vector<double>    m_phase;
        std::vector<Sample>    m_x;
    };

    template <typename Sample>
    typename howl_guard<Sample>::config guard_config(size_t mics = 1) {
        typename howl_guard<Sample>::config c;
        c.microphones = mics;
        c.block_size  = k_block;
        c.sample_rate = k_fs;
        return c;
    }

    template <typename Sample>
    Sample db_to_lin(double db) {
        return std::pow(Sample(10), static_cast<Sample>(db) / Sample(20));
    }

    /// Drive a fresh single-mic guard to OPEN (ok, quiet) and through its
    /// ramp; with `past_probation`, also through probation.
    template <typename Sample>
    void open_it(driver<Sample>& d, bool past_probation) {
        ASSERT_EQ(d.run1(k_release + 10, sig::quiet, true, guard_state::open), k_release - 1);
        d.run1(k_ramp_up, sig::quiet, true);
        if (past_probation) {
            d.run1(k_probate, sig::quiet, true);
        }
        ASSERT_EQ(d.g.state(0), guard_state::open);
        ASSERT_EQ(d.g.gain_db(0), Sample(0));
    }

    template <typename Sample>
    class howl_guard_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(howl_guard_test, sample_types);

    // From construction the guard is ARMING at -arming_duck_db, exactly, and
    // declares (-> OPEN) on the tick the verdict has been ok for
    // release_hold_s with the detector quiet; the gain then ramps to 0 dB
    // linearly in dB over release_ramp_ms and lands on exactly 1.
    TYPED_TEST(howl_guard_test, ArmsThenDeclaresAfterTheReleaseHold) {
        driver<TypeParam> d(guard_config<TypeParam>());
        EXPECT_EQ(d.g.state(0), guard_state::arming);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
        EXPECT_EQ(d.g.gain_block(0)[0], db_to_lin<TypeParam>(-30.0));
        // ok but not yet held: ARMING
        EXPECT_EQ(d.run1(k_release - 1, sig::quiet, true, guard_state::open), -1);
        d.tick1(sig::quiet, true);
        ASSERT_EQ(d.g.state(0), guard_state::open);
        EXPECT_TRUE(d.g.declared(0));
        // the entry block carries the first 64 of 9600 ramp steps
        const TypeParam* g = d.g.gain_block(0);
        for (size_t i = 0; i < k_block; ++i) {
            const double want = std::pow(10.0, (-30.0 + 30.0 * static_cast<double>(i + 1) / 9600.0) / 20.0);
            EXPECT_NEAR(static_cast<double>(g[i]), want, want * 1e-5) << i;
        }
        d.run1(k_ramp_up / 2 - 1, sig::quiet, true);
        EXPECT_NEAR(static_cast<double>(d.g.gain_db(0)), -15.0, 1e-3);
        d.run1(k_ramp_up / 2 - 1, sig::quiet, true);
        EXPECT_LT(d.g.gain_db(0), TypeParam(0));
        d.tick1(sig::quiet, true);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(0));
        EXPECT_EQ(d.g.gain_block(0)[k_block - 1], TypeParam(1));
        d.tick1(sig::quiet, true);
        for (size_t i = 0; i < k_block; ++i) {
            EXPECT_EQ(d.g.gain_block(0)[i], TypeParam(1)) << i;
        }
    }

    // A level-catch trip (one block over the ceiling) ducks in the same tick;
    // the gain ramps down duck_db over attack_ms (240 samples: 3.75 blocks)
    // and lands on exactly -20 dB. Outside probation it is not a strike.
    TYPED_TEST(howl_guard_test, CeilingTripDucksAndTheAttackLandsExactly) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, true);
        d.tick1(sig::loud, true);
        EXPECT_TRUE(d.g.tripped(0));
        EXPECT_EQ(d.g.state(0), guard_state::ducked);
        EXPECT_EQ(d.g.strikes(0), 0U);
        d.run1(2, sig::quiet, true);
        EXPECT_GT(d.g.gain_db(0), TypeParam(-20));
        d.tick1(sig::quiet, true);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-20));
        EXPECT_EQ(d.g.gain_block(0)[47], db_to_lin<TypeParam>(-20.0));
        EXPECT_EQ(d.g.gain_block(0)[k_block - 1], db_to_lin<TypeParam>(-20.0));
    }

    // The walk path: DUCKED releases on the tick the verdict has been ok for
    // release_hold_s since the duck AND the detector has been quiet for
    // quiet_s; trips restart the hold. Then RELEASING ramps back and OPENs.
    TYPED_TEST(howl_guard_test, WalkReleaseNeedsTheHoldAndQuiet) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, true);
        d.tick1(sig::loud, true);
        ASSERT_EQ(d.g.state(0), guard_state::ducked);
        // a second trip 0.5 s later restarts the hold
        d.run1(374, sig::quiet, true);
        d.tick1(sig::loud, true);
        EXPECT_EQ(d.g.state(0), guard_state::ducked);
        const long rel = d.run1(k_release + 10, sig::quiet, true, guard_state::releasing);
        EXPECT_EQ(rel, k_release - 1);
        EXPECT_EQ(d.g.rearms(0), 0U);
        EXPECT_EQ(d.run1(k_ramp_up + 2, sig::quiet, true, guard_state::open), k_ramp_up - 1);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(0));
        EXPECT_EQ(d.g.ducks(0), 1U);
    }

    // Quiet binds when the hold is shorter: with release_hold_s 0.5 s the
    // walk release waits for quiet_s (1 s) after the last detector verdict.
    TYPED_TEST(howl_guard_test, ReleaseWaitsForQuiet) {
        auto c                  = guard_config<TypeParam>();
        c.policy.release_hold_s = 0.5;
        driver<TypeParam> d(c);
        ASSERT_EQ(d.run1(k_quiet, sig::quiet, true, guard_state::open), 374);
        d.run1(k_ramp_up + k_probate, sig::quiet, true);
        d.tick1(sig::loud, true);
        ASSERT_EQ(d.g.state(0), guard_state::ducked);
        EXPECT_EQ(d.run1(k_quiet + 5, sig::quiet, true, guard_state::releasing), k_quiet - 1);
    }

    // F -> 2F: the verdict goes not-ok and stays there. LOST ducks once,
    // trip_hold_s after the ok -> not-ok edge; the timer re-arm releases
    // rearm_timeout_s after the duck; then, with the verdict still not ok,
    // nothing pumps for a minute. After the re-arm LOST arms only once the
    // verdict has been ok for release_hold_s: ok for one tick short of it,
    // then lost, does not duck; ok held, then lost, does.
    TYPED_TEST(howl_guard_test, VerdictLostDucksOnceAndTheRearmDoesNotPump) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, true);
        EXPECT_EQ(d.run1(k_trip + 5, sig::quiet, false, guard_state::ducked), k_trip - 1);
        EXPECT_EQ(d.g.strikes(0), 0U);
        EXPECT_EQ(d.run1(k_rearm + 5, sig::quiet, false, guard_state::releasing), k_rearm - 1);
        EXPECT_EQ(d.g.rearms(0), 1U);
        EXPECT_EQ(d.run1(k_ramp_up + 2, sig::quiet, false, guard_state::open), k_ramp_up - 1);
        EXPECT_EQ(d.run1(750 * 60, sig::quiet, false, guard_state::ducked), -1);
        EXPECT_EQ(d.g.ducks(0), 1U);
        d.tick1(sig::quiet, true);
        EXPECT_EQ(d.run1(750 * 10, sig::quiet, false, guard_state::ducked), -1);
        d.run1(k_release - 1, sig::quiet, true);
        EXPECT_EQ(d.run1(750 * 10, sig::quiet, false, guard_state::ducked), -1);
        EXPECT_EQ(d.g.ducks(0), 1U);
        d.run1(k_release, sig::quiet, true);
        EXPECT_EQ(d.run1(k_trip + 5, sig::quiet, false, guard_state::ducked), k_trip - 1);
        EXPECT_EQ(d.g.ducks(0), 2U);
        // A walk release (ok held while ducked) arms LOST as before.
        ASSERT_EQ(d.run1(k_release + 5, sig::quiet, true, guard_state::releasing), k_release - 1);
        EXPECT_EQ(d.run1(k_trip + 5, sig::quiet, false, guard_state::ducked), k_trip - 1);
        EXPECT_EQ(d.g.ducks(0), 3U);
    }

    // Strikes: a trip within probation of entering OPEN lowers the restore
    // level by strike_db; the third (max_strikes) latches at the level then
    // (no cap: -9 dB) through DUCKED and RELEASING. The latch holds while the
    // verdict stays ok and leaves on a FRESH ok edge held release_hold_s.
    TYPED_TEST(howl_guard_test, StrikesBackOffAndTheThirdLatches) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, false);
        for (int k = 1; k <= 3; ++k) {
            d.tick1(sig::loud, true);
            ASSERT_EQ(d.g.state(0), guard_state::ducked) << k;
            EXPECT_EQ(d.g.strikes(0), static_cast<size_t>(k));
            EXPECT_EQ(d.g.level_db(0), TypeParam(-3 * k));
            EXPECT_EQ(d.g.latched(0), k == 3);
            d.run1(10, sig::quiet, true);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-3 * k - 20)) << k;
            ASSERT_GE(d.run1(k_release + 10, sig::quiet, true, guard_state::releasing), 0) << k;
            const guard_state arrive = (k == 3) ? guard_state::latched : guard_state::open;
            ASSERT_GE(d.run1(k_ramp_up + 2, sig::quiet, true, arrive), 0) << k;
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-3 * k)) << k;
        }
        EXPECT_TRUE(d.g.unprotected(0));
        // ok all along: no fresh edge, still latched
        EXPECT_EQ(d.run1(750 * 5, sig::quiet, true, guard_state::releasing), -1);
        d.tick1(sig::quiet, false);
        EXPECT_EQ(d.run1(k_release + 5, sig::quiet, true, guard_state::releasing), k_release - 1);
        EXPECT_FALSE(d.g.latched(0));
        EXPECT_GE(d.run1(k_ramp_up + 2, sig::quiet, true, guard_state::open), 0);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-9));
        EXPECT_EQ(d.g.strikes(0), 3U);
    }

    // The re-arm timeout doubles per strike: one strike, verdict not ok ->
    // RELEASING 2 x rearm_timeout_s after the duck.
    TYPED_TEST(howl_guard_test, RearmTimeoutDoublesPerStrike) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, false);
        d.tick1(sig::loud, false);
        ASSERT_EQ(d.g.state(0), guard_state::ducked);
        ASSERT_EQ(d.g.strikes(0), 1U);
        EXPECT_EQ(d.run1(2 * k_rearm + 5, sig::quiet, false, guard_state::releasing), 2 * k_rearm - 1);
    }

    // clear() releases a latch and the back-off: ramps up to 0 dB, OPEN.
    TYPED_TEST(howl_guard_test, ClearReleasesTheLatch) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, false);
        for (int k = 1; k <= 3; ++k) {
            d.tick1(sig::loud, true);
            d.run1(k_release + 10, sig::quiet, true, guard_state::releasing);
            d.run1(k_ramp_up + 2, sig::quiet, true, k == 3 ? guard_state::latched : guard_state::open);
        }
        ASSERT_EQ(d.g.state(0), guard_state::latched);
        d.g.clear();
        EXPECT_EQ(d.g.state(0), guard_state::releasing);
        EXPECT_EQ(d.g.strikes(0), 0U);
        EXPECT_GE(d.run1(k_ramp_up + 2, sig::quiet, true, guard_state::open), 0);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(0));
        EXPECT_FALSE(d.g.unprotected(0));
    }

    // ARMING's liveness: without a cap a run that never declares stays ARMING
    // at the arming gain and raises `unprotected` at arming_timeout_s; with
    // the host's cap it ramps to the cap (OPEN_CAPPED), and a late
    // declaration from there ramps to 0 dB and OPENs. A cap below the arming
    // duck limits ARMING's gain.
    TYPED_TEST(howl_guard_test, ArmingTimesOutUnprotectedOrToTheCap) {
        {
            driver<TypeParam> d(guard_config<TypeParam>());
            d.run1(k_timeout - 1, sig::quiet, false);
            EXPECT_FALSE(d.g.unprotected(0));
            d.tick1(sig::quiet, false);
            EXPECT_TRUE(d.g.unprotected(0));
            EXPECT_EQ(d.g.state(0), guard_state::arming);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
        }
        {
            auto c          = guard_config<TypeParam>();
            c.policy.cap_db = -12.0;
            driver<TypeParam> d(c);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
            EXPECT_EQ(d.run1(k_timeout + 5, sig::quiet, false, guard_state::open_capped), k_timeout - 1);
            EXPECT_TRUE(d.g.unprotected(0));
            d.run1(k_ramp_up, sig::quiet, false);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-12));
            // LOST is disarmed before a declaration
            EXPECT_EQ(d.run1(750 * 20, sig::quiet, false, guard_state::ducked), -1);
            EXPECT_EQ(d.run1(k_release + 5, sig::quiet, true, guard_state::releasing), k_release - 1);
            EXPECT_GE(d.run1(k_ramp_up + 2, sig::quiet, true, guard_state::open), 0);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(0));
            EXPECT_FALSE(d.g.unprotected(0));
        }
        {
            auto c          = guard_config<TypeParam>();
            c.policy.cap_db = -40.0;
            driver<TypeParam> d(c);
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-40));
        }
    }

    // A trip in ARMING is a strike, restarts the hold, and does not latch
    // or leave ARMING; the declaration then waits for ok held AND quiet.
    TYPED_TEST(howl_guard_test, ArmingTripIsAStrikeAndRestartsTheHold) {
        driver<TypeParam> d(guard_config<TypeParam>());
        d.run1(500, sig::quiet, true);
        d.tick1(sig::loud, true);
        EXPECT_EQ(d.g.state(0), guard_state::arming);
        EXPECT_EQ(d.g.strikes(0), 1U);
        EXPECT_EQ(d.run1(k_release + 5, sig::quiet, true, guard_state::open), k_release - 1);
        d.run1(k_ramp_up + 1, sig::quiet, true);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-3));
    }

    // The growth path ducks only while the verdict is not ok: a growing
    // tone (the level catch off, so only growth can fire) in OPEN with the
    // verdict ok is a hint; in OPEN_CAPPED (no declaration) it is a TRIP.
    TYPED_TEST(howl_guard_test, GrowthDucksOnlyWhileTheVerdictIsNotOk) {
        for (const bool ok : {true, false}) {
            auto c                 = guard_config<TypeParam>();
            c.detector.level_catch = false;
            c.policy.cap_db        = -12.0;
            driver<TypeParam> d(c);
            if (ok) {
                open_it(d, true);
            }
            else {
                d.run1(k_timeout + k_ramp_up + 10, sig::quiet, false);
                ASSERT_EQ(d.g.state(0), guard_state::open_capped);
            }
            // a tone growing 600 dB/s from -60 dB re 1 over the -60 dB floor
            bool fired = false;
            for (int k = 0; k < 300 && !fired; ++k) {
                d.tone_amp = std::pow(10.0, (-60.0 + 600.0 * static_cast<double>(k) / 750.0) / 20.0);
                d.tick1(sig::tone, ok);
                fired = d.g.detector(0).trigger() == tap::mu::howl_trigger::growth;
            }
            ASSERT_TRUE(fired) << ok;
            if (ok) {
                EXPECT_EQ(d.g.state(0), guard_state::open);
                EXPECT_EQ(d.g.hints(0), 1U);
                EXPECT_EQ(d.g.ducks(0), 0U);
            }
            else {
                EXPECT_EQ(d.g.state(0), guard_state::ducked);
                EXPECT_EQ(d.g.hints(0), 0U);
            }
        }
    }

    // A canceller restart (A' back to 0 dB) sends an OPEN mic to ARMING:
    // the gain ramps down over attack_ms and the declaration is forgotten.
    TYPED_TEST(howl_guard_test, RestartSendsAnOpenMicToArming) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, true);
        d.tick({sig::quiet}, {false}, 1.0);
        EXPECT_EQ(d.g.state(0), guard_state::arming);
        EXPECT_FALSE(d.g.declared(0));
        d.run1(4, sig::quiet, false);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
    }

    // Attribution: a howl tripping mic 1 with mic 0 carrying the line 10.5 dB
    // lower ducks mic 1 alone; trips continuing attack_ms + 50 ms later duck
    // every mic (the fallback). Equal lines duck both at once. The bus
    // stage carries the deepest duck (relative to the restore level).
    TYPED_TEST(howl_guard_test, AttributionDucksTheLoudestThenFallsBack) {
        auto open2 = [](driver<TypeParam>& d) {
            for (long i = 0; i < k_release + k_ramp_up + k_probate; ++i) {
                d.tick({sig::quiet, sig::quiet}, {true, true});
            }
            ASSERT_EQ(d.g.state(0), guard_state::open);
            ASSERT_EQ(d.g.state(1), guard_state::open);
        };
        {
            driver<TypeParam> d(guard_config<TypeParam>(2));
            open2(d);
            d.tone_amp = 0.6;
            d.tick({sig::tone, sig::loud}, {true, true});
            EXPECT_EQ(d.g.state(0), guard_state::open);
            EXPECT_EQ(d.g.state(1), guard_state::ducked);
            EXPECT_EQ(d.g.fallbacks(), 0U);
            for (int k = 0; k < 4; ++k) {
                d.tick({sig::quiet, sig::quiet}, {true, true});
            }
            // the bus stage: mic 1's duck, landed; mic 0 contributes nothing
            const TypeParam* bus = d.g.bus_gain_block();
            EXPECT_NEAR(static_cast<double>(bus[k_block - 1]), 0.1, 1e-5);
            std::vector<TypeParam> in(k_block, TypeParam(1));
            std::vector<TypeParam> out(k_block, TypeParam(0));
            d.g.bus_stage()->process_block(in.data(), out.data(), k_block);
            EXPECT_EQ(out[k_block - 1], bus[k_block - 1]);
            // the howl continues on mic 1
            long fell = -1;
            for (long k = 0; k < 2 * k_window && fell < 0; ++k) {
                d.tick({sig::tone, sig::loud}, {true, true});
                if (d.g.state(0) == guard_state::ducked) {
                    fell = d.ticks;
                }
            }
            EXPECT_GE(fell, 0);
            EXPECT_EQ(d.g.fallbacks(), 1U);
        }
        {
            driver<TypeParam> d(guard_config<TypeParam>(2));
            open2(d);
            d.tick({sig::loud, sig::loud}, {true, true});
            EXPECT_EQ(d.g.state(0), guard_state::ducked);
            EXPECT_EQ(d.g.state(1), guard_state::ducked);
        }
        {
            driver<TypeParam> d(guard_config<TypeParam>(2));
            open2(d);
            for (size_t i = 0; i < k_block; ++i) {
                EXPECT_EQ(d.g.bus_gain_block()[i], TypeParam(1));
            }
        }
    }

    // reset() returns every mic to the constructed ARMING state.
    TYPED_TEST(howl_guard_test, ResetRestoresTheConstructedState) {
        driver<TypeParam> d(guard_config<TypeParam>());
        open_it(d, false);
        d.tick1(sig::loud, true);
        ASSERT_EQ(d.g.strikes(0), 1U);
        d.g.reset();
        EXPECT_EQ(d.g.state(0), guard_state::arming);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
        EXPECT_EQ(d.g.strikes(0), 0U);
        EXPECT_EQ(d.g.ducks(0), 0U);
        EXPECT_FALSE(d.g.declared(0));
        EXPECT_EQ(d.run1(k_release + 5, sig::quiet, true, guard_state::open), k_release - 1);
    }

    // set_policy() clamps (a non-finite field keeps its value; ranges are
    // enforced) and keeps the state; set_cap_db() moves ARMING's gain and,
    // removed, sends a capped mic back to ARMING.
    TYPED_TEST(howl_guard_test, PolicyClampsAndTheCapMoves) {
        driver<TypeParam> d(guard_config<TypeParam>());
        guard_policy      p = d.g.policy();
        p.duck_db           = std::numeric_limits<double>::quiet_NaN();
        p.attack_ms         = -5.0;
        p.max_strikes       = 0;
        p.strike_db         = 1e9;
        d.g.set_policy(p);
        EXPECT_EQ(d.g.policy().duck_db, 20.0);
        EXPECT_EQ(d.g.policy().attack_ms, 0.0);
        EXPECT_EQ(d.g.policy().max_strikes, 1U);
        EXPECT_EQ(d.g.policy().strike_db, 60.0);
        EXPECT_EQ(d.g.microphones(), 1U);
        EXPECT_EQ(d.g.block_size(), k_block);
        d.g.set_cap_db(-45.0);
        d.tick1(sig::quiet, false);
        d.run1(4, sig::quiet, false);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-45));
        d.g.set_cap_db(5.0);
        EXPECT_EQ(d.g.policy().cap_db.value_or(-1.0), 0.0);
        d.g.set_cap_db(-12.0);
        d.run1(k_timeout + 10, sig::quiet, false);
        ASSERT_EQ(d.g.state(0), guard_state::open_capped);
        d.g.set_cap_db(std::nullopt);
        EXPECT_EQ(d.g.state(0), guard_state::arming);
    }

    // The cap is mandatory for opening without a declaration: without one,
    // ARMING's timeout raises `unprotected` and the mic stays ARMING at the
    // arming gain for good (a minute here); a mic latched before any
    // declaration (its floor was the cap) re-arms when the cap is removed.
    TYPED_TEST(howl_guard_test, WithoutACapNothingOpensWithoutADeclaration) {
        {
            driver<TypeParam> d(guard_config<TypeParam>());
            EXPECT_EQ(d.run1(750 * 60, sig::quiet, false, guard_state::open_capped), -1);
            EXPECT_EQ(d.g.state(0), guard_state::arming);
            EXPECT_TRUE(d.g.unprotected(0));
            EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
            EXPECT_NEAR(d.g.time_in_state_s(0), 60.0, 1e-9);
        }
        auto c          = guard_config<TypeParam>();
        c.policy.cap_db = -12.0;
        driver<TypeParam> d(c);
        ASSERT_EQ(d.run1(k_timeout + 5, sig::quiet, false, guard_state::open_capped), k_timeout - 1);
        // three trips, each within probation of (re-)entering OPEN_CAPPED,
        // released by the timer (the verdict never ok): the third latches
        for (int k = 1; k <= 3; ++k) {
            d.run1(k_ramp_up + 2, sig::quiet, false);
            d.tick1(sig::loud, false);
            ASSERT_EQ(d.g.state(0), guard_state::ducked) << k;
            ASSERT_EQ(d.g.strikes(0), static_cast<size_t>(k));
            const guard_state arrive = (k == 3) ? guard_state::latched : guard_state::open_capped;
            ASSERT_GE(d.run1((k_rearm << k) + k_ramp_up + 10, sig::quiet, false, arrive), 0) << k;
        }
        EXPECT_TRUE(d.g.latched(0));
        EXPECT_FALSE(d.g.declared(0));
        d.run1(k_ramp_up + 2, sig::quiet, false);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-12));
        d.g.set_cap_db(std::nullopt);
        EXPECT_EQ(d.g.state(0), guard_state::arming);
        EXPECT_FALSE(d.g.latched(0));
        d.run1(10, sig::quiet, false);
        EXPECT_EQ(d.g.gain_db(0), TypeParam(-30));
    }

    // The soundcheck sampler: calibrate_s of blocks into 0.1 dB histograms
    // (later blocks are not sampled), the median / 95th percentile / max as
    // bin centres, suggested thresholds = median + the policy's margins;
    // applied, the mic runs on them (set_policy() leaves them, reset()
    // keeps them, clear_calibration() restores the policy's), and the
    // verdict trigger needs an ok tick under them first.
    TYPED_TEST(howl_guard_test, CalibrationSamplesSuggestsAndApplies) {
        auto c                   = guard_config<TypeParam>();
        c.policy.calibrate_s     = 1.0; // 750 ticks
        c.policy.cal_d_margin_db = 2.0;
        c.policy.cal_a_margin_db = 3.0;
        driver<TypeParam> d(c);
        open_it(d, true);
        d.g.calibrate_begin();
        EXPECT_TRUE(d.g.calibrating());
        // 600 ticks at D -8.04 / A' -30.04 dB, 150 at -2.04 / -20.04 (bin
        // centres -8.05 / -2.05 and -30.05 / -20.05), then 150 more that
        // the window no longer takes
        for (int k = 0; k < 600; ++k) {
            d.tick_db(-8.04, -30.04);
        }
        EXPECT_TRUE(d.g.calibrating());
        for (int k = 0; k < 150; ++k) {
            d.tick_db(-2.04, -20.04);
        }
        EXPECT_FALSE(d.g.calibrating());
        for (int k = 0; k < 150; ++k) {
            d.tick_db(-50.0, -90.0);
        }
        EXPECT_EQ(d.g.state(0), guard_state::open);
        const auto cal = d.g.calibrate_end(false);
        ASSERT_EQ(cal.size(), 1U);
        EXPECT_EQ(cal[0].blocks, 750U);
        EXPECT_NEAR(cal[0].d_median_db, -8.05, 1e-9);
        EXPECT_NEAR(cal[0].d_p95_db, -2.05, 1e-9);
        EXPECT_NEAR(cal[0].d_max_db, -2.05, 1e-9);
        EXPECT_NEAR(cal[0].a_median_db, -30.05, 1e-9);
        EXPECT_NEAR(cal[0].a_p95_db, -20.05, 1e-9);
        EXPECT_NEAR(cal[0].d_db, -6.05, 1e-9);
        EXPECT_NEAR(cal[0].a_db, -27.05, 1e-9);
        EXPECT_FALSE(cal[0].applied);
        EXPECT_EQ(d.g.threshold_d_db(0), static_cast<TypeParam>(-1.235));
        // applied: D -5 dB is now not-ok, but LOST needs an ok tick first
        d.g.calibrate_end(true);
        EXPECT_TRUE(d.g.calibration(0).applied);
        EXPECT_NEAR(static_cast<double>(d.g.threshold_d_db(0)), -6.05, 1e-5);
        EXPECT_NEAR(static_cast<double>(d.g.threshold_a_db(0)), -27.05, 1e-5);
        for (int k = 0; k < 2 * k_trip; ++k) {
            d.tick_db(-5.0, -30.0);
        }
        EXPECT_FALSE(d.g.verdict_ok(0));
        EXPECT_EQ(d.g.state(0), guard_state::open);
        d.tick_db(-7.0, -30.0);
        EXPECT_TRUE(d.g.verdict_ok(0));
        for (long k = 0; k < k_trip; ++k) {
            d.tick_db(-5.0, -30.0);
        }
        EXPECT_EQ(d.g.state(0), guard_state::ducked);
        // the policy does not overwrite them; reset() keeps them
        guard_policy p = d.g.policy();
        p.d_db         = -3.0;
        d.g.set_policy(p);
        EXPECT_NEAR(static_cast<double>(d.g.threshold_d_db(0)), -6.05, 1e-5);
        d.g.reset();
        EXPECT_NEAR(static_cast<double>(d.g.threshold_d_db(0)), -6.05, 1e-5);
        d.g.clear_calibration();
        EXPECT_EQ(d.g.threshold_d_db(0), TypeParam(-3));
        EXPECT_EQ(d.g.threshold_a_db(0), static_cast<TypeParam>(-23.842));
        // nothing sampled: nothing to suggest or apply
        d.g.calibrate_begin();
        const auto none = d.g.calibrate_end(true);
        EXPECT_EQ(none[0].blocks, 0U);
        EXPECT_FALSE(none[0].applied);
        EXPECT_EQ(d.g.threshold_d_db(0), TypeParam(-3));
        // out-of-range values land in the end bins
        d.g.calibrate_begin();
        d.tick_db(-200.0, 50.0);
        const auto ends = d.g.calibrate_end(false);
        EXPECT_NEAR(ends[0].d_median_db, -59.95, 1e-9);
        EXPECT_NEAR(ends[0].a_median_db, 19.95, 1e-9);
    }

    // ------------------------------------------------- afc_chain's hook

    template <typename Sample>
    using kalman_afc = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;
    template <typename Sample>
    using chain = tap::mu::afc_chain<Sample, kalman_afc<Sample>>;

    template <typename Sample>
    typename chain<Sample>::config chain_config(size_t mics, size_t shadow) {
        typename chain<Sample>::config c;
        c.microphones                 = mics;
        c.canceller.fdaf.block_size   = k_block;
        c.canceller.fdaf.partitions   = 4;
        c.canceller.shadow_partitions = shadow;
        return c;
    }

    // set_guard() refuses a canceller without the shadow and a guard of the
    // wrong shape (leaving the chain unguarded), attaches otherwise; nullptr
    // detaches. The guard runs on every block: one mic, no aux, no stages,
    // the speaker is exactly the guard's gain times the canceller's residual
    // (ARMING's -30 dB here), and error_block() stays the residual.
    // afc_chain::reset() returns the guard to ARMING.
    TYPED_TEST(howl_guard_test, ChainHookAttachesRunsAndResets) {
        using sample = TypeParam;
        {
            chain<sample>      c(chain_config<sample>(1, 0));
            howl_guard<sample> g(guard_config<sample>(1));
            EXPECT_FALSE(c.set_guard(&g));
            EXPECT_EQ(c.guard(), nullptr);
        }
        {
            chain<sample>      c(chain_config<sample>(2, 2));
            howl_guard<sample> g(guard_config<sample>(1));
            EXPECT_FALSE(c.set_guard(&g));
            EXPECT_TRUE(c.set_guard(nullptr));
        }
        chain<sample>      c(chain_config<sample>(1, 2));
        howl_guard<sample> g(guard_config<sample>(1));
        ASSERT_TRUE(c.set_guard(&g));
        EXPECT_EQ(c.guard(), &g);
        lcg_noise           noise(3);
        std::vector<sample> mic(k_block);
        std::vector<sample> spk(k_block);
        for (int blk = 0; blk < 50; ++blk) {
            for (auto& x : mic) {
                x = static_cast<sample>(0.1 * noise.next()); // -25 dB re 1: under the default ceiling
            }
            const sample* mics[1] = {mic.data()};
            c.process_block(mics, nullptr, spk.data(), k_block);
            const sample* e  = c.error_block(0);
            const sample* gb = g.gain_block(0);
            for (size_t i = 0; i < k_block; ++i) {
                ASSERT_EQ(spk[i], gb[i] * e[i]) << blk << " " << i;
            }
        }
        EXPECT_EQ(g.state(0), guard_state::arming);
        // A permissive policy declares at once; the chain's reset re-arms.
        guard_policy p   = g.policy();
        p.d_db           = 100.0;
        p.a_db           = 100.0;
        p.release_hold_s = 0.0;
        p.quiet_s        = 0.0;
        p.restart_a_db   = 120.0;
        g.set_policy(p);
        for (int blk = 0; blk < 3; ++blk) {
            const sample* mics[1] = {mic.data()};
            c.process_block(mics, nullptr, spk.data(), k_block);
        }
        EXPECT_EQ(g.state(0), guard_state::open);
        c.reset();
        EXPECT_EQ(g.state(0), guard_state::arming);
        EXPECT_EQ(g.gain_db(0), sample(-30));
    }

    // Without a guard the chain is unchanged: a chain that had a guard and
    // detached it matches one that never had one, bit for bit.
    TYPED_TEST(howl_guard_test, ChainWithoutAGuardIsUnchanged) {
        using sample = TypeParam;
        chain<sample>      a(chain_config<sample>(2, 2));
        chain<sample>      b(chain_config<sample>(2, 2));
        howl_guard<sample> g(guard_config<sample>(2));
        ASSERT_TRUE(b.set_guard(&g));
        ASSERT_TRUE(b.set_guard(nullptr));
        lcg_noise           noise(5);
        std::vector<sample> m0(k_block);
        std::vector<sample> m1(k_block);
        std::vector<sample> aux(k_block);
        std::vector<sample> sa(k_block);
        std::vector<sample> sb(k_block);
        for (int blk = 0; blk < 40; ++blk) {
            for (size_t i = 0; i < k_block; ++i) {
                m0[i]  = static_cast<sample>(noise.next());
                m1[i]  = static_cast<sample>(noise.next());
                aux[i] = static_cast<sample>(0.25 * noise.next());
            }
            const sample* mics[2] = {m0.data(), m1.data()};
            a.process_block(mics, aux.data(), sa.data(), k_block);
            b.process_block(mics, aux.data(), sb.data(), k_block);
            for (size_t i = 0; i < k_block; ++i) {
                ASSERT_EQ(sa[i], sb[i]) << blk << " " << i;
            }
        }
    }

    TEST(HowlGuardConfigValidation, RejectsBadConfigs) {
        using cfg_t = howl_guard<float>::config;
        auto bad    = [](auto mutate) {
            cfg_t c;
            mutate(c);
            return c;
        };
        EXPECT_NO_THROW(howl_guard<float>(cfg_t{}));
        EXPECT_NO_THROW(howl_guard<double>(howl_guard<double>::config{}));
        EXPECT_THROW(howl_guard<float>(bad([](cfg_t& c) { c.microphones = 0; })), std::invalid_argument);
        EXPECT_THROW(howl_guard<float>(bad([](cfg_t& c) { c.block_size = 0; })), std::invalid_argument);
        EXPECT_THROW(howl_guard<float>(bad([](cfg_t& c) { c.sample_rate = 0.0; })), std::invalid_argument);
        EXPECT_THROW(howl_guard<float>(bad([](cfg_t& c) { c.sample_rate = std::numeric_limits<double>::quiet_NaN(); })),
                     std::invalid_argument);
        EXPECT_THROW(howl_guard<float>(bad([](cfg_t& c) { c.detector.bands = 2; })), std::invalid_argument);
        // the detector's block size and rate are the guard's
        cfg_t c;
        c.detector.block_size  = 7;
        c.detector.sample_rate = 8000.0;
        howl_guard<float> g(c);
        EXPECT_EQ(g.detector(0).cfg().block_size, 64U);
        EXPECT_EQ(g.detector(0).cfg().sample_rate, 48000.0);
    }

    TEST(HowlGuardRtContract, PostConstructionEntryPointsAreNoexcept) {
        using gf = howl_guard<float>;
        using gd = howl_guard<double>;
        static_assert(noexcept(std::declval<gf&>().analyze(0, nullptr, 1.0F, 1.0F)));
        static_assert(noexcept(std::declval<gf&>().update()));
        static_assert(noexcept(std::declval<const gf&>().apply(0, nullptr, nullptr)));
        static_assert(noexcept(std::declval<const gf&>().accumulate(0, nullptr, nullptr)));
        static_assert(noexcept(std::declval<gf&>().reset()));
        static_assert(noexcept(std::declval<gf&>().clear()));
        static_assert(noexcept(std::declval<gf&>().set_policy(guard_policy{})));
        static_assert(noexcept(std::declval<gf&>().set_cap_db(std::nullopt)));
        static_assert(noexcept(std::declval<gf&>().bus_stage()));
        static_assert(noexcept(std::declval<gf&>().calibrate_begin()));
        static_assert(noexcept(std::declval<gf&>().calibrate_end(true)));
        static_assert(noexcept(std::declval<gf&>().set_thresholds(0, 0.0, 0.0)));
        static_assert(noexcept(std::declval<gf&>().clear_calibration()));
        static_assert(noexcept(std::declval<const gf&>().calibration(0)));
        static_assert(noexcept(std::declval<const gf&>().state(0)));
        static_assert(noexcept(std::declval<const gf&>().unprotected(0)));
        static_assert(noexcept(std::declval<gd&>().update()));
        static_assert(tap::mu::afc_stage<gf::bus_stage_type, float>);
        static_assert(tap::mu::afc_stage<gd::bus_stage_type, double>);
        static_assert(noexcept(std::declval<chain<float>&>().set_guard(nullptr)));
        SUCCEED();
    }

} // namespace
