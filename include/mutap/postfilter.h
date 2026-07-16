/// @file postfilter.h
/// @brief Residual-echo suppressor + comfort noise (the AEC post-filter),
///        and aec_chain — the linear canceller and the post-filter composed
///        into the unit the ITU compliance matrix measures.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/fft.h"
#include "mutap/pem_afc.h"

namespace mutap {

    /// Residual-echo suppressor with matched comfort noise
    /// (docs/itu-compliance.md, Stage 2).
    ///
    /// Linear cancellation measures ~20-25 dB of echo suppression; the
    /// single-talk clauses of the automotive recs want a residual below
    /// -58 dBm0(A) — every compliant product closes that gap with a
    /// post-filter, and the double-talk clauses bound what it may cost:
    /// no more than 3 dB of near-end attenuation (our margin target:
    /// 1.5 dB). Both sides fall out of one rule:
    ///
    ///   coh(k)  = |S_dy(k)|^2 / (S_dd(k) S_yy(k))   MIC-vs-Yhat coherence
    ///   lam(k) <- S_ee(k) / S_yy(k)   learned ONLY while coh(k) > gate
    ///   G(k)    = max(g_min, 1 - beta lam(k) S_yy(k) / S_ee(k))
    ///
    /// where Yhat is the linear canceller's own echo-estimate spectrum
    /// and D = E + Yhat reconstructs the microphone. Two estimators
    /// split the two jobs a residual suppressor has:
    ///
    ///  - coh(D, Yhat) answers "is this bin echo right now?". The
    ///    discriminator deliberately correlates the MIC — not the
    ///    canceller output E — against Yhat: a converged adaptive
    ///    filter keeps E orthogonal to its reference by construction,
    ///    and its weight jitter makes the tiny residual's phase wander,
    ///    so coh(E, Yhat) saturates well below 1 (measured 0.34
    ///    adapting / 0.53 frozen — the freeze experiment that located
    ///    this). The mic instead carries the FULL echo H x, so per bin
    ///    D/Yhat ~ H/Hhat ~ 1: insensitive to adaptation jitter and to
    ///    the convolution-tail (frame-multiplicative) error, since both
    ///    signals traverse nearly the same filter.
    ///
    ///  - lam(k), the LEAKAGE (residual power per unit estimated-echo
    ///    power ~ the canceller's per-bin misalignment), answers "how
    ///    much of E does echo explain?". It is learned only while the
    ///    coherence certifies the bin echo-dominant, and held through
    ///    double talk. The applied gain is the Wiener gain ON E: during
    ///    single talk S_ee ~ lam S_yy so the gain floors; during double
    ///    talk near-end energy inflates S_ee while lam S_yy holds, so
    ///    the gain rides to 1 — even in bins where the analysis
    ///    resolution cannot separate the near-end line from an echo
    ///    line 20 Hz away (P.501's AM-FM plans interleave that tightly
    ///    at the low end; a gain keyed to coherence alone measurably
    ///    took 5.5 dB off the near end there, hitting both signals
    ///    1-for-1). Transparency is intrinsic: not a detector decision
    ///    but the shape of the rule.
    ///
    /// Suppressed bins are refilled with COMFORT NOISE shaped to the
    /// tracked near-end noise floor N(k) (two-window minimum
    /// statistics): out = G E + sqrt(max(0, N - G^2 |E|^2)) . unit-noise,
    /// so the output sits at the true noise floor instead of pumping
    /// between "noise" and "digital silence" (P.111x comfort-noise
    /// clauses: level within +2/-5 dB, spectrum within mask).
    ///
    /// Framing matches the canceller: overlap-save on [previous block,
    /// current block], per-bin gains, last half kept — zero added
    /// latency beyond the canceller's own block. Gains are time-smoothed
    /// (fast attack toward suppression, slower release) and floor-capped
    /// to bound musical noise.
    ///
    /// Real-time contract: constructor allocates and may throw; every
    /// post-construction entry point is noexcept and allocation-free
    /// (the comfort-noise source is a xorshift PRNG, not <random>).
    template <typename Sample>
    class residual_suppressor {
      public:
        struct config {
            size_t block_size = 256; ///< must match the canceller's block size
            /// Analysis window = this many blocks (power of 2). The
            /// suppressor's frequency resolution is what separates echo
            /// from near-end structure — at block 256 / 48 kHz one block
            /// is 93.75 Hz per bin, too coarse for the AM-FM double-talk
            /// combs (~90-160 Hz interleave); 8 blocks (23.4 Hz bins,
            /// Hann-windowed estimation) resolves them.
            size_t analysis_blocks    = 8;
            Sample max_suppression_db = 40;          ///< g_min = -this in dB (floor per bin)
            Sample over_subtraction   = Sample(1.2); ///< beta on the coherence (>= 1)
            /// One-pole smoothing of the coherence/power accumulators,
            /// [0, 1) (~100 ms at block 256 / 48 kHz).
            Sample leakage_smoothing = Sample(0.95);
            /// Coherence above which a bin is certified echo-dominant
            /// and its leakage may be re-learned, [0, 1).
            Sample leakage_gate = Sample(0.9);
            /// Per-bin gain smoothing toward MORE suppression (attack)
            /// and toward LESS (release), [0, 1). Attack is the SLOW one
            /// (~265 ms at block 256 / 48 kHz): estimator variance at
            /// bins the analysis cannot fully resolve makes an
            /// instantaneous attack chatter, and the duty-cycle loss
            /// measured 3 dB off the 250 Hz near-end comb line during
            /// double talk (1.6 dB integrated; 1.0 dB at 0.98). Echo
            /// containment doesn't need the postfilter to be fast — the
            /// canceller's Kalman gain is the fast defense. Release is
            /// the near-end onset path — the switching clauses' build-up
            /// budget (T_r <= 25 ms target): 0.9 measured 26.0 ms to
            /// within 3 dB of the settled double-talk send level, 0.8
            /// measures 15.7 ms and slower release saturates there.
            Sample gain_attack  = Sample(0.98);
            Sample gain_release = Sample(0.85);
            /// Noise-floor tracker (two-window minimum statistics on
            /// smoothed |E|^2): power smoothing [0, 1), window length in
            /// blocks (floor reacts within 1-2 windows; speech bursts
            /// shorter than a window cannot drag it up), and the bias
            /// factor compensating the minimum's undershoot of the mean
            /// (calibrated against the P.111x comfort-noise level
            /// clause; see the measured table in test_postfilter.cpp).
            Sample floor_smoothing = Sample(0.9);
            size_t floor_window    = 128;
            Sample floor_bias      = Sample(4);
            bool   comfort_noise   = true; ///< fill suppressed bins to the noise floor
        };

