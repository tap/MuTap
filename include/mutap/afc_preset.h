/// @file afc_preset.h
/// @brief The feedback canceller's preset: every rate- and geometry-derived
///        field of a pem_afc and of its howl_guard, from (block, partitions,
///        sample rate), so a consumer sets no sample counts by hand.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHY. pem_afc's and speech_predictor's defaults are SAMPLE counts measured
// at 48 kHz: the pitch search 32 ... 400 samples (a 1500 Hz ceiling and a
// 120 Hz floor), the analysis window 1024 samples (21.3 ms), the shadow
// comparator's retention 0.9742 per block of 64 (a 51 ms time constant). A
// consumer at another rate that keeps them gets a different canceller: at
// 96 kHz the pitch floor becomes 240 Hz and the window 10.7 ms, and
// analysis_capacity >= 2 * max_lag breaks the moment max_lag is rescaled
// (the production-readiness audit of 9 October 2026, item 13). mutap.afc~
// assembled its configuration by hand and rescaled only the shadow
// smoothing and the analysis window. This header is the one place those
// rules live (the AEC chain's counterpart is aec_chain_preset in
// postfilter.h).
//
// WHAT IT SETS, and from what:
//   * fdaf.block_size, fdaf.partitions: the caller's geometry.
//   * analysis_window: k_analysis_window_s (1024 / 48000 s) of samples,
//     rounded up to a multiple of the block and at least two blocks.
//   * the speech predictor's min_lag / max_lag: round(fs / k_pitch_ceiling_hz)
//     and round(fs / k_pitch_floor_hz); its analysis_capacity the larger of
//     the window and 2 * max_lag. The warped predictor gets the capacity
//     only — its warping coefficient is a tuning measured at 48 kHz (lpc.h)
//     and is NOT rescaled: a stated exception, as aec_chain_preset's
//     transition and floor bias are.
//   * shadow_partitions: k_shadow_partitions (2, what mutap.afc~ ships and
//     what the guard's rows were measured with), clamped to the partition
//     count; shadow_smoothing: exp(-block / (fs * k_shadow_tau_s)).
//   Everything else keeps pem_afc's defaults; a consumer's policy choices
//   (the NLMS step size and gates, the Kalman transient floor, @gate) go on
//   top, as mutap.afc~ applies them.
//
// WHAT IT GUARANTEES. At 48 kHz, block 256, 8 partitions — mutap.afc~'s
// shipping geometry — and at 48 kHz, block 64, 16 partitions — the
// closed-loop claims' — the preset reproduces the external's hand-assembled
// configuration field for field (tests/test_afc_preset.cpp pins both, and
// that a canceller built from either produces identical output), so
// adopting it moves no bits. At 44.1 and 96 kHz it produces a valid
// configuration whose open-loop identification matches 48 kHz's (the test
// measures all three); the CLOSED-LOOP claims exist at 48 kHz only
// (SUPPORT.md says which rates are validated and how).
//
// afc_guard_preset: the guard's configuration for one microphone at the
// canceller's block and rate — the two detector fields the geometry forces
// (its top band under k_detector_top * fs so it stays below Nyquist; its
// growth fit spanning at least the 3 blocks howl_detector requires) and the
// two deployment calibrations the caller states in physical units: the loop
// period in seconds and the ceiling in dB. mutap.afc~'s make_guard_config,
// moved here.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "mutap/fd_kalman.h"
#include "mutap/howl_guard.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"

namespace tap::mu {

    /// The preset's physical constants: the 48 kHz measurements, in units
    /// that do not change with the rate.
    namespace afc_preset_detail {
        inline constexpr double k_pitch_floor_hz     = 120.0;              ///< max_lag = fs / this (400 at 48 kHz)
        inline constexpr double k_pitch_ceiling_hz   = 1500.0;             ///< min_lag = fs / this (32 at 48 kHz)
        inline constexpr double k_analysis_window_s  = 1024.0 / 48000.0;   ///< 21.33 ms (1024 samples at 48 kHz)
        inline constexpr double k_shadow_tau_s       = 0.051;              ///< the shadow's power smoothing
        inline constexpr size_t k_shadow_partitions  = 2;                  ///< what mutap.afc~ ships (@shadow 2)
        inline constexpr double k_detector_top       = 0.45;               ///< the detector's top band, fraction of fs
        inline constexpr double k_detector_min_fit_s = 3.0 * (1.0 + 1e-9); ///< growth fit >= 3 blocks

