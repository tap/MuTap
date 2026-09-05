// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Structural tests for the learned residual suppressor's inference plumbing
// (nn_suppressor.h), typed over BOTH numeric profiles — float is the
// embedded profile the Cortex-M33/M55 and Hexagon legs run, double the
// golden model — plus the float-tracks-double oracle check. Exercised with
// weights CONSTRUCTED to force known network outputs (numerical parity of
// the full net against the Python reference is tools/ml/test_parity.py,
// which drives real weights through both profiles in CI).
//
// - dense_out bias = +20  -> sigmoid saturates, every band gain ~= 1: the
//   suppressor must be a transparent one-block delay (perfect sqrt-Hann
//   overlap-add reconstruction; comfort noise off — the fill would top up
//   toward the tracked floor even at unit gain).
// - dense_out bias = -20  -> gains ~= 0: output ~= silence with comfort
//   noise off, and ~= the tracked noise floor with it on.
// - echo_explained() ~= 1 when yhat ~= mic, ~= 0 when yhat = 0.
// - the chain composes with nn_suppressor as its Post engine.
// - the shipping 48 kHz / hop-256 / 26-band geometry runs the same code.
// - float tracks double on a live (non-saturated) network at a measured,
//   pinned tolerance: the GRU recurrence is where float error accumulates.
//
// Numeric contract pinned here (see nn_suppressor.h): every dot product
// accumulates in Sample. There is no double arithmetic in the float
// profile, which is what lets it run on parts without FP64.

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/nn_suppressor.h"
#include "mutap/postfilter.h"

namespace {

    using tap::mu::aec_chain;
    using tap::mu::nn_geometry;
    using tap::mu::nn_suppressor;
    using tap::mu::nn_suppressor_weights;

    constexpr size_t k_block = 64;

    /// Deterministic weights at a geometry. bias_only zeroes dense_out_w so
    /// the output is driven by out_bias alone (saturating the sigmoid);
    /// otherwise the network is live and the gains vary with the input.
    nn_suppressor_weights make_weights(unsigned seed, float out_bias, bool bias_only, const nn_geometry& g = {}) {
        std::mt19937                    gen(seed);
        std::normal_distribution<float> dist(0.0f, 0.3f);
        const auto                      fill = [&](std::vector<float>& v, size_t n) {
            v.resize(n);
            for (auto& x : v) {
                x = dist(gen);
            }
        };
        nn_suppressor_weights w;
        w.geometry = g;
        fill(w.dense_in_w, g.dense * g.features());
        fill(w.dense_in_b, g.dense);
        fill(w.gru_w_ih, 3 * g.gru * g.dense);
        fill(w.gru_w_hh, 3 * g.gru * g.gru);
        fill(w.gru_b_ih, 3 * g.gru);
        fill(w.gru_b_hh, 3 * g.gru);
        fill(w.dense_out_w, g.bands * g.gru);
        if (bias_only) {
            w.dense_out_w.assign(g.bands * g.gru, 0.0f);
        }
        w.dense_out_b.assign(g.bands, out_bias);
        return w;
    }

    template <typename Sample>
    typename nn_suppressor<Sample>::config make_config(unsigned seed, float out_bias, bool comfort) {
        typename nn_suppressor<Sample>::config cfg;
        cfg.weights       = make_weights(seed, out_bias, true);
        cfg.comfort_noise = comfort;
        return cfg;
    }

