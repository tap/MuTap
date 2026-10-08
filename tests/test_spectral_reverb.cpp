// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// tap::mu::spectral_reverb's plumbing (mutap/spectral_reverb.h): the dry path
// one block late, the flat decay against its rt60, the flat mode, the shape's
// bounds and renormalisation, the multi-mic bus sum, the afc_stage shape,
// config validation and the real-time contract. Float and double; this file
// builds on every target and both emulated selections run its float suite
// (tests/bare_metal_main.cpp, TEST_FILTER in tests/CMakeLists.txt). What the
// reverb does inside the loop is test_spectral_reverb_host.cpp (host-only)
// and docs/reverb-afc.md.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/afc_chain.h"
#include "mutap/spectral_reverb.h"

namespace {

    using tap::mu::spectral_shaping;

    template <typename Sample>
    class spectral_reverb_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(spectral_reverb_test, sample_types);

    template <typename Sample>
    std::vector<Sample> ramp_signal(size_t n) {
        std::vector<Sample> x(n);
        for (size_t i = 0; i < n; ++i) {
            // Deterministic, sign-changing, no libm: a sawtooth of period 37.
            x[i] = static_cast<Sample>(static_cast<double>(i % 37) / 18.0 - 1.0);
        }
        return x;
    }

    template <typename Sample>
    typename tap::mu::spectral_reverb<Sample>::config base_config() {
        typename tap::mu::spectral_reverb<Sample>::config c;
        c.block_size  = 64;
        c.sample_rate = Sample(48000);
        c.rt60        = Sample(1);
        c.wet         = Sample(0.3);
        return c;
    }

    /// A decaying, sign-changing test response of `taps` samples (no libm in
    /// the values beyond one pow per tap).
    template <typename Sample>
    std::vector<Sample> test_response(size_t taps, unsigned salt) {
        std::vector<Sample> h(taps);
        for (size_t i = 0; i < taps; ++i) {
            const double s = ((i * 7 + salt * 13) % 11 < 5) ? -1.0 : 1.0;
            h[i]           = static_cast<Sample>(s * std::pow(0.995, static_cast<double>(i))
                                                 * (1.0 + 0.1 * static_cast<double>((i + salt) % 5)));
        }
        return h;
    }

    // Wet 0 is the input exactly one block late (latency()), from the first
    // sample, across calls of one and of three blocks, and in place.
    TYPED_TEST(spectral_reverb_test, DryPathIsTheInputOneBlockLate) {
        using sample = TypeParam;
        auto c       = base_config<sample>();
        c.wet        = sample(0);
        tap::mu::spectral_reverb<sample> rev(c);
        const size_t                     b = rev.block_size();
        ASSERT_EQ(rev.latency(), b);
        ASSERT_EQ(rev.window_size(), 2 * b);
        ASSERT_EQ(rev.bins(), b + 1);
        const size_t n = 40 * b;
        const auto   x = ramp_signal<sample>(n);
        auto         y = x; // in place
        for (size_t i = 0; i < n;) {
            const size_t len = ((i / b) % 2 == 0) ? b : 3 * b;
            const size_t m   = std::min(len, n - i);
            rev.process_block(y.data() + i, y.data() + i, m);
            i += m;
        }
        // Measured worst error (macOS x86_64, AppleClang 17, Release): float
        // 2.98e-07, double 6.66e-16.
        const sample tol   = std::is_same_v<sample, float> ? sample(5e-6) : sample(1e-13);
        size_t       bad   = 0;
        sample       worst = sample(0);
        for (size_t i = 0; i < n; ++i) {
            const sample expect = (i < b) ? sample(0) : x[i - b];
            worst               = std::max(worst, std::abs(y[i] - expect));
            if (!(std::abs(y[i] - expect) <= tol)) {
                ++bad;
            }
        }
        std::printf("[ measured ] dry path, worst |error| %.3g\n", static_cast<double>(worst));
        EXPECT_EQ(bad, 0U) << "worst " << worst;
    }

