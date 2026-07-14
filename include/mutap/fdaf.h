/// @file fdaf.h
/// @brief Partitioned-block frequency-domain adaptive filter (overlap-save,
///        NLMS update) — the FDAF core that PEM-AFROW prewhitening will wrap.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

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
            size_t block_size      = 256;          ///< B: samples per block/hop; power of 2, >= 4
            size_t partitions      = 4;            ///< P: filter length = P * B taps
            Sample step_size       = Sample(0.5);  ///< NLMS mu, stable in (0, 2)
            Sample regularization  = Sample(1e-6); ///< eps added to the per-bin power normalizer
            Sample power_smoothing = Sample(0);    ///< alpha in [0, 1): 0 = instantaneous (pure NLMS)
            bool   constrained     = true;         ///< gradient constraint (see class comment)
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
            , m_factor(cfg.block_size + 1) {
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

        /// Zero the filter, the input history and the power estimate.
        void reset() noexcept {
            fill_zero(m_input);
            fill_zero(m_u);
            fill_zero(m_h);
            fill_zero(m_power);
            m_head = m_cfg.partitions - 1;
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
            const Sample alpha = m_cfg.power_smoothing;
            for (size_t k = 0; k <= half; ++k) {
                m_power[k]  = alpha * m_power[k] + (Sample(1) - alpha) * m_norm[k];
                m_factor[k] = m_cfg.step_size / (m_power[k] + m_cfg.regularization);
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
        std::vector<Sample>    m_input;  ///< sliding 2B-sample input window
        std::vector<Sample>    m_u;      ///< P input-block spectra (ring, newest at m_head)
        std::vector<Sample>    m_h;      ///< P filter-partition spectra
        std::vector<Sample>    m_accum;  ///< output-spectrum accumulator
        std::vector<Sample>    m_espec;  ///< error-block spectrum
        std::vector<Sample>    m_time;   ///< time-domain scratch
        std::vector<Sample>    m_norm;   ///< per-bin regressor power, this block
        std::vector<Sample>    m_power;  ///< per-bin power, smoothed
        std::vector<Sample>    m_factor; ///< per-bin step factor mu / (power + eps)
        size_t                 m_head  = 0;
        bool                   m_adapt = true;
    };

} // namespace mutap
