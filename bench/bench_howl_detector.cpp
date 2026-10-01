// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Host benchmark for the safety layer's per-mic howl detector
// (mutap/howl_detector.h) at its defaults: 32 resonators, block 64 at
// 48 kHz, float32 and double. One iteration is one block, so the reported
// time is the per-block (per-mic) cost; items are samples, as in
// bench_aec.cpp. The input cycles through a precomputed white-noise corpus
// at unit RMS, so the bank, the envelopes and the growth ring run their
// steady path (no denormals, no trip). bench/README.md has the workflow and
// the recorded numbers.
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "mutap/howl_detector.h"

namespace {

    template <typename S>
    void bench_howl_detector(benchmark::State& state, size_t block) {
        typename tap::mu::howl_detector<S>::config cfg;
        cfg.sample_rate = 48000.0;
        cfg.block_size  = block;
        tap::mu::howl_detector<S> det(cfg);

        constexpr size_t                 k_blocks = 512;
        std::mt19937                     rng(0xB0B0);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<S>                   x(k_blocks * block);
        for (auto& v : x) {
            v = static_cast<S>(nd(rng));
        }
        for (size_t i = 0; i < 2000; ++i) {
            det.process_block(&x[(i % k_blocks) * block], block);
        }
        size_t i = 2000;
        for (auto _ : state) {
            det.process_block(&x[(i % k_blocks) * block], block);
            benchmark::DoNotOptimize(det.prominence_db());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(block));
    }

} // namespace

BENCHMARK_CAPTURE(bench_howl_detector<double>, b64_f64, 64);
BENCHMARK_CAPTURE(bench_howl_detector<float>, b64_f32, 64);