    // The flat decay means its rt60: the all-wet response's per-block energy
    // falls 60 dB in rt60 seconds (least-squares slope from -5 to -35 dB of
    // the energy decay curve, Schroeder's T30), the impulse at the start of a
    // hop. Measured (macOS x86_64, AppleClang 17, Release): rt60 0.5 s reads
    // T30 0.5000 s and rt60 1.0 s reads 1.0000 s, float and double; the 2 %
    // allowance is for other targets' FFT engines.
    TYPED_TEST(spectral_reverb_test, FlatDecayMatchesItsRt60) {
        using sample = TypeParam;
        for (const sample rt60 : {sample(0.5), sample(1)}) {
            auto c = base_config<sample>();
            c.wet  = sample(1);
            c.rt60 = rt60;
            tap::mu::spectral_reverb<sample> rev(c);
            const size_t                     b = rev.block_size();
            const size_t                     n = static_cast<size_t>(1.5 * static_cast<double>(rt60) * 48000.0) / b * b;
            std::vector<sample>              x(n, sample(0));
            std::vector<sample>              y(n, sample(0));
            x[0] = sample(1);
            for (size_t i = 0; i < n; i += b) {
                rev.process_block(&x[i], &y[i], b);
            }
            // Schroeder integration in double (a test-side measurement).
            std::vector<double> edc(n);
            double              acc = 0.0;
            for (size_t i = n; i-- > 0;) {
                acc += static_cast<double>(y[i]) * static_cast<double>(y[i]);
                edc[i] = acc;
            }
            double sx  = 0.0;
            double sy  = 0.0;
            double sxx = 0.0;
            double sxy = 0.0;
            double m   = 0.0;
            for (size_t i = 0; i < n; ++i) {
                const double db = 10.0 * std::log10(edc[i] / edc[0]);
                if (db <= -5.0 && db >= -35.0) {
                    const double t = static_cast<double>(i) / 48000.0;
                    sx += t;
                    sy += db;
                    sxx += t * t;
                    sxy += t * db;
                    m += 1.0;
                }
            }
            ASSERT_GT(m, 100.0);
            const double t30 = -60.0 / ((m * sxy - sx * sy) / (m * sxx - sx * sx));
            std::printf("[ measured ] rt60 %.2f s: T30 %.4f s\n", static_cast<double>(rt60), t30);
            EXPECT_NEAR(t30, static_cast<double>(rt60), 0.02 * static_cast<double>(rt60)) << "rt60 " << rt60;
        }
    }

    // Flat mode ignores every reshape; from_path mode with an all-zero
    // response keeps its shape.
    TYPED_TEST(spectral_reverb_test, FlatModeAndEmptyResponsesKeepTheShape) {
        using sample = TypeParam;
        auto       c = base_config<sample>();
        const auto h = test_response<sample>(1024, 1);
        {
            tap::mu::spectral_reverb<sample> flat(c);
            EXPECT_EQ(flat.shaping(), spectral_shaping::flat);
            EXPECT_FALSE(flat.reshape_from_impulse_response(h.data(), h.size()));
            std::vector<sample> mag(flat.bins(), sample(1));
            mag[3] = sample(1e-3);
            EXPECT_FALSE(flat.reshape(mag.data(), mag.size()));
            for (size_t k = 0; k < flat.bins(); ++k) {
                EXPECT_EQ(flat.bin_decay(k), flat.bin_decay(0));
                EXPECT_EQ(flat.bin_wet(k), c.wet);
            }
        }
        c.shaping = spectral_shaping::from_path;
        tap::mu::spectral_reverb<sample> rev(c);
        const sample                     a0 = rev.bin_decay(0);
        const std::vector<sample>        zeros(1024, sample(0));
        EXPECT_FALSE(rev.reshape_from_impulse_response(zeros.data(), zeros.size()));
        EXPECT_FALSE(rev.reshape_from_impulse_response(nullptr, 1024));
        std::vector<sample> short_mag(rev.bins() - 1, sample(1));
        EXPECT_FALSE(rev.reshape(short_mag.data(), short_mag.size()));
        for (size_t k = 0; k < rev.bins(); ++k) {
            EXPECT_EQ(rev.bin_decay(k), a0);
        }
    }

