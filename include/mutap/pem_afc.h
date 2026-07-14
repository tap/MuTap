/// @file pem_afc.h
/// @brief PEM-prewhitened acoustic feedback canceller: the FDAF core of
///        fdaf.h adapting on prewhitened signals (FDAF-PEM-AFROW structure),
///        cancelling on the raw ones.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "mutap/fdaf.h"
#include "mutap/fft.h"
#include "mutap/lpc.h"

namespace mutap {

    /// Acoustic feedback canceller with PEM decorrelation (the FDAF-PEM-AFROW
    /// structure; Gil-Cacho et al. 2014, Rombouts et al. 2007).
    ///
    /// The closed loop biases a naive adaptive estimate because the
    /// loudspeaker signal u is correlated with the near-end source v. PEM
    /// models v as shaped noise, v = w / A(q): each block, the near-end
    /// model is re-fit by linear prediction of a window of the
    /// feedback-compensated signal e (the best available proxy for v), and
    /// BOTH the adaptive filter's input and its desired signal are
    /// prefiltered by A_hat(q) before the update:
    ///
    ///   e  = y - F_hat(q) u          cancellation, on the RAW signals
    ///   u' = A_hat(q) u,  y' = A_hat(q) y
    ///   FDAF update runs on (u', y')  ->  (approximately) unbiased F_hat
    ///
    /// Because A_hat commutes with the LTI F_hat, the filter learned on the
    /// prewhitened pair is the feedback-path estimate itself; this class
    /// keeps a second (raw) input-spectrum ring and applies the FDAF's
    /// partition spectra to it for the actual cancellation.
    ///
    /// The near-end model is the pluggable component (HANDOFF.md): any type
    /// satisfying the plug-in contract documented in lpc.h. Default is the
    /// speech cascade (short-term LP + long-term pitch predictor).
    ///
    /// Real-time contract: constructor allocates and may throw;
    /// process_block() and every other post-construction entry point are
    /// noexcept and allocation-free.
    template <typename Sample, typename Predictor = speech_predictor<Sample>>
    class pem_afc {
      public:
        struct config {
            typename partitioned_fdaf<Sample>::config fdaf;
            typename Predictor::config                predictor;
            size_t                                    analysis_window = 1024; ///< samples of e per near-end re-fit
        };

        explicit pem_afc(const config& cfg)
            : m_cfg(validated(cfg))
            , m_fdaf(cfg.fdaf)
            , m_predictor(cfg.predictor)
            , m_u_state(m_predictor.make_state())
            , m_y_state(m_predictor.make_state())
            , m_n(m_fdaf.fft_size())
            , m_fft(m_n)
            , m_input(m_n)
            , m_u_raw(cfg.fdaf.partitions * m_n)
            , m_accum(m_n)
            , m_time(m_n)
            , m_u_pw(cfg.fdaf.block_size)
            , m_y_pw(cfg.fdaf.block_size)
            , m_e_pw(cfg.fdaf.block_size)
            , m_e_window(cfg.analysis_window) {
            reset();
        }

        size_t block_size() const noexcept { return m_fdaf.block_size(); }
        size_t partitions() const noexcept { return m_fdaf.partitions(); }
        size_t filter_length() const noexcept { return m_fdaf.filter_length(); }

        bool adapting() const noexcept { return m_fdaf.adapting(); }
        void set_adaptation(bool enabled) noexcept { m_fdaf.set_adaptation(enabled); }

        /// IPC of the PREWHITENED adaptation pair (see partitioned_fdaf);
        /// this staying low while the raw-pair IPC is high is exactly the
        /// bias reduction PEM buys (Gil-Cacho et al. 2014).
        Sample ipc() const noexcept { return m_fdaf.ipc(); }

        partitioned_fdaf<Sample>&       fdaf() noexcept { return m_fdaf; }
        const partitioned_fdaf<Sample>& fdaf() const noexcept { return m_fdaf; }
        const Predictor&                predictor() const noexcept { return m_predictor; }

