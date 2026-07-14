/// @file fdaf.h
/// @brief Partitioned-block frequency-domain adaptive filter (overlap-save,
///        NLMS update) — the FDAF core that PEM-AFROW prewhitening will wrap.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "mutap/fft.h"

namespace mutap {

    namespace detail {
        /// Packed-spectrum multiply-accumulate: y += a * b, in the Ooura real-FFT
        /// layout (DC in slot 0, Nyquist in slot 1, then re/im pairs).
        template <typename Sample>
        inline void packed_mac(const Sample* a, const Sample* b, Sample* y, size_t n) noexcept {
            y[0] += a[0] * b[0];
            y[1] += a[1] * b[1];
            for (size_t i = 2; i < n; i += 2) {
                const Sample ar = a[i];
                const Sample ai = a[i + 1];
                const Sample br = b[i];
                const Sample bi = b[i + 1];
                y[i] += ar * br - ai * bi;
                y[i + 1] += ar * bi + ai * br;
            }
        }
    } // namespace detail

    /// Partitioned-block frequency-domain adaptive filter (a.k.a. multidelay
    /// block frequency-domain filter, MDF): overlap-save with FFT size N = 2B,
    /// the length-P*B filter split into P partitions of B taps, adapted by a
    /// per-bin normalized (NLMS) update with an optional gradient constraint.
    ///
    /// This is the open-loop identification core: given a reference input
    /// u(n) and a desired signal d(n), it adapts F_hat so that e = d - F_hat*u
    /// is minimized. It knows nothing about closed loops or prewhitening —
    /// PEM-AFROW (milestone M3) feeds it prewhitened (u', e') pairs; used
    /// directly it is a plain acoustic echo canceller.
    ///
    /// Real-time contract: the constructor allocates and may throw
    /// (std::invalid_argument on a bad config); process_block() and every
    /// other post-construction entry point are noexcept and allocation-free.
    ///
    /// Per-bin NLMS normalization: the update divides by the regressor power
    /// in that bin summed over all partitions (optionally smoothed across
    /// blocks by power_smoothing), so step_size behaves like a classic NLMS
    /// mu: stable in (0, 2), with mu ~ 0.5 a robust default.
    ///
    /// The gradient constraint (constrained = true) projects each updated
    /// partition back onto causal length-B impulse responses (IFFT, zero the
    /// tail half, FFT). It costs two extra FFTs per partition per block and
    /// buys the exact linear-convolution filter model — the misalignment
    /// floor of the unconstrained variant is limited by circular wraparound.
    template <typename Sample>
    class partitioned_fdaf {
      public:
        struct config {
            size_t block_size     = 256;           ///< B: samples per block/hop; power of 2, >= 4
            size_t partitions     = 4;             ///< P: filter length = P * B taps
            Sample step_size      = Sample(0.5);   ///< NLMS mu, stable in (0, 2)
            Sample regularization = Sample(1e-12); ///< absolute eps floor (guards silence/denormals only;
                                                   ///< the relative term is the real regularizer)
            Sample power_smoothing = Sample(0);    ///< alpha in [0, 1): 0 = instantaneous (pure NLMS)
            bool   constrained     = true;         ///< gradient constraint (see class comment)
            /// Variable regularization: each bin's normalizer additionally
            /// gets this fraction of the MEAN per-bin power, so poorly
            /// excited bins take bounded steps at any signal scale (a fixed
            /// epsilon is only right at one scale).
            Sample relative_regularization = Sample(1e-2);
            /// IPC-gated adaptation control. The IPC (after the
            /// instantaneous pseudo-correlation of Gil-Cacho et al. 2014) is
            /// computed here as a per-bin, per-partition magnitude-squared
            /// coherence between the input-block spectra and the error
            /// spectrum (one-pole-smoothed cross/auto spectra,
            /// energy-weighted across bins, chance-level-corrected), i.e. an
            /// estimate in [0, 1] of the FRACTION OF ERROR POWER COHERENT
            /// WITH THE INPUT: high while unmodeled echo/feedback dominates
            /// the error (informative update), low when the near-end signal
            /// dominates (double-talk; adapting would inject bias). With
            /// ipc_step_scaling, mu is scaled by IPC^2 — a Wiener-flavored
            /// variable step size. Note the coherence needs ~(1+a)/(1-a)
            /// blocks of history, so a fresh filter ramps its step up over
            /// that many blocks when scaling is enabled.
            Sample ipc_smoothing        = Sample(0.95); ///< one-pole smoothing of the coherence spectra, [0, 1)
            bool   ipc_step_scaling     = false;        ///< scale mu by IPC^2 (Wiener-flavored VSS)
            Sample ipc_freeze_threshold = Sample(0);    ///< skip the update while IPC < this, [0, 1]
            /// Transient gate: skip the update for any block whose
            /// instantaneous error power exceeds this multiple of the
            /// smoothed error power (a near-end burst the smoothed IPC is
            /// too slow to catch — the block's update SNR is terrible and
            /// one such block can wreck the filter). 0 disables.
            Sample transient_freeze_ratio = Sample(0);
        };

