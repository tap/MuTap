/// @file pem_afc.h
/// @brief PEM-prewhitened acoustic feedback canceller: the FDAF core of
///        fdaf.h adapting on prewhitened signals (FDAF-PEM-AFROW structure),
///        cancelling on the raw ones.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/fft.h"
#include "mutap/lpc.h"

// The ABI tag (mutap/fft.h): pem_afc holds a basic_real_fft<Sample> by value,
// so its layout follows the build's float FFT engine, and it is defined inside
// DspTap's inline namespace for that engine (fft_split_radix / fft_cmsis /
// fft_vdsp).
namespace tap::mu::inline TAP_DSP_FFT_ABI {

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
    /// The adaptive core is pluggable too: any type with partitioned_fdaf's
    /// interface surface (config with block_size/partitions, process_block,
    /// partition_spectrum, ...). Default is the NLMS core; pass
    /// tap::mu::partitioned_fdkf<Sample> for the frequency-domain Kalman
    /// update (fd_kalman.h) — the FDAF-PEM-AFROW structure is identical,
    /// only the update rule changes. The config field keeps the name `fdaf`
    /// across cores.
    ///
    /// Convergence statistics, raw, for a policy layer to threshold and
    /// combine: uncertainty_ratio() (the Kalman core's identification
    /// progress) and the opt-in shadow comparator (config::shadow_partitions,
    /// shadow_residual_ratio()). No single one serves every use; each
    /// getter records where it was measured to fail.
    ///
    /// Real-time contract: constructor allocates and may throw;
    /// process_block() and every other post-construction entry point are
    /// noexcept and allocation-free.
    template <typename Sample, typename Predictor = speech_predictor<Sample>, typename Core = partitioned_fdaf<Sample>>
    class pem_afc {
      public:
        struct config {
            typename Core::config      fdaf;
            typename Predictor::config predictor;
            size_t                     analysis_window = 1024; ///< samples of e per near-end re-fit
            /// SHADOW COMPARATOR (0 = off, the default; at most
            /// fdaf.partitions). When on, a small fast Kalman canceller —
            /// partitioned_fdkf<Sample> at the core's block size with
            /// shadow_partitions partitions and transition
            /// shadow_transition, every other field at its default —
            /// adapts on the SAME prewhitened pair (u', y') right after the
            /// core, whatever the core is. Both prewhitened residuals feed
            /// a smoothed mean-square power; shadow_residual_ratio() is
            /// main / shadow. This is the raw statistic of aec_chain's
            /// shadow trigger (postfilter.h), exposed WITHOUT a threshold:
            /// in this loop the measured operating window is about 1 dB
            /// wide (see shadow_residual_ratio()), so the threshold is a
            /// calibration and belongs to the policy layer. Cost when on:
            /// one shadow_partitions core per block. Measured with 2
            /// partitions at block 64 / 48 kHz, 1024-tap main, scalar
            /// x86-64 (i9-8950HK): 3158.7 ns per block in double and
            /// 2476.5 in float, against 324621.6 and 324455.2 for the
            /// whole process_block — about 1 %, the speech predictor
            /// dominates. When off: one untaken branch.
            size_t shadow_partitions = 0;
            /// The shadow's state transition A: fast enough that it never
            /// deep-converges and always tracks coarse (aec_chain's
            /// default, the value the experiment measured).
            Sample shadow_transition = Sample(0.999);
            /// One-pole per-block retention of the two residual powers, in
            /// [0, 1): beta = exp(-block_size / (fs * tau)). The default is
            /// tau = 51 ms at block 64 / 48 kHz, exp(-64 / 2448) = 0.97420
            /// (the experiment ran 0.974, aec_chain_preset's 0.9 at block
            /// 256 rescaled to block 64: tau 50.6 ms). Rescale it with the
            /// block size and sample rate to keep the physical time
            /// constant.
            Sample shadow_smoothing = Sample(0.9742);
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
            if (cfg.shadow_partitions > 0) {
                typename partitioned_fdkf<Sample>::config sc;
                sc.block_size = cfg.fdaf.block_size;
                sc.partitions = cfg.shadow_partitions;
                sc.transition = cfg.shadow_transition;
                m_shadow.emplace(sc);
                m_shadow_e.resize(cfg.fdaf.block_size);
            }
            reset();
        }

        size_t block_size() const noexcept { return m_fdaf.block_size(); }
        size_t partitions() const noexcept { return m_fdaf.partitions(); }
        size_t filter_length() const noexcept { return m_fdaf.filter_length(); }

