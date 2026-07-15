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
//   - closed loop, NO gating and NO IPC anywhere: tonal ASG +4.7/+7.8
//     (double/float; NLMS-PEM +4.5..+6.8), speech-envelope and white
//     near-end saturate the +25 dB probe ceiling (NLMS-PEM +3..+12.6),
//     voiced +7.5 (NLMS-PEM +2.7..+4.5)
//   - the music rooms that forced the warped predictor's IPC pairing:
//     warped+Kalman +8.4..+13.1 dB across rooms {5..9} and speech+Kalman
//     +11.6..+13.4 — including room 9 where the NLMS speech cascade
//     DESTABILIZES (-2.2 dB) — with zero adaptation-control config
//   - a +20 dB near-end burst against the ungated converged filter is
//     survived (excursion ~2 dB; ungated NLMS is wrecked) and bounded
//     ~3x better than ungated NLMS; the opt-in transient floor contains
//     it to gated-NLMS quality (worst RMS ~24 vs ~25) at a measured
//     ~2..6 dB tonal-ASG cost, which is why it defaults off

#include <cmath>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"

namespace {

    using mutap_test::closed_loop_sim;

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
    std::vector<Sample> random_decaying_fir(size_t taps, unsigned seed) {
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
        return 10.0 * std::log10(num / den);
    }

    template <typename Sample>
    typename mutap::partitioned_fdkf<Sample>::config kalman_config() {
        typename mutap::partitioned_fdkf<Sample>::config cfg;
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
    using kalman_pem = mutap::pem_afc<Sample, mutap::speech_predictor<Sample>, mutap::partitioned_fdkf<Sample>>;
    template <typename Sample>
    using kalman_pem_warped =
        mutap::pem_afc<Sample, mutap::warped_lpc_predictor<Sample>, mutap::partitioned_fdkf<Sample>>;

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
        const auto   truth  = random_decaying_fir<TypeParam>(k_taps, 5);
        const size_t blocks = 600;
        const auto   input  = white_noise<TypeParam>(blocks * k_block, 2);
        const auto   d      = convolve(input, truth);

        mutap::partitioned_fdkf<TypeParam> kalman(kalman_config<TypeParam>());
        std::vector<TypeParam>             error(k_block);
        std::vector<TypeParam>             ir(kalman.filter_length());
        double                             early = 0.0;
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
        const auto   truth  = random_decaying_fir<double>(k_taps, 5);
        const size_t blocks = 300;
        const auto   input  = white_noise<double>(blocks * k_block, 2);
        const auto   noise  = white_noise<double>(blocks * k_block, 77);
        auto         d      = convolve(input, truth);
        for (size_t i = 0; i < d.size(); ++i) {
            d[i] += noise[i]; // 0 dB SNR vs the unit-energy echo path
        }

        mutap::partitioned_fdaf<double>::config nc;
        nc.block_size = k_block;
        nc.partitions = k_taps / k_block;
        mutap::partitioned_fdaf<double> nlms(nc);
        mutap::partitioned_fdkf<double> kalman(kalman_config<double>());

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
        const auto          truth_a = random_decaying_fir<double>(k_taps, 5);
        const auto          truth_b = random_decaying_fir<double>(k_taps, 9);
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

        mutap::partitioned_fdkf<double> kalman(kalman_config<double>());
        std::vector<double>             error(k_block);
        for (size_t blk = 0; blk < blocks; ++blk) {
            kalman.process_block(&input[blk * k_block], &d[blk * k_block], error.data());
        }
        std::vector<double> ir(kalman.filter_length());
        kalman.copy_impulse_response(ir.data());
        EXPECT_LT(misalignment_db(truth_b, ir), -15.0) << "measured -25 dB, 300 blocks after the swap";
    }

    template <typename Sample>
    class kalman_loop_test : public ::testing::Test {};
    TYPED_TEST_SUITE(kalman_loop_test, sample_types);

