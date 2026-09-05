/// @file nn_suppressor.h
/// @brief Learned residual-echo suppressor: a small GRU predicting per-band
///        gains on the linear canceller's output (tools/ml), with the
///        classical residual_suppressor's chain contract.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "mutap/fft.h"

namespace tap::mu {

    /// Analysis/network geometry of a trained model. Carried by the weights
    /// (the MUNN0002 header) rather than hardcoded: the same inference runs
    /// a 16 kHz/hop-64 model or a 48 kHz/hop-256 model, and the suppressor
    /// validates that the chain's block size equals the trained hop.
    /// tools/ml/features.py remains the source of truth for the analysis
    /// definitions; the two must change together.
    struct nn_geometry {
        double sample_rate = 16000.0;
        size_t hop         = 64; ///< canceller block size the model was trained at
        size_t bands       = 22; ///< ERB-spaced triangular bands
        size_t dense       = 64; ///< input projection width
        size_t gru         = 96; ///< recurrent state width

        size_t frame() const noexcept { return 2 * hop; }
        size_t bins() const noexcept { return hop + 1; }
        size_t features() const noexcept { return 2 * bands; }
    };

    /// Trained parameters, PyTorch layout/order (see tools/ml/nn.py):
    /// GRU gate order r, z, n in the stacked ih/hh matrices.
    struct nn_suppressor_weights {
        nn_geometry        geometry;
        std::vector<float> dense_in_w;  ///< [dense x features] row-major
        std::vector<float> dense_in_b;  ///< [dense]
        std::vector<float> gru_w_ih;    ///< [3*gru x dense]
        std::vector<float> gru_w_hh;    ///< [3*gru x gru]
        std::vector<float> gru_b_ih;    ///< [3*gru]
        std::vector<float> gru_b_hh;    ///< [3*gru]
        std::vector<float> dense_out_w; ///< [bands x gru]
        std::vector<float> dense_out_b; ///< [bands]

        bool valid() const {
            const nn_geometry& g = geometry;
            return g.hop >= 16 && (g.hop & (g.hop - 1)) == 0 && g.sample_rate > 0 && g.bands >= 4 && g.dense >= 1
                   && g.gru >= 1 && dense_in_w.size() == g.dense * g.features() && dense_in_b.size() == g.dense
                   && gru_w_ih.size() == 3 * g.gru * g.dense && gru_w_hh.size() == 3 * g.gru * g.gru
                   && gru_b_ih.size() == 3 * g.gru && gru_b_hh.size() == 3 * g.gru
                   && dense_out_w.size() == g.bands * g.gru && dense_out_b.size() == g.bands;
        }
    };

    namespace nn_detail {
        inline constexpr double k_log_floor = 1e-10;
        inline constexpr double k_shift     = 5.0; ///< feature = (log10(E)+shift)/scale
        inline constexpr double k_scale     = 5.0;
    } // namespace nn_detail

    /// Learned post-filter with the classical suppressor's contract:
    /// config-only construction (the weights ride in the config, so
    /// aec_chain<Sample, Canceller, nn_suppressor<Sample>> composes it like
    /// any other post), process_block(e, yhat, out) over the canceller's
    /// block size, one block of added latency (the analysis frame spans
    /// [previous block, current block] and synthesis overlap-adds at 50%),
    /// echo_explained() for the chain's receive guard and rescue, comfort
    /// noise shaped to a tracked noise floor, noexcept and allocation-free
    /// after construction.
    ///
    /// The analysis/synthesis and the network mirror tools/ml/features.py
    /// and tools/ml/nn.py to float precision (tools/ml/test_parity.py
    /// drives both on the same signals, in both profiles, in CI).
    ///
    /// Numeric contract: every dot product (dense_in, the GRU gates,
    /// dense_out) and every band energy accumulates in Sample. Double is
    /// the golden model; the float profile contains NO double arithmetic,
    /// so it runs natively on parts without FP64 (the Cortex-M33 of the
    /// RP2350 included) and never falls into soft-float. The float-tracks-
    /// double depth is pinned by tests/test_nn_suppressor.cpp
    /// (NnSuppressorCrossPrecision) and the promotion of these kernels into
    /// DspTap must keep it unchanged. The gain path is E-only: the
    /// echo estimate Yhat feeds the FEATURES, never the signal path, so
    /// the worst a bad prediction can do is mis-gain a band — the
    /// structural safety that motivates learning gains instead of a
    /// spectrum (RNNoise's central design decision).
    template <typename Sample>
    class nn_suppressor {
      public:
        struct config {
            /// Must equal the trained hop (aec_chain overwrites this with
            /// the canceller's block size; 0 = adopt the weights' hop).
            size_t block_size = 0;
            /// Refill suppressed bands to the tracked noise floor, so deep
            /// suppression settles at the room's floor instead of pumping
            /// toward digital silence (same contract as the classical
            /// suppressor's comfort noise).
            bool comfort_noise = true;
            /// Noise-floor tracker (two-window minimum statistics on the
            /// per-bin E PSD): one-pole PSD smoothing, window length in
            /// blocks, and the multiplicative bias that compensates the
            /// minimum statistic's downward bias. Defaults match the
            /// classical suppressor's at the 256/48k reference geometry.
            Sample floor_smoothing = Sample(0.9);
            size_t floor_window    = 128;
            Sample floor_bias      = Sample(4);
            /// One-pole smoothing of the echo_explained() accumulators.
            Sample explained_smoothing = Sample(0.95);
            /// The trained model.
            nn_suppressor_weights weights;
        };

