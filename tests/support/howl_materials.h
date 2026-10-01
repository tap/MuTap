/// @file howl_materials.h
/// @brief The howl detector's false-trip materials: the harness's near ends
///        plus the adversarial ones (note onsets, vibrato, crescendo,
///        entrances after silence or a backing track).
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Shared by tests/test_howl_detector.cpp and its host sweep, so a material
// name means the same signal wherever a number was measured. Every signal is
// deterministic (seeded, portable variates: portable_random.h) and runs at
// an implied 48 kHz. Levels: the "on" parts of every material sit at unit
// RMS, as the harness's near ends do (closed_loop.h), so the 40 dB howl rule
// (block RMS >= 100) means the same thing everywhere.
//
// What each one is (names as in the safety design note, Rev 2):
//
//   voiced      voiced_near_end: a 160-sample (300 Hz) pitch impulse train
//               through the harness's AR(4) envelope (resonances at
//               1440 / 5280 Hz): equal-height partials shaped by the AR, a
//               perfectly periodic held note.
//   music       music_near_end: an A-major chord (A1, C#2, E2) with 1/h
//               partials.
//   unison      voiced at 160 + voiced at 161 samples (300.0 / 298.1 Hz):
//               every partial beats, rising out of a null twice a second at
//               the fundamental, h times as often at partial h.
//   speech      ar_near_end: white noise through the AR(4).
//   white       white_near_end: unit white noise; also what a backing track
//               alone looks like to the detector (the harness's aux is white).
//   voiced_aux12, voiced_aux0
//               voiced plus white at -12 / 0 dB: a singer over a backing
//               track (open signals; in a loop the track goes to the speaker).
//   onsets      a note sequence: 8 pitches (107-240 samples, 200-449 Hz),
//               notes 0.25-0.8 s with a 3 ms attack and a 30 ms release,
//               gaps 0-0.25 s at a -60 dB white floor (some legato).
//   vibrato     voiced at 300 Hz +- 3 % (about +-50 cents) at 5.5 Hz.
//   crescendo   voiced ramped from -30 dB to 0 dB at 10, 20 or 40 dB/s in
//               turn, held 1 s, dropped at 40 dB/s, held 0.5 s at -30 dB.
//   entrance    3 s of a -60 dB white floor, then 4 s of voiced; repeating.
//   aux_entrance
//               as entrance, the floor at -30 dB (a cancelled backing
//               track's residual) and kept under the voice.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <string_view>
#include <vector>

#include "closed_loop.h"
#include "portable_random.h"

namespace mutap_test::howl {

    inline constexpr double k_fs = 48000.0;

    enum class material {
        voiced,
        music,
        unison,
        speech,
        white,
        voiced_aux12,
        voiced_aux0,
        onsets,
        vibrato,
        crescendo,
        entrance,
        aux_entrance
    };

    inline constexpr std::array<material, 12> k_all_materials = {
        material::voiced,  material::music,        material::unison,      material::speech,
        material::white,   material::voiced_aux12, material::voiced_aux0, material::onsets,
        material::vibrato, material::crescendo,    material::entrance,    material::aux_entrance};

    inline std::string_view name(material m) {
        switch (m) {
        case material::voiced:
            return "voiced";
        case material::music:
            return "music";
        case material::unison:
            return "unison";
        case material::speech:
            return "speech";
        case material::white:
            return "white";
        case material::voiced_aux12:
            return "voiced_aux12";
        case material::voiced_aux0:
            return "voiced_aux0";
        case material::onsets:
            return "onsets";
        case material::vibrato:
            return "vibrato";
        case material::crescendo:
            return "crescendo";
        case material::entrance:
            return "entrance";
        case material::aux_entrance:
            return "aux_entrance";
        }
        return "?";
    }

    /// Scale x to unit RMS over its whole length.
    inline void normalize(std::vector<double>& x) {
        double e = 0.0;
        for (const double v : x) {
            e += v * v;
        }
        if (e > 0.0) {
            const double g = std::sqrt(static_cast<double>(x.size()) / e);
            for (auto& v : x) {
                v *= g;
            }
        }
    }

