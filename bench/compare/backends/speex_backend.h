// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Speex MDF subject — the classic multi-delay block-frequency-domain
// NLMS echo canceller (Valin, speexdsp). The textbook adaptive-filter
// reference: no residual suppressor bundled into the number here (Speex
// ships a separate preprocessor for that; we compare the canceller), no
// double-talk detector beyond MDF's own leakage estimate. Built only
// when -DMUTAP_COMPARE_SPEEX=ON.
#pragma once

namespace mutap_compare {
    void register_speex_backend();
}
