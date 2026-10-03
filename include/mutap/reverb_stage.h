/// @file reverb_stage.h
/// @brief Reverb-slot stages for afc_chain: a dry/wet mix around any mono-in
///        reverb, and the variant that frequency-shifts the dry path only.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHAT. afc_chain's reverb slot (decorrelator -> reverb -> safety, see
// afc_chain.h) takes any noexcept process_block(in, out, n). A reverb
// engine does not have that shape: it is mono in, N >= 1 channels out, and
// says nothing about how much of it reaches the loudspeaker. The two
// classes here are that glue, with the mix written down:
//
//   reverb_mix:       y = (1 - w) x + w r(x)
//   shifted_dry_mix:  y = (1 - w) shift(x) + w r(x)
//
// where r is the reverb's mono return (output channel 0, "L", or the
// average of channels 0 and 1, "(L + R) / 2"; reverb_return) and w the wet
// level in [0, 1]. The mix is a crossfade, so it changes the bus level: for
// white input its power gain is 10 log10 of the sum of the squared impulse
// response of (1 - w) delta + w r: for the vendored Dattorro plates,
// -0.547 to -2.135 dB over wet 0.15 to 0.50 and decay 0.3 to 0.85 (L
// return, damping 0.0005; docs/reverb-afc.md). A loop measurement that
// compares a mix against the dry chain at equal forward gain therefore
// includes that level change; docs/reverb-afc.md reports both.
//
// THE DRY-ONLY SHIFT. With a frequency shifter in the decorrelator slot
// AND a reverb behind it, the reverb's tail goes round the loop through the
// shifter and climbs by the shift on every pass. shifted_dry_mix is the
// alternative topology for that case: the shifter sits on the dry
// path inside this stage (leave the chain's decorrelator slot empty), and
// the reverb is fed the UNshifted bus, so the wet path never passes through
// the shifter. What it costs and buys in the loop is measured, not assumed
// (docs/reverb-afc.md, "Shift and reverb").
//
// THE REVERB TYPE. Anything with
//
//   void process(const Sample* const* in, Sample* const* out, int n) noexcept
//
// taking ONE input channel and writing `channels` output channels: the shape
// of the FAUST shim's mutap_faust::faust_block (third_party/faust/), which
// the tests instantiate with the vendored Dattorro plates. FAUST-generated
// code never enters include/mutap: these classes know only that signature.
// If the type also has num_outputs() (faust_block does), the constructor
// checks it against config::channels. The stages do not own the reverb (as
// afc_chain does not own its stages): the caller keeps it alive, sets its
// parameters (decay, damping, ...) through its own interface and clears it
// with its own reset when the chain is reset.
//
// LATENCY. latency() is 0 for both: the mix adds no delay of its own. A
// reverb with a pre-delay delays only the wet path, and a shifter's
// frequency-dependent group delay is not a bulk latency
// (frequency_shifter::latency()).
//
// Real-time contract (as fd_kalman.h): the constructor validates its config,
// allocates the per-channel return buffers (channels x block_size samples,
// plus block_size for the shifted dry path) and may throw
// std::invalid_argument. process_block() and every other post-construction
// entry point are noexcept and allocation-free. process_block() accepts any
// n (it works in pieces of at most block_size) and may run in place
// (in == out). Arithmetic is in Sample throughout: float32 is first-class,
// and no double runs in the float hot path.

#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

// No ABI tag: nothing here holds an FFT, so the layout does not depend on the
// build's FFT engine and the classes sit in plain tap::mu (as
// frequency_shifter.h's do). Header-only; the Reverb and Shifter are
// template arguments.
namespace tap::mu {

    /// Which of the reverb's outputs the mix returns to the mono bus.
    enum class reverb_return {
        left, ///< output channel 0
        mid   ///< (channel 0 + channel 1) / 2; needs >= 2 channels
    };

    /// A mono-in reverb engine the stages can drive (see the file comment).
    template <typename Reverb, typename Sample>
    concept mono_reverb = requires(Reverb& reverb, const Sample* const* in, Sample* const* out, int n) {
        { reverb.process(in, out, n) } noexcept;
    };

    /// A block process the dry path of shifted_dry_mix can run (the shape of
    /// afc_chain's afc_stage: tap::mu::frequency_shifter, for one).
    template <typename Shifter, typename Sample>
    concept dry_path_stage = requires(Shifter& stage, const Sample* in, Sample* out, size_t n) {
        { stage.process_block(in, out, n) } noexcept;
    };

