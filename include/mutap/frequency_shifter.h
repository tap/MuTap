/// @file frequency_shifter.h
/// @brief Single-sideband frequency shifter for the feedback loop's forward
///        path: an IIR allpass-pair Hilbert transformer and a quadrature
///        oscillator.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHY. Shifting every partial of the amplified signal by a few hertz
// (Schroeder 1964) is the classic anti-howl decorrelator: the loudspeaker
// signal stops being a delayed copy of the near-end source, which breaks the
// correlation that biases a closed-loop canceller's estimate and moves a
// ringing mode off its own resonance on every trip round the loop. The shift
// has to be SINGLE sideband across the voice band: a shifter that also emits
// the image at f - shift feeds part of every partial straight back onto a
// neighbour of itself. A 65-tap Hilbert FIR cannot get there at 48 kHz (the
// anti-howl PoC's phase 0 measured its image rejection at 2.5 dB at 100 Hz
// and 7.7 dB at 300 Hz); the IIR allpass pair below does, at a
// frequency-dependent group delay.
//
// HOW. out = re cos(theta) - im sin(theta), where (re, im) is the analytic
// pair of the input and theta advances by 2 pi shift_hz / sample_rate per
// sample: every partial at f comes out at f + shift_hz (a negative shift
// moves it down).
//
//   * The analytic pair (allpass_hilbert) is Olli Niemitalo's 4+4
//     "polyphase IIR" 90-degree phase-difference network: two chains of four
//     2nd-order allpass sections in z^-2, y = c^2 (x + y[n-2]) - x[n-2], the
//     quadrature chain followed by one sample of delay. Coefficients and
//     their provenance: THIRD_PARTY_NOTICES.md.
//   * The oscillator is a recursive quadrature rotator (4 multiplies, 2 adds
//     a sample), renormalized every k_renorm_interval samples by one Newton
//     step towards unit magnitude. Chosen over a float phase accumulator with
//     sin/cos at 64-sample block boundaries (and the same rotator within a
//     block) on a scratch measurement, float32 at 48 kHz over 10 minutes:
//     the rotator held its magnitude within 1.450e-6 of unity at +-5 Hz
//     (1.587e-6 at +2 Hz; unrenormalized it ended at 0.625 at +-5 Hz) and
//     its phase within 7.585e-6 rad of the exact ramp, while the phase
//     accumulator costs a sin/cos pair per block and its phase wandered
//     1.073e-2 rad at +-5 Hz. Gated through the shifter: a 1 kHz tone's
//     output RMS moves -5.548e-09 dB from the second second to the
//     six-hundredth (FrequencyShifterDrift). No double arithmetic runs in the
//     float hot path.
//
// SHIFT 0 IS A BYPASS. At exactly 0 Hz process_block copies its input:
// sample-exact, so a chain with the shift off equals the chain without a
// shifter. The allpass chains keep running underneath (their state stays
// warm; the bypass costs what shifting costs). Switching between 0 and a
// non-zero shift IS A DISCONTINUITY: the bypass has no delay, the shifted
// path has the allpass chains' group delay and a phase rotation, so the
// output jumps. Ramp the level around a switch if it must be inaudible.
// Changing between two non-zero shifts is continuous in phase.
//
// DELAY. The shifted path is causal and adds no bulk latency, but its group
// delay depends on frequency and is large at the bottom of the voice band:
// latency() gives the measured numbers.
//
// BAND. The design is fixed in normalized frequency. Image rejection
// (the image at f - shift against the wanted line at f + shift; a property
// of the Hilbert pair at f, independent of the shift), measured by
// tests/test_frequency_shifter.cpp at 48 kHz: 44.25 dB at 30 Hz, 55.67 at
// 100 Hz, 44.80 at 300 Hz, 49.00 at 1 kHz, 44.34 at 4 kHz; at 16 kHz: 62.20
// at 30 Hz, 44.80 at 100 Hz, 45.72 at 300 Hz, 53.99 at 1 kHz. By freqz on
// the coefficients (which reproduces those points), rejection is >= 44 dB
// from 21.7 Hz to 23978.3 Hz at 48 kHz (its minimum over 30 Hz ... 23970 Hz
// is 44.24 dB), and from 7.3 Hz to 7992.7 Hz at 16 kHz. Outside that band
// (the design is symmetric about fs/4, so at both ends) the 90-degree
// network's phase error grows and the image returns.
//
// Real-time contract (as fd_kalman.h): the constructor validates its config
// and may throw std::invalid_argument; it allocates nothing (all state is
// held by value). process_block() and every other post-construction entry
// point are noexcept and allocation-free. process_block() may run in place
// (in == out).

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>

