// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M2 (HANDOFF.md): the closed-loop simulator and its metrics,
// plus the textbook failure kept as a permanent regression baseline — a
// naive (un-prewhitened) FDAF adapting inside the closed loop biases on
// self-correlated near-end program material.
//
// The claims (ClosedLoopMsg, closed_loop_host_test) run on the band-limited
// room (support/rooms.h) and assert medians over five seed sets; the typed
// closed_loop_test suite is the emulated selection's platform canary, one
// seed. Measured (room 5, 256 taps, d = 128, converge at MSG-6 dB; medians
// over seed sets 0..4, double = float unless both given):
//
//   open loop:            measured MSG median +1.91 dB over -20log10 max|F|,
//                         +0.27 over the phase-exact limit (exact_msg_db,
//                         itself +1.65 over max|F| on this room)
//   naive FDAF, white v:  ASG median +8.23 dB (+3.90..+11.12)
//   naive FDAF, tonal v:  ASG median -15.00 dB, the probe floor (bias
//                         DESTABILIZES the loop at gains the open loop
//                         handles); misalignment median +15.80 / +16.78 dB
//
// Removing that failure is PEM prewhitening's whole job (milestone M3).

#include <cmath>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fdaf.h"
#include "support/closed_loop.h"
#include "support/rooms.h"
#include "tap/dsp/math.h"

namespace {

    using mutap_test::band_limited;
    using mutap_test::closed_loop_sim;
    using mutap_test::k_claim_seed_sets;
    using mutap_test::median;
    using mutap_test::random_decaying_rir;
    using mutap_test::seed_in_set;

    template <typename Sample>
    double misalignment_db(const std::vector<Sample>& truth, const std::vector<Sample>& estimate) {
        double num = 0.0;
        double den = 0.0;
        for (size_t i = 0; i < truth.size(); ++i) {
            const double t = static_cast<double>(truth[i]);
            const double e = (i < estimate.size()) ? static_cast<double>(estimate[i]) : 0.0;
            num += (t - e) * (t - e);
            den += t * t;
        }
        return tap::dsp::power_db(num / den);
    }

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 256; // 4 partitions of 64

    /// The suite's room: synthetic room 5, raw (canaries) or band-limited
    /// (claims).
    template <typename Sample>
    std::vector<Sample> test_room(bool banded) {
        auto path = random_decaying_rir<Sample>(k_taps, 5);
        return banded ? band_limited(path) : path;
    }

    template <typename Sample>
    typename closed_loop_sim<Sample>::config loop_config(const std::vector<Sample>& path, double gain_db = 0.0) {
        typename closed_loop_sim<Sample>::config cfg;
        cfg.feedback_path   = path;
        cfg.block_size      = k_block;
        cfg.forward_delay   = 2 * k_block;
        cfg.forward_gain_db = gain_db;
        return cfg;
    }

    // Converge a fresh naive FDAF inside the closed loop at a safe gain and
    // return it (plus the achieved misalignment) for stability probing.
    template <typename Sample>
    tap::mu::partitioned_fdaf<Sample> converge_naive_canceller(const std::vector<Sample>& path, double gain_db,
                                                               const std::vector<Sample>& v, double* misalignment) {
        typename tap::mu::partitioned_fdaf<Sample>::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_taps / k_block;
        // Pin the M1-era fixed-epsilon normalizer: this baseline documents
        // the UNMITIGATED closed-loop bias. M4's variable regularization
        // (relative_regularization, on by default) softens the tonal
        // catastrophe on its own (measured ASG -12 -> -3.5 dB) — still
        // destabilizing, but enough to make the howl-below-MSG assertion
        // precision-dependent.
        cfg.relative_regularization = Sample(0);
        tap::mu::partitioned_fdaf<Sample> fdaf(cfg);

        closed_loop_sim<Sample> sim(loop_config(path, gain_db));
        for (size_t blk = 0; blk < v.size() / k_block; ++blk) {
            sim.step(&v[blk * k_block], &fdaf);
        }
        if (misalignment != nullptr) {
            std::vector<Sample> ir(fdaf.filter_length());
            fdaf.copy_impulse_response(ir.data());
            *misalignment = misalignment_db(path, ir);
        }
        return fdaf;
    }