    // The shape: the allowance (median|P| / |P_k|), clamped to [shape_min,
    // shape_max], lengthens the decay time of the weak bins by at most
    // shape_max and shortens the strong ones by at most shape_min; the wet
    // gains keep the flat shape's sum of squares; ordering follows |P|.
    TYPED_TEST(spectral_reverb_test, ShapeIsBoundedAndRenormalised) {
        using sample = TypeParam;
        for (const sample cap : {sample(1), sample(2), sample(4)}) {
            auto c      = base_config<sample>();
            c.shaping   = spectral_shaping::from_path;
            c.shape_max = cap;
            tap::mu::spectral_reverb<sample> rev(c);
            const sample                     a_flat = rev.bin_decay(0);
            std::vector<sample>              mag(rev.bins());
            for (size_t k = 0; k < mag.size(); ++k) {
                mag[k] = static_cast<sample>(std::pow(10.0, -2.0 + 4.0 * static_cast<double>(k) / 64.0)); // 1e-2 .. 1e2
            }
            ASSERT_TRUE(rev.reshape(mag.data(), mag.size()));
            double sum_w2 = 0.0;
            for (size_t k = 0; k < rev.bins(); ++k) {
                // Decay time relative to flat: ln(a_flat) / ln(a_k).
                const double ratio =
                    std::log(static_cast<double>(a_flat)) / std::log(static_cast<double>(rev.bin_decay(k)));
                EXPECT_LE(ratio, static_cast<double>(cap) * (1.0 + 1e-4)) << "bin " << k;
                EXPECT_GE(ratio, static_cast<double>(c.shape_min) * (1.0 - 1e-4)) << "bin " << k;
                if (k > 0) {
                    EXPECT_LE(rev.bin_decay(k), rev.bin_decay(k - 1)) << "bin " << k; // stronger |P|, shorter decay
                }
                sum_w2 += static_cast<double>(rev.bin_wet(k)) * static_cast<double>(rev.bin_wet(k));
            }
            const double target = static_cast<double>(rev.bins()) * static_cast<double>(c.wet * c.wet);
            EXPECT_NEAR(sum_w2 / target, 1.0, 1e-4) << "cap " << cap;
            // The weakest bin rings exactly cap times longer than flat.
            EXPECT_NEAR(std::log(static_cast<double>(a_flat)) / std::log(static_cast<double>(rev.bin_decay(0))),
                        static_cast<double>(cap), 1e-3 * static_cast<double>(cap));
        }
    }

    // Several microphones on one bus: shaping from their responses is
    // shaping from the coherent sum of them.
    TYPED_TEST(spectral_reverb_test, BusShapeIsTheCoherentSum) {
        using sample           = TypeParam;
        auto c                 = base_config<sample>();
        c.shaping              = spectral_shaping::from_path;
        const auto          h1 = test_response<sample>(1024, 1);
        const auto          h2 = test_response<sample>(1024, 4);
        std::vector<sample> sum(1024);
        for (size_t i = 0; i < sum.size(); ++i) {
            sum[i] = h1[i] + h2[i];
        }
        tap::mu::spectral_reverb<sample> a(c);
        tap::mu::spectral_reverb<sample> b(c);
        const sample* const              irs[2] = {h1.data(), h2.data()};
        ASSERT_TRUE(a.reshape_from_impulse_responses(irs, 2, 1024));
        ASSERT_TRUE(b.reshape_from_impulse_response(sum.data(), sum.size()));
        const sample tol = std::is_same_v<sample, float> ? sample(1e-5) : sample(1e-12);
        for (size_t k = 0; k < a.bins(); ++k) {
            EXPECT_NEAR(a.bin_decay(k), b.bin_decay(k), tol) << k;
            EXPECT_NEAR(a.bin_wet(k), b.bin_wet(k), tol) << k;
        }
        const sample* const bad[2] = {h1.data(), nullptr};
        EXPECT_FALSE(a.reshape_from_impulse_responses(bad, 2, 1024));
    }

