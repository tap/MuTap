// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M3 pass criteria (HANDOFF.md): PEM prewhitening removes the
// closed-loop bias that the M2 baseline documents. Same loop, same program
// material, same convergence protocol as test_closed_loop.cpp — but where
// the naive canceller is DESTABILIZING on self-correlated near-end (howls
// at the probe's -15 dB floor below the open-loop MSG), the PEM canceller
// must behave like the benign white-noise case.
//
// The claims run on band-limited rooms (support/rooms.h) and assert
// medians over five seed sets; pem_afc_test is the emulated selection's
// platform canary. Measured (converge 1500 blocks at MSG-6 dB, defaults,
// room 5 unless stated; ASG over max|F|, medians over seed sets 0..4):
//
//   PEM, tonal near-end:   stable at MSG-3 AND MSG+1 in every set; ASG
//                          median +7.08 (double) / +7.66 (float)
//   PEM, speech-envelope:  ASG median +9.68 dB
//   PEM, voiced (pitch):   ASG median +3.61 dB   (naive: howls)
//   PEM, white:            ASG median +9.68 dB   (no regression vs naive)
//   warped+IPC, music:     per-room medians +6.88..+9.38 dB, rooms 5..9
//
// Thresholds sit well inside those numbers so they gate regressions.

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/pem_afc.h"
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
    constexpr size_t k_taps  = 256;

    /// Synthetic room `seed`, raw (canaries) or band-limited (claims).
    template <typename Sample>
    std::vector<Sample> test_room(bool banded, unsigned seed = 5) {
        auto path = random_decaying_rir<Sample>(k_taps, seed);
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

    template <typename Sample>
    typename tap::mu::pem_afc<Sample>::config pem_config() {
        typename tap::mu::pem_afc<Sample>::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_taps / k_block;
        return cfg;
    }

    // Converge a PEM canceller inside the closed loop at a safe gain.
    template <typename Sample>
    tap::mu::pem_afc<Sample> converge_pem(const std::vector<Sample>& path, double gain_db, const std::vector<Sample>& v,
                                          double* misalignment = nullptr) {
        tap::mu::pem_afc<Sample> afc(pem_config<Sample>());
        closed_loop_sim<Sample>  sim(loop_config(path, gain_db));
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
    bool howls_with(const std::vector<Sample>& path, const tap::mu::pem_afc<Sample>& converged, double gain_db,
                    const std::vector<Sample>& v) {
        closed_loop_sim<Sample> sim(loop_config(path, gain_db));
        auto                    probe = converged;
        return mutap_test::loop_howls(sim, &probe, v);
    }

    /// One seed set of a PEM scenario: converge 1500 blocks at MSG-6 on
    /// `material` (seed 2 of the set), probe 400 blocks (seed 12).
    struct pem_run {
        bool   howls_at_minus3 = false;
        bool   howls_at_plus1  = false;
        double asg             = 0.0;
    };

    template <typename Sample, typename Material>
    pem_run run_pem(const std::vector<Sample>& path, unsigned set, Material material, double lo_off = -12.0) {
        const double open_msg   = mutap_test::theoretical_msg_db(path);
        const auto   v_converge = material(1500 * k_block, seed_in_set(2, set));
        const auto   afc        = converge_pem<Sample>(path, open_msg - 6.0, v_converge);
        const auto   v_probe    = material(400 * k_block, seed_in_set(12, set));
        pem_run      r;
        r.howls_at_minus3 = howls_with(path, afc, open_msg - 3.0, v_probe);
        r.howls_at_plus1  = howls_with(path, afc, open_msg + 1.0, v_probe);
        r.asg             = mutap_test::measured_msg_db<Sample>(loop_config(path), &afc, v_probe, open_msg + lo_off,
                                                                open_msg + 25.0, 0.5)
                - open_msg;
        return r;
    }

    template <typename Sample>
    auto tonal() {
        return [](size_t n, unsigned seed) { return mutap_test::tonal_near_end<Sample>(n, seed); };
    }

    template <typename Sample>
    class pem_afc_host_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(pem_afc_host_test, sample_types);

    // THE M3 HEADLINE, the mirror of the M2 baseline: on the tonal program
    // material where the naive canceller howls far BELOW the open-loop MSG,
    // the PEM canceller stays stable there, stays stable ABOVE the open-loop
    // MSG, and adds measurable stable gain. Measured on the band-limited
    // room: stable at MSG-3 and MSG+1 in 5 of 5 sets (10 of 10 over sets
    // 0..9) in both precisions; ASG median +7.08 (double, per set
    // +5.05..+7.66) / +7.66 (float, +2.45..+9.68).
    TYPED_TEST(pem_afc_host_test, StabilizesTonalNearEndWhereNaiveHowls) {
        const auto          path          = test_room<TypeParam>(true);
        int                 stable_minus3 = 0;
        int                 stable_plus1  = 0;
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto r = run_pem(path, set, tonal<TypeParam>());
            stable_minus3 += r.howls_at_minus3 ? 0 : 1;
            stable_plus1 += r.howls_at_plus1 ? 0 : 1;
            asg.push_back(r.asg);
        }
        this->RecordProperty("median_asg_db", median(asg));
        EXPECT_GE(stable_minus3, 4) << "unstable at the gain that kills naive (measured stable in 5 of 5 sets)";
        EXPECT_GE(stable_plus1, 4) << "no stable gain added over the open loop (measured stable in 5 of 5 sets)";
        EXPECT_GT(median(asg), 3.0) << "ASG too small (measured median +7.08 double / +7.66 float)";
    }

    // Same fixed-gain, fixed-material comparison as the M2 bias test: PEM's
    // estimate must be clearly less biased than the naive one. The naive
    // trajectory limit-cycles chaotically, so a single run's misalignment
    // is exquisitely sensitive to platform floating-point details; medians
    // over five seeds (2, 12, 22, 32, 42) are the claim. Measured on the
    // band-limited room: PEM median +8.63 dB (per seed +7.93..+9.35), naive
    // median +15.80 (+13.60..+18.33), a 7.17 dB gap.
    TEST(PemAfc, ReducesTonalBiasVersusNaive) {
        const auto   path     = test_room<double>(true);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        std::vector<double> pem_mis;
        std::vector<double> naive_mis;
        for (const unsigned seed : {2U, 12U, 22U, 32U, 42U}) {
            const auto v_pem = mutap_test::tonal_near_end<double>(1500 * k_block, seed);
            double     mis   = 0.0;
            converge_pem<double>(path, open_msg - 6.0, v_pem, &mis);
            pem_mis.push_back(mis);

            tap::mu::partitioned_fdaf<double>::config naive_cfg;
            naive_cfg.block_size = k_block;
            naive_cfg.partitions = k_taps / k_block;
            // M1-era fixed-epsilon naive, same pinning as the M2 baseline
            // test: M4's variable regularization softens the naive bias on
            // its own — mitigation, not the fix.
            naive_cfg.relative_regularization = 0.0;
            tap::mu::partitioned_fdaf<double> naive(naive_cfg);
            closed_loop_sim<double>           sim(loop_config(path, open_msg - 6.0));
            const auto                        v_naive = mutap_test::tonal_near_end<double>(600 * k_block, seed);
            for (size_t blk = 0; blk < v_naive.size() / k_block; ++blk) {
                sim.step(&v_naive[blk * k_block], &naive);
            }
            std::vector<double> ir(naive.filter_length());
            naive.copy_impulse_response(ir.data());
            naive_mis.push_back(misalignment_db(path, ir));
        }
        RecordProperty("median_pem_misalignment_db", median(pem_mis));
        RecordProperty("median_naive_misalignment_db", median(naive_mis));
        EXPECT_LT(median(pem_mis), 11.0) << "PEM median misalignment (measured +8.63 dB)";
        EXPECT_LT(median(pem_mis), median(naive_mis) - 3.0) << "PEM median vs naive median (measured gap 7.17 dB)";
    }

    // Speech-envelope-like (AR-colored) near-end: strongly self-correlated,
    // the case the short-term LP stage exists for. Measured ASG median
    // +9.68 dB (per set +7.08..+14.88).
    TEST(PemAfc, AddsStableGainOnSpeechEnvelopeNearEnd) {
        const auto          path = test_room<double>(true);
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            asg.push_back(run_pem(path, set, [](size_t n, unsigned seed) {
                              return mutap_test::ar_near_end<double>(n, seed);
                          }).asg);
        }
        RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 4.0) << "ASG too small (measured median +9.68 dB)";
    }

    // Voiced (pitch-periodic) near-end: the case the long-term stage of the
    // speech cascade exists for; the naive canceller howls here. Measured:
    // stable at MSG-3 in 5 of 5 sets; ASG median +3.61 dB (per set
    // +1.59..+8.52).
    TEST(PemAfc, AddsStableGainOnVoicedNearEnd) {
        const auto          path   = test_room<double>(true);
        int                 stable = 0;
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto r = run_pem(
                path, set, [](size_t n, unsigned seed) { return mutap_test::voiced_near_end<double>(n, seed, 160); });
            stable += r.howls_at_minus3 ? 0 : 1;
            asg.push_back(r.asg);
        }
        RecordProperty("median_asg_db", median(asg));
        EXPECT_GE(stable, 4) << "unstable at MSG-3 (measured stable in 5 of 5 sets)";
        EXPECT_GT(median(asg), 0.5) << "ASG too small (measured median +3.61 dB)";
    }

    // THE MUSIC PREDICTOR'S CLAIM (HANDOFF.md near-end-model decision): a
    // low chord — three incommensurate fundamentals, a dozen partials
    // packed below 500 Hz — defeats both the pitch tap (no common period)
    // and a moderate-order plain LP (the poles crowd z = 1). The warped
    // whitener NEEDS IPC-scaled stepping: without it the converged update
    // runs away on some rooms — a spurious resonance in the partial band
    // that live adaptation reinforces — and the loop howls 15 dB below the
    // open-loop MSG (~1 room in 5, at every (lambda, order) tried, found on
    // the raw rooms). With it, no room collapses: per-room medians over
    // seed sets 0..4 on the band-limited rooms {5..9} measure
    // +9.38/+8.12/+6.88/+8.12/+6.88 dB (lowest single set +4.06, room 9);
    // over sets 0..9 on room 9 it never reached the -15 dB probe floor,
    // where the speech cascade at its defaults did once in ten.
    TEST(PemAfc, WarpedPredictorRobustOnMusicAcrossRooms) {
        for (const unsigned room : {5U, 6U, 7U, 8U, 9U}) {
            const auto          path     = test_room<double>(true, room);
            const double        open_msg = mutap_test::theoretical_msg_db(path);
            std::vector<double> asg;
            for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
                const auto v_converge = mutap_test::music_near_end<double>(1500 * k_block, seed_in_set(2, set));
                const auto v_probe    = mutap_test::music_near_end<double>(600 * k_block, seed_in_set(12, set));

                tap::mu::pem_afc<double, tap::mu::warped_lpc_predictor<double>>::config wc;
                wc.fdaf.block_size       = k_block;
                wc.fdaf.partitions       = k_taps / k_block;
                wc.fdaf.ipc_step_scaling = true;
                tap::mu::pem_afc<double, tap::mu::warped_lpc_predictor<double>> warped(wc);

                closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
                for (size_t blk = 0; blk < 1500; ++blk) {
                    sim.step(&v_converge[blk * k_block], &warped);
                }
                asg.push_back(mutap_test::measured_msg_db(loop_config(path), &warped, v_probe, open_msg - 15.0,
                                                          open_msg + 25.0, 0.5)
                              - open_msg);
            }
            RecordProperty("median_asg_db_room_" + std::to_string(room), median(asg));
            EXPECT_GT(median(asg), 4.0) << "room " << room << " (measured medians >= +6.88 dB)";
        }
    }

    // And the warped predictor (in its documented pairing with IPC-scaled
    // stepping) is safe on the speech-envelope material the cascade was
    // built for — no collapse, and more headroom than the cascade measures
    // there. Measured ASG median +19.38 dB (per set +17.81..+22.50).
    TEST(PemAfc, WarpedPredictorHandlesSpeechEnvelopeToo) {
        const auto          path     = test_room<double>(true);
        const double        open_msg = mutap_test::theoretical_msg_db(path);
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto v_converge = mutap_test::ar_near_end<double>(1500 * k_block, seed_in_set(2, set));
            const auto v_probe    = mutap_test::ar_near_end<double>(600 * k_block, seed_in_set(12, set));

            tap::mu::pem_afc<double, tap::mu::warped_lpc_predictor<double>>::config wc;
            wc.fdaf.block_size       = k_block;
            wc.fdaf.partitions       = k_taps / k_block;
            wc.fdaf.ipc_step_scaling = true;
            tap::mu::pem_afc<double, tap::mu::warped_lpc_predictor<double>> warped(wc);

            closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
            for (size_t blk = 0; blk < 1500; ++blk) {
                sim.step(&v_converge[blk * k_block], &warped);
            }
            asg.push_back(
                mutap_test::measured_msg_db(loop_config(path), &warped, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
                - open_msg);
        }
        RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 10.0) << "measured median +19.38 dB";
    }

    // No regression where the naive canceller was already fine. Measured
    // ASG median +9.68 dB (per set +7.95..+10.26); the naive canceller's
    // median on the same room and material is +8.23 (test_closed_loop.cpp).
    TEST(PemAfc, MatchesNaiveOnWhiteNearEnd) {
        const auto          path = test_room<double>(true);
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            asg.push_back(run_pem(path, set, [](size_t n, unsigned seed) {
                              return mutap_test::white_near_end<double>(n, seed);
                          }).asg);
        }
        RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 5.0) << "ASG regression vs naive (measured median +9.68 dB)";
    }

    // CANARY (pem_afc_test/0 runs in the emulated selection), raw path, one
    // seed: the band-limited room's float ASG at set 0 is +2.45 dB, a 1.45
    // dB margin, so the canary stays on the raw room. Measured on host,
    // raw: ASG +7.66 (float) / +3.90 (double), stable at MSG-3 and MSG+1.
    // The claim is pem_afc_host_test above.
    template <typename Sample>
    class pem_afc_test : public ::testing::Test {};

    TYPED_TEST_SUITE(pem_afc_test, sample_types);

    TYPED_TEST(pem_afc_test, StabilizesTonalNearEndWhereNaiveHowls) {
        const auto r = run_pem(test_room<TypeParam>(false), 0, tonal<TypeParam>());
        EXPECT_FALSE(r.howls_at_minus3) << "unstable at the gain that kills naive";
        EXPECT_FALSE(r.howls_at_plus1) << "no stable gain added over the open loop";
        EXPECT_GT(r.asg, 1.0) << "ASG too small (measured +7.66 float / +3.90 double)";
    }

    TEST(PemAfcConfigValidation, RejectsBadConfigs) {
        using afc = tap::mu::pem_afc<float>;

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
    // after construction is noexcept — for the warped instantiation too.
    TEST(PemAfcRtContract, PostConstructionEntryPointsAreNoexcept) {
        using wafc = tap::mu::pem_afc<float, tap::mu::warped_lpc_predictor<float>>;
        static_assert(noexcept(std::declval<wafc&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<wafc&>().reset()));
        using afc = tap::mu::pem_afc<float>;
        static_assert(noexcept(std::declval<afc&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<afc&>().copy_impulse_response(nullptr)));
        static_assert(noexcept(std::declval<afc&>().reset()));
        static_assert(noexcept(std::declval<afc&>().set_adaptation(false)));
        SUCCEED();
    }

} // namespace
