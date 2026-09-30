/// @file decorrelated_loop.h
/// @brief closed_loop_sim plus the two things a karaoke rig puts in the loop:
///        a forward-path decorrelator and an auxiliary speaker feed.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The stock simulator (support/closed_loop.h) models the loop a canceller
// alone has to hold: mic -> canceller -> gain and delay -> speaker -> room.
// Two things a real feedback rig also puts in that loop change the
// identification problem, and neither can be expressed there:
//
//   * a FORWARD-PATH DECORRELATOR. Frequency shifting the amplified signal
//     (Schroeder 1964) is the classic anti-howl trick; delay modulation is
//     its gentler cousin. Both break the correlation between the loudspeaker
//     signal and the near-end source, which is the very term that biases the
//     closed-loop estimate (see test_closed_loop.cpp) and the term PEM
//     prewhitening exists to remove. They are COMPLEMENTARY, not
//     alternatives - asserted in test_afc_decorrelation.cpp.
//
//   * an AUXILIARY SPEAKER FEED. A karaoke backing track is summed into the
//     loudspeaker signal after the loop gain, and it is uncorrelated with the
//     singer, so it is free excitation for identification - but only if the
//     canceller's reference is tapped AFTER the mix, which is what this
//     simulator models.
//
// Both sit between the loop gain and the loudspeaker, so the canceller's
// reference u is what the DAC actually plays, exactly as on the target.
//
// LOOP DELAY. `forward_delay` is the fixed delay from the canceller's output
// to the loudspeaker. In plain mode the loop is exactly closed_loop_sim's
// (L = K z^-d F, so exact_msg_db(F, d) is its open-loop limit). Delay
// modulation wobbles around d. The frequency shifter's delay does NOT come
// out of d: it is a causal IIR whose group delay depends on frequency, so a
// shifted loop is longer than a plain one at the same d, by 2.56 ms at
// 100 Hz, 1.39 ms at 200 Hz, 0.94 ms at 300 Hz (the held note) and 0.29 ms
// at 1 kHz (AfcDecorrelation.ShifterGroupDelayIsFrequencyDependent).
// Comparisons plain vs shifted at one d therefore include that extra delay;
// it is part of what the shifter costs a product.
//
// THE SHIFTER (iir_ssb_shifter, below) is a single-sideband frequency shift
// x cos(wt) - H{x} sin(wt) with the analytic pair from the library's IIR
// allpass-pair Hilbert transformer (tap::mu::allpass_hilbert, Olli
// Niemitalo's 4+4 design, in mutap/frequency_shifter.h). Image rejection of a
// +5 Hz shift, measured by AfcDecorrelation.ShifterIsSingleSideband: 44.3 dB
// at 30 Hz, 55.7 at 100 Hz, 44.8 at 300 Hz, 49.0 at 1 kHz, 46.8 at 5 kHz.
// It replaced the branch's 65-tap Hamming-windowed Hilbert FIR, which was
// partly double sideband at the held note (the anti-howl PoC's phase 0
// review measured its image rejection at 2.5 dB at 100 Hz and 7.7 dB at
// 300 Hz; the review's dl_iir.h is where these mechanics come from).

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include "closed_loop.h"
#include "mutap/frequency_shifter.h"

namespace mutap_test {

    /// What the forward path does to the loop signal before it reaches the
    /// loudspeaker.
    enum class forward_mode {
        plain,           ///< gain and delay only (matches closed_loop_sim)
        shift,           ///< SSB frequency shift by `shift_hz` (IIR allpass-pair Hilbert)
        delay_modulation ///< delay wobbled +-`depth`/2 samples at `rate_hz`
    };

    /// Single-sideband frequency shifter: the analytic pair from the
    /// library's allpass_hilbert (mutap/frequency_shifter.h: Olli
    /// Niemitalo's 4+4 IIR allpass pair), rotated by the caller's phase:
    /// out = re cos(theta) - im sin(theta). A positive phase ramp shifts
    /// every partial UP. Causal, with the frequency-dependent group delay
    /// the file comment gives.
    ///
    /// Why not tap::mu::frequency_shifter itself: the loop evaluates the
    /// rotation EXACTLY, cos and sin of the absolute-clock phase every
    /// sample, as the double golden model it is; the library's shifter runs
    /// a renormalized recursive oscillator instead (cheap and stable in
    /// float, but not bit-identical to the exact ramp), and every karaoke
    /// number measured on this loop was measured on the exact ramp. The
    /// Hilbert arithmetic is the library's, bit for bit.
    class iir_ssb_shifter {
      public:
        /// Advance one sample: x in, the shifted sample out.
        double process(double x, double theta) {
            double re = 0.0;
            double im = 0.0;
            m_pair.process(x, re, im);
            return re * std::cos(theta) - im * std::sin(theta);
        }

