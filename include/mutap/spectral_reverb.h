/// @file spectral_reverb.h
/// @brief A per-bin block-frequency-domain reverb for afc_chain's reverb
///        slot, flat or with its decay shaped by the identified feedback path.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHAT. One complex one-pole per frequency bin on weighted-overlap-add
// spectra (Hann analysis window of 2 * block_size, hop block_size,
// COLA-exact, no synthesis window):
//
//   S_k[m] = a_k S_k[m - 1] + X_k[m]
//   Y_k[m] = (1 - w) X_k[m] + w_k S_k[m]
//
// so the dry path is the input delayed by exactly one block (latency()) and
// each bin rings down with its own pole a_k. Flat, every a_k gives the
// configured rt60 and every w_k is the wet level w. SHAPED (from_path), the
// per-bin allowance (median|P| / |P_k|)^shape, clamped to [shape_min,
// shape_max], scales w_k up and the decay TIME up in the bins where the
// shaping input |P| is weak, and down where it is strong; the w_k are then
// renormalised so that sum_k w_k^2 equals the flat shape's (the decay times
// are not renormalised, so a shaped reverb's output energy is not the flat
// one's). P is the feedback path the canceller identified, F_hat
// (copy_impulse_response()), or for several microphones on one bus the
// coherent sum of their F_hat (reshape_from_impulse_responses()).
//
// A MEASURED NEGATIVE RESULT. Kept so the measurements that reject it can
// be re-run, not as a product stage: docs/reverb-afc.md, "Spectral reverb"
// (simulation, one host: macOS x86_64, AppleClang 17, Release). Behind PEM +
// FD-Kalman, six band-limited rooms, five seed sets, the held note, it costs
// +2.83 to +30.57 dB of runaway gain (median over the rooms, flat and shaped
// with shape_max 1, 2, 4, wet 0.15 and 0.30, S1 and S3) where the vendored
// Dattorro plate at the same wet costs -1.37 to +0.88; per seed, the
// plate's chain holds more in every room's median for 22 of the 24 spectral
// rows compared, and in 5 of 6 rooms for the other two. Shaping helps only at
// shape_max 1 and never reaches the plate. Without a canceller the flat
// reverb's limit is 17 to 32 dB below the dry room's (a bank of high-Q
// resonators), and shaping does not make it safe. At equal wet it is also
// 4.3 to 14.8 dB louder than the plate's mix. Use the plate
// (reverb_stage.h's reverb_mix over a mono-in reverb).
//
// LATENCY. latency() is block_size: the whole output (dry included) is the
// input one block late. On a chain the block comes out of the latency
// budget (afc_chain's output delay line), which is how the measurements
// above budgeted it: the loop's total delay is the plates' rows'.
//
// THE ABI TAG. The class holds a basic_real_fft<Sample> by value, so its
// layout follows the build's float FFT engine: it is defined inside
// tap::mu::inline TAP_DSP_FFT_ABI (mutap/fft.h; HANDOFF working note 6 (a)),
// and tests/test_fft_engine_contract.cpp pins it as the sixth embedder.
// Never forward-declare it in plain tap::mu.
//
// THE SIZE GATE. The transform size 2 * block_size comes from
// configuration, so it passes through fft_detail::checked_fft_size before any
// buffer is sized from it: CMSIS-DSP on the Cortex-M55 supports 32 ... 4096
// only, and an unchecked size there is a release-mode HardFault (working
// note 6 (b)). block_size 16 ... 2048 on that target.
//
// Real-time contract (as fd_kalman.h): the constructor validates its config
// (std::invalid_argument), allocates every buffer and may throw.
// process_block() and every other post-construction entry point are
// noexcept and allocation-free, reshape*() included (std::nth_element and
// std::pow on preallocated storage). Arithmetic is in Sample throughout:
// float32 is first-class and no double runs in the float hot path.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "mutap/fft.h"

namespace tap::mu {

    /// How a spectral_reverb's per-bin decay and wet gain are set.
    enum class spectral_shaping {
        flat,     ///< every bin at the configured rt60 and wet; reshape*() is ignored
        from_path ///< reshape*() shapes the bins from a feedback-path magnitude
    };

} // namespace tap::mu

// Inside the tag: the class holds a basic_real_fft<Sample> by value.
namespace tap::mu::inline TAP_DSP_FFT_ABI {