        explicit nn_suppressor(config cfg)
            : m_cfg(validated(std::move(cfg)))
            , m_g(m_cfg.weights.geometry)
            , m_fft(m_g.frame()) {
            build_bands();
            const size_t f = m_g.frame();
            m_window.resize(f);
            for (size_t i = 0; i < f; ++i) {
                const double hann =
                    0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(f));
                m_window[i] = static_cast<Sample>(std::sqrt(hann));
            }
            m_prev_e.assign(m_g.hop, Sample(0));
            m_prev_yhat.assign(m_g.hop, Sample(0));
            m_overlap.assign(m_g.hop, Sample(0));
            m_spec_e.resize(f);
            m_spec_y.resize(f);
            m_time.resize(f);
            m_bin_pow.assign(m_g.bins(), Sample(0));
            m_feat.assign(m_g.features(), Sample(0));
            m_dense.assign(m_g.dense, Sample(0));
            m_gates.assign(3 * m_g.gru, Sample(0));
            m_gates_h.assign(3 * m_g.gru, Sample(0));
            m_state.assign(m_g.gru, Sample(0));
            m_gains.assign(m_g.bands, Sample(0));
            m_bin_gain.assign(m_g.bins(), Sample(0));
            m_floor.assign(m_g.bins(), Sample(0));
            m_min_cur.assign(m_g.bins(), std::numeric_limits<Sample>::max());
            m_min_prev.assign(m_g.bins(), Sample(0));
            reset();
        }

        size_t             block_size() const noexcept { return m_g.hop; }
        const nn_geometry& geometry() const noexcept { return m_g; }

        /// Fraction of mic power the echo estimate explains, the chain's
        /// guard/rescue statistic — same definition as the classical
        /// suppressor (smoothed Yhat power over smoothed mic power, where
        /// mic = E + Yhat), accumulated over this engine's analysis frames.
        Sample echo_explained() const noexcept { return m_syy / (m_sdd + std::numeric_limits<Sample>::epsilon()); }

        /// The gains the last block applied (diagnostics / tests).
        const std::vector<Sample>& band_gains() const noexcept { return m_gains; }

        void reset() noexcept {
            std::fill(m_prev_e.begin(), m_prev_e.end(), Sample(0));
            std::fill(m_prev_yhat.begin(), m_prev_yhat.end(), Sample(0));
            std::fill(m_overlap.begin(), m_overlap.end(), Sample(0));
            std::fill(m_state.begin(), m_state.end(), Sample(0));
            std::fill(m_gains.begin(), m_gains.end(), Sample(0));
            std::fill(m_bin_gain.begin(), m_bin_gain.end(), Sample(0));
            std::fill(m_floor.begin(), m_floor.end(), Sample(0));
            std::fill(m_min_cur.begin(), m_min_cur.end(), std::numeric_limits<Sample>::max());
            std::fill(m_min_prev.begin(), m_min_prev.end(), Sample(0));
            m_min_count = 0;
            m_syy       = Sample(0);
            m_sdd       = Sample(0);
            m_rng       = 0x2545F491U; // deterministic reset, like the classical engine
        }

