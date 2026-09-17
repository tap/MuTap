// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// THE BIT-IDENTITY GATE FOR EVERY DspTap PIN BUMP.
//
// Runs a fixed, xorshift-driven synthetic echo corpus through each of MuTap's
// signal-processing components in BOTH numeric profiles and prints one line
// per (component, profile):
//
//     FINGERPRINT <component> <profile> <16 hex digits>
//
// where the digits are FNV-1a (64-bit) over the raw bytes of every output
// sample the component produced. Any single-ULP change anywhere in a
// component's output stream flips its line. Components:
//
//     fdaf           partitioned_fdaf, the NLMS core with its control stack
//     fd_kalman      partitioned_fdkf at the certified preset calibration
//     pem_afc        the FDAF-PEM-AFROW canceller (speech predictor + fdaf)
//     postfilter     residual_suppressor alone, fed a fixed (e, yhat) pair
//     nn_suppressor  the learned post-filter alone, deterministic weights
//     aec_chain      the certified chain: fd_kalman + postfilter (preset)
//     aec_chain_nn   the learned chain:   fd_kalman + nn_suppressor (preset)
//
// For fdaf and fd_kalman both output channels are hashed: the error block
// and the echo-estimate block (the four-argument process_block), because
// error = desired - estimate can absorb a one-ULP move of the estimate when
// the near end is loud; the estimate stream alone exposes it.
//
// The corpus is generated in double from integer state with basic IEEE
// arithmetic only (no libm, no <random>, no wall clock, no filesystem) and
// rounded once to float, so both profiles consume identical sample values
// and the harness runs unchanged on bare metal. Determinism holds per
// (host, compiler, flags): libm and fp-contraction differ across hosts, so
// two fingerprints are comparable only when both runs were produced by the
// same build configuration on the same machine — which is exactly the pin-
// bump workflow below. The '#' header names the float32 FFT backend the
// binary was compiled with (backend=cmsis|vdsp|ooura), so a log is
// self-describing and the M55 legs can assert which backend they ran.
//
// How to diff two DspTap pins (the check every submodule bump runs):
//
//     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
//     cmake --build build --target mutap_fingerprint
//     ./build/tests/mutap_fingerprint > /tmp/before.txt
//     git -C submodules/dsptap checkout <new pin>
//     cmake --build build --target mutap_fingerprint
//     ./build/tests/mutap_fingerprint > /tmp/after.txt
//     diff /tmp/before.txt /tmp/after.txt
//
// An empty diff is the proof that the bump changed no output sample; every
// stage of the FFT plan (DspTap docs/audit-fft-and-code-smells.md) states
// which lines here must be unchanged and which are expected to move (the
// float profile through an FFT port; never the double golden model unless
// the plan says so). CI runs this binary on every leg, hosted and emulated
// (a "Fingerprints" step records the lines in each log, so two CI logs can
// be diffed the same way, and the M55's CMSIS and Ooura legs against each
// other) and once more, compiled twice, as the suppressor's
// branch-free/branchy parity check (MUTAP_SUPPRESSOR_BRANCHLESS, see
// include/mutap/postfilter.h): the two builds must print identical lines.
//
// Builds two ways: as the normal CMake target mutap_fingerprint (a ctest
// test on every target), and standalone as the parity job compiles it (g++
// on this file plus DspTap's Ooura .c files — see the branchless-parity job
// in .github/workflows/ci.yml).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/nn_chain.h"
#include "mutap/pem_afc.h"
#include "mutap/postfilter.h"

namespace {

    // The certified reference geometry: block 256, 8 partitions (2048 taps)
    // at 48 kHz — the calibration point of every preset.
    constexpr std::size_t k_block       = 256;
    constexpr std::size_t k_partitions  = 8;
    constexpr double      k_sample_rate = 48000.0;
    // Corpus length in blocks (~2.1 s of audio). Long enough to carry every
    // canceller through convergence and into the double-talk / quiet
    // segments; short enough for the double profile under soft-float
    // emulation on the Cortex-M legs.
    constexpr std::size_t k_blocks = 400;

    // The float32 FFT backend this binary was compiled against, from the
    // compile definitions DspTap's fft.h keys on (not the CMake cache).
#if defined(TAP_DSP_FFT_CMSIS)
    constexpr const char* k_backend = "cmsis";
#elif defined(TAP_DSP_FFT_ACCELERATE)
    constexpr const char* k_backend = "vdsp";
#else
    constexpr const char* k_backend = "ooura";
#endif