    namespace reverb_detail {

        /// The return half both stages share: the reverb's output buffers,
        /// the wet level and the return mode.
        template <typename Sample, typename Reverb>
        class reverb_return_core {
          public:
            reverb_return_core(Reverb& reverb, size_t block_size, size_t channels, Sample wet, reverb_return mode)
                : m_reverb(&reverb)
                , m_block(validated_block(block_size))
                , m_channels(validated_channels(reverb, channels))
                , m_buffer(channels * block_size, Sample(0))
                , m_outs(channels, nullptr) {
                for (size_t c = 0; c < channels; ++c) {
                    m_outs[c] = m_buffer.data() + c * block_size;
                }
                if (!set_wet(wet)) {
                    throw std::invalid_argument("reverb_mix: wet must be finite and in [0, 1]");
                }
                if (!set_return_mode(mode)) {
                    throw std::invalid_argument("reverb_mix: the (L + R) / 2 return needs >= 2 reverb channels");
                }
            }

            bool set_wet(Sample wet) noexcept {
                if (!std::isfinite(wet) || wet < Sample(0) || wet > Sample(1)) {
                    return false;
                }
                m_wet = wet;
                m_dry = Sample(1) - wet;
                return true;
            }

            bool set_return_mode(reverb_return mode) noexcept {
                if (mode == reverb_return::mid && m_channels < 2) {
                    return false;
                }
                m_mode = mode;
                return true;
            }

            Sample        wet() const noexcept { return m_wet; }
            Sample        dry_gain() const noexcept { return m_dry; }
            reverb_return return_mode() const noexcept { return m_mode; }
            size_t        block_size() const noexcept { return m_block; }
            size_t        channels() const noexcept { return m_channels; }

            /// Run the reverb on `n` <= block_size() samples of `in`; its
            /// outputs land in the channel buffers.
            void run_reverb(const Sample* in, size_t n) noexcept {
                const Sample* const ins[1] = {in};
                m_reverb->process(ins, m_outs.data(), static_cast<int>(n));
            }

            /// out[i] = dry_gain() * dry[i] + wet() * r[i] for the last
            /// run_reverb() block; dry may equal out.
            void mix(const Sample* dry, Sample* out, size_t n) const noexcept {
                const Sample* l = m_outs[0];
                if (m_mode == reverb_return::mid) {
                    const Sample* r    = m_outs[1];
                    const Sample  half = m_wet * Sample(0.5);
                    for (size_t i = 0; i < n; ++i) {
                        out[i] = m_dry * dry[i] + half * (l[i] + r[i]);
                    }
                }
                else {
                    for (size_t i = 0; i < n; ++i) {
                        out[i] = m_dry * dry[i] + m_wet * l[i];
                    }
                }
            }

          private:
            static size_t validated_block(size_t block_size) {
                if (block_size == 0) {
                    throw std::invalid_argument("reverb_mix: block_size must be >= 1");
                }
                return block_size;
            }

            static size_t validated_channels(Reverb& reverb, size_t channels) {
                if (channels == 0) {
                    throw std::invalid_argument("reverb_mix: channels must be >= 1");
                }
                if constexpr (requires(const Reverb& r) { r.num_outputs(); }) {
                    if (static_cast<size_t>(reverb.num_outputs()) != channels) {
                        throw std::invalid_argument("reverb_mix: channels differs from the reverb's num_outputs()");
                    }
                }
                if constexpr (requires(const Reverb& r) { r.num_inputs(); }) {
                    if (reverb.num_inputs() != 1) {
                        throw std::invalid_argument("reverb_mix: the reverb must take exactly one input");
                    }
                }
                return channels;
            }

            Reverb*              m_reverb;
            size_t               m_block;
            size_t               m_channels;
            std::vector<Sample>  m_buffer; ///< channel-major, block_size each
            std::vector<Sample*> m_outs;   ///< one pointer per channel into m_buffer
            Sample               m_wet  = Sample(0);
            Sample               m_dry  = Sample(1);
            reverb_return        m_mode = reverb_return::left;
        };

    } // namespace reverb_detail

