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
//     tonal closed loop (band-limited room, medians over five seed sets):
//                     raw/naive pair 0.87, PEM-prewhitened 0.06 — the
//                     paper's "PEM heavily reduces IPC" headline
//
//   Burst robustness (tonal closed loop, +20 dB near-end for 50 blocks;
//   band-limited room, medians over five seed sets): ungated worst block
//   RMS 54,894 (double) / 77,743 (float); IPC step scaling + transient
//   gate 88.91 / 134.40, contained under 1000 in 5 / 3 of the 5 sets
//
//   Variable regularization: identification is scale-invariant
//     (-163 dB misalignment at 1e-5x, 1x, 1000x input scale), where a
//     fixed epsilon degrades to -8 dB at 1e-5x.
//
// The open-loop tests (IPC tracking, the gate and freeze plumbing,
// regularization) use the raw synthetic room: an echo path, not a loop.
// burst_test is the emulated selection's platform canary; the closed-loop
// claims are PemPrewhiteningReducesIpc and burst_host_test.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>
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

        tap::mu::partitioned_fdaf<double>::config cfg;
        cfg.block_size = k_block;
        cfg.partitions = k_taps / k_block;
        tap::mu::partitioned_fdaf<double> fdaf(cfg);

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
    // near-end that biases the naive update. Band-limited room, medians over
    // seed sets 0..4: naive 0.87 (per set 0.75..0.90), PEM 0.06
    // (0.04..0.38).
    TEST(AdaptationControl, PemPrewhiteningReducesIpc) {
        const auto   path     = band_limited(random_decaying_rir<double>(k_taps, 5));
        const double open_msg = mutap_test::theoretical_msg_db(path);

        typename closed_loop_sim<double>::config lc;
        lc.feedback_path   = path;
        lc.block_size      = k_block;
        lc.forward_delay   = 2 * k_block;
        lc.forward_gain_db = open_msg - 6.0;

        std::vector<double> naive_ipc;
        std::vector<double> pem_ipc;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto v = mutap_test::tonal_near_end<double>(600 * k_block, seed_in_set(2, set));

            double ipc_naive = 0.0;
            int    n         = 0;
            {
                tap::mu::partitioned_fdaf<double>::config fc;
                fc.block_size              = k_block;
                fc.partitions              = k_taps / k_block;
                fc.relative_regularization = 0.0; // M1-era naive, as in the M2 baseline
                tap::mu::partitioned_fdaf<double> naive(fc);
                closed_loop_sim<double>           sim(lc);
                for (size_t blk = 0; blk < 600; ++blk) {
                    sim.step(&v[blk * k_block], &naive);
                    if (blk >= 100) {
                        ipc_naive += static_cast<double>(naive.ipc());
                        ++n;
                    }
                }
                naive_ipc.push_back(ipc_naive / n);
            }

            double ipc_pem = 0.0;
            n              = 0;
            {
                tap::mu::pem_afc<double>::config pc;
                pc.fdaf.block_size = k_block;
                pc.fdaf.partitions = k_taps / k_block;
                tap::mu::pem_afc<double> pem(pc);
                closed_loop_sim<double>  sim(lc);
                for (size_t blk = 0; blk < 600; ++blk) {
                    sim.step(&v[blk * k_block], &pem);
                    if (blk >= 100) {
                        ipc_pem += static_cast<double>(pem.ipc());
                        ++n;
                    }
                }
                pem_ipc.push_back(ipc_pem / n);
            }
        }
        RecordProperty("median_ipc_naive", median(naive_ipc));
        RecordProperty("median_ipc_pem", median(pem_ipc));
        EXPECT_GT(median(naive_ipc), 0.55) << "raw-pair IPC should be high in the tonal loop (measured median 0.87)";
        EXPECT_LT(median(pem_ipc), 0.25) << "prewhitened-pair IPC should be low (measured median 0.06)";
        EXPECT_GT(median(naive_ipc), median(pem_ipc) + 0.3);
    }

    // Near-end burst robustness in the closed loop: IPC step scaling plus
    // the transient gate must contain a +20 dB, 50-block near-end burst
    // that blows the ungated loop up, without wrecking the converged
    // estimate. One run of both configurations on seed set `set`.
    struct burst_run {
        double ungated_rms       = 0.0;
        double gated_rms         = 0.0;
        double gated_excursion   = 0.0;
        double ungated_excursion = 0.0;
    };

    template <typename Sample>
    burst_run run_burst(const std::vector<Sample>& path, unsigned set) {
        const double open_msg = mutap_test::theoretical_msg_db(path);

        typename closed_loop_sim<Sample>::config lc;
        lc.feedback_path   = path;
        lc.block_size      = k_block;
        lc.forward_delay   = 2 * k_block;
        lc.forward_gain_db = open_msg - 6.0;

        auto run = [&](bool gated) {
            typename tap::mu::pem_afc<Sample>::config pc;
            pc.fdaf.block_size = k_block;
            pc.fdaf.partitions = k_taps / k_block;
            if (gated) {
                pc.fdaf.ipc_step_scaling       = true;
                pc.fdaf.transient_freeze_ratio = Sample(4);
            }
            tap::mu::pem_afc<Sample> pem(pc);
            closed_loop_sim<Sample>  sim(lc);
            const auto               v = mutap_test::tonal_near_end<Sample>(1700 * k_block, seed_in_set(2, set));

            double              worst_rms = 0.0;
            double              before    = 0.0;
            std::vector<Sample> vb(k_block);
            std::vector<Sample> ir(pem.filter_length());
            for (size_t blk = 0; blk < 1700; ++blk) {
                const bool burst = blk >= 1500 && blk < 1550;
                for (size_t i = 0; i < k_block; ++i) {
                    vb[i] = static_cast<Sample>(static_cast<double>(v[blk * k_block + i]) * (burst ? 10.0 : 1.0));
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

        burst_run r;
        std::tie(r.ungated_rms, r.ungated_excursion) = run(false);
        std::tie(r.gated_rms, r.gated_excursion)     = run(true);
        return r;
    }

    template <typename Sample>
    class burst_host_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(burst_host_test, sample_types);

    // THE CLAIM, band-limited room, medians over seed sets 0..4. Measured:
    // ungated worst block RMS median 54,894 (double, per set
    // 28,215..106,229) / 77,743 (float, 20,156..84,190); gated 88.91
    // (15.82..556.85) / 134.40 (15.74..5993.48); |gated excursion| median
    // 0.77 / 0.98 dB. The gate does not contain every trajectory: the
    // gated worst RMS stays <= 1000 in 5 (double) / 3 (float) of the 5 sets
    // (recorded as contained_seed_sets and printed, not asserted), 9 / 8
    // of 10 over sets 0..9 (the raw room: 8 / 7 of 10). The ungated check
    // is a median too: band-limited double set 5 peaks at only 1003.
    TYPED_TEST(burst_host_test, GatingContainsNearEndBurst) {
        const auto          path = band_limited(random_decaying_rir<TypeParam>(k_taps, 5));
        std::vector<double> ungated;
        std::vector<double> gated;
        std::vector<double> excursion;
        int                 contained = 0;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            const auto r = run_burst(path, set);
            ungated.push_back(r.ungated_rms);
            gated.push_back(r.gated_rms);
            excursion.push_back(std::abs(r.gated_excursion));
            contained += r.gated_rms <= 1000.0 ? 1 : 0;
        }
        this->RecordProperty("median_ungated_worst_rms", median(ungated));
        this->RecordProperty("median_gated_worst_rms", median(gated));
        this->RecordProperty("contained_seed_sets", contained);
        std::printf("burst_host_test: gated worst RMS <= 1000 in %d of %u seed sets\n", contained, k_claim_seed_sets);
        EXPECT_GT(median(ungated), 3000.0)
            << "the burst should blow up the ungated loop (measured median 54,894 / 77,743)";
        EXPECT_LT(median(gated), 1000.0) << "gating should contain the burst (measured median 88.91 / 134.40)";
        EXPECT_LT(median(gated), median(ungated) / 10.0);
        EXPECT_LT(median(excursion), 4.0) << "the gated estimate should survive the burst (measured 0.77 / 0.98 dB)";
    }

    // CANARY (burst_test/0 runs in the emulated selection): raw path, one
    // seed, the thresholds the test has always had. On the band-limited
    // room the float leg's gated worst RMS at this seed is 5993.48, over
    // the 1000 threshold, so the canary stays on the raw room. Measured on
    // host, raw: ungated 45,571 (float) / 45,499 (double), gated 237.87 /
    // 189.74, excursion +0.80 / +1.90 dB. The claim is burst_host_test.
    template <typename Sample>
    class burst_test : public ::testing::Test {};

    TYPED_TEST_SUITE(burst_test, sample_types);

    TYPED_TEST(burst_test, GatingContainsNearEndBurst) {
        const auto r = run_burst(random_decaying_rir<TypeParam>(k_taps, 5), 0);
        EXPECT_GT(r.ungated_rms, 3000.0) << "the burst should blow up the ungated loop (measured ~45,500)";
        EXPECT_LT(r.gated_rms, 1000.0) << "gating should contain the burst (measured 237.87 / 189.74)";
        EXPECT_LT(r.gated_rms, r.ungated_rms / 10.0);
        EXPECT_LT(std::abs(r.gated_excursion), 4.0) << "the gated estimate should survive the burst";
    }

    // Plumbing: the transient gate must hold the filter for exactly the
    // spiked blocks, and release afterwards.
    TEST(AdaptationControl, TransientGateHoldsSpikedBlocks) {
        const auto path = random_decaying_rir<double>(k_taps, 5);

        tap::mu::partitioned_fdaf<double>::config cfg;
        cfg.block_size             = k_block;
        cfg.partitions             = k_taps / k_block;
        cfg.transient_freeze_ratio = 4.0;
        tap::mu::partitioned_fdaf<double> fdaf(cfg);

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
            tap::mu::partitioned_fdaf<double>::config cfg;
            cfg.block_size           = k_block;
            cfg.partitions           = k_taps / k_block;
            cfg.ipc_freeze_threshold = threshold;
            tap::mu::partitioned_fdaf<double> fdaf(cfg);

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
            tap::mu::partitioned_fdaf<double>::config cfg;
            cfg.block_size              = k_block;
            cfg.partitions              = k_taps / k_block;
            cfg.relative_regularization = relative;
            cfg.regularization          = absolute;
            tap::mu::partitioned_fdaf<double> fdaf(cfg);

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
        using fdaf = tap::mu::partitioned_fdaf<float>;

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
