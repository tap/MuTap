// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Open-loop acoustic echo cancellation (AEC — HANDOFF.md "The next effort",
// Stage 1). The cores were built for the closed feedback loop, but the AEC
// paper the PEM structure implements (Gil-Cacho et al. 2014) is an OPEN-loop
// double-talk framework: pem_afc's process_block(x, y, e) is already the AEC
// signature, with the far-end reference x exogenous instead of looped back.
// These tests pin the open-loop behavior the AEC external will ship on.
//
// Protocol (echo_sim, block 64, 16 partitions over the first 1024 taps of
// the room): 1500 blocks far-end single-talk (converge; measure the last
// 400), 600 blocks of double-talk (near-end at unit RMS — 0 dB relative to
// the far-end), 600 blocks single-talk again (recovery; measure the last
// 400). Far-end is the speech-envelope (AR-colored) material unless a test
// says otherwise. Measured on the studio fixture room, seeds {2,12,22}
// (medians in [] — open-loop trajectories are far tamer than closed-loop
// ones, but medians still absorb the platform libm/FMA spread):
//
//   variant       st ERLE   st misalign   dt suppression   post-dt misalign
//   naive NLMS    38..44      -1.0..-2.6     6.0..6.5 [6.1]   +2.1..+5.9 [+3.4]
//   gated NLMS    39..40      -2.1           15.5..16.4       kick +-0.1
//   PEM-NLMS      19..20     -19..-23        7.4..7.7 [7.4]   -8.1..-8.6 [-8.5]
//   PEM-Kalman    18..19     -19..-20       13.1..14.1 [13.4] -13.1..-14.0 [-13.3]
//
// The two headline shapes those numbers pin:
//  1. Double-talk kicks the naive estimate PAST useless (misalignment goes
//     positive: subtracting its echo estimate adds energy) while the PEM
//     variants stay deeply useful and the Kalman core barely moves — with
//     zero adaptation-control configuration (no gate, no DTD).
//  2. The naive filter posts the best single-talk ERLE (excitation-weighted
//     convergence on colored far-end buys ~44 dB where PEM's whitened
//     update measures ~20) at the price of a shallow, biased-looking
//     uniform estimate (-2 dB) that double-talk then destroys. That trade
//     is the AEC default-engine story and the book chapter's opening.
//
// White far-end, single-talk, studio room, seeds {2,12,22} (typed suite;
// f32 and f64 measured within 1.5 dB of each other everywhere):
//
//   PEM-NLMS    ERLE 25.0..25.7   misalignment -24.6..-26.2
//   PEM-Kalman  ERLE 18.4..23.8   misalignment -20.5..-23.8
//
// Music-material double-talk (Kalman core, speech vs warped predictor,
// rooms {synth9, studio} x seeds {2,12,22}): the warped predictor won the
// double-talk suppression in all 18 room x seed pairs measured across six
// rooms (min gap 1.0 dB); per-room medians: synth9 19.6 vs 16.7, studio
// 17.3 vs 15.2. Thresholds sit inside those numbers to gate regressions.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/echo_scenario.h"

namespace {

    using mutap_test::echo_sim;
    using mutap_test::run_aec;

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 1024; ///< first 21 ms of the room: a practical AEC length
    constexpr size_t k_parts = k_taps / k_block;

    // Same synthetic-room generator family as the closed-loop tests.
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

