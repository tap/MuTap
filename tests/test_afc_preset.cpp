// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// THE AFC PRESET (mutap/afc_preset.h; production-readiness M2c). Three
// claims:
//
//   1. At 48 kHz the preset reproduces mutap.afc~'s hand-assembled
//      configuration field for field, at the external's shipping geometry
//      (block 256, 8 partitions, 2048 taps) and at the claims' (block 64, 16
//      partitions, 1024 taps), and a canceller built from either produces
//      bit-identical output: adopting the preset moves no bits. The hand
//      assembly is transcribed here from mutap.afc_tilde.cpp (make_config /
//      make_kalman_config / set_shadow, MuTap-Max at 35c144e): the
//      predictor's lags at their sample-count defaults, analysis_window
//      max(2 * block, 1024), analysis_capacity max(default, window),
//      shadow partitions min(2, partitions), shadow smoothing
//      exp(-block / (fs * 0.051)).
//   2. At 44.1 and 96 kHz the preset is valid for both predictors and both
//      cores, its lags are the 48 kHz pitch range in hertz (120 ... 1500 Hz)
//      and its window the 48 kHz window in seconds (21.3 ms), rounded to the
//      block.
//   3. The canceller's open-loop identification does not depend on the rate
//      once the preset scales it: the misalignment reached on a synthetic
//      echo at 44.1 and 96 kHz sits within a stated margin of 48 kHz's.
//      (The closed-loop claims exist at 48 kHz only — SUPPORT.md.)
//
// Measured for claim 3 (macOS 15 x86_64, AppleClang, Release, double; the
// speech cascade on the Kalman core, block 256, 8 partitions, the xorshift
// corpus and a 3-tap path at block/2, block, 3 block/2 — the icount
// harness's corpus — 400 blocks): misalignment −27.21 dB at 44.1 kHz,
// −27.21 at 48 kHz, −30.99 at 96 kHz. The corpus is defined in samples and
// the path in blocks, so the only rate-dependent inputs are the preset's:
// at 44.1 and 48 kHz the window is 1024 samples either way and the pitch
// tap stays off on white noise (29–368 against 32–400 lags change nothing
// the voicing threshold lets through), so the two are identical; at 96 kHz
// the window is 2048 samples, the short-term fit sees twice the data, and
// the canceller lands 3.8 dB deeper. The assertions are the floor each
// reaches (−24 dB, 3 dB under the shallowest) and the spread (under 6 dB,
// against 3.78 measured): a preset that broke the capacity constraint
// would throw at construction, one that mis-sized the window would move
// the spread.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/afc_preset.h"

namespace {

    using tap::mu::afc_guard_preset;
    using tap::mu::partitioned_fdaf;
    using tap::mu::partitioned_fdkf;
    using tap::mu::pem_afc;
    using tap::mu::pem_afc_preset;
    using tap::mu::speech_predictor;
    using tap::mu::warped_lpc_predictor;

    using kalman_speech = pem_afc<double, speech_predictor<double>, partitioned_fdkf<double>>;
    using kalman_warped = pem_afc<double, warped_lpc_predictor<double>, partitioned_fdkf<double>>;
    using nlms_speech   = pem_afc<double, speech_predictor<double>, partitioned_fdaf<double>>;
    using nlms_warped   = pem_afc<double, warped_lpc_predictor<double>, partitioned_fdaf<double>>;

    constexpr double k_shadow_tau_s = 0.051; ///< mutap.afc~'s k_shadow_tau_s

    /// mutap.afc~'s hand assembly (make_kalman_config + set_shadow), the
    /// geometry- and rate-derived fields only.
    template <typename Afc>
    typename Afc::config hand_assembled(size_t block, size_t partitions, double fs) {
        typename Afc::config cfg;
        cfg.fdaf.block_size             = block;
        cfg.fdaf.partitions             = partitions;
        cfg.analysis_window             = std::max<size_t>(2 * block, 1024);
        cfg.predictor.analysis_capacity = std::max(cfg.predictor.analysis_capacity, cfg.analysis_window);
        cfg.shadow_partitions           = std::min<size_t>(2, partitions);
        cfg.shadow_smoothing            = std::exp(-static_cast<double>(block) / (fs * k_shadow_tau_s));
        return cfg;
    }

