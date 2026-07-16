// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
// Deterministic block-synchronous simulation of the OPEN-loop echo problem
// (AEC — HANDOFF.md "The next effort", Stage 1):
//
//   d(n) = F(q) x(n)              echo: true path * far-end reference
//   y(n) = v(n) + d(n)            mic = near-end (double-talk) + echo
//   e(n) = y(n) - F_hat(q) x(n)   canceller output
//
// The closed-loop simulator (closed_loop.h) exists because feedback creates
// its own input; echo has no loop — the far-end reference x is exogenous —
// so this harness is deliberately the simple half of that file: convolve,
// add the near-end, hand the pair to the canceller.
//
// Because the simulator knows d and v separately, it can report what an
// observable ERLE cannot: the TRUE residual echo e - v, uncontaminated by
// the near-end. run_aec() returns both — erle_db (mic vs output power, the
// ratio a deployed canceller could measure) equals the true suppression
// only during far-end single talk; suppression_db (true echo vs residual
// echo) is the one that stays meaningful under double-talk.
#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <vector>

#include "mutap/fdaf.h"

namespace mutap_test {

    /// Normalized coefficient error ||f - f_hat||^2 / ||f||^2 in dB; the
    /// estimate is zero-padded/truncated to the truth's length.
    template <typename Sample>
    double misalignment_db(const std::vector<Sample>& truth, const std::vector<Sample>& estimate) {
        double num = 0.0;
        double den = 0.0;
        for (size_t i = 0; i < truth.size(); ++i) {
            const double t = static_cast<double>(truth[i]);
            const double e = (i < estimate.size()) ? static_cast<double>(estimate[i]) : 0.0;
            num += (t - e) * (t - e);
            den += t * t;
        }
        return 10.0 * std::log10(num / den);
    }

    template <typename Sample>
    class echo_sim {
      public:
        struct config {
            std::vector<Sample> echo_path;       ///< F: true speaker->mic path (RIR)
            size_t              block_size = 64; ///< must match the canceller's block size
        };

        explicit echo_sim(config cfg)
            : m_cfg(std::move(cfg))
            , m_x_work(m_cfg.echo_path.size() - 1 + m_cfg.block_size)
            , m_d(m_cfg.block_size)
            , m_y(m_cfg.block_size)
            , m_e(m_cfg.block_size) {
            assert(!m_cfg.echo_path.empty());
        }

        size_t block_size() const { return m_cfg.block_size; }

        /// Per-block energies (sums of squares, block-size samples each) for
        /// the caller to window and ratio as it sees fit.
        struct block_energies {
            double mic      = 0.0; ///< sum y^2 (echo + near-end)
            double echo     = 0.0; ///< sum d^2 (true echo alone)
            double error    = 0.0; ///< sum e^2 (canceller output)
            double residual = 0.0; ///< sum (e - v)^2 (true residual echo)
        };

        /// Advance the scenario by one block: x is the far-end reference,
        /// v the near-end source (nullptr = silent, far-end single talk).
        /// `canceller` is any type with process_block(x, y, e) over
        /// block_size samples (mutap::partitioned_fdaf, mutap::pem_afc, ...)
        /// and may be null (e = y, the uncancelled mic).
        template <typename Canceller>
        block_energies step(const Sample* x, const Sample* v, Canceller* canceller) {
            const size_t b  = m_cfg.block_size;
            const size_t lf = m_cfg.echo_path.size();

            // Echo block d = F * x at sample resolution.
            for (size_t i = 0; i < b; ++i) {
                m_x_work[lf - 1 + i] = x[i];
            }
            for (size_t i = 0; i < b; ++i) {
                double acc = 0.0;
                for (size_t k = 0; k < lf; ++k) {
                    acc += static_cast<double>(m_cfg.echo_path[k]) * static_cast<double>(m_x_work[lf - 1 + i - k]);
                }
                m_d[i] = static_cast<Sample>(acc);
                m_y[i] = static_cast<Sample>((v != nullptr ? static_cast<double>(v[i]) : 0.0) + acc);
            }
            for (size_t i = 0; i + 1 < lf; ++i) { // slide x history
                m_x_work[i] = m_x_work[b + i];
            }

            if (canceller != nullptr) {
                canceller->process_block(x, m_y.data(), m_e.data());
            }
            else {
                for (size_t i = 0; i < b; ++i) {
                    m_e[i] = m_y[i];
                }
            }

            block_energies out;
            for (size_t i = 0; i < b; ++i) {
                const double y = static_cast<double>(m_y[i]);
                const double d = static_cast<double>(m_d[i]);
                const double e = static_cast<double>(m_e[i]);
                const double r = e - (v != nullptr ? static_cast<double>(v[i]) : 0.0);
                out.mic += y * y;
                out.echo += d * d;
                out.error += e * e;
                out.residual += r * r;
            }
            return out;
        }

