// Test runner main for bare-metal emulated targets (Cortex-M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate selection is baked in. This is a POSITIVE filter:
// the float32 typed suites (the embedded profile this target exists for),
// the double FFT (small; exercises the soft-float path), the LP /
// conditioning suite, the float closed-loop scenarios including the PEM
// canceller's tonal headline, the float-tracks-double oracle check, and
// the learned suppressor's float profile with its own oracle check (the
// wake-word plan's M2: the learned path had never run on target before).
//
// Excluded: the double-typed adaptive suites and the double closed-loop
// scenarios — minutes-to-hours of soft-float virtual audio validating
// target-independent math already covered on every host platform — and the
// bisection-heavy ASG measurements beyond the float ones kept.
//
// MUTAP_ON_TARGET_SOFT_FP64 (the Cortex-M33 leg: single-precision FPU, no
// FP64) additionally drops the long float PEM scenarios. Their cost is not
// the canceller: the closed-loop and echo harnesses simulate the room in
// double on purpose (tests/support/closed_loop.h convolves the feedback
// path in double per sample; the MSG bisection measures in double), and the
// speech predictor's pitch search accumulates in double (lpc.h). That is
// hardware on the M55 and software on the M33: the tonal PEM headline alone
// measured 1030 s under qemu mps2-an505 against 84 s on mps3-an547. Those
// scenarios stay covered on the M55 and Hexagon legs and every host; the
// M33 leg exists for the pure-float32 paths and the learned suppressor.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    // Typed-suite naming: /0 = float, /1 = double (sample_types order).
    ::testing::GTEST_FLAG(filter) =
        "real_fft_test/0.*:real_fft_test/1.*:RealFftCrossPrecision.*:"
        "CertifiedGeometries/fft_backend_parity.*:"
        "fdaf_test/0.*:FdafCrossPrecision.*:FdafConfigValidation.*:FdafRtContract.*:"
        "fd_kalman_test/0.*:fd_kalman_test/1.*:FdKalmanConfigValidation.*:FdKalmanRtContract.*:"
        "Levinson.*:LpcPredictor.*:SpeechPredictor.*:WarpedLpcPredictor.*:PredictorConfigValidation.*:"
        "PemAfcConfigValidation.*:PemAfcRtContract.*:"
        "closed_loop_test/0.*:"
        "AdaptationControlConfigValidation.*:"
#ifndef MUTAP_ON_TARGET_SOFT_FP64
        // The long float PEM scenarios (double harness), see the header comment.
        "kalman_loop_test/0.*:pem_afc_test/0.*:burst_test/0.*:aec_test/0.*:"
#endif
        "nn_suppressor_test/0.*:NnSuppressorCrossPrecision.*:NnChainFloat32.*";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo selects zero tests and RUN_ALL_TESTS() returns 0 — an
    // empty run must not pass green. Checked after the run because gtest
    // only applies the filter inside RUN_ALL_TESTS. The on-target selection
    // is ~57 tests; 30 leaves headroom for legitimate removals without
    // masking a typo.
    const int selected = ::testing::UnitTest::GetInstance()->test_to_run_count();
    if (selected < 30) {
        std::printf("only %d tests selected (expected >= 30): filter is broken\n", selected);
        std::printf("MUTAP_TESTS_COMPLETE rc=1\n");
        return 1;
    }
    // CTest's pass criterion: printed only if we get all the way here, so a
    // crash after gtest's summary cannot register as a pass.
    std::printf("MUTAP_TESTS_COMPLETE rc=%d\n", rc);
    return rc;
}
