// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The reverb-slot stages' plumbing (mutap/reverb_stage.h): the mix formula,
// both returns, the dry-only-shift topology, any block length, in place,
// config validation and the real-time contract. A small stateful stand-in
// replaces the reverb, so this file builds on every target and the emulated
// selections run its float suite (tests/bare_metal_main.cpp, TEST_FILTER in
// tests/CMakeLists.txt). The vendored Dattorro plates behind the same
// classes, and what they cost inside the loop, are test_reverb_stage.cpp
// (host-only).

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/afc_chain.h"
#include "mutap/frequency_shifter.h"
#include "mutap/reverb_stage.h"

namespace {

    using tap::mu::reverb_return;

    /// A stand-in reverb with the FAUST shim's shape: mono in, `Channels`
    /// out, state carried across calls. Channel c is a one-pole recursion
    /// y = x + a_c y[n-1] with a different a per channel, so L and R differ
    /// and a block split that lost state would show.
    template <typename Sample, int Channels = 2>
    class stand_in_reverb {
      public:
        void process(const Sample* const* in, Sample* const* out, int n) noexcept {
            for (int c = 0; c < Channels; ++c) {
                const Sample a = Sample(0.5) + Sample(0.25) * static_cast<Sample>(c);
                for (int i = 0; i < n; ++i) {
                    auto& s   = m_state[static_cast<size_t>(c)];
                    s         = in[0][i] + a * s;
                    out[c][i] = s;
                }
            }
        }
        int  num_inputs() const noexcept { return 1; }
        int  num_outputs() const noexcept { return Channels; }
        void clear() noexcept {
            for (auto& s : m_state) {
                s = Sample(0);
            }
        }

      private:
        std::array<Sample, static_cast<size_t>(Channels)> m_state{};
    };

    /// The same reverb without num_outputs() / num_inputs(): the stages
    /// must not require them.
    template <typename Sample>
    struct bare_reverb {
        void process(const Sample* const* in, Sample* const* out, int n) noexcept {
            for (int i = 0; i < n; ++i) {
                out[0][i] = Sample(2) * in[0][i];
            }
        }
    };

