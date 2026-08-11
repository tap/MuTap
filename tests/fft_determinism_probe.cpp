// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// #31 probe: is the float32 real FFT a fixed function of its input, and if
// not, what selects the draw and how far apart are the draws?
//
// Background. The macOS float32 failures in #31 are a per-process bifurcation:
// the same binary on the same host passes one repetition and fails the next,
// landing on values that are bit-identical across machines, weeks and build
// types. A 200-process run of the original two-geometry version of this probe
// established the mechanism — vDSP_fft_zrip at N=2048 returns one of two
// bit-exact outputs for identical input, 145/55, while N=512 and Ooura are
// stable. This version adds the three measurements that decide what to do
// about it.
//
//   sweep [--dump DIR]  every power of two 64..8192, not just the two
//                       certified geometries: does the bifurcation reach the
//                       geometry a given consumer actually uses? (1a)
//                       --dump writes the raw output bits once per distinct
//                       (N, hash) so the draws can be compared. (1b)
//   diff A B            report how far apart two dumped draws actually are —
//                       max/median relative error and the per-bin picture.
//                       Decides "the backend is broken" vs "the chain is
//                       knife-edge". (1b)
//   align [N]           Accelerate only. Same transform, same process, same
//                       FFTSetup, buffers deliberately placed at varying byte
//                       offsets. If the hash moves with the offset, the draw
//                       is selected by buffer address and over-aligning the
//                       wrapper fixes it while keeping vDSP. (1c)
//
// Standalone by design (compiled directly, like branchless_parity_check.cpp)
// so one source builds against either float32 backend.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tap/dsp/fft.h"

#if defined(TAP_DSP_FFT_ACCELERATE)
#include <Accelerate/Accelerate.h>
#endif

namespace {

