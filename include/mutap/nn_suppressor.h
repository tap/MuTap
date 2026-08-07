/// @file nn_suppressor.h
/// @brief Learned residual-echo suppressor: a ~51k-parameter GRU predicting
///        per-band gains on the linear canceller's output (tools/ml).
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "mutap/fft.h"

namespace tap::mu {

    /// Geometry of the trained model (tools/ml/features.py is the source of
    /// truth; these constants and that file must change together).
    namespace nn_detail {
        inline constexpr size_t k_frame     = 128; ///< STFT frame (2 x block)
        inline constexpr size_t k_hop       = 64;  ///< canceller block size
        inline constexpr size_t k_bins      = k_frame / 2 + 1;
        inline constexpr size_t k_bands     = 22;
        inline constexpr size_t k_features  = 2 * k_bands;
        inline constexpr size_t k_dense     = 64;
        inline constexpr size_t k_gru       = 96;
        inline constexpr double k_rate      = 16000.0;
        inline constexpr double k_log_floor = 1e-10;
        inline constexpr double k_shift     = 5.0;
        inline constexpr double k_scale     = 5.0;
    } // namespace nn_detail

    /// Trained parameters, PyTorch layout/order (see tools/ml/nn.py):
    /// GRU gate order r, z, n in the stacked ih/hh matrices.
    struct nn_suppressor_weights {
        std::vector<float> dense_in_w;  ///< [64 x 44] row-major
        std::vector<float> dense_in_b;  ///< [64]
        std::vector<float> gru_w_ih;    ///< [288 x 64]
        std::vector<float> gru_w_hh;    ///< [288 x 96]
        std::vector<float> gru_b_ih;    ///< [288]
        std::vector<float> gru_b_hh;    ///< [288]
        std::vector<float> dense_out_w; ///< [22 x 96]
        std::vector<float> dense_out_b; ///< [22]

        bool valid() const {
            using namespace nn_detail;
            return dense_in_w.size() == k_dense * k_features && dense_in_b.size() == k_dense
                   && gru_w_ih.size() == 3 * k_gru * k_dense && gru_w_hh.size() == 3 * k_gru * k_gru
                   && gru_b_ih.size() == 3 * k_gru && gru_b_hh.size() == 3 * k_gru
                   && dense_out_w.size() == k_bands * k_gru && dense_out_b.size() == k_bands;
        }
    };

    /// Learned post-filter with the classical suppressor's contract:
    /// process_block(e, yhat, out) over the canceller's block size, one
    /// block of added latency (the analysis frame spans [previous block,
    /// current block] and synthesis overlap-adds at 50%), noexcept and
    /// allocation-free after construction.
    ///
    /// The analysis/synthesis and the network mirror tools/ml/features.py
    /// and tools/ml/nn.py to float precision (tools/ml/test_parity.py
    /// drives both on the same signals). The gain path is E-only: the
    /// echo estimate Yhat feeds the FEATURES, never the signal path, so
    /// the worst a bad prediction can do is mis-gain a band — the
    /// structural safety that motivates learning gains instead of a
    /// spectrum (RNNoise's central design decision).
    template <typename Sample>
    class nn_suppressor {
      public:
        struct config {
            size_t block_size = nn_detail::k_hop; ///< must equal the trained hop (64)
        };

        nn_suppressor(config cfg, nn_suppressor_weights weights)
            : m_w(std::move(weights))
            , m_fft(nn_detail::k_frame) {
            if (cfg.block_size != nn_detail::k_hop) {
                throw std::invalid_argument("nn_suppressor: block_size must be 64 (trained geometry)");
            }
            if (!m_w.valid()) {
                throw std::invalid_argument("nn_suppressor: weight dimensions do not match the architecture");
            }
            build_bands();
            const size_t f = nn_detail::k_frame;
            m_window.resize(f);
            for (size_t i = 0; i < f; ++i) {
                const double hann =
                    0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(f));
                m_window[i] = static_cast<Sample>(std::sqrt(hann));
            }
            m_prev_e.assign(nn_detail::k_hop, Sample(0));
            m_prev_yhat.assign(nn_detail::k_hop, Sample(0));
            m_overlap.assign(nn_detail::k_hop, Sample(0));
            m_spec_e.resize(f);
            m_spec_y.resize(f);
            m_time.resize(f);
            m_feat.assign(nn_detail::k_features, Sample(0));
            m_dense.assign(nn_detail::k_dense, Sample(0));
            m_gates.assign(3 * nn_detail::k_gru, Sample(0));
            m_gates_h.assign(3 * nn_detail::k_gru, Sample(0));
            m_state.assign(nn_detail::k_gru, Sample(0));
            m_gains.assign(nn_detail::k_bands, Sample(0));
            m_bin_gain.assign(nn_detail::k_bins, Sample(0));
        }