// No ABI tag: nothing here holds an FFT, so the layout does not depend on the
// build's FFT engine and the classes sit in plain tap::mu (as lpc.h's do).
namespace tap::mu {

    namespace hilbert_detail {

        /// Niemitalo's 4+4 coefficients c, [chain][section] (the allpass
        /// sections use c^2): chain 0 in phase, chain 1 quadrature.
        inline constexpr std::array<std::array<double, 4>, 2> k_coefficients = {{
            {0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278},
            {0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737},
        }};

        /// c^2, squared in double and rounded once to Sample (for double:
        /// exactly c * c). Compile-time only.
        template <typename Sample>
        constexpr Sample squared(size_t chain, size_t section) {
            return static_cast<Sample>(k_coefficients[chain][section] * k_coefficients[chain][section]);
        }

    } // namespace hilbert_detail

    /// Analytic pair (re, im) of a real signal: Olli Niemitalo's 4+4 IIR
    /// allpass-pair Hilbert transformer. re and im are each the input through
    /// an allpass chain; their phase difference is 90 degrees (im lagging)
    /// over the design band, to within the image rejection frequency_shifter
    /// documents. Both outputs carry the chains' frequency-dependent group
    /// delay (frequency_shifter::latency()). Holds 33 samples of state and no
    /// allocation; every member is noexcept.
    template <typename Sample>
    class allpass_hilbert {
      public:
        static constexpr size_t k_sections = 4;

        /// Advance one sample: x in, the analytic pair out.
        void process(Sample x, Sample& re, Sample& im) noexcept {
            re         = chain(0, x);
            im         = m_im_delay;
            m_im_delay = chain(1, x);
        }

        /// Zero every state (as constructed).
        void reset() noexcept {
            m_in       = {};
            m_out      = {};
            m_im_delay = Sample(0);
        }

      private:
        /// Chain 0 gives the in-phase output, chain 1 (then one sample of
        /// delay) the quadrature one.
        static constexpr std::array<std::array<Sample, k_sections>, 2> k_c2 = {{
            {hilbert_detail::squared<Sample>(0, 0), hilbert_detail::squared<Sample>(0, 1),
             hilbert_detail::squared<Sample>(0, 2), hilbert_detail::squared<Sample>(0, 3)},
            {hilbert_detail::squared<Sample>(1, 0), hilbert_detail::squared<Sample>(1, 1),
             hilbert_detail::squared<Sample>(1, 2), hilbert_detail::squared<Sample>(1, 3)},
        }};

        /// One chain of four sections: y = c^2 (x + y[n-2]) - x[n-2].
        Sample chain(size_t c, Sample x) noexcept {
            for (size_t s = 0; s < k_sections; ++s) {
                const Sample y = k_c2[c][s] * (x + m_out[c][s][1]) - m_in[c][s][1];
                m_in[c][s][1]  = m_in[c][s][0];
                m_in[c][s][0]  = x;
                m_out[c][s][1] = m_out[c][s][0];
                m_out[c][s][0] = y;
                x              = y;
            }
            return x;
        }

        /// States [chain][section][x or y at n-1, n-2].
        std::array<std::array<std::array<Sample, 2>, k_sections>, 2> m_in{};
        std::array<std::array<std::array<Sample, 2>, k_sections>, 2> m_out{};
        Sample                                                       m_im_delay = Sample(0);
    };

    /// Single-sideband frequency shifter: every partial at f comes out at
    /// f + shift_hz. See the file comment for the design, the bypass at
    /// 0 Hz and the real-time contract.
    template <typename Sample>
    class frequency_shifter {
      public:
        struct config {
            double sample_rate = 48000.0;   ///< Hz; finite, > 0
            Sample shift_hz    = Sample(5); ///< Hz; finite, |shift_hz| < sample_rate / 2; 0 = bypass
        };

        /// Samples between the oscillator's renormalizations.
        static constexpr size_t k_renorm_interval = 64;

        /// @throws std::invalid_argument on a non-finite or non-positive
        ///         sample rate, or a shift set_shift_hz() would reject.
        explicit frequency_shifter(const config& cfg)
            : m_cfg(validated(cfg))
            , m_rate(static_cast<Sample>(cfg.sample_rate)) {
            if (!set_shift_hz(cfg.shift_hz)) {
                throw std::invalid_argument("frequency_shifter: shift_hz must be finite with |shift_hz| < fs / 2");
            }
            reset();
        }