    template <typename Sample>
    std::vector<Sample> noise(size_t n, unsigned seed, double rms = 1.0) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, rms);
        std::vector<Sample>              v(n);
        for (auto& x : v) {
            x = static_cast<Sample>(dist(gen));
        }
        return v;
    }

    template <typename Sample>
    class nn_suppressor_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(nn_suppressor_test, sample_types);

    TYPED_TEST(nn_suppressor_test, UnitGainsAreATransparentOneBlockDelay) {
        nn_suppressor<TypeParam> sup(make_config<TypeParam>(7, 20.0f, false)); // sigmoid(20) ~ 1
        const size_t             blocks = 50;
        const auto               e      = noise<TypeParam>(blocks * k_block, 11);
        const auto               yhat   = noise<TypeParam>(blocks * k_block, 12);

        std::vector<TypeParam> out(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        for (const TypeParam g : sup.band_gains()) {
            EXPECT_NEAR(g, 1.0, 1e-6);
        }
        // out trails e by one block; skip the warm-up frame.
        double err = 0.0;
        double ref = 0.0;
        for (size_t i = 2 * k_block; i < out.size(); ++i) {
            const double d = static_cast<double>(out[i]) - static_cast<double>(e[i - k_block]);
            err += d * d;
            ref += static_cast<double>(e[i - k_block]) * static_cast<double>(e[i - k_block]);
        }
        // Reconstruction sits at the profile's own rounding floor: float64-deep
        // for double, a float32 epsilon walk (~-135 dB measured) for float.
        const double bound_db = std::is_same_v<TypeParam, double> ? -140.0 : -120.0;
        EXPECT_LT(10.0 * std::log10(err / ref), bound_db) << "reconstruction should sit at the rounding floor";
    }

    TYPED_TEST(nn_suppressor_test, ZeroGainsSilenceTheOutputWithoutComfortNoise) {
        nn_suppressor<TypeParam> sup(make_config<TypeParam>(7, -20.0f, false)); // sigmoid(-20) ~ 0
        const size_t             blocks = 50;
        const auto               e      = noise<TypeParam>(blocks * k_block, 11);
        const auto               yhat   = noise<TypeParam>(blocks * k_block, 12);

        std::vector<TypeParam> out(blocks * k_block);
        double                 energy = 0.0;
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            energy += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        }
        EXPECT_LT(energy, 1e-12);
    }

    // With comfort noise on, fully-suppressed output settles near the
    // tracked floor of the (stationary) input instead of digital silence:
    // within a few dB of the input level once the first minimum-statistics
    // window has completed. The floor bias intentionally overshoots the
    // biased-low minimum statistic, so bound both sides loosely.
    TYPED_TEST(nn_suppressor_test, ZeroGainsSettleAtTheComfortFloor) {
        auto cfg         = make_config<TypeParam>(7, -20.0f, true);
        cfg.floor_window = 32; // complete both min-statistics windows quickly
        nn_suppressor<TypeParam> sup(std::move(cfg));

        const size_t blocks = 300;
        const auto   e      = noise<TypeParam>(blocks * k_block, 11);
        const auto   yhat   = noise<TypeParam>(blocks * k_block, 12);

        std::vector<TypeParam> out(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        double fill = 0.0;
        double ref  = 0.0;
        for (size_t i = out.size() / 2; i < out.size(); ++i) {
            fill += static_cast<double>(out[i]) * static_cast<double>(out[i]);
            ref += static_cast<double>(e[i]) * static_cast<double>(e[i]);
        }
        const double rel_db = 10.0 * std::log10(fill / ref);
        EXPECT_GT(rel_db, -10.0) << "comfort fill should sit near the floor, not at silence";
        EXPECT_LT(rel_db, 6.0) << "and must not exceed the input level by more than the bias";
    }

    TYPED_TEST(nn_suppressor_test, EchoExplainedTracksYhatShare) {
        nn_suppressor<TypeParam> sup(make_config<TypeParam>(7, 0.0f, false));
        const size_t             blocks = 100;
        const auto               sig    = noise<TypeParam>(blocks * k_block, 11);
        std::vector<TypeParam>   out(blocks * k_block);
        std::vector<TypeParam>   zeros(blocks * k_block, TypeParam(0));
        const double             tol = std::is_same_v<TypeParam, double> ? 1e-6 : 1e-4;

        // yhat == mic-and-then-some: E ~ 0, yhat = signal -> explained ~ 1.
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&zeros[b * k_block], &sig[b * k_block], &out[b * k_block]);
        }
        EXPECT_NEAR(sup.echo_explained(), 1.0, tol);

        sup.reset();
        // yhat == 0: nothing explained.
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&sig[b * k_block], &zeros[b * k_block], &out[b * k_block]);
        }
        EXPECT_NEAR(sup.echo_explained(), 0.0, tol);
    }

    TYPED_TEST(nn_suppressor_test, RejectsWrongGeometry) {
        auto cfg       = make_config<TypeParam>(7, 0.0f, false);
        cfg.block_size = 128; // != trained hop 64
        EXPECT_THROW(nn_suppressor<TypeParam>(std::move(cfg)), std::invalid_argument);
        auto bad = make_config<TypeParam>(7, 0.0f, false);
        bad.weights.gru_b_ih.resize(5);
        EXPECT_THROW(nn_suppressor<TypeParam>(std::move(bad)), std::invalid_argument);
    }

    // The shipping model's geometry (tools/ml/pretrained/suppressor_v2_48k):
    // 48 kHz, hop 256, 26 bands, dense 64, GRU 96. Geometry is a value the
    // weights carry, so the same code must run it unchanged — pinned with
    // the transparent-delay check at that hop.
    TYPED_TEST(nn_suppressor_test, RunsAtTheShippingGeometry) {
        const nn_geometry                         g{48000.0, 256, 26, 64, 96};
        typename nn_suppressor<TypeParam>::config cfg;
        cfg.weights       = make_weights(9, 20.0f, true, g);
        cfg.comfort_noise = false;
        nn_suppressor<TypeParam> sup(std::move(cfg));
        EXPECT_EQ(sup.block_size(), 256U);
        EXPECT_EQ(sup.geometry().bands, 26U);

        const size_t           hop    = 256;
        const size_t           blocks = 40;
        const auto             e      = noise<TypeParam>(blocks * hop, 31);
        const auto             yhat   = noise<TypeParam>(blocks * hop, 32);
        std::vector<TypeParam> out(blocks * hop);
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * hop], &yhat[b * hop], &out[b * hop]);
        }
        double err = 0.0;
        double ref = 0.0;
        for (size_t i = 2 * hop; i < out.size(); ++i) {
            const double d = static_cast<double>(out[i]) - static_cast<double>(e[i - hop]);
            err += d * d;
            ref += static_cast<double>(e[i - hop]) * static_cast<double>(e[i - hop]);
        }
        const double bound_db = std::is_same_v<TypeParam, double> ? -140.0 : -120.0;
        EXPECT_LT(10.0 * std::log10(err / ref), bound_db);
    }

    // The chain composes with the learned post engine: block sizes match up
    // (matched() writes the canceller's block into the post config), the
    // guard reads the post's echo_explained(), and processing runs.
    TYPED_TEST(nn_suppressor_test, ComposesAsTheChainPostEngine) {
        using chain_t = aec_chain<TypeParam, tap::mu::partitioned_fdkf<TypeParam>, nn_suppressor<TypeParam>>;
        typename chain_t::config cfg;
        cfg.canceller.block_size = k_block;
        cfg.canceller.partitions = 4;
        cfg.postfilter           = make_config<TypeParam>(7, 20.0f, false);
        cfg.guard_attenuation_db = 0.0; // guard off: pass-through check below
        chain_t chain(cfg);

        const size_t           blocks = 50;
        const auto             x      = noise<TypeParam>(blocks * k_block, 21, 0.5);
        const auto             y      = noise<TypeParam>(blocks * k_block, 22, 0.5);
        std::vector<TypeParam> e(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            chain.process_block(&x[b * k_block], &y[b * k_block], &e[b * k_block]);
        }
        double energy = 0.0;
        for (const TypeParam v : e) {
            energy += static_cast<double>(v) * static_cast<double>(v);
        }
        EXPECT_TRUE(std::isfinite(energy));
        EXPECT_GT(energy, 0.0);
        EXPECT_TRUE(chain.converged()) << "guard disabled reports converged";
    }

    // The float-tracks-double oracle on a LIVE network (dense_out_w
    // populated, bias 0, so gains follow the input through the GRU): the
    // float profile must reproduce the double golden model's output stream
    // to a stated depth. This is the check the promotion of the kernels
    // (plan M3) must keep passing unchanged. Measured 2026-09 on this
    // signal: -129.4 dB; pinned at -120 dB (9 dB of margin, so a regression
    // in the float path is caught rather than absorbed).
    TEST(NnSuppressorCrossPrecision, FloatTracksDouble) {
        nn_suppressor<double>::config cd;
        cd.weights       = make_weights(5, 0.0f, false);
        cd.comfort_noise = false;
        nn_suppressor<float>::config cf;
        cf.weights       = cd.weights;
        cf.comfort_noise = false;
        nn_suppressor<double> sd(std::move(cd));
        nn_suppressor<float>  sf(std::move(cf));

        const size_t        blocks = 200;
        const auto          e      = noise<double>(blocks * k_block, 41, 0.3);
        const auto          yhat   = noise<double>(blocks * k_block, 42, 0.2);
        std::vector<float>  ef(e.begin(), e.end());
        std::vector<float>  yf(yhat.begin(), yhat.end());
        std::vector<double> od(blocks * k_block);
        std::vector<float>  of(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            sd.process_block(&e[b * k_block], &yhat[b * k_block], &od[b * k_block]);
            sf.process_block(&ef[b * k_block], &yf[b * k_block], &of[b * k_block]);
        }
        double err       = 0.0;
        double ref       = 0.0;
        double gain_span = 0.0;
        for (size_t i = k_block; i < od.size(); ++i) {
            const double d = od[i] - static_cast<double>(of[i]);
            err += d * d;
            ref += od[i] * od[i];
        }
        for (const double g : sd.band_gains()) {
            gain_span = std::max(gain_span, std::abs(g - 0.5));
        }
        EXPECT_GT(gain_span, 0.05) << "the network must be live, not saturated, for this to mean anything";
        const double rel_db = 10.0 * std::log10(err / ref);
        RecordProperty("float_vs_double_db", rel_db);
        EXPECT_LT(rel_db, -120.0) << "float profile drifts from the golden model";
    }

} // namespace