    /// The icount harness's corpus: xorshift white noise through a 3-tap
    /// path, in samples, so it is the same signal at every rate.
    struct corpus {
        std::vector<double> x;
        std::vector<double> y;
        size_t              block;
        size_t              blocks;
        corpus(size_t block_size, size_t n_blocks)
            : block(block_size)
            , blocks(n_blocks) {
            const size_t n = blocks * block;
            x.resize(n);
            y.resize(n);
            std::uint32_t s    = 0x9E3779B9U;
            auto          next = [&s]() noexcept {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                return (static_cast<double>(s) / 2147483648.0 - 1.0) * 0.1;
            };
            for (auto& v : x) {
                v = next();
            }
            const size_t d1 = block / 2, d2 = block, d3 = 3 * block / 2;
            for (size_t i = 0; i < n; ++i) {
                y[i] = 0.25 * x[(i + n - d1) % n] - 0.12 * x[(i + n - d2) % n] + 0.06 * x[(i + n - d3) % n]
                       + 0.001 * next();
            }
        }
        const double* xb(size_t i) const { return &x[(i % blocks) * block]; }
        const double* yb(size_t i) const { return &y[(i % blocks) * block]; }
        /// The true path as taps (for the misalignment).
        std::vector<double> path(size_t taps) const {
            std::vector<double> h(taps, 0.0);
            h[block / 2]     = 0.25;
            h[block]         = -0.12;
            h[3 * block / 2] = 0.06;
            return h;
        }
    };