        /// The analysis window for a block at a rate: the physical window
        /// in samples, rounded up to a multiple of the block, at least two
        /// blocks (pem_afc's constraints).
        inline size_t analysis_window(size_t block_size, double sample_rate) {
            const double samples = k_analysis_window_s * sample_rate;
            const auto   blocks  = static_cast<size_t>(std::ceil(samples / static_cast<double>(block_size) - 1e-9));
            return std::max<size_t>(2, blocks) * block_size;
        }

        inline size_t lag(double sample_rate, double hz) {
            return static_cast<size_t>(std::lround(sample_rate / hz));
        }
    } // namespace afc_preset_detail

    /// The feedback canceller's configuration at (block, partitions, rate):
    /// see the file comment for every field it sets. @throws
    /// std::invalid_argument on a block of 0 or a rate that is not finite
    /// and positive; the canceller's own constructor validates the rest.
    template <typename Sample, typename Predictor = speech_predictor<Sample>, typename Core = partitioned_fdkf<Sample>>
    typename pem_afc<Sample, Predictor, Core>::config pem_afc_preset(size_t block_size, size_t partitions,
                                                                     double sample_rate) {
        namespace d = afc_preset_detail;
        if (block_size == 0) {
            throw std::invalid_argument("pem_afc_preset: block_size must be >= 1");
        }
        if (!std::isfinite(sample_rate) || !(sample_rate > 0.0)) {
            throw std::invalid_argument("pem_afc_preset: sample_rate must be finite and > 0");
        }
        typename pem_afc<Sample, Predictor, Core>::config cfg;
        cfg.fdaf.block_size   = block_size;
        cfg.fdaf.partitions   = partitions;
        cfg.analysis_window   = d::analysis_window(block_size, sample_rate);
        cfg.shadow_partitions = std::min(d::k_shadow_partitions, partitions);
        cfg.shadow_smoothing =
            static_cast<Sample>(std::exp(-static_cast<double>(block_size) / (sample_rate * d::k_shadow_tau_s)));
        if constexpr (requires { cfg.predictor.min_lag; }) {
            // The speech cascade: the pitch search in hertz.
            cfg.predictor.min_lag           = d::lag(sample_rate, d::k_pitch_ceiling_hz);
            cfg.predictor.max_lag           = d::lag(sample_rate, d::k_pitch_floor_hz);
            cfg.predictor.analysis_capacity = std::max(cfg.analysis_window, 2 * cfg.predictor.max_lag);
        }
        else {
            // The warped predictor: the window only (its lambda is a 48 kHz
            // tuning, deliberately not rescaled; see the file comment).
            cfg.predictor.analysis_capacity = std::max(cfg.predictor.analysis_capacity, cfg.analysis_window);
        }
        return cfg;
    }

    /// The guard's configuration for one microphone at the canceller's
    /// block and rate, with the loop period and the ceiling in physical
    /// units; the policy stays at howl_guard's defaults for the caller to
    /// set. @throws std::invalid_argument on a bad block or rate (the
    /// guard's own constructor validates the rest).
    template <typename Sample>
    typename howl_guard<Sample>::config afc_guard_preset(size_t block_size, double sample_rate,
                                                         double loop_period_s = 0.010, double ceiling_db = -6.0) {
        namespace d = afc_preset_detail;
        if (block_size == 0) {
            throw std::invalid_argument("afc_guard_preset: block_size must be >= 1");
        }
        if (!std::isfinite(sample_rate) || !(sample_rate > 0.0)) {
            throw std::invalid_argument("afc_guard_preset: sample_rate must be finite and > 0");
        }
        typename howl_guard<Sample>::config cfg;
        cfg.microphones            = 1;
        cfg.block_size             = block_size;
        cfg.sample_rate            = sample_rate;
        cfg.detector.ceiling_db    = static_cast<Sample>(ceiling_db);
        cfg.detector.loop_period_s = static_cast<Sample>(loop_period_s);
        cfg.detector.f_hi_hz = std::min(cfg.detector.f_hi_hz, static_cast<Sample>(d::k_detector_top * sample_rate));
        const double fit     = d::k_detector_min_fit_s * static_cast<double>(block_size) / sample_rate;
        cfg.detector.growth_window_s = std::max(cfg.detector.growth_window_s, static_cast<Sample>(fit));
        return cfg;
    }

} // namespace tap::mu
