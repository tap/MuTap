// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// ITU-T P.501 (04/2025) test-signal generators for the compliance suite
// (docs/itu-compliance.md, Stage 1). Everything here is GENERATED from
// the recommendation's algorithmic descriptions — no ITU signal files
// are redistributed. The layer runs natively at 44.1 kHz, the CSS's own
// sampling rate: P.501's segment framing is sample-exact only there
// (350 ms period = 15435 samples), and P.501 7.2.1.1 b lists 44.1 kHz
// as a preferred calibration rate. Signals for other rates would need
// the >60 dB stopband interpolation of P.501 NOTE 2 (not implemented).
//
// What is spec-exact vs method-equivalent (recorded per the matrix):
//  - CSS voiced segments: EXACT — the literal sample tables 7-1/7-2 of
//    P.501 transcribed below (134/229 values, column-major).
//  - CSS PN segment: compliant-by-construction (flat magnitude, random
//    conjugate-symmetric 0/pi phases, 8192-point adaptive-systems
//    variant per 7.2.1.3) but not bit-exact with ITU reference files —
//    the phase vector is "random" by specification; ours is a fixed
//    mt19937 seed so every run is reproducible.
//  - Real-speech sequences (P.501 7.3.x) are recordings and cannot be
//    generated; the tests that need them are marked in the matrix.
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <vector>

#include "mutap/fft.h"

namespace mutap_test::itu {

    inline constexpr double k_fs = 44100.0; ///< native CSS rate (see header)

    // P.501 Table 7-1: the single-talk voiced segment, 3.04 ms at
    // 44.1 kHz, repeated 16x for 48.62 ms. 16-bit sample values.
    inline constexpr int k_css_voiced_st[134] = {
        -76,   112,   298,   472,   628,   776,   916,   1068,  1234,  1398,  1572,  1752,  1932,  2098,  2244,
        2360,  2456,  2538,  2626,  2730,  2824,  2904,  2964,  2996,  3032,  3072,  3116,  3158,  3180,  3180,
        3168,  3146,  3132,  3122,  3108,  3096,  3076,  3038,  2992,  2930,  2866,  2808,  2764,  2728,  2686,
        2632,  2572,  2496,  2432,  2382,  2362,  2368,  2392,  2410,  2430,  2444,  2460,  2472,  2452,  2398,
        2300,  2178,  2068,  1976,  1892,  1824,  1772,  1742,  1750,  1760,  1762,  1736,  1684,  1624,  1572,
        1516,  1460,  1390,  1306,  1170,  968,   702,   394,   76,    -244,  -594,  -968,  -1384, -1846, -2356,
        -2898, -3462, -4024, -4590, -5154, -5716, -6298, -6912, -7556, -8194, -8719, -8998, -8898, -8378, -7492,
        -6414, -5334, -4428, -3772, -3360, -3128, -3002, -2924, -2870, -2830, -2800, -2792, -2806, -2844, -2888,
        -2898, -2846, -2698, -2460, -2166, -1846, -1544, -1274, -1032, -818,  -626,  -456,  -298,  -130};

    // P.501 Table 7-2: the double-talk voiced segment, 5.19 ms at
    // 44.1 kHz, repeated 14x for 72.69 ms; a different pitch from the
    // single-talk segment so the two stay decorrelated.
    inline constexpr int k_css_voiced_dt[229] = {
        -64,   148,   341,   502,   637,   750,   856,   973,   1095,  1230,  1380,  1486,  1591,  1681,  1763,  1844,
        1927,  2014,  2097,  2170,  2242,  2310,  2364,  2413,  2450,  2474,  2485,  2474,  2446,  2408,  2363,  2322,
        2287,  2243,  2185,  2109,  2009,  1906,  1806,  1709,  1614,  1515,  1416,  1316,  1212,  1118,  1023,  935,
        852,   762,   686,   615,   540,   477,   416,   364,   316,   271,   231,   197,   166,   139,   121,   104,
        96,    88,    88,    96,    97,    109,   124,   139,   158,   178,   206,   227,   255,   291,   323,   357,
        391,   429,   462,   500,   534,   568,   606,   640,   676,   706,   735,   762,   789,   814,   835,   856,
        875,   890,   905,   911,   926,   930,   935,   939,   935,   940,   935,   926,   925,   911,   901,   887,
        875,   857,   845,   826,   812,   789,   766,   751,   727,   709,   690,   660,   645,   626,   604,   588,
        571,   555,   538,   513,   500,   483,   470,   456,   441,   428,   419,   409,   406,   397,   388,   386,
        379,   377,   377,   368,   367,   413,   422,   413,   422,   426,   429,   432,   443,   456,   479,   512,
        561,   618,   686,   770,   856,   959,   1073,  1190,  1319,  1458,  1598,  1749,  1894,  2039,  2179,  2310,
        2413,  2477,  2488,  2461,  2395,  2299,  2174,  2033,  1864,  1663,  1439,  1188,  905,   588,   255,   -73,
        -457,  -852,  -1263, -1693, -2153, -2643, -3152, -3687, -4239, -4797, -5381, -5974, -6571, -7170, -7779, -8382,
        -8968, -9478, -9826, -9925, -9752, -9318, -8667, -7903, -7087, -6287, -5541, -4849, -4202, -3601, -3033, -2506,
        -2015, -1553, -1120, -726,  -364};

