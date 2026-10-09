// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Host benchmark for the safety layer's per-mic howl detector
// (mutap/howl_detector.h) and the guard over it (mutap/howl_guard.h), at the
// detector's defaults: 32 resonators, block 64 at 48 kHz, float32 and double.
// One iteration is one block, so the reported time is the per-block (per-mic)
// cost; items are samples, as in bench_aec.cpp. The input cycles through a
// precomputed white-noise corpus at unit RMS, so the bank, the envelopes and
// the growth ring run their steady path (no denormals). bench/README.md has
// the workflow and the recorded numbers.
//
// Besides the whole detector (howl_detector/b64_*) the file breaks one block
// into its stages, so one run localizes the cost on a host:
//   * howl_bank      the resonators and their envelopes over 64 samples (one
//                    fused loop in the detector: a detector whose tick never
//                    comes, so process_block runs the bank alone);
//   * howl_tick      one tick, howl_detail::decide (levels, prominence, line
//                    ring, per-band growth fit, level tracker, verdict) on
//                    envelope snapshots taken from a running detector;
//   * howl_tick_logs the tick's transcendentals alone: the 32 + 32 + 2
//                    std::log10 calls decide() makes on the same snapshots;
//   * howl_fit       one band's least-squares fit over a full ring (decide()
//                    runs it for the peak band and every prominent one);
//   * howl_readouts  the harmonic / subharmonic readouts and the 32
//                    band_level_db() reads the guard's attribution makes;
//   * guard_policy   howl_guard::update() + apply() per mic, the detector not
//                    run (the guard's own step);
//   * guard          the whole guard per block, as HowlGuardHost.CostPerBlock
//                    times it: analyze() per mic, update(), apply() per mic.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "mutap/howl_detector.h"
#include "mutap/howl_guard.h"

namespace {

    constexpr double k_fs     = 48000.0;
    constexpr size_t k_block  = 64;
    constexpr size_t k_blocks = 512; ///< corpus length, blocks
    constexpr size_t k_warm   = 2000;

    template <typename S>
    std::vector<S> white_corpus(size_t block) {
        std::mt19937                     rng(0xB0B0);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<S>                   x(k_blocks * block);
        for (auto& v : x) {
            v = static_cast<S>(nd(rng));
        }
        return x;
    }

    template <typename S>
    void bench_howl_detector(benchmark::State& state, size_t block) {
        typename tap::mu::howl_detector<S>::config cfg;
        cfg.sample_rate = k_fs;
        cfg.block_size  = block;
        tap::mu::howl_detector<S> det(cfg);
        const std::vector<S>      x = white_corpus<S>(block);
        for (size_t i = 0; i < k_warm; ++i) {
            det.process_block(&x[(i % k_blocks) * block], block);
        }
        size_t i = k_warm;
        for (auto _ : state) {
            det.process_block(&x[(i % k_blocks) * block], block);
            benchmark::DoNotOptimize(det.prominence_db());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(block));
    }

    /// The resonators and envelopes alone: a detector whose tick is 2^40
    /// samples away runs only run_bank() in process_block (the growth
    /// window is stretched to the 3 ticks validation needs).
    template <typename S>
    void bench_howl_bank(benchmark::State& state) {
        typename tap::mu::howl_detector<S>::config cfg;
        cfg.sample_rate     = k_fs;
        cfg.block_size      = size_t{1} << 40;
        cfg.growth_window_s = static_cast<S>(4.0 * static_cast<double>(cfg.block_size) / k_fs);
        tap::mu::howl_detector<S> det(cfg);
        const std::vector<S>      x = white_corpus<S>(k_block);
        for (size_t i = 0; i < k_warm; ++i) {
            det.process_block(&x[(i % k_blocks) * k_block], k_block);
        }
        size_t i = k_warm;
        for (auto _ : state) {
            det.process_block(&x[(i % k_blocks) * k_block], k_block);
            benchmark::DoNotOptimize(det.band_level_db(0));
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(k_block));
    }

    /// k_blocks ticks of a running default detector on the corpus: each
    /// tick's 32 band envelopes (from band_level_db) and block power.
    template <typename S>
    struct snapshots {
        size_t         bands = 0;
        std::vector<S> env;
        std::vector<S> power;
        snapshots() {
            typename tap::mu::howl_detector<S>::config cfg;
            cfg.sample_rate = k_fs;
            cfg.block_size  = k_block;
            tap::mu::howl_detector<S> det(cfg);
            const std::vector<S>      x = white_corpus<S>(k_block);
            bands                       = det.bands();
            for (size_t i = 0; i < k_warm; ++i) {
                det.process_block(&x[(i % k_blocks) * k_block], k_block);
            }
            for (size_t k = 0; k < k_blocks; ++k) {
                const S* in = &x[k * k_block];
                det.process_block(in, k_block);
                for (size_t b = 0; b < bands; ++b) {
                    env.push_back(std::pow(S(10), det.band_level_db(b) / S(20)));
                }
                S sq = S(0);
                for (size_t n = 0; n < k_block; ++n) {
                    sq += in[n] * in[n];
                }
                power.push_back(sq / static_cast<S>(k_block));
            }
        }
    };

