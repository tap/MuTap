/// @file fd_kalman.h
/// @brief Partitioned-block frequency-domain Kalman filter (diagonalized
///        PBFDKF) — the v2 adaptive core for the PEM feedback canceller.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
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
            /// Excitation-novelty covariance discount (0 = off, the
            /// default): one-pole smoothing, in [0, 1), of a per-bin
            /// magnitude-coherence between successive input block
            /// spectra; the per-bin P decrement is scaled by
            /// (1 - coherence), floored at novelty_floor. Rationale
            /// (measured, the block-128 notch): the P decrement
            /// 1 - c g|U|^2 is innovation-independent — correct for the
            /// joint Kalman, but under the diagonal approximation a
            /// PERIODIC input (P.501's CSS voiced segment is a 329 Hz
            /// comb) repeats the same regressor block after block and
            /// each repeat claims a fresh information gain it does not
            /// deliver. Where the analysis window resolves the comb
            /// (16 ms window = 8 ms hop), P burns to the floor during
            /// the voiced segment, the noise tracker then absorbs the
            /// still-unmodeled echo, and initial convergence self-locks
            /// exactly like the post-swap failure reinflate_uncertainty
            /// rescues (ERL 8.9 dB by 600 ms at block 128 / 16 kHz vs
            /// 22.5 at block 256; the notch follows the 8 ms hop to
            /// block 256 at 32 kHz). White/PN-like input decorrelates
            /// block to block (coherence ~0) and keeps the exact
            /// stock decrement. Requires partitions >= 2 (the previous
            /// block's spectrum is read from the partition ring);
            /// silently inactive at partitions == 1.
            Sample novelty_smoothing = Sample(0);
            Sample novelty_floor     = Sample(0); ///< lower bound of the discount scale
            /// Per-partition initial-uncertainty decay r in (0, 1]
            /// (1 = off, the default): partition p starts at
            /// P(0) = initial_uncertainty * r^p, encoding "echo paths
            /// decay along the filter" as a Kalman prior. On
            /// rank-deficient (periodic) excitation the filter cannot
            /// identify the partition distribution and splits its
            /// minimum-norm solution proportional to P — a flat prior
            /// spreads it uniformly (measured at the notch: misalignment
            /// lands ABOVE 0 dB, worse than an empty filter), a decaying
            /// prior lands it like an impulse response. The trade is
            /// real and measured: a path with heavy bulk delay (energy
            /// arriving partitions deep) converges slower by roughly the
            /// prior ratio until the process noise re-opens the late
            /// partitions (32 ms of dead delay at 16 kHz / block 128:
            /// ERL by 600 ms 21.1 -> 16.8 dB at r = 0.5), so keep this
            /// off where bulk delay is expected and uncompensated.
            Sample initial_uncertainty_decay = Sample(1);
            /// Narrowband guard (0 = off, the default): when the newest
            /// input block spectrum holds more than this fraction of its
            /// power in its 4 largest bins, sustained
            /// narrowband_hold_blocks, adaptation freezes until the
            /// excitation broadens again (the counter halves per
            /// broadband block, releasing in ~7 blocks). This is the
            /// classical tone-disabler discipline, and it is what closes
            /// the float32 tone row: on a sustained on-bin tone every
            /// partition sees the SAME input spectrum, so the
            /// partition-redistribution null space is invisible to the
            /// error, and the gradient constraint's per-block projection
            /// churns it into a noise-driven weight walk whose
            /// equilibrium scales with rounding (measured, 16 kHz /
            /// block 256, 30 s 1 kHz on-bin tone: double -101 dBm0(A),
            /// float -20 against a -49.3 gate; unconstrained reaches
            /// -233 / -162 but collapses broadband convergence, so the
            /// constraint must stay). Freezing once converged removes
            /// the churn: float lands at -72 (on-bin worst case; off-bin
            /// -151, DTMF -148). A tone conveys no new broadband
            /// information, so nothing is lost by freezing — and G.168
            /// SS6/7 want exactly this (tones must not corrupt the
            /// filter). Threshold 0.8 separates cleanly (measured, 1 s
            /// hold): fires on single tones and DTMF pairs, never on
            /// CSS, the P.501 AM-FM combs, or noise (0 blocks frozen,
            /// convergence bit-identical).
            Sample narrowband_guard = Sample(0);
            /// Blocks of sustained concentration before the freeze
            /// engages (~1 s of real time at the caller's geometry —
            /// the guard must outwait a CSS voiced segment, 48.6 ms).
            size_t narrowband_hold_blocks = 187;
            bool   constrained            = true; ///< gradient constraint, as in partitioned_fdaf
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
            , m_coh_num(2 * (cfg.block_size + 1))
            , m_coh_den(cfg.block_size + 1)
            , m_nov(cfg.block_size + 1) {
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

        /// True while the narrowband guard is holding adaptation frozen.
        bool narrowband_frozen() const noexcept {
            return m_cfg.narrowband_guard > Sample(0) && m_nb_count >= m_cfg.narrowband_hold_blocks;
        }

        /// Lift every bin's state uncertainty back to the initial value,
        /// WITHOUT touching the filter weights: the converged estimate is
        /// kept, but the next blocks' gains are cold-start-sized, so the
        /// filter re-converges from where it stands at cold-start speed.
        ///
        /// This is the core's half of the RE-CONVERGENCE RESCUE for abrupt
        /// path changes (the policy half — detecting that the path
        /// changed — lives in aec_chain, which owns a statistic that can
        /// tell; see postfilter.h). A converged filter's failure mode is
        /// measured and structural: P sits orders of magnitude below
        /// P(0), and within ~100 ms the noise tracker absorbs the
        /// unmodeled-echo residual into Psi_s, so the filter books its
        /// own error as near-end noise and locks itself at a gain ~20x
        /// too small (deep re-convergence ~7 s where a cold start takes
        /// 1.4 s).
        ///
        /// REJECTED DESIGN, recorded (Stage 6, measured): an in-core
        /// per-bin detector — a smoothed momentum of the normalized raw
        /// update direction conj(U_p)E/S_uu, lifting P where it exceeded
        /// its statistical noise floor. It fixed the swap trajectories
        /// (~7 s -> ~3 s) and survived CSS double talk, background noise
        /// and cold starts bit-identically — but sinusoid-pair near ends
        /// defeat the whole correlation family: two FM comb lines sharing
        /// an analysis bin (P.501's AM-FM double-talk plans) beat at
        /// their difference frequency and masquerade as sustained drift.
        /// At 48 kHz bin resolution the misfire is broadband (a breadth
        /// gate failed) and coherent across partitions (an explained-
        /// fraction dominance gate failed): the AM-FM double-talk rows
        /// diverged past -400 dB send attenuation in both attempts. The
        /// one-shot bounded lift below cannot diverge by construction —
        /// a false trigger merely hands the filter a cold start, and
        /// cold starts under double talk / noise are the regime Psi_s
        /// keeps conservative by design.
        void reinflate_uncertainty() noexcept {
            const size_t stride = m_cfg.block_size + 1;
            Sample       p0     = m_cfg.initial_uncertainty;
            for (size_t p = 0; p < m_cfg.partitions; ++p) {
                for (size_t k = 0; k < stride; ++k) {
                    Sample& x = m_p[p * stride + k];
                    x         = x < p0 ? p0 : x;
                }
                p0 *= m_cfg.initial_uncertainty_decay;
            }
        }

        /// Zero the filter and histories, restore the initial uncertainty
        /// (shaped by initial_uncertainty_decay when configured).
        void reset() noexcept {
            fill_zero(m_input);
            fill_zero(m_u);
            fill_zero(m_h);
            fill_zero(m_psi_s);
            fill_zero(m_coh_num);
            fill_zero(m_coh_den);
            const size_t stride = m_cfg.block_size + 1;
            Sample       p0     = m_cfg.initial_uncertainty;
            for (size_t p = 0; p < m_cfg.partitions; ++p) {
                for (size_t k = 0; k < stride; ++k) {
                    m_p[p * stride + k] = p0;
                }
                p0 *= m_cfg.initial_uncertainty_decay;
            }
            for (auto& x : m_nov) {
                x = Sample(1);
            }
            m_head     = m_cfg.partitions - 1;
            m_nb_count = 0;
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
            // as in partitioned_fdaf (memmove/memcpy: bit-identical to the
            // element loop they replace).
            std::memmove(m_input.data(), m_input.data() + b, b * sizeof(Sample));
            std::memcpy(m_input.data() + b, input, b * sizeof(Sample));
            m_head        = (m_head + 1) % p_n;
            Sample* u_new = &m_u[m_head * m_n];
            for (size_t i = 0; i < m_n; ++i) {
                u_new[i] = m_input[i];
            }
            m_fft.forward_inplace(u_new);

            // Excitation-novelty discount: per-bin coherence between this
            // block's spectrum and the previous one (partition-1 ring
            // slot). Coherent (repeating) excitation suspends the P
            // decrement below; decorrelated excitation keeps it exact.
            if (m_cfg.novelty_smoothing > Sample(0) && p_n >= 2) {
                const Sample* up  = &m_u[((m_head + p_n - 1) % p_n) * m_n];
                const Sample  bc  = m_cfg.novelty_smoothing;
                const Sample  ob  = Sample(1) - bc;
                const auto    upd = [&](size_t bin, Sample nr, Sample ni, Sample mag) noexcept {
                    m_coh_num[2 * bin]     = bc * m_coh_num[2 * bin] + ob * nr;
                    m_coh_num[2 * bin + 1] = bc * m_coh_num[2 * bin + 1] + ob * ni;
                    m_coh_den[bin]         = bc * m_coh_den[bin] + ob * mag;
                    const Sample num =
                        m_coh_num[2 * bin] * m_coh_num[2 * bin] + m_coh_num[2 * bin + 1] * m_coh_num[2 * bin + 1];
                    const Sample den = m_coh_den[bin] * m_coh_den[bin] + m_cfg.regularization;
                    Sample       nu  = Sample(1) - num / den;
                    nu               = nu < m_cfg.novelty_floor ? m_cfg.novelty_floor : nu;
                    m_nov[bin]       = nu > Sample(1) ? Sample(1) : nu;
                };
                const Sample d0 = u_new[0] * up[0];
                upd(0, d0, Sample(0), d0 < Sample(0) ? -d0 : d0);
                const Sample dn = u_new[1] * up[1];
                upd(half, dn, Sample(0), dn < Sample(0) ? -dn : dn);
                for (size_t k = 1; k < half; ++k) {
                    const Sample ar = u_new[2 * k];
                    const Sample ai = u_new[2 * k + 1];
                    const Sample br = up[2 * k];
                    const Sample bi = up[2 * k + 1];
                    upd(k, ar * br + ai * bi, ai * br - ar * bi, std::sqrt((ar * ar + ai * ai) * (br * br + bi * bi)));
                }
            }

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

            // Narrowband guard: freeze adaptation on sustained
            // spectrally-concentrated excitation (see the config
            // comment). The detector keeps running while frozen so the
            // freeze releases when the excitation broadens.
            if (m_cfg.narrowband_guard > Sample(0)) {
                Sample     top[4] = {Sample(0), Sample(0), Sample(0), Sample(0)};
                Sample     total  = Sample(0);
                const auto feed   = [&](Sample p2) noexcept {
                    total += p2;
                    if (p2 > top[3]) {
                        top[3] = p2;
                        for (int j = 3; j > 0 && top[j] > top[j - 1]; --j) {
                            const Sample t = top[j - 1];
                            top[j - 1]     = top[j];
                            top[j]         = t;
                        }
                    }
                };
                feed(u_new[0] * u_new[0]);
                feed(u_new[1] * u_new[1]);
                for (size_t k = 1; k < half; ++k) {
                    feed(u_new[2 * k] * u_new[2 * k] + u_new[2 * k + 1] * u_new[2 * k + 1]);
                }
                const Sample conc = (top[0] + top[1] + top[2] + top[3]) / (total + m_cfg.regularization);
                if (total > m_cfg.regularization && conc > m_cfg.narrowband_guard) {
                    if (m_nb_count < m_cfg.narrowband_hold_blocks) {
                        ++m_nb_count;
                    }
                }
                else {
                    m_nb_count /= 2;
                }
                if (m_nb_count >= m_cfg.narrowband_hold_blocks) {
                    return;
                }
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
                    pp[0] *= Sample(1) - Sample(0.5) * g * u[0] * u[0] * m_nov[0];
                }
                {
                    const Sample g = pp[half] / m_denom[half];
                    h[1] += g * u[1] * m_espec[1];
                    pp[half] *= Sample(1) - Sample(0.5) * g * u[1] * u[1] * m_nov[half];
                }
                for (size_t k = 1; k < half; ++k) {
                    const Sample g  = pp[k] / m_denom[k];
                    const Sample ur = u[2 * k];
                    const Sample ui = u[2 * k + 1];
                    const Sample er = m_espec[2 * k];
                    const Sample ei = m_espec[2 * k + 1];
                    h[2 * k] += g * (ur * er + ui * ei);
                    h[2 * k + 1] += g * (ur * ei - ui * er);
                    pp[k] *= Sample(1) - Sample(0.5) * g * (ur * ur + ui * ui) * m_nov[k];
                }
                if (m_cfg.constrained) {
                    m_fft.inverse(h, m_time.data());
                    for (size_t i = 0; i < b; ++i) {
                        m_time[i + b] = Sample(0);
                    }
                    m_fft.forward(m_time.data(), h);
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
            if (!(cfg.novelty_smoothing >= Sample(0)) || !(cfg.novelty_smoothing < Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: novelty_smoothing must be in [0, 1)");
            }
            if (!(cfg.novelty_floor >= Sample(0)) || !(cfg.novelty_floor <= Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: novelty_floor must be in [0, 1]");
            }
            if (!(cfg.initial_uncertainty_decay > Sample(0)) || !(cfg.initial_uncertainty_decay <= Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: initial_uncertainty_decay must be in (0, 1]");
            }
            if (!(cfg.narrowband_guard >= Sample(0)) || !(cfg.narrowband_guard <= Sample(1))) {
                throw std::invalid_argument("partitioned_fdkf: narrowband_guard must be in [0, 1]");
            }
            if (cfg.narrowband_guard > Sample(0) && cfg.narrowband_hold_blocks == 0) {
                throw std::invalid_argument(
                    "partitioned_fdkf: narrowband_hold_blocks must be >= 1 when the guard is on");
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
        std::vector<Sample>    m_input;   ///< sliding 2B-sample input window
        std::vector<Sample>    m_u;       ///< P input-block spectra (ring, newest at m_head)
        std::vector<Sample>    m_h;       ///< P filter-partition spectra (the state mean)
        std::vector<Sample>    m_accum;   ///< output-spectrum accumulator
        std::vector<Sample>    m_espec;   ///< error-block spectrum
        std::vector<Sample>    m_time;    ///< time-domain scratch
        std::vector<Sample>    m_p;       ///< per-partition, per-bin state variance
        std::vector<Sample>    m_psi_s;   ///< per-bin near-end PSD estimate
        std::vector<Sample>    m_denom;   ///< per-bin innovation power, this block
        std::vector<Sample>    m_coh_num; ///< novelty: packed complex <U_t conj(U_t-1)> accumulator
        std::vector<Sample>    m_coh_den; ///< novelty: |U_t||U_t-1| accumulator
        std::vector<Sample>    m_nov;     ///< novelty: per-bin P-decrement scale, this block
        size_t                 m_head     = 0;
        size_t                 m_nb_count = 0;
        bool                   m_adapt    = true;
    };

} // namespace mutap
