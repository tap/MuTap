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
// (host, compiler, flags, C library): libm, fp-contraction and the float FFT
// backend differ across targets, so every CI leg has its own expected lines,
// committed in tests/fingerprints/<leg>.txt. Measured at DspTap 0c5bf59: no
// line is the same on all nine legs; the two Linux compilers (and the
// sanitizer build) agree on all 14, and the two M55 legs on the seven double
// rows only (the float rows go through different FFT engines). The '#'
// header names the float32 FFT backend the binary was compiled with
// (backend=cmsis|vdsp|split_radix; "ooura" in logs from pins up to DspTap
// 8350f13, when the default engine was Ooura's C or its bit-identical port)
// and the ABI tag DspTap's fft.h compiled under
// (abi=fft_cmsis|fft_vdsp|fft_split_radix, tap::dsp::k_real_fft_abi_tag), so
// a log is self-describing and each emulated leg asserts which engine it
// ran. Neither is part of the compared FINGERPRINT lines.
//
// THE GATE. The build names its CI leg with -DMUTAP_FINGERPRINT_LEG=<leg>;
// tests/CMakeLists.txt compiles that leg's committed lines into this binary,
// and after printing its own lines the harness compares them row by row
// ((component, profile) must be unique on both sides). Every difference
// prints one "FINGERPRINT_DIFF <component> <profile>: expected <hex>, actual
// <hex>" line, then a FINGERPRINT_FAIL verdict, then the run's lines once
// more as a paste-ready block ("FINGERPRINT_NEW leg=<leg> FINGERPRINT ..."),
// and the binary exits 1. On hosted legs ctest judges the exit code (and the
// FINGERPRINT_FAIL line); on bare metal, where semihosting does not reliably
// carry the exit code, it requires "FINGERPRINT_PASS leg=<leg> ". In CI every
// leg runs scripts/fingerprints.sh check, which also requires that PASS line,
// so a leg whose id is not wired fails instead of passing unchecked. With no
// leg configured (a developer build on an arbitrary host) the lines are
// printed and nothing is compared.
//
// Re-recording (a change that is MEANT to move output bits: a DspTap pin
// bump whose stage moves lines, or a MuTap change to a component's
// arithmetic):
//
//   1. Push the change. Every leg whose lines moved fails, and every leg runs
//      its Fingerprints step even when an earlier step of its job failed
//      (the M55 job's split-radix leg too), so ONE run yields every leg. A
//      failing leg's job uploads the artifact "fingerprints-<leg>": its
//      complete, commit-ready tests/fingerprints/<leg>.txt, with this file's
//      description kept and a "# Recorded:" line naming the DspTap pin, the
//      run, the job and the runner image. Legs that did not fail did not move.
//   2. Download the artifacts of that run (the run page's Artifacts list, or
//      gh run download <run-id> -p 'fingerprints-*' -D /tmp/fp) and copy them
//      in (cp /tmp/fp/fingerprints-*/*.txt tests/fingerprints/). This is the
//      only route for the legs with no local rig (macOS, Windows, Hexagon,
//      the clang ASan build), and it works for all nine at once. From a
//      saved log instead: scripts/fingerprints.sh record <leg> <job-log.txt>
//      reads only that leg's FINGERPRINT_NEW block (never the diff lines, and
//      never another leg's block in the same log). The Linux and QEMU legs
//      also reproduce locally with the CI toolchain, e.g.
//          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTAP_FINGERPRINT_LEG=linux-gcc
//          cmake --build build --target mutap_fingerprint
//          scripts/fingerprints.sh check linux-gcc --test-dir build
//      (the M33/M55 legs: the toolchain file, MinSizeRel and the leg id, as
//      in .github/workflows/ci.yml).
//   3. In the PR, list which lines moved on which legs and why (the diff of
//      tests/fingerprints/ is the record), against what the stage predicted:
//      every stage of the FFT plan (DspTap docs/audit-fft-and-code-smells.md)
//      states which lines must hold and which may move (the float profile
//      through an FFT port; never the double golden model unless the plan
//      says so, so a moved double row needs its plan citation). A line that
//      moved and was not predicted is the regression the gate exists to
//      catch, not a line to re-record.
//
// CI also runs this binary, compiled twice, as the suppressor's
// branch-free/branchy parity check (MUTAP_SUPPRESSOR_BRANCHLESS, see
// include/mutap/postfilter.h): the two builds must print identical lines.
//
// Builds two ways: as the normal CMake target mutap_fingerprint (a ctest
// test on every target), and standalone as the parity job compiles it (g++
// on this file alone, header-only since DspTap Stage 2c — see the
// branchless-parity job in .github/workflows/ci.yml); standalone there is
// no generated expectation header, so nothing is compared.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include("mutap_fingerprint_expected.h")
#include "mutap_fingerprint_expected.h"
#else
namespace mutap_fingerprint_expected {
    // Standalone compile (the branchless-parity job): no leg, no lines.
    inline constexpr std::string_view                k_leg;
    inline constexpr std::string_view                k_source;
    inline constexpr std::array<std::string_view, 0> k_lines{};
} // namespace mutap_fingerprint_expected
#endif

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
    constexpr const char* k_backend = "split_radix";
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
                const double floor = 0.0010000001 * m_floor.next(); // DRILL: moves every line on every leg
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

    /// One printed FINGERPRINT line ("FINGERPRINT <component> <profile>
    /// <16 hex digits>", no newline), kept for the comparison.
    using fingerprint_line = std::array<char, 96>;

    /// Runs `step(source, out, estimate)` once per block, prints the
    /// component's line and appends it to `lines`. `out` is always hashed;
    /// `estimate` is hashed too when the step writes it (the two cancellers'
    /// second output channel).
    template <typename Sample, typename Step>
    void run(std::vector<fingerprint_line>& lines, const char* component, bool hashes_estimate, Step&& step) {
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
        fingerprint_line line{};
        std::snprintf(line.data(), line.size(), "FINGERPRINT %s %s %016llx", component, profile_name<Sample>(),
                      static_cast<unsigned long long>(fp.value()));
        std::printf("%s\n", line.data());
        lines.push_back(line);
    }

    template <typename Sample>
    void run_profile(std::vector<fingerprint_line>& lines) {
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
            run<Sample>(lines, "fdaf", true,
                        [&f](const src_t& s, Sample* out, Sample* est) { f.process_block(s.x(), s.y(), out, est); });
        }
        {
            partitioned_fdkf<Sample> f(aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate).canceller);
            run<Sample>(lines, "fd_kalman", true,
                        [&f](const src_t& s, Sample* out, Sample* est) { f.process_block(s.x(), s.y(), out, est); });
        }
        {
            typename pem_afc<Sample>::config cfg;
            cfg.fdaf.block_size = k_block;
            cfg.fdaf.partitions = k_partitions;
            pem_afc<Sample> f(cfg);
            run<Sample>(lines, "pem_afc", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
        {
            auto cfg       = aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate).postfilter;
            cfg.block_size = k_block;
            residual_suppressor<Sample> f(cfg);
            run<Sample>(lines, "postfilter", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.e(), s.yhat(), out); });
        }
        {
            auto cfg = aec_chain_nn_preset<Sample>(k_block, k_partitions, k_sample_rate, nn_weights()).postfilter;
            nn_suppressor<Sample> f(std::move(cfg));
            run<Sample>(lines, "nn_suppressor", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.e(), s.yhat(), out); });
        }
        {
            aec_chain<Sample> f(aec_chain_preset<Sample>(k_block, k_partitions, k_sample_rate));
            run<Sample>(lines, "aec_chain", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
        {
            aec_chain_nn<Sample> f(aec_chain_nn_preset<Sample>(k_block, k_partitions, k_sample_rate, nn_weights()));
            run<Sample>(lines, "aec_chain_nn", false,
                        [&f](const src_t& s, Sample* out, Sample*) { f.process_block(s.x(), s.y(), out); });
        }
    }

    /// "FINGERPRINT <component> <profile>": the line without its hash.
    std::string_view key_of(std::string_view line) noexcept {
        return line.substr(0, line.rfind(' '));
    }

    /// "<component> <profile>", for messages.
    std::string_view name_of(std::string_view line) noexcept {
        const std::string_view key = key_of(line);
        return key.substr(key.find(' ') + 1);
    }

    /// The 16 hex digits.
    std::string_view hash_of(std::string_view line) noexcept {
        return line.substr(line.rfind(' ') + 1);
    }

    /// printf's precision argument for a string_view.
    int len(std::string_view s) noexcept {
        return static_cast<int>(s.size());
    }

    /// Compares the printed lines with the configured leg's committed
    /// expectation, prints the verdict and returns whether it passed (always
    /// true when no leg is configured: nothing to compare against).
    ///
    /// Rows are matched by (component, profile), so the key must be unique on
    /// both sides: configure rejects a file that repeats one, and a harness
    /// that prints one twice fails here (otherwise one of the two would be
    /// compared against nothing). A changed hash, a row the run did not print
    /// and a row the file does not have each fail the leg with one
    /// FINGERPRINT_DIFF line. On a failure the harness then prints its lines
    /// again as a FINGERPRINT_NEW block tagged with the leg, the only lines
    /// scripts/fingerprints.sh record reads: the paste-ready replacement.
    /// The diff and verdict lines never contain "FINGERPRINT <component>",
    /// so they cannot be mistaken for printed lines; the NEW block repeats
    /// the printed lines verbatim behind its prefix.
    bool check(const std::vector<fingerprint_line>& printed) {
        namespace expected = mutap_fingerprint_expected;
        if (expected::k_leg.empty()) {
            std::printf("FINGERPRINT_UNCHECKED no MUTAP_FINGERPRINT_LEG configured: lines printed, not compared\n");
            return true;
        }
        const int leg    = len(expected::k_leg);
        const int source = len(expected::k_source);

        std::size_t duplicates = 0;
        for (std::size_t i = 0; i < printed.size(); ++i) {
            for (std::size_t j = i + 1; j < printed.size(); ++j) {
                if (key_of(printed[i].data()) == key_of(printed[j].data())) {
                    const std::string_view name = name_of(printed[i].data());
                    std::printf("FINGERPRINT_DUPLICATE %.*s is printed twice (lines %u and %u): each (component, "
                                "profile) must be printed once, or one of them is compared against nothing\n",
                                len(name), name.data(), static_cast<unsigned>(i + 1), static_cast<unsigned>(j + 1));
                    ++duplicates;
                }
            }
        }

        std::size_t differing  = 0;
        std::size_t unexpected = 0;
        for (const std::string_view want : expected::k_lines) {
            const fingerprint_line* got = nullptr;
            for (const fingerprint_line& p : printed) {
                if (got == nullptr && key_of(p.data()) == key_of(want)) {
                    got = &p;
                }
            }
            const std::string_view name = name_of(want);
            const std::string_view old  = hash_of(want);
            if (got == nullptr) {
                std::printf("FINGERPRINT_DIFF %.*s: expected %.*s, actual (not printed)\n", len(name), name.data(),
                            len(old), old.data());
                ++differing;
            }
            else if (std::string_view(got->data()) != want) {
                const std::string_view now = hash_of(got->data());
                std::printf("FINGERPRINT_DIFF %.*s: expected %.*s, actual %.*s\n", len(name), name.data(), len(old),
                            old.data(), len(now), now.data());
                ++differing;
            }
        }
        for (const fingerprint_line& p : printed) {
            bool known = false;
            for (const std::string_view want : expected::k_lines) {
                known = known || key_of(want) == key_of(p.data());
            }
            if (!known) {
                const std::string_view name = name_of(p.data());
                const std::string_view now  = hash_of(p.data());
                std::printf("FINGERPRINT_DIFF %.*s: expected (not in the file), actual %.*s\n", len(name), name.data(),
                            len(now), now.data());
                ++differing;
                ++unexpected;
            }
        }

        const auto n_printed  = static_cast<unsigned>(printed.size());
        const auto n_expected = static_cast<unsigned>(expected::k_lines.size());
        if (duplicates == 0 && differing == 0 && n_printed == n_expected) {
            std::printf("FINGERPRINT_PASS leg=%.*s lines=%u: all %u printed lines equal the %u lines of %.*s\n", leg,
                        expected::k_leg.data(), n_printed, n_printed, n_expected, source, expected::k_source.data());
            return true;
        }
        // Rows = the union of the file's keys and the printed keys.
        const auto rows = static_cast<unsigned>(expected::k_lines.size() + unexpected);
        std::printf("FINGERPRINT_FAIL leg=%.*s: %u of %u rows differ, %u printed key(s) repeated; printed %u lines, "
                    "%.*s has %u\n",
                    leg, expected::k_leg.data(), static_cast<unsigned>(differing), rows,
                    static_cast<unsigned>(duplicates), n_printed, source, expected::k_source.data(), n_expected);
        if (duplicates != 0) {
            std::printf("FINGERPRINT_FAIL The harness itself prints a (component, profile) twice; fix its labels "
                        "before recording anything.\n");
            return false;
        }
        for (const fingerprint_line& p : printed) {
            std::printf("FINGERPRINT_NEW leg=%.*s %s\n", leg, expected::k_leg.data(), p.data());
        }
        std::printf("FINGERPRINT_FAIL If an output bit was meant to move (a DspTap pin bump whose stage moves it, or "
                    "a change to a component's arithmetic), the FINGERPRINT_NEW block above is this leg's new "
                    "expectation: scripts/fingerprints.sh record %.*s <this log> writes it to %.*s (in CI, the "
                    "artifact fingerprints-%.*s of this run is that file). See \"Re-recording\" at the top of "
                    "tests/fingerprint_harness.cpp. If it was not meant to move, this is the regression.\n",
                    leg, expected::k_leg.data(), source, expected::k_source.data(), leg, expected::k_leg.data());
        return false;
    }

} // namespace

int main() {
    // Informational header (not part of the diffed lines): the geometry, the
    // float32 FFT backend, the ABI tag and the suppressor form this binary
    // compiled, so a log is self-describing and the emulated legs can assert
    // their backend.
    std::printf("# mutap_fingerprint block=%u partitions=%u rate=%u blocks=%u backend=%s abi=%s branchless=%d\n",
                static_cast<unsigned>(k_block), static_cast<unsigned>(k_partitions),
                static_cast<unsigned>(k_sample_rate), static_cast<unsigned>(k_blocks), k_backend,
                tap::dsp::k_real_fft_abi_tag, MUTAP_SUPPRESSOR_BRANCHLESS);
    std::vector<fingerprint_line> lines;
    run_profile<float>(lines);
    run_profile<double>(lines);
    // Printed only after every line: CTest's pass criterion on bare metal
    // when no leg is configured (semihosting does not reliably propagate the
    // exit code). With a leg, the verdict below is the criterion.
    std::printf("FINGERPRINT_COMPLETE\n");
    return check(lines) ? 0 : 1;
}