    /// howl_detail::decision_params as howl_detector's constructor derives
    /// them at the defaults (block 64 at 48 kHz: 16 slots, growth 0.04 dB
    /// per tick, ceiling -6 dB, hold 150 ticks).
    template <typename S>
    tap::mu::howl_detail::decision_params<S> default_params(size_t bands) {
        const typename tap::mu::howl_detector<S>::config cfg;
        const double                                     tick = static_cast<double>(k_block) / k_fs;
        tap::mu::howl_detail::decision_params<S>         p;
        p.bands      = bands;
        p.slots      = static_cast<size_t>(std::lround(static_cast<double>(cfg.growth_window_s) / tick));
        p.inv_others = static_cast<S>(1.0 / static_cast<double>(bands - 1));
        p.prominence = cfg.prominence_db;
        p.growth_tick =
            static_cast<S>(static_cast<double>(cfg.growth_db_per_pass) * tick / static_cast<double>(cfg.loop_period_s));
        p.linearity    = cfg.linearity_db;
        p.rise         = cfg.rise_db;
        p.level_catch  = cfg.level_catch;
        p.ceiling_pow  = static_cast<S>(std::pow(10.0, static_cast<double>(cfg.ceiling_db) / 10.0));
        p.level        = cfg.level_db;
        p.level_prom   = cfg.level_prominence_db;
        p.program_coef = static_cast<S>(1.0 - std::exp(-tick / static_cast<double>(cfg.program_s)));
        p.hold_ticks   = static_cast<size_t>(std::lround(static_cast<double>(cfg.hold_s) / tick));
        p.conf_att     = static_cast<S>(std::exp(-tick / static_cast<double>(cfg.hold_s)));
        p.conf_rel     = static_cast<S>(std::exp(-tick / (4.0 * static_cast<double>(cfg.hold_s))));
        p.slope_w.resize(p.slots);
        const double mean = 0.5 * static_cast<double>(p.slots - 1);
        double       sxx  = 0.0;
        for (size_t k = 0; k < p.slots; ++k) {
            sxx += (static_cast<double>(k) - mean) * (static_cast<double>(k) - mean);
        }
        for (size_t k = 0; k < p.slots; ++k) {
            p.slope_w[k] = static_cast<S>((static_cast<double>(k) - mean) / sxx);
        }
        return p;
    }

    template <typename S>
    void bench_howl_tick(benchmark::State& state) {
        const snapshots<S>                      snap;
        const auto                              p = default_params<S>(snap.bands);
        tap::mu::howl_detail::decision_state<S> s;
        s.resize(p.bands, p.slots);
        s.reset();
        for (size_t i = 0; i < k_warm; ++i) {
            const size_t k = i % k_blocks;
            tap::mu::howl_detail::decide(p, s, &snap.env[k * snap.bands], snap.power[k]);
        }
        size_t i = k_warm;
        for (auto _ : state) {
            const size_t k = i % k_blocks;
            tap::mu::howl_detail::decide(p, s, &snap.env[k * snap.bands], snap.power[k]);
            benchmark::DoNotOptimize(s.prominence);
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(k_block));
    }

    /// decide()'s std::log10 calls on the same snapshots: 20 log10(env) and
    /// 20 log10 of the prominence ratio per band, then the block power's two.
    template <typename S>
    void bench_howl_tick_logs(benchmark::State& state) {
        const snapshots<S> snap;
        const size_t       nb = snap.bands;
        std::vector<S>     level(nb);
        std::vector<S>     prom(nb);
        size_t             i = 0;
        for (auto _ : state) {
            const size_t k   = i % k_blocks;
            const S*     env = &snap.env[k * nb];
            for (size_t b = 0; b < nb; ++b) {
                level[b] = S(20) * std::log10(env[b]);
            }
            for (size_t b = 0; b < nb; ++b) {
                prom[b] = S(20) * std::log10((env[b] + S(1e-9)) / (env[(b + 1) % nb] + S(1e-9)));
            }
            const S pw = snap.power[k] + S(1e-30);
            S       a  = S(10) * std::log10(pw);
            S       c  = S(10) * std::log10(pw / S(0.9));
            benchmark::DoNotOptimize(level.data());
            benchmark::DoNotOptimize(prom.data());
            benchmark::DoNotOptimize(a);
            benchmark::DoNotOptimize(c);
            benchmark::ClobberMemory();
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(k_block));
    }