        /// One canceller block in, one (delayed) cleaned block out.
        void process_block(const Sample* e, const Sample* yhat_block, Sample* out) noexcept {
            const size_t hop  = m_g.hop;
            const size_t bins = m_g.bins();

            // Analysis frames [prev, current], sqrt-Hann windowed.
            for (size_t i = 0; i < hop; ++i) {
                m_spec_e[i]       = m_prev_e[i] * m_window[i];
                m_spec_e[hop + i] = e[i] * m_window[hop + i];
                m_spec_y[i]       = m_prev_yhat[i] * m_window[i];
                m_spec_y[hop + i] = yhat_block[i] * m_window[hop + i];
            }
            m_fft.forward_inplace(m_spec_e.data());
            m_fft.forward_inplace(m_spec_y.data());

            // Features: normalized log10 band energies of E then Yhat.
            // (E's per-bin powers stay in m_bin_pow for the floor tracker.)
            band_log_energies(m_spec_e.data(), m_feat.data(), true);
            band_log_energies(m_spec_y.data(), m_feat.data() + m_g.bands, false);

            update_explained();
            update_floor();
            infer();

            // Band gains -> per-bin gains (band-weight-normalized), applied
            // to the E spectrum in packed layout, then comfort fill toward
            // the tracked floor in the bins the gain emptied.
            for (size_t k = 0; k < bins; ++k) {
                Sample g = Sample(0);
                for (size_t b = 0; b < m_g.bands; ++b) {
                    g += m_gains[b] * m_bmat[b * bins + k];
                }
                m_bin_gain[k] = g * m_bnorm[k];
            }
            m_spec_e[0] *= m_bin_gain[0];
            m_spec_e[1] *= m_bin_gain[bins - 1];
            for (size_t k = 1; k + 1 < bins; ++k) {
                m_spec_e[2 * k] *= m_bin_gain[k];
                m_spec_e[2 * k + 1] *= m_bin_gain[k];
            }
            if (m_cfg.comfort_noise) {
                comfort_fill();
            }

            // Synthesis: windowed inverse, overlap-add, emit first half.
            m_fft.inverse(m_spec_e.data(), m_time.data());
            for (size_t i = 0; i < hop; ++i) {
                out[i]       = m_overlap[i] + m_time[i] * m_window[i];
                m_overlap[i] = m_time[hop + i] * m_window[hop + i];
            }
            for (size_t i = 0; i < hop; ++i) {
                m_prev_e[i]    = e[i];
                m_prev_yhat[i] = yhat_block[i];
            }
        }

      private:
        static config validated(config cfg) {
            if (!cfg.weights.valid()) {
                throw std::invalid_argument("nn_suppressor: weight dimensions do not match their geometry");
            }
            if (cfg.block_size != 0 && cfg.block_size != cfg.weights.geometry.hop) {
                throw std::invalid_argument("nn_suppressor: block_size must equal the trained hop");
            }
            if (cfg.floor_smoothing < Sample(0) || cfg.floor_smoothing >= Sample(1)
                || cfg.explained_smoothing < Sample(0) || cfg.explained_smoothing >= Sample(1)) {
                throw std::invalid_argument("nn_suppressor: smoothing constants must be in [0, 1)");
            }
            if (cfg.floor_window == 0 || cfg.floor_bias <= Sample(0)) {
                throw std::invalid_argument("nn_suppressor: floor_window and floor_bias must be positive");
            }
            return cfg;
        }

        static Sample sigmoid(Sample x) noexcept { return Sample(1) / (Sample(1) + std::exp(-x)); }

