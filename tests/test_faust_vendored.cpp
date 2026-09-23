// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The vendored FAUST material (third_party/faust/): the anti-howl PoC's
// reference reverb (FAUST's re.dattorro_rev, as shipped and with the paper's
// delay lengths) and its baseline suppressor (faust-icc, corrected in
// icc_48k.lib), through the shim's real-time wrapper. Host-only: not
// compiled for the cross / bare-metal targets (tests/CMakeLists.txt), and no
// emulated selection names the FaustVendored suite.
//
// Measured (macOS x86_64, AppleClang 17, Release, 48 kHz); every threshold
// below is one of these with margin:
//
//   Dattorro as shipped, paper defaults: f32 vs f64 max |diff| 2.481e-08,
//     difference energy -139.42 dB re the IR (phase 0: 2.48e-08, -137 to
//     -140 dB); T30 L 1.184593 s, R 1.173498 s (phase 0: 1.1846 s, L).
//   Paper lengths (dattorro_paper): T30 L 2.118617 s, R 2.089709 s over 4 s
//     (as shipped over 4 s: L 1.184580, R 1.173501), f32 identical to 6
//     digits; the first recirculation through 3163 / 3720 lands at exactly
//     0.25 where the derivation puts it, and nothing lands where upstream's
//     repeated 2656 / 1800 would put it.
//   Corrected faust-icc shifter, notches bypassed, guard 20 Hz, 0 Hz shift:
//     -18.8857 / -6.0318 / -0.8318 / -0.0434 / +0.0000 dB at 200 / 1000 /
//     3000 / 6000 / 12000 Hz; image at +4 Hz -0.6928 / -2.0785 / -6.9306 dB
//     at 100 / 300 / 1000 Hz. +4 Hz on 1000 Hz (guard 100): 1003.999753 Hz,
//     image at 996 Hz -7.3 dB.
//   Howl detector, 1 kHz tone 3 dB over pink noise: confidence > 0.5 after
//     0.1528 / 0.1562 / 0.1538 s (seeds 1-3); pink noise alone: confidence 0,
//     prominence at most 7.32 dB (threshold 15).
//
// Every signal here is deterministic (fixed seeds), so each number repeats
// exactly on a given platform; the margins absorb libm / FMA differences
// between platforms, not noise.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "faust_generated.h"

namespace {

    using mutap_faust::faust_block;

    constexpr int    k_fs    = 48000;
    constexpr size_t k_block = 64;

    using channels = std::vector<std::vector<double>>;

    /// Run a mono-in block over `in` in k_block pieces; every output channel
    /// comes back as double.
    template <class Dsp, class Sample>
    channels run(faust_block<Dsp, Sample>& b, const std::vector<double>& in) {
        const auto                       outs = static_cast<size_t>(b.num_outputs());
        channels                         y(outs, std::vector<double>(in.size()));
        std::vector<Sample>              x(k_block);
        std::vector<std::vector<Sample>> yb(outs, std::vector<Sample>(k_block));
        std::vector<Sample*>             yp(outs);
        for (size_t c = 0; c < outs; ++c) {
            yp[c] = yb[c].data();
        }
        for (size_t i = 0; i < in.size(); i += k_block) {
            const size_t n = std::min(k_block, in.size() - i);
            for (size_t k = 0; k < n; ++k) {
                x[k] = static_cast<Sample>(in[i + k]);
            }
            b.process_mono(x.data(), yp.data(), static_cast<int>(n));
            for (size_t c = 0; c < outs; ++c) {
                for (size_t k = 0; k < n; ++k) {
                    y[c][i + k] = static_cast<double>(yb[c][k]);
                }
            }
        }
        return y;
    }

    std::vector<double> impulse(size_t n) {
        std::vector<double> x(n, 0.0);
        x[0] = 1.0;
        return x;
    }

