// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The v2 adaptive core (HANDOFF.md upgrade path): the partitioned-block
// frequency-domain Kalman filter, open-loop and as pem_afc's core
// (PEM-FD-Kalman; Bernardi et al.). What the Kalman buys over the tuned
// NLMS stack, each claim measured before its threshold was set:
//
//   - the mu tradeoff dissolves: at 0 dB SNR the Kalman reaches -15.7 dB
//     misalignment by block 300 where NLMS mu=0.5 sits at -5.9 (fast but
//     shallow) and mu=0.1 at -13.7 (deep but slow to start)
//   - closed loop, NO gating and NO IPC anywhere (band-limited rooms,
//     medians over five seed sets): tonal ASG +8.44/+6.88 (double/float;
//     NLMS-PEM +7.08/+7.66), speech-envelope +24.69, at the +25 dB probe
//     ceiling (NLMS-PEM +9.68)
//   - the music rooms that forced the warped predictor's IPC pairing:
//     warped+Kalman per-room medians +11.25..+12.81 dB across rooms {5..9}
//     and the speech cascade +12.50 on room 9 — with zero
//     adaptation-control config
//   - a +20 dB near-end burst against the ungated converged filter is
//     SURVIVED — the ring-down completes and the loop is quiet again
//     (ungated NLMS is wrecked by the same burst); the opt-in transient
//     floor contains the hit (median worst RMS 10.73) at a measured
//     ~2..6 dB tonal-ASG cost, which is why it defaults off
//
// The open-loop identification tests use the raw synthetic room (an echo
// path, not a loop); kalman_loop_test is the emulated selection's
// platform canary; the other closed-loop tests carry the claims.