        /// ERB-spaced triangular band weights (features.py band_matrix()).
        void build_bands() {
            const size_t        bins     = m_g.bins();
            const size_t        bands    = m_g.bands;
            const auto          erb_rate = [](double f) { return 21.4 * std::log10(1.0 + 0.00437 * f); };
            const auto          erb_inv  = [](double r) { return (std::pow(10.0, r / 21.4) - 1.0) / 0.00437; };
            std::vector<double> edges(bands + 2);
            const double        top = erb_rate(m_g.sample_rate / 2.0);
            for (size_t i = 0; i < edges.size(); ++i) {
                edges[i] = erb_inv(top * static_cast<double>(i) / static_cast<double>(bands + 1));
            }
            // The top edge is fs/2 EXACTLY (features.py band_edges_hz): the ERB
            // round trip lands ~1e-11 below it at 48 kHz and above it at 16 kHz,
            // and used to decide whether the Nyquist bin was covered at all —
            // notching it at 48 kHz in C++ while the Python reference kept it.
            edges.back() = m_g.sample_rate / 2.0;
            m_bmat.assign(bands * bins, Sample(0));
            for (size_t b = 0; b < bands; ++b) {
                const double lo  = edges[b];
                const double mid = edges[b + 1];
                const double hi  = edges[b + 2];
                for (size_t k = 0; k < bins; ++k) {
                    const double f = static_cast<double>(k) * m_g.sample_rate / static_cast<double>(m_g.frame());
                    double       w = 0.0;
                    if (f > lo && f <= mid) {
                        w = (f - lo) / std::max(mid - lo, 1e-9);
                    }
                    else if (f > mid && f < hi) {
                        w = (hi - f) / std::max(hi - mid, 1e-9);
                    }
                    m_bmat[b * bins + k] = static_cast<Sample>(w);
                }
            }
            m_bmat[0]                             = Sample(1); // DC belongs to the first band
            m_bmat[(bands - 1) * bins + bins - 1] = Sample(1); // and the Nyquist bin to the last
            m_bnorm.assign(bins, Sample(0));
            for (size_t k = 0; k < bins; ++k) {
                Sample s = Sample(0);
                for (size_t b = 0; b < bands; ++b) {
                    s += m_bmat[b * bins + k];
                }
                m_bnorm[k] = (s == Sample(0)) ? Sample(1) : Sample(1) / s;
            }
        }

        /// Per-bin |spec|^2 (kept in m_bin_pow when keep_bins), then band
        /// energies -> normalized log features.
        void band_log_energies(const Sample* packed, Sample* feat, bool keep_bins) noexcept {
            const size_t bins = m_g.bins();
            Sample*      p    = m_scratch_pow.data();
            p[0]              = packed[0] * packed[0];
            p[bins - 1]       = packed[1] * packed[1];
            for (size_t k = 1; k + 1 < bins; ++k) {
                p[k] = packed[2 * k] * packed[2 * k] + packed[2 * k + 1] * packed[2 * k + 1];
            }
            if (keep_bins) {
                std::copy(p, p + bins, m_bin_pow.begin());
            }
            for (size_t b = 0; b < m_g.bands; ++b) {
                Sample acc = Sample(0);
                for (size_t k = 0; k < bins; ++k) {
                    acc += m_bmat[b * bins + k] * p[k];
                }
                feat[b] = static_cast<Sample>(
                    (std::log10(static_cast<double>(acc) + nn_detail::k_log_floor) + nn_detail::k_shift)
                    / nn_detail::k_scale);
            }
        }

        /// Smoothed Yhat-power over mic-power (mic = E + Yhat, per bin in
        /// the packed layout), the guard/rescue statistic.
        void update_explained() noexcept {
            const size_t bins = m_g.bins();
            Sample       syy  = Sample(0);
            Sample       sdd  = Sample(0);
            const Sample d0   = m_spec_e[0] + m_spec_y[0];
            const Sample dn   = m_spec_e[1] + m_spec_y[1];
            syy += m_spec_y[0] * m_spec_y[0] + m_spec_y[1] * m_spec_y[1];
            sdd += d0 * d0 + dn * dn;
            for (size_t k = 1; k + 1 < bins; ++k) {
                const Sample yr = m_spec_y[2 * k];
                const Sample yi = m_spec_y[2 * k + 1];
                const Sample dr = m_spec_e[2 * k] + yr;
                const Sample di = m_spec_e[2 * k + 1] + yi;
                syy += yr * yr + yi * yi;
                sdd += dr * dr + di * di;
            }
            const Sample a = m_cfg.explained_smoothing;
            m_syy          = a * m_syy + (Sample(1) - a) * syy;
            m_sdd          = a * m_sdd + (Sample(1) - a) * sdd;
        }

