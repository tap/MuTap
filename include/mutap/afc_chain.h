/// @file afc_chain.h
/// @brief The anti-howl chain: M feedback cancellers on one delayed speaker
///        reference, summed into a voice bus, optional decorrelator / reverb /
///        safety stages, an output delay line and the backing track after it.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"

// afc_chain is not tagged itself (like aec_chain, postfilter.h): the only
// FFT-engine-dependent members are its Canceller objects, and Canceller is a
// template argument, so the tag is in afc_chain's mangled name through it.
// afc_stage_ref holds no FFT at all.
namespace tap::mu {

    /// What a chain stage must provide: a block process from `in` to `out`
    /// over `n` samples that cannot throw. The chain never passes aliasing
    /// buffers (in != out), so a stage need not support in-place work.
    template <typename Stage, typename Sample>
    concept afc_stage = requires(Stage& stage, const Sample* in, Sample* out, size_t n) {
        { stage.process_block(in, out, n) } noexcept;
    };

    /// A non-owning, type-erased reference to a chain stage: an object
    /// pointer and a plain function pointer, no allocation, no virtual
    /// dispatch. Built implicitly from a pointer to any afc_stage (a
    /// frequency shifter, a reverb, a safety ducker, ...); nullptr, or a
    /// default-constructed ref, is the bypass. The caller owns the stage and
    /// keeps it alive while the chain refers to it.
    template <typename Sample>
    class afc_stage_ref {
      public:
        constexpr afc_stage_ref() noexcept = default;
        constexpr afc_stage_ref(std::nullptr_t) noexcept {} ///< implicit: nullptr is the bypass

        template <afc_stage<Sample> Stage>
        constexpr afc_stage_ref(Stage* stage) noexcept // implicit: set_reverb(&my_reverb)
            : m_stage(stage)
            , m_process(stage != nullptr ? &invoke<Stage> : nullptr) {}

        /// True when a stage is attached (false = bypass).
        constexpr explicit operator bool() const noexcept { return m_process != nullptr; }

        /// @pre a stage is attached (operator bool).
        void process_block(const Sample* in, Sample* out, size_t n) const noexcept { m_process(m_stage, in, out, n); }

      private:
        using process_fn = void (*)(void*, const Sample*, Sample*, size_t) noexcept;

        template <typename Stage>
        static void invoke(void* stage, const Sample* in, Sample* out, size_t n) noexcept {
            static_cast<Stage*>(stage)->process_block(in, out, n);
        }

        void*      m_stage   = nullptr;
        process_fn m_process = nullptr;
    };