        bool adapting() const noexcept { return m_fdaf.adapting(); }
        void set_adaptation(bool enabled) noexcept { m_fdaf.set_adaptation(enabled); }

        /// IPC of the PREWHITENED adaptation pair (see partitioned_fdaf);
        /// this staying low while the raw-pair IPC is high is exactly the
        /// bias reduction PEM buys (Gil-Cacho et al. 2014). Cores without
        /// the IPC machinery (the Kalman core, whose noise-PSD tracking
        /// makes the gate redundant) report 0.
        Sample ipc() const noexcept {
            if constexpr (requires(const Core& c) { c.ipc(); }) {
                return m_fdaf.ipc();
            }
            else {
                return Sample(0);
            }
        }

        Core&            fdaf() noexcept { return m_fdaf; }
        const Core&      fdaf() const noexcept { return m_fdaf; }
        const Predictor& predictor() const noexcept { return m_predictor; }

        /// The core's identification progress, sum P / sum P(reset)
        /// (partitioned_fdkf::uncertainty_ratio(), which records what it
        /// measured). Exists only for cores that keep a state uncertainty
        /// — the Kalman core; the NLMS core has none.
        Sample uncertainty_ratio() const noexcept
            requires requires(const Core& c) { c.uncertainty_ratio(); }
        {
            return m_fdaf.uncertainty_ratio();
        }

        /// True when config::shadow_partitions > 0 built the shadow.
        bool shadow_enabled() const noexcept { return m_shadow.has_value(); }

        /// Smoothed mean-square power of the core's PREWHITENED residual
        /// e' = y' - F_hat u' (the error the core adapts on, not the raw
        /// output e). Tracked only while the shadow is on; 0 otherwise.
        Sample residual_power() const noexcept { return m_pm; }

        /// Smoothed mean-square power of the shadow's prewhitened residual
        /// on the same (u', y'). 0 while the shadow is off.
        Sample shadow_residual_power() const noexcept { return m_ps; }

        /// residual_power() / shadow_residual_power(): below 1 while the
        /// long converged core out-cancels the short fast shadow, rising
        /// toward and past 1 when the shadow's fresh estimate beats it.
        /// Returns exactly 1 while the shadow is off and while both powers
        /// sit at or below 1e-30 (after reset(), and in digital silence),
        /// so a ratio of two vanishing powers never reads as an alarm;
        /// otherwise main / (shadow + 1e-30).
        ///
        /// Both powers — and so the ratio — update only on blocks that
        /// adapt: while adaptation is frozen (set_adaptation(false)),
        /// process_block returns before prewhitening, the shadow does not
        /// run, and the powers hold their last values.
        ///
        /// Measured (the convergence-indicator experiment: this class on
        /// the Kalman core in a closed loop at MSG - 6 dB, block 64 /
        /// 48 kHz, 1024 and 2048 taps, double and float, a 2-partition
        /// shadow; statistic "D" there, thresholded at a calibrated
        /// -1.235 dB with a 0.3 s hold):
        ///   - it detects a walk to a different room (F -> F2) at the
        ///     change block in 11 of 12, 13 of 14, 12 of 12 and 13 of 14
        ///     runs that were "ok" at the change (double 1024 / 2048,
        ///     float 1024 / 2048) — the experiment's only walk detector;
        ///   - 0 false alarms in 48 runs of near-end silence with the
        ///     backing track playing;
        ///   - it is blind to held-note bias: it claims "ok" in 72 of 72
        ///     held-note runs, because the main and the shadow are biased
        ///     alike and a comparator cannot see common-mode bias (steady
        ///     value median -2.21 dB there, range -2.98 to -1.53,
        ///     indistinguishable from converged voiced material's median
        ///     -2.04 dB, range -3.10 to -0.72);
        ///   - it is blind to a louder-coupling change (F -> 2F): 0
        ///     detections, the core re-tracks a louder copy of the path as
        ///     fast as the shadow does;
        ///   - it releases early after a walk: reconvergence reported a
        ///     median 0.40 to 0.47 s after the change, 0.13 to 0.26 s
        ///     before the misalignment oracle;
        ///   - its usable threshold window is about 1 dB wide: the near end
        ///     dominates the mic at every stable gain, so the ratio sits
        ///     close to 0 dB, and the steady states span -3.10 to -0.33 dB
        ///     (medians of the last 5 s, 216 runs). At 0 dB or at
        ///     aec_chain's +3 dB it detects 0 of 72 walks; at -2.0 dB it
        ///     never declares convergence in 12 of 50 cold starts.
        /// Hence no threshold here: the policy layer calibrates one, and
        /// pairs this with uncertainty_ratio(), which covers the held note
        /// and F -> 2F this cannot see.
        Sample shadow_residual_ratio() const noexcept {
            if (!m_shadow.has_value() || (!(m_pm > k_power_floor) && !(m_ps > k_power_floor))) {
                return Sample(1);
            }
            return m_pm / (m_ps + k_power_floor);
        }

