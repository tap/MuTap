// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Milestone M1 pass criteria (HANDOFF.md): the partitioned-block FDAF,
// run open-loop as a plain identification problem (known FIR as the true
// path), converges in misalignment on white AND colored input, and the
// float32 build tracks the float64 golden model. Thresholds carry ~10 dB
// of margin over measured behavior so they gate regressions, not noise.

#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fdaf.h"

namespace {

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

    // Random FIR with an exponentially decaying envelope, normalized to
    // unit energy — a crude stand-in for a room impulse response.
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
        const double scale = 1.0 / std::sqrt(energy);
        for (auto& v : f) {
            v = static_cast<Sample>(static_cast<double>(v) * scale);
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

    struct id_result {
        double early_misalignment_db = 0.0; ///< after the first few blocks
        double final_misalignment_db = 0.0;
        double erle_db               = 0.0; ///< over the last quarter of the run
    };

    // Drive an identification run: desired = truth * input, block by block.
    template <typename Sample>
    id_result run_identification(tap::mu::partitioned_fdaf<Sample>& fdaf, const std::vector<Sample>& input,
                                 const std::vector<Sample>& truth) {
        const size_t b       = fdaf.block_size();
        const size_t blocks  = input.size() / b;
        const auto   desired = convolve(input, truth);

        std::vector<Sample> error(b);
        std::vector<Sample> ir(fdaf.filter_length());
        id_result           result;

        double err_energy = 0.0;
        double des_energy = 0.0;
        for (size_t blk = 0; blk < blocks; ++blk) {
            fdaf.process_block(&input[blk * b], &desired[blk * b], error.data());
            if (blk == 4) {
                fdaf.copy_impulse_response(ir.data());
                result.early_misalignment_db = misalignment_db(truth, ir);
            }
            if (blk >= (3 * blocks) / 4) {
                for (size_t i = 0; i < b; ++i) {
                    err_energy += static_cast<double>(error[i]) * static_cast<double>(error[i]);
                    des_energy += static_cast<double>(desired[blk * b + i]) * static_cast<double>(desired[blk * b + i]);
                }
            }
        }
        fdaf.copy_impulse_response(ir.data());
        result.final_misalignment_db = misalignment_db(truth, ir);
        result.erle_db               = 10.0 * std::log10(des_energy / err_energy);
        return result;
    }

    template <typename Sample>
    class fdaf_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(fdaf_test, sample_types);

    // Noiseless white-noise identification: misalignment must fall fast and
    // deep — this is the best-conditioned case the FDAF ever sees.
    TYPED_TEST(fdaf_test, WhiteNoiseIdentificationConverges) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size = 64;
        cfg.partitions = 4;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        const auto truth  = random_decaying_fir<TypeParam>(fdaf.filter_length(), 11);
        const auto input  = white_noise<TypeParam>(400 * fdaf.block_size(), 22);
        const auto result = run_identification(fdaf, input, truth);

        EXPECT_LT(result.final_misalignment_db, (std::is_same_v<TypeParam, double> ? -100.0 : -55.0));
        EXPECT_LT(result.final_misalignment_db, result.early_misalignment_db - 20.0) << "no convergence trend";
        EXPECT_GT(result.erle_db, (std::is_same_v<TypeParam, double> ? 100.0 : 55.0));
    }

    // Colored input (deep one-pole lowpass): per-bin normalization is what
    // keeps the frequency-domain NLMS converging here.
    TYPED_TEST(fdaf_test, ColoredNoiseIdentificationConverges) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size = 64;
        cfg.partitions = 4;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        auto input = white_noise<TypeParam>(1200 * fdaf.block_size(), 33);
        for (size_t i = 1; i < input.size(); ++i) {
            input[i] = static_cast<TypeParam>(0.9 * static_cast<double>(input[i - 1]) + static_cast<double>(input[i]));
        }
        const auto truth  = random_decaying_fir<TypeParam>(fdaf.filter_length(), 44);
        const auto result = run_identification(fdaf, input, truth);

