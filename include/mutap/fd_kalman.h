/// @file fd_kalman.h
/// @brief Partitioned-block frequency-domain Kalman filter (diagonalized
///        PBFDKF) — the v2 adaptive core for the PEM feedback canceller.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "mutap/fdaf.h"
#include "mutap/fft.h"

namespace mutap {

    /// Partitioned-block frequency-domain Kalman filter (after Enzner & Vary
    /// 2006; Kuech, Mabande & Enzner 2014; PEM-wrapped for feedback
    /// cancellation by Bernardi, van Waterschoot, Wouters & Moonen). Same
    /// overlap-save partitioned structure and drop-in interface as
    /// partitioned_fdaf, but the NLMS update is replaced by a per-bin,
    /// per-partition scalar Kalman filter under the usual diagonal
    /// approximation:
    ///
    ///   state:        W_p(k) <- A * W_p(k) + dW,  process noise
    ///                 (1 - A^2)|W_p(k)|^2  (echo-path variability
    ///                 proportional to the path itself)
    ///   observation:  E(k) = D(k) - sum_p U_p(k) W_p(k) + S(k),
    ///                 near-end PSD Psi_s(k) estimated from the residual
    ///   update:       P_p <- A^2 P_p + (1-A^2)|W_p|^2            (predict)
    ///                 denom = sum_q |U_q|^2 P_q + Psi_s + eps
    ///                 W_p += (P_p / denom) conj(U_p) E
    ///                 P_p *= 1 - c |U_p|^2 P_p / denom   (c = B/N = 1/2,
    ///                 the gradient constraint's information loss)
    ///
    /// Why this replaces the M4 control stack instead of extending it: the
    /// per-bin state uncertainty P and near-end PSD Psi_s are exactly the
    /// quantities the NLMS core approximates with a fixed step size,
    /// IPC-scaled stepping and variable regularization. A near-end burst
    /// raises Psi_s and the gain collapses (automatic double-talk
    /// robustness); a path change leaves P alive through the process noise
    /// and the filter re-converges at speed (tracking); a converged quiet
    /// filter has small P and takes small steps (low misadjustment) — all
    /// without a tuned mu.
    ///
    /// Real-time contract: the constructor allocates and may throw
    /// (std::invalid_argument on a bad config); process_block() and every
    /// other post-construction entry point are noexcept and allocation-free.
    template <typename Sample>
    class partitioned_fdkf {
      public:
        struct config {
            size_t block_size = 256; ///< B: samples per block/hop; power of 2, >= 4
            size_t partitions = 4;   ///< P: filter length = P * B taps
            /// State transition A in (0, 1]: models how fast the true path
            /// drifts. The process noise (1 - A^2)|W|^2 keeps the state
            /// uncertainty alive after convergence, which is what buys
            /// re-convergence; A = 1 freezes a converged filter forever.
            Sample transition = Sample(0.9995);
            /// One-pole smoothing of the near-end (observation-noise) PSD
            /// estimated from the residual spectrum, in [0, 1). The residual
            /// contains unmodeled echo too, so this over-estimates during
            /// convergence — a conservative gain, never an unstable one.
            Sample noise_smoothing = Sample(0.9);
            /// Initial per-bin state uncertainty P(0). Order-of-magnitude
            /// only: the first blocks' gains are NLMS-like regardless
            /// because P >> Psi_s until the filter starts explaining echo.
            Sample initial_uncertainty = Sample(1);
            Sample regularization      = Sample(1e-12); ///< denominator guard (silence/denormals)
            /// Transient floor (0 = off, the default): a bin whose
            /// instantaneous error power exceeds this multiple of the
            /// smoothed near-end PSD is an outlier the smoother has not
            /// seen yet (a burst); that bin's innovation power uses the
            /// instantaneous value, so the gain collapses on exactly the
            /// blocks that would otherwise kick the filter (measured: a
            /// +20 dB near-end burst contained to block RMS ~10..25 at
            /// ratio 8..16, vs ~17600 with the floor off). The cost is
            /// real and measured too: the floor cannot tell a burst from
            /// the loop ringing up, and ASG on tonal material drops
            /// ~2..6 dB — live re-convergence under ring-up is exactly
            /// what buys added stable gain, so the default optimizes ASG
            /// and leaves burst hardening opt-in for fixed-gain
            /// deployments.
            Sample transient_floor_ratio = Sample(0);
            /// UNCERTAINTY RE-INFLATION on sustained innovation excess
            /// (0 = off, the default). The converged filter's failure
            /// mode after an ABRUPT path change (measured, Stage 6): the
            /// state uncertainty P is orders of magnitude below P(0), and
            /// within ~100 ms the noise tracker absorbs the unmodeled-echo
            /// residual into Psi_s — the filter books its own error as
            /// near-end noise and locks itself at a gain ~20x too small
            /// (deep re-convergence measured ~7 s where a cold start takes
            /// 1.4 s). The fix tracks, per bin and partition, a smoothed
            /// MOMENTUM of the normalized raw update direction,
            /// mu = a mu + (1-a) conj(U_p) E / S_uu: zero-mean under
            /// genuine near-end innovation, but converging to the actual
            /// weight error dW under an unmodeled path change. When |mu|^2
            /// exceeds `reinflation_excess` times its own statistical
            /// noise floor ((1-a)/(1+a) Psi_s/S_uu), P is lifted to
            /// |mu|^2 — the implied missing uncertainty itself. Double
            /// talk stays safe because the floor scales with Psi_s
            /// (measured: 0 dB double-talk divergence keeps > 20 dB of
            /// bound margin, convergence-in-noise and cold-start rows
            /// bit-identical; the swap's deep descent moves from ~7 s to
            /// ~3 s at excess 2). Gating the Psi_s update on the same
            /// trigger was REJECTED: it diverges outright in sustained
            /// background noise (the starved estimate lets the filter
            /// chase the noise; measured +60 dBm0 blowup).
            Sample reinflation_excess = Sample(0);
            /// Momentum smoothing a in [0, 1) (~100 ms at block 256 /
            /// 48 kHz; the compliance preset rescales it with block
            /// duration like every other smoothing).
            Sample reinflation_smoothing = Sample(0.95);
            /// ECHO-DOMINANCE GATE on the re-inflation trigger: lifts
            /// apply only on blocks where the implied weight error
            /// explains at least this fraction of the total residual
            /// power, R = sum_k S_uu sum_p |mu|^2 / sum_k S_ee. A real
            /// path change drives R toward 1 (the residual IS the
            /// unmodeled echo); the detector's false positives cannot:
            /// two FM comb lines sharing an analysis bin (P.501's AM-FM
            /// double-talk plans) beat at their difference frequency and
            /// masquerade as sustained drift per bin, but the loud near
            /// end that carries them dominates the residual, so R stays
            /// low. Without this gate the AM-FM double-talk and
            /// noise-pumping rows measurably DIVERGED (send attenuation
            /// past -400 dB) — a per-bin breadth gate was tried first
            /// and failed, because at 48 kHz bin resolution the comb
            /// misfires are themselves broadband. Note the statistic's
            /// own noise floor is ~partitions x (1-a)/(1+a) (~0.2 at 8
            /// partitions, a = 0.95); the threshold must sit above it.
            Sample reinflation_dominance = Sample(0.5);
            bool   constrained           = true; ///< gradient constraint, as in partitioned_fdaf
        };

