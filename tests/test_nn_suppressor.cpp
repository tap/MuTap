// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Structural tests for the learned residual suppressor's inference plumbing
// (nn_suppressor.h): the STFT analysis/synthesis path, the gain application,
// and the chain-contract surface (echo_explained, comfort noise), exercised
// with weights CONSTRUCTED to force known network outputs (numerical parity
// of the full net against the Python reference is tools/ml/test_parity.py,
// which drives real weights through both).
//
// - dense_out bias = +20  -> sigmoid saturates, every band gain ~= 1: the
//   suppressor must be a transparent one-block delay (perfect sqrt-Hann
//   overlap-add reconstruction; comfort noise off — the fill would top up
//   toward the tracked floor even at unit gain).
// - dense_out bias = -20  -> gains ~= 0: output ~= silence with comfort
//   noise off, and ~= the tracked noise floor with it on.
// - echo_explained() ~= 1 when yhat ~= mic, ~= 0 when yhat = 0.
// - the chain composes with nn_suppressor as its Post engine.

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/nn_suppressor.h"
#include "mutap/postfilter.h"

namespace {

    using tap::mu::aec_chain;
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
        nn_suppressor_weights w; // default geometry: 16 kHz / hop 64
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

    nn_suppressor<double>::config make_config(unsigned seed, float out_bias, bool comfort) {
        nn_suppressor<double>::config cfg;
        cfg.weights       = make_weights(seed, out_bias);
        cfg.comfort_noise = comfort;
        return cfg;
    }

    std::vector<double> noise(size_t n, unsigned seed, double rms = 1.0) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, rms);
        std::vector<double>              v(n);
        for (auto& x : v) {
            x = dist(gen);
        }
        return v;
    }

    TEST(NnSuppressor, UnitGainsAreATransparentOneBlockDelay) {
        nn_suppressor<double> sup(make_config(7, 20.0f, false)); // sigmoid(20) ~ 1
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

    TEST(NnSuppressor, ZeroGainsSilenceTheOutputWithoutComfortNoise) {
        nn_suppressor<double> sup(make_config(7, -20.0f, false)); // sigmoid(-20) ~ 0
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

    // With comfort noise on, fully-suppressed output settles near the
    // tracked floor of the (stationary) input instead of digital silence:
    // within a few dB of the input level once the first minimum-statistics
    // window has completed. The floor bias intentionally overshoots the
    // biased-low minimum statistic, so bound both sides loosely.
    TEST(NnSuppressor, ZeroGainsSettleAtTheComfortFloor) {
        auto cfg         = make_config(7, -20.0f, true);
        cfg.floor_window = 32; // complete both min-statistics windows quickly
        nn_suppressor<double> sup(std::move(cfg));

        const size_t blocks = 300;
        const auto   e      = noise(blocks * k_block, 11);
        const auto   yhat   = noise(blocks * k_block, 12);

        std::vector<double> out(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&e[b * k_block], &yhat[b * k_block], &out[b * k_block]);
        }
        double fill = 0.0;
        double ref  = 0.0;
        for (size_t i = out.size() / 2; i < out.size(); ++i) {
            fill += out[i] * out[i];
            ref += e[i] * e[i];
        }
        const double rel_db = 10.0 * std::log10(fill / ref);
        EXPECT_GT(rel_db, -10.0) << "comfort fill should sit near the floor, not at silence";
        EXPECT_LT(rel_db, 6.0) << "and must not exceed the input level by more than the bias";
    }

    TEST(NnSuppressor, EchoExplainedTracksYhatShare) {
        nn_suppressor<double> sup(make_config(7, 0.0f, false));
        const size_t          blocks = 100;
        const auto            sig    = noise(blocks * k_block, 11);
        std::vector<double>   out(blocks * k_block);
        std::vector<double>   zeros(blocks * k_block, 0.0);

        // yhat == mic-and-then-some: E ~ 0, yhat = signal -> explained ~ 1.
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&zeros[b * k_block], &sig[b * k_block], &out[b * k_block]);
        }
        EXPECT_NEAR(sup.echo_explained(), 1.0, 1e-6);

        sup.reset();
        // yhat == 0: nothing explained.
        for (size_t b = 0; b < blocks; ++b) {
            sup.process_block(&sig[b * k_block], &zeros[b * k_block], &out[b * k_block]);
        }
        EXPECT_NEAR(sup.echo_explained(), 0.0, 1e-6);
    }

    TEST(NnSuppressor, RejectsWrongGeometry) {
        auto cfg       = make_config(7, 0.0f, false);
        cfg.block_size = 128; // != trained hop 64
        EXPECT_THROW(nn_suppressor<double>(std::move(cfg)), std::invalid_argument);
        auto bad = make_config(7, 0.0f, false);
        bad.weights.gru_b_ih.resize(5);
        EXPECT_THROW(nn_suppressor<double>(std::move(bad)), std::invalid_argument);
    }

    // The chain composes with the learned post engine: block sizes match up
    // (matched() writes the canceller's block into the post config), the
    // guard reads the post's echo_explained(), and processing runs.
    TEST(NnSuppressor, ComposesAsTheChainPostEngine) {
        using chain_t = aec_chain<double, tap::mu::partitioned_fdkf<double>, nn_suppressor<double>>;
        chain_t::config cfg;
        cfg.canceller.block_size = k_block;
        cfg.canceller.partitions = 4;
        cfg.postfilter           = make_config(7, 20.0f, false);
        cfg.guard_attenuation_db = 0.0; // guard off: pass-through check below
        chain_t chain(cfg);

        const size_t        blocks = 50;
        const auto          x      = noise(blocks * k_block, 21, 0.5);
        const auto          y      = noise(blocks * k_block, 22, 0.5);
        std::vector<double> e(blocks * k_block);
        for (size_t b = 0; b < blocks; ++b) {
            chain.process_block(&x[b * k_block], &y[b * k_block], &e[b * k_block]);
        }
        double energy = 0.0;
        for (const double v : e) {
            energy += v * v;
        }
        EXPECT_TRUE(std::isfinite(energy));
        EXPECT_GT(energy, 0.0);
        EXPECT_TRUE(chain.converged()) << "guard disabled reports converged";
    }

} // namespace