        /// Open-loop overload: a literal `nullptr` cannot deduce Canceller.
        block_energies step(const Sample* x, const Sample* v, std::nullptr_t) {
            return step(x, v, static_cast<mutap::partitioned_fdaf<Sample>*>(nullptr));
        }

        const std::vector<Sample>& error_block() const { return m_e; }
        const std::vector<Sample>& echo_block() const { return m_d; }

        /// Replace the echo path with one of the SAME length (keeps the
        /// input history), for time-variant-path scenarios (ITU Stage 1:
        /// the rotating-reflector analogue rebuilds the path per block).
        void set_echo_path(const Sample* path, size_t n) {
            assert(n == m_cfg.echo_path.size());
            std::copy(path, path + n, m_cfg.echo_path.begin());
        }

      private:
        config              m_cfg;
        std::vector<Sample> m_x_work; ///< x history + current block, for the F convolution
        std::vector<Sample> m_d;
        std::vector<Sample> m_y;
        std::vector<Sample> m_e;
    };

    // ---------------------------------------------------------------- metrics

    struct aec_run_result {
        /// Observable ERLE, 10 log10(sum y^2 / sum e^2) over the measurement
        /// window: what a deployed canceller could report. Equals the true
        /// suppression only when the near-end is silent over the window.
        double erle_db = 0.0;
        /// True echo suppression, 10 log10(sum d^2 / sum (e - v)^2) over the
        /// window: residual echo measured against the echo alone, valid
        /// under double-talk (simulation-only — needs d and v separately).
        double suppression_db = 0.0;
        /// +inf residual/error anywhere in the run (divergence sentinel).
        bool finite = true;
    };

    /// Run the far-end signal x (and near-end v, elementwise-aligned with x;
    /// nullptr = single talk) through the scenario; energies accumulate from
    /// `measure_from_block` onward (0 = whole run, convergence included).
    template <typename Sample, typename Canceller>
    aec_run_result run_aec(echo_sim<Sample>& sim, const std::vector<Sample>& x,
                           const std::vector<std::type_identity_t<Sample>>* v, Canceller* canceller,
                           size_t measure_from_block = 0) {
        const size_t b = sim.block_size();
        assert(v == nullptr || v->size() >= x.size());

        double mic      = 0.0;
        double echo     = 0.0;
        double error    = 0.0;
        double residual = 0.0;
        bool   finite   = true;
        for (size_t blk = 0; blk + 1 <= x.size() / b; ++blk) {
            const auto en = sim.step(&x[blk * b], v != nullptr ? &(*v)[blk * b] : nullptr, canceller);
            if (!std::isfinite(en.error) || !std::isfinite(en.residual)) {
                finite = false;
                continue;
            }
            if (blk >= measure_from_block) {
                mic += en.mic;
                echo += en.echo;
                error += en.error;
                residual += en.residual;
            }
        }

        aec_run_result out;
        out.finite         = finite;
        out.erle_db        = 10.0 * std::log10(mic / error);
        out.suppression_db = 10.0 * std::log10(echo / residual);
        return out;
    }

    /// Single-talk overload: a literal `nullptr` canceller cannot deduce.
    template <typename Sample>
    aec_run_result run_aec(echo_sim<Sample>& sim, const std::vector<Sample>& x,
                           const std::vector<std::type_identity_t<Sample>>* v, std::nullptr_t,
                           size_t                                           measure_from_block = 0) {
        return run_aec(sim, x, v, static_cast<mutap::partitioned_fdaf<Sample>*>(nullptr), measure_from_block);
    }

} // namespace mutap_test