        explicit partitioned_fdkf(const config& cfg)
            : m_cfg(validated(cfg))
            , m_n(2 * cfg.block_size)
            , m_fft(m_n)
            , m_input(m_n)
            , m_u(cfg.partitions * m_n)
            , m_h(cfg.partitions * m_n)
            , m_accum(m_n)
            , m_espec(m_n)
            , m_time(m_n)
            , m_p(cfg.partitions * (cfg.block_size + 1))
            , m_psi_s(cfg.block_size + 1)
            , m_denom(cfg.block_size + 1)
            // Re-inflation state only when the feature is on: the momentum
            // array doubles the filter-sized state, and embedded deployments
            // run with the feature off.
            , m_mu(cfg.reinflation_excess > Sample(0) ? cfg.partitions * m_n : 0)
            , m_suu(cfg.reinflation_excess > Sample(0) ? cfg.block_size + 1 : 0)
            , m_see(cfg.reinflation_excess > Sample(0) ? cfg.block_size + 1 : 0) {
            reset();
        }

        size_t block_size() const noexcept { return m_cfg.block_size; }
        size_t partitions() const noexcept { return m_cfg.partitions; }
        size_t filter_length() const noexcept { return m_cfg.block_size * m_cfg.partitions; }
        size_t fft_size() const noexcept { return m_n; }