        EXPECT_LT(result.final_misalignment_db, (std::is_same_v<TypeParam, double> ? -60.0 : -40.0));
    }

    // A pure delay-and-scale path: the recovered impulse response must have
    // the tap where the delay is and (near) nothing anywhere else.
    TYPED_TEST(fdaf_test, DelayedImpulsePathIsRecovered) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size = 64;
        cfg.partitions = 4;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        std::vector<TypeParam> truth(fdaf.filter_length(), TypeParam(0));
        truth[100]       = TypeParam(0.8);
        const auto input = white_noise<TypeParam>(300 * fdaf.block_size(), 55);
        run_identification(fdaf, input, truth);

        std::vector<TypeParam> ir(fdaf.filter_length());
        fdaf.copy_impulse_response(ir.data());
        EXPECT_NEAR(static_cast<double>(ir[100]), 0.8, 1e-3);
        double residual = 0.0;
        for (size_t i = 0; i < ir.size(); ++i) {
            if (i != 100) {
                residual += static_cast<double>(ir[i]) * static_cast<double>(ir[i]);
            }
        }
        EXPECT_LT(10.0 * std::log10(residual / (0.8 * 0.8)), -60.0);
    }

    // The unconstrained variant trades the gradient constraint's two FFTs
    // per partition for slower, shallower convergence (the circular-wraparound
    // gradient components perturb the update). Measured here: ~-22 dB after
    // 400 blocks where the constrained variant is below -100 dB; it does keep
    // improving with more data (~-34 dB at 3000 blocks). It must converge.
    TYPED_TEST(fdaf_test, UnconstrainedVariantConverges) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size  = 64;
        cfg.partitions  = 4;
        cfg.constrained = false;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        const auto truth  = random_decaying_fir<TypeParam>(fdaf.filter_length(), 66);
        const auto input  = white_noise<TypeParam>(400 * fdaf.block_size(), 77);
        const auto result = run_identification(fdaf, input, truth);

        EXPECT_LT(result.final_misalignment_db, -15.0);
        EXPECT_LT(result.final_misalignment_db, result.early_misalignment_db - 10.0) << "no convergence trend";
    }

    TYPED_TEST(fdaf_test, FrozenFilterDoesNotAdapt) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size = 64;
        cfg.partitions = 2;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        const auto truth = random_decaying_fir<TypeParam>(fdaf.filter_length(), 88);
        const auto input = white_noise<TypeParam>(60 * fdaf.block_size(), 99);
        run_identification(fdaf, input, truth);

        std::vector<TypeParam> before(fdaf.filter_length());
        fdaf.copy_impulse_response(before.data());

        fdaf.set_adaptation(false);
        const auto             more    = white_noise<TypeParam>(20 * fdaf.block_size(), 111);
        const auto             desired = convolve(more, truth);
        std::vector<TypeParam> error(fdaf.block_size());
        for (size_t blk = 0; blk < 20; ++blk) {
            fdaf.process_block(&more[blk * fdaf.block_size()], &desired[blk * fdaf.block_size()], error.data());
        }

        std::vector<TypeParam> after(fdaf.filter_length());
        fdaf.copy_impulse_response(after.data());
        for (size_t i = 0; i < before.size(); ++i) {
            EXPECT_EQ(before[i], after[i]) << "tap " << i << " moved while frozen";
        }
    }

    TYPED_TEST(fdaf_test, ResetRestoresInitialState) {
        typename tap::mu::partitioned_fdaf<TypeParam>::config cfg;
        cfg.block_size = 64;
        cfg.partitions = 2;
        tap::mu::partitioned_fdaf<TypeParam> fdaf(cfg);

        const auto truth = random_decaying_fir<TypeParam>(fdaf.filter_length(), 13);
        const auto input = white_noise<TypeParam>(40 * fdaf.block_size(), 14);
        run_identification(fdaf, input, truth);

        fdaf.reset();
        std::vector<TypeParam> ir(fdaf.filter_length());
        fdaf.copy_impulse_response(ir.data());
        for (const auto tap : ir) {
            EXPECT_EQ(tap, TypeParam(0));
        }
    }

    // The float32 run must track the float64 golden model through hundreds of
    // adaptive blocks — the whole one-core/three-targets strategy rests on
    // these two staying this close.
    TEST(FdafCrossPrecision, FloatTracksDouble) {
        tap::mu::partitioned_fdaf<double>::config cfg64;
        cfg64.block_size = 64;
        cfg64.partitions = 4;
        tap::mu::partitioned_fdaf<float>::config cfg32;
        cfg32.block_size = 64;
        cfg32.partitions = 4;

        tap::mu::partitioned_fdaf<double> fdaf64(cfg64);
        tap::mu::partitioned_fdaf<float>  fdaf32(cfg32);

        const auto         truth64 = random_decaying_fir<double>(fdaf64.filter_length(), 17);
        const auto         input64 = white_noise<double>(300 * fdaf64.block_size(), 18);
        std::vector<float> truth32(truth64.size());
        std::vector<float> input32(input64.size());
        for (size_t i = 0; i < truth64.size(); ++i) {
            truth32[i] = static_cast<float>(truth64[i]);
        }
        for (size_t i = 0; i < input64.size(); ++i) {
            input32[i] = static_cast<float>(input64[i]);
        }

        const auto r64 = run_identification(fdaf64, input64, truth64);
        const auto r32 = run_identification(fdaf32, input32, truth32);

        // Both converge; float bottoms out at single-precision depth.
        EXPECT_LT(r64.final_misalignment_db, -100.0);
        EXPECT_LT(r32.final_misalignment_db, -55.0);

        // And the recovered impulse responses agree to float rounding depth.
        std::vector<double> ir64(fdaf64.filter_length());
        std::vector<float>  ir32(fdaf32.filter_length());
        fdaf64.copy_impulse_response(ir64.data());
        fdaf32.copy_impulse_response(ir32.data());
        double err = 0.0;
        double ref = 0.0;
        for (size_t i = 0; i < ir64.size(); ++i) {
            const double d = static_cast<double>(ir32[i]) - ir64[i];
            err += d * d;
            ref += ir64[i] * ir64[i];
        }
        EXPECT_LT(10.0 * std::log10(err / ref), -55.0);
    }

    TEST(FdafConfigValidation, RejectsBadConfigs) {
        using fdaf = tap::mu::partitioned_fdaf<float>;

        fdaf::config cfg;
        cfg.block_size = 100; // not a power of 2
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg            = {};
        cfg.partitions = 0;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg           = {};
        cfg.step_size = 0.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg           = {};
        cfg.step_size = 2.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg                = {};
        cfg.regularization = 0.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);

        cfg                 = {};
        cfg.power_smoothing = 1.0F;
        EXPECT_THROW(fdaf{cfg}, std::invalid_argument);
    }

    // The real-time contract is part of the API: everything after
    // construction is noexcept.
    TEST(FdafRtContract, PostConstructionEntryPointsAreNoexcept) {
        using fdaf = tap::mu::partitioned_fdaf<float>;
        static_assert(noexcept(std::declval<fdaf&>().process_block(nullptr, nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<fdaf&>().copy_impulse_response(nullptr)));
        static_assert(noexcept(std::declval<fdaf&>().reset()));
        static_assert(noexcept(std::declval<fdaf&>().set_step_size(0.5F)));
        static_assert(noexcept(std::declval<fdaf&>().set_adaptation(false)));
        SUCCEED();
    }

} // namespace