    /// Weighted-overlap-add reverb with an independent one-pole decay per
    /// frequency bin (see the file comment). An afc_stage: process_block(in,
    /// out, n) with n a multiple of block_size().
    template <typename Sample>
    class spectral_reverb {
      public:
        struct config {
            size_t           block_size  = 64;            ///< hop; a power of two >= 4 with 2 * block_size an FFT size
            Sample           sample_rate = Sample(48000); ///< Hz, for the rt60 conversion
            Sample           rt60        = Sample(1);     ///< seconds to -60 dB per bin, before shaping
            Sample           wet         = Sample(0.15);  ///< w in [0, 1]; the dry gain is 1 - w
            spectral_shaping shaping     = spectral_shaping::flat;
            /// Shaping exponent (from_path): allowance = (median|P| / |P_k|)^shape.
            Sample shape = Sample(1);
            /// Floor and ceiling on the per-bin allowance: a bin's wet gain
            /// scales by it (before the renormalisation) and its decay time
            /// by it, so shape_max bounds how much longer and louder the
            /// quietest bins of P ring than the flat reverb.
            Sample shape_min = Sample(0.05);
            Sample shape_max = Sample(4);
        };

        /// @throws std::invalid_argument on a bad config, the FFT size gate
        ///         included (before any buffer is sized from it).
        explicit spectral_reverb(const config& cfg)
            : m_cfg(validated(cfg))
            , m_window(fft_detail::checked_fft_size<Sample>(2 * cfg.block_size,
                                                            "spectral_reverb: 2 * block_size" MUTAP_FFT_SIZE_RANGES))
            , m_bins(cfg.block_size + 1)
            , m_fft(m_window)
            , m_analysis(m_window)
            , m_frame(m_window, Sample(0))
            , m_spectrum(m_window, Sample(0))
            , m_time(m_window, Sample(0))
            , m_tail(m_window, Sample(0))
            , m_state(m_window, Sample(0))
            , m_decay(m_bins, Sample(0))
            , m_wet(m_bins, Sample(0))
            , m_scratch(m_bins, Sample(0)) {
            for (size_t i = 0; i < m_window; ++i) {
                m_analysis[i] = Sample(0.5)
                                - Sample(0.5)
                                      * std::cos(Sample(2) * std::numbers::pi_v<Sample>
                                                 * static_cast<Sample>(i) / static_cast<Sample>(m_window));
            }
            reshape_flat();
        }

        size_t block_size() const noexcept { return m_cfg.block_size; }
        /// The transform size, 2 * block_size().
        size_t window_size() const noexcept { return m_window; }
        /// Delay of the whole output, dry path included, in samples.
        size_t latency() const noexcept { return m_cfg.block_size; }
        /// Shaping bins: DC, block_size - 1 complex bins, Nyquist; bin k at
        /// k * sample_rate / window_size() Hz.
        size_t           bins() const noexcept { return m_bins; }
        Sample           wet() const noexcept { return m_cfg.wet; }
        Sample           rt60() const noexcept { return m_cfg.rt60; }
        spectral_shaping shaping() const noexcept { return m_cfg.shaping; }
        Sample           shape_max() const noexcept { return m_cfg.shape_max; }
        /// Bin k's pole per hop and wet gain as currently shaped. @pre k < bins()
        Sample bin_decay(size_t k) const noexcept { return m_decay[k]; }
        Sample bin_wet(size_t k) const noexcept { return m_wet[k]; }

        /// Wet level in [0, 1], keeping the shape (the per-bin wet gains
        /// scale with it) and the tail. Returns false (and changes nothing)
        /// for a value outside [0, 1] or not finite.
        bool set_wet(Sample wet) noexcept {
            if (!std::isfinite(wet) || wet < Sample(0) || wet > Sample(1)) {
                return false;
            }
            const Sample old = m_cfg.wet;
            m_cfg.wet        = wet;
            if (old > Sample(0)) {
                const Sample scale = wet / old;
                for (auto& w : m_wet) {
                    w *= scale;
                }
            }
            else {
                reshape_flat(); // no shape survives a zero wet level
            }
            return true;
        }

        /// Silence the tail and the analysis history; keeps the shape.
        void reset() noexcept {
            std::fill(m_frame.begin(), m_frame.end(), Sample(0));
            std::fill(m_tail.begin(), m_tail.end(), Sample(0));
            std::fill(m_state.begin(), m_state.end(), Sample(0));
        }

        /// Every bin at the configured rt60 and wet level.
        void reshape_flat() noexcept {
            const Sample a = flat_pole();
            for (size_t k = 0; k < m_bins; ++k) {
                m_decay[k] = a;
                m_wet[k]   = m_cfg.wet;
            }
        }

        /// Shape from a per-bin magnitude of the path: `magnitude` holds
        /// `bins` values, bins() of them. Ignored (false) in flat mode, for a
        /// null pointer or a wrong count.
        bool reshape(const Sample* magnitude, size_t bins) noexcept {
            if (m_cfg.shaping != spectral_shaping::from_path || magnitude == nullptr || bins != m_bins) {
                return false;
            }
            std::copy(magnitude, magnitude + m_bins, m_scratch.begin());
            apply_shape();
            return true;
        }