    /// The reverb slot's mix: y = (1 - w) x + w r(x), r the reverb's mono
    /// return. An afc_stage (afc_chain::set_reverb(&mix)). Does not own the
    /// reverb. See the file comment for the contract.
    template <typename Sample, typename Reverb>
        requires mono_reverb<Reverb, Sample>
    class reverb_mix {
      public:
        struct config {
            size_t        block_size = 64;          ///< the largest piece process_block() hands the reverb; >= 1
            size_t        channels   = 2;           ///< the reverb's output channels; >= 1
            Sample        wet        = Sample(0.3); ///< w, finite, in [0, 1]
            reverb_return mode       = reverb_return::left;
        };

        /// @throws std::invalid_argument on a bad config, or when the
        ///         reverb reports a channel count other than cfg.channels
        reverb_mix(Reverb& reverb, const config& cfg)
            : m_core(reverb, cfg.block_size, cfg.channels, cfg.wet, cfg.mode) {}

        /// Set w; a non-finite w, or one outside [0, 1], is rejected (false,
        /// nothing changes). Takes effect from the next block.
        bool set_wet(Sample wet) noexcept { return m_core.set_wet(wet); }
        /// Choose the return; mid with a one-channel reverb is rejected.
        bool set_return_mode(reverb_return mode) noexcept { return m_core.set_return_mode(mode); }

        Sample        wet() const noexcept { return m_core.wet(); }
        reverb_return return_mode() const noexcept { return m_core.return_mode(); }
        size_t        block_size() const noexcept { return m_core.block_size(); }
        size_t        channels() const noexcept { return m_core.channels(); }

        /// 0 samples: the mix adds no delay of its own (file comment).
        static constexpr size_t latency() noexcept { return 0; }

        /// Mix n samples. in == out is allowed.
        void process_block(const Sample* in, Sample* out, size_t n) noexcept {
            const size_t b = m_core.block_size();
            for (size_t off = 0; off < n; off += b) {
                const size_t len = (n - off < b) ? n - off : b;
                m_core.run_reverb(in + off, len);
                m_core.mix(in + off, out + off, len);
            }
        }

      private:
        reverb_detail::reverb_return_core<Sample, Reverb> m_core;
    };

    /// The dry-only-shift topology: y = (1 - w) shift(x) + w r(x), the
    /// reverb fed the unshifted input so its tail never recirculates through
    /// the shifter. Put it in the chain's REVERB slot and leave the
    /// decorrelator slot empty. An afc_stage. Owns neither the shifter nor
    /// the reverb. See the file comment for the contract.
    template <typename Sample, typename Shifter, typename Reverb>
        requires dry_path_stage<Shifter, Sample> && mono_reverb<Reverb, Sample>
    class shifted_dry_mix {
      public:
        using config = typename reverb_mix<Sample, Reverb>::config;

        /// @throws std::invalid_argument as reverb_mix's constructor
        shifted_dry_mix(Shifter& shifter, Reverb& reverb, const config& cfg)
            : m_shifter(&shifter)
            , m_core(reverb, cfg.block_size, cfg.channels, cfg.wet, cfg.mode)
            , m_dry(cfg.block_size, Sample(0)) {}

        bool set_wet(Sample wet) noexcept { return m_core.set_wet(wet); }
        bool set_return_mode(reverb_return mode) noexcept { return m_core.set_return_mode(mode); }

        Sample        wet() const noexcept { return m_core.wet(); }
        reverb_return return_mode() const noexcept { return m_core.return_mode(); }
        size_t        block_size() const noexcept { return m_core.block_size(); }
        size_t        channels() const noexcept { return m_core.channels(); }

        /// 0 samples (file comment; the shifter's group delay is its own).
        static constexpr size_t latency() noexcept { return 0; }

        /// Mix n samples. in == out is allowed.
        void process_block(const Sample* in, Sample* out, size_t n) noexcept {
            const size_t b = m_core.block_size();
            for (size_t off = 0; off < n; off += b) {
                const size_t len = (n - off < b) ? n - off : b;
                m_core.run_reverb(in + off, len);
                m_shifter->process_block(in + off, m_dry.data(), len);
                m_core.mix(m_dry.data(), out + off, len);
            }
        }

      private:
        Shifter*                                          m_shifter;
        reverb_detail::reverb_return_core<Sample, Reverb> m_core;
        std::vector<Sample>                               m_dry; ///< the shifted dry path, one piece
    };

} // namespace tap::mu