    /// xorshift32 -> uniform in [-1, 1), exactly representable in float:
    /// a 24-bit signed integer times a power of two, so the corpus is the
    /// same value set in both profiles by construction.
    class xorshift32 {
      public:
        explicit xorshift32(std::uint32_t seed) noexcept
            : m_s(seed) {}

        double next() noexcept {
            m_s ^= m_s << 13;
            m_s ^= m_s >> 17;
            m_s ^= m_s << 5;
            const auto q = static_cast<std::int32_t>(m_s >> 8) - 8388608; // [-2^23, 2^23)
            return static_cast<double>(q) * (1.0 / 8388608.0);
        }

      private:
        std::uint32_t m_s;
    };

    /// FNV-1a over raw sample bytes. Any ULP flips it.
    class fingerprint {
      public:
        template <typename Sample>
        void mix(const Sample* v, std::size_t n) noexcept {
            unsigned char bytes[sizeof(Sample)];
            for (std::size_t i = 0; i < n; ++i) {
                std::memcpy(bytes, &v[i], sizeof(Sample));
                for (const unsigned char b : bytes) {
                    m_h = (m_h ^ b) * 1099511628211ULL;
                }
            }
        }

        std::uint64_t value() const noexcept { return m_h; }

      private:
        std::uint64_t m_h = 1469598103934665603ULL;
    };

    /// One block of the corpus, produced on demand from fixed integer state
    /// (no stored corpus: the bare-metal targets are RAM-bound). Every
    /// component gets a fresh source, so every component sees the same
    /// blocks in the same order.
    ///
    ///   x     far end: uniform noise under a slow triangular level sweep
    ///   y     mic:     3-tap sparse echo of x + near end + a tiny floor
    ///   e     a converged canceller's residual (y minus 95 % of the echo)
    ///   yhat  that canceller's echo estimate (the echo itself)
    ///
    /// The near end is present in every third 40-block stretch from block
    /// 40 on (double talk), so each canceller first converges in single
    /// talk: one-pole-colored noise plus a white component, incoherent with
    /// the echo — enough structure to drive every data-dependent path the
    /// suppressors and the control stacks branch on.
    template <typename Sample>
    class corpus_source {
      public:
        corpus_source()
            : m_far(0x12345u)
            , m_near(0x9E3779B9u)
            , m_floor(0x2545F491u)
            , m_history(k_history, 0.0)
            , m_x(k_block)
            , m_y(k_block)
            , m_e(k_block)
            , m_yhat(k_block) {}

        void generate(std::size_t blk) noexcept {
            const std::size_t p   = blk % 200;
            const double      tri = static_cast<double>(p < 100 ? p : 200 - p) * 0.01;
            const double      amp = 0.05 + 0.1 * tri;
            const bool        dt  = (blk / 40) % 3 == 1;
            for (std::size_t i = 0; i < k_block; ++i) {
                const double far = amp * m_far.next();
                // Delay line: newest sample at index 0.
                for (std::size_t k = k_history - 1; k > 0; --k) {
                    m_history[k] = m_history[k - 1];
                }
                m_history[0] = far;
                const double echo =
                    0.25 * m_history[k_block / 2] - 0.12 * m_history[k_block] + 0.06 * m_history[3 * k_block / 2];
                double near = 0.0;
                if (dt) {
                    m_colored = 0.9 * m_colored + 0.1 * m_near.next();
                    near      = 0.6 * m_colored + 0.03 * m_near.next();
                }
                const double floor = 0.001 * m_floor.next();
                // Round once to float; the double profile widens exactly.
                m_x[i]    = static_cast<Sample>(static_cast<float>(far));
                m_y[i]    = static_cast<Sample>(static_cast<float>(echo + near + floor));
                m_e[i]    = static_cast<Sample>(static_cast<float>(0.05 * echo + near + floor));
                m_yhat[i] = static_cast<Sample>(static_cast<float>(echo));
            }
        }

        const Sample* x() const noexcept { return m_x.data(); }
        const Sample* y() const noexcept { return m_y.data(); }
        const Sample* e() const noexcept { return m_e.data(); }
        const Sample* yhat() const noexcept { return m_yhat.data(); }

      private:
        static constexpr std::size_t k_history = 3 * k_block / 2 + 1;
        xorshift32                   m_far;
        xorshift32                   m_near;
        xorshift32                   m_floor;
        std::vector<double>          m_history;
        double                       m_colored = 0.0;
        std::vector<Sample>          m_x;
        std::vector<Sample>          m_y;
        std::vector<Sample>          m_e;
        std::vector<Sample>          m_yhat;
    };