    // A call that is not a whole number of blocks: the whole blocks are
    // processed and the remainder is silence. reset() silences the tail and
    // keeps the shape; set_wet() scales the shaped gains.
    TYPED_TEST(spectral_reverb_test, RemainderResetAndWet) {
        using sample = TypeParam;
        auto c       = base_config<sample>();
        c.shaping    = spectral_shaping::from_path;
        tap::mu::spectral_reverb<sample> rev(c);
        const auto                       h = test_response<sample>(1024, 2);
        ASSERT_TRUE(rev.reshape_from_impulse_response(h.data(), h.size()));
        const size_t        b = rev.block_size();
        const auto          x = ramp_signal<sample>(2 * b + 5);
        std::vector<sample> y(x.size(), sample(7));
        rev.process_block(x.data(), y.data(), x.size());
        for (size_t i = 2 * b; i < y.size(); ++i) {
            EXPECT_EQ(y[i], sample(0)) << i;
        }
        const sample w5 = rev.bin_wet(5);
        const sample a5 = rev.bin_decay(5);
        rev.reset();
        std::vector<sample> zeros(b, sample(0));
        std::vector<sample> out(b, sample(1));
        rev.process_block(zeros.data(), out.data(), b);
        for (const sample v : out) {
            EXPECT_EQ(v, sample(0));
        }
        EXPECT_EQ(rev.bin_decay(5), a5);
        EXPECT_TRUE(rev.set_wet(sample(0.15)));
        EXPECT_NEAR(rev.bin_wet(5), w5 * sample(0.5), std::abs(w5) * sample(1e-5));
        EXPECT_EQ(rev.wet(), sample(0.15));
        EXPECT_FALSE(rev.set_wet(sample(1.5)));
        EXPECT_FALSE(rev.set_wet(std::numeric_limits<sample>::quiet_NaN()));
        EXPECT_EQ(rev.wet(), sample(0.15));
    }

    // The class fits afc_chain's reverb slot.
    TYPED_TEST(spectral_reverb_test, FitsTheChainSlot) {
        using sample = TypeParam;
        static_assert(tap::mu::afc_stage<tap::mu::spectral_reverb<sample>, sample>);
        tap::mu::spectral_reverb<sample> rev(base_config<sample>());
        tap::mu::afc_stage_ref<sample>   ref(&rev);
        EXPECT_TRUE(static_cast<bool>(ref));
    }

    TEST(SpectralReverbConfigValidation, RejectsBadConfigs) {
        using rv  = tap::mu::spectral_reverb<float>;
        auto make = [](auto edit) {
            rv::config c;
            edit(c);
            return rv(c);
        };
        EXPECT_THROW(make([](rv::config& c) { c.block_size = 100; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.block_size = 2; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.sample_rate = 0.0F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.rt60 = 0.0F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.rt60 = std::numeric_limits<float>::infinity(); }),
                     std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.wet = -0.1F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.wet = 1.5F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.wet = std::nanf(""); }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.shape = -1.0F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.shape_min = 0.0F; }), std::invalid_argument);
        EXPECT_THROW(make([](rv::config& c) { c.shape_max = 0.01F; }), std::invalid_argument); // < shape_min
        EXPECT_THROW(make([](rv::config& c) { c.shaping = static_cast<spectral_shaping>(7); }), std::invalid_argument);
        EXPECT_NO_THROW(make([](rv::config& c) { c.wet = 1.0F; }));
        EXPECT_NO_THROW(make([](rv::config& c) {
            c.shaping   = spectral_shaping::from_path;
            c.shape_max = 1.0F;
        }));
    }

    TEST(SpectralReverbRtContract, PostConstructionEntryPointsAreNoexcept) {
        using rv = tap::mu::spectral_reverb<float>;
        static_assert(noexcept(std::declval<rv&>().process_block(nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<rv&>().reshape(nullptr, 0)));
        static_assert(noexcept(std::declval<rv&>().reshape_from_impulse_response(nullptr, 0)));
        static_assert(noexcept(std::declval<rv&>().reshape_from_impulse_responses(nullptr, 0, 0)));
        static_assert(noexcept(std::declval<rv&>().reshape_flat()));
        static_assert(noexcept(std::declval<rv&>().set_wet(0.1F)));
        static_assert(noexcept(std::declval<rv&>().reset()));
        static_assert(noexcept(std::declval<const rv&>().latency()));
        static_assert(noexcept(std::declval<const rv&>().bin_decay(0)));
        SUCCEED();
    }

} // namespace