    template <typename S>
    void bench_howl_fit(benchmark::State& state) {
        const snapshots<S>                      snap;
        const auto                              p = default_params<S>(snap.bands);
        tap::mu::howl_detail::decision_state<S> s;
        s.resize(p.bands, p.slots);
        s.reset();
        for (size_t i = 0; i < k_warm; ++i) {
            const size_t k = i % k_blocks;
            tap::mu::howl_detail::decide(p, s, &snap.env[k * snap.bands], snap.power[k]);
        }
        size_t b = 0;
        for (auto _ : state) {
            S slope;
            S residual;
            tap::mu::howl_detail::fit(p, s, b, slope, residual);
            benchmark::DoNotOptimize(slope);
            benchmark::DoNotOptimize(residual);
            b = (b + 1 == p.bands) ? 0 : b + 1;
        }
    }

    template <typename S>
    void bench_howl_readouts(benchmark::State& state) {
        typename tap::mu::howl_detector<S>::config cfg;
        cfg.sample_rate = k_fs;
        cfg.block_size  = k_block;
        tap::mu::howl_detector<S> det(cfg);
        const std::vector<S>      x = white_corpus<S>(k_block);
        for (size_t i = 0; i < k_warm; ++i) {
            det.process_block(&x[(i % k_blocks) * k_block], k_block);
        }
        for (auto _ : state) {
            benchmark::DoNotOptimize(det.harmonic_ratio_db());
            benchmark::DoNotOptimize(det.subharmonic_ratio_db());
            for (size_t b = 0; b < det.bands(); ++b) {
                benchmark::DoNotOptimize(det.band_level_db(b));
            }
        }
    }

    /// The guard as tests/support/guard_loop.h builds it (S1's 10 ms loop
    /// period, the harness's +30 dB ceiling: no trip on the corpus).
    template <typename S>
    typename tap::mu::howl_guard<S>::config guard_config(size_t mics) {
        typename tap::mu::howl_guard<S>::config c;
        c.microphones            = mics;
        c.block_size             = k_block;
        c.sample_rate            = k_fs;
        c.detector.loop_period_s = S(0.010);
        c.detector.ceiling_db    = S(30);
        return c;
    }

    template <typename S>
    void bench_guard(benchmark::State& state, size_t mics, bool analyze) {
        tap::mu::howl_guard<S> g(guard_config<S>(mics));
        const std::vector<S>   x = white_corpus<S>(k_block);
        std::vector<S>         out(k_block);
        const auto             step = [&](size_t i) {
            const S* e = &x[(i % k_blocks) * k_block];
            if (analyze) {
                for (size_t m = 0; m < mics; ++m) {
                    g.analyze(m, e, S(1e-3), S(0.5));
                }
            }
            g.update();
            for (size_t m = 0; m < mics; ++m) {
                g.apply(m, e, out.data());
            }
        };
        for (size_t m = 0; m < mics; ++m) {
            g.analyze(m, x.data(), S(1e-3), S(0.5));
        }
        for (size_t i = 0; i < k_warm; ++i) {
            step(i);
        }
        size_t i = k_warm;
        for (auto _ : state) {
            step(i);
            benchmark::DoNotOptimize(out.data());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(k_block));
    }

} // namespace

BENCHMARK_CAPTURE(bench_howl_detector<double>, b64_f64, 64);
BENCHMARK_CAPTURE(bench_howl_detector<float>, b64_f32, 64);
BENCHMARK_TEMPLATE(bench_howl_bank, double);
BENCHMARK_TEMPLATE(bench_howl_bank, float);
BENCHMARK_TEMPLATE(bench_howl_tick, double);
BENCHMARK_TEMPLATE(bench_howl_tick, float);
BENCHMARK_TEMPLATE(bench_howl_tick_logs, double);
BENCHMARK_TEMPLATE(bench_howl_tick_logs, float);
BENCHMARK_TEMPLATE(bench_howl_fit, double);
BENCHMARK_TEMPLATE(bench_howl_fit, float);
BENCHMARK_TEMPLATE(bench_howl_readouts, double);
BENCHMARK_TEMPLATE(bench_howl_readouts, float);
BENCHMARK_CAPTURE(bench_guard<double>, policy_m1_f64, 1, false);
BENCHMARK_CAPTURE(bench_guard<float>, policy_m1_f32, 1, false);
BENCHMARK_CAPTURE(bench_guard<double>, m1_f64, 1, true);
BENCHMARK_CAPTURE(bench_guard<float>, m1_f32, 1, true);
BENCHMARK_CAPTURE(bench_guard<double>, m2_f64, 2, true);
BENCHMARK_CAPTURE(bench_guard<float>, m2_f32, 2, true);
