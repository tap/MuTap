// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The measurement driver: it owns the scenarios and the metrics, and
// treats every subject as a black box (aec_backend). Because the echo
// path, the signals, and the metric code are identical across subjects,
// the numbers are directly comparable — this file IS "our tests", made
// algorithm-agnostic. (The complementary direction, our chain scored by
// WebRTC's own AECMOS on the neutral AEC-Challenge corpus, lives in the
// Python harness under tools/compare/.)
//
// Ground truth the driver keeps and a deployed canceller cannot:
//   d(n) = F(q) x(n)          the echo alone (near-end excluded)
//   v(n)                      the near-end alone
//   y(n) = d(n) + v(n)        the microphone the backend sees
//   e(n)                      the backend's cleaned output
// Knowing d and v separately is what lets us report echo suppression and
// near-end fidelity SEPARATELY, and keep the echo metric valid under
// double-talk (mutap_test::run_aec's suppression_db, the same definition).
//
// Latency: each backend declares latency_ms(); the driver shifts e back
// by that many samples before the sample-aligned metrics so a canceller
// is not charged for its designed delay.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "aec_backend.h"

namespace mutap_compare {

    using std::size_t;

    // ------------------------------------------------------------------
    // Signals — deterministic, seeded, rate-parameterized.
    // ------------------------------------------------------------------

    // A speech-like excitation: white noise through a 2-pole resonant
    // low-pass (formant-ish) with slow amplitude modulation (syllabic
    // envelope). Not speech, but colored and non-stationary — a harder,
    // more honest convergence driver than white noise, and reproducible
    // at any rate. seed varies the realization (far vs near independent).
    inline std::vector<float> speechlike(size_t n, double fs, uint32_t seed) {
        std::mt19937                    rng(seed);
        std::normal_distribution<float> g(0.0f, 1.0f);
        // 2nd-order resonator near 300 Hz, r = 0.95.
        const double f0 = 320.0, r = 0.95;
        const double a1 = -2.0 * r * std::cos(2.0 * M_PI * f0 / fs);
        const double a2 = r * r;
        std::vector<float> x(n);
        double             y1 = 0.0, y2 = 0.0;
        const double       env_hz = 3.3; // syllabic rate
        for (size_t i = 0; i < n; ++i) {
            const double in  = g(rng);
            const double y0  = in - a1 * y1 - a2 * y2;
            y2               = y1;
            y1               = y0;
            // syllabic envelope in [0.15, 1], plus occasional pauses.
            const double t   = static_cast<double>(i) / fs;
            double       env = 0.575 + 0.425 * std::sin(2.0 * M_PI * env_hz * t);
            if (std::sin(2.0 * M_PI * 0.31 * t) > 0.7) env *= 0.15; // pauses
            x[i] = static_cast<float>(env * y0);
        }
        // Normalize to a realistic active-speech level: RMS -24 dBFS with a
        // ~6 dB crest headroom, so no subject's fixed-point (int16) path
        // clips — clipping would unfairly cripple it and is not the echo
        // behavior under test.
        double e = 0.0;
        for (float v : x) e += double(v) * v;
        const double rms   = std::sqrt(e / std::max<size_t>(1, x.size()));
        const double scale = rms > 0.0 ? 0.06 / rms : 0.0;
        for (float& v : x) v = static_cast<float>(v * scale);
        return x;
    }

    inline std::vector<float> white(size_t n, uint32_t seed, float amp = 0.05f) {
        std::mt19937                    rng(seed);
        std::normal_distribution<float> g(0.0f, amp);
        std::vector<float>              x(n);
        for (auto& v : x) v = g(rng);
        return x;
    }

    // ------------------------------------------------------------------
    // Echo paths — synthesized at the run rate (no resampling), image-
    // source-like: a direct arrival after a bulk delay, then an
    // exponentially decaying diffuse tail with the given RT60. Character
    // matches the committed measured RIR fixtures (cabin ~60 ms, room
    // ~200 ms, hall ~500 ms); unit-energy normalized, coupling loss set
    // by an overall gain.
    // ------------------------------------------------------------------
    inline std::vector<float> make_rir(double fs, double delay_ms, double rt60_ms, double len_ms,
                                       double coupling_db, uint32_t seed) {
        const size_t                    n    = static_cast<size_t>(fs * len_ms / 1000.0);
        const size_t                    d0   = static_cast<size_t>(fs * delay_ms / 1000.0);
        std::mt19937                    rng(seed);
        std::normal_distribution<float> g(0.0f, 1.0f);
        std::vector<float>              h(n, 0.0f);
        const double tau = rt60_ms / 1000.0 / 6.9078; // RT60 -> e-fold: 60 dB = 6.9078 nepers
        for (size_t i = d0; i < n; ++i) {
            const double t = static_cast<double>(i - d0) / fs;
            h[i]           = static_cast<float>(g(rng) * std::exp(-t / tau));
        }
        if (d0 < n) h[d0] += 1.5f; // a stronger direct arrival
        // Unit-energy, then apply coupling loss (echo path gain).
        double e = 0.0;
        for (float v : h) e += double(v) * v;
        const double norm = (e > 0.0) ? std::pow(10.0, -coupling_db / 20.0) / std::sqrt(e) : 0.0;
        for (float& v : h) v = static_cast<float>(v * norm);
        return h;
    }