    // Same generator as DspTap's fft_backend_parity, so the material matches
    // the existing backend gate.
    std::vector<float> broadband(int n, unsigned seed) {
        std::vector<float> x(static_cast<size_t>(n));
        unsigned           s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = (static_cast<float>(s) / 2147483648.0f - 1.0f) * 0.1f;
        }
        return x;
    }

    std::uint64_t fnv1a(const void* p, size_t bytes) {
        const auto*   b = static_cast<const unsigned char*>(p);
        std::uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < bytes; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    const int k_sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};

    // ---- 1a: size sweep, optionally dumping one file per distinct draw ----

    int sweep(const char* dump_dir) {
        for (const int n : k_sizes) {
            const auto x = broadband(n, 0x9E3779B9u);

            tap::dsp::basic_real_fft<float> fft(static_cast<size_t>(n));
            std::vector<float>              a = x;
            std::vector<float>              b = x;
            fft.forward_inplace(a.data());
            fft.forward_inplace(b.data()); // same object, same input, twice

            const auto ha = fnv1a(a.data(), a.size() * sizeof(float));
            const auto hb = fnv1a(b.data(), b.size() * sizeof(float));
            std::printf("N=%-5d %016llx %s\n", n, static_cast<unsigned long long>(ha),
                        ha == hb ? "intra-ok" : "INTRA-VARIES");

            if (dump_dir) {
                char path[512];
                std::snprintf(path, sizeof(path), "%s/N%d-%016llx.bin", dump_dir, n,
                              static_cast<unsigned long long>(ha));
                if (std::FILE* f = std::fopen(path, "wbx")) { // x: never rewrite a draw
                    std::fwrite(a.data(), sizeof(float), a.size(), f);
                    std::fclose(f);
                }
            }
        }
        return 0;
    }

    // ---- 1b: how far apart are two draws? ----

    std::vector<float> read_bin(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            std::fprintf(stderr, "cannot open %s\n", path);
            std::exit(2);
        }
        std::fseek(f, 0, SEEK_END);
        const long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<float> v(static_cast<size_t>(bytes) / sizeof(float));
        if (std::fread(v.data(), sizeof(float), v.size(), f) != v.size()) {
            std::fprintf(stderr, "short read on %s\n", path);
            std::exit(2);
        }
        std::fclose(f);
        return v;
    }

    int diff(const char* pa, const char* pb) {
        const auto a = read_bin(pa);
        const auto b = read_bin(pb);
        if (a.size() != b.size()) {
            std::fprintf(stderr, "size mismatch: %zu vs %zu\n", a.size(), b.size());
            return 2;
        }

        // Packed real spectrum: peak magnitude normalizes the absolute view,
        // and per-bin relative error is the quantity the perturbation study
        // in #31 injected, so the two are directly comparable.
        double peak = 0.0;
        for (const float v : a) {
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }

        std::vector<double> rel;
        rel.reserve(a.size());
        double max_abs = 0.0, max_rel = 0.0;
        size_t differing = 0, max_rel_idx = 0, max_abs_idx = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            const double x = a[i], y = b[i];
            const double d = std::abs(x - y);
            if (d != 0.0) {
                ++differing;
            }
            if (d > max_abs) {
                max_abs     = d;
                max_abs_idx = i;
            }
            const double denom = std::max(std::abs(x), std::abs(y));
            const double r     = denom > 0.0 ? d / denom : 0.0;
            rel.push_back(r);
            if (r > max_rel) {
                max_rel     = r;
                max_rel_idx = i;
            }
        }
        std::vector<double> sorted = rel;
        std::sort(sorted.begin(), sorted.end());
        const auto pct = [&](double p) { return sorted[static_cast<size_t>(p * (sorted.size() - 1))]; };

        std::printf("bins            %zu\n", a.size());
        std::printf("peak |X|        %.6g\n", peak);
        std::printf("differing bins  %zu (%.1f%%)\n", differing,
                    100.0 * static_cast<double>(differing) / static_cast<double>(a.size()));
        std::printf("max abs diff    %.6g   at bin %zu (a=%.9g b=%.9g)\n", max_abs, max_abs_idx, a[max_abs_idx],
                    b[max_abs_idx]);
        std::printf("max abs / peak  %.6g\n", peak > 0.0 ? max_abs / peak : 0.0);
        std::printf("max rel diff    %.6g   at bin %zu (a=%.9g b=%.9g)\n", max_rel, max_rel_idx, a[max_rel_idx],
                    b[max_rel_idx]);
        std::printf("rel p50/p90/p99 %.6g / %.6g / %.6g\n", pct(0.50), pct(0.90), pct(0.99));
        // Two metrics, deliberately both reported, because they disagree by
        // orders of magnitude on this material and the whole argument in #31
        // turns on which one is meant:
        //
        //   max_abs/peak   what fft_backend_parity asserts (5e-6 * peak) and
        //                  what "agrees to <4e-7 relative" was measured as.
        //                  Normalizing by the GLOBAL peak makes a small bin's
        //                  error invisible.
        //   max per-bin rel  what the chain is actually sensitive to, and what
        //                  #31's perturbation study injected when it found the
        //                  failing row survives 1e-5 and breaks at 1e-4.
        //
        // A backend can sit comfortably inside the first and far outside the
        // second. Reporting one number would hide exactly that.
        const double abs_over_peak = peak > 0.0 ? max_abs / peak : 0.0;
        std::printf("\nverdict:\n");
        std::printf("  by peak-normalized abs (%.3g): %s the 4e-7 contract as measured\n", abs_over_peak,
                    abs_over_peak < 4e-7 ? "WITHIN" : "OUTSIDE");
        std::printf("  by per-bin relative    (%.3g): %s\n", max_rel,
                    max_rel >= 1e-4   ? "PAST the chain's measured breaking point (>=1e-4)"
                    : max_rel >= 1e-6 ? "between the contract and the chain's breaking point (1e-6..1e-4)"
                                      : "epsilon (<1e-6)");
        if (abs_over_peak < 4e-7 && max_rel >= 1e-4) {
            std::printf("  -> the gap is INSIDE the stated contract and OUTSIDE what the chain\n"
                        "     tolerates. The contract is stated in a metric that does not bound\n"
                        "     what this consumer needs; neither side is simply 'broken'.\n");
        }
        return 0;
    }

    // ---- 1c: is the draw selected by buffer address? ----