        /// Read access to partition p's filter spectrum (packed layout) —
        /// the surface that lets pem_afc cancel on the raw signal stream.
        const Sample* partition_spectrum(size_t p) const noexcept { return &m_h[p * m_n]; }

        bool adapting() const noexcept { return m_adapt; }
        void set_adaptation(bool enabled) noexcept { m_adapt = enabled; }

        /// Zero the filter and histories, restore the initial uncertainty.
        void reset() noexcept {
            fill_zero(m_input);
            fill_zero(m_u);
            fill_zero(m_h);
            fill_zero(m_psi_s);
            fill_zero(m_mu);
            fill_zero(m_suu);
            fill_zero(m_see);
            for (auto& x : m_p) {
                x = m_cfg.initial_uncertainty;
            }
            m_head = m_cfg.partitions - 1;
        }

        /// Process exactly block_size() samples: filter the reference input,
        /// write error = desired - estimate, and (unless adaptation is
        /// frozen) run the Kalman update. Optionally also writes the filter
        /// output itself through `estimate`.
        void process_block(const Sample* input, const Sample* desired, Sample* error,
                           Sample* estimate = nullptr) noexcept {
            const size_t b    = m_cfg.block_size;
            const size_t p_n  = m_cfg.partitions;
            const size_t half = b; // bin count is half + 1

            // Overlap-save input window and newest block spectrum, exactly
            // as in partitioned_fdaf.
            for (size_t i = 0; i < b; ++i) {
                m_input[i]     = m_input[i + b];
                m_input[i + b] = input[i];
            }
            m_head        = (m_head + 1) % p_n;
            Sample* u_new = &m_u[m_head * m_n];
            for (size_t i = 0; i < m_n; ++i) {
                u_new[i] = m_input[i];
            }
            m_fft.forward_inplace(u_new);

            fill_zero(m_accum);
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u = &m_u[((m_head + p_n - p) % p_n) * m_n];
                detail::packed_mac(u, &m_h[p * m_n], m_accum.data(), m_n);
            }
            m_fft.inverse(m_accum.data(), m_time.data());
            for (size_t i = 0; i < b; ++i) {
                const Sample y = m_time[i + b];
                error[i]       = desired[i] - y;
                if (estimate != nullptr) {
                    estimate[i] = y;
                }
            }

            if (!m_adapt) {
                return;
            }

            // Error spectrum of the zero-padded block [0 | e].
            for (size_t i = 0; i < b; ++i) {
                m_espec[i]     = Sample(0);
                m_espec[i + b] = error[i];
            }
            m_fft.forward_inplace(m_espec.data());

            const Sample a2     = m_cfg.transition * m_cfg.transition;
            const Sample q_gain = Sample(1) - a2;

            // Predict: age the state covariance with path-proportional
            // process noise, then form the per-bin innovation power
            // denom = sum_p |U_p|^2 P_p + Psi_s + eps.
            m_denom[0]    = inst_floor(m_psi_s[0], m_espec[0] * m_espec[0]);
            m_denom[half] = inst_floor(m_psi_s[half], m_espec[1] * m_espec[1]);
            for (size_t k = 1; k < half; ++k) {
                const Sample er = m_espec[2 * k];
                const Sample ei = m_espec[2 * k + 1];
                m_denom[k]      = inst_floor(m_psi_s[k], er * er + ei * ei);
            }
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u  = &m_u[((m_head + p_n - p) % p_n) * m_n];
                const Sample* h  = &m_h[p * m_n];
                Sample*       pp = &m_p[p * (half + 1)];