    /// voiced_near_end's construction with a time-varying fundamental: an
    /// impulse of height sqrt(period) each time the phase accumulator wraps,
    /// plus the same 0.01 noise floor, through the same AR(4); unit RMS.
    /// `f0` is called once per sample with the time in seconds.
    template <typename F0>
    std::vector<double> voiced_f0(size_t n, unsigned seed, F0 f0) {
        std::mt19937               gen(seed);
        mutap_test::normal<double> dist(0.0, 1.0);
        const double               a1[] = {-2.0 * 0.97 * std::cos(0.03 * 2.0 * std::numbers::pi), 0.97 * 0.97};
        const double               a2[] = {-2.0 * 0.95 * std::cos(0.11 * 2.0 * std::numbers::pi), 0.95 * 0.95};
        std::vector<double>        x(n, 0.0);
        double                     s1[2] = {0.0, 0.0};
        double                     s2[2] = {0.0, 0.0};
        double                     phase = 1.0; // an impulse at n = 0, as voiced_near_end
        for (size_t i = 0; i < n; ++i) {
            const double f = f0(static_cast<double>(i) / k_fs);
            double       w = 0.0;
            if (phase >= 1.0) {
                phase -= 1.0;
                w = std::sqrt(k_fs / f);
            }
            phase += f / k_fs;
            w += 0.01 * dist(gen);
            w     = w - a1[0] * s1[0] - a1[1] * s1[1];
            s1[1] = s1[0];
            s1[0] = w;
            w     = w - a2[0] * s2[0] - a2[1] * s2[1];
            s2[1] = s2[0];
            s2[0] = w;
            x[i]  = w;
        }
        normalize(x);
        return x;
    }

    /// A deterministic uniform [0, 1) stream (64-bit LCG) for the material
    /// schedules: the same draws on every host.
    class schedule_rng {
      public:
        explicit schedule_rng(std::uint64_t seed)
            : m_state(seed * 2654435761ULL + 1ULL) {}
        double next() {
            m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>(m_state >> 11) * 0x1.0p-53;
        }

      private:
        std::uint64_t m_state;
    };

    inline std::vector<double> onsets(size_t n, unsigned seed) {
        static constexpr std::array<double, 8> k_periods = {160, 120, 180, 135, 240, 200, 150, 107};
        schedule_rng                           rng(seed);
        // The pitch schedule first, then one excitation through one AR (the
        // AR state carries across notes, as a voice's tract does).
        std::vector<double> f0(n);
        std::vector<double> gain(n, 0.0);
        size_t              i = 0;
        while (i < n) {
            const double period = k_periods[static_cast<size_t>(rng.next() * 8.0) % 8];
            const auto   len    = static_cast<size_t>((0.25 + 0.55 * rng.next()) * k_fs);
            const auto   gap    = (rng.next() < 0.3) ? size_t{0} : static_cast<size_t>(0.25 * rng.next() * k_fs);
            const auto   attack = static_cast<size_t>(0.003 * k_fs);
            const auto   rel    = static_cast<size_t>(0.030 * k_fs);
            for (size_t k = 0; k < len + gap && i + k < n; ++k) {
                f0[i + k] = k_fs / period;
                double g  = 0.0;
                if (k < attack) {
                    g = static_cast<double>(k) / static_cast<double>(attack);
                }
                else if (k < len) {
                    g = 1.0;
                }
                else if (k < len + rel) {
                    g = 1.0 - static_cast<double>(k - len) / static_cast<double>(rel);
                }
                gain[i + k] = g;
            }
            i += len + gap;
        }
        auto v = voiced_f0(n, seed, [&](double t) { return f0[std::min(n - 1, static_cast<size_t>(t * k_fs + 0.5))]; });
        const auto floor = white_near_end<double>(n, seed + 999);
        for (size_t k = 0; k < n; ++k) {
            v[k] = v[k] * gain[k] + 1e-3 * floor[k];
        }
        return v;
    }