        explicit residual_suppressor(const config& cfg)
            : m_cfg(validated(cfg))
            , m_n(cfg.analysis_blocks * cfg.block_size)
            , m_fft(m_n)
            , m_g_min(std::pow(Sample(10), -cfg.max_suppression_db / Sample(20)))
            , m_input(m_n)
            , m_ywin(m_n)
            , m_yspec(m_n)
            , m_dspec(m_n)
            , m_window(m_n)
            , m_spec(m_n)
            , m_time(m_n)
            , m_sdy_re(m_n / 2 + 1)
            , m_sdy_im(m_n / 2 + 1)
            , m_sdd(m_n / 2 + 1)
            , m_syy(m_n / 2 + 1)
            , m_see(m_n / 2 + 1)
            , m_leak(m_n / 2 + 1)
            , m_gain(m_n / 2 + 1)
            , m_psm(m_n / 2 + 1)
            , m_min_cur(m_n / 2 + 1)
            , m_min_prev(m_n / 2 + 1)
            , m_gspec(m_n)
            , m_gtime(m_n) {
            reset();
        }

        size_t block_size() const noexcept { return m_cfg.block_size; }
        size_t analysis_bins() const noexcept { return m_n / 2 + 1; }
        /// Introspection for tests/diagnostics: the smoothed per-bin gain
        /// and tracked leakage (analysis-bin index).
        Sample gain_at(size_t k) const noexcept { return m_gain[k]; }
        /// The mic-vs-echo-estimate coherence gating the leakage learner.
        Sample coherence_at(size_t k) const noexcept {
            const Sample c =
                (m_sdy_re[k] * m_sdy_re[k] + m_sdy_im[k] * m_sdy_im[k]) / (m_sdd[k] * m_syy[k] + Sample(1e-20));
            return c > Sample(1) ? Sample(1) : c;
        }
        /// The learned per-bin leakage lam (the canceller's effective
        /// per-bin misalignment; 1 until first certified echo-dominant).
        Sample leakage_at(size_t k) const noexcept { return m_leak[k]; }

