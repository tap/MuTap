// Host benchmarks for the AEC hot path — the scalar baselines every
// optimization is measured against (bench/README.md has the workflow).
// Three component layers plus the deployment number:
//  - fdkf:       partitioned_fdkf::process_block at the certified
//                canceller config, adaptation live — the core's cost.
//  - suppressor: residual_suppressor::process_block alone.
//  - shadow:     the rescue's 2-partition comparator canceller alone
//                (measured ~11 % of the chain — cheaper than the
//                partition-count estimate suggested).
//  - chain:      the full certified aec_chain (canceller + suppressor +
//                both rescue triggers; float32 also the narrowband
//                guard) — what a deployment pays per block.
// All state is warmed to steady state before timing so the constraint
// path and converged gains are what is measured, and inputs cycle
// through a precomputed noise-echo corpus so no rescue trigger fires.
// Items processed are SAMPLES, so benchmark's items/s divided by the
// rate gives the x-realtime figure directly.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"

namespace {

    struct geometry {
        double fs;
        size_t block;
        size_t partitions;
    };
    constexpr geometry k_48k{48000.0, 256, 8}; // 2048 taps, the certified 48 kHz shape
    constexpr geometry k_16k{16000.0, 256, 4}; // 1024 taps, the certified 16 kHz shape

    /// Precomputed far-end / mic corpus: white far end through a sparse
    /// synthetic echo path (three taps, 12 dB coupling loss) plus a
    /// -60 dB near-end floor. Stationary by construction — the rescue
    /// and the narrowband guard never engage, so the timed loop is the
    /// steady adapting path.
    template <typename S>
    struct corpus {
        std::vector<S> x, y;
        size_t         blocks;
        size_t         block;
        explicit corpus(const geometry& g)
            : blocks(512)
            , block(g.block) {
            std::mt19937                     rng(0xB0B0);
            std::normal_distribution<double> nd(0.0, 0.1);
            const size_t                     n = blocks * g.block;
            std::vector<double>              xd(n), yd(n);
            for (auto& v : xd) {
                v = nd(rng);
            }
            const size_t d1 = g.block / 2, d2 = g.block, d3 = 3 * g.block / 2;
            for (size_t i = 0; i < n; ++i) {
                double e = 0.25 * xd[(i + n - d1) % n] - 0.12 * xd[(i + n - d2) % n] + 0.06 * xd[(i + n - d3) % n];
                yd[i]    = e + 0.001 * nd(rng);
            }
            x.assign(xd.begin(), xd.end());
            y.assign(yd.begin(), yd.end());
        }
        const S* xb(size_t i) const noexcept { return &x[(i % blocks) * block]; }
        const S* yb(size_t i) const noexcept { return &y[(i % blocks) * block]; }
    };

    template <typename S>
    typename tap::mu::aec_chain<S>::config preset_of(const geometry& g) {
        return tap::mu::aec_chain_preset<S>(g.block, g.partitions, g.fs);
    }

    template <typename Proc, typename S>
    void warm(Proc& p, const corpus<S>& c, std::vector<S>& e, size_t blocks = 2000) {
        for (size_t i = 0; i < blocks; ++i) {
            p.process_block(c.xb(i), c.yb(i), e.data());
        }
    }

    template <typename S>
    void bench_fdkf(benchmark::State& state, geometry g) {
        tap::mu::partitioned_fdkf<S> core(preset_of<S>(g).canceller);
        corpus<S>                    c(g);
        std::vector<S>               e(g.block);
        warm(core, c, e);
        size_t i = 2000;
        for (auto _ : state) {
            core.process_block(c.xb(i), c.yb(i), e.data());
            benchmark::DoNotOptimize(e.data());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * g.block));
    }

    template <typename S>
    void bench_suppressor(benchmark::State& state, geometry g) {
        auto pf       = preset_of<S>(g).postfilter;
        pf.block_size = g.block;
        tap::mu::residual_suppressor<S> sup(pf);
        corpus<S>                       c(g);
        std::vector<S>                  e(g.block);
        // mid = the corpus mic, yhat = the corpus echo estimate stand-in
        for (size_t i = 0; i < 2000; ++i) {
            sup.process_block(c.yb(i), c.xb(i), e.data());
        }
        size_t i = 2000;
        for (auto _ : state) {
            sup.process_block(c.yb(i), c.xb(i), e.data());
            benchmark::DoNotOptimize(e.data());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * g.block));
    }

    template <typename S>
    void bench_shadow(benchmark::State& state, geometry g) {
        auto cfg      = preset_of<S>(g);
        auto sc       = cfg.canceller;
        sc.partitions = cfg.shadow_partitions;
        sc.transition = cfg.shadow_transition;
        tap::mu::partitioned_fdkf<S> shadow(sc);
        corpus<S>                    c(g);
        std::vector<S>               e(g.block);
        warm(shadow, c, e);
        size_t i = 2000;
        for (auto _ : state) {
            shadow.process_block(c.xb(i), c.yb(i), e.data());
            benchmark::DoNotOptimize(e.data());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * g.block));
    }

    template <typename S>
    void bench_chain(benchmark::State& state, geometry g) {
        tap::mu::aec_chain<S> chain(preset_of<S>(g));
        corpus<S>             c(g);
        std::vector<S>        e(g.block);
        warm(chain, c, e);
        size_t i = 2000;
        for (auto _ : state) {
            chain.process_block(c.xb(i), c.yb(i), e.data());
            benchmark::DoNotOptimize(e.data());
            ++i;
        }
        state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * g.block));
    }

} // namespace

BENCHMARK_CAPTURE(bench_fdkf<double>, 48k_f64, k_48k);
BENCHMARK_CAPTURE(bench_fdkf<float>, 48k_f32, k_48k);
BENCHMARK_CAPTURE(bench_fdkf<double>, 16k_f64, k_16k);
BENCHMARK_CAPTURE(bench_fdkf<float>, 16k_f32, k_16k);

BENCHMARK_CAPTURE(bench_suppressor<double>, 48k_f64, k_48k);
BENCHMARK_CAPTURE(bench_suppressor<float>, 48k_f32, k_48k);
BENCHMARK_CAPTURE(bench_suppressor<double>, 16k_f64, k_16k);
BENCHMARK_CAPTURE(bench_suppressor<float>, 16k_f32, k_16k);

BENCHMARK_CAPTURE(bench_shadow<double>, 48k_f64, k_48k);
BENCHMARK_CAPTURE(bench_shadow<float>, 48k_f32, k_48k);
BENCHMARK_CAPTURE(bench_shadow<double>, 16k_f64, k_16k);
BENCHMARK_CAPTURE(bench_shadow<float>, 16k_f32, k_16k);

BENCHMARK_CAPTURE(bench_chain<double>, 48k_f64, k_48k);
BENCHMARK_CAPTURE(bench_chain<float>, 48k_f32, k_48k);
BENCHMARK_CAPTURE(bench_chain<double>, 16k_f64, k_16k);
BENCHMARK_CAPTURE(bench_chain<float>, 16k_f32, k_16k);