      private:
        tap::mu::allpass_hilbert<double> m_pair;
    };

    /// Loop with a decorrelator and an auxiliary speaker feed. Same contract
    /// as closed_loop_sim: step() advances one block and returns the RMS of
    /// the loop output (+inf once the loop has gone non-finite).
    template <typename Sample>
    class decorrelated_loop {
      public:
        struct config {
            std::vector<Sample> feedback_path;          ///< F: true room path (RIR)
            size_t              block_size      = 64;   ///< must match the canceller's
            size_t              forward_delay   = 128;  ///< d: canceller out to speaker, samples (see above)
            double              forward_gain_db = 0.0;  ///< K
            double              speaker_limit   = 1000; ///< hard clip on |u|
            double              sample_rate     = 48000.0;

            forward_mode mode     = forward_mode::plain;
            double       shift_hz = 5.0;  ///< frequency-shift mode
            double       depth    = 16.0; ///< delay-modulation mode, peak-to-peak samples
            double       rate_hz  = 1.3;  ///< delay-modulation LFO

            /// Summed into the loudspeaker after the loop gain (a backing
            /// track, or a shaped excitation signal); read cyclically, so a
            /// short buffer loops. Null for none.
            const std::vector<Sample>* aux      = nullptr;
            double                     aux_gain = 1.0;
        };

        explicit decorrelated_loop(config cfg)
            : m_cfg(std::move(cfg))
            , m_e_hist(k_history, Sample(0))
            , m_u_work(m_cfg.feedback_path.size() - 1 + m_cfg.block_size, Sample(0))
            , m_u(m_cfg.block_size)
            , m_y(m_cfg.block_size)
            , m_e(m_cfg.block_size)
            , m_aux(m_cfg.block_size, Sample(0)) {
            if (m_cfg.feedback_path.empty()) {
                throw std::invalid_argument("decorrelated_loop: empty feedback path");
            }
            // Delay modulation reads the error history up to depth/2 past the
            // nominal delay, and only samples older than one block exist yet.
            // The IIR shifter is causal and needs no lookahead.
            const size_t lookahead =
                (m_cfg.mode == forward_mode::delay_modulation) ? static_cast<size_t>(0.5 * m_cfg.depth + 1.0) : 0;
            if (m_cfg.forward_delay < m_cfg.block_size + lookahead) {
                throw std::invalid_argument("decorrelated_loop: forward_delay too short for this mode");
            }
            set_forward_gain_db(m_cfg.forward_gain_db);
        }

        void set_forward_gain_db(double k_db) {
            m_cfg.forward_gain_db = k_db;
            m_gain                = std::pow(10.0, k_db / 20.0);
        }
        double forward_gain_db() const { return m_cfg.forward_gain_db; }
        size_t block_size() const { return m_cfg.block_size; }

