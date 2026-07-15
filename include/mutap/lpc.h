/// @file lpc.h
/// @brief Linear-prediction analysis and the pluggable near-end models used
///        for PEM prewhitening: autocorrelation, Levinson-Durbin with the
///        conditioning guards, a short-term LP predictor, the speech cascade
///        (short-term + long-term/pitch) and the frequency-warped predictor
///        for music/tonal program material.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// This is the numerically delicate block of the whole feedback canceller
// (HANDOFF.md "hard parts" #1): the autocorrelation -> Levinson-Durbin ->
// prewhitening chain inverts a spectrum and goes ill-conditioned exactly on
// the program material PEM exists for (near-sinusoidal spectra). Guards:
// a relative ridge on r[0] (a synthetic broadband noise floor that bounds
// the prediction gain and keeps reflection coefficients strictly inside the
// unit circle), an absolute silence floor, and an early stop of the
// recursion if a reflection coefficient still escapes or the prediction
// error energy stops being positive. The whitening filter A(q) itself is
// FIR, so a conservatively truncated model is always safe to APPLY.
//
// Near-end model plug-in contract (duck-typed; pem_afc is templated on it):
//   struct config;   struct state;
//   explicit predictor(const config&);        // allocates, may throw
//   state  make_state() const;                // allocates
//   void   reset_state(state&) const noexcept;  // zeroes in place, no allocation
//   void   analyze(const Sample* window, size_t n) noexcept;
//   void   apply(state&, const Sample* in, Sample* out, size_t n) const noexcept;
// analyze() re-fits the model on a window of the feedback-compensated
// signal e (the best proxy for the near-end source v); apply() runs the
// whitening filter A(q) over a stream, with per-stream state so the same
// fitted model can prefilter both u and y.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace mutap {

    /// r[k] = sum_n x[n] x[n-k] for k = 0..max_lag (biased estimate; the
    /// common scale cancels inside Levinson-Durbin).
    template <typename Sample>
    inline void autocorrelation(const Sample* x, size_t n, Sample* r, size_t max_lag) noexcept {
        for (size_t k = 0; k <= max_lag; ++k) {
            Sample acc = Sample(0);
            for (size_t i = k; i < n; ++i) {
                acc += x[i] * x[i - k];
            }
            r[k] = acc;
        }
    }

    /// Levinson-Durbin with conditioning guards. Solves for the prediction
    /// ERROR filter A(z) = 1 + a[1] z^-1 + ... + a[m] z^-m from
    /// autocorrelation r[0..order]; a[0] is set to 1. `ridge` scales r[0] by
    /// (1 + ridge) before the recursion — a synthetic noise floor that caps
    /// the prediction gain near -10*log10(ridge) dB and keeps the recursion
    /// well-conditioned on line spectra. Returns the achieved order: the
    /// recursion stops early (higher coefficients zeroed) if a reflection
    /// coefficient reaches unit magnitude or the error energy stops being
    /// positive, and returns 0 (identity filter) on a silent/degenerate
    /// frame. `scratch` must hold `order` samples.
    template <typename Sample>
    inline size_t levinson(const Sample* r, size_t order, Sample* a, Sample* scratch, Sample ridge) noexcept {
        a[0] = Sample(1);
        for (size_t j = 1; j <= order; ++j) {
            a[j] = Sample(0);
        }
        const Sample r0 = r[0] * (Sample(1) + ridge);
        if (!(r0 > Sample(0)) || !std::isfinite(static_cast<double>(r0))) {
            return 0; // silence or garbage: identity whitening
        }
        Sample err = r0;
        for (size_t i = 1; i <= order; ++i) {
            Sample acc = r[i];
            for (size_t j = 1; j < i; ++j) {
                acc += a[j] * r[i - j];
            }
            const Sample k = -acc / err;
            if (!std::isfinite(static_cast<double>(k)) || std::abs(static_cast<double>(k)) >= 1.0) {
                return i - 1;
            }
            for (size_t j = 1; j < i; ++j) {
                scratch[j] = a[j] + k * a[i - j];
            }
            for (size_t j = 1; j < i; ++j) {
                a[j] = scratch[j];
            }
            a[i] = k;
            err *= Sample(1) - k * k;
            if (!(err > Sample(0))) {
                return i;
            }
        }
        return order;
    }

    /// Short-term all-pole near-end model: per-frame LP of the analysis
    /// window; apply() runs the prediction-error (whitening) filter A(q).
    /// This alone whitens spectral envelopes and line spectra (tones, the
    /// formant coloring of speech); the pitch stage below adds long-term
    /// (voiced-periodicity) prediction.
    template <typename Sample>
    class lpc_predictor {
      public:
        struct config {
            size_t order = 16;           ///< LP order
            Sample ridge = Sample(1e-4); ///< relative noise floor on r[0]
        };

        struct state {
            std::vector<Sample> hist; ///< last `order` inputs
        };

        explicit lpc_predictor(const config& cfg)
            : m_cfg(cfg)
            , m_a(cfg.order + 1)
            , m_r(cfg.order + 1)
            , m_scratch(cfg.order + 1) {
            if (cfg.order == 0) {
                throw std::invalid_argument("lpc_predictor: order must be >= 1");
            }
            if (!(cfg.ridge > Sample(0))) {
                throw std::invalid_argument("lpc_predictor: ridge must be > 0");
            }
            m_a[0] = Sample(1);
        }

        state make_state() const { return state{std::vector<Sample>(m_cfg.order, Sample(0))}; }

        void reset_state(state& s) const noexcept {
            for (auto& x : s.hist) {
                x = Sample(0);
            }
        }

        size_t order() const noexcept { return m_cfg.order; }

        /// Prediction-error filter coefficients, a[0] = 1, length order()+1.
        const std::vector<Sample>& coefficients() const noexcept { return m_a; }

        void analyze(const Sample* window, size_t n) noexcept {
            autocorrelation(window, n, m_r.data(), m_cfg.order);
            const size_t achieved = levinson(m_r.data(), m_cfg.order, m_a.data(), m_scratch.data(), m_cfg.ridge);
            for (size_t j = achieved + 1; j <= m_cfg.order; ++j) {
                m_a[j] = Sample(0);
            }
        }

        /// out[i] = in[i] + sum_j a[j] in[i-j], streaming across blocks.
        void apply(state& s, const Sample* in, Sample* out, size_t n) const noexcept {
            const size_t m = m_cfg.order;
            for (size_t i = 0; i < n; ++i) {
                Sample acc = in[i];
                for (size_t j = 1; j <= m; ++j) {
                    const Sample past = (i >= j) ? in[i - j] : s.hist[m - (j - i)];
                    acc += m_a[j] * past;
                }
                out[i] = acc;
            }
            // Refill history with the last m inputs (oldest first).
            for (size_t j = 0; j < m; ++j) {
                const size_t back = m - j; // input index n - back
                s.hist[j]         = (n >= back) ? in[n - back] : s.hist[j + n];
            }
        }

      private:
        config              m_cfg;
        std::vector<Sample> m_a;
        std::vector<Sample> m_r;
        std::vector<Sample> m_scratch;
    };

    /// Frequency-warped all-pole near-end model — the music/tonal predictor
    /// (HANDOFF.md near-end-model decision). Plain LP spends its resolution
    /// uniformly over frequency; musical program material concentrates its
    /// partials low, where a moderate plain order cannot resolve them (poles
    /// crowd z = 1, exactly where the ridge-guarded analysis is stiffest).
    /// Warped LP replaces every unit delay in the predictor with a
    /// first-order allpass D(z) = (-lambda + z^-1)/(1 - lambda z^-1), which
    /// warps the frequency axis (lambda ~ 0.766 approximates the Bark scale
    /// at 48 kHz) so the same order buys far more resolution in the bass.
    ///
    /// analyze() computes the WARPED autocorrelation r[k] = <x, D^k x>
    /// (Haermae et al.) and runs the same ridge-guarded Levinson-Durbin;
    /// apply() runs the prediction-error filter as a tapped allpass chain —
    /// feed-forward, so it is unconditionally stable, like the plain FIR
    /// whitener. At lambda = 0 both collapse exactly to lpc_predictor.
    ///
    /// PAIR IT WITH IPC-SCALED STEPPING (fdaf.ipc_step_scaling = true).
    /// The warped whitener notches the near-end partials so hard that on
    /// some rooms the converged closed-loop update runs away — it builds a
    /// spurious resonance in the partial band that live adaptation
    /// re-inforces instead of correcting (measured: howls 15 dB BELOW the
    /// open-loop MSG on ~1 room in 5, at every (lambda, order) tried).
    /// The IPC step scale suppresses exactly that update — error power no
    /// longer coherent with the input — and with it the warped canceller
    /// measured ASG +7.2..+11.3 dB on low-chord music across eleven random
    /// rooms from two generator families, with no collapse anywhere
    /// (tests/test_pem_afc.cpp).
    ///
    /// Defaults (lambda 0.5, order 16) are the best worst-case of that
    /// sweep; the Bark-scale lambda 0.766/order 24 buys a slightly better
    /// mean (+10.2 vs +9.6 dB) at a weaker floor (+5.9 vs +7.2 dB) and
    /// half again the per-sample cost.
    template <typename Sample>
    class warped_lpc_predictor {
      public:
        struct config {
            size_t order = 16;           ///< warped LP order
            Sample ridge = Sample(1e-4); ///< relative noise floor on r[0]
            /// Warping coefficient in (-1, 1); 0 = plain LP. 0.766 is the
            /// Bark-scale fit at 48 kHz (Smith & Abel 1999); the default is
            /// the room-robust 0.5 (see the class comment).
            Sample lambda            = Sample(0.5);
            size_t analysis_capacity = 1024; ///< max analyze() window length
        };

        struct state {
            std::vector<Sample> allpass; ///< one state per section
        };

        explicit warped_lpc_predictor(const config& cfg)
            : m_cfg(cfg)
            , m_a(cfg.order + 1)
            , m_r(cfg.order + 1)
            , m_scratch(cfg.order + 1)
            , m_d_prev(cfg.analysis_capacity)
            , m_d_cur(cfg.analysis_capacity) {
            if (cfg.order == 0) {
                throw std::invalid_argument("warped_lpc_predictor: order must be >= 1");
            }
            if (!(cfg.ridge > Sample(0))) {
                throw std::invalid_argument("warped_lpc_predictor: ridge must be > 0");
            }
            if (!(cfg.lambda > Sample(-1)) || !(cfg.lambda < Sample(1))) {
                throw std::invalid_argument("warped_lpc_predictor: lambda must be in (-1, 1)");
            }
            if (cfg.analysis_capacity == 0) {
                throw std::invalid_argument("warped_lpc_predictor: analysis_capacity must be >= 1");
            }
            m_a[0] = Sample(1);
        }

        state make_state() const { return state{std::vector<Sample>(m_cfg.order, Sample(0))}; }

        void reset_state(state& s) const noexcept {
            for (auto& x : s.allpass) {
                x = Sample(0);
            }
        }

        size_t order() const noexcept { return m_cfg.order; }
        Sample lambda() const noexcept { return m_cfg.lambda; }

        /// Prediction-error coefficients over warped delays, a[0] = 1.
        const std::vector<Sample>& coefficients() const noexcept { return m_a; }

        void analyze(const Sample* window, size_t n) noexcept {
            if (n > m_d_prev.size()) {
                n = m_d_prev.size();
            }
            // Warped autocorrelation: r[k] = <window, D^k window>, each
            // allpass pass one-shot over the frame with zero initial state.
            for (size_t i = 0; i < n; ++i) {
                m_d_prev[i] = window[i];
            }
            Sample r0 = Sample(0);
            for (size_t i = 0; i < n; ++i) {
                r0 += window[i] * window[i];
            }
            m_r[0]         = r0;
            const Sample l = m_cfg.lambda;
            for (size_t k = 1; k <= m_cfg.order; ++k) {
                Sample s   = Sample(0);
                Sample acc = Sample(0);
                for (size_t i = 0; i < n; ++i) {
                    const Sample x = m_d_prev[i];
                    const Sample y = -l * x + s;
                    s              = x + l * y;
                    m_d_cur[i]     = y;
                    acc += window[i] * y;
                }
                m_r[k] = acc;
                m_d_prev.swap(m_d_cur);
            }
            const size_t achieved = levinson(m_r.data(), m_cfg.order, m_a.data(), m_scratch.data(), m_cfg.ridge);
            for (size_t j = achieved + 1; j <= m_cfg.order; ++j) {
                m_a[j] = Sample(0);
            }
        }

        /// out[i] = in[i] + sum_j a[j] d_j(i), the tapped allpass chain,
        /// streaming across blocks (one allpass state per section).
        void apply(state& st, const Sample* in, Sample* out, size_t n) const noexcept {
            const size_t m = m_cfg.order;
            const Sample l = m_cfg.lambda;
            for (size_t i = 0; i < n; ++i) {
                Sample prev = in[i];
                Sample acc  = in[i];
                for (size_t j = 0; j < m; ++j) {
                    const Sample y = -l * prev + st.allpass[j];
                    st.allpass[j]  = prev + l * y;
                    acc += m_a[j + 1] * y;
                    prev = y;
                }
                out[i] = acc;
            }
        }

      private:
        config              m_cfg;
        std::vector<Sample> m_a;
        std::vector<Sample> m_r;
        std::vector<Sample> m_scratch;
        std::vector<Sample> m_d_prev;
        std::vector<Sample> m_d_cur;
    };

    /// The speech near-end model from the PEM-AFROW papers: a cascade of the
    /// short-term LP above (formant/envelope coloring) and a one-tap
    /// long-term predictor 1 - beta q^-T (voiced-pitch periodicity). The
    /// pitch lag T maximizes the normalized autocorrelation of the
    /// short-term residual over [min_lag, max_lag]; the stage disables
    /// itself (beta = 0) when the residual shows no periodicity stronger
    /// than `voicing_threshold`.
    template <typename Sample>
    class speech_predictor {
      public:
        struct config {
            typename lpc_predictor<Sample>::config short_term;
            size_t                                 min_lag           = 32;          ///< pitch search range, samples
            size_t                                 max_lag           = 400;         ///< must fit the analysis window
            size_t                                 analysis_capacity = 1024;        ///< max analyze() window length
            Sample                                 max_gain          = Sample(0.9); ///< clamp on beta
            Sample voicing_threshold = Sample(0.3); ///< normalized correlation to enable the tap
        };

        struct state {
            typename lpc_predictor<Sample>::state short_term;
            std::vector<Sample>                   ring; ///< last max_lag short-term residuals
            size_t                                pos = 0;
        };

        explicit speech_predictor(const config& cfg)
            : m_cfg(cfg)
            , m_short(cfg.short_term)
            , m_residual(cfg.analysis_capacity) {
            if (cfg.min_lag < 1 || cfg.min_lag >= cfg.max_lag) {
                throw std::invalid_argument("speech_predictor: need 1 <= min_lag < max_lag");
            }
            if (cfg.analysis_capacity < 2 * cfg.max_lag) {
                throw std::invalid_argument("speech_predictor: analysis_capacity must be >= 2 * max_lag");
            }
            if (!(cfg.max_gain > Sample(0)) || !(cfg.max_gain < Sample(1))) {
                throw std::invalid_argument("speech_predictor: max_gain must be in (0, 1)");
            }
        }

        state make_state() const {
            return state{m_short.make_state(), std::vector<Sample>(m_cfg.max_lag, Sample(0)), 0};
        }

        void reset_state(state& s) const noexcept {
            m_short.reset_state(s.short_term);
            for (auto& x : s.ring) {
                x = Sample(0);
            }
            s.pos = 0;
        }

        size_t                       pitch_lag() const noexcept { return m_lag; }
        Sample                       pitch_gain() const noexcept { return m_beta; }
        const lpc_predictor<Sample>& short_term() const noexcept { return m_short; }

        void analyze(const Sample* window, size_t n) noexcept {
            if (n > m_residual.size()) {
                n = m_residual.size();
            }
            m_short.analyze(window, n);

            // Short-term residual of the window (zero initial state), then
            // the best long-term lag by normalized correlation.
            const auto&  a = m_short.coefficients();
            const size_t m = m_short.order();
            for (size_t i = 0; i < n; ++i) {
                Sample acc = window[i];
                for (size_t j = 1; j <= m && j <= i; ++j) {
                    acc += a[j] * window[i - j];
                }
                m_residual[i] = acc;
            }

            m_lag  = 0;
            m_beta = Sample(0);
            if (n < 2 * m_cfg.min_lag) {
                return;
            }
            const size_t max_lag = (m_cfg.max_lag < n / 2) ? m_cfg.max_lag : n / 2;
            double       best    = 0.0;
            for (size_t lag = m_cfg.min_lag; lag <= max_lag; ++lag) {
                double cross = 0.0;
                double e_now = 0.0;
                double e_lag = 0.0;
                for (size_t i = lag; i < n; ++i) {
                    const double x0 = static_cast<double>(m_residual[i]);
                    const double x1 = static_cast<double>(m_residual[i - lag]);
                    cross += x0 * x1;
                    e_now += x0 * x0;
                    e_lag += x1 * x1;
                }
                if (e_now <= 0.0 || e_lag <= 0.0) {
                    continue;
                }
                const double rho = cross / std::sqrt(e_now * e_lag);
                if (rho > best) {
                    best = rho;
                    if (rho > static_cast<double>(m_cfg.voicing_threshold)) {
                        const double beta = cross / e_lag;
                        m_lag             = lag;
                        m_beta            = static_cast<Sample>(std::clamp(beta, -static_cast<double>(m_cfg.max_gain),
                                                                           static_cast<double>(m_cfg.max_gain)));
                    }
                }
            }
        }

        /// Cascade: short-term whitening, then out[i] = st[i] - beta st[i-T].
        void apply(state& s, const Sample* in, Sample* out, size_t n) const noexcept {
            m_short.apply(s.short_term, in, out, n); // out = short-term residual
            const size_t t = m_lag;
            for (size_t i = 0; i < n; ++i) {
                const Sample st = out[i];
                if (t != 0) {
                    const size_t idx = (s.pos + m_cfg.max_lag - t) % m_cfg.max_lag;
                    out[i]           = st - m_beta * s.ring[idx];
                }
                s.ring[s.pos] = st;
                s.pos         = (s.pos + 1) % m_cfg.max_lag;
            }
        }

      private:
        config                m_cfg;
        lpc_predictor<Sample> m_short;
        std::vector<Sample>   m_residual;
        size_t                m_lag  = 0;
        Sample                m_beta = Sample(0);
    };

} // namespace mutap
