// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M4 (HANDOFF.md): adaptation control + robustness. Locks down
// the measured behavior of the three mechanisms:
//
//   IPC (coherent-error-fraction estimate; the instantaneous
//   pseudo-correlation of Gil-Cacho et al. 2014):
//     open-loop AEC:  ~0.7 while unconverged (error IS echo),
//                     ~0.00 converged, ~0.02 under double-talk
//     tonal closed loop: raw/naive pair ~0.73, PEM-prewhitened ~0.05 —
//                     the paper's "PEM heavily reduces IPC" headline
//
//   Burst robustness (tonal closed loop, +20 dB near-end for 50 blocks):
//     ungated worst block RMS ~56000; IPC step scaling + transient gate
//     ~25 — both layers needed (each alone: ~33000 / ~25000)
//
//   Variable regularization: identification is scale-invariant
//     (-163 dB misalignment at 1e-5x, 1x, 1000x input scale), where a
//     fixed epsilon degrades to -8 dB at 1e-5x.

#include <algorithm>
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
    std::vector<Sample> echo_of(const std::vector<Sample>& u, const std::vector<Sample>& f) {
        std::vector<Sample> d(u.size());
        for (size_t n = 0; n < u.size(); ++n) {
            double       acc  = 0.0;
            const size_t kmax = std::min(n + 1, f.size());
            for (size_t k = 0; k < kmax; ++k) {
                acc += static_cast<double>(f[k]) * static_cast<double>(u[n - k]);
            }
            d[n] = static_cast<Sample>(acc);
        }
        return d;
    }

    // IPC must read as the coherent-error fraction: ~1 while the error is
    // all unmodeled echo, ~0 once converged, and ~0 under double-talk
    // (error dominated by an independent near-end signal).
    TEST(AdaptationControl, IpcTracksEchoVsDoubleTalk) {
        const auto path = random_decaying_rir<double>(k_taps, 5);

        mutap::partitioned_fdaf<double>::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_taps / k_block;
        mutap::partitioned_fdaf<double> fdaf(cfg);

        const auto          u = mutap_test::white_near_end<double>(480 * k_block, 1);
        const auto          d = echo_of(u, path);
        const auto          v = mutap_test::white_near_end<double>(u.size(), 9);
        std::vector<double> e(k_block);

        double early   = 0.0;
        double conv    = 0.0;
        double dt      = 0.0;
        int    n_early = 0;
        int    n_conv  = 0;
        int    n_dt    = 0;
        for (size_t blk = 0; blk < 400; ++blk) {
            fdaf.process_block(&u[blk * k_block], &d[blk * k_block], e.data());
            if (blk >= 15 && blk < 40) { // past the coherence warm-up (~14 blocks at a=0.95)
                early += static_cast<double>(fdaf.ipc());
                ++n_early;
            }
            if (blk >= 300) {
                conv += static_cast<double>(fdaf.ipc());
                ++n_conv;
            }
        }
        for (size_t blk = 400; blk < 480; ++blk) { // double-talk: strong independent near-end
            std::vector<double> noisy(k_block);
            for (size_t i = 0; i < k_block; ++i) {
                noisy[i] = d[blk * k_block + i] + 3.0 * v[blk * k_block + i];
            }
            fdaf.process_block(&u[blk * k_block], noisy.data(), e.data());
            if (blk >= 420) {
                dt += static_cast<double>(fdaf.ipc());
                ++n_dt;
            }
        }

        EXPECT_GT(early / n_early, 0.5) << "IPC should be high while the error is all echo (measured 0.70)";
        EXPECT_LT(conv / n_conv, 0.1) << "IPC should vanish once converged (measured 0.00)";
        EXPECT_LT(dt / n_dt, 0.1) << "IPC should stay low under double-talk (measured 0.02)";
    }

    // The FDAF-PEM paper's headline observation, AFC version: prewhitening
    // collapses the pseudo-correlation between the loop signal and the
    // near-end that biases the naive update.
    TEST(AdaptationControl, PemPrewhiteningReducesIpc) {
        const auto   path     = random_decaying_rir<double>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);
        const auto   v        = mutap_test::tonal_near_end<double>(600 * k_block, 2);

        typename closed_loop_sim<double>::config lc;
        lc.feedback_path   = path;
        lc.block_size      = k_block;
        lc.forward_delay   = 2 * k_block;
        lc.forward_gain_db = open_msg - 6.0;

        double ipc_naive = 0.0;
        int    n         = 0;
        {
            mutap::partitioned_fdaf<double>::config fc;
            fc.block_size              = k_block;
            fc.partitions              = k_taps / k_block;
            fc.relative_regularization = 0.0; // M1-era naive, as in the M2 baseline
            mutap::partitioned_fdaf<double> naive(fc);
            closed_loop_sim<double>         sim(lc);
            for (size_t blk = 0; blk < 600; ++blk) {
                sim.step(&v[blk * k_block], &naive);
                if (blk >= 100) {
                    ipc_naive += static_cast<double>(naive.ipc());
                    ++n;
                }
            }
            ipc_naive /= n;
        }

        double ipc_pem = 0.0;
        n              = 0;
        {
            mutap::pem_afc<double>::config pc;
            pc.fdaf.block_size = k_block;
            pc.fdaf.partitions = k_taps / k_block;
            mutap::pem_afc<double>  pem(pc);
            closed_loop_sim<double> sim(lc);
            for (size_t blk = 0; blk < 600; ++blk) {
                sim.step(&v[blk * k_block], &pem);
                if (blk >= 100) {
                    ipc_pem += static_cast<double>(pem.ipc());
                    ++n;
                }
            }
            ipc_pem /= n;
        }

        EXPECT_GT(ipc_naive, 0.55) << "raw-pair IPC should be high in the tonal loop (measured 0.73)";
        EXPECT_LT(ipc_pem, 0.25) << "prewhitened-pair IPC should be low (measured 0.05)";
        EXPECT_GT(ipc_naive, ipc_pem + 0.3);
    }

    // Near-end burst robustness in the closed loop: IPC step scaling plus
    // the transient gate must contain a +20 dB, 50-block near-end burst
    // that blows the ungated loop up (worst block RMS ~36000 ungated vs
    // ~150 gated), without wrecking the converged estimate.
    template <typename Sample>
    class burst_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(burst_test, sample_types);

    TYPED_TEST(burst_test, GatingContainsNearEndBurst) {
        const auto   path     = random_decaying_rir<TypeParam>(k_taps, 5);
        const double open_msg = mutap_test::theoretical_msg_db(path);

        typename closed_loop_sim<TypeParam>::config lc;
        lc.feedback_path   = path;
        lc.block_size      = k_block;
        lc.forward_delay   = 2 * k_block;
        lc.forward_gain_db = open_msg - 6.0;

        auto run = [&](bool gated) {
            typename mutap::pem_afc<TypeParam>::config pc;
            pc.fdaf.block_size = k_block;
            pc.fdaf.partitions = k_taps / k_block;
            if (gated) {
                pc.fdaf.ipc_step_scaling       = true;
                pc.fdaf.transient_freeze_ratio = TypeParam(4);
            }
            mutap::pem_afc<TypeParam>  pem(pc);
            closed_loop_sim<TypeParam> sim(lc);
            const auto                 v = mutap_test::tonal_near_end<TypeParam>(1700 * k_block, 2);

            double                 worst_rms = 0.0;
            double                 before    = 0.0;
            std::vector<TypeParam> vb(k_block);
            std::vector<TypeParam> ir(pem.filter_length());
            for (size_t blk = 0; blk < 1700; ++blk) {
                const bool burst = blk >= 1500 && blk < 1550;
                for (size_t i = 0; i < k_block; ++i) {
                    vb[i] = static_cast<TypeParam>(static_cast<double>(v[blk * k_block + i]) * (burst ? 10.0 : 1.0));
                }
                const double rms = sim.step(vb.data(), &pem);
                if (blk == 1499) {
                    pem.copy_impulse_response(ir.data());
                    before = misalignment_db(path, ir);
                }
                if (blk >= 1500 && blk < 1650 && rms > worst_rms) {
                    worst_rms = rms;
                }
            }
            pem.copy_impulse_response(ir.data());
            const double after = misalignment_db(path, ir);
            return std::pair<double, double>{worst_rms, after - before};
        };

        const auto [ungated_rms, ungated_excursion] = run(false);
        const auto [gated_rms, gated_excursion]     = run(true);

        EXPECT_GT(ungated_rms, 3000.0) << "the burst should blow up the ungated loop (measured ~56000)";
        EXPECT_LT(gated_rms, 1000.0) << "gating should contain the burst (measured ~25)";
        EXPECT_LT(gated_rms, ungated_rms / 10.0);
        EXPECT_LT(std::abs(gated_excursion), 4.0) << "the gated estimate should survive the burst";
        (void)ungated_excursion;
    }

    // Plumbing: the transient gate must hold the filter for exactly the
    // spiked blocks, and release afterwards.
    TEST(AdaptationControl, TransientGateHoldsSpikedBlocks) {
        const auto path = random_decaying_rir<double>(k_taps, 5);

        mutap::partitioned_fdaf<double>::config cfg;
        cfg.block_size             = k_block;
        cfg.partitions             = k_taps / k_block;
        cfg.transient_freeze_ratio = 4.0;
        mutap::partitioned_fdaf<double> fdaf(cfg);

        const auto          u = mutap_test::white_near_end<double>(120 * k_block, 1);
        const auto          d = echo_of(u, path);
        std::vector<double> e(k_block);
        for (size_t blk = 0; blk < 100; ++blk) {
            fdaf.process_block(&u[blk * k_block], &d[blk * k_block], e.data());
            EXPECT_FALSE(fdaf.transient_held());
        }

        std::vector<double> before(fdaf.filter_length());
        fdaf.copy_impulse_response(before.data());

        // One spiked block: +30 dB of independent noise on the desired signal.
        std::vector<double>              noisy(k_block);
        std::mt19937                     gen(77);
        std::normal_distribution<double> dist(0.0, 1.0);
        for (size_t i = 0; i < k_block; ++i) {
            noisy[i] = d[100 * k_block + i] + 30.0 * dist(gen);
        }
        fdaf.process_block(&u[100 * k_block], noisy.data(), e.data());
        EXPECT_TRUE(fdaf.transient_held());

        std::vector<double> after(fdaf.filter_length());
        fdaf.copy_impulse_response(after.data());
        for (size_t i = 0; i < before.size(); ++i) {
            ASSERT_EQ(before[i], after[i]) << "tap " << i << " moved on a held block";
        }

        // And it releases: subsequent clean blocks adapt again.
        fdaf.process_block(&u[101 * k_block], &d[101 * k_block], e.data());
        EXPECT_FALSE(fdaf.transient_held());
    }

    // Plumbing: when the desired signal is pure independent noise (no echo
    // at all), IPC stays ~0 and the freeze threshold must hold the filter;
    // with the threshold off, the same data lets the filter drift.
    TEST(AdaptationControl, FreezeThresholdHoldsFilter) {
        auto tap_energy = [&](double threshold) {
            mutap::partitioned_fdaf<double>::config cfg;
            cfg.block_size           = k_block;
            cfg.partitions           = k_taps / k_block;
            cfg.ipc_freeze_threshold = threshold;
            mutap::partitioned_fdaf<double> fdaf(cfg);

            const auto          u = mutap_test::white_near_end<double>(100 * k_block, 1);
            const auto          d = mutap_test::white_near_end<double>(100 * k_block, 2); // independent: no echo
            std::vector<double> e(k_block);
            for (size_t blk = 0; blk < 100; ++blk) {
                fdaf.process_block(&u[blk * k_block], &d[blk * k_block], e.data());
            }
            std::vector<double> ir(fdaf.filter_length());
            fdaf.copy_impulse_response(ir.data());
            double energy = 0.0;
            for (const auto tap : ir) {
                energy += tap * tap;
            }
            return energy;
        };

        EXPECT_GT(tap_energy(0.0), 1e-4) << "ungated filter should drift on uninformative data";
        EXPECT_LT(tap_energy(0.5), 1e-8) << "IPC-gated filter should hold near zero";
    }

    // Variable regularization makes identification scale-invariant; the
    // fixed epsilon it replaces is only right at one scale.
    TEST(AdaptationControl, RelativeRegularizationIsScaleInvariant) {
        const auto path = random_decaying_rir<double>(k_taps, 5);

        auto identify = [&](double scale, double relative, double absolute) {
            mutap::partitioned_fdaf<double>::config cfg;
            cfg.block_size              = k_block;
            cfg.partitions              = k_taps / k_block;
            cfg.relative_regularization = relative;
            cfg.regularization          = absolute;
            mutap::partitioned_fdaf<double> fdaf(cfg);

            auto u = mutap_test::white_near_end<double>(300 * k_block, 1);
            for (auto& x : u) {
                x *= scale;
            }
            const auto          d = echo_of(u, path);
            std::vector<double> e(k_block);
            for (size_t blk = 0; blk < 300; ++blk) {
                fdaf.process_block(&u[blk * k_block], &d[blk * k_block], e.data());
            }
            std::vector<double> ir(fdaf.filter_length());
            fdaf.copy_impulse_response(ir.data());
            return misalignment_db(path, ir);
        };

        // Variable scheme (defaults): deep convergence at every scale.
        for (const double scale : {1e-5, 1.0, 1e3}) {
            EXPECT_LT(identify(scale, 1e-2, 1e-12), -60.0) << "scale " << scale << " (measured -163 dB)";
        }
        // The fixed epsilon this replaces: fine at unit scale, broken quiet.
        EXPECT_LT(identify(1.0, 0.0, 1e-6), -60.0);
        EXPECT_GT(identify(1e-5, 0.0, 1e-6), -20.0) << "fixed eps should degrade at 1e-5 scale (measured -8 dB)";
    }

    TEST(AdaptationControlConfigValidation, RejectsBadConfigs) {
        using fdaf = mutap::partitioned_fdaf<float>;

        fdaf::config cfg;
        cfg.relative_regularization = -1.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg               = {};
        cfg.ipc_smoothing = 1.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg                      = {};
        cfg.ipc_freeze_threshold = 1.5F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg                        = {};
        cfg.transient_freeze_ratio = -1.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);
    }

} // namespace
