// Deterministic fixed workloads for the instruction-count ratchet
// (bench/README.md). One scenario per binary, selected at compile time
// (MUTAP_SC_LAYER / MUTAP_SC_RATE) because bare-metal targets have no argv.
//
// The workloads mirror the wall-clock bench layers (bench/bench_aec.cpp) so
// a vector optimization shows up in both. All are FLOAT32 — the deployment
// precision the embedded targets run (double is soft-float on the M55 and
// not the optimization target; the float32 parity gates in
// tests/test_float32.cpp are the correctness oracle). Each certified
// geometry (block 256; 8 partitions at 48 kHz, 4 at 16 kHz) is a scenario.
//
// The qemu plugin counts the whole run including setup; the timed loop is
// sized so it dominates the one-time FFT-plan/corpus cost. The corpus is a
// self-contained xorshift-driven synthetic echo (no <random> distribution,
// so counts do not drift with a toolchain's libstdc++), and every block's
// output feeds a checksum that both defeats dead-code elimination and pins
// cross-run determinism.
//
// MUTAP_SC_LAYER: 0 = fdkf core, 1 = suppressor, 2 = shadow canceller,
//                 3 = full certified chain
// MUTAP_SC_RATE:  0 = 48 kHz (2048 taps), 1 = 16 kHz (1024 taps)
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"

#ifndef MUTAP_SC_LAYER
#define MUTAP_SC_LAYER 3
#endif
#ifndef MUTAP_SC_RATE
#define MUTAP_SC_RATE 0
#endif

namespace {

    struct geometry {
        double      fs;
        std::size_t block;
        std::size_t partitions;
    };

#if MUTAP_SC_RATE == 0
    constexpr geometry k_geo{48000.0, 256, 8};
#else
    constexpr geometry k_geo{16000.0, 256, 4};
#endif

    // Timed blocks per scenario. Sized so the loop dominates setup while
    // keeping QEMU system-emulation time bounded; instruction counts land
    // in the tens-to-hundreds of millions (CI records the exact values).
    constexpr std::size_t k_warm  = 128;
    constexpr std::size_t k_timed = 400;

    // Deterministic broadband corpus: xorshift uniform in [-1, 1] scaled to
    // ~0.1 RMS, echo = a sparse 3-tap path, plus a tiny near-end floor.
    struct corpus {
        std::vector<float> x, y;
        std::size_t        blocks;
        std::size_t        block;
        explicit corpus(const geometry& g)
            : blocks(192)
            , block(g.block) {
            const std::size_t n = blocks * g.block;
            x.resize(n);
            y.resize(n);
            std::uint32_t s    = 0x9E3779B9u;
            auto          next = [&s]() noexcept {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                return (static_cast<float>(s) / 2147483648.0f - 1.0f) * 0.1f;
            };
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = next();
            }
            const std::size_t d1 = g.block / 2, d2 = g.block, d3 = 3 * g.block / 2;
            for (std::size_t i = 0; i < n; ++i) {
                float e = 0.25f * x[(i + n - d1) % n] - 0.12f * x[(i + n - d2) % n] + 0.06f * x[(i + n - d3) % n];
                y[i]    = e + 0.001f * next();
            }
        }
        const float* xb(std::size_t i) const noexcept { return &x[(i % blocks) * block]; }
        const float* yb(std::size_t i) const noexcept { return &y[(i % blocks) * block]; }
    };

    auto preset() {
        return mutap::aec_chain_preset<float>(k_geo.block, k_geo.partitions, k_geo.fs);
    }

    // Accumulate a checksum over the timed output so nothing is dead code.
    double sum_out(const std::vector<float>& e) noexcept {
        double s = 0.0;
        for (float v : e) {
            s += static_cast<double>(v);
        }
        return s;
    }

    double run() {
        corpus             c(k_geo);
        std::vector<float> e(k_geo.block);
        double             sink = 0.0;

#if MUTAP_SC_LAYER == 0
        mutap::partitioned_fdkf<float> core(preset().canceller);
        for (std::size_t i = 0; i < k_warm; ++i) {
            core.process_block(c.xb(i), c.yb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            core.process_block(c.xb(i), c.yb(i), e.data());
            sink += sum_out(e);
        }
#elif MUTAP_SC_LAYER == 1
        auto pf       = preset().postfilter;
        pf.block_size = k_geo.block;
        mutap::residual_suppressor<float> sup(pf);
        for (std::size_t i = 0; i < k_warm; ++i) {
            sup.process_block(c.yb(i), c.xb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            sup.process_block(c.yb(i), c.xb(i), e.data());
            sink += sum_out(e);
        }
#elif MUTAP_SC_LAYER == 2
        auto cfg      = preset();
        auto sc       = cfg.canceller;
        sc.partitions = cfg.shadow_partitions;
        sc.transition = cfg.shadow_transition;
        mutap::partitioned_fdkf<float> shadow(sc);
        for (std::size_t i = 0; i < k_warm; ++i) {
            shadow.process_block(c.xb(i), c.yb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            shadow.process_block(c.xb(i), c.yb(i), e.data());
            sink += sum_out(e);
        }
#else
        mutap::aec_chain<float> chain(preset());
        for (std::size_t i = 0; i < k_warm; ++i) {
            chain.process_block(c.xb(i), c.yb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            chain.process_block(c.xb(i), c.yb(i), e.data());
            sink += sum_out(e);
        }
#endif
        return sink;
    }

} // namespace

int main() {
    const double checksum = run();
    const bool   ok       = checksum == checksum; // NaN would poison it
    std::printf("MUTAP_ICOUNT_DONE ok=%d checksum=%.17g\n", ok ? 1 : 0, checksum);
    return ok ? 0 : 1;
}
