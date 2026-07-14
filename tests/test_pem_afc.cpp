// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M3 pass criteria (HANDOFF.md): PEM prewhitening removes the
// closed-loop bias that the M2 baseline documents. Same loop, same program
// material, same convergence protocol as test_closed_loop.cpp — but where
// the naive canceller is DESTABILIZING on self-correlated near-end (howls
// >= 12 dB below the open-loop MSG), the PEM canceller must behave like the
// benign white-noise case. Measured across seeds and precisions (converge
// 1500 blocks at MSG-6 dB, defaults):
//
//   PEM, tonal near-end:   stable at MSG-3 AND MSG+1; ASG +4.5..+6.8 dB
//   PEM, speech-envelope:  ASG +3.0..+12.6 dB   (naive: +3.2)
//   PEM, voiced (pitch):   ASG +2.7..+4.5 dB    (naive: howls, <= -12)
//   PEM, white:            ASG ~ +7.4 dB        (no regression vs naive)
//
// Thresholds sit well inside those ranges so they gate regressions.

#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/pem_afc.h"
#include "support/closed_loop.h"

namespace {

    using mutap_test::closed_loop_sim;

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
    constexpr size_t k_taps  = 256;

    template <typename Sample>
    typename closed_loop_sim<Sample>::config loop_config(const std::vector<Sample>& path, double gain_db = 0.0) {
        typename closed_loop_sim<Sample>::config cfg;
        cfg.feedback_path   = path;
        cfg.block_size      = k_block;
        cfg.forward_delay   = 2 * k_block;
        cfg.forward_gain_db = gain_db;
        return cfg;
    }

    template <typename Sample>
    typename mutap::pem_afc<Sample>::config pem_config() {
        typename mutap::pem_afc<Sample>::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_taps / k_block;
        return cfg;
    }

    // Converge a PEM canceller inside the closed loop at a safe gain.
    template <typename Sample>
    mutap::pem_afc<Sample> converge_pem(const std::vector<Sample>& path, double gain_db, const std::vector<Sample>& v,
                                        double* misalignment = nullptr) {
        mutap::pem_afc<Sample>  afc(pem_config<Sample>());
        closed_loop_sim<Sample> sim(loop_config(path, gain_db));
        for (size_t blk = 0; blk < v.size() / k_block; ++blk) {
            sim.step(&v[blk * k_block], &afc);
        }
        if (misalignment != nullptr) {
            std::vector<Sample> ir(afc.filter_length());
            afc.copy_impulse_response(ir.data());
            *misalignment = misalignment_db(path, ir);
        }
        return afc;
    }

    template <typename Sample>
    bool howls_with(const std::vector<Sample>& path, const mutap::pem_afc<Sample>& converged, double gain_db,
                    const std::vector<Sample>& v) {
        closed_loop_sim<Sample> sim(loop_config(path, gain_db));
        auto                    probe = converged;
        return mutap_test::loop_howls(sim, &probe, v);
    }

    template <typename Sample>
    class pem_afc_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(pem_afc_test, sample_types);

    // THE M3 HEADLINE, the mirror of the M2 baseline: on the tonal program
    // material where the naive canceller howls 3 dB BELOW the open-loop MSG,
    // the PEM canceller stays stable there, stays stable ABOVE the open-loop
    // MSG, and adds measurable stable gain.
    TYPED_TEST(pem_afc_test, StabilizesTonalNearEndWhereNaiveHowls) {
        const auto   path     = random_decaying_rir<TypeParam>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::tonal_near_end<TypeParam>(1500 * k_block, 2);
        const auto afc        = converge_pem<TypeParam>(path, open_msg - 6.0, v_converge);

        const auto v_probe = mutap_test::tonal_near_end<TypeParam>(400 * k_block, 12);
        EXPECT_FALSE(howls_with(path, afc, open_msg - 3.0, v_probe)) << "unstable at the gain that kills naive";
        EXPECT_FALSE(howls_with(path, afc, open_msg + 1.0, v_probe)) << "no stable gain added over the open loop";

        const double msg_c = mutap_test::measured_msg_db<TypeParam>(loop_config(path), &afc, v_probe, open_msg - 12.0,
                                                                    open_msg + 25.0, 0.5);
        EXPECT_GT(msg_c - open_msg, 1.0) << "ASG too small (measured +4.5..+6.8 dB)";
    }

    // Same fixed-gain, fixed-material comparison as the M2 bias test: PEM's
    // estimate must be dramatically less biased than the naive one
    // (measured ~+5 dB vs ~+18 dB misalignment).
    TEST(PemAfc, ReducesTonalBiasVersusNaive) {
        const auto   path     = random_decaying_rir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);
        const auto   v        = mutap_test::tonal_near_end<double>(1500 * k_block, 2);