        double sample_rate() const noexcept { return m_cfg.sample_rate; }
        Sample shift_hz() const noexcept { return m_cfg.shift_hz; }

        /// Change the shift, live and phase-continuously (the oscillator
        /// keeps its phase; only its step changes). 0 selects the bypass,
        /// which is a discontinuity (file comment). A non-finite shift, or
        /// one with |hz| >= sample_rate / 2, is rejected: the shift stays
        /// as it was and the call returns false.
        bool set_shift_hz(Sample hz) noexcept {
            if (!std::isfinite(hz) || !(std::abs(hz) < m_rate / Sample(2))) {
                return false;
            }
            m_cfg.shift_hz = hz;
            const Sample w = Sample(2) * std::numbers::pi_v<Sample> * hz / m_rate;
            m_step_cos     = std::cos(w);
            m_step_sin     = std::sin(w);
            return true;
        }

        /// 0 samples. Not because the shifted path is instantaneous, but
        /// because its delay is not a bulk latency a caller could compensate
        /// with a fixed number of samples: it is the allpass chains' group
        /// delay, which depends on frequency. Measured through the shifter
        /// (GroupDelay in tests/test_frequency_shifter.cpp), float and
        /// double alike:
        ///
        ///   f (Hz)        100      200      300     1000
        ///   48 kHz     2.5969   1.3757   0.9363   0.2902   ms
        ///   16 kHz     2.8088   1.4357   0.9653   0.2996   ms
        ///
        /// A loop built with this shifter is longer than the same loop
        /// without it by that much, at each frequency. The bypass (shift 0)
        /// has no delay at all.
        static constexpr size_t latency() noexcept { return 0; }

        /// Clear the allpass chains and restart the oscillator at phase 0;
        /// the shift is kept.
        void reset() noexcept {
            m_hilbert.reset();
            m_cos          = Sample(1);
            m_sin          = Sample(0);
            m_since_renorm = 0;
        }

        /// Shift n samples. in == out is allowed.
        void process_block(const Sample* in, Sample* out, size_t n) noexcept {
            if (m_cfg.shift_hz == Sample(0)) {
                for (size_t i = 0; i < n; ++i) {
                    const Sample x = in[i];
                    Sample       re;
                    Sample       im;
                    m_hilbert.process(x, re, im); // keeps the chains warm
                    out[i] = x;
                }
                return;
            }
            size_t i = 0;
            while (i < n) {
                const size_t room = k_renorm_interval - m_since_renorm;
                const size_t end  = (n - i > room) ? i + room : n;
                for (; i < end; ++i) {
                    Sample re;
                    Sample im;
                    m_hilbert.process(in[i], re, im);
                    out[i]         = re * m_cos - im * m_sin;
                    const Sample c = m_cos * m_step_cos - m_sin * m_step_sin;
                    const Sample s = m_sin * m_step_cos + m_cos * m_step_sin;
                    m_cos          = c;
                    m_sin          = s;
                    ++m_since_renorm;
                }
                if (m_since_renorm == k_renorm_interval) {
                    // One Newton step of 1 / |z| about |z| = 1: after 64
                    // rotations the magnitude is off by ~1e-6 in float, and
                    // one step brings that back to rounding level.
                    const Sample g = Sample(1.5) - Sample(0.5) * (m_cos * m_cos + m_sin * m_sin);
                    m_cos *= g;
                    m_sin *= g;
                    m_since_renorm = 0;
                }
            }
        }

      private:
        static config validated(const config& cfg) {
            // Checked as Sample too: the hot path divides by it in Sample.
            const auto rate = static_cast<Sample>(cfg.sample_rate);
            if (!std::isfinite(cfg.sample_rate) || !std::isfinite(rate) || !(rate > Sample(0))) {
                throw std::invalid_argument("frequency_shifter: sample_rate must be finite and > 0");
            }
            return cfg;
        }

        config                  m_cfg;
        Sample                  m_rate;
        allpass_hilbert<Sample> m_hilbert;
        Sample                  m_cos          = Sample(1);
        Sample                  m_sin          = Sample(0);
        Sample                  m_step_cos     = Sample(1);
        Sample                  m_step_sin     = Sample(0);
        size_t                  m_since_renorm = 0;
    };

} // namespace tap::mu
