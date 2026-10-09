// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// THE NaN WATCHDOG'S TEST (mutap/watchdog.h; the production-readiness plan's
// M0c). One non-finite block per stage — partitioned_fdaf, partitioned_fdkf,
// pem_afc on both cores, residual_suppressor, aec_chain, howl_guard — and
// three assertions each:
//
//   1. the block that carried the NaN (or infinity) comes out as zeros, never
//      as the NaN;
//   2. the stage's watchdog counter reads exactly 1 (the stage that saw the
//      fault counts it; a stage the fault never reached counts nothing);
//   3. RECOVERY WITHIN ONE BLOCK: the next finite block's output is
//      bit-identical to a freshly constructed stage's on the same block, so
//      the trip reset every piece of state reset() resets — and reset()
//      resets everything (a predictor coefficient or a tracker left behind
//      would show here as a differing sample).
//
// The guard's trip sends the mic to ARMING; its recovery is that the same
// finite material declares it again. Nothing here asserts a measured
// acoustic number: the watchdog touches no arithmetic on finite input,
// which the fingerprint gate (tests/fingerprint_harness.cpp, nine CI legs)
// asserts, and its cost is read off the instruction-count ratchet
// (bench/README.md) and recorded in mutap/watchdog.h.
//
// Material: an xorshift-driven synthetic echo (no <random>, no libm in the
// corpus), as the fingerprint harness and the ratchet use, so the file runs
// unchanged on bare metal. Both emulated selections run the float suite.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/howl_guard.h"
#include "mutap/pem_afc.h"
#include "mutap/postfilter.h"

namespace {

    using tap::mu::aec_chain;
    using tap::mu::aec_chain_preset;
    using tap::mu::guard_state;
    using tap::mu::howl_guard;
    using tap::mu::partitioned_fdaf;
    using tap::mu::partitioned_fdkf;
    using tap::mu::pem_afc;
    using tap::mu::residual_suppressor;
    using tap::mu::speech_predictor;

    /// xorshift64 in [-1, 1): integer state, one multiply to Sample.
    class noise {
      public:
        explicit noise(std::uint64_t seed) noexcept
            : m_state(seed) {}
        template <typename Sample>
        Sample next() noexcept {
            m_state ^= m_state << 13;
            m_state ^= m_state >> 7;
            m_state ^= m_state << 17;
            return static_cast<Sample>(static_cast<double>(m_state >> 11) * (2.0 / 9007199254740992.0) - 1.0);
        }

      private:
        std::uint64_t m_state;
    };

    /// A reference u and a mic y = 0.5 u[n - 37] + 0.05 v, block by block.
    template <typename Sample>
    class echo_source {
      public:
        explicit echo_source(size_t block, std::uint64_t seed = 0x9E3779B97F4A7C15ULL)
            : m_u(block)
            , m_y(block)
            , m_hist(block + 64, Sample(0))
            , m_rng(seed) {}

        void next() {
            const size_t b = m_u.size();
            for (size_t i = 0; i < b; ++i) {
                const Sample u = m_rng.template next<Sample>();
                m_hist[i + 64] = u;
                m_u[i]         = u;
                m_y[i]         = Sample(0.5) * m_hist[i + 64 - 37] + Sample(0.05) * m_rng.template next<Sample>();
            }
            for (size_t i = 0; i < 64; ++i) {
                m_hist[i] = m_hist[b + i];
            }
        }

        std::vector<Sample>& u() { return m_u; }
        std::vector<Sample>& y() { return m_y; }

      private:
        std::vector<Sample> m_u;
        std::vector<Sample> m_y;
        std::vector<Sample> m_hist;
        noise               m_rng;
    };

    template <typename Sample>
    void expect_zeros(const std::vector<Sample>& v) {
        for (size_t i = 0; i < v.size(); ++i) {
            EXPECT_EQ(v[i], Sample(0)) << "sample " << i;
        }
    }