        void reset() noexcept {
            for (size_t i = 0; i < m_n; ++i) { // Hann for the ESTIMATION ffts
                m_window[i] = Sample(0.5)
                              - Sample(0.5)
                                    * std::cos(Sample(2) * static_cast<Sample>(std::numbers::pi)
                                               * static_cast<Sample>(i) / static_cast<Sample>(m_n - 1));
            }
            for (auto& v : m_input) {
                v = Sample(0);
            }
            for (auto& v : m_ywin) {
                v = Sample(0);
            }
            for (auto& v : m_sdy_re) {
                v = Sample(0);
            }
            for (auto& v : m_sdy_im) {
                v = Sample(0);
            }
            for (auto& v : m_sdd) {
                v = Sample(0);
            }
            for (auto& v : m_see) {
                v = Sample(0);
            }
            for (auto& v : m_leak) {
                v = Sample(1); // pessimistic: unconverged canceller leaks everything
            }
            for (auto& v : m_syy) {
                v = Sample(0);
            }
            for (auto& v : m_gain) {
                v = Sample(1);
            }
            for (auto& v : m_psm) {
                v = Sample(0);
            }
            // The PREVIOUS-window minimum starts at 0: the floor — and
            // with it the comfort fill — stays OFF until one full window
            // of real signal has been observed.
            for (auto& v : m_min_cur) {
                v = std::numeric_limits<Sample>::infinity();
            }
            for (auto& v : m_min_prev) {
                v = Sample(0);
            }
            m_min_count = 0;
            m_rng       = 0x2545F491U;
        }