    /// Deterministic live weights at the shipping 48 kHz geometry (the
    /// values do not matter, only that the network's gains vary with the
    /// input and never change between runs).
    tap::mu::nn_suppressor_weights nn_weights() {
        const tap::mu::nn_geometry g{48000.0, 256, 26, 64, 96};
        xorshift32                 rng(0xC0FFEE11u);
        auto                       fill = [&rng](std::vector<float>& v, std::size_t n) {
            v.resize(n);
            for (auto& w : v) {
                w = static_cast<float>(rng.next() * 0.3);
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

    template <typename Sample>
    const char* profile_name() noexcept {
        if constexpr (std::is_same_v<Sample, float>) {
            return "float";
        }
        else {
            static_assert(std::is_same_v<Sample, double>, "two profiles: float and double");
            return "double";
        }
    }

    /// Runs `step(source, out, estimate)` once per block and prints the
    /// component's line. `out` is always hashed; `estimate` is hashed too
    /// when the step writes it (the two cancellers' second output channel).
    template <typename Sample, typename Step>
    void run(const char* component, bool hashes_estimate, Step&& step) {
        corpus_source<Sample> src;
        std::vector<Sample>   out(k_block);
        std::vector<Sample>   estimate(k_block);
        fingerprint           fp;
        for (std::size_t blk = 0; blk < k_blocks; ++blk) {
            src.generate(blk);
            step(src, out.data(), estimate.data());
            fp.mix(out.data(), k_block);
            if (hashes_estimate) {
                fp.mix(estimate.data(), k_block);
            }
        }
        std::printf("FINGERPRINT %s %s %016llx\n", component, profile_name<Sample>(),
                    static_cast<unsigned long long>(fp.value()));
    }

    template <typename Sample>
    void run_profile() {
        using namespace tap::mu;
        using src_t = corpus_source<Sample>;

        {
            typename partitioned_fdaf<Sample>::config cfg;
            cfg.block_size             = k_block;
            cfg.partitions             = k_partitions;
            cfg.ipc_step_scaling       = true;
            cfg.ipc_freeze_threshold   = Sample(0.1);
            cfg.transient_freeze_ratio = Sample(8);
            partitioned_fdaf<Sample> f(cfg);
            run<Sample>("fdaf", true,
                        [&f](const src_t& s, Sample* out, Sample* est) { f.process_block(s.x(), s.y(), out, est); });
        }
        {
            partitioned_fdkf<Sample> f(aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate).canceller);
            run<Sample>("fd_kalman", true,
                        [&f](const src_t& s, Sample* out, Sample* est) { f.process_block(s.x(), s.y(), out, est); });
        }
        {
            typename pem_afc<Sample>::config cfg;
            cfg.fdaf.block_size = k_block;
            cfg.fdaf.partitions = k_partitions;
            pem_afc<Sample> f(cfg);
            run<Sample>("pem_afc", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
        {
            auto cfg       = aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate).postfilter;
            cfg.block_size = k_block;
            residual_suppressor<Sample> f(cfg);
            run<Sample>("postfilter", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.e(), s.yhat(), out); });
        }
        {
            auto cfg = aec_chain_nn_preset<Sample>(k_block, k_partitions, k_sample_rate, nn_weights()).postfilter;
            nn_suppressor<Sample> f(std::move(cfg));
            run<Sample>("nn_suppressor", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.e(), s.yhat(), out); });
        }
        {
            aec_chain<Sample> f(aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate));
            run<Sample>("aec_chain", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
        {
            aec_chain_nn<Sample> f(aec_chain_nn_preset<Sample>(k_block, k_partitions, k_sample_rate, nn_weights()));
            run<Sample>("aec_chain_nn", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
    }

} // namespace

int main() {
    // Informational header (not part of the diffed lines): the geometry, the
    // float32 FFT backend and the suppressor form this binary compiled, so a
    // log is self-describing and the M55 legs can assert their backend.
    std::printf("# mutap_fingerprint block=%u partitions=%u rate=%u blocks=%u backend=%s branchless=%d\n",
                static_cast<unsigned>(k_block), static_cast<unsigned>(k_partitions),
                static_cast<unsigned>(k_sample_rate), static_cast<unsigned>(k_blocks), k_backend,
                MUTAP_SUPPRESSOR_BRANCHLESS);
    run_profile<float>();
    run_profile<double>();
    // CTest's pass criterion on bare metal, where semihosting does not
    // reliably propagate the exit code: printed only after every line.
    std::printf("FINGERPRINT_COMPLETE\n");
    return 0;
}
