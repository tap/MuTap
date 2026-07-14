// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// Deterministic block-synchronous simulation of the closed acoustic feedback
// loop (HANDOFF.md milestone M2):
//
//   y(n) = v(n) + F(q) u(n)     mic = near-end + true feedback path * speaker
//   e(n) = y(n) - F_hat(q) u(n) canceller output (e = y when open loop)
//   u(n) = clip(K * e(n - d))   forward path: gain K, delay d, speaker limit
//
// The forward delay must be at least one block so u for the current block
// depends only on already-computed error samples; the true-path convolution
// runs at sample resolution. Runs are exactly reproducible.
#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

#include "mutap/fdaf.h"
#include "mutap/fft.h"

namespace mutap_test {

    template <typename Sample>
    class closed_loop_sim {
      public:
        struct config {
            std::vector<Sample> feedback_path;          ///< F: true room path (RIR)
            size_t              block_size      = 64;   ///< must match the canceller's block size
            size_t              forward_delay   = 128;  ///< d in samples; >= block_size
            double              forward_gain_db = 0.0;  ///< K
            double              speaker_limit   = 1000; ///< hard clip on |u|
        };

        explicit closed_loop_sim(config cfg)
            : m_cfg(std::move(cfg))
            , m_u_work(m_cfg.feedback_path.size() - 1 + m_cfg.block_size)
            , m_e_hist(m_cfg.forward_delay)
            , m_u(m_cfg.block_size)
            , m_y(m_cfg.block_size)
            , m_e(m_cfg.block_size) {
            assert(!m_cfg.feedback_path.empty());
            assert(m_cfg.forward_delay >= m_cfg.block_size);
            set_forward_gain_db(m_cfg.forward_gain_db);
        }

        void set_forward_gain_db(double k_db) {
            m_cfg.forward_gain_db = k_db;
            m_gain                = std::pow(10.0, k_db / 20.0);
        }
        double forward_gain_db() const { return m_cfg.forward_gain_db; }
        size_t block_size() const { return m_cfg.block_size; }

        /// Advance the loop by one block of near-end input v. `canceller` may
        /// be null (open loop). Returns the RMS of the error/output block e
        /// (+inf if the loop has produced non-finite samples).
        double step(const Sample* v, mutap::partitioned_fdaf<Sample>* canceller) {
            const size_t b   = m_cfg.block_size;
            const size_t lf  = m_cfg.feedback_path.size();
            const double lim = m_cfg.speaker_limit;

            // Loudspeaker block from the delayed loop output (d >= b makes
            // every needed e sample come from earlier blocks), then clipped.
            for (size_t i = 0; i < b; ++i) {
                double u = m_gain * static_cast<double>(m_e_hist[i]);
                if (!(u >= -lim)) { // catches NaN too
                    u = -lim;
                }
                if (u > lim) {
                    u = lim;
                }
                m_u[i] = static_cast<Sample>(u);
            }

            // Mic block: y = v + F * u, at sample resolution.
            for (size_t i = 0; i < b; ++i) {
                m_u_work[lf - 1 + i] = m_u[i];
            }
            for (size_t i = 0; i < b; ++i) {
                double acc = 0.0;
                for (size_t k = 0; k < lf; ++k) {
                    acc += static_cast<double>(m_cfg.feedback_path[k]) * static_cast<double>(m_u_work[lf - 1 + i - k]);
                }
                m_y[i] = static_cast<Sample>(static_cast<double>(v[i]) + acc);
            }
            for (size_t i = 0; i + 1 < lf; ++i) { // slide u history
                m_u_work[i] = m_u_work[b + i];
            }

            // Canceller (or straight-through when open loop).
            if (canceller != nullptr) {
                canceller->process_block(m_u.data(), m_y.data(), m_e.data());
            }
            else {
                for (size_t i = 0; i < b; ++i) {
                    m_e[i] = m_y[i];
                }
            }

            // Slide the error history the forward delay taps from.
            for (size_t i = 0; i + b < m_cfg.forward_delay; ++i) {
                m_e_hist[i] = m_e_hist[i + b];
            }
            for (size_t i = 0; i < b; ++i) {
                m_e_hist[m_cfg.forward_delay - b + i] = m_e[i];
            }

            double rms = 0.0;
            for (size_t i = 0; i < b; ++i) {
                rms += static_cast<double>(m_e[i]) * static_cast<double>(m_e[i]);
            }
            rms = std::sqrt(rms / static_cast<double>(b));
            return std::isfinite(rms) ? rms : std::numeric_limits<double>::infinity();
        }

        const std::vector<Sample>& error_block() const { return m_e; }
        const std::vector<Sample>& speaker_block() const { return m_u; }

