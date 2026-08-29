// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// The outdoor close-range scenario (HANDOFF.md "Outdoor close-range AEC"):
// a loudspeaker within an inch of the mic, in the open. This inverts the
// assumptions the ITU battery encodes, on three axes this header models:
//
//  1. NEGATIVE ERL. Near-field coupling puts the echo 10..30 dB ABOVE the
//     far-end reference level at the mic (P.1110's Annex E sweep stops at
//     ERL = 0; this scenario lives below it). make_outdoor_path() takes
//     erl_db directly — the path is built unit-energy and scaled by
//     10^(-erl/20), so echo-at-mic level = reference level - erl_db for
//     broadband material, negative values meaning louder-than-reference.
//
//  2. NO ROOM. Outdoors there is no reverb tail: the path is the direct
//     near-field spike, a short structure-borne/enclosure cluster, and a
//     ground reflection that physics makes tiny (1/r: direct at 0.0254 m
//     vs a ~2 m bounce puts the reflection ~40 dB down before the ground
//     even absorbs anything). All support inside ~7 ms — the certified
//     64/43 ms tap budgets are mostly idle here by construction.
//
//  3. THE TRANSDUCER IS THE PROBLEM. A small driver at outdoor SPL
//     distorts; speaker_drive is the memoryless odd saturator
//     tanh(g x)/g — unity small-signal gain, so the LINEAR component the
//     canceller can identify stays calibrated while the distortion power
//     grows with drive. The echo path is driven by the distorted signal;
//     the canceller sees the clean reference (echo_sim's 4-arg step).
//     Severity presets are calibrated by measured THD at the scenario's
//     -10 dBm0 operating level (gated in test_outdoor.cpp, values in the
//     preset comments). Mic-side overload is echo_sim's mic_clip knob.
//
// Plus the outdoor near-end disturbance: make_wind_noise() — LF-dominated
// spectrum (buffeting, not Hoth) under a slow gust envelope, deterministic
// per seed like every generator in this suite.
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

#include "echo_scenario.h"
#include "itu_levels.h"
#include "itu_signals.h"

namespace mutap_test::outdoor {

    inline constexpr double k_speed_of_sound = 343.0; ///< m/s, 20 C

    // ------------------------------------------------------------- echo path

    /// Geometry of the close-range rig. Defaults: mic one inch from the
    /// driver, device a metre off the ground (a handheld / pole-mount),
    /// grassy-ground energy reflectance ~0.5 (amplitude 0.7).
    struct outdoor_geometry {
        double   coupling_m     = 0.0254; ///< driver-to-mic distance
        double   height_m       = 1.0;    ///< device height above ground
        double   ground_reflect = 0.7;    ///< ground amplitude reflection
        double   structure_db   = -15.0;  ///< enclosure/structure-borne cluster energy vs direct
        double   structure_ms   = 1.5;    ///< cluster decay span after the direct spike
        unsigned seed           = 68;     ///< structure-cluster shape seed
    };

    /// Bandlimited spike: windowed-sinc fractional-delay tap (half-width 8),
    /// the physical stand-in for "a delta through the anti-alias chain".
    inline void add_frac_spike(std::vector<double>& path, double delay_samples, double amplitude) {
        const int  half = 8;
        const long c    = static_cast<long>(std::floor(delay_samples));
        for (long k = c - half + 1; k <= c + half; ++k) {
            if (k < 0 || k >= static_cast<long>(path.size())) {
                continue;
            }
            const double u = static_cast<double>(k) - delay_samples;
            const double s = (u == 0.0) ? 1.0 : std::sin(std::numbers::pi * u) / (std::numbers::pi * u);
            const double w = 0.5 + 0.5 * std::cos(std::numbers::pi * u / static_cast<double>(half)); // Hann taper
            path[static_cast<size_t>(k)] += amplitude * s * w;
        }
    }

