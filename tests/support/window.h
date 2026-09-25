// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// The test batteries' symmetric Hann window, in one place. Test-only: the
// shipping headers' windows are nn_suppressor.h's periodic sqrt-Hann (DspTap's
// tap::dsp::periodic_hann) and postfilter.h's Sample-precision symmetric Hann.
//
// symmetric_hann(i, n) = 0.5 - 0.5 * std::cos(2.0 * pi * double(i) / double(n - 1))
//
// evaluated in double, left to right, exactly as the four copies it replaced
// (itu_chain.h's Welch PSD, itu_signals.h's FIR taper and AM-FM analysis
// window, test_postfilter.cpp's band levels), so the move changed no bit of
// any measured number. It is the symmetric form (denominator n - 1, w[0] =
// w[n - 1] = 0), NOT DspTap's periodic window: do not swap one for the other.
#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>

namespace mutap_test {

    /// Symmetric Hann window sample i of n (see the file comment for the exact
    /// expression, a pinned association). @pre n >= 2 and i < n.
    inline double symmetric_hann(std::size_t i, std::size_t n) noexcept {
        return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1));
    }

} // namespace mutap_test
