// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M2 (HANDOFF.md): the closed-loop simulator and its metrics,
// plus the textbook failure kept as a permanent regression baseline — a
// naive (un-prewhitened) FDAF adapting inside the closed loop biases on
// self-correlated near-end program material. Measured behavior these tests
// gate (double, defaults, converge at MSG-6 dB):
//
//   open loop:            measured MSG within a couple dB of -20log10 max|F|
//   naive FDAF, white v:  ASG +6..+10 dB  (machinery works; v uncorrelated)
//   naive FDAF, tonal v:  ASG <= -12 dB   (bias DESTABILIZES the loop at
//                                          gains the open loop handles)
//
// Removing that failure is PEM prewhitening's whole job (milestone M3):
// when M3 lands, its canceller must pass the white-style assertions on the
// tonal program material that fails here.

#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fdaf.h"
#include "support/closed_loop.h"

namespace {

    using mutap_test::closed_loop_sim;

    // Random unit-energy FIR with an exponentially decaying envelope — the
    // stand-in room impulse response used as the true feedback path F.
    template <typename Sample>
    std::vector<Sample> random_decaying_rir(size_t taps, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              f(taps);
        double                           energy = 0.0;
        for (size_t i = 0; i < taps; ++i) {
            const double v = dist(gen) * std::exp(-static_cast<double>(i) / (static_cast<double>(taps) / 4.0));
            f[i]           = static_cast<Sample>(v);
            energy += v * v;
        }
        for (auto& v : f) {
            v = static_cast<Sample>(static_cast<double>(v) / std::sqrt(energy));
        }
        return f;
    }

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
        return 10.0 * std::log10(num / den);
    }

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 256; // 4 partitions of 64

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
    mutap::partitioned_fdaf<Sample> converge_naive_canceller(const std::vector<Sample>& path, double gain_db,
                                                             const std::vector<Sample>& v, double* misalignment) {
        typename mutap::partitioned_fdaf<Sample>::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_taps / k_block;
        mutap::partitioned_fdaf<Sample> fdaf(cfg);

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

    template <typename Sample>
    class closed_loop_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(closed_loop_test, sample_types);

    // The measured maximum stable gain must sit near the loop-magnitude
    // bound -20log10 max|F(w)| — the delay-rich loop phase makes the bound
    // nearly tight (measured 0.35 dB above it for this path).
    TEST(ClosedLoopMsg, MeasuredOpenLoopMsgMatchesTheory) {
        const auto   path   = random_decaying_rir<double>(k_taps, 5);
        const auto   v      = mutap_test::white_near_end<double>(400 * k_block, 1);
        const double theory = mutap_test::theoretical_msg_db(path);
        const double msg =
            mutap_test::measured_msg_db<double>(loop_config(path), nullptr, v, theory - 10.0, theory + 15.0, 0.25);

        EXPECT_NEAR(msg, theory, 2.0);
    }

    TYPED_TEST(closed_loop_test, OpenLoopStableBelowMsgHowlsAbove) {
        const auto   path   = random_decaying_rir<TypeParam>(k_taps, 5);
        const auto   v      = mutap_test::white_near_end<TypeParam>(400 * k_block, 1);
        const double theory = mutap_test::theoretical_msg_db(path);

        closed_loop_sim<TypeParam> below(loop_config(path, theory - 3.0));
        EXPECT_FALSE(mutap_test::loop_howls<TypeParam>(below, nullptr, v));

        closed_loop_sim<TypeParam> above(loop_config(path, theory + 3.0));
        EXPECT_TRUE(mutap_test::loop_howls<TypeParam>(above, nullptr, v));
    }

    // Benign case: white near-end is uncorrelated with the (delayed) loop
    // signal, so the naive closed-loop estimate is unbiased and the
    // canceller ADDS stable gain (measured +6..+10 dB across step sizes).
    TYPED_TEST(closed_loop_test, NaiveCancellerAddsStableGainOnWhiteNearEnd) {
        const auto   path     = random_decaying_rir<TypeParam>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::white_near_end<TypeParam>(600 * k_block, 2);
        auto       fdaf       = converge_naive_canceller<TypeParam>(path, open_msg - 6.0, v_converge, nullptr);

        const auto   v_probe = mutap_test::white_near_end<TypeParam>(400 * k_block, 3);
        const double msg_c = mutap_test::measured_msg_db<TypeParam>(loop_config(path), &fdaf, v_probe, open_msg - 12.0,
                                                                    open_msg + 25.0, 0.5);

        EXPECT_GT(msg_c - open_msg, 3.0) << "naive canceller failed to add stable gain on white near-end";
    }

    // THE M2 REGRESSION BASELINE. Self-correlated (tonal) near-end biases
    // the naive closed-loop estimate so badly that the "canceller" is
    // DESTABILIZING: the loop howls at a gain 3 dB below the open-loop MSG
    // — a gain that is perfectly stable with no canceller at all (measured:
    // it howls even 12 dB below). The estimate is also worse than the zero
    // filter (misalignment > 0 dB). PEM prewhitening (M3) exists to remove
    // exactly this failure; its canceller must turn this scenario into the
    // white-near-end behavior above.
    TYPED_TEST(closed_loop_test, NaiveCancellerDestabilizesOnTonalNearEnd) {
        const auto   path     = random_decaying_rir<TypeParam>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge   = mutap_test::tonal_near_end<TypeParam>(600 * k_block, 2);
        double     misalignment = 0.0;
        auto       fdaf         = converge_naive_canceller<TypeParam>(path, open_msg - 6.0, v_converge, &misalignment);

        EXPECT_GT(misalignment, 0.0) << "biased estimate should be worse than the zero filter";

        const auto                 v_probe = mutap_test::tonal_near_end<TypeParam>(400 * k_block, 3);
        closed_loop_sim<TypeParam> sim(loop_config(path, open_msg - 3.0));
        auto                       probe = fdaf; // fresh copy, keeps the converged (biased) state
        EXPECT_TRUE(mutap_test::loop_howls<TypeParam>(sim, &probe, v_probe))
            << "expected the biased canceller to destabilize a loop the open loop handles";
    }

} // namespace