        explicit partitioned_fdaf(const config& cfg)
            : m_cfg(validated(cfg))
            , m_n(2 * cfg.block_size)
            , m_fft(m_n)
            , m_input(m_n)
            , m_u(cfg.partitions * m_n)
            , m_h(cfg.partitions * m_n)
            , m_accum(m_n)
            , m_espec(m_n)
            , m_time(m_n)
            , m_norm(cfg.block_size + 1)
            , m_power(cfg.block_size + 1)
            , m_factor(cfg.block_size + 1)
            , m_s_cross(cfg.partitions * m_n)
            , m_s_uu(cfg.partitions * (cfg.block_size + 1))
            , m_s_ee(cfg.block_size + 1) {
            reset();
        }

        size_t block_size() const noexcept { return m_cfg.block_size; }
        size_t partitions() const noexcept { return m_cfg.partitions; }
        size_t filter_length() const noexcept { return m_cfg.block_size * m_cfg.partitions; }
        size_t fft_size() const noexcept { return m_n; }

        /// Read access to partition p's filter spectrum (packed layout,
        /// fft_size() slots). This is what lets a PEM wrapper apply the
        /// learned filter to a *different* (non-prewhitened) input stream.
        const Sample* partition_spectrum(size_t p) const noexcept { return &m_h[p * m_n]; }

        Sample step_size() const noexcept { return m_cfg.step_size; }
        void   set_step_size(Sample mu) noexcept { m_cfg.step_size = mu; }

        bool adapting() const noexcept { return m_adapt; }
        void set_adaptation(bool enabled) noexcept { m_adapt = enabled; }

        /// Smoothed IPC of the adaptation signal pair, in [0, 1]; updated on
        /// every adapting process_block() (also while the freeze threshold
        /// is holding the filter, so it can release).
        Sample ipc() const noexcept { return m_ipc; }

        /// True if the last adapting block was held by the transient gate.
        bool transient_held() const noexcept { return m_transient; }

        /// Zero the filter, the input history and the power estimate.
        void reset() noexcept {
            fill_zero(m_input);
            fill_zero(m_u);
            fill_zero(m_h);
            fill_zero(m_power);
            fill_zero(m_s_cross);
            fill_zero(m_s_uu);
            fill_zero(m_s_ee);
            m_head      = m_cfg.partitions - 1;
            m_ipc       = Sample(0);
            m_transient = false;
            m_ee_weight = Sample(0);
        }

        /// Process exactly block_size() samples: filter the reference input,
        /// write error = desired - estimate, and (unless adaptation is
        /// frozen) update the filter from that error. Optionally also writes
        /// the filter output itself through `estimate`.
        void process_block(const Sample* input, const Sample* desired, Sample* error,
                           Sample* estimate = nullptr) noexcept {
            const size_t b    = m_cfg.block_size;
            const size_t p_n  = m_cfg.partitions;
            const size_t half = b; // bin count is half + 1

            // Slide the overlap-save window and take the newest input block's
            // spectrum into the partition ring.
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

            // Filter output: Y = sum_p U(k-p) * H_p, valid samples in the
            // second half after the inverse transform (overlap-save).
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

            update_ipc();

            // Per-bin regressor power summed over partitions -> NLMS factor.
            fill_zero(m_norm);
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u = &m_u[p * m_n];
                m_norm[0] += u[0] * u[0];
                m_norm[half] += u[1] * u[1];
                for (size_t k = 1; k < half; ++k) {
                    const Sample re = u[2 * k];
                    const Sample im = u[2 * k + 1];
                    m_norm[k] += re * re + im * im;
                }
            }
            const Sample alpha      = m_cfg.power_smoothing;
            Sample       mean_power = Sample(0);
            for (size_t k = 0; k <= half; ++k) {
                m_power[k] = alpha * m_power[k] + (Sample(1) - alpha) * m_norm[k];
                mean_power += m_power[k];
            }
            mean_power /= static_cast<Sample>(half + 1);

            // The power estimate and IPC keep tracking during a freeze; only
            // the filter update itself is held.
            if (m_transient || m_ipc < m_cfg.ipc_freeze_threshold) {
                return;
            }
            Sample mu_eff = m_cfg.step_size;
            if (m_cfg.ipc_step_scaling) {
                mu_eff *= m_ipc * m_ipc;
            }
            const Sample eps = m_cfg.regularization + m_cfg.relative_regularization * mean_power;
            for (size_t k = 0; k <= half; ++k) {
                m_factor[k] = mu_eff / (m_power[k] + eps);
            }