    std::vector<double> sine(double f, double amplitude, size_t n) {
        std::vector<double> x(n);
        for (size_t i = 0; i < n; ++i) {
            x[i] = amplitude * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / k_fs);
        }
        return x;
    }

    double db10(double x) {
        return 10.0 * std::log10(x);
    }
    double db20(double x) {
        return 20.0 * std::log10(x);
    }

    double energy(const std::vector<double>& x) {
        double e = 0.0;
        for (const double v : x) {
            e += v * v;
        }
        return e;
    }

    double rms(const std::vector<double>& x, size_t from, size_t to) {
        double e = 0.0;
        for (size_t i = from; i < to; ++i) {
            e += x[i] * x[i];
        }
        return std::sqrt(e / static_cast<double>(to - from));
    }

    /// T30 by Schroeder backward integration: a least-squares line through
    /// the energy decay curve from its first -5 dB to its first -35 dB sample.
    double t30_seconds(const std::vector<double>& h) {
        std::vector<double> edc(h.size());
        double              acc = 0.0;
        for (size_t i = h.size(); i-- > 0;) {
            acc += h[i] * h[i];
            edc[i] = acc;
        }
        size_t lo = 0;
        while (lo < h.size() && db10(edc[lo] / edc[0]) > -5.0) {
            ++lo;
        }
        size_t hi = lo;
        while (hi < h.size() && db10(edc[hi] / edc[0]) > -35.0) {
            ++hi;
        }
        double sx  = 0.0;
        double sy  = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (size_t i = lo; i <= hi && i < h.size(); ++i) {
            const double t = static_cast<double>(i) / k_fs;
            const double y = db10(edc[i] / edc[0]);
            sx += t;
            sy += y;
            sxx += t * t;
            sxy += t * y;
        }
        const auto   m     = static_cast<double>(hi - lo + 1);
        const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx); // dB per second
        return -60.0 / slope;
    }

    /// Complex DFT of x[from, from + len) at `f` Hz, phase referenced to
    /// sample 0 (so a stationary tone at exactly f has the same phase in any
    /// window). The amplitude of a real sinusoid is 2|X|/len.
    std::complex<double> dft_at(const std::vector<double>& x, double f, size_t from, size_t len) {
        std::complex<double> acc{0.0, 0.0};
        const double         w = 2.0 * std::numbers::pi * f / k_fs;
        for (size_t i = from; i < from + len; ++i) {
            acc += x[i] * std::polar(1.0, -w * static_cast<double>(i));
        }
        return acc;
    }

    double tone_amplitude(const std::vector<double>& x, double f, size_t from, size_t len) {
        return 2.0 * std::abs(dft_at(x, f, from, len)) / static_cast<double>(len);
    }

    /// Pink noise at `rms_target` (Paul Kellet's refined filter over
    /// Gaussian white noise, seeded).
    std::vector<double> pink(size_t n, unsigned seed, double rms_target) {
        std::mt19937                     rng(seed);
        std::normal_distribution<double> white(0.0, 1.0);
        std::vector<double>              x(n);
        double                           b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0, b4 = 0.0, b5 = 0.0, b6 = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double w = white(rng);
            b0             = 0.99886 * b0 + w * 0.0555179;
            b1             = 0.99332 * b1 + w * 0.0750759;
            b2             = 0.96900 * b2 + w * 0.1538520;
            b3             = 0.86650 * b3 + w * 0.3104856;
            b4             = 0.55000 * b4 + w * 0.5329522;
            b5             = -0.7616 * b5 - w * 0.0168980;
            x[i]           = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6             = w * 0.115926;
        }
        const double g = rms_target / rms(x, 0, n);
        for (auto& v : x) {
            v *= g;
        }
        return x;
    }

    /// A delay length of the paper (samples at 29761 Hz) at k_fs, rounded as
    /// dattorro_paper.dsp rounds it.
    size_t paper_len(double n) {
        return static_cast<size_t>(std::lround(n * k_fs / 29761.0));
    }

    // ------------------------------------------------------------------ shim

    TEST(FaustVendored, ShimIndexesControlsAndPorts) {
        faust_block<mutap_faust::dattorro_f32, float> rev(k_fs);
        EXPECT_EQ(rev.num_inputs(), 1);
        EXPECT_EQ(rev.num_outputs(), 2);
        for (const char* label : {"bw", "i_diff1", "i_diff2", "decay", "d_diff1", "d_diff2", "damping"}) {
            EXPECT_TRUE(rev.set(label, 0.5f)) << label;
        }
        EXPECT_FALSE(rev.set("no_such_control", 0.5f));

        faust_block<mutap_faust::icc_suppressor_f64, double> sup(k_fs);
        EXPECT_EQ(sup.num_inputs(), 1);
        EXPECT_EQ(sup.num_outputs(), 1);
        for (const char* label : {"shift", "guard", "prom_thresh", "hold_time", "max_depth", "notch_bypass"}) {
            EXPECT_TRUE(sup.set(label, 1.0)) << label;
        }

        faust_block<mutap_faust::icc_howl_detect_f32, float> det(k_fs);
        EXPECT_EQ(det.num_inputs(), 1);
        EXPECT_EQ(det.num_outputs(), 3);

        std::printf("sizeof: dattorro_f32 %zu, dattorro_f64 %zu, dattorro_paper_f32 %zu, dattorro_paper_f64 %zu, "
                    "icc_suppressor_f32 %zu, icc_howl_detect_f32 %zu bytes\n",
                    sizeof(mutap_faust::dattorro_f32), sizeof(mutap_faust::dattorro_f64),
                    sizeof(mutap_faust::dattorro_paper_f32), sizeof(mutap_faust::dattorro_paper_f64),
                    sizeof(mutap_faust::icc_suppressor_f32), sizeof(mutap_faust::icc_howl_detect_f32));
    }

    // -------------------------------------------------------------- Dattorro

    // Phase 0's IR length at the paper defaults (its double IR, cut one past
    // the last sample above peak - 90 dB), so these reproduce its numbers.
    constexpr size_t k_phase0_ir = 73992;

    TEST(FaustVendored, DattorroFloatTracksDouble) {
        faust_block<mutap_faust::dattorro_f32, float>  f32(k_fs);
        faust_block<mutap_faust::dattorro_f64, double> f64(k_fs);
        const auto                                     x = impulse(k_phase0_ir);
        const channels                                 a = run(f32, x);
        const channels                                 b = run(f64, x);
        for (size_t c = 0; c < 2; ++c) {
            double max_abs = 0.0;
            double diff_e  = 0.0;
            for (size_t i = 0; i < x.size(); ++i) {
                const double d = a[c][i] - b[c][i];
                max_abs        = std::max(max_abs, std::abs(d));
                diff_e += d * d;
            }
            const double rel_db = db10(diff_e / energy(b[c]));
            std::printf("dattorro f32 vs f64, %s: max |diff| %.4g, diff energy %.2f dB re IR\n", c == 0 ? "L" : "R",
                        max_abs, rel_db);
            EXPECT_LT(max_abs, 1e-7) << c;  // measured 2.481e-08 (L and R; phase 0: 2.48e-08)
            EXPECT_LT(rel_db, -130.0) << c; // measured -139.42 (L and R; phase 0: -137 to -140)
        }
    }

    TEST(FaustVendored, DattorroAsShippedT30) {
        faust_block<mutap_faust::dattorro_f64, double> rev(k_fs);
        const channels                                 y  = run(rev, impulse(k_phase0_ir));
        const double                                   tl = t30_seconds(y[0]);
        const double                                   tr = t30_seconds(y[1]);
        std::printf("dattorro as shipped, T30 over %zu samples: L %.6f s, R %.6f s\n", k_phase0_ir, tl, tr);
        EXPECT_NEAR(tl, 1.1846, 0.01); // measured 1.184593 (phase 0, noise-compensated: 1.1846)
        EXPECT_NEAR(tr, 1.1735, 0.01); // measured 1.173498
    }

    TEST(FaustVendored, DattorroPaperLengthsDecayLonger) {
        // Side by side on 4 s IRs.
        const size_t                                         n = 4 * static_cast<size_t>(k_fs);
        faust_block<mutap_faust::dattorro_f64, double>       shipped(k_fs);
        faust_block<mutap_faust::dattorro_paper_f64, double> paper(k_fs);
        faust_block<mutap_faust::dattorro_paper_f32, float>  paper32(k_fs);
        const channels                                       ys   = run(shipped, impulse(n));
        const channels                                       yp   = run(paper, impulse(n));
        const channels                                       yp32 = run(paper32, impulse(n));
        const double                                         sl   = t30_seconds(ys[0]);
        const double                                         pl   = t30_seconds(yp[0]);
        const double                                         pr   = t30_seconds(yp[1]);
        std::printf("T30 over 4 s: as shipped L %.6f R %.6f s; paper lengths L %.6f R %.6f s (f32 L %.6f R %.6f)\n", sl,
                    t30_seconds(ys[1]), pl, pr, t30_seconds(yp32[0]), t30_seconds(yp32[1]));
        EXPECT_NEAR(pl, 2.1186, 0.02);                // measured 2.118617 (f32 the same to 6 digits)
        EXPECT_NEAR(pr, 2.0897, 0.02);                // measured 2.089709
        EXPECT_NEAR(t30_seconds(yp32[0]), pl, 0.001); // measured equal to 6 digits
        EXPECT_GT(pl / sl, 1.7);                      // measured 1.788 (as shipped L 1.184580 over 4 s)
    }

    TEST(FaustVendored, DattorroPaperTankUsesThePapersSecondDelays) {
        // With every diffusion coefficient 0, damping 0 and bw 1, each allpass
        // is a pure delay of its length + 1 and each tank block a pure delay
        // with gain decay^2, so the IR is a train of impulses at predictable
        // samples. L = s + block1(R, one sample late), R = s + block0(L, one
        // sample late), s = the diffused input. The first recirculation lands
        // in L at D0 + 1 + B1 and in R at D0 + 1 + B0, where B1 ends in the
        // second tank delay: 3163 in the paper, 2656 (repeated) as shipped;
        // B0 ends in 3720 (paper) or 1800 (as shipped).
        const auto zero_diffusion = [](auto& rev) {
            for (const char* label : {"i_diff1", "i_diff2", "d_diff1", "d_diff2", "damping"}) {
                rev.set(label, 0.0);
            }
            rev.set("bw", 1.0);
            rev.set("decay", 0.5);
        };
        const size_t n = 2 * static_cast<size_t>(k_fs);

        faust_block<mutap_faust::dattorro_paper_f64, double> paper(k_fs);
        zero_diffusion(paper);
        const channels yp = run(paper, impulse(n));
        const size_t   d0 = (paper_len(142) + 1) + (paper_len(107) + 1) + (paper_len(379) + 1) + (paper_len(277) + 1);
        const size_t   b0 = (paper_len(672) + 1) + paper_len(4453) + (paper_len(1800) + 1) + paper_len(3720);
        const size_t   b1 = (paper_len(908) + 1) + paper_len(4217) + (paper_len(2656) + 1) + paper_len(3163);
        // the same blocks with upstream's repeated delay in place of 3720 / 3163
        const size_t b0_bug = b0 - paper_len(3720) + paper_len(1800);
        const size_t b1_bug = b1 - paper_len(3163) + paper_len(2656);
        std::printf("paper lengths @48k: D0 %zu, B0 %zu, B1 %zu; L[%zu] = %.9f, R[%zu] = %.9f; "
                    "at the as-shipped lengths L[%zu] = %.3g, R[%zu] = %.3g\n",
                    d0, b0, b1, d0 + 1 + b1, yp[0][d0 + 1 + b1], d0 + 1 + b0, yp[1][d0 + 1 + b0], d0 + 1 + b1_bug,
                    yp[0][d0 + 1 + b1_bug], d0 + 1 + b0_bug, yp[1][d0 + 1 + b0_bug]);
        EXPECT_NEAR(yp[0][d0], 1.0, 1e-12);           // the diffused input, measured 1
        EXPECT_NEAR(yp[0][d0 + 1 + b1], 0.25, 1e-12); // measured 0.250000000 at L[19117]
        EXPECT_NEAR(yp[1][d0 + 1 + b0], 0.25, 1e-12); // measured 0.250000000 at R[18636]
        EXPECT_EQ(yp[0][d0 + 1 + b1_bug], 0.0);       // measured 0 at L[18300]
        EXPECT_EQ(yp[1][d0 + 1 + b0_bug], 0.0);       // measured 0 at R[15539]

        // The derivation, checked on the as-shipped plate (unscaled samples).
        faust_block<mutap_faust::dattorro_f64, double> shipped(k_fs);
        zero_diffusion(shipped);
        const channels ys  = run(shipped, impulse(n));
        const size_t   sd0 = 143 + 108 + 380 + 278;
        const size_t   sb0 = 673 + 4453 + 1801 + 1800;
        const size_t   sb1 = 909 + 4217 + 2657 + 2656;
        std::printf("as shipped: D0 %zu, B0 %zu, B1 %zu; L[%zu] = %.9f, R[%zu] = %.9f\n", sd0, sb0, sb1, sd0 + 1 + sb1,
                    ys[0][sd0 + 1 + sb1], sd0 + 1 + sb0, ys[1][sd0 + 1 + sb0]);
        EXPECT_NEAR(ys[0][sd0 + 1 + sb1], 0.25, 1e-12); // measured 0.250000000 at L[11349]
        EXPECT_NEAR(ys[1][sd0 + 1 + sb0], 0.25, 1e-12); // measured 0.250000000 at R[9637]
    }

    // ------------------------------------------------------------- faust-icc

    /// The suppressor with the notch bank bypassed: the shifter alone.
    template <class Dsp, class Sample>
    void shifter_only(faust_block<Dsp, Sample>& s, double guard_hz, double shift_hz) {
        s.set("notch_bypass", Sample(1));
        s.set("guard", static_cast<Sample>(guard_hz));
        s.set("shift", static_cast<Sample>(shift_hz));
    }

    TEST(FaustVendored, SuppressorPassbandCarriesTheFactorTwo) {
        // At 0 Hz shift the output is the wanted positive-frequency path plus
        // whatever of the negative-frequency image fi.pospass(6) lets through,
        // summed in phase or against it. pospass is an order-6 Butterworth
        // half-band lowpass modulated to SR/4, so its transition band is
        // thousands of Hz wide whatever the guard: the image is rejected only
        // far above the guard, and the voice band is NOT flat. Where the
        // image is gone (SR/4 = 12 kHz; nearly at 6 kHz) the gain is 0 dB,
        // which is the x2 (without it every row reads 6.02 dB lower). The
        // lower rows match an independent scipy model of the same filter
        // (x2, (cos, sin)) to 1e-4 dB: -18.8856 / -6.0318 / -0.8318 /
        // -0.0434 / +0.0000.
        struct row {
            double f;
            double gain_db; // measured f32 / f64
        };
        const row    rows[] = {{200.0, -18.8857},  // f64 -18.8856
                               {1000.0, -6.0318},  // f64 -6.0318
                               {3000.0, -0.8318},  // f64 -0.8318
                               {6000.0, -0.0434},  // f64 -0.0434
                               {12000.0, 0.0000}}; // f32 +0.0000, f64 -0.0000
        const size_t settle = static_cast<size_t>(k_fs);
        const size_t window = 2 * static_cast<size_t>(k_fs);
        for (const row& r : rows) {
            faust_block<mutap_faust::icc_suppressor_f32, float>  s32(k_fs);
            faust_block<mutap_faust::icc_suppressor_f64, double> s64(k_fs);
            shifter_only(s32, 20.0, 0.0);
            shifter_only(s64, 20.0, 0.0);
            const auto   x   = sine(r.f, 0.5, settle + window);
            const double in  = rms(x, settle, settle + window);
            const double g32 = db20(rms(run(s32, x)[0], settle, settle + window) / in);
            const double g64 = db20(rms(run(s64, x)[0], settle, settle + window) / in);
            std::printf("passband %6.0f Hz (guard 20, shift 0): f32 %+.4f dB, f64 %+.4f dB\n", r.f, g32, g64);
            EXPECT_NEAR(g32, r.gain_db, 0.05) << r.f;
            EXPECT_NEAR(g64, r.gain_db, 0.05) << r.f;
        }
    }

    TEST(FaustVendored, SuppressorShiftsUp) {
        faust_block<mutap_faust::icc_suppressor_f32, float> s(k_fs);
        shifter_only(s, 100.0, 4.0);
        const size_t   settle = static_cast<size_t>(k_fs);
        const size_t   half   = 2 * static_cast<size_t>(k_fs);
        const channels y      = run(s, sine(1000.0, 0.5, settle + 2 * half));
        const double   up     = tone_amplitude(y[0], 1004.0, settle, 2 * half);
        const double   down   = tone_amplitude(y[0], 996.0, settle, 2 * half);
        // Fine frequency from the phase advance between two 2 s windows of the
        // DFT at 1004 Hz (unambiguous within +-0.25 Hz of it).
        const std::complex<double> p1 = dft_at(y[0], 1004.0, settle, half);
        const std::complex<double> p2 = dft_at(y[0], 1004.0, settle + half, half);
        const double f_est = 1004.0 + std::arg(p2 / p1) / (2.0 * std::numbers::pi * static_cast<double>(half) / k_fs);
        std::printf("1000 Hz, +4 Hz (guard 100): amplitude at 1004 %.6f, at 996 %.3g (%.1f dB); frequency %.6f Hz\n",
                    up, down, db20(down / up), f_est);
        EXPECT_LT(db20(down / up), -6.0);        // measured -7.3 (the image; upstream's order put the tone at 996)
        EXPECT_NEAR(f_est, 1004.0, 0.01);        // measured 1003.999753
        EXPECT_NEAR(db20(up / 0.5), -0.94, 0.1); // measured -0.94 dB (0.448631): the wanted sideband at 1 kHz
    }

    TEST(FaustVendored, SuppressorImageRejection) {
        // Image (f - 4 Hz) re the wanted sideband (f + 4 Hz), guard 20 Hz.
        // Poor, for the reason in SuppressorPassbandCarriesTheFactorTwo; the
        // scipy model gives -0.69 / -2.08 / -6.93 dB.
        struct row {
            double f;
            double image_db; // measured f32 (f64 in the comment)
        };
        const row    rows[] = {{100.0, -0.6928},   // f64 -0.6929
                               {300.0, -2.0785},   // f64 -2.0787
                               {1000.0, -6.9306}}; // f64 -6.9311
        const double shift  = 4.0;
        const size_t settle = static_cast<size_t>(k_fs);
        const size_t window = 4 * static_cast<size_t>(k_fs); // whole periods of f +- 4 Hz for integer f
        for (const row& r : rows) {
            faust_block<mutap_faust::icc_suppressor_f32, float>  s32(k_fs);
            faust_block<mutap_faust::icc_suppressor_f64, double> s64(k_fs);
            shifter_only(s32, 20.0, shift);
            shifter_only(s64, 20.0, shift);
            const auto     x   = sine(r.f, 0.5, settle + window);
            const channels y32 = run(s32, x);
            const channels y64 = run(s64, x);
            const double   r32 = db20(tone_amplitude(y32[0], r.f - shift, settle, window)
                                      / tone_amplitude(y32[0], r.f + shift, settle, window));
            const double   r64 = db20(tone_amplitude(y64[0], r.f - shift, settle, window)
                                      / tone_amplitude(y64[0], r.f + shift, settle, window));
            std::printf("image rejection %5.0f Hz (guard 20, +4 Hz): f32 %.4f dB, f64 %.4f dB\n", r.f, r32, r64);
            EXPECT_NEAR(r32, r.image_db, 0.1) << r.f;
            EXPECT_NEAR(r64, r.image_db, 0.1) << r.f;
        }
    }

    TEST(FaustVendored, HowlDetectorFindsAStationaryTone) {
        // Pink noise at -26 dBFS rms; from `onset` a 1 kHz tone 3 dB above
        // the noise's broadband rms. Three seeds, all asserted.
        const double noise_rms = 0.05;
        const double tone_rms  = noise_rms * std::pow(10.0, 3.0 / 20.0);
        const size_t onset     = 2 * static_cast<size_t>(k_fs);
        const size_t n         = onset + 2 * static_cast<size_t>(k_fs);
        const size_t warmup    = static_cast<size_t>(k_fs) / 2;
        // the analysis band nearest 1 kHz: logBandFreq(16, 32, 150, 6000)
        const double band = 150.0 * std::pow(6000.0 / 150.0, 16.0 / 31.0);
        for (const unsigned seed : {1U, 2U, 3U}) {
            auto x = pink(n, seed, noise_rms);
            for (size_t i = onset; i < n; ++i) {
                x[i] += tone_rms * std::numbers::sqrt2
                        * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i - onset) / k_fs);
            }
            faust_block<mutap_faust::icc_howl_detect_f32, float> det(k_fs);
            const channels                                       y         = run(det, x);
            double                                               noise_max = 0.0;
            double                                               prom_max  = -1e9;
            for (size_t i = warmup; i < onset; ++i) {
                noise_max = std::max(noise_max, y[0][i]);
                prom_max  = std::max(prom_max, y[2][i]);
            }
            size_t rise = onset;
            while (rise < n && y[0][rise] <= 0.5) {
                ++rise;
            }
            const double rise_s = static_cast<double>(rise - onset) / k_fs;
            std::printf("howl detect seed %u: noise-only max confidence %.3g, max prominence %.2f dB; tone +3 dB: "
                        "confidence > 0.5 after %.4f s, at %.2f Hz, prominence %.2f dB (final)\n",
                        seed, noise_max, prom_max, rise_s, y[1][n - 1], y[2][n - 1]);
            EXPECT_LT(noise_max, 0.01) << seed;           // measured 0 (seeds 1-3)
            EXPECT_LT(prom_max, 11.0) << seed;            // measured 6.98 / 5.42 / 7.32 dB (threshold 15)
            EXPECT_LT(rise_s, 0.25) << seed;              // measured 0.1528 / 0.1562 / 0.1538 s
            EXPECT_NEAR(y[1][n - 1], band, 0.01) << seed; // measured 1006.84 Hz
            EXPECT_GT(y[2][n - 1], 15.5) << seed;         // measured 16.85 / 17.11 / 16.62 dB

            // pink noise alone for 10 s
            faust_block<mutap_faust::icc_howl_detect_f32, float> det2(k_fs);
            const channels y2        = run(det2, pink(10 * static_cast<size_t>(k_fs), seed + 100U, noise_rms));
            double         long_max  = 0.0;
            double         long_prom = -1e9;
            for (size_t i = warmup; i < y2[0].size(); ++i) {
                long_max  = std::max(long_max, y2[0][i]);
                long_prom = std::max(long_prom, y2[2][i]);
            }
            std::printf("howl detect seed %u: 10 s pink noise, max confidence %.3g, max prominence %.2f dB\n",
                        seed + 100U, long_max, long_prom);
            EXPECT_LT(long_max, 0.01) << seed + 100U;  // measured 0 (seeds 101-103)
            EXPECT_LT(long_prom, 11.0) << seed + 100U; // measured 6.66 / 7.10 / 6.51 dB
        }
    }

} // namespace
