// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// Level calibration and measurement for the ITU compliance suite
// (docs/itu-compliance.md, Stage 1). The recommendations speak in dBm0
// (electrical, at the network reference point), dBPa (acoustic, at the
// mouth reference point) and dBov (digital, relative to overload); the
// simulation runs in linear float. This header pins ONE documented
// mapping between those planes and provides the meters the test
// procedures specify.
//
// Conventions (the simulation's calibration plane):
//  - Digital overload is |x| = 1.0. dBov follows ITU-T G.100.1: the
//    level of a signal relative to a full-scale SQUARE wave, so
//    L_dBov = 20 log10(rms) and a full-scale sine reads -3.01 dBov.
//  - dBm0 uses the A-law relationship between the overload point and
//    the transmission plane: overload = +3.14 dBm0, i.e.
//    L_dBm0 = L_dBov + 3.14. (Nominal receive -16 dBm0 = -19.14 dBov.)
//  - dBPa maps through the alignment the P-series uses for hands-free
//    testing: the nominal acoustic speech level -4.7 dBPa at the MRP is
//    aligned with the nominal digital active speech level -26 dBov
//    (P.501/P.56 normalization), giving L_dBov = L_dBPa - 21.3.
// Any consistent mapping proves the same algorithmic facts; this one is
// recorded so every number in the compliance suite is traceable.

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace mutap_test::itu {

    inline constexpr double k_dbov_per_dbm0 = -3.14; ///< L_dBov = L_dBm0 + this
    inline constexpr double k_dbov_per_dbpa = -21.3; ///< L_dBov = L_dBPa + this

    inline double rms_of(const double* x, size_t n) {
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i) {
            acc += x[i] * x[i];
        }
        return std::sqrt(acc / static_cast<double>(n));
    }

    inline double level_dbov(const double* x, size_t n) {
        return 20.0 * std::log10(rms_of(x, n));
    }

    inline double dbm0_to_rms(double dbm0) {
        return std::pow(10.0, (dbm0 + k_dbov_per_dbm0) / 20.0);
    }
    inline double rms_to_dbm0(double rms) {
        return 20.0 * std::log10(rms) - k_dbov_per_dbm0;
    }
    inline double dbpa_to_rms(double dbpa) {
        return std::pow(10.0, (dbpa + k_dbov_per_dbpa) / 20.0);
    }

    /// Scale a signal in place to the given total-RMS level in dBm0
    /// (CSS levels are defined over the WHOLE cycled sequence including
    /// pauses — P.501 7.2.1.1 b).
    inline void set_level_dbm0(std::vector<double>& x, double dbm0) {
        const double r = rms_of(x.data(), x.size());
        const double g = dbm0_to_rms(dbm0) / r;
        for (auto& v : x) {
            v *= g;
        }
    }

    /// Crest factor in dB: peak over RMS.
    inline double crest_factor_db(const double* x, size_t n) {
        double peak = 0.0;
        for (size_t i = 0; i < n; ++i) {
            peak = std::max(peak, std::abs(x[i]));
        }
        return 20.0 * std::log10(peak / rms_of(x, n));
    }

    // ------------------------------------------------------------- A-weighting

    /// IEC 61672 A-weighting as a cascade of bilinear-transformed biquads,
    /// normalized to 0 dB at 1 kHz for the given sample rate. The analog
    /// prototype: four zeros at s = 0, double poles at 20.598997 Hz and
    /// 12194.217 Hz, single poles at 107.65265 Hz and 737.86223 Hz.
    class a_weighting {
      public:
        explicit a_weighting(double fs) {
            const double pi = std::numbers::pi;
            // Bilinear transform of each first-order section 1/(s + w) and
            // zeros s^4 distributed as (1 - z^-1)^4.
            const double w1 = 2.0 * pi * 20.598997;
            const double w2 = 2.0 * pi * 107.65265;
            const double w3 = 2.0 * pi * 737.86223;
            const double w4 = 2.0 * pi * 12194.217;

            // First-order pole sections: H(z) = (1 + z^-1) / (a0 + a1 z^-1)
            // from 1/(s + w), bilinear with PER-POLE frequency prewarping
            // (k_w = w / tan(w / 2fs)) so each pole lands at its analog
            // frequency — the plain transform shades the 12.2 kHz pole
            // pair by ~1.5 dB at 10 kHz at audio rates.
            auto pole = [&](double w) {
                const double kw = w / std::tan(w / (2.0 * fs));
                return std::pair<double, double>{kw + w, w - kw}; // a0, a1
            };
            const auto [p1a0, p1a1] = pole(w1);
            const auto [p2a0, p2a1] = pole(w2);
            const auto [p3a0, p3a1] = pole(w3);
            const auto [p4a0, p4a1] = pole(w4);

            // Assemble three biquads: {zero^2/(p1^2)}, {zero^2/(p4^2)},
            // {1/(p2 p3)} — zeros as (1 - z^-1)^2 pairs.
            m_sections[0] = {1.0, -2.0, 1.0, p1a0 * p1a0, 2.0 * p1a0 * p1a1, p1a1 * p1a1};
            m_sections[1] = {1.0, -2.0, 1.0, p4a0 * p4a0, 2.0 * p4a0 * p4a1, p4a1 * p4a1};
            m_sections[2] = {1.0, 2.0, 1.0, p2a0 * p3a0, p2a0 * p3a1 + p2a1 * p3a0, p2a1 * p3a1};
            // NOTE the third section carries the (1 + z^-1)^2 numerator that
            // the two pole-only bilinear sections contribute.

            // Normalize to unity gain at 1 kHz.
            m_gain         = 1.0;
            const double g = magnitude_at(1000.0, fs);
            m_gain         = 1.0 / g;
        }

        /// Filter a whole buffer (offline use; resets state first).
        std::vector<double> apply(const std::vector<double>& x) const {
            std::vector<double> y = x;
            for (const auto& s : m_sections) {
                double x1 = 0.0;
                double x2 = 0.0;
                double y1 = 0.0;
                double y2 = 0.0;
                for (auto& v : y) {
                    const double in  = v;
                    const double out = (s.b0 * in + s.b1 * x1 + s.b2 * x2 - s.a1 * y1 - s.a2 * y2) / s.a0;
                    x2               = x1;
                    x1               = in;
                    y2               = y1;
                    y1               = out;
                    v                = out;
                }
            }
            for (auto& v : y) {
                v *= m_gain;
            }
            return y;
        }

        /// Magnitude response at frequency f (for validation).
        double magnitude_at(double f, double fs) const {
            const double w  = 2.0 * std::numbers::pi * f / fs;
            const double c1 = std::cos(w);
            const double s1 = std::sin(w);
            const double c2 = std::cos(2.0 * w);
            const double s2 = std::sin(2.0 * w);
            double       re = 1.0;
            double       im = 0.0;
            for (const auto& s : m_sections) {
                const double nre = s.b0 + s.b1 * c1 + s.b2 * c2;
                const double nim = -s.b1 * s1 - s.b2 * s2;
                const double dre = s.a0 + s.a1 * c1 + s.a2 * c2;
                const double dim = -s.a1 * s1 - s.a2 * s2;
                const double den = dre * dre + dim * dim;
                const double hre = (nre * dre + nim * dim) / den;
                const double him = (nim * dre - nre * dim) / den;
                const double ore = re * hre - im * him;
                const double oim = re * him + im * hre;
                re               = ore;
                im               = oim;
            }
            return std::hypot(re, im) * m_gain;
        }

        /// A-weighted level of a buffer in dBm0(A).
        double level_dbm0a(const std::vector<double>& x) const {
            const auto y = apply(x);
            return rms_to_dbm0(rms_of(y.data(), y.size()));
        }

      private:
        struct biquad {
            double b0, b1, b2, a0, a1, a2;
        };
        biquad m_sections[3]{};
        double m_gain = 1.0;
    };

    // ------------------------------------------------------------ level meters

    /// One-pole exponential mean-square level meter (the 35 ms / 5 ms
    /// integrators the P.111x test procedures specify). Emits the level
    /// trace in dBm0 per sample.
    class exp_level_meter {
      public:
        exp_level_meter(double fs, double tau_seconds)
            : m_alpha(1.0 - std::exp(-1.0 / (fs * tau_seconds))) {}

        std::vector<double> trace_dbm0(const std::vector<double>& x) {
            std::vector<double> out(x.size());
            double              ms = 0.0;
            for (size_t i = 0; i < x.size(); ++i) {
                ms += m_alpha * (x[i] * x[i] - ms);
                out[i] = rms_to_dbm0(std::sqrt(std::max(ms, 1e-30)));
            }
            return out;
        }

      private:
        double m_alpha;
    };

    /// Peak of the sliding-window RMS (G.168's 35 ms rectangular window).
    inline double peak_windowed_rms_dbm0(const std::vector<double>& x, double fs, double window_seconds = 0.035) {
        const size_t n = static_cast<size_t>(fs * window_seconds);
        assert(x.size() >= n && n > 0);
        double acc = 0.0;
        for (size_t i = 0; i < n; ++i) {
            acc += x[i] * x[i];
        }
        double best = acc;
        for (size_t i = n; i < x.size(); ++i) {
            acc += x[i] * x[i] - x[i - n] * x[i - n];
            best = std::max(best, acc);
        }
        return rms_to_dbm0(std::sqrt(std::max(best, 1e-30) / static_cast<double>(n)));
    }

    /// Active speech level after the method of ITU-T P.56: a 30 ms
    /// two-stage exponential envelope, a 200 ms hangover, and the
    /// threshold at which the measured active level sits 15.9 dB above
    /// it (found by scanning candidate thresholds downward from the
    /// envelope peak in 0.5 dB steps). Returns the ACTIVE level in dBm0
    /// and, optionally, the activity factor.
    inline double active_speech_level_dbm0(const std::vector<double>& x, double fs, double* activity = nullptr) {
        const double g = std::exp(-1.0 / (fs * 0.03));
        // Two cascaded one-poles on |x| (P.56's p and q).
        std::vector<double> env(x.size());
        double              p        = 0.0;
        double              q        = 0.0;
        double              env_peak = 0.0;
        double              sq       = 0.0;
        for (size_t i = 0; i < x.size(); ++i) {
            sq += x[i] * x[i];
            p        = g * p + (1.0 - g) * std::abs(x[i]);
            q        = g * q + (1.0 - g) * p;
            env[i]   = q;
            env_peak = std::max(env_peak, q);
        }
        const size_t hangover  = static_cast<size_t>(fs * 0.2);
        const double margin_db = 15.9;

        double best_level = rms_to_dbm0(std::sqrt(sq / static_cast<double>(x.size())));
        double best_act   = 1.0;
        for (double thr_db = 20.0 * std::log10(env_peak); thr_db > 20.0 * std::log10(env_peak) - 60.0; thr_db -= 0.5) {
            const double thr    = std::pow(10.0, thr_db / 20.0);
            size_t       active = 0;
            size_t       hang   = 0;
            for (size_t i = 0; i < env.size(); ++i) {
                if (env[i] >= thr) {
                    hang = hangover;
                }
                if (hang > 0) {
                    ++active;
                    --hang;
                }
            }
            if (active == 0) {
                continue;
            }
            const double level_db = 20.0 * std::log10(std::sqrt(sq / static_cast<double>(active)));
            // P.56: as the threshold drops the margin (active level minus
            // threshold) GROWS; the active level is read where the margin
            // first reaches 15.9 dB.
            if (level_db - thr_db >= margin_db) {
                best_level = level_db - k_dbov_per_dbm0;
                best_act   = static_cast<double>(active) / static_cast<double>(env.size());
                break;
            }
        }
        if (activity != nullptr) {
            *activity = best_act;
        }
        return best_level;
    }

} // namespace mutap_test::itu