    // ------------------------------------------------------------ FIR helpers

    /// Windowed-sinc linear-phase low-pass (Kaiser window), designed for
    /// >= 80 dB stopband. Offline test tooling — not RT code.
    inline std::vector<double> design_lowpass(double fs, double cutoff_hz, size_t taps) {
        assert(taps % 2 == 1);
        std::vector<double> h(taps);
        const double        fc        = cutoff_hz / fs;
        const long          mid       = static_cast<long>(taps / 2);
        const double        beta      = 7.857; // Kaiser beta for ~80 dB
        auto                bessel_i0 = [](double v) {
            double sum  = 1.0;
            double term = 1.0;
            for (int i = 1; i < 32; ++i) {
                term *= (v / (2.0 * i)) * (v / (2.0 * i));
                sum += term;
            }
            return sum;
        };
        const double i0b = bessel_i0(beta);
        for (long i = 0; i < static_cast<long>(taps); ++i) {
            const long   m            = i - mid;
            const double s            = (m == 0) ? 2.0 * fc
                                                 : std::sin(2.0 * std::numbers::pi * fc * static_cast<double>(m))
                                            / (std::numbers::pi * static_cast<double>(m));
            const double r            = static_cast<double>(m) / static_cast<double>(mid);
            h[static_cast<size_t>(i)] = s * bessel_i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        }
        return h;
    }