    /// The anti-howl PoC's chain (the PoC plan's §2.1), canceller-first:
    ///
    ///   mic_1..M -> canceller_m (reference u = the speaker feed, delayed)
    ///            -> voice bus = sum_m e_m
    ///            -> decorrelator -> reverb -> safety   (optional; null = bypass)
    ///            -> output delay line (spends what is left of the budget)
    ///            -> + aux (the backing track, summed after the chain)
    ///            -> speaker
    ///
    /// Every canceller shares ONE reference: with a mono voice send to every
    /// loudspeaker each mic's loop is a single path from the speaker feed, so
    /// M mics cost M cancellers summed into the bus (§2.5; whether two mics
    /// hold is the two-mic experiment's question, not this class's). The
    /// three stages are off by default — the canceller-first decision (Rev
    /// 4.1): the shift and the reverb are studied options, and the safety
    /// layer waits on the release-policy experiment. Stage order is fixed.
    ///
    /// THE REFERENCE. The canceller input u is what the DAC plays: the
    /// speaker output (bus through the stages and the delay line, plus aux),
    /// fed back through a delay line of reference_delay_samples. The speaker
    /// block the chain writes now is the reference of the NEXT block at the
    /// earliest (causality: it depends on this block's cancellation), so the
    /// chain always lags the reference by one block, and
    ///
    ///   u[n] = speaker[n - block_size - reference_delay_samples].
    ///
    /// On a rig, where a speaker sample written at stream index j reaches
    /// the mic stream at j + R (R the loopback round trip in samples,
    /// measured per session — PROTOCOL.md §5.1; loudspeaker DSP latency
    /// belongs to it too, §5.3), set
    ///
    ///   reference_delay_samples = R - block_size - jitter margin
    ///
    /// (the margin: the loopback repeats' min-max spread; 0 if that comes
    /// out negative, and the direct path then lands that much earlier than
    /// flight + margin — never before tap 0, since R >= block_size on any
    /// rig that writes the speaker block after reading the mic block).
    /// The filter's
    /// taps then model the room and not the buffers: the direct path lands
    /// at the acoustic flight time plus the margin, instead of R -
    /// block_size samples late, where it spends taps on dead delay and cuts
    /// the room's tail off the end of the filter. reference_aligned() is
    /// the readback check.
    ///
    /// Block size: every canceller runs at Canceller::block_size() (from
    /// the canceller config); process_block() takes whole multiples of it.
    ///
    /// The canceller is pluggable like aec_chain's: any type with a
    /// `config`, construction from it, block_size(), filter_length(),
    /// process_block(u, y, e), copy_impulse_response(dest), reset() and
    /// set_adaptation(bool) — pem_afc (the default, on the Kalman core with
    /// the speech cascade) or a raw core. The convergence statistics are
    /// forwarded per mic when the canceller has them
    /// (uncertainty_ratio(), shadow_residual_ratio()), raw: thresholds
    /// belong to the safety layer's policy (pem_afc.h records what each
    /// statistic was measured to miss).
    ///
    /// Real-time contract (as fd_kalman.h): the constructor allocates and
    /// may throw (std::invalid_argument on a bad config, the canceller's own
    /// validation included); process_block() and every other
    /// post-construction entry point are noexcept and allocation-free.
    template <typename Sample, typename Canceller = pem_afc<Sample, speech_predictor<Sample>, partitioned_fdkf<Sample>>>
    class afc_chain {
      public:
        using canceller_type = Canceller;
        using stage_ref      = afc_stage_ref<Sample>;

        struct config {
            size_t                     microphones = 1; ///< M >= 1
            typename Canceller::config canceller;       ///< every mic's canceller
            /// Extra reference delay beyond the chain's inherent block (see
            /// the class comment): the measured round trip minus block_size
            /// minus a jitter margin.
            size_t reference_delay_samples = 0;
            /// The delay line after the stages: whatever the latency budget
            /// leaves (the PoC plan's §2.2 "delay line" row).
            size_t output_delay_samples = 0;
            /// Recorded for the host (sample_rate()); the chain's own
            /// processing is rate-agnostic.
            double sample_rate = 48000.0;
        };

        explicit afc_chain(const config& cfg)
            : m_cfg(validated(cfg)) {
            m_cancellers.reserve(cfg.microphones);
            for (size_t m = 0; m < cfg.microphones; ++m) {
                m_cancellers.emplace_back(cfg.canceller);
            }
            const size_t b = m_cancellers.front().block_size();
            m_u.resize(b);
            m_e.resize(cfg.microphones * b);
            m_bus_a.resize(b);
            m_bus_b.resize(b);
            m_ref_ring.resize(cfg.reference_delay_samples + b);
            m_out_ring.resize(cfg.output_delay_samples + b);
            reset();
        }

        size_t microphones() const noexcept { return m_cancellers.size(); }
        size_t block_size() const noexcept { return m_cancellers.front().block_size(); }
        size_t filter_length() const noexcept { return m_cancellers.front().filter_length(); }
        double sample_rate() const noexcept { return m_cfg.sample_rate; }
        size_t reference_delay_samples() const noexcept { return m_cfg.reference_delay_samples; }
        size_t output_delay_samples() const noexcept { return m_cfg.output_delay_samples; }

