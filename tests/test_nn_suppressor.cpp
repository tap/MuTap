// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Structural tests for the learned residual suppressor's inference plumbing
// (nn_suppressor.h): the STFT analysis/synthesis path and the gain
// application, exercised with weights CONSTRUCTED to force known network
// outputs (numerical parity of the full net against the Python reference is
// tools/ml/test_parity.py, which drives real weights through both).
//
// - dense_out bias = +20  -> sigmoid saturates, every band gain ~= 1: the
//   suppressor must be a transparent one-block delay (perfect
//   sqrt-Hann overlap-add reconstruction).
// - dense_out bias = -20  -> gains ~= 0: output ~= silence.
// - band_gains() exposes the applied gains for both checks.

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/nn_suppressor.h"

namespace {

    using tap::mu::nn_suppressor;
    using tap::mu::nn_suppressor_weights;

    constexpr size_t k_block = 64;

    nn_suppressor_weights make_weights(unsigned seed, float out_bias) {
        std::mt19937                    gen(seed);
        std::normal_distribution<float> dist(0.0f, 0.3f);
        const auto                      fill = [&](std::vector<float>& v, size_t n) {
            v.resize(n);
            for (auto& x : v) {
                x = dist(gen);
            }
        };
        nn_suppressor_weights w;
        fill(w.dense_in_w, 64 * 44);
        fill(w.dense_in_b, 64);
        fill(w.gru_w_ih, 288 * 64);
        fill(w.gru_w_hh, 288 * 96);
        fill(w.gru_b_ih, 288);
        fill(w.gru_b_hh, 288);
        fill(w.dense_out_w, 22 * 96);
        w.dense_out_w.assign(22 * 96, 0.0f); // output driven by bias alone
        w.dense_out_b.assign(22, out_bias);
        return w;
    }

    std::vector<double> noise(size_t n, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              v(n);
        for (auto& x : v) {
            x = dist(gen);
        }
        return v;
    }

    TEST(NnSuppressor, UnitGainsAreATransparentOneBlockDelay) {
        nn_suppressor<double> sup({}, make_weights(7, 20.0f)); // sigmoid(20) ~ 1
        const size_t          blocks = 50;
        const auto            e      = noise(blocks * k_block, 11);
        const auto            yhat   = noise(blocks * k_block, 12);

        std::vector<double> out(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        for (const double g : sup.band_gains()) {
            EXPECT_NEAR(g, 1.0, 1e-8);
        }
        // out trails e by one block; skip the warm-up frame.
        double err = 0.0;
        double ref = 0.0;
        for (size_t i = 2 * k_block; i < out.size(); ++i) {
            err += (out[i] - e[i - k_block]) * (out[i] - e[i - k_block]);
            ref += e[i - k_block] * e[i - k_block];
        }
        EXPECT_LT(10.0 * std::log10(err / ref), -140.0) << "reconstruction should be float64-deep";
    }

    TEST(NnSuppressor, ZeroGainsSilenceTheOutput) {
        nn_suppressor<double> sup({}, make_weights(7, -20.0f)); // sigmoid(-20) ~ 0
        const size_t          blocks = 50;
        const auto            e      = noise(blocks * k_block, 11);
        const auto            yhat   = noise(blocks * k_block, 12);

        std::vector<double> out(blocks * k_block);
        double              energy = 0.0;
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        for (size_t i = 0; i < out.size(); ++i) {
            energy += out[i] * out[i];
        }
        EXPECT_LT(energy, 1e-12);
    }

    TEST(NnSuppressor, RejectsWrongGeometry) {
        auto w = make_weights(7, 0.0f);
        EXPECT_THROW(nn_suppressor<double>({.block_size = 128}, w), std::invalid_argument);
        w.gru_b_ih.resize(5);
        EXPECT_THROW(nn_suppressor<double>({}, std::move(w)), std::invalid_argument);
    }

} // namespace
