// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Loudspeaker band model for the closed-loop test paths: an 80 Hz 2nd-order
// Butterworth highpass cascaded with a 16 kHz 4th-order Butterworth lowpass,
// bilinear with prewarping at 48 kHz — the anti-howl PoC's phase 0 speaker
// model — applied to a room fixture as a finite IR.
//
// Why: the raw fixtures are room responses alone, full band from DC. A real
// speaker cannot drive the loop at 4-22 Hz, but a raw fixture can put the
// loop's limit there: phase 0 found the cabin's limits decided at 4-22 Hz,
// and every ASG number it measured moved by -2.6 to +3.3 dB once the paths
// were band-limited. Band-limiting every feedback path keeps the loop limit
// in band.
//
// Length and normalization mirror the phase 0 path preparation (its
// prep_paths.py): the fixture is zero-padded to `taps` (4096, the fixtures'
// own length), filtered, and the output truncated to `taps`, so the filters'
// ring-out past the end is dropped. The result is NOT re-normalized: the
// unit-energy fixtures come out at energy 0.63-0.67 (the band's loss), and
// the open-loop MSG is computed on the band-limited path itself.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace mutap_test {

    /// One biquad section, normalized a0 = 1: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
    struct biquad_coeffs {
        double b0, b1, b2, a1, a2;
    };

    /// Bilinear (prewarped at fc) 2nd-order section with quality factor q —
    /// a Butterworth section of the order whose pole pair q describes.
    inline biquad_coeffs bilinear_section(double fc, double q, double fs, bool highpass) {
        const double k    = std::tan(std::numbers::pi * fc / fs);
        const double k2   = k * k;
        const double norm = 1.0 / (1.0 + k / q + k2);
        const double b0   = highpass ? norm : k2 * norm;
        return {b0, highpass ? -2.0 * b0 : 2.0 * b0, b0, 2.0 * (k2 - 1.0) * norm, (1.0 - k / q + k2) * norm};
    }

    /// The speaker model's three sections: the 80 Hz Butterworth highpass
    /// (q = 1/sqrt 2) and the 16 kHz 4th-order Butterworth lowpass as its two
    /// pole pairs (q = 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8))).
    inline std::array<biquad_coeffs, 3> loudspeaker_band_sections(double fs = 48000.0) {
        return {bilinear_section(80.0, 1.0 / std::numbers::sqrt2, fs, true),
                bilinear_section(16000.0, 1.0 / (2.0 * std::cos(std::numbers::pi / 8.0)), fs, false),
                bilinear_section(16000.0, 1.0 / (2.0 * std::cos(3.0 * std::numbers::pi / 8.0)), fs, false)};
    }

    /// Band-limit a room path: zero-pad `rir` (n taps) to `taps`, run it
    /// through the speaker model in double, keep the first `taps` samples.
    template <typename T>
    std::vector<double> loudspeaker_band(const T* rir, size_t n, size_t taps = 4096, double fs = 48000.0) {
        std::vector<double> x(taps, 0.0);
        for (size_t i = 0; i < n && i < taps; ++i) {
            x[i] = static_cast<double>(rir[i]);
        }
        for (const biquad_coeffs& s : loudspeaker_band_sections(fs)) {
            double z1 = 0.0; // transposed direct form II
            double z2 = 0.0;
            for (double& v : x) {
                const double in  = v;
                const double out = s.b0 * in + z1;
                z1               = s.b1 * in - s.a1 * out + z2;
                z2               = s.b2 * in - s.a2 * out;
                v                = out;
            }
        }
        return x;
    }

} // namespace mutap_test
