/// @file watchdog.h
/// @brief The NaN watchdog shared by every hot path: one finite check per
///        block on the input and the residual, read off the sums the stage
///        already computes, and the contract each stage keeps.
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
// partitioned_fdkf, pem_afc, residual_suppressor, aec_chain, howl_guard):
//
//   * One finite check per block on the input and on the residual. The
//     value checked is a sum the stage computes anyway, so the check is a
//     handful of std::isfinite calls and nothing per sample (see COST):
//       - the cores (fdaf, fdkf): the error spectrum's DC slot, which is the
//         sum of the error block, after the forward transform the update
//         runs. Every fault of the block reaches it: a non-finite input or
//         desired sample lands in the error directly, a non-finite filter
//         bin through the inverse transform (below). On the paths that run
//         no transform - adaptation frozen, the Kalman core's narrowband
//         guard holding - the stage sums the block itself.
//       - pem_afc: nothing of its own while adapting. Every fault of the
//         block (u, y, F_hat, the predictor's state) ends up in the
//         prewhitened pair, where the core's check sees it; pem_afc watches
//         the core's counter and propagates the trip. Frozen, it sums e.
//       - residual_suppressor: the DC slots of its three analysis spectra
//         (the inputs), the DC slot of the constrained gain spectrum (every
//         state that feeds a gain), and two consecutive output samples (the
//         output spectrum; the comfort fill's floor is the one state the
//         gain slot does not see).
//       - aec_chain: the canceller's and the post stage's counters; a trip
//         in either resets the whole chain, policy layer included.
//       - howl_guard::analyze: the detector's block power (its sum of
//         squares, floored before the log) and the two statistics.
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
//     a fresh stage's.
//
// WHY A SPECTRUM SLOT SEES THE WHOLE BLOCK. A NaN or an infinity survives
// every add, subtract and multiply (NaN * 0 and inf * 0 are NaN, inf - inf
// is NaN), so what matters is only which outputs of a transform depend on
// which inputs - a structural fact of the engine, not a numerical one.
// Measured on DspTap's srdif engine (the engine of every double profile and
// of the float profile off the M55), float and double, sizes 128 ... 4096,
// one NaN or infinity in each slot in turn (scratch harness, 9 October
// 2026): the forward transform's DC and Nyquist slots are non-finite for a
// fault in ANY input slot (0 misses of 2n cases per size); the inverse
// transform leaves at least half of its second half non-finite for a fault
// in any packed slot (the cores keep the second half), at least half of the
// last b of n samples for n = 2048, b = 256 and n = 128, b = 64 (the
// suppressor's output slice), never both of the first two samples of that
// slice finite, and never all of the taps [n - b, n) + [0, b) finite (the
// suppressor's gain IR). One forward output slot CAN stay finite for a
// fault in one input slot, and one output sample of the inverse is not a
// check: hence DC slots and sample pairs, never a single sample. The CMSIS
// engine on the bare-metal M55 was not measured; the forward DC slot's
// claim holds for any exact algorithm (X[0] is the plain sum of the
// inputs), and the float suite of test_watchdog.cpp runs on that leg.
//
// COST (the production-readiness plan's criterion: at most 0.1 % of the
// stage on the instruction-count ratchet). The first cut summed every
// input and output block per sample and measured, against main's own
// ratchet run of the same day, +1.00 / +1.71 % on fdkf_48k / fdkf_16k,
// +2.30 / +2.28 % on the suppressor, +2.72 % on the shadow and +1.19 /
// +1.38 % on the chain on the Cortex-M55 (a scalar float reduction is not
// vectorised without reassociation, which no MuTap target permits), +0.58 /
// +0.99 % and +0.67 % on the M33, +0.26 / +0.44 % and +0.19 % on Hexagon;
// nn_suppressor, untouched, read +0.000 % on all three, so the instrument
// resolves this. This cut's counts are recorded below from the PR's run.
//
// A block of finite samples large enough for its sum to overflow Sample
// trips the watchdog too; that is treated as the fault it is. The check
// relies on std::isfinite honouring non-finite values: a build with
// -ffinite-math-only (or -ffast-math, which implies it) defeats it, and no
// MuTap target sets either.

#pragma once

#include <cmath>
#include <cstddef>

/// The trip paths are cold: out of line and marked so, to leave the hot
/// functions' size — and the compiler's inlining decisions inside them —
/// as they were. MUTAP_NOINLINE keeps a stage's per-block entry point out
/// of line where a wrapper (aec_chain) would otherwise inline it into a
/// function so large that the compiler stops inlining the stage's own
/// per-bin helpers: one call per block instead of a dozen per bin. Measured reason: with the trips inline, the second
/// cut read +0.14 % on the suppressor and +0.8 % on the chain on the Cortex-M33 (GCC) against +0.003 % on the Kalman
/// core beside them, and -0.16 % on the M55's suppressor: a few isfinite calls cannot cost that; a shifted inlining
/// decision inside the suppressor's per-bin loops can.
#if defined(__GNUC__) || defined(__clang__)
#define MUTAP_WATCHDOG_COLD [[gnu::cold, gnu::noinline]]
#define MUTAP_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
#define MUTAP_WATCHDOG_COLD __declspec(noinline)
#define MUTAP_NOINLINE __declspec(noinline)
#else
#define MUTAP_WATCHDOG_COLD
#define MUTAP_NOINLINE
#endif

namespace tap::mu::detail {

    /// Sum of squares of n samples, accumulated in Sample: the fallback for
    /// a path that computes no transform. Non-finite for any non-finite
    /// sample (a NaN propagates; an infinity squares to an infinity, and
    /// inf - inf cannot arise in a sum of squares).
    template <typename Sample>
    inline Sample sum_of_squares(const Sample* x, size_t n) noexcept {
        Sample s = Sample(0);
        for (size_t i = 0; i < n; ++i) {
            s += x[i] * x[i];
        }
        return s;
    }

    /// The watchdog's verdict on a sum: finite, or not.
    template <typename Sample>
    inline bool finite_power(Sample s) noexcept {
        return std::isfinite(s);
    }

} // namespace tap::mu::detail