#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
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

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 256;

    template <typename Sample>
    std::vector<Sample> white_noise(size_t n, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              x(n);
        for (auto& v : x) {
            v = static_cast<Sample>(dist(gen));
        }
        return x;
    }

    template <typename Sample>
    std::vector<Sample> convolve(const std::vector<Sample>& x, const std::vector<Sample>& f) {
        std::vector<Sample> y(x.size(), Sample(0));
        for (size_t n = 0; n < x.size(); ++n) {
            double       acc  = 0.0;
            const size_t kmax = (n + 1 < f.size()) ? n + 1 : f.size();
            for (size_t k = 0; k < kmax; ++k) {
                acc += static_cast<double>(f[k]) * static_cast<double>(x[n - k]);
            }
            y[n] = static_cast<Sample>(acc);
        }
        return y;
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
        return tap::dsp::power_db(num / den);
    }

    template <typename Sample>
    typename tap::mu::partitioned_fdkf<Sample>::config kalman_config() {
        typename tap::mu::partitioned_fdkf<Sample>::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_taps / k_block;
        return cfg;
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
    using kalman_pem = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;
    template <typename Sample>
    using kalman_pem_warped =
        tap::mu::pem_afc<Sample, tap::mu::warped_lpc_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;

    template <typename Sample>
    typename kalman_pem<Sample>::config kalman_pem_config() {
        typename kalman_pem<Sample>::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_taps / k_block;
        return cfg;
    }

    template <typename Sample>
    class fd_kalman_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(fd_kalman_test, sample_types);

    // Open-loop identification, noiseless: fast early convergence AND a
    // deep floor with the default config (no step size to choose).
    // Measured: -46 dB at block 50 in both precisions; -129 dB (double) /
    // -126 dB (float) at block 600 — the process-noise floor, far beyond
    // any acoustic requirement.
    TYPED_TEST(fd_kalman_test, ConvergesOnWhiteNoiseIdentification) {
        const auto   truth  = random_decaying_rir<TypeParam>(k_taps, 5);
        const size_t blocks = 600;
        const auto   input  = white_noise<TypeParam>(blocks * k_block, 2);
        const auto   d      = convolve(input, truth);

        tap::mu::partitioned_fdkf<TypeParam> kalman(kalman_config<TypeParam>());
        std::vector<TypeParam>               error(k_block);
        std::vector<TypeParam>               ir(kalman.filter_length());
        double                               early = 0.0;
        for (size_t blk = 0; blk < blocks; ++blk) {
            kalman.process_block(&input[blk * k_block], &d[blk * k_block], error.data());
            if (blk == 50) {
                kalman.copy_impulse_response(ir.data());
                early = misalignment_db(truth, ir);
            }
        }
        kalman.copy_impulse_response(ir.data());
        EXPECT_LT(early, -35.0) << "measured -46 dB at block 50";
        EXPECT_LT(misalignment_db(truth, ir), -90.0) << "measured -126..-129 dB at block 600";
    }

    // THE POINT OF THE KALMAN CORE: at low SNR, NLMS must choose between
    // fast (mu = 0.5, shallow: -5.9 dB) and deep (mu = 0.1, slow), and the
    // Kalman gets both without a knob. Measured at block 300, 0 dB SNR:
    // Kalman -15.7 dB vs NLMS mu=0.5 -5.9 dB.
    TEST(FdKalman, BeatsNlmsSpeedDepthTradeoffInNoise) {
        const auto   truth  = random_decaying_rir<double>(k_taps, 5);
        const size_t blocks = 300;
        const auto   input  = white_noise<double>(blocks * k_block, 2);
        const auto   noise  = white_noise<double>(blocks * k_block, 77);
        auto         d      = convolve(input, truth);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] += noise[i]; // 0 dB SNR vs the unit-energy echo path
        }

        tap::mu::partitioned_fdaf<double>::config nc;
        nc.block_size = k_block;
        nc.partitions = k_taps / k_block;
        tap::mu::partitioned_fdaf<double> nlms(nc);
        tap::mu::partitioned_fdkf<double> kalman(kalman_config<double>());

        std::vector<double> error(k_block);
        for (size_t blk = 0; blk < blocks; ++blk) {
            nlms.process_block(&input[blk * k_block], &d[blk * k_block], error.data());
            kalman.process_block(&input[blk * k_block], &d[blk * k_block], error.data());
        }
        std::vector<double> ir(kalman.filter_length());
        kalman.copy_impulse_response(ir.data());
        const double kal_mis = misalignment_db(truth, ir);
        nlms.copy_impulse_response(ir.data());
        const double nlms_mis = misalignment_db(truth, ir);

        EXPECT_LT(kal_mis, -12.0) << "measured -15.7 dB";
        EXPECT_LT(kal_mis, nlms_mis - 5.0) << "measured gap 9.8 dB (NLMS mu=0.5: -5.9 dB)";
    }

    // Abrupt path change: the process noise keeps the state uncertainty
    // alive, so a converged filter re-converges without intervention.
    // Measured (20 dB SNR, swap at block 600): -25 dB within 300 blocks of
    // the swap. The first ~50 post-swap blocks are slower than a large-mu
    // NLMS — the sudden residual is indistinguishable from near-end noise
    // until the input-side term outgrows it; that caution is the same
    // property that makes the filter burst-proof.
    TEST(FdKalman, TracksAbruptPathChange) {
        const auto          truth_a = random_decaying_rir<double>(k_taps, 5);
        const auto          truth_b = random_decaying_rir<double>(k_taps, 9);
        const size_t        blocks  = 900;
        const size_t        swap    = 600;
        const auto          input   = white_noise<double>(blocks * k_block, 2);
        const auto          noise   = white_noise<double>(blocks * k_block, 77);
        const auto          d_a     = convolve(input, truth_a);
        const auto          d_b     = convolve(input, truth_b);
        std::vector<double> d(blocks * k_block);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] = ((i < swap * k_block) ? d_a[i] : d_b[i]) + 0.1 * noise[i]; // 20 dB SNR
        }

        tap::mu::partitioned_fdkf<double> kalman(kalman_config<double>());
        std::vector<double>               error(k_block);
        for (size_t blk = 0; blk < blocks; ++blk) {
            kalman.process_block(&input[blk * k_block], &d[blk * k_block], error.data());
        }
        std::vector<double> ir(kalman.filter_length());
        kalman.copy_impulse_response(ir.data());
        EXPECT_LT(misalignment_db(truth_b, ir), -15.0) << "measured -25 dB, 300 blocks after the swap";
    }

    /// Synthetic room `seed`, band-limited: the closed-loop feedback path.
    template <typename Sample>
    std::vector<Sample> loop_room(unsigned seed = 5) {
        return band_limited(random_decaying_rir<Sample>(k_taps, seed));
    }

    /// Converge `afc` 1500 blocks at MSG-6 on v_converge, then bisect its
    /// ASG over max|F| with v_probe.
    template <typename Sample, typename Afc>
    double converge_and_measure(Afc& afc, const std::vector<Sample>& path, const std::vector<Sample>& v_converge,
                                const std::vector<Sample>& v_probe) {
        const double            open_msg = mutap_test::theoretical_msg_db(path);
        closed_loop_sim<Sample> sim(loop_config(path, open_msg - 6.0));
        for (size_t blk = 0; blk < 1500; ++blk) {
            sim.step(&v_converge[blk * k_block], &afc);
        }
        return mutap_test::measured_msg_db(loop_config(path), &afc, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
               - open_msg;
    }

    /// Kalman-PEM tonal ASG for seed set `set` (converge seed 2, probe 12).
    template <typename Sample>
    double kalman_tonal_asg(const std::vector<Sample>& path, unsigned set) {
        kalman_pem<Sample> pem(kalman_pem_config<Sample>());
        return converge_and_measure(pem, path, mutap_test::tonal_near_end<Sample>(1500 * k_block, seed_in_set(2, set)),
                                    mutap_test::tonal_near_end<Sample>(600 * k_block, seed_in_set(12, set)));
    }

    template <typename Sample>
    class kalman_loop_host_test : public ::testing::Test {};
    TYPED_TEST_SUITE(kalman_loop_host_test, sample_types);

    // Closed loop, tonal near-end — the M3 headline scenario, now with the
    // Kalman core and NOT ONE adaptation-control knob. Measured ASG median
    // +8.44 (double, per set +6.56..+11.56) / +6.88 (float, +6.88..+10.62);
    // the NLMS-PEM stack measures +7.08 / +7.66 (test_pem_afc.cpp).
    TYPED_TEST(kalman_loop_host_test, PemAddsStableGainOnTonal) {
        const auto          path = loop_room<TypeParam>();
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            asg.push_back(kalman_tonal_asg(path, set));
        }
        this->RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 3.0) << "measured median +8.44 (double) / +6.88 (float)";
    }

    // CANARY (kalman_loop_test/0 runs in the emulated selection):
    // band-limited room, one seed (set 0). Measured on host +10.62 (float) /
    // +11.56 (double) against the threshold of 2, a margin wide enough for
    // the band-limited path. The claim is kalman_loop_host_test above.
    template <typename Sample>
    class kalman_loop_test : public ::testing::Test {};
    TYPED_TEST_SUITE(kalman_loop_test, sample_types);

    TYPED_TEST(kalman_loop_test, PemAddsStableGainOnTonal) {
        EXPECT_GT(kalman_tonal_asg(loop_room<TypeParam>(), 0), 2.0) << "measured +10.62 (float) / +11.56 (double)";
    }

    // Broadband near-end: the Kalman-PEM reaches the +25 dB probe ceiling
    // on speech-envelope material (measured median +24.69, per set
    // +23.44..+24.69; the NLMS stack +9.68). Asserted well below the
    // ceiling so the claim is about the canceller, not the probe bound.
    TEST(KalmanPem, HugeGainOnBroadbandNearEnd) {
        const auto          path = loop_room<double>();
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            kalman_pem<double> pem(kalman_pem_config<double>());
            asg.push_back(converge_and_measure(pem, path,
                                               mutap_test::ar_near_end<double>(1500 * k_block, seed_in_set(2, set)),
                                               mutap_test::ar_near_end<double>(600 * k_block, seed_in_set(12, set))));
        }
        RecordProperty("median_asg_db", median(asg));
        EXPECT_GT(median(asg), 15.0) << "measured median +24.69 dB (the probe search ceiling)";
    }

    // The music rooms that exposed the warped predictor's runaway and
    // forced its IPC pairing (see test_pem_afc.cpp): with the Kalman core
    // there is no IPC machinery at all, and nothing collapses — per-room
    // medians over seed sets 0..4 on the band-limited rooms {5..9}: warped
    // +12.19/+12.81/+12.19/+12.81/+11.25 dB (lowest single set +10.00), and
    // the speech cascade +12.50 on room 9.
    TEST(KalmanPem, RobustOnMusicAcrossRooms) {
        auto music_asg = [](auto make, unsigned room) {
            const auto          path = loop_room<double>(room);
            std::vector<double> asg;
            for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
                auto afc = make();
                asg.push_back(converge_and_measure(
                    afc, path, mutap_test::music_near_end<double>(1500 * k_block, seed_in_set(2, set)),
                    mutap_test::music_near_end<double>(600 * k_block, seed_in_set(12, set))));
            }
            return median(asg);
        };
        auto make_warped = [] {
            typename kalman_pem_warped<double>::config wc;
            wc.fdaf.block_size = k_block;
            wc.fdaf.partitions = k_taps / k_block;
            return kalman_pem_warped<double>(wc);
        };
        for (const unsigned room : {5U, 6U, 7U, 8U, 9U}) {
            const double med = music_asg(make_warped, room);
            RecordProperty("median_warped_asg_db_room_" + std::to_string(room), med);
            EXPECT_GT(med, 6.0) << "room " << room << " (measured medians >= +11.25 dB)";
        }
        const double speech = music_asg([] { return kalman_pem<double>(kalman_pem_config<double>()); }, 9U);
        RecordProperty("median_speech_asg_db_room_9", speech);
        EXPECT_GT(speech, 5.0) << "room 9, speech cascade (measured median +12.50)";
    }

    // A +20 dB near-end burst against the converged filter. The PEAK of the
    // ungated hit is a chaotic-trajectory quantity (measured 16,000..46,000
    // across seed sets and precisions), so this test asserts the
    // platform-robust DIRECTIONS, as medians over seed sets 0..4:
    //
    //  - ungated, the estimate SURVIVES: the ring-down completes and the
    //    same loop is quiet again (worst RMS over blocks 450..550 after
    //    the burst: median 0.35; one set in five, 264, still ringing); the
    //    ungated NLMS filter is wrecked by the same burst — the M4 test
    //    documents that.
    //  - the opt-in transient floor CONTAINS the hit outright (median worst
    //    RMS 10.73, per set 9.85..23.11) — at the tonal-ASG cost documented
    //    in fd_kalman.h, which is why it is opt-in.
    TEST(KalmanPem, BurstSurvivedUngatedContainedWithFloor) {
        const auto   path     = loop_room<double>();
        const double open_msg = mutap_test::theoretical_msg_db(path);

        auto run = [&](double floor_ratio, unsigned set) {
            auto pc                       = kalman_pem_config<double>();
            pc.fdaf.transient_floor_ratio = floor_ratio;
            kalman_pem<double>      pem(pc);
            closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
            const auto              v = mutap_test::tonal_near_end<double>(2100 * k_block, seed_in_set(2, set));

            double              worst = 0.0;
            double              tail  = 0.0;
            std::vector<double> vb(k_block);
            for (size_t blk = 0; blk < 2100; ++blk) {
                const bool burst = blk >= 1500 && blk < 1550;
                for (size_t i = 0; i < k_block; ++i) {
                    vb[i] = v[blk * k_block + i] * (burst ? 10.0 : 1.0);
                }
                const double rms = sim.step(vb.data(), &pem);
                if (blk >= 1500 && blk < 1650 && rms > worst) {
                    worst = rms;
                }
                if (blk >= 2000 && rms > tail) {
                    tail = rms;
                }
            }
            return std::pair<double, double>{worst, tail};
        };

        std::vector<double> ungated_tail;
        std::vector<double> floored_worst;
        std::vector<double> floored_tail;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            ungated_tail.push_back(run(0.0, set).second);
            const auto [worst, tail] = run(8.0, set);
            floored_worst.push_back(worst);
            floored_tail.push_back(tail);
        }
        RecordProperty("median_ungated_tail_rms", median(ungated_tail));
        RecordProperty("median_floored_worst_rms", median(floored_worst));
        RecordProperty("median_floored_tail_rms", median(floored_tail));
        EXPECT_LT(median(ungated_tail), 100.0) << "the loop should be quiet again after the burst (measured 0.35)";
        EXPECT_LT(median(floored_worst), 1000.0) << "measured median 10.73";
        EXPECT_LT(median(floored_tail), 100.0) << "measured median 0.43";
    }

    TEST(FdKalmanConfigValidation, RejectsBadConfigs) {
        using kf = tap::mu::partitioned_fdkf<float>;

        kf::config cfg = kalman_config<float>();
        cfg.block_size = 100; // not a power of 2
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg            = kalman_config<float>();
        cfg.partitions = 0;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg            = kalman_config<float>();
        cfg.transition = 1.5F;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg            = kalman_config<float>();
        cfg.transition = 0.0F;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg                 = kalman_config<float>();
        cfg.noise_smoothing = 1.0F;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg                     = kalman_config<float>();
        cfg.initial_uncertainty = 0.0F;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);

        cfg                       = kalman_config<float>();
        cfg.transient_floor_ratio = -1.0F;
        EXPECT_THROW(kf{cfg}, std::invalid_argument);
    }

    TEST(FdKalmanRtContract, PostConstructionEntryPointsAreNoexcept) {
        using kf = tap::mu::partitioned_fdkf<float>;
        static_assert(noexcept(std::declval<kf&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<kf&>().copy_impulse_response(nullptr)));
        static_assert(noexcept(std::declval<kf&>().reset()));
        static_assert(noexcept(std::declval<kf&>().set_adaptation(false)));
        using kpem = kalman_pem<float>;
        static_assert(noexcept(std::declval<kpem&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<kpem&>().reset()));
        SUCCEED();
    }

} // namespace