        /// Advance one block. `canceller` is any type with
        /// process_block(u, y, e); null runs the loop open.
        template <typename Canceller>
        double step(const Sample* v, Canceller* canceller) {
            const size_t b   = m_cfg.block_size;
            const size_t lf  = m_cfg.feedback_path.size();
            const double lim = m_cfg.speaker_limit;

            for (size_t i = 0; i < b; ++i) {
                const long long t = m_t + static_cast<long long>(i);
                // The forward signal is rounded to Sample before the gain,
                // as the loudspeaker path of a Sample-typed chain would be.
                const auto fwd = static_cast<Sample>(forward_sample(t));
                double     u   = m_gain * static_cast<double>(fwd);
                if (m_cfg.aux != nullptr && !m_cfg.aux->empty()) {
                    const double a =
                        m_cfg.aux_gain * static_cast<double>((*m_cfg.aux)[static_cast<size_t>(t) % m_cfg.aux->size()]);
                    m_aux[i] = static_cast<Sample>(a);
                    u += a;
                }
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

            if (canceller != nullptr) {
                canceller->process_block(m_u.data(), m_y.data(), m_e.data());
            }
            else {
                for (size_t i = 0; i < b; ++i) {
                    m_e[i] = m_y[i];
                }
            }

            double rms = 0.0;
            for (size_t i = 0; i < b; ++i) {
                m_e_hist[static_cast<size_t>(m_t + static_cast<long long>(i)) & k_history_mask] = m_e[i];
                rms += static_cast<double>(m_e[i]) * static_cast<double>(m_e[i]);
            }
            m_t += static_cast<long long>(b);
            rms = std::sqrt(rms / static_cast<double>(b));
            return std::isfinite(rms) ? rms : std::numeric_limits<double>::infinity();
        }

        const std::vector<Sample>& error_block() const { return m_e; }
        const std::vector<Sample>& speaker_block() const { return m_u; }
        const std::vector<Sample>& mic_block() const { return m_y; }
        /// The aux feed's contribution to the last speaker block (zeros without one).
        const std::vector<Sample>& aux_block() const { return m_aux; }

      private:
        static constexpr size_t k_history      = 8192; ///< power of two; >> forward_delay
        static constexpr size_t k_history_mask = k_history - 1;

        double e_at(long long t) const {
            return static_cast<double>(m_e_hist[static_cast<size_t>(t) & k_history_mask]);
        }

        double e_interpolated(double t) const {
            const long long i = static_cast<long long>(std::floor(t));
            const double    f = t - static_cast<double>(i);
            return (1.0 - f) * e_at(i) + f * e_at(i + 1);
        }

        /// The loop signal as it reaches the loudspeaker at absolute time t
        /// (called once per sample, in order: the shifter carries state).
        double forward_sample(long long t) {
            const double d = static_cast<double>(m_cfg.forward_delay);
            switch (m_cfg.mode) {
            case forward_mode::delay_modulation: {
                // Mean delay stays d; the wobble is +-depth/2 around it.
                const double mod =
                    0.5 * m_cfg.depth
                    * std::sin(2.0 * std::numbers::pi * m_cfg.rate_hz * static_cast<double>(t) / m_cfg.sample_rate);
                return e_interpolated(static_cast<double>(t) - d + mod);
            }
            case forward_mode::shift: {
                const double x = e_at(t - static_cast<long long>(d));
                const double theta =
                    2.0 * std::numbers::pi * m_cfg.shift_hz * static_cast<double>(t) / m_cfg.sample_rate;
                return m_shifter.process(x, theta);
            }
            case forward_mode::plain:
            default:
                return e_at(t - static_cast<long long>(d));
            }
        }

        config m_cfg;
        double m_gain = 1.0;
        /// Absolute sample clock, started mid-buffer so the first blocks'
        /// history reads land on the zero-filled past rather than wrapping.
        long long           m_t = static_cast<long long>(k_history) / 2;
        std::vector<Sample> m_e_hist;
        std::vector<Sample> m_u_work;
        std::vector<Sample> m_u;
        std::vector<Sample> m_y;
        std::vector<Sample> m_e;
        std::vector<Sample> m_aux; ///< the aux feed as played, this block
        iir_ssb_shifter     m_shifter;
    };

    /// loop_howls() for decorrelated_loop (same 40 dB-over-unit-RMS rule).
    template <typename Sample, typename Canceller>
    bool decorrelated_loop_howls(decorrelated_loop<Sample>& sim, Canceller* canceller, const std::vector<Sample>& v,
                                 double howl_rms = 100.0) {
        const size_t b = sim.block_size();
        for (size_t blk = 0; blk + 1 <= v.size() / b; ++blk) {
            if (sim.step(&v[blk * b], canceller) >= howl_rms) {
                return true;
            }
        }
        return false;
    }

    /// measured_msg_db() for decorrelated_loop: bisect the forward gain,
    /// rebuilding the loop and copying the canceller fresh for each probe.
    template <typename Sample, typename Canceller>
    double decorrelated_msg_db(const typename decorrelated_loop<Sample>::config& loop_cfg,
                               const Canceller* canceller_template, const std::vector<Sample>& v, double lo_db,
                               double hi_db, double tol_db = 0.5) {
        auto howls_at = [&](double k_db) {
            auto cfg            = loop_cfg;
            cfg.forward_gain_db = k_db;
            decorrelated_loop<Sample> sim(std::move(cfg));
            if (canceller_template != nullptr) {
                auto canceller = *canceller_template; // fresh copy per probe
                return decorrelated_loop_howls(sim, &canceller, v);
            }
            return decorrelated_loop_howls(sim, static_cast<Canceller*>(nullptr), v);
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