            // Normalized update, H_p += factor .* conj(U(k-p)) * E, per bin;
            // then (optionally) constrain each partition back to a causal
            // length-B impulse response.
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u = &m_u[((m_head + p_n - p) % p_n) * m_n];
                Sample*       h = &m_h[p * m_n];
                h[0] += m_factor[0] * u[0] * m_espec[0];
                h[1] += m_factor[half] * u[1] * m_espec[1];
                for (size_t k = 1; k < half; ++k) {
                    const Sample ur = u[2 * k];
                    const Sample ui = u[2 * k + 1];
                    const Sample er = m_espec[2 * k];
                    const Sample ei = m_espec[2 * k + 1];
                    const Sample f  = m_factor[k];
                    h[2 * k] += f * (ur * er + ui * ei);
                    h[2 * k + 1] += f * (ur * ei - ui * er);
                }
                if (m_cfg.constrained) {
                    m_fft.inverse(h, m_time.data());
                    for (size_t i = 0; i < b; ++i) {
                        m_time[i + b] = Sample(0);
                    }
                    m_fft.forward(m_time.data(), h);
                }
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
        /// Coherence-based IPC (see the config comment). Uses the one-pole
        /// smoothed cross-spectrum between each partition's input spectrum
        /// (at its update lag) and the error spectrum, the smoothed input
        /// auto-spectra and the smoothed error spectrum; the energy-weighted
        /// coherent fraction is corrected for its chance level P/M (M = the
        /// smoother's effective averaging length).
        void update_ipc() noexcept {
            const size_t  half  = m_cfg.block_size;
            const size_t  p_n   = m_cfg.partitions;
            const Sample  a     = m_cfg.ipc_smoothing;
            const Sample  one_a = Sample(1) - a;
            const Sample* eb    = m_espec.data();

            // Transient gate: instantaneous error power vs the smoothed
            // error power from BEFORE this block.
            Sample inst_e2 = eb[0] * eb[0] + eb[1] * eb[1];
            for (size_t k = 1; k < half; ++k) {
                inst_e2 += eb[2 * k] * eb[2 * k] + eb[2 * k + 1] * eb[2 * k + 1];
            }
            Sample prev_total = Sample(0);
            for (size_t k = 0; k <= half; ++k) {
                prev_total += m_s_ee[k];
            }
            // Debias the one-pole average (it starts at zero, which would
            // make every early block look like a spike): after n blocks the
            // smoother's weight mass is 1 - a^n, tracked in m_ee_weight.
            const Sample prev_mean = (m_ee_weight > Sample(0)) ? prev_total / m_ee_weight : Sample(0);
            m_transient            = m_cfg.transient_freeze_ratio > Sample(0) && prev_mean > Sample(0)
                          && inst_e2 > m_cfg.transient_freeze_ratio * prev_mean;
            m_ee_weight = a * m_ee_weight + one_a;

            // Smooth the error auto-spectrum.
            m_s_ee[0]    = a * m_s_ee[0] + one_a * eb[0] * eb[0];
            m_s_ee[half] = a * m_s_ee[half] + one_a * eb[1] * eb[1];
            for (size_t k = 1; k < half; ++k) {
                m_s_ee[k] = a * m_s_ee[k] + one_a * (eb[2 * k] * eb[2 * k] + eb[2 * k + 1] * eb[2 * k + 1]);
            }

            // Per partition: smooth the cross- and auto-spectra at the
            // partition's update lag, and accumulate the explained power
            // sum_p |S_ue,p(k)|^2 / S_uu,p(k) per bin, energy-weighted.
            Sample explained = Sample(0);
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u  = &m_u[((m_head + p_n - p) % p_n) * m_n];
                Sample*       sc = &m_s_cross[p * m_n];
                Sample*       su = &m_s_uu[p * (half + 1)];

                su[0]    = a * su[0] + one_a * u[0] * u[0];
                su[half] = a * su[half] + one_a * u[1] * u[1];
                sc[0]    = a * sc[0] + one_a * u[0] * eb[0];
                sc[1]    = a * sc[1] + one_a * u[1] * eb[1];
                if (su[0] > Sample(0)) {
                    explained += sc[0] * sc[0] / su[0];
                }
                if (su[half] > Sample(0)) {
                    explained += sc[1] * sc[1] / su[half];
                }
                for (size_t k = 1; k < half; ++k) {
                    const Sample ur = u[2 * k];
                    const Sample ui = u[2 * k + 1];
                    const Sample er = eb[2 * k];
                    const Sample ei = eb[2 * k + 1];
                    su[k]           = a * su[k] + one_a * (ur * ur + ui * ui);
                    sc[2 * k]       = a * sc[2 * k] + one_a * (ur * er + ui * ei); // conj(U) * E
                    sc[2 * k + 1]   = a * sc[2 * k + 1] + one_a * (ur * ei - ui * er);
                    if (su[k] > Sample(0)) {
                        explained += (sc[2 * k] * sc[2 * k] + sc[2 * k + 1] * sc[2 * k + 1]) / su[k];
                    }
                }
            }

