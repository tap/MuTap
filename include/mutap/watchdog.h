/// @file watchdog.h
/// @brief The NaN watchdog shared by every hot path: one finite check per
///        block on a signal power, and the contract each stage keeps.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHY. A non-finite sample from upstream (a Max patch's divide by zero, a
// disconnected signal inlet, an interface glitch) enters an adaptive
// filter's state - F_hat, the Kalman P, the suppressor's smoothed spectra,
// the guard's verdict - and stays there until a reset: every later block
// is NaN, silently, forever. Before this file the only finite check in a
// hot path was the NLMS IPC estimator's (fdaf.h), and the guard's analyze()
// floored its statistics with std::max, which a NaN passes (the
// production-readiness audit of 9 October 2026, item 10).
//
// THE CONTRACT (every stage that carries the watchdog: partitioned_fdaf,
// partitioned_fdkf, pem_afc, residual_suppressor, howl_guard::analyze):
//
//   * One finite check per block on the INPUT power (the sum of squares of
//     every input block, computed in Sample) before the block is processed,
//     and one on the RESIDUAL power (the stage's output block) after. A NaN
//     or an infinity anywhere in a block makes its sum non-finite, so the
//     check is one std::isfinite on a scalar; the sums cost two
//     multiply-adds per sample per signal, nothing on the FFT-sized work.
//   * On a failed check the stage resets its state exactly as reset()
//     does (the guard sends that mic to ARMING), writes a block of zeros
//     to its output - never the non-finite block, which would poison the
//     next stage - and increments its watchdog counter. Nothing else moves:
//     the configuration, an adaptation freeze, a soundcheck calibration
//     and the guard's strikes all survive, as they survive reset().
//   * The counter counts trips since construction; reset() keeps it (a
//     trip resets the stage, so a counter cleared by reset() could never
//     read 1). A host reports it: the Max externals on their right outlet.
//   * On finite input the arithmetic is untouched, so the fingerprints
//     (tests/fingerprint_harness.cpp) do not move; tests/test_watchdog.cpp
//     feeds one non-finite block per stage and asserts recovery within one
//     block, the counter at 1, and the next finite block's output equal to
//     a fresh stage's. The cost is measured on the instruction-count
//     ratchet (bench/README.md) and recorded in the stages' headers.
//
// A block of finite samples large enough for its sum of squares to overflow
// Sample (above about 1.8e19 per sample in float) trips the watchdog too;
// that is treated as the fault it is. The check relies on std::isfinite
// honouring non-finite values: a build with -ffinite-math-only (or
// -ffast-math, which implies it) defeats it, and no MuTap target sets
// either.

#pragma once

#include <cmath>
#include <cstddef>

namespace tap::mu::detail {

    /// Sum of squares of n samples, accumulated in Sample. Non-finite for
    /// any non-finite sample (a NaN propagates; an infinity squares to an
    /// infinity, and inf - inf cannot arise in a sum of squares).
    template <typename Sample>
    inline Sample sum_of_squares(const Sample* x, size_t n) noexcept {
        Sample s = Sample(0);
        for (size_t i = 0; i < n; ++i) {
            s += x[i] * x[i];
        }
        return s;
    }

    /// The watchdog's verdict on a power: finite, or not.
    template <typename Sample>
    inline bool finite_power(Sample s) noexcept {
        return std::isfinite(s);
    }

} // namespace tap::mu::detail