        size_t block_size() const noexcept { return nn_detail::k_hop; }

        /// One canceller block in, one (delayed) cleaned block out.
        void process_block(const Sample* e, const Sample* yhat_block, Sample* out) noexcept {
            using namespace nn_detail;

            // Analysis frames [prev, current], sqrt-Hann windowed.
            for (size_t i = 0; i < k_hop; ++i) {
                m_spec_e[i]         = m_prev_e[i] * m_window[i];
                m_spec_e[k_hop + i] = e[i] * m_window[k_hop + i];
                m_spec_y[i]         = m_prev_yhat[i] * m_window[i];
                m_spec_y[k_hop + i] = yhat_block[i] * m_window[k_hop + i];
            }
            m_fft.forward_inplace(m_spec_e.data());
            m_fft.forward_inplace(m_spec_y.data());

            // Features: normalized log10 band energies of E then Yhat.
            band_log_energies(m_spec_e.data(), m_feat.data());
            band_log_energies(m_spec_y.data(), m_feat.data() + k_bands);

            infer();

            // Band gains -> per-bin gains (band-weight-normalized), applied
            // to the E spectrum in packed layout.
            for (size_t k = 0; k < k_bins; ++k) {
                Sample g = Sample(0);
                for (size_t b = 0; b < k_bands; ++b) {
                    g += m_gains[b] * m_bmat[b * k_bins + k];
                }
                m_bin_gain[k] = g * m_bnorm[k];
            }
            m_spec_e[0] *= m_bin_gain[0];
            m_spec_e[1] *= m_bin_gain[k_bins - 1];
            for (size_t k = 1; k + 1 < k_bins; ++k) {
                m_spec_e[2 * k] *= m_bin_gain[k];
                m_spec_e[2 * k + 1] *= m_bin_gain[k];
            }

            // Synthesis: windowed inverse, overlap-add, emit first half.
            m_fft.inverse(m_spec_e.data(), m_time.data());
            for (size_t i = 0; i < k_hop; ++i) {
                out[i]       = m_overlap[i] + m_time[i] * m_window[i];
                m_overlap[i] = m_time[k_hop + i] * m_window[k_hop + i];
            }
            for (size_t i = 0; i < k_hop; ++i) {
                m_prev_e[i]    = e[i];
                m_prev_yhat[i] = yhat_block[i];
            }
        }

        void reset() noexcept {
            std::fill(m_prev_e.begin(), m_prev_e.end(), Sample(0));
            std::fill(m_prev_yhat.begin(), m_prev_yhat.end(), Sample(0));
            std::fill(m_overlap.begin(), m_overlap.end(), Sample(0));
            std::fill(m_state.begin(), m_state.end(), Sample(0));
        }

      private:
        static Sample sigmoid(Sample x) noexcept { return Sample(1) / (Sample(1) + std::exp(-x)); }

        /// ERB-spaced triangular band weights (features.py band_matrix()).
        void build_bands() {
            using namespace nn_detail;
            const auto          erb_rate = [](double f) { return 21.4 * std::log10(1.0 + 0.00437 * f); };
            const auto          erb_inv  = [](double r) { return (std::pow(10.0, r / 21.4) - 1.0) / 0.00437; };
            std::vector<double> edges(k_bands + 2);
            const double        top = erb_rate(k_rate / 2.0);
            for (size_t i = 0; i < edges.size(); ++i) {
                edges[i] = erb_inv(top * static_cast<double>(i) / static_cast<double>(k_bands + 1));
            }
            m_bmat.assign(k_bands * k_bins, Sample(0));
            for (size_t b = 0; b < k_bands; ++b) {
                const double lo  = edges[b];
                const double mid = edges[b + 1];
                const double hi  = edges[b + 2];
                for (size_t k = 0; k < k_bins; ++k) {
                    const double f = static_cast<double>(k) * k_rate / static_cast<double>(k_frame);
                    double       w = 0.0;
                    if (f > lo && f <= mid) {
                        w = (f - lo) / std::max(mid - lo, 1e-9);
                    }
                    else if (f > mid && f < hi) {
                        w = (hi - f) / std::max(hi - mid, 1e-9);
                    }
                    m_bmat[b * k_bins + k] = static_cast<Sample>(w);
                }
            }
            m_bmat[0] = Sample(1); // DC belongs to the first band
            m_bnorm.assign(k_bins, Sample(0));
            for (size_t k = 0; k < k_bins; ++k) {
                Sample s = Sample(0);
                for (size_t b = 0; b < k_bands; ++b) {
                    s += m_bmat[b * k_bins + k];
                }
                m_bnorm[k] = (s == Sample(0)) ? Sample(1) : Sample(1) / s;
            }
        }