    /// A dry-path stand-in: y = 3 x + the previous input (stateful).
    template <typename Sample>
    struct stand_in_shifter {
        void process_block(const Sample* in, Sample* out, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) {
                const Sample x = in[i];
                out[i]         = Sample(3) * x + m_prev;
                m_prev         = x;
            }
        }
        Sample m_prev = Sample(0);
    };

    template <typename Sample>
    std::vector<Sample> ramp_signal(size_t n) {
        std::vector<Sample> x(n);
        for (size_t i = 0; i < n; ++i) {
            // Deterministic, sign-changing, no libm: a sawtooth of period 37.
            x[i] = static_cast<Sample>(static_cast<double>(i % 37) / 18.0 - 1.0);
        }
        return x;
    }

    /// Run a reference reverb over x in one call and return its channels.
    template <typename Sample, int Channels>
    std::vector<std::vector<Sample>> reference_channels(const std::vector<Sample>& x) {
        stand_in_reverb<Sample, Channels> rev;
        std::vector<std::vector<Sample>>  ch(Channels, std::vector<Sample>(x.size()));
        std::vector<Sample*>              outs(Channels);
        for (int c = 0; c < Channels; ++c) {
            outs[static_cast<size_t>(c)] = ch[static_cast<size_t>(c)].data();
        }
        const Sample* const ins[1] = {x.data()};
        rev.process(ins, outs.data(), static_cast<int>(x.size()));
        return ch;
    }

    template <typename Sample>
    class reverb_mix_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(reverb_mix_test, sample_types);

    template <typename Sample>
    Sample tolerance() {
        return std::is_same_v<Sample, float> ? Sample(1e-5) : Sample(1e-12);
    }

    // y = (1 - w) x + w L, and with the mid return w (L + R) / 2, across
    // calls longer and shorter than the block (the stage works in pieces).
    TYPED_TEST(reverb_mix_test, MixIsTheCrossfadeOfDryAndReturn) {
        using sample         = TypeParam;
        const size_t n       = 1000;
        const auto   x       = ramp_signal<sample>(n);
        const auto   ref     = reference_channels<sample, 2>(x);
        const sample w       = sample(0.3);
        const sample tol     = tolerance<sample>();
        using reverb_t       = stand_in_reverb<sample, 2>;
        using mix_t          = tap::mu::reverb_mix<sample, reverb_t>;
        const size_t calls[] = {64, 200, 7, 64, 1, 300, 364};
        for (const auto mode : {reverb_return::left, reverb_return::mid}) {
            reverb_t               rev;
            typename mix_t::config cfg;
            cfg.block_size = 64;
            cfg.wet        = w;
            cfg.mode       = mode;
            mix_t               mix(rev, cfg);
            std::vector<sample> y(n);
            size_t              off = 0;
            for (const size_t len : calls) {
                mix.process_block(x.data() + off, y.data() + off, len);
                off += len;
            }
            ASSERT_EQ(off, n);
            size_t bad = 0;
            for (size_t i = 0; i < n; ++i) {
                const sample r      = (mode == reverb_return::left) ? ref[0][i] : sample(0.5) * (ref[0][i] + ref[1][i]);
                const sample expect = (sample(1) - w) * x[i] + w * r;
                if (!(std::abs(y[i] - expect) <= tol * (sample(1) + std::abs(expect)))) {
                    ++bad;
                }
            }
            EXPECT_EQ(bad, 0U) << (mode == reverb_return::left ? "L" : "(L + R) / 2");
        }
    }

    // Wet 0 is the dry signal and wet 1 the return, sample for sample; a
    // live set_wet() takes effect from the next block; in place works.
    TYPED_TEST(reverb_mix_test, WetEndpointsAndLiveChanges) {
        using sample     = TypeParam;
        using reverb_t   = stand_in_reverb<sample, 2>;
        using mix_t      = tap::mu::reverb_mix<sample, reverb_t>;
        const size_t n   = 256;
        const auto   x   = ramp_signal<sample>(n);
        const auto   ref = reference_channels<sample, 2>(x);

        reverb_t               rev;
        typename mix_t::config cfg;
        cfg.wet = sample(0);
        mix_t               mix(rev, cfg);
        std::vector<sample> y = x; // in place
        mix.process_block(y.data(), y.data(), 64);
        for (size_t i = 0; i < 64; ++i) {
            EXPECT_EQ(y[i], x[i]) << i;
        }
        ASSERT_TRUE(mix.set_wet(sample(1)));
        mix.process_block(y.data() + 64, y.data() + 64, 64);
        for (size_t i = 64; i < 128; ++i) {
            EXPECT_EQ(y[i], ref[0][i]) << i;
        }
        EXPECT_EQ(mix.wet(), sample(1));
        EXPECT_EQ(mix.channels(), 2U);
        EXPECT_EQ(mix.block_size(), 64U);
        EXPECT_EQ(mix_t::latency(), 0U);
    }

    // The dry-only shift: y = (1 - w) s(x) + w r(x), the reverb fed the
    // UNshifted input.
    TYPED_TEST(reverb_mix_test, DryOnlyShiftFeedsTheReverbTheUnshiftedInput) {
        using sample            = TypeParam;
        using reverb_t          = stand_in_reverb<sample, 2>;
        using shift_t           = stand_in_shifter<sample>;
        using dry_t             = tap::mu::shifted_dry_mix<sample, shift_t, reverb_t>;
        const size_t        n   = 500;
        const auto          x   = ramp_signal<sample>(n);
        const auto          ref = reference_channels<sample, 2>(x);
        shift_t             ref_shift;
        std::vector<sample> s(n);
        ref_shift.process_block(x.data(), s.data(), n);

        reverb_t               rev;
        shift_t                sh;
        typename dry_t::config cfg;
        cfg.wet  = sample(0.25);
        cfg.mode = reverb_return::mid;
        dry_t               mix(sh, rev, cfg);
        std::vector<sample> y(n);
        mix.process_block(x.data(), y.data(), 130);
        mix.process_block(x.data() + 130, y.data() + 130, n - 130);
        const sample tol = tolerance<sample>();
        size_t       bad = 0;
        for (size_t i = 0; i < n; ++i) {
            const sample r      = sample(0.5) * (ref[0][i] + ref[1][i]);
            const sample expect = sample(0.75) * s[i] + sample(0.25) * r;
            if (!(std::abs(y[i] - expect) <= tol * (sample(1) + std::abs(expect)))) {
                ++bad;
            }
        }
        EXPECT_EQ(bad, 0U);
        EXPECT_EQ(dry_t::latency(), 0U);
    }

    // With the library's shifter at 0 Hz (its sample-exact bypass) and wet 0,
    // the dry-only stage is the identity.
    TYPED_TEST(reverb_mix_test, DryOnlyShiftAtZeroHertzAndZeroWetIsTheIdentity) {
        using sample   = TypeParam;
        using reverb_t = stand_in_reverb<sample, 2>;
        using shift_t  = tap::mu::frequency_shifter<sample>;
        using dry_t    = tap::mu::shifted_dry_mix<sample, shift_t, reverb_t>;
        typename shift_t::config sc;
        sc.shift_hz = sample(0);
        shift_t                sh(sc);
        reverb_t               rev;
        typename dry_t::config cfg;
        cfg.wet = sample(0);
        dry_t               mix(sh, rev, cfg);
        const auto          x = ramp_signal<sample>(300);
        std::vector<sample> y(x.size());
        mix.process_block(x.data(), y.data(), x.size());
        EXPECT_EQ(y, x);
    }

    // Both stages fit afc_chain's reverb slot.
    TYPED_TEST(reverb_mix_test, StagesFitTheChainSlot) {
        using sample   = TypeParam;
        using reverb_t = stand_in_reverb<sample, 2>;
        static_assert(tap::mu::afc_stage<tap::mu::reverb_mix<sample, reverb_t>, sample>);
        static_assert(
            tap::mu::afc_stage<tap::mu::shifted_dry_mix<sample, tap::mu::frequency_shifter<sample>, reverb_t>, sample>);
        static_assert(tap::mu::afc_stage<tap::mu::reverb_mix<sample, bare_reverb<sample>>, sample>);
        reverb_t                              rev;
        tap::mu::reverb_mix<sample, reverb_t> mix(rev, {});
        tap::mu::afc_stage_ref<sample>        ref(&mix);
        EXPECT_TRUE(static_cast<bool>(ref));
    }

    TEST(ReverbMixConfigValidation, RejectsBadConfigs) {
        using reverb_t = stand_in_reverb<float, 2>;
        using mix_t    = tap::mu::reverb_mix<float, reverb_t>;
        reverb_t rev;
        auto     make = [&](auto edit) {
            mix_t::config c;
            edit(c);
            return mix_t(rev, c);
        };
        EXPECT_THROW(make([](mix_t::config& c) { c.block_size = 0; }), std::invalid_argument);
        EXPECT_THROW(make([](mix_t::config& c) { c.channels = 0; }), std::invalid_argument);
        EXPECT_THROW(make([](mix_t::config& c) { c.channels = 3; }), std::invalid_argument); // reverb reports 2
        EXPECT_THROW(make([](mix_t::config& c) { c.wet = -0.1F; }), std::invalid_argument);
        EXPECT_THROW(make([](mix_t::config& c) { c.wet = 1.5F; }), std::invalid_argument);
        EXPECT_THROW(make([](mix_t::config& c) { c.wet = std::nanf(""); }), std::invalid_argument);
        EXPECT_NO_THROW(make([](mix_t::config& c) { c.wet = 1.0F; }));

        // One channel: the (L + R) / 2 return is refused, at construction and live.
        using mono_t = tap::mu::reverb_mix<float, stand_in_reverb<float, 1>>;
        stand_in_reverb<float, 1> mono;
        mono_t::config            mc;
        mc.channels = 1;
        mc.mode     = reverb_return::mid;
        EXPECT_THROW(mono_t(mono, mc), std::invalid_argument);
        mc.mode = reverb_return::left;
        mono_t m(mono, mc);
        EXPECT_FALSE(m.set_return_mode(reverb_return::mid));
        EXPECT_EQ(m.return_mode(), reverb_return::left);

        // set_wet refuses what the constructor refuses, changing nothing.
        EXPECT_FALSE(m.set_wet(-0.01F));
        EXPECT_FALSE(m.set_wet(1.01F));
        EXPECT_FALSE(m.set_wet(std::numeric_limits<float>::infinity()));
        EXPECT_EQ(m.wet(), 0.3F);
    }

    TEST(ReverbMixRtContract, PostConstructionEntryPointsAreNoexcept) {
        using rf   = stand_in_reverb<float, 2>;
        using mixf = tap::mu::reverb_mix<float, rf>;
        using dryf = tap::mu::shifted_dry_mix<float, tap::mu::frequency_shifter<float>, rf>;
        static_assert(noexcept(std::declval<mixf&>().process_block(nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<mixf&>().set_wet(0.0F)));
        static_assert(noexcept(std::declval<mixf&>().set_return_mode(reverb_return::left)));
        static_assert(noexcept(std::declval<const mixf&>().wet()));
        static_assert(noexcept(mixf::latency()));
        static_assert(mixf::latency() == 0);
        static_assert(noexcept(std::declval<dryf&>().process_block(nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<dryf&>().set_wet(0.0F)));
        static_assert(noexcept(std::declval<dryf&>().set_return_mode(reverb_return::mid)));
        static_assert(dryf::latency() == 0);
        SUCCEED();
    }

} // namespace