    double misalignment_db(const std::vector<double>& h, const std::vector<double>& h_hat) {
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < h.size(); ++i) {
            num += (h[i] - h_hat[i]) * (h[i] - h_hat[i]);
            den += h[i] * h[i];
        }
        return 10.0 * std::log10(num / den);
    }

    template <typename Afc>
    void expect_same_config(const typename Afc::config& a, const typename Afc::config& b) {
        EXPECT_EQ(a.fdaf.block_size, b.fdaf.block_size);
        EXPECT_EQ(a.fdaf.partitions, b.fdaf.partitions);
        EXPECT_EQ(a.analysis_window, b.analysis_window);
        EXPECT_EQ(a.predictor.analysis_capacity, b.predictor.analysis_capacity);
        EXPECT_EQ(a.shadow_partitions, b.shadow_partitions);
        EXPECT_EQ(a.shadow_smoothing, b.shadow_smoothing) << "bit-equal: the same expression";
        if constexpr (requires { a.predictor.min_lag; }) {
            EXPECT_EQ(a.predictor.min_lag, b.predictor.min_lag);
            EXPECT_EQ(a.predictor.max_lag, b.predictor.max_lag);
        }
        else {
            EXPECT_EQ(a.predictor.lambda, b.predictor.lambda);
        }
    }

    /// Claim 1 for one instantiation and geometry: fields, then output.
    template <typename Afc, typename Predictor, typename Core>
    void preset_reproduces(size_t block, size_t partitions) {
        const auto preset = pem_afc_preset<double, Predictor, Core>(block, partitions, 48000.0);
        const auto hand   = hand_assembled<Afc>(block, partitions, 48000.0);
        expect_same_config<Afc>(preset, hand);
        Afc                 a(preset);
        Afc                 b(hand);
        corpus              c(block, 64);
        std::vector<double> ea(block), eb(block);
        for (size_t i = 0; i < 128; ++i) {
            a.process_block(c.xb(i), c.yb(i), ea.data());
            b.process_block(c.xb(i), c.yb(i), eb.data());
            for (size_t k = 0; k < block; ++k) {
                ASSERT_EQ(ea[k], eb[k]) << "block " << i << " sample " << k;
            }
        }
    }

    TEST(AfcPreset, ReproducesTheExternalsHandAssemblyAt48kHz) {
        preset_reproduces<kalman_speech, speech_predictor<double>, partitioned_fdkf<double>>(256, 8);
        preset_reproduces<kalman_speech, speech_predictor<double>, partitioned_fdkf<double>>(64, 16);
        preset_reproduces<kalman_warped, warped_lpc_predictor<double>, partitioned_fdkf<double>>(256, 8);
        preset_reproduces<kalman_warped, warped_lpc_predictor<double>, partitioned_fdkf<double>>(64, 16);
        preset_reproduces<nlms_speech, speech_predictor<double>, partitioned_fdaf<double>>(256, 8);
        preset_reproduces<nlms_warped, warped_lpc_predictor<double>, partitioned_fdaf<double>>(64, 16);
    }

    TEST(AfcPreset, ScalesTheLagsAndTheWindowWithTheRate) {
        // 48 kHz: the defaults, exactly.
        const auto p48 = pem_afc_preset<double>(256, 8, 48000.0);
        EXPECT_EQ(p48.predictor.min_lag, 32u);
        EXPECT_EQ(p48.predictor.max_lag, 400u);
        EXPECT_EQ(p48.analysis_window, 1024u);
        EXPECT_EQ(p48.predictor.analysis_capacity, 1024u);
        // 44.1 kHz: 29 ... 368 samples (1520 ... 120 Hz), 1024 samples of
        // window (23.2 ms: 941 rounded up to the block).
        const auto p44 = pem_afc_preset<double>(256, 8, 44100.0);
        EXPECT_EQ(p44.predictor.min_lag, 29u);
        EXPECT_EQ(p44.predictor.max_lag, 368u);
        EXPECT_EQ(p44.analysis_window, 1024u);
        EXPECT_EQ(p44.predictor.analysis_capacity, 1024u);
        // 96 kHz: 64 ... 800 samples, 2048 of window (21.3 ms exactly), the
        // capacity 2048 >= 2 * 800.
        const auto p96 = pem_afc_preset<double>(256, 8, 96000.0);
        EXPECT_EQ(p96.predictor.min_lag, 64u);
        EXPECT_EQ(p96.predictor.max_lag, 800u);
        EXPECT_EQ(p96.analysis_window, 2048u);
        EXPECT_EQ(p96.predictor.analysis_capacity, 2048u);
        // Block 64 at 96 kHz: the window rounds to the block (2048 = 32 x 64).
        EXPECT_EQ(pem_afc_preset<double>(64, 16, 96000.0).analysis_window, 2048u);
        // Block 1024 at 44.1 kHz: at least two blocks.
        EXPECT_EQ(pem_afc_preset<double>(1024, 2, 44100.0).analysis_window, 2048u);
        // The shadow: clamped to the partition count, 51 ms at every rate.
        EXPECT_EQ(pem_afc_preset<double>(256, 1, 48000.0).shadow_partitions, 1u);
        EXPECT_NEAR(-static_cast<double>(256) / std::log(p96.shadow_smoothing) / 96000.0, 0.051, 1e-12);
        EXPECT_NEAR(-static_cast<double>(256) / std::log(p44.shadow_smoothing) / 44100.0, 0.051, 1e-12);
    }

    TEST(AfcPreset, ConstructsEveryInstantiationAtEveryRate) {
        // A lambda per instantiation: the macro cannot take a template
        // argument list's commas.
        const auto builds = [](auto make, double fs, size_t block) {
            EXPECT_NO_THROW(make()) << fs << " Hz, block " << block;
        };
        for (const double fs : {44100.0, 48000.0, 96000.0}) {
            for (const size_t block : {64u, 256u}) {
                const size_t p = 2048 / block;
                builds(
                    [&] {
                        return kalman_speech{
                            pem_afc_preset<double, speech_predictor<double>, partitioned_fdkf<double>>(block, p, fs)};
                    },
                    fs, block);
                builds(
                    [&] {
                        return kalman_warped{
                            pem_afc_preset<double, warped_lpc_predictor<double>, partitioned_fdkf<double>>(block, p,
                                                                                                           fs)};
                    },
                    fs, block);
                builds(
                    [&] {
                        return nlms_speech{
                            pem_afc_preset<double, speech_predictor<double>, partitioned_fdaf<double>>(block, p, fs)};
                    },
                    fs, block);
                builds(
                    [&] {
                        return nlms_warped{
                            pem_afc_preset<double, warped_lpc_predictor<double>, partitioned_fdaf<double>>(block, p,
                                                                                                           fs)};
                    },
                    fs, block);
                using float_afc = pem_afc<float, speech_predictor<float>, partitioned_fdkf<float>>;
                builds(
                    [&] {
                        return float_afc{
                            pem_afc_preset<float, speech_predictor<float>, partitioned_fdkf<float>>(block, p, fs)};
                    },
                    fs, block);
            }
        }
        EXPECT_THROW(pem_afc_preset<double>(0, 8, 48000.0), std::invalid_argument);
        EXPECT_THROW(pem_afc_preset<double>(256, 8, 0.0), std::invalid_argument);
        EXPECT_THROW(pem_afc_preset<double>(256, 8, std::nan("")), std::invalid_argument);
    }

    TEST(AfcPreset, OpenLoopIdentificationMatchesAcrossRates) {
        std::vector<double> reached;
        for (const double fs : {44100.0, 48000.0, 96000.0}) {
            kalman_speech afc(pem_afc_preset<double, speech_predictor<double>, partitioned_fdkf<double>>(256, 8, fs));
            corpus        c(256, 64);
            std::vector<double> e(256);
            for (size_t i = 0; i < 400; ++i) {
                afc.process_block(c.xb(i), c.yb(i), e.data());
            }
            std::vector<double> h_hat(afc.filter_length());
            afc.copy_impulse_response(h_hat.data());
            const double mis = misalignment_db(c.path(afc.filter_length()), h_hat);
            ::testing::Test::RecordProperty("misalignment_db_" + std::to_string(static_cast<int>(fs)), mis);
            reached.push_back(mis);
            EXPECT_LT(mis, -24.0) << "fs " << fs; // measured -27.21 / -27.21 / -30.99
        }
        const double lo = *std::min_element(reached.begin(), reached.end());
        const double hi = *std::max_element(reached.begin(), reached.end());
        // Measured spread 3.78 dB.
        EXPECT_LT(hi - lo, 6.0) << "44.1 / 48 / 96 kHz reached " << reached[0] << " / " << reached[1] << " / "
                                << reached[2] << " dB";
    }

    TEST(AfcPreset, GuardPresetForcesTheDetectorsGeometry) {
        // 48 kHz, block 64: the defaults untouched, the calibrations placed.
        const auto g48 = afc_guard_preset<double>(64, 48000.0, 0.012, -9.0);
        EXPECT_EQ(g48.microphones, 1u);
        EXPECT_EQ(g48.block_size, 64u);
        EXPECT_EQ(g48.sample_rate, 48000.0);
        EXPECT_EQ(g48.detector.loop_period_s, 0.012);
        EXPECT_EQ(g48.detector.ceiling_db, -9.0);
        EXPECT_EQ(g48.detector.f_hi_hz, 16000.0);
        EXPECT_EQ(g48.detector.growth_window_s, 0.0213);
        // 16 kHz: the top band under 0.45 fs (7200 Hz).
        EXPECT_EQ(afc_guard_preset<double>(64, 16000.0).detector.f_hi_hz, 7200.0);
        // Block 1024 at 44.1 kHz: the growth fit spans 3 blocks (69.7 ms).
        const auto g = afc_guard_preset<double>(1024, 44100.0);
        EXPECT_GT(g.detector.growth_window_s, 3.0 * 1024.0 / 44100.0);
        EXPECT_LT(g.detector.growth_window_s, 3.0 * 1024.0 / 44100.0 * 1.001);
        // Every one of these constructs a guard.
        for (const double fs : {16000.0, 44100.0, 48000.0, 96000.0}) {
            for (const size_t block : {64u, 256u, 1024u}) {
                const auto make = [&] { return tap::mu::howl_guard<double>{afc_guard_preset<double>(block, fs)}; };
                EXPECT_NO_THROW(make()) << fs << " / " << block;
            }
        }
        EXPECT_THROW(afc_guard_preset<double>(0, 48000.0), std::invalid_argument);
        EXPECT_THROW(afc_guard_preset<double>(64, -1.0), std::invalid_argument);
    }

} // namespace