        /// Process one block: e is the linear canceller's output,
        /// yhat_block the echo estimate for the SAME block
        /// (pem_afc::echo_estimate_block(), block_size time samples —
        /// windowed HERE at analysis resolution: the canceller's own
        /// 2*block bins cannot resolve the AM-FM double-talk combs, and
        /// a coarse reference bleeds echo credit onto near-end bins),
        /// out receives the suppressed block. e and out may alias. The
        /// constrained gain filter is linear-phase with block_size
        /// samples of delay — the postfilter's only added latency.
        void process_block(const Sample* e, const Sample* yhat_block, Sample* out) noexcept {
            const size_t b    = m_cfg.block_size;
            const size_t bins = m_n / 2 + 1;
            const Sample eps  = Sample(1e-20);

            // Slide both analysis windows by one block.
            for (size_t i = 0; i + b < m_n; ++i) {
                m_input[i] = m_input[i + b];
                m_ywin[i]  = m_ywin[i + b];
            }
            for (size_t i = 0; i < b; ++i) {
                m_input[m_n - b + i] = e[i];
                m_ywin[m_n - b + i]  = yhat_block[i];
            }
            // Signal-path FFT stays RECTANGULAR (exact overlap-save
            // filtering); the ESTIMATION FFTs are Hann-windowed —
            // rectangular sidelobes (-13 dB) bleed echo energy into
            // near-end bins and were a measured double-talk
            // transparency failure.
            for (size_t i = 0; i < m_n; ++i) {
                m_spec[i] = m_input[i];
                // d = e + yhat reconstructs the MICROPHONE frame — the
                // discriminator's left-hand signal (see class comment).
                m_dspec[i] = m_window[i] * (m_input[i] + m_ywin[i]);
                m_yspec[i] = m_window[i] * m_ywin[i];
            }
            m_fft.forward_inplace(m_spec.data());
            m_fft.forward_inplace(m_dspec.data());
            m_fft.forward_inplace(m_yspec.data());

            // Pass 1 per analysis bin: gated power-domain leakage ->
            // Wiener gain -> time smoothing (attack 0 = same-block
            // suppression, so talk onsets cannot burst through).
            for (size_t k = 0; k < bins; ++k) {
                Sample d_re;
                Sample d_im;
                Sample y_re;
                Sample y_im;
                packed_bin(m_dspec.data(), k, m_n, d_re, d_im);
                packed_bin(m_yspec.data(), k, m_n, y_re, y_im);

                const Sample pd = d_re * d_re + d_im * d_im + eps;
                const Sample py = y_re * y_re + y_im * y_im + eps;
                // E = D - Yhat by linearity of the windowed transform.
                const Sample e_re = d_re - y_re;
                const Sample e_im = d_im - y_im;
                const Sample pe   = e_re * e_re + e_im * e_im + eps;

                // THE DISCRIMINATOR: magnitude-squared coherence between
                // the MIC (d = e + yhat) and the echo estimate, from
                // smoothed COMPLEX cross-spectra. In single talk
                // D/Yhat ~ H/Hhat ~ 1 per bin — stable against the
                // canceller's weight jitter — so coh -> 1 and the gain
                // collapses. Near-end energy is incoherent with Yhat
                // and DILUTES coh: g = 1 - coh is the Wiener near-end-
                // preserving gain, so double-talk transparency is
                // intrinsic, not a detector decision. (Three rejected
                // designs are in git history: power-leakage ratios hug
                // their noise floor or re-learn near-end energy during
                // sustained double talk; rules keyed to INSTANTANEOUS
                // |E|^2 excursions read loud residual as near-end and
                // pass exactly the blocks that matter; and coh(E, Yhat)
                // saturates at 0.34 while adapting — the update keeps E
                // orthogonal to its reference, so the canceller output
                // is the one signal the echo estimate cannot explain.)
                const Sample a_r = m_cfg.leakage_smoothing;
                m_sdy_re[k] += (Sample(1) - a_r) * ((d_re * y_re + d_im * y_im) - m_sdy_re[k]);
                m_sdy_im[k] += (Sample(1) - a_r) * ((d_im * y_re - d_re * y_im) - m_sdy_im[k]);
                m_sdd[k] += (Sample(1) - a_r) * (pd - m_sdd[k]);
                m_syy[k] += (Sample(1) - a_r) * (py - m_syy[k]);
                m_see[k] += (Sample(1) - a_r) * (pe - m_see[k]);

                Sample coh = (m_sdy_re[k] * m_sdy_re[k] + m_sdy_im[k] * m_sdy_im[k]) / (m_sdd[k] * m_syy[k] + eps);
                coh        = coh > Sample(1) ? Sample(1) : coh;
                if (m_syy[k] <= eps * Sample(100)) {
                    coh = Sample(0); // no estimated echo: nothing to suppress
                }

                // Re-learn the leakage only while the coherence certifies
                // the bin echo-dominant; hold it through double talk.
                if (coh > m_cfg.leakage_gate) {
                    const Sample lam = m_see[k] / (m_syy[k] + eps);
                    m_leak[k] += (Sample(1) - a_r) * (lam - m_leak[k]);
                }

                // Wiener gain ON E, with the estimated residual PSD
                // lam S_yy in the numerator. The denominator takes the
                // INSTANTANEOUS |E|^2 when it exceeds the smoothed power
                // — a near-end onset inside the smoother's lag must ride
                // through, not get clipped by yesterday's S_ee.
                const Sample se  = m_see[k] > pe ? m_see[k] : pe;
                Sample       r   = m_leak[k] * m_syy[k] / (se + eps);
                r                = r > Sample(1) ? Sample(1) : r;
                Sample g         = Sample(1) - m_cfg.over_subtraction * r;
                g                = g < m_g_min ? m_g_min : (g > Sample(1) ? Sample(1) : g);
                const Sample a_g = g < m_gain[k] ? m_cfg.gain_attack : m_cfg.gain_release;
                m_gain[k] += (Sample(1) - a_g) * (g - m_gain[k]);
            }

            // Pass 2: CONSTRAIN the gain response to a causal block_size-
            // tap linear-phase filter (the postfilter's version of the
            // FDAF's gradient constraint). Unconstrained per-bin gains
            // act as a CIRCULAR filter on the frame and smear suppression
            // into bins that must stay transparent — measured: 4-8 dB of
            // near-end attenuation during double talk from this alone.
            // Constrained and causalized, the last block of the frame is
            // a true linear convolution; the price is the filter's
            // block_size/2-sample linear-phase delay.
            m_gspec[0] = m_gain[0];
            m_gspec[1] = m_gain[m_n / 2];
            for (size_t k = 1; k < m_n / 2; ++k) {
                m_gspec[2 * k]     = m_gain[k];
                m_gspec[2 * k + 1] = Sample(0);
            }
            m_fft.inverse(m_gspec.data(), m_gtime.data());
            // Causal window: taps [0, 2b) of h_c[tau] = g_ir[(tau - b) mod n]
            // — a 2*block-tap filter resolves gain structure down to
            // fs/(2*block) (needed for the double-talk combs; a
            // block-tap filter measurably smeared suppression onto the
            // near-end lines). Its linear phase is the postfilter's
            // latency: one block.
            for (size_t tau = 0; tau < m_n; ++tau) {
                m_gspec[tau] = Sample(0);
            }
            for (size_t tau = 0; tau < 2 * b; ++tau) {
                // Rectangular cut, deliberately: a Hann taper here was
                // measured WORSE for double-talk transparency (2.25 dB
                // near-end attenuation vs 1.60) — the taper's doubled
                // mainlobe smears deep echo notches onto near-end comb
                // lines more than the rectangular sidelobes do.
                m_gspec[tau] = m_gtime[(tau + m_n - b) % m_n];
            }
            m_fft.forward_inplace(m_gspec.data());

            // Pass 3: apply the constrained gains + comfort-noise fill.
            for (size_t k = 0; k < bins; ++k) {
                Sample e_re;
                Sample e_im;
                Sample g_re;
                Sample g_im;
                packed_bin(m_spec.data(), k, m_n, e_re, e_im);
                packed_bin(m_gspec.data(), k, m_n, g_re, g_im);

                Sample out_re = g_re * e_re - g_im * e_im;
                Sample out_im = g_re * e_im + g_im * e_re;

                // Near-end noise floor by TWO-WINDOW MINIMUM STATISTICS
                // on the smoothed PRE-gain power |E|^2: the minimum of
                // the last one-to-two windows of smoothed power is the
                // stationary floor — speech and echo bursts shorter than
                // a window cannot drag it up, and the floor is acquired
                // within one window of the first pause. (Two rejected
                // trackers, both measured: asymmetric fast-fall/slow-rise
                // one-poles either chase every downward fluctuation of
                // the exponentially-distributed bin powers — the raw-
                // minimum bias, -8 dB comfort-noise undershoot — or,
                // rise-limited, take tens of seconds to acquire the
                // floor at all.) The bias factor compensates what is
                // left of the minimum's undershoot of the mean. Fill
                // the gap between floor and suppressed power with
                // random-phase noise so suppression never gates below
                // the true near-end floor.
                const Sample p_in = e_re * e_re + e_im * e_im;
                m_psm[k] += (Sample(1) - m_cfg.floor_smoothing) * (p_in - m_psm[k]);
                if (m_psm[k] < m_min_cur[k]) {
                    m_min_cur[k] = m_psm[k];
                }
                if (m_cfg.comfort_noise) {
                    const Sample fl = m_cfg.floor_bias * (m_min_cur[k] < m_min_prev[k] ? m_min_cur[k] : m_min_prev[k]);
                    const Sample po = out_re * out_re + out_im * out_im;
                    const Sample deficit = fl - po;
                    if (deficit > Sample(0)) {
                        const Sample amp = std::sqrt(deficit / Sample(2));
                        out_re += amp * next_noise();
                        out_im += amp * next_noise();
                    }
                }
                store_bin(m_spec.data(), k, m_n, out_re, out_im);
            }

            // Rotate the minimum-statistics windows.
            if (++m_min_count >= m_cfg.floor_window) {
                m_min_count = 0;
                for (size_t k = 0; k < bins; ++k) {
                    m_min_prev[k] = m_min_cur[k];
                    m_min_cur[k]  = std::numeric_limits<Sample>::infinity();
                }
            }

            m_fft.inverse(m_spec.data(), m_time.data());
            for (size_t i = 0; i < b; ++i) {
                out[i] = m_time[m_n - b + i];
            }
        }

