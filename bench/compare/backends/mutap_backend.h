// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// MuTap subjects for the comparison harness. Three variants, so the
// head-to-head can separate "the linear canceller" from "the shipped
// product" and float32 (deployment / WebRTC's domain) from float64
// (MuTap's quality ceiling):
//
//   mutap        — the certified aec_chain<double> (raw FD-Kalman
//                  canceller + residual suppressor + comfort noise +
//                  receive guard) built from aec_chain_preset, the exact
//                  configuration docs/itu-compliance.md certifies. The
//                  product, at its reference precision.
//   mutap-f32    — the same chain at float32: the number a deployment
//                  actually runs, and the honest peer for WebRTC AEC3.
//   mutap-linear — the raw partitioned_fdkf canceller ALONE (no
//                  suppressor / comfort noise / guard): the linear-stage
//                  peer for "WebRTC linear AEC only" style comparisons.
//
// Frame = the certified block (256). Filter length follows the certified
// geometry: 2048 taps at 48 kHz, 1024 at 16 kHz, scaled by rate
// elsewhere (partitions = filter_len / block).
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"

#include "../aec_backend.h"

namespace mutap_compare {

    namespace detail {
        // Certified filter length in samples for a given rate: ~43 ms at
        // 48 kHz (2048), 64 ms at 16 kHz (1024) — the two shapes the
        // compliance suite pins; other rates hold ~43 ms.
        inline std::size_t certified_taps(double fs, std::size_t block) {
            const double ms      = (fs <= 24000.0) ? 64.0 : 43.0;
            std::size_t  taps    = static_cast<std::size_t>(fs * ms / 1000.0 + 0.5);
            // round up to a whole number of blocks, >= 1 partition
            std::size_t  parts   = (taps + block - 1) / block;
            return parts < 1 ? block : parts * block;
        }
    } // namespace detail

    // The full certified chain at precision Sample.
    template <typename Sample>
    class mutap_chain_backend final : public aec_backend {
      public:
        explicit mutap_chain_backend(double fs, std::size_t block = 256)
            : m_fs(fs)
            , m_block(block)
            , m_partitions(detail::certified_taps(fs, block) / block)
            , m_chain(make_chain(fs, block, m_partitions))
            , m_x(block)
            , m_y(block)
            , m_e(block) {}

        std::string name() const override {
            return std::is_same_v<Sample, double> ? "mutap" : "mutap-f32";
        }
        std::size_t frame() const override { return m_block; }
        // One block of send-path hop, plus one more for the suppressor's
        // constrained causal gain filter (documented in the C ABI header).
        double latency_ms() const override { return 2.0 * 1000.0 * static_cast<double>(m_block) / m_fs; }
        void   reset() override { m_chain.reset(); }

        void process(const float* far, const float* mic, float* out) override {
            for (std::size_t i = 0; i < m_block; ++i) {
                m_x[i] = static_cast<Sample>(far[i]);
                m_y[i] = static_cast<Sample>(mic[i]);
            }
            m_chain.process_block(m_x.data(), m_y.data(), m_e.data());
            for (std::size_t i = 0; i < m_block; ++i) {
                out[i] = static_cast<float>(m_e[i]);
            }
        }

      private:
        static mutap::aec_chain<Sample> make_chain(double fs, std::size_t block, std::size_t partitions) {
            auto cfg = mutap::aec_chain_preset<Sample>(block, partitions, fs);
            return mutap::aec_chain<Sample>(cfg);
        }

        double                    m_fs;
        std::size_t               m_block;
        std::size_t               m_partitions;
        mutap::aec_chain<Sample>  m_chain;
        std::vector<Sample>       m_x, m_y, m_e;
    };

    // The raw FD-Kalman canceller alone — the linear stage, no post.
    template <typename Sample>
    class mutap_linear_backend final : public aec_backend {
      public:
        explicit mutap_linear_backend(double fs, std::size_t block = 256)
            : m_fs(fs)
            , m_block(block)
            , m_canceller(make_canceller(block, detail::certified_taps(fs, block) / block))
            , m_x(block)
            , m_y(block)
            , m_e(block)
            , m_yhat(block) {}

        std::string name() const override { return "mutap-linear"; }
        std::size_t frame() const override { return m_block; }
        double      latency_ms() const override { return 1000.0 * static_cast<double>(m_block) / m_fs; }
        void        reset() override { m_canceller.reset(); }

        void process(const float* far, const float* mic, float* out) override {
            for (std::size_t i = 0; i < m_block; ++i) {
                m_x[i] = static_cast<Sample>(far[i]);
                m_y[i] = static_cast<Sample>(mic[i]);
            }
            m_canceller.process_block(m_x.data(), m_y.data(), m_e.data(), m_yhat.data());
            for (std::size_t i = 0; i < m_block; ++i) {
                out[i] = static_cast<float>(m_e[i]);
            }
        }

      private:
        static mutap::partitioned_fdkf<Sample> make_canceller(std::size_t block, std::size_t partitions) {
            typename mutap::partitioned_fdkf<Sample>::config cfg;
            cfg.block_size          = block;
            cfg.partitions          = partitions;
            cfg.transition          = Sample(0.9998);
            cfg.initial_uncertainty = Sample(10);
            return mutap::partitioned_fdkf<Sample>(cfg);
        }

        double                          m_fs;
        std::size_t                     m_block;
        mutap::partitioned_fdkf<Sample> m_canceller;
        std::vector<Sample>             m_x, m_y, m_e, m_yhat;
    };

    // Registration (definitions in mutap_backend.cpp).
    void register_mutap_backends();

} // namespace mutap_compare