    struct room {
        std::string name;
        double      delay_ms, rt60_ms, len_ms, coupling_db;
    };
    inline const std::vector<room>& rooms() {
        static const std::vector<room> r = {
            {"cabin", 3.0, 60.0, 80.0, 12.0},  // small car cabin, tight coupling
            {"room", 8.0, 200.0, 260.0, 18.0}, // office / living room
            {"hall", 12.0, 500.0, 600.0, 24.0} // reverberant hall
        };
        return r;
    }

    // ------------------------------------------------------------------
    // Convolution (sample-accurate, direct — paths are short) and the
    // black-box stream runner with latency alignment.
    // ------------------------------------------------------------------
    inline std::vector<float> convolve(const std::vector<float>& x, const std::vector<float>& h) {
        std::vector<float> d(x.size(), 0.0f);
        const size_t       lh = h.size();
        for (size_t i = 0; i < x.size(); ++i) {
            double acc = 0.0;
            const size_t kmax = std::min(lh, i + 1);
            for (size_t k = 0; k < kmax; ++k) acc += double(h[k]) * double(x[i - k]);
            d[i] = static_cast<float>(acc);
        }
        return d;
    }

    // Stream (far, mic) through the backend in native frames; return the
    // cleaned output, shifted back by the backend's declared latency so
    // it is sample-aligned with far/mic (zeros pad the tail).
    inline std::vector<float> run_stream(aec_backend& be, const std::vector<float>& far,
                                         const std::vector<float>& mic, double fs) {
        const size_t       fr = be.frame();
        const size_t       n  = (far.size() / fr) * fr;
        std::vector<float> out(far.size(), 0.0f);
        std::vector<float> of(fr), mf(fr), ef(fr);
        for (size_t i = 0; i < n; i += fr) {
            std::copy_n(&far[i], fr, of.begin());
            std::copy_n(&mic[i], fr, mf.begin());
            be.process(of.data(), mf.data(), ef.data());
            std::copy_n(ef.begin(), fr, &out[i]);
        }
        const size_t shift = static_cast<size_t>(be.latency_ms() * fs / 1000.0 + 0.5);
        if (shift == 0 || shift >= out.size()) return out;
        std::vector<float> aligned(out.size(), 0.0f);
        std::copy(out.begin() + shift, out.end(), aligned.begin());
        return aligned;
    }

    // ------------------------------------------------------------------
    // Metric primitives (dB power ratios over a sample window).
    // ------------------------------------------------------------------
    inline double energy(const float* a, size_t n) {
        double s = 0.0;
        for (size_t i = 0; i < n; ++i) s += double(a[i]) * a[i];
        return s;
    }
    // 10log10(sum num^2 / sum den^2) over [lo, hi). A pure energy ratio:
    // delay-invariant to within the window edges, so it stays meaningful
    // when num and den come from black boxes with different latencies
    // (unlike a per-sample num-den subtraction, which a few ms of
    // misalignment on a strong signal completely dominates). Every
    // cross-algorithm metric here is therefore an energy ratio, never a
    // sample-domain difference.
    inline double ratio_db(const std::vector<float>& num, const std::vector<float>& den, size_t lo, size_t hi) {
        double sn = 0.0, sd = 0.0;
        for (size_t i = lo; i < hi; ++i) {
            sn += double(num[i]) * num[i];
            sd += double(den[i]) * den[i];
        }
        if (sd <= 0.0) return sn <= 0.0 ? 0.0 : 200.0;
        return 10.0 * std::log10(sn / sd);
    }

    // Time-to-ERLE-threshold, in ms: earliest window (win samples) whose
    // ERLE(mic vs out) exceeds `thr` dB and stays above it. Returns +inf
    // (1e9) if never reached.
    inline double convergence_ms(const std::vector<float>& mic, const std::vector<float>& out, double fs,
                                 double thr) {
        const size_t win = static_cast<size_t>(0.04 * fs); // 40 ms
        for (size_t i = 0; i + win <= mic.size(); i += win) {
            const double em = energy(&mic[i], win);
            const double eo = energy(&out[i], win);
            if (eo > 0.0 && 10.0 * std::log10(em / eo) >= thr) {
                return 1000.0 * static_cast<double>(i) / fs;
            }
        }
        return 1e9;
    }

    // ------------------------------------------------------------------
    // The metric bundle for one subject at one rate.
    // ------------------------------------------------------------------
    struct metrics {
        std::string subject;
        double      fs = 0.0;
        double      latency_ms = 0.0;
        // Far-end single talk (per room, then reported as the worst/median):
        double erle_db = 0.0;          // steady-state, mic vs out
        double converge_ms = 0.0;      // to 20 dB ERLE
        double reconverge_ms = 0.0;    // to 20 dB after a path swap
        // Near-end single talk: transparency = 10log10(sum out^2/sum near^2)
        // over the near-active region. 0 dB = the near-end passes through
        // untouched; negative = the canceller over-suppresses / ducks it.
        double near_keep_db = 0.0;
        // Double talk (near-end enters after the canceller has converged):
        double dt_echo_supp_db = 0.0;  // echo depth sustained in the DT window (near muted, same far)
        double dt_near_keep_db = 0.0;  // 10log10(sum out^2/sum near^2) during DT: near-end ducking check
        // Cost:
        double us_per_frame = 0.0;
        double x_realtime = 0.0;
        bool   diverged = false;
    };

} // namespace mutap_compare