    /// Linear-phase FIR from a magnitude response given as corner points
    /// (log-frequency straight-line interpolation in dB — the form
    /// P.501's Figures 7-10/7-11 and Hoth-style spectra use), via
    /// frequency sampling. `corners` = {Hz, dB} pairs, ascending.
    inline std::vector<double> design_from_corners(double fs, const std::vector<std::pair<double, double>>& corners,
                                                   size_t fft_size = 4096) {
        auto gain_at = [&](double f) {
            if (f <= corners.front().first) {
                return corners.front().second;
            }
            if (f >= corners.back().first) {
                return corners.back().second;
            }
            for (size_t i = 1; i < corners.size(); ++i) {
                if (f <= corners[i].first) {
                    const double lf0 = std::log10(corners[i - 1].first);
                    const double lf1 = std::log10(corners[i].first);
                    const double t   = (std::log10(f) - lf0) / (lf1 - lf0);
                    return corners[i - 1].second + t * (corners[i].second - corners[i - 1].second);
                }
            }
            return corners.back().second;
        };
        // Zero-phase magnitude sampling, then a real IFFT and circular
        // shift to make it causal linear-phase.
        mutap::real_fft     fft(fft_size);
        std::vector<double> spec(fft_size, 0.0);
        const double        df = fs / static_cast<double>(fft_size);
        spec[0]                = std::pow(10.0, gain_at(1e-3) / 20.0);
        spec[1]                = std::pow(10.0, gain_at(fs / 2.0) / 20.0); // packed Nyquist
        for (size_t k = 1; k < fft_size / 2; ++k) {
            spec[2 * k]     = std::pow(10.0, gain_at(df * static_cast<double>(k)) / 20.0);
            spec[2 * k + 1] = 0.0;
        }
        std::vector<double> h(fft_size);
        fft.inverse(spec.data(), h.data());
        std::rotate(h.begin(), h.begin() + static_cast<long>(fft_size / 2), h.end());
        // Hann-window the centre half to bound leakage, keep odd length.
        const size_t        taps = fft_size / 2 + 1;
        std::vector<double> out(taps);
        for (size_t i = 0; i < taps; ++i) {
            const double w =
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(taps - 1));
            out[i] = h[fft_size / 2 - taps / 2 + i] * w;
        }
        return out;
    }

    inline std::vector<double> fir_apply(const std::vector<double>& x, const std::vector<double>& h) {
        std::vector<double> y(x.size(), 0.0);
        const size_t        half = h.size() / 2; // compensate linear-phase delay
        for (size_t n = 0; n < x.size(); ++n) {
            double acc = 0.0;
            for (size_t k = 0; k < h.size(); ++k) {
                const long idx = static_cast<long>(n + half) - static_cast<long>(k);
                if (idx >= 0 && idx < static_cast<long>(x.size())) {
                    acc += h[k] * x[static_cast<size_t>(idx)];
                }
            }
            y[n] = acc;
        }
        return y;
    }

    /// P.501 Figure 7-10: the speech-shaping response for the fullband
    /// CSS PN segment (5 dB/octave tilt, corner-point table).
    inline std::vector<std::pair<double, double>> speech_shaping_corners() {
        return {{50.0, -0.8},  {100.0, 9.2},   {200.0, 19.6},  {215.0, 18.8},  {500.0, 13.1},  {1000.0, 8.0},
                {2850.0, 0.4}, {3600.0, -1.3}, {5000.0, -3.5}, {7690.0, -6.6}, {7800.0, -7.8}, {8700.0, -20.0}};
    }

    /// P.501 Figure 7-11: the narrow-band CSS band-limiting/shaping
    /// response (200 Hz - 3.6 kHz, speech tilt, steep upper skirt).
    inline std::vector<std::pair<double, double>> narrowband_shaping_corners() {
        return {{50.0, -25.8}, {100.0, -12.8}, {200.0, 17.4},  {215.0, 17.8},   {500.0, 12.2},
                {1000.0, 7.2}, {2850.0, 0.0},  {3600.0, -2.0}, {3660.0, -20.0}, {3680.0, -30.0}};
    }

    // ------------------------------------------------------------------- CSS

    enum class css_kind { single_talk, double_talk };

    struct css_config {
        css_kind kind    = css_kind::single_talk;
        size_t   periods = 4;     ///< periods to emit (polarity alternates)
        bool     shaped  = false; ///< apply the Figure 7-10 speech shaping to the PN part
        unsigned seed    = 501;   ///< PN phase / noise seed (fixed = reproducible)
    };

    /// One CSS stream per P.501 7.2.1.2/7.2.1.3/7.2.1.4 at 44.1 kHz.
    /// Single talk: voiced 2144 (134 x 16) + PN 8820 (8192-point block
    /// cycled) + pause 4471 = 15435 samples (350 ms). Double talk:
    /// voiced 3206 (229 x 14) + Gaussian noise 8820 + pause 5614 =
    /// 17640 samples (400 ms). Unit-agnostic amplitude — scale with
    /// itu::set_level_dbm0 afterwards.
    inline std::vector<double> make_css(const css_config& cfg) {
        const bool   st       = cfg.kind == css_kind::single_talk;
        const int*   tab      = st ? k_css_voiced_st : k_css_voiced_dt;
        const size_t tab_n    = st ? 134 : 229;
        const size_t reps     = st ? 16 : 14;
        const size_t noise_n  = 8820;
        const size_t period_n = st ? 15435 : 17640;
        const size_t voiced_n = tab_n * reps;
        const size_t pause_n  = period_n - voiced_n - noise_n;

        // Voiced part, normalized to unit RMS.
        std::vector<double> voiced(voiced_n);
        double              vsq = 0.0;
        for (size_t i = 0; i < voiced_n; ++i) {
            voiced[i] = static_cast<double>(tab[i % tab_n]) / 32768.0;
            vsq += voiced[i] * voiced[i];
        }
        const double vrms = std::sqrt(vsq / static_cast<double>(voiced_n));
        for (auto& v : voiced) {
            v /= vrms;
        }

        // Measurement segment: PN (single talk) or Gaussian (double talk),
        // RMS-matched to the voiced part (i.e. unity).
        std::vector<double> meas(noise_n);
        std::mt19937        gen(cfg.seed);
        if (st) {
            constexpr size_t            n_fft = 8192;
            mutap::real_fft             fft(n_fft);
            std::vector<double>         spec(n_fft, 0.0);
            std::bernoulli_distribution bit(0.5);
            spec[0] = 0.0;
            spec[1] = 0.0; // DC and Nyquist zero
            for (size_t k = 1; k < n_fft / 2; ++k) {
                spec[2 * k]     = bit(gen) ? 1.0 : -1.0; // exp(j pi i_k) with i_k in {0,1}
                spec[2 * k + 1] = 0.0;
            }
            std::vector<double> block(n_fft);
            fft.inverse(spec.data(), block.data());
            if (cfg.shaped) {
                const auto h = design_from_corners(k_fs, speech_shaping_corners());
                block        = fir_apply(block, h);
            }
            double msq = 0.0;
            for (const double v : block) {
                msq += v * v;
            }
            const double mrms = std::sqrt(msq / static_cast<double>(n_fft));
            for (size_t i = 0; i < noise_n; ++i) {
                meas[i] = block[i % n_fft] / mrms; // cycled to 200 ms per 7.2.1.2
            }
        }
        else {
            std::normal_distribution<double> dist(0.0, 1.0);
            for (auto& v : meas) {
                v = dist(gen);
            }
            double msq = 0.0;
            for (const double v : meas) {
                msq += v * v;
            }
            const double mrms = std::sqrt(msq / static_cast<double>(noise_n));
            for (auto& v : meas) {
                v /= mrms;
            }
        }

        std::vector<double> out;
        out.reserve(period_n * cfg.periods);
        for (size_t p = 0; p < cfg.periods; ++p) {
            const double sign = (p % 2 == 0) ? 1.0 : -1.0; // polarity inversion per period
            for (const double v : voiced) {
                out.push_back(sign * v);
            }
            for (const double v : meas) {
                out.push_back(sign * v);
            }
            for (size_t i = 0; i < pause_n; ++i) {
                out.push_back(0.0);
            }
        }
        return out;
    }

    /// Band-limiting per P.501 Table 7-7 (values in Hz: -3 dB point /
    /// cutoff). NB 3600/4000, WB 7200/8000, SWB 14400/16000. At the
    /// 44.1 kHz native rate fullband needs no filter.
    enum class bandwidth { narrowband, wideband, super_wideband, fullband };

    inline std::vector<double> band_limit(const std::vector<double>& x, bandwidth bw) {
        double cutoff = 0.0;
        switch (bw) {
        case bandwidth::narrowband:
            cutoff = 3800.0;
            break; // -3 dB ~3.6k, deep by 4k
        case bandwidth::wideband:
            cutoff = 7600.0;
            break; // -3 dB ~7.2k, deep by 8k
        case bandwidth::super_wideband:
            cutoff = 15200.0;
            break; // -3 dB ~14.4k, deep by 16k
        case bandwidth::fullband:
            return x;
        }
        return fir_apply(x, design_lowpass(k_fs, cutoff, 1023));
    }

    // ------------------------------------------- AM-FM orthogonal double talk

    /// One channel of the P.501 7.2.4 AM-FM modulated multi-sine:
    /// s(t) = [1 + (2/3) cos(2 pi 3 t)] * sum_n A(n) cos(2 pi f0(n) t +
    /// (df(n)/1 Hz) sin(2 pi t)), with A(n) following the 250 Hz-corner
    /// 5 dB/octave shaping. The two channels' frequency plans interleave
    /// (P.501 Table 7-6, wideband) and never overlap once each component
    /// stays within f0 +- df — that disjointness is what the comb-filter
    /// analysis of the double-talk echo tests relies on.
    struct amfm_plan {
        std::vector<double> f0;
        std::vector<double> df;
    };

    inline amfm_plan amfm_send_plan() {
        amfm_plan    p;
        const double base[] = {125, 250, 500, 750, 1000, 1250, 1500, 1750};
        const double dev[]  = {2.5, 5, 10, 15, 20, 25, 30, 35};
        for (size_t i = 0; i < 8; ++i) {
            p.f0.push_back(base[i]);
            p.df.push_back(dev[i]);
        }
        for (double f = 2000.0; f <= 7000.0; f += 250.0) {
            p.f0.push_back(f);
            p.df.push_back(40.0);
        }
        return p;
    }

    inline amfm_plan amfm_receive_plan() {
        amfm_plan    p;
        const double base[] = {180, 270, 540, 810, 1080, 1350, 1620, 1890};
        const double dev[]  = {2.5, 5, 10, 15, 20, 25, 30, 35};
        for (size_t i = 0; i < 8; ++i) {
            p.f0.push_back(base[i]);
            p.df.push_back(dev[i]);
        }
        for (double f = 2160.0; f <= 6900.0; f += 250.0) {
            p.f0.push_back(f);
            p.df.push_back(35.0);
        }
        return p;
    }

    inline std::vector<double> make_amfm(const amfm_plan& plan, size_t samples) {
        std::vector<double> out(samples, 0.0);
        const double        pi = std::numbers::pi;
        for (size_t c = 0; c < plan.f0.size(); ++c) {
            // 5 dB/octave rolloff above the 250 Hz corner (P.501 7.2.4).
            const double a = (plan.f0[c] <= 250.0) ? 1.0 : std::pow(10.0, -5.0 * std::log2(plan.f0[c] / 250.0) / 20.0);
            for (size_t n = 0; n < samples; ++n) {
                const double t = static_cast<double>(n) / k_fs;
                out[n] += a * std::cos(2.0 * pi * plan.f0[c] * t + plan.df[c] * std::sin(2.0 * pi * t));
            }
        }
        const double am_f  = 3.0;
        const double am_mu = 2.0 / 3.0;
        for (size_t n = 0; n < samples; ++n) {
            const double t = static_cast<double>(n) / k_fs;
            out[n] *= 1.0 + am_mu * std::cos(2.0 * pi * am_f * t);
        }
        return out;
    }

    /// Energy (dB) of a signal inside the comb bands f0 +- (df + guard),
    /// via one long Hann-windowed FFT — the analysis half of the AM-FM
    /// method. P.501 7.2.4.1 sets the comb edges at exactly f0 +- df
    /// (guard 0); widening the guard reaches across the plans' designed
    /// interleave gaps (e.g. send 250+-5 vs receive 270+-5, 10 Hz apart)
    /// and destroys the separation floor.
    inline double comb_band_level_db(const std::vector<double>& x, const amfm_plan& plan, double guard_hz = 0.0) {
        size_t n_fft = 1;
        while (n_fft < x.size()) {
            n_fft *= 2;
        }
        mutap::real_fft     fft(n_fft);
        std::vector<double> buf(n_fft, 0.0);
        // Hann window: the components are not FFT-periodic, and the comb
        // separation the double-talk analysis needs lives or dies on
        // spectral leakage.
        for (size_t i = 0; i < x.size(); ++i) {
            const double w =
                0.5
                - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(x.size() - 1));
            buf[i] = x[i] * w;
        }
        fft.forward_inplace(buf.data());
        const double df_bin = k_fs / static_cast<double>(n_fft);
        double       acc    = 0.0;
        for (size_t c = 0; c < plan.f0.size(); ++c) {
            const size_t k0 = static_cast<size_t>((plan.f0[c] - plan.df[c] - guard_hz) / df_bin);
            const size_t k1 = static_cast<size_t>((plan.f0[c] + plan.df[c] + guard_hz) / df_bin) + 1;
            for (size_t k = std::max<size_t>(1, k0); k <= std::min(n_fft / 2 - 1, k1); ++k) {
                acc += buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1];
            }
        }
        return 10.0 * std::log10(std::max(acc, 1e-30));
    }

    // --------------------------------------------------- activation sequence

    /// P.501 7.3.4-style activation sequence built on the CSS voiced
    /// sound (which is also P.340's activation signal): a 500 ms voiced
    /// token, 500 ms pause, level rising 1 dB per repetition across
    /// `steps` repetitions. Returns the sequence and the sample index at
    /// which each token starts (for build-up timing analysis).
    inline std::vector<double> make_activation_sequence(size_t steps, double start_gain_db,
                                                        std::vector<size_t>* token_starts = nullptr) {
        const size_t        token_n = static_cast<size_t>(0.5 * k_fs);
        const size_t        pause_n = token_n;
        std::vector<double> token(token_n);
        double              sq = 0.0;
        for (size_t i = 0; i < token_n; ++i) {
            token[i] = static_cast<double>(k_css_voiced_st[i % 134]) / 32768.0;
            sq += token[i] * token[i];
        }
        const double rms = std::sqrt(sq / static_cast<double>(token_n));
        for (auto& v : token) {
            v /= rms;
        }
        std::vector<double> out;
        out.reserve((token_n + pause_n) * steps);
        for (size_t s = 0; s < steps; ++s) {
            const double g = std::pow(10.0, (start_gain_db + static_cast<double>(s)) / 20.0);
            if (token_starts != nullptr) {
                token_starts->push_back(out.size());
            }
            for (const double v : token) {
                out.push_back(g * v);
            }
            out.insert(out.end(), pause_n, 0.0);
        }
        return out;
    }

    // ------------------------------------------------------------ noise fields

    /// Hoth noise (the P.800/P.340 room-noise spectrum): white noise
    /// shaped by the standard Hoth density corners (relative dB).
    inline std::vector<double> make_hoth_noise(size_t samples, unsigned seed) {
        static const std::vector<std::pair<double, double>> k_corners = {
            {100.0, 0.0},    {200.0, -1.7},   {400.0, -5.7},   {800.0, -12.4},
            {1000.0, -14.7}, {2000.0, -22.6}, {4000.0, -30.5}, {8000.0, -39.0}};
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              x(samples);
        for (auto& v : x) {
            v = dist(gen);
        }
        return fir_apply(x, design_from_corners(k_fs, k_corners));
    }

    /// Synthetic driving-noise analogue (labeled as such in the matrix:
    /// the automotive recs use vehicle-specific recordings; this is the
    /// documented stand-in): low-frequency-dominated noise, -12 dB/oct
    /// above 120 Hz with a mild shelf, non-stationary +-2 dB slow
    /// amplitude wander.
    inline std::vector<double> make_driving_noise(size_t samples, unsigned seed) {
        static const std::vector<std::pair<double, double>> k_corners = {
            {50.0, 0.0},     {120.0, 0.0},    {240.0, -12.0},  {480.0, -22.0},
            {1000.0, -30.0}, {4000.0, -42.0}, {12000.0, -54.0}};
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              x(samples);
        for (auto& v : x) {
            v = dist(gen);
        }
        auto         y  = fir_apply(x, design_from_corners(k_fs, k_corners));
        const double pi = std::numbers::pi;
        for (size_t n = 0; n < y.size(); ++n) {
            const double t = static_cast<double>(n) / k_fs;
            y[n] *= std::pow(10.0, 2.0 * std::sin(2.0 * pi * 0.13 * t) / 20.0);
        }
        return y;
    }

    // ----------------------------------------------- time-variant echo paths

    /// The rotating-reflector analogue of P.1110 11.11.6 / P.1120
    /// 11.11.6: a base impulse response plus one early reflection whose
    /// delay sweeps sinusoidally (a moving surface changes a path
    /// length) and whose amplitude follows the rotation. path(t) is
    /// rebuilt per block by the caller via fill().
    struct moving_reflector {
        std::vector<double> base;              ///< the static room/cabin IR
        double              tap_gain = 0.1;    ///< reflector strength vs unit-energy base
        double              delay0_s = 0.004;  ///< mean extra path delay
        double              sweep_s  = 0.0008; ///< delay sweep amplitude (+- ~27 cm)
        double              rate_hz  = 0.25;   ///< rotation rate (15 rpm = 0.25 Hz)

        void fill(double t_seconds, std::vector<double>& path) const {
            path.assign(base.begin(), base.end());
            const double d  = delay0_s + sweep_s * std::sin(2.0 * std::numbers::pi * rate_hz * t_seconds);
            const double g  = tap_gain * std::abs(std::cos(2.0 * std::numbers::pi * rate_hz * t_seconds));
            const double ds = d * k_fs;
            const size_t i0 = static_cast<size_t>(ds);
            const double fr = ds - static_cast<double>(i0);
            if (i0 + 1 < path.size()) { // linear-interpolated fractional tap
                path[i0] += g * (1.0 - fr);
                path[i0 + 1] += g * fr;
            }
        }
    };

} // namespace mutap_test::itu