        /// Mic-to-speaker latency of the chain in samples: one block of
        /// framing (a sample waits up to a block to enter the chain) plus
        /// the output delay line. Not included: the stages' own delays (an
        /// IIR shifter's group delay depends on frequency, and the budget
        /// reserves it at the lowest frequency it must cover — §2.2) and
        /// the interface's converters and buffers.
        size_t latency() const noexcept { return block_size() + m_cfg.output_delay_samples; }

        /// How far the cancellers' reference lags the speaker feed, in
        /// samples: block_size() + reference_delay_samples (class comment).
        size_t reference_lag() const noexcept { return block_size() + m_cfg.reference_delay_samples; }

        /// @pre mic < microphones()
        Canceller&       canceller(size_t mic) noexcept { return m_cancellers[mic]; }
        const Canceller& canceller(size_t mic) const noexcept { return m_cancellers[mic]; }

        /// Attach (or, with nullptr, bypass) a stage. Call between blocks,
        /// on the audio thread: the ref is read inside process_block().
        void set_decorrelator(stage_ref stage) noexcept { m_decorrelator = stage; }
        void set_reverb(stage_ref stage) noexcept { m_reverb = stage; }
        void set_safety(stage_ref stage) noexcept { m_safety = stage; }

        /// Adaptation on or off for every canceller.
        void set_adaptation(bool enabled) noexcept {
            for (auto& c : m_cancellers) {
                c.set_adaptation(enabled);
            }
        }

        /// Clears the cancellers and both delay lines. The stages are the
        /// caller's and are not touched.
        void reset() noexcept {
            for (auto& c : m_cancellers) {
                c.reset();
            }
            fill_zero(m_u);
            fill_zero(m_e);
            fill_zero(m_bus_a);
            fill_zero(m_bus_b);
            fill_zero(m_ref_ring);
            fill_zero(m_out_ring);
            m_ref_pos = 0;
            m_out_pos = 0;
        }

        /// Run n samples through the chain.
        /// @param mics    microphones() pointers to n mic samples each
        /// @param aux     n samples of backing track summed into the speaker
        ///                after the chain, or nullptr for none
        /// @param speaker n samples out: what the DAC should play (and what
        ///                the reference records); may alias aux or any mic
        /// @param n       a multiple of block_size()
        /// @pre n % block_size() == 0; a trailing partial block is written
        ///      as silence (plus aux) and not processed
        void process_block(const Sample* const* mics, const Sample* aux, Sample* speaker, size_t n) noexcept {
            const size_t b      = block_size();
            const size_t blocks = n / b;
            for (size_t k = 0; k < blocks; ++k) {
                process_one(mics, aux, speaker, k * b);
            }
            for (size_t i = blocks * b; i < n; ++i) {
                speaker[i] = (aux != nullptr) ? aux[i] : Sample(0);
            }
        }

        /// The error (feedback-compensated) block of mic `mic` from the last
        /// processed block: block_size() samples. @pre mic < microphones()
        const Sample* error_block(size_t mic) const noexcept { return m_e.data() + mic * block_size(); }

        /// The reference the cancellers saw in the last processed block:
        /// block_size() samples.
        const Sample* reference_block() const noexcept { return m_u.data(); }

        /// Canceller `mic`'s identification progress (pem_afc /
        /// partitioned_fdkf uncertainty_ratio()). @pre mic < microphones()
        Sample uncertainty_ratio(size_t mic) const noexcept
            requires requires(const Canceller& c) { c.uncertainty_ratio(); }
        {
            return m_cancellers[mic].uncertainty_ratio();
        }

        /// Canceller `mic`'s shadow comparator ratio (pem_afc; 1 while its
        /// shadow is off). @pre mic < microphones()
        Sample shadow_residual_ratio(size_t mic) const noexcept
            requires requires(const Canceller& c) { c.shadow_residual_ratio(); }
        {
            return m_cancellers[mic].shadow_residual_ratio();
        }