#if defined(TAP_DSP_FFT_ACCELERATE)

    // Replicates accelerate_real_fft_f32::forward_inplace exactly, but on
    // caller-placed buffers, so the split-complex halves and the interleaved
    // input can be moved to arbitrary byte offsets within one process.
    void capture_transform(FFTSetup setup, int n, int log2n, const std::vector<float>& x, float* in, float* rp,
                           float* ip, float* out) {
        std::memcpy(in, x.data(), x.size() * sizeof(float));
        DSPSplitComplex sp{rp, ip};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(in), 2, &sp, 1, static_cast<vDSP_Length>(n / 2));
        vDSP_fft_zrip(setup, &sp, 1, static_cast<vDSP_Length>(log2n), kFFTDirection_Forward);
        out[0] = rp[0] * 0.5f;
        out[1] = ip[0] * 0.5f;
        for (int k = 1; k < n / 2; ++k) {
            out[2 * k]     = rp[k] * 0.5f;
            out[2 * k + 1] = -ip[k] * 0.5f;
        }
    }

    std::uint64_t transform_at(FFTSetup setup, int n, int log2n, const std::vector<float>& x, float* in, float* rp,
                               float* ip) {
        std::vector<float> out(static_cast<size_t>(n));
        capture_transform(setup, n, log2n, x, in, rp, ip, out.data());
        return fnv1a(out.data(), out.size() * sizeof(float));
    }

    int align_probe(int n) {
        const int  log2n = static_cast<int>(std::lround(std::log2(static_cast<double>(n))));
        const auto x     = broadband(n, 0x9E3779B9u);

        // One shared setup: holds the "setup identity" variable fixed so any
        // hash movement is attributable to the buffers alone.
        FFTSetup shared = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
        if (!shared) {
            std::fprintf(stderr, "vDSP_create_fftsetup failed\n");
            return 2;
        }

        // Page-aligned slab, generously sized: every placement below lands
        // inside it, so the offsets are exact rather than allocator-dependent.
        const size_t slab_bytes = static_cast<size_t>(n) * sizeof(float) * 8 + 3 * 4096;
        void*        slab       = nullptr;
        if (posix_memalign(&slab, 4096, slab_bytes) != 0) {
            std::fprintf(stderr, "posix_memalign failed\n");
            return 2;
        }
        auto* base = static_cast<unsigned char*>(slab);

        // float needs 4-byte alignment, so offsets step in 4s; 4092 puts the
        // buffer one float short of a page boundary so the transform straddles
        // it, and 2048/64+ cover the cache-line and AMX-ish alignment classes.
        const size_t offsets[] = {0, 4, 8, 16, 32, 64, 128, 256, 2048, 4092};

        std::printf("== align probe: N=%d, one process, one FFTSetup ==\n", n);
        std::uint64_t first = 0;
        bool          moved = false;
        for (size_t oi = 0; oi < sizeof(offsets) / sizeof(offsets[0]); ++oi) {
            const size_t off = offsets[oi];
            auto*        in  = reinterpret_cast<float*>(base + off);
            auto*        rp  = reinterpret_cast<float*>(base + off + static_cast<size_t>(n) * sizeof(float) + 4096);
            auto*        ip  = rp + n / 2;
            const auto   h   = transform_at(shared, n, log2n, x, in, rp, ip);
            if (oi == 0) {
                first = h;
            }
            else if (h != first) {
                moved = true;
            }
            std::printf("off=%-6zu rp=%p (mod64=%2zu mod4096=%4zu)  %016llx%s\n", off, static_cast<void*>(rp),
                        reinterpret_cast<std::uintptr_t>(rp) % 64, reinterpret_cast<std::uintptr_t>(rp) % 4096,
                        static_cast<unsigned long long>(h), h == first ? "" : "  <-- DIFFERS");
        }

        // Second axis: fresh setup per iteration, buffers held fixed. Separates
        // "the setup object varies" from "the buffer address varies".
        std::printf("-- fresh FFTSetup each time, buffers fixed at offset 0 --\n");
        auto*         in  = reinterpret_cast<float*>(base);
        auto*         rp  = reinterpret_cast<float*>(base + static_cast<size_t>(n) * sizeof(float) + 4096);
        auto*         ip  = rp + n / 2;
        std::uint64_t sf  = 0;
        bool          smv = false;
        for (int i = 0; i < 8; ++i) {
            FFTSetup   s = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
            const auto h = transform_at(s, n, log2n, x, in, rp, ip);
            vDSP_destroy_fftsetup(s);
            if (i == 0) {
                sf = h;
            }
            else if (h != sf) {
                smv = true;
            }
            std::printf("setup#%d  %016llx%s\n", i, static_cast<unsigned long long>(h), h == sf ? "" : "  <-- DIFFERS");
        }

        std::printf("\nverdict: buffer offset %s the draw; fresh setup %s the draw\n",
                    moved ? "MOVES" : "does not move", smv ? "MOVES" : "does not move");
        if (moved) {
            std::printf("  -> address-selected: over-aligning m_rp/m_ip in the wrapper should fix it,\n"
                        "     keeping vDSP and its speedup.\n");
        }
        else {
            std::printf("  -> not address-selected within a process. If the cross-process sweep still\n"
                        "     splits, the draw is fixed at process start (dispatch/lazy init), and\n"
                        "     over-aligning will not help.\n");
        }

        std::free(slab);
        vDSP_destroy_fftsetup(shared);
        return 0;
    }