        double pem_mis = 0.0;
        converge_pem<double>(path, open_msg - 6.0, v, &pem_mis);

        mutap::partitioned_fdaf<double>::config naive_cfg;
        naive_cfg.block_size = k_block;
        naive_cfg.partitions = k_taps / k_block;
        // M1-era fixed-epsilon naive, same pinning as the M2 baseline test:
        // M4's variable regularization softens the naive bias on its own
        // (misalignment ~+18 -> ~+10 dB), which is mitigation, not the fix.
        naive_cfg.relative_regularization = 0.0;
        mutap::partitioned_fdaf<double> naive(naive_cfg);
        closed_loop_sim<double>         sim(loop_config(path, open_msg - 6.0));
        for (size_t blk = 0; blk < v.size() / k_block; ++blk) {
            sim.step(&v[blk * k_block], &naive);
        }
        std::vector<double> ir(naive.filter_length());
        naive.copy_impulse_response(ir.data());
        const double naive_mis = misalignment_db(path, ir);

        EXPECT_LT(pem_mis, naive_mis - 6.0);
    }

    // Speech-envelope-like (AR-colored) near-end: strongly self-correlated,
    // the case the short-term LP stage exists for.
    TEST(PemAfc, AddsStableGainOnSpeechEnvelopeNearEnd) {
        const auto   path     = random_decaying_rir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::ar_near_end<double>(1500 * k_block, 2);
        const auto afc        = converge_pem<double>(path, open_msg - 6.0, v_converge);

        const auto   v_probe = mutap_test::ar_near_end<double>(400 * k_block, 12);
        const double msg_c   = mutap_test::measured_msg_db<double>(loop_config(path), &afc, v_probe, open_msg - 12.0,
                                                                   open_msg + 25.0, 0.5);
        EXPECT_GT(msg_c - open_msg, 1.0) << "ASG too small (measured +3.0..+12.6 dB)";
    }

    // Voiced (pitch-periodic) near-end: the case the long-term stage of the
    // speech cascade exists for; the naive canceller howls here.
    TEST(PemAfc, AddsStableGainOnVoicedNearEnd) {
        const auto   path     = random_decaying_rir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::voiced_near_end<double>(1500 * k_block, 2, 160);
        const auto afc        = converge_pem<double>(path, open_msg - 6.0, v_converge);

        const auto v_probe = mutap_test::voiced_near_end<double>(400 * k_block, 12, 160);
        EXPECT_FALSE(howls_with(path, afc, open_msg - 3.0, v_probe));

        const double msg_c = mutap_test::measured_msg_db<double>(loop_config(path), &afc, v_probe, open_msg - 12.0,
                                                                 open_msg + 25.0, 0.5);
        EXPECT_GT(msg_c - open_msg, 0.5) << "ASG too small (measured +2.7..+4.5 dB)";
    }

    // No regression where the naive canceller was already fine.
    TEST(PemAfc, MatchesNaiveOnWhiteNearEnd) {
        const auto   path     = random_decaying_rir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::white_near_end<double>(1500 * k_block, 2);
        const auto afc        = converge_pem<double>(path, open_msg - 6.0, v_converge);

        const auto   v_probe = mutap_test::white_near_end<double>(400 * k_block, 12);
        const double msg_c   = mutap_test::measured_msg_db<double>(loop_config(path), &afc, v_probe, open_msg - 12.0,
                                                                   open_msg + 25.0, 0.5);
        EXPECT_GT(msg_c - open_msg, 3.0) << "ASG regression vs naive (~+7.4 dB measured for both)";
    }

    TEST(PemAfcConfigValidation, RejectsBadConfigs) {
        using afc = mutap::pem_afc<float>;

        afc::config cfg     = pem_config<float>();
        cfg.analysis_window = k_block; // < 2 * block_size
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                 = pem_config<float>();
        cfg.analysis_window = 2 * k_block + 1; // not a multiple of block_size
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                 = pem_config<float>();
        cfg.fdaf.block_size = 100; // fdaf validation still applies
        EXPECT_THROW(afc{cfg}, std::invalid_argument);
    }

    // The real-time contract carries through the PEM wrapper: everything
    // after construction is noexcept.
    TEST(PemAfcRtContract, PostConstructionEntryPointsAreNoexcept) {
        using afc = mutap::pem_afc<float>;
        static_assert(noexcept(std::declval<afc&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<afc&>().copy_impulse_response(nullptr)));
        static_assert(noexcept(std::declval<afc&>().reset()));
        static_assert(noexcept(std::declval<afc&>().set_adaptation(false)));
        SUCCEED();
    }

} // namespace