    /// Naive canceller on white near-end: ASG over max|F| for seed set `set`.
    template <typename Sample>
    double naive_white_asg(const std::vector<Sample>& path, unsigned set) {
        const double open_msg   = mutap_test::theoretical_msg_db(path);
        const auto   v_converge = mutap_test::white_near_end<Sample>(600 * k_block, seed_in_set(2, set));
        auto         fdaf       = converge_naive_canceller<Sample>(path, open_msg - 6.0, v_converge, nullptr);
        const auto   v_probe    = mutap_test::white_near_end<Sample>(400 * k_block, seed_in_set(3, set));
        return mutap_test::measured_msg_db<Sample>(loop_config(path), &fdaf, v_probe, open_msg - 12.0, open_msg + 25.0,
                                                   0.5)
               - open_msg;
    }

    /// Naive canceller on tonal near-end: {misalignment, ASG over max|F|}.
    template <typename Sample>
    std::pair<double, double> naive_tonal(const std::vector<Sample>& path, unsigned set) {
        const double open_msg     = mutap_test::theoretical_msg_db(path);
        const auto   v_converge   = mutap_test::tonal_near_end<Sample>(600 * k_block, seed_in_set(2, set));
        double       misalignment = 0.0;
        auto         fdaf         = converge_naive_canceller<Sample>(path, open_msg - 6.0, v_converge, &misalignment);
        const auto   v_probe      = mutap_test::tonal_near_end<Sample>(600 * k_block, seed_in_set(3, set));
        const double asg = mutap_test::measured_msg_db<Sample>(loop_config(path), &fdaf, v_probe, open_msg - 15.0,
                                                               open_msg + 25.0, 0.5)
                           - open_msg;
        return {misalignment, asg};
    }

    // ------------------------------------------------------------ claims