        /// Two-window minimum statistics on the per-bin E PSD (m_bin_pow),
        /// same scheme as the classical engine: track the minimum of a
        /// smoothed PSD over two half-windows; the floor is the biased
        /// minimum of the completed and the running window.
        void update_floor() noexcept {
            const size_t bins = m_g.bins();
            const Sample a    = m_cfg.floor_smoothing;
            for (size_t k = 0; k < bins; ++k) {
                m_floor[k]   = a * m_floor[k] + (Sample(1) - a) * m_bin_pow[k];
                m_min_cur[k] = std::min(m_min_cur[k], m_floor[k]);
            }
            if (++m_min_count >= m_cfg.floor_window) {
                m_min_count = 0;
                std::copy(m_min_cur.begin(), m_min_cur.end(), m_min_prev.begin());
                std::fill(m_min_cur.begin(), m_min_cur.end(), std::numeric_limits<Sample>::max());
            }
        }

        Sample next_noise() noexcept { // xorshift32 in [-1, 1]
            m_rng ^= m_rng << 13;
            m_rng ^= m_rng >> 17;
            m_rng ^= m_rng << 5;
            return Sample(2) * (static_cast<Sample>(m_rng) / Sample(4294967296.0)) - Sample(1);
        }

        /// Refill what the gains removed up to the tracked noise floor:
        /// per bin, add noise with power max(0, N_k - G_k^2 |E_k|^2) in the
        /// same windowed-spectrum units the floor was tracked in.
        void comfort_fill() noexcept {
            const size_t bins = m_g.bins();
            for (size_t k = 0; k < bins; ++k) {
                const Sample n_k     = m_cfg.floor_bias * std::min(m_min_prev[k], m_min_cur[k]);
                const Sample kept    = m_bin_gain[k] * m_bin_gain[k] * m_bin_pow[k];
                const Sample deficit = n_k - kept;
                if (deficit <= Sample(0)) {
                    continue;
                }
                // Unit-power complex noise scaled to the deficit (real-only
                // at DC/Nyquist).
                const Sample amp = std::sqrt(deficit);
                if (k == 0) {
                    m_spec_e[0] += amp * next_noise();
                }
                else if (k == bins - 1) {
                    m_spec_e[1] += amp * next_noise();
                }
                else {
                    const Sample nr   = next_noise();
                    const Sample ni   = next_noise();
                    const Sample norm = std::sqrt(nr * nr + ni * ni) + std::numeric_limits<Sample>::epsilon();
                    m_spec_e[2 * k] += amp * nr / norm;
                    m_spec_e[2 * k + 1] += amp * ni / norm;
                }
            }
        }

        /// dense(tanh) -> GRU (PyTorch r,z,n convention) -> dense(sigmoid).
        void infer() noexcept {
            const nn_suppressor_weights& w  = m_cfg.weights;
            const size_t                 nf = m_g.features();
            const size_t                 nd = m_g.dense;
            const size_t                 ng = m_g.gru;
            for (size_t i = 0; i < nd; ++i) {
                Sample acc = static_cast<Sample>(w.dense_in_b[i]);
                for (size_t j = 0; j < nf; ++j) {
                    acc += static_cast<Sample>(w.dense_in_w[i * nf + j]) * m_feat[j];
                }
                m_dense[i] = std::tanh(acc);
            }
            for (size_t i = 0; i < 3 * ng; ++i) {
                Sample acc = static_cast<Sample>(w.gru_b_ih[i]);
                for (size_t j = 0; j < nd; ++j) {
                    acc += static_cast<Sample>(w.gru_w_ih[i * nd + j]) * m_dense[j];
                }
                m_gates[i] = acc;
                acc        = static_cast<Sample>(w.gru_b_hh[i]);
                for (size_t j = 0; j < ng; ++j) {
                    acc += static_cast<Sample>(w.gru_w_hh[i * ng + j]) * m_state[j];
                }
                m_gates_h[i] = acc;
            }
            for (size_t i = 0; i < ng; ++i) {
                const Sample r = sigmoid(m_gates[i] + m_gates_h[i]);
                const Sample z = sigmoid(m_gates[ng + i] + m_gates_h[ng + i]);
                const Sample n = std::tanh(m_gates[2 * ng + i] + r * m_gates_h[2 * ng + i]);
                m_state[i]     = (Sample(1) - z) * n + z * m_state[i];
            }
            for (size_t b = 0; b < m_g.bands; ++b) {
                Sample acc = static_cast<Sample>(w.dense_out_b[b]);
                for (size_t j = 0; j < ng; ++j) {
                    acc += static_cast<Sample>(w.dense_out_w[b * ng + j]) * m_state[j];
                }
                m_gains[b] = sigmoid(acc);
            }
        }