      private:
        static config validated(const config& cfg) {
            if (cfg.block_size < 4 || (cfg.block_size & (cfg.block_size - 1)) != 0) {
                throw std::invalid_argument("residual_suppressor: block_size must be a power of 2 >= 4");
            }
            if (cfg.analysis_blocks < 4 || (cfg.analysis_blocks & (cfg.analysis_blocks - 1)) != 0) {
                throw std::invalid_argument("residual_suppressor: analysis_blocks must be a power of 2 >= 4");
            }
            if (cfg.max_suppression_db <= Sample(0)) {
                throw std::invalid_argument("residual_suppressor: max_suppression_db must be positive");
            }
            if (cfg.over_subtraction < Sample(1)) {
                throw std::invalid_argument("residual_suppressor: over_subtraction must be >= 1");
            }
            auto unit = [](Sample v) { return v >= Sample(0) && v < Sample(1); };
            if (!unit(cfg.leakage_smoothing) || !unit(cfg.gain_attack) || !unit(cfg.gain_release)
                || !unit(cfg.floor_smoothing)) {
                throw std::invalid_argument("residual_suppressor: smoothing constants must be in [0, 1)");
            }
            if (!unit(cfg.leakage_gate)) {
                throw std::invalid_argument("residual_suppressor: leakage_gate must be in [0, 1)");
            }
            if (cfg.floor_window == 0 || cfg.floor_bias <= Sample(0)) {
                throw std::invalid_argument("residual_suppressor: floor_window/floor_bias must be positive");
            }
            return cfg;
        }