        /// Index of the largest-magnitude tap of canceller `mic`'s current
        /// impulse response (the first on a tie), which is left in `ir`.
        /// Non-const: copy_impulse_response() runs its inverse FFTs in the
        /// canceller's own scratch.
        /// @param ir filter_length() samples, caller-provided (allocation-free)
        /// @pre mic < microphones()
        size_t direct_path_tap(size_t mic, Sample* ir) noexcept {
            m_cancellers[mic].copy_impulse_response(ir);
            const size_t len  = filter_length();
            size_t       best = 0;
            Sample       peak = Sample(0);
            for (size_t i = 0; i < len; ++i) {
                const Sample a = std::abs(ir[i]);
                if (a > peak) {
                    peak = a;
                    best = i;
                }
            }
            return best;
        }

        /// THE REFERENCE ALIGNMENT CHECK (the PoC plan's §2.1): whether
        /// canceller `mic`'s largest tap sits within `tolerance` samples of
        /// `expected_direct_tap`.
        ///
        /// Intended use: on a rig, after the soundcheck has converged the
        /// cancellers (the run sheets wait on the convergence readout, §2.7),
        /// read the check with expected_direct_tap = the acoustic flight
        /// time (taped distance / c, in samples) plus the jitter margin
        /// subtracted in reference_delay_samples. A misaligned reference
        /// shows as the direct path appearing hundreds of samples late (the
        /// unsubtracted round trip) or not at all (a reference delayed past
        /// the direct path — the margin was too small, and the filter can
        /// only model what arrives after it). The largest tap is the direct
        /// path only where the direct sound dominates the early response;
        /// a loudspeaker with a strong nearby reflection can read that
        /// instead, so the tolerance is set from the rig's own geometry.
        ///
        /// Measured (tests/test_afc_chain.cpp: float, 1024 taps, a 480-sample
        /// electrical delay, a 32-sample margin): after 2 s of speech the
        /// band-limited image-source rooms read the true peak + 32 to within
        /// one tap in every run, and an unaligned reference reads 416 taps
        /// later. It needs broadband material: after a held note the largest
        /// tap is the closed-loop bias, where the reference is in phase with
        /// the note, not the room. On synthetic rooms without a physical
        /// direct path (random taps under a decaying envelope) near-tied peaks
        /// make it read the runner-up in some runs.
        /// @param ir filter_length() samples of caller-provided scratch; the
        ///           impulse response is left in it
        /// @pre mic < microphones()
        bool reference_aligned(size_t mic, size_t expected_direct_tap, size_t tolerance, Sample* ir) noexcept {
            const size_t tap = direct_path_tap(mic, ir);
            const size_t gap = (tap > expected_direct_tap) ? tap - expected_direct_tap : expected_direct_tap - tap;
            return gap <= tolerance;
        }