    // The physically-modeled family: first k_taps of a fixture room,
    // re-normalized to unit energy (same conditioning as test_rir_fixtures).
    template <typename Sample>
    std::vector<Sample> studio_path() {
        std::vector<double> f(mutap_test::fixtures::k_rir_studio, mutap_test::fixtures::k_rir_studio + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        std::vector<Sample> out(k_taps);
        for (size_t i = 0; i < k_taps; ++i) {
            out[i] = static_cast<Sample>(f[i] / std::sqrt(energy));
        }
        return out;
    }

    using naive_t  = mutap::partitioned_fdaf<double>;
    using pem_t    = mutap::pem_afc<double>;
    using kalman_t = mutap::pem_afc<double, mutap::speech_predictor<double>, mutap::partitioned_fdkf<double>>;
    using warped_kalman_t =
        mutap::pem_afc<double, mutap::warped_lpc_predictor<double>, mutap::partitioned_fdkf<double>>;

    template <typename Core>
    typename Core::config core_config() {
        typename Core::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_parts;
        return cfg;
    }

    template <typename Afc>
    Afc make_pem() {
        typename Afc::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_parts;
        return Afc(cfg);
    }

    template <typename Sample, typename Canceller>
    double mis_db(const std::vector<Sample>& path, Canceller& c) {
        std::vector<Sample> ir(c.filter_length());
        c.copy_impulse_response(ir.data());
        return mutap_test::misalignment_db(path, ir);
    }

    struct dt_result {
        double st_erle_db        = 0.0; ///< single-talk ERLE, last 400 converge blocks
        double mis_converged_db  = 0.0;
        double dt_suppression_db = 0.0; ///< true echo suppression during double-talk
        double mis_after_dt_db   = 0.0;
        double rec_erle_db       = 0.0; ///< recovery ERLE, last 400 blocks
        double mis_recovered_db  = 0.0;
    };

    /// The double-talk protocol from the header comment. near_end() maps
    /// (length, seed) to the double-talk material.
    template <typename Canceller, typename NearEnd>
    dt_result run_double_talk(const std::vector<double>& path, Canceller& c, unsigned seed, NearEnd&& near_end) {
        echo_sim<double>::config scfg;
        scfg.echo_path  = path;
        scfg.block_size = k_block;
        echo_sim<double> sim(scfg);
        dt_result        out;

        const auto x1        = mutap_test::ar_near_end<double>(1500 * k_block, seed);
        out.st_erle_db       = run_aec(sim, x1, nullptr, &c, 1100).erle_db;
        out.mis_converged_db = mis_db(path, c);

        const auto x2         = mutap_test::ar_near_end<double>(600 * k_block, seed + 100);
        const auto v2         = near_end(600 * k_block, seed + 200);
        out.dt_suppression_db = run_aec(sim, x2, &v2, &c, 0).suppression_db;
        out.mis_after_dt_db   = mis_db(path, c);

        const auto x3        = mutap_test::ar_near_end<double>(600 * k_block, seed + 300);
        out.rec_erle_db      = run_aec(sim, x3, nullptr, &c, 200).erle_db;
        out.mis_recovered_db = mis_db(path, c);
        return out;
    }

    double median3(std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[1];
    }

    constexpr unsigned k_seeds[] = {2U, 12U, 22U};

    // ------------------------------------------------------------ typed suite

    template <typename Sample>
    class aec_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(aec_test, sample_types);

    // White far-end single-talk on the studio room: the best-conditioned
    // open-loop case, both engines, both precisions. Measured ERLE
    // 25.0..25.7 (NLMS) / 18.4..23.8 (Kalman); misalignment -24.6..-26.2 /
    // -20.5..-23.8 — thresholds carry ~5 dB of margin.
    TYPED_TEST(aec_test, SingleTalkWhiteFarEndNlms) {
        const auto path = studio_path<TypeParam>();

        typename echo_sim<TypeParam>::config scfg;
        scfg.echo_path  = path;
        scfg.block_size = k_block;
        echo_sim<TypeParam> sim(scfg);

        auto       afc = make_pem<mutap::pem_afc<TypeParam>>();
        const auto x   = mutap_test::white_near_end<TypeParam>(1500 * k_block, 2);
        const auto r   = run_aec(sim, x, nullptr, &afc, 1100);

        EXPECT_TRUE(r.finite);
        EXPECT_GT(r.erle_db, 20.0) << "measured 25.0..25.7 dB";
        EXPECT_LT(mis_db(path, afc), -20.0) << "measured -24.6..-26.2 dB";
    }

    TYPED_TEST(aec_test, SingleTalkWhiteFarEndKalman) {
        const auto path = studio_path<TypeParam>();

        typename echo_sim<TypeParam>::config scfg;
        scfg.echo_path  = path;
        scfg.block_size = k_block;
        echo_sim<TypeParam> sim(scfg);

        using afc_t = mutap::pem_afc<TypeParam, mutap::speech_predictor<TypeParam>, mutap::partitioned_fdkf<TypeParam>>;
        auto       afc = make_pem<afc_t>();
        const auto x   = mutap_test::white_near_end<TypeParam>(1500 * k_block, 2);
        const auto r   = run_aec(sim, x, nullptr, &afc, 1100);

        EXPECT_TRUE(r.finite);
        EXPECT_GT(r.erle_db, 14.0) << "measured 18.4..23.8 dB";
        EXPECT_LT(mis_db(path, afc), -16.0) << "measured -20.5..-23.8 dB";
    }

    // ---------------------------------------------------------- double talk

    // THE AEC HEADLINE (the open-loop analogue of the M2 bias baseline):
    // 0 dB double-talk kicks the naive estimate past useless — its
    // post-double-talk misalignment goes POSITIVE — while PEM stays deeply
    // useful and the Kalman core barely moves, with zero adaptation-control
    // configuration. Medians over three seeds, studio room; measured values
    // in the file header.
    TEST(AecDoubleTalk, NaiveDivergesWherePemHolds) {
        const auto path    = studio_path<double>();
        const auto ar_near = [](size_t n, unsigned seed) { return mutap_test::ar_near_end<double>(n, seed); };

        std::vector<double> naive_mis;
        std::vector<double> naive_sup;
        std::vector<double> pem_mis;
        std::vector<double> kalman_mis;
        std::vector<double> kalman_sup;
        for (const unsigned seed : k_seeds) {
            naive_t::config naive_cfg = core_config<naive_t>();
            // M1-era fixed-epsilon pinning, like the M2 baseline: the M4
            // robustness layers are measured separately (gated test below).
            naive_cfg.relative_regularization = 0.0;
            naive_t    naive(naive_cfg);
            const auto rn = run_double_talk(path, naive, seed, ar_near);
            naive_mis.push_back(rn.mis_after_dt_db);
            naive_sup.push_back(rn.dt_suppression_db);

            auto       pem = make_pem<pem_t>();
            const auto rp  = run_double_talk(path, pem, seed, ar_near);
            pem_mis.push_back(rp.mis_after_dt_db);

            auto       kalman = make_pem<kalman_t>();
            const auto rk     = run_double_talk(path, kalman, seed, ar_near);
            kalman_mis.push_back(rk.mis_after_dt_db);
            kalman_sup.push_back(rk.dt_suppression_db);
        }

        EXPECT_GT(median3(naive_mis), -1.0) << "naive should be kicked past useless (measured median +3.4 dB)";
        EXPECT_LT(median3(pem_mis), -5.0) << "PEM should hold a useful estimate (measured median -8.5 dB)";
        EXPECT_LT(median3(kalman_mis), -10.0) << "Kalman should barely move (measured median -13.3 dB)";
        EXPECT_GT(median3(kalman_sup), 10.0) << "Kalman double-talk suppression (measured median 13.4 dB)";
        EXPECT_GT(median3(kalman_sup), median3(naive_sup) + 3.0)
            << "Kalman vs naive during double-talk (measured gap ~7 dB)";
    }

    // The M4 gate is the classical answer to double-talk: IPC-scaled
    // stepping plus the transient freeze holds the naive filter completely
    // still through the double-talk segment (measured kick +-0.1 dB,
    // suppression 15.5..16.4 dB) while keeping the naive engine's high
    // excitation-weighted ERLE (measured 39..40 dB).
    TEST(AecDoubleTalk, GatedNlmsFreezesThroughDoubleTalk) {
        const auto path = studio_path<double>();

        naive_t::config cfg        = core_config<naive_t>();
        cfg.ipc_step_scaling       = true;
        cfg.transient_freeze_ratio = 4.0;
        naive_t gated(cfg);

        const auto r = run_double_talk(
            path, gated, 2, [](size_t n, unsigned seed) { return mutap_test::ar_near_end<double>(n, seed); });

        EXPECT_GT(r.st_erle_db, 33.0) << "measured 39.8 dB";
        EXPECT_NEAR(r.mis_after_dt_db, r.mis_converged_db, 1.5) << "measured kick +0.1 dB";
        EXPECT_GT(r.dt_suppression_db, 12.0) << "measured 16.2 dB";
    }

    // Room sweep across BOTH generator families (working note: one room is
    // not an evaluation): the Kalman engine's double-talk behavior holds on
    // synthetic random-decay rooms and the physically-modeled fixture room
    // alike. Measured suppression 13.2..16.0 dB, post-double-talk
    // misalignment -13.7..-14.5 dB across these rooms at seed 2.
    TEST(AecDoubleTalk, KalmanHoldsAcrossRoomFamilies) {
        const auto ar_near = [](size_t n, unsigned seed) { return mutap_test::ar_near_end<double>(n, seed); };

        struct named_room {
            const char*         name;
            std::vector<double> path;
        };
        const named_room rooms[] = {
            {"synth5", random_decaying_rir<double>(k_taps, 5)},
            {"synth9", random_decaying_rir<double>(k_taps, 9)},
            {"studio", studio_path<double>()},
        };
        for (const auto& room : rooms) {
            auto       kalman = make_pem<kalman_t>();
            const auto r      = run_double_talk(room.path, kalman, 2, ar_near);
            EXPECT_GT(r.dt_suppression_db, 10.0) << room.name << " (measured >= 13.2 dB)";
            EXPECT_LT(r.mis_after_dt_db, -10.0) << room.name << " (measured <= -13.7 dB)";
            EXPECT_LT(r.mis_recovered_db, -15.0) << room.name << " recovery (measured <= -19 dB)";
        }
    }

    // THE PREDICTOR STORY ON MUSIC: with the Kalman core, the warped
    // near-end model beat the speech cascade's double-talk suppression in
    // all 18 room x seed pairs measured across six rooms. Pinned here on
    // one room from each generator family, medians over three seeds:
    // synth9 19.6 vs 16.7 dB, studio 17.3 vs 15.2 dB.
    TEST(AecDoubleTalk, WarpedPredictorImprovesMusicDoubleTalk) {
        const auto music = [](size_t n, unsigned seed) { return mutap_test::music_near_end<double>(n, seed); };

        struct named_room {
            const char*         name;
            std::vector<double> path;
        };
        const named_room rooms[] = {
            {"synth9", random_decaying_rir<double>(k_taps, 9)},
            {"studio", studio_path<double>()},
        };
        for (const auto& room : rooms) {
            std::vector<double> speech_sup;
            std::vector<double> warped_sup;
            std::vector<double> warped_mis;
            for (const unsigned seed : k_seeds) {
                auto speech = make_pem<kalman_t>();
                speech_sup.push_back(run_double_talk(room.path, speech, seed, music).dt_suppression_db);

                auto       warped = make_pem<warped_kalman_t>();
                const auto rw     = run_double_talk(room.path, warped, seed, music);
                warped_sup.push_back(rw.dt_suppression_db);
                warped_mis.push_back(rw.mis_after_dt_db);
            }
            EXPECT_GT(median3(warped_sup), median3(speech_sup) + 0.5)
                << room.name << " (measured median gaps 2.9 / 2.1 dB)";
            EXPECT_GT(median3(warped_sup), 15.0) << room.name << " (measured medians 19.6 / 17.3 dB)";
            EXPECT_LT(median3(warped_mis), -5.0) << room.name << " (measured ~ -9 dB)";
        }
    }

    // The naive engine's single-talk ERLE is not a bug — it is the trade
    // the default-engine decision weighs. On colored far-end the naive
    // NLMS converges excitation-weighted (deep where the signal is, ~44 dB
    // ERLE, shallow uniformly at -2.6 dB misalignment); PEM whitens the
    // update and converges uniformly (-20.4 dB) at ~20 dB ERLE. Both
    // directions of that contrast are asserted so neither silently erodes.
    TEST(AecSingleTalk, NaiveTradesUniformDepthForExcitationWeightedErle) {
        const auto path = studio_path<double>();

        echo_sim<double>::config scfg;
        scfg.echo_path  = path;
        scfg.block_size = k_block;

        naive_t::config naive_cfg         = core_config<naive_t>();
        naive_cfg.relative_regularization = 0.0;
        naive_t          naive(naive_cfg);
        echo_sim<double> sim_naive(scfg);
        const auto       x  = mutap_test::ar_near_end<double>(1500 * k_block, 2);
        const auto       rn = run_aec(sim_naive, x, nullptr, &naive, 1100);

        auto             pem = make_pem<pem_t>();
        echo_sim<double> sim_pem(scfg);
        const auto       rp = run_aec(sim_pem, x, nullptr, &pem, 1100);

        EXPECT_GT(rn.erle_db, rp.erle_db + 10.0) << "measured 44.3 vs 20.0 dB";
        EXPECT_LT(mis_db(path, pem), mis_db(path, naive) - 12.0) << "measured -20.4 vs -2.6 dB";
    }

} // namespace