    // Closed loop, tonal near-end — the M3 headline scenario, now with the
    // Kalman core and NOT ONE adaptation-control knob. Measured ASG +4.7
    // (double) / +7.8 (float); the NLMS-PEM stack measures +4.5..+6.8.
    TYPED_TEST(kalman_loop_test, PemAddsStableGainOnTonal) {
        const auto   path     = random_decaying_fir<TypeParam>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::tonal_near_end<TypeParam>(1500 * k_block, 2);
        const auto v_probe    = mutap_test::tonal_near_end<TypeParam>(600 * k_block, 12);

        kalman_pem<TypeParam>      pem(kalman_pem_config<TypeParam>());
        closed_loop_sim<TypeParam> sim(loop_config(path, open_msg - 6.0));
        for (size_t blk = 0; blk < 1500; ++blk) {
            sim.step(&v_converge[blk * k_block], &pem);
        }
        const double asg =
            mutap_test::measured_msg_db(loop_config(path), &pem, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
            - open_msg;
        EXPECT_GT(asg, 2.0) << "measured +4.7 (double) / +7.8 (float)";
    }

    // Broadband near-end: the Kalman-PEM saturates the +25 dB probe
    // ceiling on speech-envelope material (the NLMS stack measured
    // +3..+12.6). Asserted well below the ceiling so the claim is about
    // the canceller, not the probe bound.
    TEST(KalmanPem, HugeGainOnBroadbandNearEnd) {
        const auto   path     = random_decaying_fir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        const auto v_converge = mutap_test::ar_near_end<double>(1500 * k_block, 2);
        const auto v_probe    = mutap_test::ar_near_end<double>(600 * k_block, 12);

        kalman_pem<double>      pem(kalman_pem_config<double>());
        closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
        for (size_t blk = 0; blk < 1500; ++blk) {
            sim.step(&v_converge[blk * k_block], &pem);
        }
        const double asg =
            mutap_test::measured_msg_db(loop_config(path), &pem, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
            - open_msg;
        EXPECT_GT(asg, 15.0) << "measured +24.7 dB (the probe search ceiling)";
    }

    // The music rooms that exposed the warped predictor's runaway and
    // forced its IPC pairing (see test_pem_afc.cpp): with the Kalman core
    // there is no IPC machinery at all, and nothing collapses — measured
    // warped +8.4..+13.1 dB across rooms {5..9}, and the speech cascade
    // +13.4 dB on room 9 where its NLMS incarnation destabilizes (-2.2).
    TEST(KalmanPem, RobustOnMusicAcrossRooms) {
        const auto v_converge = mutap_test::music_near_end<double>(1500 * k_block, 2);
        const auto v_probe    = mutap_test::music_near_end<double>(600 * k_block, 12);

        auto converge_and_measure = [&](auto& afc, const std::vector<double>& path, double open_msg) {
            closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
            for (size_t blk = 0; blk < 1500; ++blk) {
                sim.step(&v_converge[blk * k_block], &afc);
            }
            return mutap_test::measured_msg_db(loop_config(path), &afc, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
                   - open_msg;
        };

        for (const unsigned room : {5U, 6U, 7U, 8U, 9U}) {
            const auto   path     = random_decaying_fir<double>(k_taps, room);
            const double open_msg = mutap_test::theoretical_msg_db(path);

            typename kalman_pem_warped<double>::config wc;
            wc.fdaf.block_size = k_block;
            wc.fdaf.partitions = k_taps / k_block;
            kalman_pem_warped<double> warped(wc);
            EXPECT_GT(converge_and_measure(warped, path, open_msg), 4.0) << "room " << room << " (measured >= +8.4 dB)";
        }

        const auto         path     = random_decaying_fir<double>(k_taps, 9);
        const double       open_msg = mutap_test::theoretical_msg_db(path);
        kalman_pem<double> speech(kalman_pem_config<double>());
        EXPECT_GT(converge_and_measure(speech, path, open_msg), 5.0)
            << "room 9, speech cascade (measured +13.4; its NLMS incarnation destabilizes at -2.2)";
    }

    // A +20 dB near-end burst against the converged, UNGATED filter: the
    // noise-PSD tracking bounds the hit (measured worst block RMS ~17600
    // vs ~56000 for ungated NLMS) and the estimate survives (measured
    // excursion ~2 dB; ungated NLMS is wrecked). The opt-in transient
    // floor contains the burst to gated-NLMS quality (measured ~24 vs the
    // M4 gate's ~25) — at the tonal-ASG cost documented in fd_kalman.h,
    // which is why it is opt-in.
    TEST(KalmanPem, BurstBoundedUngatedContainedWithFloor) {
        const auto   path     = random_decaying_fir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        auto run = [&](double floor_ratio) {
            auto pc                       = kalman_pem_config<double>();
            pc.fdaf.transient_floor_ratio = floor_ratio;
            kalman_pem<double>      pem(pc);
            closed_loop_sim<double> sim(loop_config(path, open_msg - 6.0));
            const auto              v = mutap_test::tonal_near_end<double>(1700 * k_block, 2);

            double              worst  = 0.0;
            double              before = 0.0;
            std::vector<double> vb(k_block);
            std::vector<double> ir(pem.filter_length());
            for (size_t blk = 0; blk < 1700; ++blk) {
                const bool burst = blk >= 1500 && blk < 1550;
                for (size_t i = 0; i < k_block; ++i) {
                    vb[i] = v[blk * k_block + i] * (burst ? 10.0 : 1.0);
                }
                const double rms = sim.step(vb.data(), &pem);
                if (blk == 1499) {
                    pem.copy_impulse_response(ir.data());
                    before = misalignment_db(path, ir);
                }
                if (blk >= 1500 && blk < 1650 && rms > worst) {
                    worst = rms;
                }
            }
            pem.copy_impulse_response(ir.data());
            return std::pair<double, double>{worst, misalignment_db(path, ir) - before};
        };

        const auto [ungated_rms, ungated_excursion] = run(0.0);
        EXPECT_LT(ungated_rms, 30000.0) << "measured ~17600 (ungated NLMS: ~56000)";
        EXPECT_LT(std::abs(ungated_excursion), 6.0) << "the estimate survives (measured ~2 dB)";

        const auto [floored_rms, floored_excursion] = run(8.0);
        EXPECT_LT(floored_rms, 1000.0) << "measured ~24 (the M4 gate: ~25)";
        EXPECT_LT(std::abs(floored_excursion), 6.0);
    }

    TEST(FdKalmanConfigValidation, RejectsBadConfigs) {
        using kf = mutap::partitioned_fdkf<float>;

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
        using kf = mutap::partitioned_fdkf<float>;
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