        void reset() noexcept {
            m_fdaf.reset();
            m_predictor.reset_state(m_u_state);
            m_predictor.reset_state(m_y_state);
            for (auto& x : m_u_raw) {
                x = Sample(0);
            }
            for (auto& x : m_input) {
                x = Sample(0);
            }
            for (auto& x : m_e_window) {
                x = Sample(0);
            }
            m_head = m_fdaf.partitions() - 1;
        }

        /// Process exactly block_size() samples of the loudspeaker signal u
        /// and the microphone signal y; writes the feedback-compensated
        /// output e = y - F_hat u and (unless frozen) re-fits the near-end
        /// model and updates F_hat on the prewhitened pair.
        void process_block(const Sample* u, const Sample* y, Sample* e) noexcept {
            const size_t b   = block_size();
            const size_t p_n = partitions();

            // Raw-u spectrum into this class's own overlap-save ring.
            for (size_t i = 0; i < b; ++i) {
                m_input[i]     = m_input[i + b];
                m_input[i + b] = u[i];
            }
            m_head           = (m_head + 1) % p_n;
            Sample* u_newest = &m_u_raw[m_head * m_n];
            for (size_t i = 0; i < m_n; ++i) {
                u_newest[i] = m_input[i];
            }
            m_fft.forward_inplace(u_newest);

            // Cancellation with the CURRENT filter on the raw input:
            // e = y - IFFT(sum_p U_raw(k-p) . H_p), last half valid.
            for (auto& x : m_accum) {
                x = Sample(0);
            }
            for (size_t p = 0; p < p_n; ++p) {
                const Sample* u_p = &m_u_raw[((m_head + p_n - p) % p_n) * m_n];
                detail::packed_mac(u_p, m_fdaf.partition_spectrum(p), m_accum.data(), m_n);
            }
            m_fft.inverse(m_accum.data(), m_time.data());
            for (size_t i = 0; i < b; ++i) {
                e[i] = y[i] - m_time[i + b];
            }

            if (!m_fdaf.adapting()) {
                return;
            }

            // Slide e into the analysis window and re-fit the near-end model.
            const size_t w = m_e_window.size();
            for (size_t i = 0; i + b < w; ++i) {
                m_e_window[i] = m_e_window[i + b];
            }
            for (size_t i = 0; i < b; ++i) {
                m_e_window[w - b + i] = e[i];
            }
            m_predictor.analyze(m_e_window.data(), w);

            // Prewhiten both streams and adapt the FDAF on the pair.
            m_predictor.apply(m_u_state, u, m_u_pw.data(), b);
            m_predictor.apply(m_y_state, y, m_y_pw.data(), b);
            m_fdaf.process_block(m_u_pw.data(), m_y_pw.data(), m_e_pw.data());
        }

        /// Current feedback-path estimate as filter_length() time-domain taps.
        void copy_impulse_response(Sample* dest) noexcept { m_fdaf.copy_impulse_response(dest); }

      private:
        static config validated(const config& cfg) {
            if (cfg.analysis_window < 2 * cfg.fdaf.block_size) {
                throw std::invalid_argument("pem_afc: analysis_window must be >= 2 * block_size");
            }
            if (cfg.analysis_window % cfg.fdaf.block_size != 0) {
                throw std::invalid_argument("pem_afc: analysis_window must be a multiple of block_size");
            }
            return cfg;
        }

        config                    m_cfg;
        partitioned_fdaf<Sample>  m_fdaf; ///< adapts on the prewhitened pair; owns F_hat
        Predictor                 m_predictor;
        typename Predictor::state m_u_state;
        typename Predictor::state m_y_state;
        size_t                    m_n; ///< FFT size, 2 * block_size
        basic_real_fft<Sample>    m_fft;
        std::vector<Sample>       m_input; ///< sliding raw-u window
        std::vector<Sample>       m_u_raw; ///< raw input-spectrum ring
        std::vector<Sample>       m_accum;
        std::vector<Sample>       m_time;
        std::vector<Sample>       m_u_pw;
        std::vector<Sample>       m_y_pw;
        std::vector<Sample>       m_e_pw;
        std::vector<Sample>       m_e_window;
        size_t                    m_head = 0;
    };

} // namespace mutap