        /// Packed-format bin access (Ooura layout: [DC, Nyquist, re, im, ...]).
        static void packed_bin(const Sample* s, size_t k, size_t n, Sample& re, Sample& im) noexcept {
            if (k == 0) {
                re = s[0];
                im = Sample(0);
            }
            else if (k == n / 2) {
                re = s[1];
                im = Sample(0);
            }
            else {
                re = s[2 * k];
                im = s[2 * k + 1];
            }
        }

        static void store_bin(Sample* s, size_t k, size_t n, Sample re, Sample im) noexcept {
            if (k == 0) {
                s[0] = re;
            }
            else if (k == n / 2) {
                s[1] = re;
            }
            else {
                s[2 * k]     = re;
                s[2 * k + 1] = im;
            }
        }

        /// Allocation-free noise source in [-1, 1] (xorshift32).
        Sample next_noise() noexcept {
            m_rng ^= m_rng << 13;
            m_rng ^= m_rng >> 17;
            m_rng ^= m_rng << 5;
            return Sample(2) * (static_cast<Sample>(m_rng) / Sample(4294967296.0)) - Sample(1);
        }

        config                 m_cfg;
        size_t                 m_n; ///< FFT size, 2 * block_size
        basic_real_fft<Sample> m_fft;
        Sample                 m_g_min;
        std::vector<Sample>    m_input; ///< sliding analysis window of e
        std::vector<Sample>    m_ywin;  ///< sliding analysis window of the echo estimate
        std::vector<Sample>    m_yspec; ///< Hann-windowed estimation spectrum of the echo estimate
        std::vector<Sample>    m_dspec; ///< Hann-windowed estimation spectrum of the mic d = e + yhat
        std::vector<Sample>    m_window;
        std::vector<Sample>    m_spec;
        std::vector<Sample>    m_time;
        std::vector<Sample>    m_sdy_re; ///< smoothed complex cross-spectrum D . conj(Yhat)
        std::vector<Sample>    m_sdy_im;
        std::vector<Sample>    m_sdd;      ///< smoothed |D|^2
        std::vector<Sample>    m_syy;      ///< smoothed |Yhat|^2
        std::vector<Sample>    m_see;      ///< smoothed |E|^2
        std::vector<Sample>    m_leak;     ///< per-bin leakage lam (residual per unit Yhat power)
        std::vector<Sample>    m_gain;     ///< smoothed per-bin gain
        std::vector<Sample>    m_psm;      ///< smoothed |E|^2 for the floor tracker
        std::vector<Sample>    m_min_cur;  ///< current-window minimum of m_psm
        std::vector<Sample>    m_min_prev; ///< previous-window minimum
        size_t                 m_min_count = 0;
        std::vector<Sample>    m_gspec; ///< constrained gain spectrum (packed)
        std::vector<Sample>    m_gtime; ///< gain impulse response workspace
        std::uint32_t          m_rng = 0x2545F491U;
    };