      private:
        /// One block at offset `off` into the caller's buffers.
        void process_one(const Sample* const* mics, const Sample* aux, Sample* speaker, size_t off) noexcept {
            const size_t b = block_size();
            const size_t m = m_cancellers.size();

            // The reference: the oldest block of the speaker history ring,
            // speaker[n - b - reference_delay_samples].
            ring_read(m_ref_ring, m_ref_pos, m_u.data(), b);

            // Cancel every mic against the one reference; the bus is their sum
            // (the first mic's error copied, so one mic passes bit-exactly).
            for (size_t k = 0; k < m; ++k) {
                Sample* e = m_e.data() + k * b;
                m_cancellers[k].process_block(m_u.data(), mics[k] + off, e);
            }
            Sample* bus = m_bus_a.data();
            Sample* alt = m_bus_b.data();
            for (size_t i = 0; i < b; ++i) {
                bus[i] = m_e[i];
            }
            for (size_t k = 1; k < m; ++k) {
                const Sample* e = m_e.data() + k * b;
                for (size_t i = 0; i < b; ++i) {
                    bus[i] += e[i];
                }
            }

            // The stages, in order, ping-ponging so no stage sees in == out.
            run_stage(m_decorrelator, bus, alt, b);
            run_stage(m_reverb, bus, alt, b);
            run_stage(m_safety, bus, alt, b);

            // The output delay line: write the bus, read the block that is
            // output_delay_samples old.
            ring_write(m_out_ring, m_out_pos, bus, b);
            m_out_pos = advance(m_out_pos, b, m_out_ring.size());
            ring_read(m_out_ring, m_out_pos, alt, b);

            // Speaker = delayed chain + aux; it becomes the reference history.
            Sample* out = speaker + off;
            if (aux != nullptr) {
                const Sample* a = aux + off;
                for (size_t i = 0; i < b; ++i) {
                    out[i] = alt[i] + a[i];
                }
            }
            else {
                for (size_t i = 0; i < b; ++i) {
                    out[i] = alt[i];
                }
            }
            ring_write(m_ref_ring, m_ref_pos, out, b);
            m_ref_pos = advance(m_ref_pos, b, m_ref_ring.size());
        }

        /// Run `stage` from `bus` into `alt` and swap them, or do nothing
        /// when it is bypassed.
        static void run_stage(const stage_ref& stage, Sample*& bus, Sample*& alt, size_t b) noexcept {
            if (stage) {
                stage.process_block(bus, alt, b);
                Sample* t = bus;
                bus       = alt;
                alt       = t;
            }
        }

        static size_t advance(size_t pos, size_t b, size_t size) noexcept {
            pos += b;
            return (pos >= size) ? pos - size : pos;
        }

        /// Copy b samples out of `ring` starting at `pos`, wrapping.
        static void ring_read(const std::vector<Sample>& ring, size_t pos, Sample* dest, size_t b) noexcept {
            const size_t size  = ring.size();
            const size_t first = (size - pos < b) ? size - pos : b;
            for (size_t i = 0; i < first; ++i) {
                dest[i] = ring[pos + i];
            }
            for (size_t i = first; i < b; ++i) {
                dest[i] = ring[i - first];
            }
        }

        /// Copy b samples into `ring` starting at `pos`, wrapping.
        static void ring_write(std::vector<Sample>& ring, size_t pos, const Sample* src, size_t b) noexcept {
            const size_t size  = ring.size();
            const size_t first = (size - pos < b) ? size - pos : b;
            for (size_t i = 0; i < first; ++i) {
                ring[pos + i] = src[i];
            }
            for (size_t i = first; i < b; ++i) {
                ring[i - first] = src[i];
            }
        }

        static void fill_zero(std::vector<Sample>& v) noexcept {
            for (auto& x : v) {
                x = Sample(0);
            }
        }

        static config validated(const config& cfg) {
            if (cfg.microphones == 0) {
                throw std::invalid_argument("afc_chain: microphones must be >= 1");
            }
            if (!(cfg.sample_rate > 0.0) || !std::isfinite(cfg.sample_rate)) {
                throw std::invalid_argument("afc_chain: sample_rate must be positive and finite");
            }
            return cfg;
        }

        config                 m_cfg;
        std::vector<Canceller> m_cancellers;
        std::vector<Sample>    m_u;     ///< this block's reference
        std::vector<Sample>    m_e;     ///< per-mic error blocks, mic-major
        std::vector<Sample>    m_bus_a; ///< the bus and its ping-pong partner
        std::vector<Sample>    m_bus_b;
        std::vector<Sample>    m_ref_ring; ///< speaker history, reference_delay_samples + block
        std::vector<Sample>    m_out_ring; ///< bus history, output_delay_samples + block
        size_t                 m_ref_pos = 0;
        size_t                 m_out_pos = 0;
        stage_ref              m_decorrelator;
        stage_ref              m_reverb;
        stage_ref              m_safety;
    };

} // namespace tap::mu
