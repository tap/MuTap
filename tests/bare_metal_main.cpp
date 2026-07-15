// Test runner main for bare-metal emulated targets (Cortex-M55 under
// qemu-system-arm): there is no argv on the target, so the
// emulation-appropriate selection is baked in. This is a POSITIVE filter:
// the float32 typed suites (the embedded profile this target exists for),
// the double FFT (small; exercises the soft-float path), the LP /
// conditioning suite, the float closed-loop scenarios including the PEM
// canceller's tonal headline, and the float-tracks-double oracle check.
//
// Excluded: the double-typed adaptive suites and the double closed-loop
// scenarios — minutes-to-hours of soft-float virtual audio validating
// target-independent math already covered on every host platform — and the
// bisection-heavy ASG measurements beyond the float ones kept.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include <cstdio>

#include <gtest/gtest.h>

int main() {
    // Typed-suite naming: /0 = float, /1 = double (sample_types order).
    ::testing::GTEST_FLAG(filter) = "real_fft_test/0.*:real_fft_test/1.*:RealFftCrossPrecision.*:"
                                    "fdaf_test/0.*:FdafCrossPrecision.*:FdafConfigValidation.*:FdafRtContract.*:"
                                    "Levinson.*:LpcPredictor.*:SpeechPredictor.*:PredictorConfigValidation.*:"
                                    "pem_afc_test/0.*:PemAfcConfigValidation.*:PemAfcRtContract.*:"
                                    "closed_loop_test/0.*:burst_test/0.*:"
                                    "AdaptationControlConfigValidation.*";
    ::testing::InitGoogleTest();
    const int rc = RUN_ALL_TESTS();
    // A filter typo selects zero tests and RUN_ALL_TESTS() returns 0 — an
    // empty run must not pass green. Checked after the run because gtest
    // only applies the filter inside RUN_ALL_TESTS. The on-target selection
    // is ~45 tests; 30 leaves headroom for legitimate removals without
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
