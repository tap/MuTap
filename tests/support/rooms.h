// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The synthetic rooms of the closed-loop (and open-loop) suites, the
// loudspeaker band applied to them, and the seed-set convention the
// closed-loop claims are measured over.
//
// HOW THE CLOSED-LOOP SUITES SPLIT THE WORK:
//
//  - Host suites carry the acoustic claims. A claim runs on band-limited
//    feedback paths (band_limited(), below: without a speaker model the
//    loop's limit is decided where no speaker plays, 0-24 Hz or 16-24 kHz)
//    and asserts medians over >= k_claim_seed_sets seed sets, never a
//    single trajectory (closed-loop runs are chaotic; single-seed ASG
//    moves by dB between platforms).
//  - The emulated selections (tests/bare_metal_main.cpp, TEST_FILTER in
//    tests/CMakeLists.txt) are platform canaries: one seed, checking that
//    the target's arithmetic tracks the host, not an acoustic claim. A
//    canary whose band-limited float margin was under ~4 dB stays on the
//    raw path; each canary's comment says which path it runs and why.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

#include "loudspeaker_band.h"

namespace mutap_test {

    /// Random unit-energy FIR with an exponentially decaying envelope (time
    /// constant taps / 4): the synthetic room, the stand-in true path. The
    /// suites' former local copies (test_closed_loop, test_pem_afc,
    /// test_adaptation_control, test_fd_kalman's random_decaying_fir,
    /// test_aec) were textually identical to this one, so every path they
    /// build is bit-identical. (test_fdaf.cpp keeps its own copy: it scales
    /// by a precomputed 1 / sqrt(energy), which rounds differently.)
    template <typename Sample>
    std::vector<Sample> random_decaying_rir(size_t taps, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              f(taps);
        double                           energy = 0.0;
        for (size_t i = 0; i < taps; ++i) {
            const double v = dist(gen) * std::exp(-static_cast<double>(i) / (static_cast<double>(taps) / 4.0));
            f[i]           = static_cast<Sample>(v);
            energy += v * v;
        }
        for (auto& v : f) {
            v = static_cast<Sample>(static_cast<double>(v) / std::sqrt(energy));
        }
        return f;
    }

    /// The path through the loudspeaker band (loudspeaker_band.h: 80 Hz
    /// 2nd-order highpass, 16 kHz 4th-order lowpass, at the suites' implied
    /// 48 kHz), filtered in double and truncated to the path's own length,
    /// so a canceller sized to the path still spans it. Not re-normalized.
    /// Truncation drops the highpass's ring-out: at 256 taps the speaker
    /// model reads -14.4 dB at DC and -12.2 dB at 20 Hz (-24.1 untruncated);
    /// every band-limited test room still has its loop limit at >= 149 Hz.
    template <typename Sample>
    std::vector<Sample> band_limited(const std::vector<Sample>& path) {
        const std::vector<double> banded = loudspeaker_band(path.data(), path.size(), path.size());
        std::vector<Sample>       out(banded.size());
        for (size_t i = 0; i < banded.size(); ++i) {
            out[i] = static_cast<Sample>(banded[i]);
        }
        return out;
    }

    /// Seed sets for the host claims: set k shifts every near-end seed of a
    /// test by k * k_seed_stride, so set 0 is the test's historical seed.
    inline constexpr unsigned k_claim_seed_sets = 5;
    inline constexpr unsigned k_seed_stride     = 20;
    constexpr unsigned        seed_in_set(unsigned base, unsigned set) {
        return base + k_seed_stride * set;
    }

    /// Median of a sample (the upper median for an even count).
    inline double median(std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[v.size() / 2];
    }

} // namespace mutap_test