        config                 m_cfg;
        nn_geometry            m_g;
        basic_real_fft<Sample> m_fft;
        std::vector<Sample>    m_window;
        std::vector<Sample>    m_bmat;  ///< [bands x bins] triangular weights
        std::vector<Sample>    m_bnorm; ///< per-bin 1/sum(band weights)
        std::vector<Sample>    m_prev_e;
        std::vector<Sample>    m_prev_yhat;
        std::vector<Sample>    m_overlap;
        std::vector<Sample>    m_spec_e;
        std::vector<Sample>    m_spec_y;
        std::vector<Sample>    m_time;
        std::vector<Sample>    m_bin_pow; ///< current frame per-bin |E|^2
        std::vector<Sample>    m_scratch_pow = std::vector<Sample>(m_g.bins());
        std::vector<Sample>    m_feat;
        std::vector<Sample>    m_dense;
        std::vector<Sample>    m_gates;
        std::vector<Sample>    m_gates_h;
        std::vector<Sample>    m_state;
        std::vector<Sample>    m_gains;
        std::vector<Sample>    m_bin_gain;
        std::vector<Sample>    m_floor;    ///< smoothed per-bin E PSD
        std::vector<Sample>    m_min_cur;  ///< running half-window minimum
        std::vector<Sample>    m_min_prev; ///< completed half-window minimum
        size_t                 m_min_count = 0;
        Sample                 m_syy       = Sample(0);
        Sample                 m_sdd       = Sample(0);
        std::uint32_t          m_rng       = 0x2545F491U;
    };

    /// Parse weights from an in-memory MUNN image (the format
    /// tools/ml/export_weights.py writes). MUNN0002 carries the geometry
    /// (five uint32: sample_rate, hop, bands, dense, gru) before the eight
    /// float32 arrays in declaration order; legacy MUNN0001 images imply
    /// the original 16 kHz / hop-64 / 22-band / 64-dense / 96-GRU
    /// geometry. This is the loader an embedded default uses (the package
    /// ships the image as a byte array).
    inline nn_suppressor_weights parse_nn_suppressor_weights(const unsigned char* data, size_t size) {
        size_t     pos  = 0;
        const auto take = [&](void* dst, size_t n) {
            if (pos + n > size) {
                throw std::runtime_error("nn_suppressor: truncated weights image");
            }
            std::memcpy(dst, data + pos, n);
            pos += n;
        };
        char magic[8];
        take(magic, 8);
        nn_suppressor_weights  w;
        const std::string_view tag(magic, 8);
        if (tag == "MUNN0002") {
            std::uint32_t g[5];
            take(g, sizeof g);
            w.geometry = {static_cast<double>(g[0]), g[1], g[2], g[3], g[4]};
        }
        else if (tag != "MUNN0001") {
            throw std::runtime_error("nn_suppressor: bad weights magic");
        }
        const nn_geometry& g    = w.geometry;
        const auto         read = [&](std::vector<float>& dst, size_t n) {
            dst.resize(n);
            take(dst.data(), n * sizeof(float));
        };
        read(w.dense_in_w, g.dense * g.features());
        read(w.dense_in_b, g.dense);
        read(w.gru_w_ih, 3 * g.gru * g.dense);
        read(w.gru_w_hh, 3 * g.gru * g.gru);
        read(w.gru_b_ih, 3 * g.gru);
        read(w.gru_b_hh, 3 * g.gru);
        read(w.dense_out_w, g.bands * g.gru);
        read(w.dense_out_b, g.bands);
        if (!w.valid()) {
            throw std::runtime_error("nn_suppressor: weights fail geometry validation");
        }
        return w;
    }

    /// Load weights from a MUNN file on disk (host-side convenience).
    inline nn_suppressor_weights load_nn_suppressor_weights(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) {
            throw std::runtime_error("nn_suppressor: cannot open weights file");
        }
        std::fseek(f, 0, SEEK_END);
        const long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> image(static_cast<size_t>(bytes > 0 ? bytes : 0));
        const size_t               got = std::fread(image.data(), 1, image.size(), f);
        std::fclose(f);
        if (got != image.size()) {
            throw std::runtime_error("nn_suppressor: short read on weights file");
        }
        return parse_nn_suppressor_weights(image.data(), image.size());
    }

} // namespace tap::mu
