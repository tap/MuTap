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
//                 3 = full certified chain, 4 = learned nn_suppressor,
//                 5 = the feedback canceller on the speech cascade (pem_afc,
//                     speech_predictor, partitioned_fdkf), 6 = the same on
//                     the warped predictor with a 2-partition shadow,
//                     7 = the howl guard, one microphone
// MUTAP_SC_RATE:  layers 0-4: 0 = 48 kHz (2048 taps), 1 = 16 kHz (1024 taps);
//                 layers 5-7 (the AFC path, production-readiness M2a): the
//                 two candidate product geometries, both at 48 kHz —
//                 0 = block 256 / 8 partitions (2048 taps, what mutap.afc~
//                 ships), 1 = block 64 / 16 partitions (1024 taps, what the
//                 closed-loop claims and the guard's rows are measured at).
//
// The AFC layers run the same corpus (an open-loop synthetic echo: the
// feedback canceller is an identification core with a predictor in front,
// and the instruction count does not depend on the loop being closed) at
// pem_afc's defaults for everything the geometry does not set — the speech
// predictor's lags are 32 / 400 samples (120 Hz pitch floor at 48 kHz), the
// analysis window 1024 samples, the shadow smoothing pem_afc's default. The
// guard scenario feeds the corpus's y as the residual with a constant "ok"
// verdict (A' 1e-3, D 0.5: -30 / -3 dB, under the factory thresholds), so
// it arms and, where the run is long enough, declares and opens; its cost
// is the detector bank plus the policy tick, per block.
//
// Layer 4 is the learned residual suppressor (nn_suppressor.h) at the two
// trained geometries — 48 kHz / hop 256 / 26 bands and 16 kHz / hop 64 /
// 22 bands, dense 64, GRU 96 — with deterministic xorshift weights (no
// file I/O on bare metal; the cost does not depend on the values). This
// is the per-hop cost the wake-word plan's ceilings are stated against.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/howl_guard.h"
#include "mutap/lpc.h"
#include "mutap/nn_suppressor.h"
#include "mutap/pem_afc.h"
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

#if MUTAP_SC_LAYER >= 5
#if MUTAP_SC_RATE == 0
    constexpr geometry k_geo{48000.0, 256, 8}; // the external's shipping geometry, 2048 taps
#else
    constexpr geometry k_geo{48000.0, 64, 16}; // the claims' geometry, 1024 taps
#endif
#elif MUTAP_SC_RATE == 0
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

    // Each binary is one scenario, so the helper the other layers use is
    // unreferenced in some of them by design.
    [[maybe_unused]] auto preset() {
        return tap::mu::aec_chain_preset<float>(k_geo.block, k_geo.partitions, k_geo.fs);
    }

    // Deterministic weights at the scenario's trained geometry (layer 4).
    [[maybe_unused]] tap::mu::nn_suppressor_weights nn_weights() {
#if MUTAP_SC_RATE == 0
        const tap::mu::nn_geometry g{48000.0, 256, 26, 64, 96};
#else
        const tap::mu::nn_geometry g{16000.0, 64, 22, 64, 96};
#endif
        std::uint32_t s    = 0x2545F491u;
        auto          fill = [&s](std::vector<float>& v, std::size_t n) {
            v.resize(n);
            for (auto& x : v) {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                x = (static_cast<float>(s) / 2147483648.0f - 1.0f) * 0.3f;
            }
        };
        tap::mu::nn_suppressor_weights w;
        w.geometry = g;
        fill(w.dense_in_w, g.dense * g.features());
        fill(w.dense_in_b, g.dense);
        fill(w.gru_w_ih, 3 * g.gru * g.dense);
        fill(w.gru_w_hh, 3 * g.gru * g.gru);
        fill(w.gru_b_ih, 3 * g.gru);
        fill(w.gru_b_hh, 3 * g.gru);
        fill(w.dense_out_w, g.bands * g.gru);
        fill(w.dense_out_b, g.bands);
        return w;
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
        tap::mu::partitioned_fdkf<float> core(preset().canceller);
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
        tap::mu::residual_suppressor<float> sup(pf);
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
        tap::mu::partitioned_fdkf<float> shadow(sc);
        for (std::size_t i = 0; i < k_warm; ++i) {
            shadow.process_block(c.xb(i), c.yb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            shadow.process_block(c.xb(i), c.yb(i), e.data());
            sink += sum_out(e);
        }
#elif MUTAP_SC_LAYER == 4
        // The suppressor's hop is the trained one (256 at 48 kHz, 64 at
        // 16 kHz); the corpus block is 256, so the 16 kHz scenario feeds
        // four hops per corpus block. Same audio duration per scenario.
        tap::mu::nn_suppressor<float>::config cfg;
        cfg.weights = nn_weights();
        tap::mu::nn_suppressor<float> sup(std::move(cfg));
        const std::size_t             hop  = sup.block_size();
        const std::size_t             hops = k_geo.block / hop;
        std::vector<float>            eh(hop);
        for (std::size_t i = 0; i < k_warm; ++i) {
            for (std::size_t h = 0; h < hops; ++h) {
                sup.process_block(c.yb(i) + h * hop, c.xb(i) + h * hop, eh.data());
            }
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            for (std::size_t h = 0; h < hops; ++h) {
                sup.process_block(c.yb(i) + h * hop, c.xb(i) + h * hop, eh.data());
                sink += sum_out(eh);
            }
        }
#elif MUTAP_SC_LAYER == 5 || MUTAP_SC_LAYER == 6
        // The feedback canceller (production-readiness M2a): the speech
        // cascade, or the warped predictor with the 2-partition shadow the
        // external ships (@shadow 2), on the Kalman core.
#if MUTAP_SC_LAYER == 5
        using afc_type = tap::mu::pem_afc<float, tap::mu::speech_predictor<float>, tap::mu::partitioned_fdkf<float>>;
#else
        using afc_type =
            tap::mu::pem_afc<float, tap::mu::warped_lpc_predictor<float>, tap::mu::partitioned_fdkf<float>>;
#endif
        typename afc_type::config cfg;
        cfg.fdaf.block_size = k_geo.block;
        cfg.fdaf.partitions = k_geo.partitions;
#if MUTAP_SC_LAYER == 6
        cfg.shadow_partitions = 2;
#endif
        afc_type afc(cfg);
        for (std::size_t i = 0; i < k_warm; ++i) {
            afc.process_block(c.xb(i), c.yb(i), e.data());
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            afc.process_block(c.xb(i), c.yb(i), e.data());
            sink += sum_out(e);
        }
#elif MUTAP_SC_LAYER == 7
        // The howl guard, one microphone (M2a's guard_m1): one analyze()
        // on the corpus's y as the residual, update(), apply(), per block.
        typename tap::mu::howl_guard<float>::config gc;
        gc.microphones = 1;
        gc.block_size  = k_geo.block;
        gc.sample_rate = k_geo.fs;
        tap::mu::howl_guard<float> guard(gc);
        const auto                 tick = [&](std::size_t i) noexcept {
            guard.analyze(0, c.yb(i), 1e-3f, 0.5f);
            guard.update();
            guard.apply(0, c.yb(i), e.data());
        };
        for (std::size_t i = 0; i < k_warm; ++i) {
            tick(i);
        }
        for (std::size_t i = k_warm; i < k_warm + k_timed; ++i) {
            tick(i);
            sink += sum_out(e);
        }
#else
        tap::mu::aec_chain<float> chain(preset());
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