    // The bisected open-loop MSG against both analytic references, on the
    // band-limited room, medians over seed sets 0..4 of the white probe
    // (0.53 s). Against the magnitude bound max|F| it sits above by the
    // phase condition's slack (measured median +1.91 dB; the exact limit
    // itself is +1.65 over max|F| here); against the phase-exact limit it
    // sits just above (a short probe over-reads a slowly growing loop;
    // measured median +0.27 dB), and must not sit below it. (Raw room, for
    // the record: +0.94 over max|F|, where the old comment said 0.35.)
    TEST(ClosedLoopMsg, MeasuredOpenLoopMsgMatchesTheory) {
        const auto          path   = test_room<double>(true);
        const double        theory = mutap_test::theoretical_msg_db(path);
        const double        exact  = mutap_test::exact_msg_db(path, 2 * k_block);
        std::vector<double> over_theory;
        std::vector<double> over_exact;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto   v = mutap_test::white_near_end<double>(400 * k_block, seed_in_set(1, set));
            const double msg =
                mutap_test::measured_msg_db<double>(loop_config(path), nullptr, v, theory - 10.0, theory + 15.0, 0.05);
            over_theory.push_back(msg - theory);
            over_exact.push_back(msg - exact);
        }
        RecordProperty("exact_minus_theory_db", exact - theory);
        RecordProperty("median_msg_minus_theory_db", median(over_theory));
        RecordProperty("median_msg_minus_exact_db", median(over_exact));
        EXPECT_NEAR(median(over_theory), 0.0, 3.0) << "measured median +1.91 dB over max|F|";
        EXPECT_GE(median(over_exact), 0.0) << "the bisected limit should not sit below the exact one (measured +0.27)";
        EXPECT_LE(median(over_exact), 1.0) << "measured median +0.27 dB over the exact limit";
    }

    template <typename Sample>
    class closed_loop_host_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(closed_loop_host_test, sample_types);

    // Stable 3 dB below the phase-exact limit, howling 3 dB above it, on
    // every seed set. The open loop is LTI, so the margins (the bisected
    // limit sits a median +0.27 dB over the exact one) are not a chaotic
    // quantity.
    TYPED_TEST(closed_loop_host_test, OpenLoopStableBelowMsgHowlsAbove) {
        const auto   path         = test_room<TypeParam>(true);
        const double exact        = mutap_test::exact_msg_db(path, 2 * k_block);
        int          below_stable = 0;
        int          above_howls  = 0;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto                 v = mutap_test::white_near_end<TypeParam>(400 * k_block, seed_in_set(1, set));
            closed_loop_sim<TypeParam> below(loop_config(path, exact - 3.0));
            below_stable += mutap_test::loop_howls<TypeParam>(below, nullptr, v) ? 0 : 1;
            closed_loop_sim<TypeParam> above(loop_config(path, exact + 3.0));
            above_howls += mutap_test::loop_howls<TypeParam>(above, nullptr, v) ? 1 : 0;
        }
        EXPECT_EQ(below_stable, static_cast<int>(k_claim_seed_sets));
        EXPECT_EQ(above_howls, static_cast<int>(k_claim_seed_sets));
    }

    // Benign case: white near-end is uncorrelated with the (delayed) loop
    // signal, so the naive closed-loop estimate is unbiased and the
    // canceller ADDS stable gain. Measured ASG median +8.23 dB (per set
    // +3.90..+11.12), float = double.
    TYPED_TEST(closed_loop_host_test, NaiveCancellerAddsStableGainOnWhiteNearEnd) {
        const auto          path = test_room<TypeParam>(true);
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            asg.push_back(naive_white_asg(path, set));
        }
        this->RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 3.0) << "naive canceller failed to add stable gain on white near-end";
    }

    // THE M2 REGRESSION BASELINE. Self-correlated (tonal) near-end biases
    // the naive closed-loop estimate so badly that the "canceller" is
    // DESTABILIZING: its maximum stable gain sits BELOW the open-loop MSG
    // (measured median -15.00 dB, the probe's floor; per set
    // -15.00..-9.69), and the estimate is worse than the zero filter
    // (misalignment median +15.80 double / +16.78 float). PEM prewhitening
    // (M3) exists to remove exactly this failure. Bisected MSGs, not single
    // stability probes: the biased loop limit-cycles chaotically.
    TYPED_TEST(closed_loop_host_test, NaiveCancellerDestabilizesOnTonalNearEnd) {
        const auto          path = test_room<TypeParam>(true);
        std::vector<double> mis;
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto [m, a] = naive_tonal(path, set);
            mis.push_back(m);
            asg.push_back(a);
        }
        this->RecordProperty("median_misalignment_db", median(mis));
        this->RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(mis), 0.0) << "biased estimate should be worse than the zero filter";
        EXPECT_LT(median(asg), -1.0) << "expected the biased canceller's MSG to sit below the open loop's";
    }

    // ------------------------------------------------------------ canaries
    //
    // closed_loop_test is the emulated selection's platform canary (float
    // /0 on the Cortex-M55, Cortex-M33 and Hexagon legs): one seed (set 0),
    // checking that the target's arithmetic tracks the host. The claims
    // are the host tests above.

    template <typename Sample>
    class closed_loop_test : public ::testing::Test {};

    TYPED_TEST_SUITE(closed_loop_test, sample_types);

    // CANARY, raw path: the band-limited room's +3 dB probe sits only
    // 1.09 dB over its bisected limit (the raw room's 2.06 dB), under the
    // ~4 dB a canary needs.
    TYPED_TEST(closed_loop_test, OpenLoopStableBelowMsgHowlsAbove) {
        const auto   path   = test_room<TypeParam>(false);
        const auto   v      = mutap_test::white_near_end<TypeParam>(400 * k_block, 1);
        const double theory = mutap_test::theoretical_msg_db(path);

        closed_loop_sim<TypeParam> below(loop_config(path, theory - 3.0));
        EXPECT_FALSE(mutap_test::loop_howls<TypeParam>(below, nullptr, v));

        closed_loop_sim<TypeParam> above(loop_config(path, theory + 3.0));
        EXPECT_TRUE(mutap_test::loop_howls<TypeParam>(above, nullptr, v));
    }

    // CANARY, raw path: measured ASG +6.50 dB on host, float and double, on
    // either path — a 3.50 dB margin, under the ~4 dB a canary needs on
    // the band-limited path, so it stays where it was.
    TYPED_TEST(closed_loop_test, NaiveCancellerAddsStableGainOnWhiteNearEnd) {
        EXPECT_GT(naive_white_asg(test_room<TypeParam>(false), 0), 3.0)
            << "naive canceller failed to add stable gain on white near-end";
    }

    // CANARY, band-limited path (margins are wide): measured misalignment
    // +15.60 (float) / +15.80 (double) dB and ASG -15.00 dB, the probe
    // floor, in both precisions on host.
    TYPED_TEST(closed_loop_test, NaiveCancellerDestabilizesOnTonalNearEnd) {
        const auto [mis, asg] = naive_tonal(test_room<TypeParam>(true), 0);
        EXPECT_GT(mis, 0.0) << "biased estimate should be worse than the zero filter";
        EXPECT_LT(asg, -1.0) << "expected the biased canceller's MSG to sit below the open loop's";
    }

} // namespace