                pp[0]    = a2 * pp[0] + q_gain * h[0] * h[0];
                pp[half] = a2 * pp[half] + q_gain * h[1] * h[1];
                m_denom[0] += u[0] * u[0] * pp[0];
                m_denom[half] += u[1] * u[1] * pp[half];
                for (size_t k = 1; k < half; ++k) {
                    const Sample hr = h[2 * k];
                    const Sample hi = h[2 * k + 1];
                    pp[k]           = a2 * pp[k] + q_gain * (hr * hr + hi * hi);
                    const Sample ur = u[2 * k];
                    const Sample ui = u[2 * k + 1];
                    m_denom[k] += (ur * ur + ui * ui) * pp[k];
                }
            }

            // Correct: per bin and partition, gain g = P_p / denom;
            // W_p += g conj(U_p) E, P_p *= 1 - (1/2) g |U_p|^2.
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u  = &m_u[((m_head + p_n - p) % p_n) * m_n];
                Sample*       h  = &m_h[p * m_n];
                Sample*       pp = &m_p[p * (half + 1)];

                {
                    const Sample g = pp[0] / m_denom[0];
                    h[0] += g * u[0] * m_espec[0];
                    pp[0] *= Sample(1) - Sample(0.5) * g * u[0] * u[0];
                }
                {
                    const Sample g = pp[half] / m_denom[half];
                    h[1] += g * u[1] * m_espec[1];
                    pp[half] *= Sample(1) - Sample(0.5) * g * u[1] * u[1];
                }
                for (size_t k = 1; k < half; ++k) {
                    const Sample g  = pp[k] / m_denom[k];
                    const Sample ur = u[2 * k];
                    const Sample ui = u[2 * k + 1];
                    const Sample er = m_espec[2 * k];
                    const Sample ei = m_espec[2 * k + 1];
                    h[2 * k] += g * (ur * er + ui * ei);
                    h[2 * k + 1] += g * (ur * ei - ui * er);
                    pp[k] *= Sample(1) - Sample(0.5) * g * (ur * ur + ui * ui);
                }
                if (m_cfg.constrained) {
                    m_fft.inverse(h, m_time.data());
                    for (size_t i = 0; i < b; ++i) {
                        m_time[i + b] = Sample(0);
                    }
                    m_fft.forward(m_time.data(), h);
                }
            }

            // Uncertainty re-inflation (see the config comment): track the
            // momentum of the normalized raw update direction per bin and
            // partition; lift P to the implied missing uncertainty where
            // the momentum clears its noise floor — but only on blocks
            // where the implied weight error dominates the residual (the
            // echo-dominance gate). Runs after the correct step so the
            // lift takes effect in the NEXT block's gain.
            if (m_cfg.reinflation_excess > Sample(0)) {
                const Sample  al  = m_cfg.reinflation_smoothing;
                const Sample  oma = Sample(1) - al;
                const Sample  nf  = m_cfg.reinflation_excess * oma / (Sample(1) + al);
                const Sample  bu  = m_cfg.noise_smoothing;
                const Sample* u0  = &m_u[m_head * m_n];
                m_suu[0]          = bu * m_suu[0] + (Sample(1) - bu) * u0[0] * u0[0];
                m_suu[half]       = bu * m_suu[half] + (Sample(1) - bu) * u0[1] * u0[1];
                m_see[0]          = bu * m_see[0] + (Sample(1) - bu) * m_espec[0] * m_espec[0];
                m_see[half]       = bu * m_see[half] + (Sample(1) - bu) * m_espec[1] * m_espec[1];
                for (size_t k = 1; k < half; ++k) {
                    m_suu[k] =
                        bu * m_suu[k] + (Sample(1) - bu) * (u0[2 * k] * u0[2 * k] + u0[2 * k + 1] * u0[2 * k + 1]);
                    m_see[k] = bu * m_see[k]
                               + (Sample(1) - bu)
                                     * (m_espec[2 * k] * m_espec[2 * k] + m_espec[2 * k + 1] * m_espec[2 * k + 1]);
                }
                // Pass 1: update the momenta; accumulate the explained-power
                // numerator sum_k S_uu sum_p |mu|^2 and residual denominator.
                Sample explained = Sample(0);
                for (size_t p = 0; p < p_n; ++p) {
                    const Sample* u  = &m_u[((m_head + p_n - p) % p_n) * m_n];
                    Sample*       mu = &m_mu[p * m_n];
                    {
                        const Sample su = m_suu[0] + m_cfg.regularization;
                        mu[0]           = al * mu[0] + oma * u[0] * m_espec[0] / su;
                        explained += m_suu[0] * mu[0] * mu[0];
                    }
                    {
                        const Sample su = m_suu[half] + m_cfg.regularization;
                        mu[1]           = al * mu[1] + oma * u[1] * m_espec[1] / su;
                        explained += m_suu[half] * mu[1] * mu[1];
                    }
                    for (size_t k = 1; k < half; ++k) {
                        const Sample su = m_suu[k] + m_cfg.regularization;
                        const Sample ur = u[2 * k];
                        const Sample ui = u[2 * k + 1];
                        const Sample er = m_espec[2 * k];
                        const Sample ei = m_espec[2 * k + 1];
                        mu[2 * k]       = al * mu[2 * k] + oma * (ur * er + ui * ei) / su;
                        mu[2 * k + 1]   = al * mu[2 * k + 1] + oma * (ur * ei - ui * er) / su;
                        explained += m_suu[k] * (mu[2 * k] * mu[2 * k] + mu[2 * k + 1] * mu[2 * k + 1]);
                    }
                }
                Sample resid = Sample(0);
                for (size_t k = 0; k <= half; ++k) {
                    resid += m_see[k];
                }
                // Pass 2: dominance-gated lifts on bins whose momentum
                // clears the per-bin noise floor.
                if (explained >= m_cfg.reinflation_dominance * resid + m_cfg.regularization) {
                    for (size_t p = 0; p < p_n; ++p) {
                        const Sample* mu = &m_mu[p * m_n];
                        Sample*       pp = &m_p[p * (half + 1)];
                        {
                            const Sample m2 = mu[0] * mu[0];
                            const Sample su = m_suu[0] + m_cfg.regularization;
                            if (m2 > nf * m_psi_s[0] / su && m2 > pp[0]) {
                                pp[0] = m2;
                            }
                        }
                        {
                            const Sample m2 = mu[1] * mu[1];
                            const Sample su = m_suu[half] + m_cfg.regularization;
                            if (m2 > nf * m_psi_s[half] / su && m2 > pp[half]) {
                                pp[half] = m2;
                            }
                        }
                        for (size_t k = 1; k < half; ++k) {
                            const Sample m2 = mu[2 * k] * mu[2 * k] + mu[2 * k + 1] * mu[2 * k + 1];
                            const Sample su = m_suu[k] + m_cfg.regularization;
                            if (m2 > nf * m_psi_s[k] / su && m2 > pp[k]) {
                                pp[k] = m2;
                            }
                        }
                    }
                }
            }

            // Track the near-end PSD from the residual (post-update: the
            // denominator above used the estimate from before this block).
            const Sample beta     = m_cfg.noise_smoothing;
            const Sample one_beta = Sample(1) - beta;
            m_psi_s[0]            = beta * m_psi_s[0] + one_beta * m_espec[0] * m_espec[0];
            m_psi_s[half]         = beta * m_psi_s[half] + one_beta * m_espec[1] * m_espec[1];
            for (size_t k = 1; k < half; ++k) {
                const Sample er = m_espec[2 * k];
                const Sample ei = m_espec[2 * k + 1];
                m_psi_s[k]      = beta * m_psi_s[k] + one_beta * (er * er + ei * ei);
            }
        }

        /// Copy the current filter estimate as a time-domain impulse response
        /// of filter_length() taps. Allocation-free (uses internal scratch).
        void copy_impulse_response(Sample* dest) noexcept {
            const size_t b = m_cfg.block_size;
            for (size_t p = 0; p < m_cfg.partitions; ++p) {
                m_fft.inverse(&m_h[p * m_n], m_time.data());
                for (size_t i = 0; i < b; ++i) {
                    dest[p * b + i] = m_time[i];
                }
            }
        }

      private:
        Sample inst_floor(Sample psi, Sample inst_e2) const noexcept {
            const Sample psi_eff =
                (inst_e2 > m_cfg.transient_floor_ratio * psi && m_cfg.transient_floor_ratio > Sample(0)) ? inst_e2
                                                                                                         : psi;
            return psi_eff + m_cfg.regularization;
        }

        static config validated(const config& cfg) {
            if (cfg.block_size < 4 || (cfg.block_size & (cfg.block_size - 1)) != 0) {
                throw std::invalid_argument("partitioned_fdkf: block_size must be a power of 2, >= 4");
            }
            if (cfg.partitions == 0) {
                throw std::invalid_argument("partitioned_fdkf: partitions must be >= 1");
            }
            if (!(cfg.transition > Sample(0)) || !(cfg.transition <= Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: transition must be in (0, 1]");
            }
            if (!(cfg.noise_smoothing >= Sample(0)) || !(cfg.noise_smoothing < Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: noise_smoothing must be in [0, 1)");
            }
            if (!(cfg.initial_uncertainty > Sample(0))) {
                throw std::invalid_argument("partitioned_fdkf: initial_uncertainty must be > 0");
            }
            if (!(cfg.regularization > Sample(0))) {
                throw std::invalid_argument("partitioned_fdkf: regularization must be > 0");
            }
            if (!(cfg.transient_floor_ratio >= Sample(0))) {
                throw std::invalid_argument("partitioned_fdkf: transient_floor_ratio must be >= 0");
            }
            if (!(cfg.reinflation_excess >= Sample(0))) {
                throw std::invalid_argument("partitioned_fdkf: reinflation_excess must be >= 0");
            }
            if (!(cfg.reinflation_smoothing >= Sample(0)) || !(cfg.reinflation_smoothing < Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: reinflation_smoothing must be in [0, 1)");
            }
            if (!(cfg.reinflation_dominance >= Sample(0))) {
                throw std::invalid_argument("partitioned_fdkf: reinflation_dominance must be >= 0");
            }
            return cfg;
        }

        static void fill_zero(std::vector<Sample>& v) noexcept {
            for (auto& x : v) {
                x = Sample(0);
            }
        }

        config                 m_cfg;
        size_t                 m_n; ///< FFT size N = 2 * block_size
        basic_real_fft<Sample> m_fft;
        std::vector<Sample>    m_input; ///< sliding 2B-sample input window
        std::vector<Sample>    m_u;     ///< P input-block spectra (ring, newest at m_head)
        std::vector<Sample>    m_h;     ///< P filter-partition spectra (the state mean)
        std::vector<Sample>    m_accum; ///< output-spectrum accumulator
        std::vector<Sample>    m_espec; ///< error-block spectrum
        std::vector<Sample>    m_time;  ///< time-domain scratch
        std::vector<Sample>    m_p;     ///< per-partition, per-bin state variance
        std::vector<Sample>    m_psi_s; ///< per-bin near-end PSD estimate
        std::vector<Sample>    m_denom; ///< per-bin innovation power, this block
        std::vector<Sample>    m_mu;    ///< re-inflation momentum (packed; empty when off)
        std::vector<Sample>    m_suu;   ///< smoothed input PSD per bin (empty when off)
        std::vector<Sample>    m_see;   ///< smoothed residual PSD per bin (empty when off)
        size_t                 m_head  = 0;
        bool                   m_adapt = true;
    };

} // namespace mutap