      private:
        config              m_cfg;
        double              m_gain = 1.0;
        std::vector<Sample> m_u_work; ///< u history + current block, for the F convolution
        std::vector<Sample> m_e_hist; ///< last forward_delay error samples
        std::vector<Sample> m_u;
        std::vector<Sample> m_y;
        std::vector<Sample> m_e;
    };

    // ---------------------------------------------------------------- signals

    template <typename Sample>
    std::vector<Sample> white_near_end(size_t n, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              v(n);
        for (auto& x : v) {
            x = static_cast<Sample>(dist(gen));
        }
        return v;
    }

    /// Strongly self-correlated near-end (three incommensurate sinusoids at
    /// unit total RMS plus a -40 dB noise floor) — the program material that
    /// biases a naive closed-loop adaptive estimate.
    template <typename Sample>
    std::vector<Sample> tonal_near_end(size_t n, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              v(n);
        const double                     amp = std::sqrt(2.0 / 3.0); // unit total RMS over 3 tones
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i);
            const double s = amp
                                 * (std::sin(0.031 * 2.0 * std::numbers::pi * t + 0.1)
                                    + std::sin(0.093 * 2.0 * std::numbers::pi * t + 1.3)
                                    + std::sin(0.171 * 2.0 * std::numbers::pi * t + 2.9))
                             + 0.01 * dist(gen);
            v[i] = static_cast<Sample>(s);
        }
        return v;
    }

    // ---------------------------------------------------------------- metrics

    /// Run the whole near-end signal through the loop; howling = any block
    /// RMS at or beyond `howl_rms` (40 dB over the unit-RMS near-end by
    /// default; the speaker clip keeps a howling run bounded above it).
    template <typename Sample>
    bool loop_howls(closed_loop_sim<Sample>& sim, mutap::partitioned_fdaf<Sample>* canceller,
                    const std::vector<Sample>& v, double howl_rms = 100.0) {
        const size_t b = sim.block_size();
        for (size_t blk = 0; blk + 1 <= v.size() / b; ++blk) {
            if (sim.step(&v[blk * b], canceller) >= howl_rms) {
                return true;
            }
        }
        return false;
    }

    /// MSG upper bound from the loop-magnitude condition: the broadband gain
    /// K at which max_w |K * F(w)| = 1. The delay-rich forward path makes the
    /// phase condition near-satisfiable at the peak, so a measured MSG should
    /// sit within a couple of dB of this.
    template <typename Sample>
    double theoretical_msg_db(const std::vector<Sample>& feedback_path) {
        constexpr size_t    k_fft = 8192;
        mutap::real_fft     fft(k_fft);
        std::vector<double> buf(k_fft, 0.0);
        for (size_t i = 0; i < feedback_path.size(); ++i) {
            buf[i] = static_cast<double>(feedback_path[i]);
        }
        fft.forward_inplace(buf.data());
        double peak = std::max(std::abs(buf[0]), std::abs(buf[1]));
        for (size_t k = 1; k < k_fft / 2; ++k) {
            const double mag = std::hypot(buf[2 * k], buf[2 * k + 1]);
            if (mag > peak) {
                peak = mag;
            }
        }
        return -20.0 * std::log10(peak);
    }

    /// Measure the loop's maximum stable gain by bisecting the forward gain:
    /// each probe rebuilds the loop (and copies `canceller_template` fresh,
    /// when given) and runs the whole near-end signal. Returns the largest
    /// probed gain (dB, within tol_db) that did not howl.
    template <typename Sample>
    double measured_msg_db(const typename closed_loop_sim<Sample>::config& loop_cfg,
                           const mutap::partitioned_fdaf<Sample>* canceller_template, const std::vector<Sample>& v,
                           double lo_db, double hi_db, double tol_db = 0.5) {
        auto howls_at = [&](double k_db) {
            auto cfg            = loop_cfg;
            cfg.forward_gain_db = k_db;
            closed_loop_sim<Sample> sim(std::move(cfg));
            if (canceller_template != nullptr) {
                auto canceller = *canceller_template; // fresh copy per probe
                return loop_howls(sim, &canceller, v);
            }
            return loop_howls<Sample>(sim, nullptr, v);
        };
        if (howls_at(lo_db)) {
            return lo_db; // caller picked lo too high; report the bound
        }
        while (hi_db - lo_db > tol_db) {
            const double mid = 0.5 * (lo_db + hi_db);
            if (howls_at(mid)) {
                hi_db = mid;
            }
            else {
                lo_db = mid;
            }
        }
        return lo_db;
    }

} // namespace mutap_test