    /// The unit the compliance matrix measures: a linear canceller
    /// followed by the residual suppressor, sharing one block size.
    /// Same process_block(x, y, e) signature and real-time contract as
    /// the cores themselves.
    ///
    /// The canceller is pluggable, and the default is the RAW
    /// frequency-domain Kalman core — deliberately not pem_afc. PEM
    /// prewhitening earns its keep in the CLOSED loop (feedback), where
    /// the loudspeaker signal is correlated with the near end and a
    /// naive update is biased; open-loop AEC has an exogenous far end,
    /// no bias to fix, and the predictor's block-by-block refit just
    /// injects gradient noise that floors the misalignment near -20 dB
    /// (measured: raw fdkf single-talk residual -73 dBm0(A) where
    /// PEM-Kalman plateaus at -29 on the same scenario, and double talk
    /// barely moves the raw core — its per-bin noise-PSD tracker IS the
    /// double-talk defense). Any type with the 4-argument raw-core
    /// surface process_block(x, y, e, yhat) works, as does pem_afc's
    /// 3-argument surface with echo_estimate_block().
    template <typename Sample, typename Canceller = partitioned_fdkf<Sample>>
    class aec_chain {
      public:
        using canceller_type = Canceller;

        struct config {
            typename Canceller::config                   canceller;
            typename residual_suppressor<Sample>::config postfilter;
        };

        explicit aec_chain(const config& cfg)
            : m_afc(cfg.canceller)
            , m_post(matched(cfg.postfilter, m_afc.block_size()))
            , m_mid(m_afc.block_size())
            , m_yhat(m_afc.block_size()) {}

        size_t block_size() const noexcept { return m_afc.block_size(); }

        Canceller&                         canceller() noexcept { return m_afc; }
        const Canceller&                   canceller() const noexcept { return m_afc; }
        residual_suppressor<Sample>&       postfilter() noexcept { return m_post; }
        const residual_suppressor<Sample>& postfilter() const noexcept { return m_post; }

        void reset() noexcept {
            m_afc.reset();
            m_post.reset();
        }

        void set_adaptation(bool enabled) noexcept { m_afc.set_adaptation(enabled); }

        void process_block(const Sample* x, const Sample* y, Sample* e) noexcept {
            if constexpr (requires(Canceller& c) { c.process_block(x, y, e, e); }) {
                m_afc.process_block(x, y, m_mid.data(), m_yhat.data());
                m_post.process_block(m_mid.data(), m_yhat.data(), e);
            }
            else {
                m_afc.process_block(x, y, m_mid.data());
                m_post.process_block(m_mid.data(), m_afc.echo_estimate_block(), e);
            }
        }

      private:
        static typename residual_suppressor<Sample>::config matched(typename residual_suppressor<Sample>::config pf,
                                                                    size_t block_size) {
            pf.block_size = block_size; // one block size for the chain
            return pf;
        }

        Canceller                   m_afc;
        residual_suppressor<Sample> m_post;
        std::vector<Sample>         m_mid;
        std::vector<Sample>         m_yhat;
    };

} // namespace mutap
