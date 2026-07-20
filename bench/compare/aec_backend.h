// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The black-box AEC interface for the cross-algorithm comparison
// (docs/aec-comparison.md). Every subject — MuTap, Speex MDF, WebRTC
// AEC3 — is reduced to the same contract: a streaming canceller that,
// given the far-end reference and the microphone, returns the cleaned
// send signal. Once an algorithm implements this, the driver
// (compare_driver.h) runs it through the SAME echo paths, the SAME
// signals, and the SAME measurement code as every other subject —
// which is exactly what "run their algorithm through our tests" means.
//
// I/O is float32 mono: it is WebRTC AEC3's native domain and MuTap's
// deployment precision, so it is the honest common ground (a float64
// MuTap variant is offered too, for its quality ceiling). Sample rate
// and frame size are the backend's own — the driver adapts the stream
// to whatever each subject wants (WebRTC insists on 10 ms frames,
// Speex on a fixed MDF frame, MuTap on its power-of-two block).
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mutap_compare {

    /// One streaming acoustic-echo canceller, black box.
    class aec_backend {
      public:
        virtual ~aec_backend() = default;

        /// Human-readable subject label (appears in every result row).
        virtual std::string name() const = 0;

        /// Samples consumed and produced per process() call. The driver
        /// feeds exactly this many samples each call.
        virtual std::size_t frame() const = 0;

        /// Designed/algorithmic latency in milliseconds — the send-path
        /// delay the canceller adds (block hop + any internal lookahead).
        /// Used to align the cleaned signal before the timing metrics and
        /// reported in its own column.
        virtual double latency_ms() const = 0;

        /// Return the canceller to its cold, just-constructed state
        /// (zero filter, no convergence) so a fresh scenario can run.
        virtual void reset() = 0;

        /// One frame: far = loudspeaker reference, mic = microphone
        /// (echo + near-end), out = cleaned send. All frame() samples.
        /// far/mic/out may not alias.
        virtual void process(const float* far, const float* mic, float* out) = 0;
    };

    /// A subject the driver can instantiate at a chosen sample rate. The
    /// factory builds a canceller sized for that rate (filter length,
    /// block, partitions all follow from it). Returns nullptr if the
    /// subject cannot run at that rate (e.g. a 16 kHz-only core asked
    /// for 48 kHz) — the driver then skips the row and says so.
    struct subject {
        std::string                                          key;   ///< CLI name, e.g. "mutap", "webrtc"
        std::string                                          blurb; ///< one line for the doc
        std::function<std::unique_ptr<aec_backend>(double fs)> make;
    };

    /// The subject registry. Each backend TU appends its subject(s) at
    /// static-init time via register_subject(); compare_main lists and
    /// selects from whatever linked in, so a build without WebRTC simply
    /// offers fewer subjects rather than failing.
    std::vector<subject>& registry();
    bool                  register_subject(subject s);

} // namespace mutap_compare