        void reset() noexcept {
            m_fdaf.reset();
            if (m_shadow.has_value()) {
                m_shadow->reset();
            }
            m_pm = Sample(0);
            m_ps = Sample(0);
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
            if (m_shadow.has_value()) {
                update_shadow();
            }
        }

        /// Current feedback-path estimate as filter_length() time-domain taps.
        void copy_impulse_response(Sample* dest) noexcept { m_fdaf.copy_impulse_response(dest); }

        /// The echo estimate of the LAST processed block — block_size()
        /// time-domain samples of what the cancellation subtracted (the
        /// residual suppressor's reference, postfilter.h). Valid until
        /// the next process_block()/reset().
        const Sample* echo_estimate_block() const noexcept { return m_time.data() + block_size(); }

      private:
        /// Below this both residual powers count as zero (the ratio's
        /// digital-silence convention); a normal number in float as well.
        static constexpr Sample k_power_floor = Sample(1e-30);

        /// The shadow comparator's per-block work: the shadow adapts on the
        /// prewhitened pair the core just adapted on, then both residuals
        /// feed their smoothed mean-square powers. Reads m_e_pw only; the
        /// core, the cancellation and e are untouched.
        void update_shadow() noexcept {
            const size_t b = block_size();
            m_shadow->process_block(m_u_pw.data(), m_y_pw.data(), m_shadow_e.data());
            Sample m2 = Sample(0);
            Sample s2 = Sample(0);
            for (size_t i = 0; i < b; ++i) {
                m2 += m_e_pw[i] * m_e_pw[i];
                s2 += m_shadow_e[i] * m_shadow_e[i];
            }
            const Sample inv_b = Sample(1) / static_cast<Sample>(b);
            const Sample a     = Sample(1) - m_cfg.shadow_smoothing;
            m_pm += a * (m2 * inv_b - m_pm);
            m_ps += a * (s2 * inv_b - m_ps);
        }

        static config validated(const config& cfg) {
            // First, before the core is constructed: this class's own FFT
            // runs at 2 * block_size (the core's fft_size() for both shipped
            // cores), so the size gate is pem_afc's to state, whatever Core
            // checks for itself.
            fft_detail::checked_fft_size<Sample>(2 * cfg.fdaf.block_size,
                                                 "pem_afc: 2 * fdaf.block_size" MUTAP_FFT_SIZE_RANGES);
            if (cfg.analysis_window < 2 * cfg.fdaf.block_size) {
                throw std::invalid_argument("pem_afc: analysis_window must be >= 2 * block_size");
            }
            if (cfg.analysis_window % cfg.fdaf.block_size != 0) {
                throw std::invalid_argument("pem_afc: analysis_window must be a multiple of block_size");
            }
            // Validated whether or not the shadow is on, like the cores'
            // inactive fields.
            if (cfg.shadow_partitions > cfg.fdaf.partitions) {
                throw std::invalid_argument("pem_afc: shadow_partitions must be <= fdaf.partitions");
            }
            if (!(cfg.shadow_transition > Sample(0)) || !(cfg.shadow_transition <= Sample(1))) {
                throw std::invalid_argument("pem_afc: shadow_transition must be in (0, 1]");
            }
            if (!(cfg.shadow_smoothing >= Sample(0)) || !(cfg.shadow_smoothing < Sample(1))) {
                throw std::invalid_argument("pem_afc: shadow_smoothing must be in [0, 1)");
            }
            return cfg;
        }

        config                    m_cfg;
        Core                      m_fdaf; ///< adapts on the prewhitened pair; owns F_hat
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

        // The shadow comparator (config::shadow_partitions): engaged only
        // when on; m_shadow_e is block_size() then, empty otherwise.
        std::optional<partitioned_fdkf<Sample>> m_shadow;
        std::vector<Sample>                     m_shadow_e;       ///< the shadow's prewhitened residual
        Sample                                  m_pm = Sample(0); ///< smoothed main e' power
        Sample                                  m_ps = Sample(0); ///< smoothed shadow residual power
    };

} // namespace tap::mu::inline TAP_DSP_FFT_ABI