        /// Shape from the canceller's F_hat as copy_impulse_response() hands
        /// it over (`taps` samples). The response is folded onto the
        /// window_size() grid, which samples its transfer function exactly
        /// at the bins() frequencies. Ignored (false) in flat mode or while
        /// the response is still all zero.
        bool reshape_from_impulse_response(const Sample* ir, size_t taps) noexcept {
            const Sample* const irs[1] = {ir};
            return reshape_from_impulse_responses(irs, 1, taps);
        }

        /// Shape from the coherent bus sum |sum_m F_hat_m| of several
        /// microphones' paths (`paths` responses of `taps` samples each):
        /// the path the bus sees when every mic is summed into it.
        bool reshape_from_impulse_responses(const Sample* const* irs, size_t paths, size_t taps) noexcept {
            if (m_cfg.shaping != spectral_shaping::from_path || irs == nullptr || paths == 0 || taps == 0) {
                return false;
            }
            for (size_t m = 0; m < paths; ++m) {
                if (irs[m] == nullptr) {
                    return false;
                }
            }
            std::fill(m_spectrum.begin(), m_spectrum.end(), Sample(0));
            for (size_t m = 0; m < paths; ++m) {
                for (size_t i = 0; i < taps; ++i) {
                    m_spectrum[i % m_window] += irs[m][i];
                }
            }
            Sample energy = Sample(0);
            for (const Sample x : m_spectrum) {
                energy += x * x;
            }
            if (!(energy > k_tiny)) {
                return false; // nothing identified yet: keep the current shape
            }
            m_fft.forward_inplace(m_spectrum.data());
            m_scratch[0]          = std::abs(m_spectrum[0]);
            m_scratch[m_bins - 1] = std::abs(m_spectrum[1]);
            for (size_t k = 1; k + 1 < m_bins; ++k) {
                m_scratch[k] = std::hypot(m_spectrum[2 * k], m_spectrum[2 * k + 1]);
            }
            apply_shape();
            return true;
        }

        /// The afc_stage process: `n` samples in, `n` out, delayed by
        /// latency(). @pre n is a multiple of block_size() (afc_chain passes
        /// exactly block_size()); a remainder is written as silence. In
        /// place (in == out) is allowed.
        void process_block(const Sample* in, Sample* out, size_t n) noexcept {
            const size_t b = m_cfg.block_size;
            size_t       i = 0;
            for (; i + b <= n; i += b) {
                hop(in + i, out + i);
            }
            for (; i < n; ++i) {
                out[i] = Sample(0);
            }
        }

      private:
        static constexpr Sample k_tiny = Sample(1e-12);

        static config validated(const config& cfg) {
            if (cfg.block_size < 4 || (cfg.block_size & (cfg.block_size - 1)) != 0) {
                throw std::invalid_argument("spectral_reverb: block_size must be a power of two >= 4");
            }
            if (!std::isfinite(cfg.sample_rate) || !(cfg.sample_rate > Sample(0))) {
                throw std::invalid_argument("spectral_reverb: sample_rate must be finite and > 0");
            }
            if (!std::isfinite(cfg.rt60) || !(cfg.rt60 > Sample(0))) {
                throw std::invalid_argument("spectral_reverb: rt60 must be finite and > 0");
            }
            if (!std::isfinite(cfg.wet) || cfg.wet < Sample(0) || cfg.wet > Sample(1)) {
                throw std::invalid_argument("spectral_reverb: wet must be finite and in [0, 1]");
            }
            if (cfg.shaping != spectral_shaping::flat && cfg.shaping != spectral_shaping::from_path) {
                throw std::invalid_argument("spectral_reverb: unknown shaping mode");
            }
            if (!std::isfinite(cfg.shape) || cfg.shape < Sample(0)) {
                throw std::invalid_argument("spectral_reverb: shape must be finite and >= 0");
            }
            if (!std::isfinite(cfg.shape_max) || !(cfg.shape_min > Sample(0)) || !(cfg.shape_max >= cfg.shape_min)) {
                throw std::invalid_argument("spectral_reverb: need 0 < shape_min <= shape_max, finite");
            }
            return cfg;
        }

        /// Per-hop pole giving the configured rt60 (-60 dB) before shaping.
        Sample flat_pole() const noexcept {
            const Sample hops = m_cfg.rt60 * m_cfg.sample_rate / static_cast<Sample>(m_cfg.block_size);
            return std::pow(Sample(10), Sample(-3) / std::max(hops, Sample(1)));
        }