            Sample total = Sample(0);
            for (size_t k = 0; k <= half; ++k) {
                total += m_s_ee[k];
            }
            if (!(total > Sample(0)) || !std::isfinite(static_cast<double>(total))) {
                m_ipc = Sample(0);
                return;
            }
            const Sample coherent = explained / total; // raw coherent fraction, ~[0, 1]
            // Chance level: independent signals still show ~P/M apparent
            // coherence with an M-block averaging window. During warm-up the
            // smoothers hold fewer effective blocks (weight mass 1 - a^n),
            // which inflates apparent coherence — scale the chance level by
            // the accumulated mass; while it is >= 1 the estimate carries no
            // information yet and IPC reads 0 (conservative for gating).
            const Sample m_eff  = ((Sample(1) + a) / (Sample(1) - a)) * m_ee_weight;
            const Sample chance = (m_eff > Sample(0)) ? static_cast<Sample>(p_n) / m_eff : Sample(1);
            // Below half the smoother's mass — or with the chance level near
            // 1, where the correction divides by ~0 and amplifies noise into
            // spurious spikes — the estimate carries no information yet:
            // report 0 (conservative for gating).
            if (m_ee_weight < Sample(0.5) || chance >= Sample(0.5)) {
                m_ipc = Sample(0);
                return;
            }
            Sample ipc2 = (coherent - chance) / (Sample(1) - chance);
            if (!(ipc2 > Sample(0))) {
                ipc2 = Sample(0);
            }
            if (ipc2 > Sample(1)) {
                ipc2 = Sample(1);
            }
            m_ipc = std::sqrt(ipc2);
        }

        static config validated(const config& cfg) {
            if (cfg.block_size < 4 || (cfg.block_size & (cfg.block_size - 1)) != 0) {
                throw std::invalid_argument("partitioned_fdaf: block_size must be a power of 2, >= 4");
            }
            if (cfg.partitions == 0) {
                throw std::invalid_argument("partitioned_fdaf: partitions must be >= 1");
            }
            if (!(cfg.step_size > Sample(0)) || !(cfg.step_size < Sample(2))) {
                throw std::invalid_argument("partitioned_fdaf: step_size must be in (0, 2)");
            }
            if (!(cfg.regularization > Sample(0))) {
                throw std::invalid_argument("partitioned_fdaf: regularization must be > 0");
            }
            if (!(cfg.power_smoothing >= Sample(0)) || !(cfg.power_smoothing < Sample(1))) {
                throw std::invalid_argument("partitioned_fdaf: power_smoothing must be in [0, 1)");
            }
            if (!(cfg.relative_regularization >= Sample(0))) {
                throw std::invalid_argument("partitioned_fdaf: relative_regularization must be >= 0");
            }
            if (!(cfg.ipc_smoothing >= Sample(0)) || !(cfg.ipc_smoothing < Sample(1))) {
                throw std::invalid_argument("partitioned_fdaf: ipc_smoothing must be in [0, 1)");
            }
            if (!(cfg.ipc_freeze_threshold >= Sample(0)) || !(cfg.ipc_freeze_threshold <= Sample(1))) {
                throw std::invalid_argument("partitioned_fdaf: ipc_freeze_threshold must be in [0, 1]");
            }
            if (!(cfg.transient_freeze_ratio >= Sample(0))) {
                throw std::invalid_argument("partitioned_fdaf: transient_freeze_ratio must be >= 0");
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
        std::vector<Sample>    m_h;       ///< P filter-partition spectra
        std::vector<Sample>    m_accum;   ///< output-spectrum accumulator
        std::vector<Sample>    m_espec;   ///< error-block spectrum
        std::vector<Sample>    m_time;    ///< time-domain scratch
        std::vector<Sample>    m_norm;    ///< per-bin regressor power, this block
        std::vector<Sample>    m_power;   ///< per-bin power, smoothed
        std::vector<Sample>    m_factor;  ///< per-bin step factor mu / (power + eps)
        std::vector<Sample>    m_s_cross; ///< smoothed cross-spectra S_ue per partition (packed)
        std::vector<Sample>    m_s_uu;    ///< smoothed input auto-spectra per partition, per bin
        std::vector<Sample>    m_s_ee;    ///< smoothed error auto-spectrum, per bin
        size_t                 m_head      = 0;
        bool                   m_adapt     = true;
        Sample                 m_ipc       = Sample(0);
        bool                   m_transient = false;
        Sample                 m_ee_weight = Sample(0); ///< 1 - a^n debiasing weight for m_s_ee
    };

} // namespace mutap