        void band_log_energies(const Sample* packed, Sample* feat) noexcept {
            using namespace nn_detail;
            Sample bins[k_bins];
            bins[0]          = packed[0] * packed[0];
            bins[k_bins - 1] = packed[1] * packed[1];
            for (size_t k = 1; k + 1 < k_bins; ++k) {
                bins[k] = packed[2 * k] * packed[2 * k] + packed[2 * k + 1] * packed[2 * k + 1];
            }
            for (size_t b = 0; b < k_bands; ++b) {
                Sample acc = Sample(0);
                for (size_t k = 0; k < k_bins; ++k) {
                    acc += m_bmat[b * k_bins + k] * bins[k];
                }
                feat[b] = static_cast<Sample>((std::log10(static_cast<double>(acc) + k_log_floor) + k_shift) / k_scale);
            }
        }

        /// dense(tanh) -> GRU (PyTorch r,z,n convention) -> dense(sigmoid).
        void infer() noexcept {
            using namespace nn_detail;
            for (size_t i = 0; i < k_dense; ++i) {
                Sample acc = static_cast<Sample>(m_w.dense_in_b[i]);
                for (size_t j = 0; j < k_features; ++j) {
                    acc += static_cast<Sample>(m_w.dense_in_w[i * k_features + j]) * m_feat[j];
                }
                m_dense[i] = std::tanh(acc);
            }
            for (size_t i = 0; i < 3 * k_gru; ++i) {
                Sample acc = static_cast<Sample>(m_w.gru_b_ih[i]);
                for (size_t j = 0; j < k_dense; ++j) {
                    acc += static_cast<Sample>(m_w.gru_w_ih[i * k_dense + j]) * m_dense[j];
                }
                m_gates[i] = acc;
                acc        = static_cast<Sample>(m_w.gru_b_hh[i]);
                for (size_t j = 0; j < k_gru; ++j) {
                    acc += static_cast<Sample>(m_w.gru_w_hh[i * k_gru + j]) * m_state[j];
                }
                m_gates_h[i] = acc;
            }
            for (size_t i = 0; i < k_gru; ++i) {
                const Sample r = sigmoid(m_gates[i] + m_gates_h[i]);
                const Sample z = sigmoid(m_gates[k_gru + i] + m_gates_h[k_gru + i]);
                const Sample n = std::tanh(m_gates[2 * k_gru + i] + r * m_gates_h[2 * k_gru + i]);
                m_state[i]     = (Sample(1) - z) * n + z * m_state[i];
            }
            for (size_t b = 0; b < k_bands; ++b) {
                Sample acc = static_cast<Sample>(m_w.dense_out_b[b]);
                for (size_t j = 0; j < k_gru; ++j) {
                    acc += static_cast<Sample>(m_w.dense_out_w[b * k_gru + j]) * m_state[j];
                }
                m_gains[b] = sigmoid(acc);
            }
        }

        nn_suppressor_weights  m_w;
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
        std::vector<Sample>    m_feat;
        std::vector<Sample>    m_dense;
        std::vector<Sample>    m_gates;
        std::vector<Sample>    m_gates_h;
        std::vector<Sample>    m_state;
        std::vector<Sample>    m_gains;
        std::vector<Sample>    m_bin_gain;

      public:
        /// The gains the last block applied (diagnostics / tests).
        const std::vector<Sample>& band_gains() const noexcept { return m_gains; }
    };

    /// Load weights from the flat little-endian float32 file written by
    /// tools/ml/export_weights.py (magic "MUNN0001", then the eight arrays
    /// in declaration order). Host-side convenience; embedded targets embed
    /// the arrays directly.
    inline nn_suppressor_weights load_nn_suppressor_weights(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) {
            throw std::runtime_error("nn_suppressor: cannot open weights file");
        }
        char magic[8];
        if (std::fread(magic, 1, 8, f) != 8 || std::string_view(magic, 8) != "MUNN0001") {
            std::fclose(f);
            throw std::runtime_error("nn_suppressor: bad weights magic");
        }
        using namespace nn_detail;
        nn_suppressor_weights w;
        const auto            read = [&](std::vector<float>& dst, size_t n) {
            dst.resize(n);
            if (std::fread(dst.data(), sizeof(float), n, f) != n) {
                std::fclose(f);
                throw std::runtime_error("nn_suppressor: truncated weights file");
            }
        };
        read(w.dense_in_w, k_dense * k_features);
        read(w.dense_in_b, k_dense);
        read(w.gru_w_ih, 3 * k_gru * k_dense);
        read(w.gru_w_hh, 3 * k_gru * k_gru);
        read(w.gru_b_ih, 3 * k_gru);
        read(w.gru_b_hh, 3 * k_gru);
        read(w.dense_out_w, k_bands * k_gru);
        read(w.dense_out_b, k_bands);
        std::fclose(f);
        return w;
    }

} // namespace tap::mu