    inline std::vector<double> crescendo(size_t n, unsigned seed) {
        auto                                   v          = voiced_near_end<double>(n, seed, 160);
        static constexpr double                k_floor_db = -30.0;
        static constexpr double                k_drop     = 40.0; // dB/s
        static constexpr std::array<double, 3> k_rates    = {10.0, 20.0, 40.0};
        size_t                                 i          = 0;
        size_t                                 turn       = 0;
        while (i < n) {
            const double rate  = k_rates[turn % 3];
            const double t_up  = -k_floor_db / rate;
            const double t_dn  = -k_floor_db / k_drop;
            const double cycle = 0.5 + t_up + 1.0 + t_dn;
            const auto   len   = static_cast<size_t>(cycle * k_fs);
            for (size_t k = 0; k < len && i + k < n; ++k) {
                const double t  = static_cast<double>(k) / k_fs;
                double       db = k_floor_db;
                if (t < 0.5) {
                    db = k_floor_db;
                }
                else if (t < 0.5 + t_up) {
                    db = k_floor_db + rate * (t - 0.5);
                }
                else if (t < 1.5 + t_up) {
                    db = 0.0;
                }
                else {
                    db = -k_drop * (t - 1.5 - t_up);
                }
                v[i + k] *= std::pow(10.0, db / 20.0);
            }
            i += len;
            ++turn;
        }
        return v;
    }

    /// floor_db: the white floor's level (-60 for entrance, -30 for
    /// aux_entrance, kept under the voice there).
    inline std::vector<double> entrance(size_t n, unsigned seed, double floor_db, bool floor_under_voice) {
        const auto          voice = voiced_near_end<double>(n, seed, 160);
        const auto          floor = white_near_end<double>(n, seed + 999);
        const double        g     = std::pow(10.0, floor_db / 20.0);
        const auto          quiet = static_cast<size_t>(3.0 * k_fs);
        const auto          cycle = static_cast<size_t>(7.0 * k_fs);
        std::vector<double> v(n);
        for (size_t k = 0; k < n; ++k) {
            const bool on = (k % cycle) >= quiet;
            v[k]          = (on ? voice[k] : 0.0) + ((on && !floor_under_voice) ? 0.0 : g * floor[k]);
        }
        return v;
    }

    /// n samples of material m at seed `seed`.
    inline std::vector<double> make(material m, size_t n, unsigned seed) {
        switch (m) {
        case material::voiced:
            return voiced_near_end<double>(n, seed, 160);
        case material::music:
            return music_near_end<double>(n, seed);
        case material::unison: {
            auto       a = voiced_near_end<double>(n, seed, 160);
            const auto b = voiced_near_end<double>(n, seed + 1, 161);
            for (size_t i = 0; i < n; ++i) {
                a[i] += b[i];
            }
            normalize(a);
            return a;
        }
        case material::speech:
            return ar_near_end<double>(n, seed);
        case material::white:
            return white_near_end<double>(n, seed);
        case material::voiced_aux12:
        case material::voiced_aux0: {
            auto         a = voiced_near_end<double>(n, seed, 160);
            const auto   w = white_near_end<double>(n, seed + 777);
            const double g = (m == material::voiced_aux12) ? std::pow(10.0, -12.0 / 20.0) : 1.0;
            for (size_t i = 0; i < n; ++i) {
                a[i] += g * w[i];
            }
            return a;
        }
        case material::onsets:
            return onsets(n, seed);
        case material::vibrato:
            return voiced_f0(
                n, seed, [](double t) { return 300.0 * (1.0 + 0.03 * std::sin(2.0 * std::numbers::pi * 5.5 * t)); });
        case material::crescendo:
            return crescendo(n, seed);
        case material::entrance:
            return entrance(n, seed, -60.0, false);
        case material::aux_entrance:
            return entrance(n, seed, -30.0, true);
        }
        return {};
    }

} // namespace mutap_test::howl