    /// The outdoor close-range echo path at fs, `taps` long (use the
    /// certified geometry's tap count so the baseline measures the chain
    /// as deployed), unit-energy then scaled to the requested ERL:
    /// broadband echo-at-mic level = reference level - erl_db.
    inline std::vector<double> make_outdoor_path(double fs, size_t taps, double erl_db,
                                                 const outdoor_geometry& g = {}) {
        std::vector<double> path(taps, 0.0);

        // Direct near-field spike. A fixed 1 ms of bulk delay stands in
        // for the device's ADC/DAC chain (same-clock, known and small —
        // the outdoor rig's delay budget is system latency, not acoustics).
        const double bulk   = 0.001 * fs;
        const double direct = bulk + g.coupling_m / k_speed_of_sound * fs;
        add_frac_spike(path, direct, 1.0);

        // Structure-borne / enclosure cluster: seeded noise under an
        // exponential decay across structure_ms, energy structure_db
        // relative to the (unit-amplitude) direct spike.
        std::mt19937                     gen(g.seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        const size_t                     c0 = static_cast<size_t>(direct) + 2;
        const size_t                     cn = static_cast<size_t>(g.structure_ms * 1e-3 * fs);
        double                           ce = 0.0;
        std::vector<double>              cluster(cn);
        for (size_t i = 0; i < cn; ++i) {
            cluster[i] = dist(gen) * std::exp(-3.0 * static_cast<double>(i) / static_cast<double>(cn));
            ce += cluster[i] * cluster[i];
        }
        const double cg = std::pow(10.0, g.structure_db / 20.0) / std::sqrt(ce);
        for (size_t i = 0; i < cn && c0 + i < taps; ++i) {
            path[c0 + i] += cg * cluster[i];
        }

        // Ground reflection: extra path length vs direct, amplitude down
        // by the distance ratio (1/r) times the ground reflectance.
        const double r_dir = g.coupling_m;
        const double r_ref = 2.0 * std::sqrt(g.height_m * g.height_m + 0.25 * r_dir * r_dir);
        add_frac_spike(path, bulk + r_ref / k_speed_of_sound * fs, g.ground_reflect * r_dir / r_ref);

        double e = 0.0;
        for (const double v : path) {
            e += v * v;
        }
        const double s = std::pow(10.0, -erl_db / 20.0) / std::sqrt(e);
        for (auto& v : path) {
            v *= s;
        }
        return path;
    }

    // ------------------------------------------------- loudspeaker distortion

    /// Memoryless odd-saturation loudspeaker model, y = tanh(g x)/g:
    /// unity gain for small signals (the linear component stays on the
    /// calibration plane), distortion power rising with g. drive <= 0 is
    /// the identity (the linear-path control condition). `clip` (0 = off)
    /// adds a hard ceiling after the saturator — with drive 0 it is the
    /// PURE hard clipper, the multibranch bake-off's off-model drive
    /// (docs/multibranch-canceller.md §4: the basis choice must be made
    /// on a drive the basis was not built from).
    struct speaker_drive {
        double drive = 0.0;
        double clip  = 0.0;

        double operator()(double x) const {
            double y = x;
            if (drive > 0.0) {
                y = std::tanh(drive * x) / drive;
            }
            if (clip > 0.0) {
                y = std::clamp(y, -clip, clip);
            }
            return y;
        }
        std::vector<double> apply(const std::vector<double>& x) const {
            std::vector<double> y(x.size());
            for (size_t i = 0; i < x.size(); ++i) {
                y[i] = (*this)(x[i]);
            }
            return y;
        }
    };

    // Severity presets, calibrated by MEASURED THD on a 1 kHz sine at the
    // scenario operating level of -10 dBm0 (test_outdoor.cpp SpeakerDriveThd
    // pins these).
    inline constexpr double k_drive_mild     = 1.1; ///< -40.4 dB (~1 %) THD: a good driver near its limit
    inline constexpr double k_drive_moderate = 2.5; ///< -27.1 dB (~4.4 %) THD: a small driver pushed for SPL
    inline constexpr double k_drive_severe   = 5.0; ///< -17.8 dB (~13 %) THD: hard limiting territory

    /// THD (dB, harmonics-to-fundamental) of the drive model for an
    /// on-bin sine near f_hz at level_dbm0 — the calibration instrument
    /// behind the presets.
    inline double drive_thd_db(const speaker_drive& sp, double level_dbm0, double fs, double f_hz = 1000.0) {
        const size_t        n_fft = 8192;
        const size_t        bin   = static_cast<size_t>(std::round(f_hz * static_cast<double>(n_fft) / fs));
        const double        amp   = itu::dbm0_to_rms(level_dbm0) * std::numbers::sqrt2;
        std::vector<double> x(n_fft);
        for (size_t i = 0; i < n_fft; ++i) {
            x[i] = sp(amp
                      * std::sin(2.0 * std::numbers::pi * static_cast<double>(bin) * static_cast<double>(i)
                                 / static_cast<double>(n_fft)));
        }
        tap::mu::real_fft fft(n_fft);
        fft.forward_inplace(x.data());
        auto power_at = [&](size_t k) {
            if (k >= n_fft / 2) {
                return 0.0;
            }
            return x[2 * k] * x[2 * k] + x[2 * k + 1] * x[2 * k + 1];
        };
        const double fund = power_at(bin);
        double       harm = 0.0;
        for (size_t h = 2; h * bin < n_fft / 2; ++h) {
            harm += power_at(h * bin);
        }
        return 10.0 * std::log10(harm / fund);
    }

    /// Normalization gain for a nonlinear-basis branch signal at the
    /// scenario operating level: 1 / rms(phi(x)) over shaped CSS at
    /// level_dbm0. Branch signals differ by orders of magnitude in power
    /// (x^3 of an RMS-0.22 signal), and the canceller's P(0) semantics,
    /// regularization floor and float32 headroom all assume O(1) inputs
    /// (docs/multibranch-canceller.md §4) — so branch gains are fixed
    /// calibration constants measured by this instrument, not adaptive.
    template <typename Phi>
    double branch_gain(Phi&& phi, double level_dbm0, double fs) {
        itu::css_config cc;
        cc.periods = 3;
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, fs);
        itu::set_level_dbm0(x, level_dbm0);
        double sq = 0.0;
        for (const double v : x) {
            const double p = phi(v);
            sq += p * p;
        }
        return 1.0 / std::sqrt(sq / static_cast<double>(x.size()));
    }