        /// The allowance per bin from the magnitudes in m_scratch (which it
        /// overwrites while taking their median).
        void apply_shape() noexcept {
            // m_spectrum is free here: keep the magnitudes there while
            // m_scratch is reordered for the median.
            std::copy(m_scratch.begin(), m_scratch.end(), m_spectrum.begin());
            const size_t mid = m_bins / 2;
            std::nth_element(m_scratch.begin(), m_scratch.begin() + static_cast<std::ptrdiff_t>(mid), m_scratch.end());
            const Sample median = std::max(m_scratch[mid], k_tiny);

            const Sample a   = flat_pole();
            Sample       sum = Sample(0);
            for (size_t k = 0; k < m_bins; ++k) {
                const Sample ratio     = median / std::max(m_spectrum[k], k_tiny);
                const Sample allowance = std::clamp(std::pow(ratio, m_cfg.shape), m_cfg.shape_min, m_cfg.shape_max);
                m_wet[k]               = m_cfg.wet * allowance;
                m_decay[k]             = std::pow(a, Sample(1) / allowance);
                sum += m_wet[k] * m_wet[k];
            }
            // Renormalise to the flat shape's sum of squared wet gains.
            const Sample target = static_cast<Sample>(m_bins) * m_cfg.wet * m_cfg.wet;
            const Sample norm   = (sum > k_tiny) ? std::sqrt(target / sum) : Sample(1);
            for (auto& w : m_wet) {
                w *= norm;
            }
        }

        /// One hop: block_size samples in, block_size out.
        void hop(const Sample* in, Sample* out) noexcept {
            const size_t b = m_cfg.block_size;
            for (size_t i = 0; i < m_window - b; ++i) {
                m_frame[i] = m_frame[i + b];
            }
            for (size_t i = 0; i < b; ++i) {
                m_frame[m_window - b + i] = in[i];
            }
            for (size_t i = 0; i < m_window; ++i) {
                m_spectrum[i] = m_frame[i] * m_analysis[i];
            }
            m_fft.forward_inplace(m_spectrum.data());

            const Sample dry = Sample(1) - m_cfg.wet;
            // DC and Nyquist are real, in slots 0 and 1 (the packed layout).
            m_state[0]    = m_decay[0] * m_state[0] + m_spectrum[0];
            m_state[1]    = m_decay[m_bins - 1] * m_state[1] + m_spectrum[1];
            m_spectrum[0] = dry * m_spectrum[0] + m_wet[0] * m_state[0];
            m_spectrum[1] = dry * m_spectrum[1] + m_wet[m_bins - 1] * m_state[1];
            for (size_t k = 1; k + 1 < m_bins; ++k) {
                const Sample a  = m_decay[k];
                const Sample w  = m_wet[k];
                const size_t re = 2 * k;
                const size_t im = re + 1;
                m_state[re]     = a * m_state[re] + m_spectrum[re];
                m_state[im]     = a * m_state[im] + m_spectrum[im];
                m_spectrum[re]  = dry * m_spectrum[re] + w * m_state[re];
                m_spectrum[im]  = dry * m_spectrum[im] + w * m_state[im];
            }

            m_fft.inverse(m_spectrum.data(), m_time.data());
            for (size_t i = 0; i < m_window; ++i) {
                m_tail[i] += m_time[i];
            }
            for (size_t i = 0; i < b; ++i) {
                out[i] = m_tail[i];
            }
            for (size_t i = 0; i < m_window - b; ++i) {
                m_tail[i] = m_tail[i + b];
            }
            for (size_t i = m_window - b; i < m_window; ++i) {
                m_tail[i] = Sample(0);
            }
        }

        config                 m_cfg;
        size_t                 m_window; ///< 2 * block_size, through the size gate
        size_t                 m_bins;
        basic_real_fft<Sample> m_fft;
        std::vector<Sample>    m_analysis; ///< Hann, m_window
        std::vector<Sample>    m_frame;    ///< sliding time-domain window
        std::vector<Sample>    m_spectrum; ///< analysis / synthesis scratch
        std::vector<Sample>    m_time;     ///< inverse-FFT scratch
        std::vector<Sample>    m_tail;     ///< overlap-add tail
        std::vector<Sample>    m_state;    ///< one complex one-pole per bin (packed)
        std::vector<Sample>    m_decay;    ///< per-bin pole per hop
        std::vector<Sample>    m_wet;      ///< per-bin wet gain
        std::vector<Sample>    m_scratch;  ///< magnitudes / median workspace
    };

} // namespace tap::mu::inline TAP_DSP_FFT_ABI