    template <typename Sample>
    void expect_same(const std::vector<Sample>& a, const std::vector<Sample>& b) {
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i], b[i]) << "sample " << i;
        }
    }

    template <typename Sample>
    Sample quiet_nan() {
        return std::numeric_limits<Sample>::quiet_NaN();
    }
    template <typename Sample>
    Sample infinity() {
        return std::numeric_limits<Sample>::infinity();
    }

    constexpr size_t k_block      = 64;
    constexpr size_t k_partitions = 4;
    constexpr size_t k_warm       = 40; ///< finite blocks before the fault

    // ------------------------------------------------------------ the cores

    /// One core (partitioned_fdaf / partitioned_fdkf): a NaN in the input
    /// trips, an infinity in the desired signal trips, a frozen core trips
    /// too (the residual check), and after each the core equals a fresh one.
    template <typename Core, typename Sample>
    void core_trips_and_recovers() {
        typename Core::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_partitions;
        Core                core(cfg);
        echo_source<Sample> src(k_block);
        std::vector<Sample> e(k_block);
        std::vector<Sample> est(k_block);
        for (size_t n = 0; n < k_warm; ++n) {
            src.next();
            core.process_block(src.u().data(), src.y().data(), e.data(), est.data());
        }
        ASSERT_EQ(core.watchdog_trips(), 0u);

        // A NaN in the input: zeros out, one trip, the filter empty.
        src.next();
        src.u()[7] = quiet_nan<Sample>();
        core.process_block(src.u().data(), src.y().data(), e.data(), est.data());
        expect_zeros(e);
        expect_zeros(est);
        EXPECT_EQ(core.watchdog_trips(), 1u);
        std::vector<Sample> taps(core.filter_length());
        core.copy_impulse_response(taps.data());
        expect_zeros(taps);

        // Recovery within one block: the next finite block equals a fresh
        // core's, bit for bit, and the count holds.
        Core                fresh(cfg);
        std::vector<Sample> e2(k_block);
        std::vector<Sample> est2(k_block);
        for (size_t n = 0; n < 3; ++n) {
            src.next();
            core.process_block(src.u().data(), src.y().data(), e.data(), est.data());
            fresh.process_block(src.u().data(), src.y().data(), e2.data(), est2.data());
            expect_same(e, e2);
            expect_same(est, est2);
        }
        EXPECT_EQ(core.watchdog_trips(), 1u);
        EXPECT_EQ(fresh.watchdog_trips(), 0u);

        // An infinity in the desired signal, with adaptation frozen: the
        // input check runs before the freeze, so it trips all the same.
        core.set_adaptation(false);
        src.next();
        src.y()[k_block - 1] = infinity<Sample>();
        core.process_block(src.u().data(), src.y().data(), e.data(), est.data());
        expect_zeros(e);
        EXPECT_EQ(core.watchdog_trips(), 2u);
        EXPECT_FALSE(core.adapting()) << "a trip keeps the adaptation freeze, as reset() does";
        core.set_adaptation(true);

        // reset() keeps the count.
        core.reset();
        EXPECT_EQ(core.watchdog_trips(), 2u);
    }

    template <typename Sample>
    class watchdog_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(watchdog_test, sample_types);

    TYPED_TEST(watchdog_test, NlmsCoreTripsAndRecovers) {
        core_trips_and_recovers<partitioned_fdaf<TypeParam>, TypeParam>();
    }

    TYPED_TEST(watchdog_test, KalmanCoreTripsAndRecovers) {
        core_trips_and_recovers<partitioned_fdkf<TypeParam>, TypeParam>();
    }

    // ------------------------------------------------------------- pem_afc

    /// The canceller on either core: the raw pair's check trips before the
    /// core ever sees the block (the core counts nothing), and after the
    /// trip three blocks equal a fresh canceller's — which covers the
    /// predictor's coefficients and both prewhitening states.
    template <typename Core, typename Sample>
    void canceller_trips_and_recovers() {
        using afc = pem_afc<Sample, speech_predictor<Sample>, Core>;
        typename afc::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_partitions;
        afc                 a(cfg);
        echo_source<Sample> src(k_block);
        std::vector<Sample> e(k_block);
        for (size_t n = 0; n < k_warm; ++n) {
            src.next();
            a.process_block(src.u().data(), src.y().data(), e.data());
        }
        ASSERT_EQ(a.watchdog_trips(), 0u);

        src.next();
        src.y()[3] = quiet_nan<Sample>();
        a.process_block(src.u().data(), src.y().data(), e.data());
        expect_zeros(e);
        EXPECT_EQ(a.watchdog_trips(), 1u);
        EXPECT_EQ(a.fdaf().watchdog_trips(), 0u) << "the raw pair never reached the core";

        afc                 fresh(cfg);
        std::vector<Sample> e2(k_block);
        for (size_t n = 0; n < 3; ++n) {
            src.next();
            a.process_block(src.u().data(), src.y().data(), e.data());
            fresh.process_block(src.u().data(), src.y().data(), e2.data());
            expect_same(e, e2);
        }
        EXPECT_EQ(a.watchdog_trips(), 1u);
        a.reset();
        EXPECT_EQ(a.watchdog_trips(), 1u);
    }

    TYPED_TEST(watchdog_test, PemAfcOnNlmsTripsAndRecovers) {
        canceller_trips_and_recovers<partitioned_fdaf<TypeParam>, TypeParam>();
    }

    TYPED_TEST(watchdog_test, PemAfcOnKalmanTripsAndRecovers) {
        canceller_trips_and_recovers<partitioned_fdkf<TypeParam>, TypeParam>();
    }

    // -------------------------------------------------- residual_suppressor

    TYPED_TEST(watchdog_test, SuppressorTripsAndRecovers) {
        using sample_t = TypeParam;
        typename residual_suppressor<sample_t>::config cfg;
        cfg.block_size = 256;
        residual_suppressor<sample_t> s(cfg);
        echo_source<sample_t>         src(256); // u stands in for e, y for yhat
        std::vector<sample_t>         out(256);
        for (size_t n = 0; n < 20; ++n) {
            src.next();
            s.process_block(src.u().data(), src.y().data(), out.data());
        }
        ASSERT_EQ(s.watchdog_trips(), 0u);

        src.next();
        src.y()[100] = infinity<sample_t>();
        s.process_block(src.u().data(), src.y().data(), out.data());
        expect_zeros(out);
        EXPECT_EQ(s.watchdog_trips(), 1u);

        residual_suppressor<sample_t> fresh(cfg);
        std::vector<sample_t>         out2(256);
        for (size_t n = 0; n < 3; ++n) {
            src.next();
            s.process_block(src.u().data(), src.y().data(), out.data());
            fresh.process_block(src.u().data(), src.y().data(), out2.data());
            expect_same(out, out2);
        }
        EXPECT_EQ(s.watchdog_trips(), 1u);
    }

    // ------------------------------------------------------------ aec_chain

    /// The certified chain: its own check fires first, so the canceller
    /// and the post stage count nothing, and the chain's policy state
    /// (receive floor, guard, rescue, shadow) is fresh afterwards.
    TYPED_TEST(watchdog_test, AecChainTripsAndRecovers) {
        using sample_t            = TypeParam;
        const auto            cfg = aec_chain_preset<sample_t>(256, 8, 48000.0);
        aec_chain<sample_t>   chain(cfg);
        echo_source<sample_t> src(256);
        std::vector<sample_t> e(256);
        for (size_t n = 0; n < 20; ++n) {
            src.next();
            chain.process_block(src.u().data(), src.y().data(), e.data());
        }
        ASSERT_EQ(chain.watchdog_trips(), 0u);

        src.next();
        src.u()[0] = quiet_nan<sample_t>();
        chain.process_block(src.u().data(), src.y().data(), e.data());
        expect_zeros(e);
        EXPECT_EQ(chain.watchdog_trips(), 1u);
        EXPECT_EQ(chain.canceller().watchdog_trips(), 0u);
        EXPECT_EQ(chain.postfilter().watchdog_trips(), 0u);

        aec_chain<sample_t>   fresh(cfg);
        std::vector<sample_t> e2(256);
        for (size_t n = 0; n < 3; ++n) {
            src.next();
            chain.process_block(src.u().data(), src.y().data(), e.data());
            fresh.process_block(src.u().data(), src.y().data(), e2.data());
            expect_same(e, e2);
        }
        EXPECT_EQ(chain.watchdog_trips(), 1u);
    }

    // ----------------------------------------------------------- howl_guard

    /// Quiet noise with an ok verdict (A' -30 dB, D -3 dB: both under the
    /// policy's thresholds) declares a mic; returns the tick it opened on.
    template <typename Sample>
    long drive_to_open(howl_guard<Sample>& g, size_t mic, noise& rng, long max_ticks) {
        std::vector<Sample> e(k_block);
        for (long t = 0; t < max_ticks; ++t) {
            for (size_t m = 0; m < g.microphones(); ++m) {
                for (auto& x : e) {
                    x = Sample(1e-3) * rng.template next<Sample>();
                }
                g.analyze(m, e.data(), Sample(1e-3), Sample(0.5));
            }
            g.update();
            if (g.state(mic) == guard_state::open) {
                return t;
            }
        }
        return -1;
    }

    TYPED_TEST(watchdog_test, GuardTripsToArmingAndDeclaresAgain) {
        using sample_t = TypeParam;
        typename howl_guard<sample_t>::config cfg;
        cfg.microphones = 2;
        cfg.block_size  = k_block;
        cfg.sample_rate = 48000.0;
        howl_guard<sample_t> g(cfg);
        noise                rng(11);
        ASSERT_GE(drive_to_open(g, 0, rng, 4000), 0);
        ASSERT_EQ(g.state(1), guard_state::open);
        ASSERT_EQ(g.watchdog_trips(0), 0u);
        ASSERT_EQ(g.watchdog_trips(1), 0u);

        // A NaN in mic 1's residual: mic 1 to ARMING, mic 0 untouched, the
        // tick's gains finite.
        std::vector<sample_t> e(k_block, sample_t(0));
        g.analyze(0, e.data(), sample_t(1e-3), sample_t(0.5)); // mic 0: a finite block
        e[5] = quiet_nan<sample_t>();
        g.analyze(1, e.data(), sample_t(1e-3), sample_t(0.5)); // mic 1: the fault
        g.update();
        EXPECT_EQ(g.state(1), guard_state::arming);
        EXPECT_EQ(g.watchdog_trips(1), 1u);
        EXPECT_EQ(g.state(0), guard_state::open);
        EXPECT_EQ(g.watchdog_trips(0), 0u);
        for (size_t i = 0; i < k_block; ++i) {
            EXPECT_TRUE(std::isfinite(g.gain_block(1)[i]));
        }
        EXPECT_FALSE(g.declared(1));

        // A non-finite statistic trips the same way.
        e[5] = sample_t(0);
        g.analyze(1, e.data(), quiet_nan<sample_t>(), sample_t(0.5));
        EXPECT_EQ(g.watchdog_trips(1), 2u);
        g.analyze(1, e.data(), sample_t(1e-3), infinity<sample_t>());
        EXPECT_EQ(g.watchdog_trips(1), 3u);

        // Recovery: the same finite material declares mic 1 again.
        EXPECT_GE(drive_to_open(g, 1, rng, 4000), 0);
        EXPECT_EQ(g.watchdog_trips(1), 3u);

        // reset() keeps the count.
        g.reset();
        EXPECT_EQ(g.watchdog_trips(1), 3u);
    }

    TEST(WatchdogRtContract, CountersAreNoexcept) {
        static_assert(noexcept(std::declval<const partitioned_fdaf<float>&>().watchdog_trips()));
        static_assert(noexcept(std::declval<const partitioned_fdkf<float>&>().watchdog_trips()));
        static_assert(noexcept(std::declval<const pem_afc<float>&>().watchdog_trips()));
        static_assert(noexcept(std::declval<const residual_suppressor<float>&>().watchdog_trips()));
        static_assert(noexcept(std::declval<const aec_chain<float>&>().watchdog_trips()));
        static_assert(noexcept(std::declval<const howl_guard<float>&>().watchdog_trips(0)));
        SUCCEED();
    }

} // namespace
