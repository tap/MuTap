// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WebRTC AEC3 subject — the canceller inside Chromium / most soft-phones,
// via the maintained webrtc-audio-processing package (AudioProcessing
// module, echo_canceller enabled, mobile_mode off = AEC3). AEC3's output
// is post-suppressor (its linear filter + residual echo suppressor +
// comfort noise are one unit with no public linear-only tap), so its
// fair MuTap peer is the full `mutap` chain, not `mutap-linear`. Only
// the echo canceller is enabled — no AGC, no noise suppression, no
// high-pass — to isolate echo behavior. Built when -DMUTAP_COMPARE_WEBRTC=ON.
#pragma once

namespace mutap_compare {
    void register_webrtc_backend();
}