#endif // TAP_DSP_FFT_ACCELERATE

    // ---- which kernel is actually CORRECT? ----
    //
    // The A/B showed the compliance rows pass on the non-64-aligned kernel and
    // fail on the aligned one. That does NOT by itself say the aligned kernel
    // is wrong — it may just be differently rounded, in which case picking the
    // other one is choosing a lucky coin rather than fixing anything.
    //
    // Settle it against a double-precision transform of the same input (error
    // ~1e-15 at these sizes, so a legitimate stand-in for truth against
    // float32's ~1e-7). Two materials, because the failing rows are tonal and
    // the existing parity gate is broadband.

    constexpr double k_pi = 3.14159265358979323846;

    // A sinusoid at bin index k (fractional k = deliberately off-bin, which
    // spreads leakage into every bin and removes the near-empty ones).
    std::vector<float> tone_at_bin(int n, double k, double amp = 0.5) {
        std::vector<float> x(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            x[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(2.0 * k_pi * k * i / static_cast<double>(n)));
        }
        return x;
    }

    // Three widely separated on-bin partials: still a sparse spectrum, but not
    // the single-tone special case, so a finding cannot be blamed on it.
    std::vector<float> sparse_three_tone(int n) {
        std::vector<float> x(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double t            = static_cast<double>(i) / static_cast<double>(n);
            x[static_cast<size_t>(i)] = static_cast<float>(0.30 * std::sin(2.0 * k_pi * (n / 16.0) * t)
                                                           + 0.15 * std::sin(2.0 * k_pi * (n / 5.0) * t)
                                                           + 0.05 * std::sin(2.0 * k_pi * (n / 3.0) * t));
        }
        return x;
    }

    struct material {
        const char*        name;
        std::vector<float> x;
    };

    // Sparse (mostly-empty) spectra are the class where the defect should
    // show; broadband and the off-bin tone are the controls where every bin
    // carries energy and the two kernels should look the same.
    std::vector<material> materials(int n) {
        std::vector<material> m;
        m.push_back({"broadband", broadband(n, 0x9E3779B9u)});
        m.push_back({"tone n/16", tone_at_bin(n, n / 16.0)});
        m.push_back({"tone n/8", tone_at_bin(n, n / 8.0)});
        m.push_back({"tone offbin", tone_at_bin(n, n / 16.0 + 0.37)});
        m.push_back({"3-tone", sparse_three_tone(n)});
        return m;
    }

    void report_vs_double(const char* label, const char* material, const std::vector<float>& got,
                          const std::vector<double>& ref) {
        double peak = 0.0;
        for (const double v : ref) {
            peak = std::max(peak, std::abs(v));
        }
        std::vector<double> rel;
        double              max_rel = 0.0, max_abs = 0.0;
        for (size_t i = 0; i < ref.size(); ++i) {
            const double d = std::abs(static_cast<double>(got[i]) - ref[i]);
            max_abs        = std::max(max_abs, d);
            const double r = std::abs(ref[i]) > 0.0 ? d / std::abs(ref[i]) : 0.0;
            rel.push_back(r);
            max_rel = std::max(max_rel, r);
        }
        std::sort(rel.begin(), rel.end());
        std::printf("  %-22s %-10s max_rel=%-11.4g p50_rel=%-11.4g abs/peak=%.4g\n", label, material, max_rel,
                    rel[rel.size() / 2], peak > 0.0 ? max_abs / peak : 0.0);
    }

    int truth(int n) {
        std::printf("== accuracy vs a double-precision reference, N=%d ==\n", n);
        for (const auto& m : materials(n)) {
            std::vector<double>              ref(m.x.begin(), m.x.end());
            tap::dsp::basic_real_fft<double> dfft(static_cast<size_t>(n));
            dfft.forward_inplace(ref.data());

            // THE WRAPPER as actually compiled — this is the path MuTap uses,
            // and measuring it removes the probe's own buffer placement as an
            // explanation. Build once stock and once with the alignment
            // defeated to see both kernels through it.
            {
                std::vector<float>              got = m.x;
                tap::dsp::basic_real_fft<float> ffft(static_cast<size_t>(n));
                ffft.forward_inplace(got.data());
                report_vs_double("wrapper (as built)", m.name, got, ref);
            }

#if defined(TAP_DSP_FFT_ACCELERATE)
            // Raw vDSP at both alignments, for cross-checking the wrapper.
            const int log2n = static_cast<int>(std::lround(std::log2(static_cast<double>(n))));
            FFTSetup  setup = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
            if (!setup) {
                std::fprintf(stderr, "vDSP_create_fftsetup failed\n");
                return 2;
            }
            void* slab = nullptr;
            if (posix_memalign(&slab, 4096, static_cast<size_t>(n) * sizeof(float) * 8 + 3 * 4096) != 0) {
                return 2;
            }
            auto* base = static_cast<unsigned char*>(slab);
            for (const size_t off : {size_t{0}, size_t{4}}) {
                auto* in = reinterpret_cast<float*>(base + off);
                auto* rp = reinterpret_cast<float*>(base + off + static_cast<size_t>(n) * sizeof(float) + 4096);
                auto* ip = rp + n / 2;
                std::vector<float> out(static_cast<size_t>(n));
                capture_transform(setup, n, log2n, m.x, in, rp, ip, out.data());
                report_vs_double(off == 0 ? "raw vdsp 64-aligned" : "raw vdsp not-aligned", m.name, out, ref);
            }
            std::free(slab);
            vDSP_destroy_fftsetup(setup);
#endif
        }
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "sweep";

    if (mode == "sweep") {
        const char* dump = nullptr;
        for (int i = 2; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--dump") {
                dump = argv[i + 1];
            }
        }
        return sweep(dump);
    }
    if (mode == "diff") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: %s diff A.bin B.bin\n", argv[0]);
            return 2;
        }
        return diff(argv[2], argv[3]);
    }
    if (mode == "truth") {
        return truth(argc > 2 ? std::atoi(argv[2]) : 2048);
    }
    if (mode == "align") {
#if defined(TAP_DSP_FFT_ACCELERATE)
        return align_probe(argc > 2 ? std::atoi(argv[2]) : 2048);
#else
        std::fprintf(stderr, "align mode requires the Accelerate backend\n");
        return 2;
#endif
    }

    std::fprintf(stderr, "usage: %s [sweep [--dump DIR] | diff A B | align [N]]\n", argv[0]);
    return 2;
}