    // ------------------------------------------------------------ wind noise

    /// Outdoor wind buffeting (labeled synthetic, like the driving-noise
    /// stand-in): much steeper LF dominance than Hoth, under a slow gust
    /// envelope — amplitude control points every 0.4 s, cosine-
    /// interpolated, ~+-8 dB wander. Deterministic per seed.
    inline std::vector<double> make_wind_noise(size_t samples, unsigned seed, double fs) {
        static const std::vector<std::pair<double, double>> k_corners = {
            {16.0, 0.0},    {40.0, -2.0},   {80.0, -9.0},    {160.0, -18.0},
            {315.0, -28.0}, {630.0, -38.0}, {1250.0, -48.0}, {5000.0, -62.0}};
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              x(samples);
        for (auto& v : x) {
            v = dist(gen);
        }
        auto y = itu::fir_apply(x, itu::design_from_corners(fs, k_corners));

        const size_t        hop = static_cast<size_t>(0.4 * fs);
        const size_t        pts = samples / hop + 2;
        std::vector<double> ctrl(pts);
        for (auto& v : ctrl) {
            v = 8.0 * dist(gen); // gust wander control points, dB
        }
        for (size_t n = 0; n < y.size(); ++n) {
            const size_t i = n / hop;
            const double t = static_cast<double>(n % hop) / static_cast<double>(hop);
            const double w = 0.5 - 0.5 * std::cos(std::numbers::pi * t); // cosine interp
            const double g = ctrl[i] * (1.0 - w) + ctrl[i + 1] * w;
            y[n] *= std::pow(10.0, g / 20.0);
        }
        return y;
    }

    // --------------------------------------------------------------- runner

    /// One outdoor run: aggregates from measure_from_block onward (the
    /// echo_scenario conventions — erle_db is the observable ratio,
    /// suppression_db the true one), plus the full traces the level
    /// meters read (out = send output, echo = true echo at the mic,
    /// residual = out minus the near-end, the true send-path echo junk).
    struct outdoor_run {
        double              erle_db        = 0.0;
        double              suppression_db = 0.0;
        bool                finite         = true;
        std::vector<double> out;
        std::vector<double> echo;
        std::vector<double> residual;
    };

    /// Drive `p` (any process_block(x, y, e) type, or nullptr) over the
    /// scenario: x_ref is the clean far-end reference the canceller sees,
    /// x_drive the loudspeaker output that excites the path (pass x_ref
    /// itself for a linear speaker), v the near-end (nullptr = silent).
    template <typename Proc>
    outdoor_run run_outdoor(echo_sim<double>& sim, Proc* p, const std::vector<double>& x_ref,
                            const std::vector<double>& x_drive, const std::vector<double>* v,
                            size_t measure_from_block = 0) {
        assert(x_drive.size() >= x_ref.size());
        assert(v == nullptr || v->size() >= x_ref.size());
        const size_t b = sim.block_size();
        outdoor_run  r;
        r.out.reserve(x_ref.size());
        r.echo.reserve(x_ref.size());
        r.residual.reserve(x_ref.size());

        double mic      = 0.0;
        double echo     = 0.0;
        double error    = 0.0;
        double residual = 0.0;
        for (size_t blk = 0; blk + 1 <= x_ref.size() / b; ++blk) {
            const auto en = sim.step(&x_ref[blk * b], &x_drive[blk * b], v != nullptr ? &(*v)[blk * b] : nullptr, p);
            if (!std::isfinite(en.error) || !std::isfinite(en.residual)) {
                r.finite = false;
                continue;
            }
            if (blk >= measure_from_block) {
                mic += en.mic;
                echo += en.echo;
                error += en.error;
                residual += en.residual;
            }
            const auto& e = sim.error_block();
            const auto& d = sim.echo_block();
            for (size_t i = 0; i < b; ++i) {
                r.out.push_back(e[i]);
                r.echo.push_back(d[i]);
                r.residual.push_back(e[i] - (v != nullptr ? (*v)[blk * b + i] : 0.0));
            }
        }
        r.erle_db        = 10.0 * std::log10(mic / error);
        r.suppression_db = 10.0 * std::log10(echo / residual);
        return r;
    }

} // namespace mutap_test::outdoor
