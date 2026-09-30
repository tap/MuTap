// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The forward-path frequency shifter (mutap/frequency_shifter.h): single
// sideband where the voice lives, a sample-exact bypass at 0 Hz, the group
// delay the header quotes, float tracking double, a float oscillator that
// holds its amplitude, and the real-time contract.
//
// Measurement (tones): a unit cosine at f, shifted, settles for 0.5 s (the
// slowest allpass pole, c^2 = 0.99750 in z^-2, decays 1/e in ~800 samples at
// 48 kHz), then a Hann-windowed DFT over EXACTLY one second: every
// integer-hertz frequency sits on a bin, so the wanted line at f + shift,
// the image at f - shift and the input's own f leak nothing into each
// other's bins and the ratios below are the shifter's, not the window's.
// The analysis runs in double on every target.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/frequency_shifter.h"

namespace {

    using tap::mu::frequency_shifter;

    /// Unit cosine at `hz`, from n = 0 (a double rotator: no per-sample
    /// libm call, which matters under soft-float emulation).
    template <typename Sample>
    std::vector<Sample> tone(double hz, double fs, size_t n) {
        std::vector<Sample>        x(n);
        const std::complex<double> step = std::polar(1.0, 2.0 * std::numbers::pi * hz / fs);
        std::complex<double>       z(1.0, 0.0);
        for (size_t i = 0; i < n; ++i) {
            x[i] = static_cast<Sample>(z.real());
            z *= step;
        }
        return x;
    }

    /// Hann-windowed DFT of x at `hz` over [from, from + fs), phase
    /// referenced to n = 0 (so a cosine from n = 0 reads phase 0).
    template <typename Sample>
    std::complex<double> dft_at(const std::vector<Sample>& x, size_t from, double hz, double fs) {
        const auto                 n     = static_cast<size_t>(fs);
        const std::complex<double> step  = std::polar(1.0, -2.0 * std::numbers::pi * hz / fs);
        const std::complex<double> wstep = std::polar(1.0, 2.0 * std::numbers::pi / static_cast<double>(n));
        std::complex<double>       z = std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(from) / fs);
        std::complex<double>       wz(1.0, 0.0);
        std::complex<double>       acc(0.0, 0.0);
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * wz.real();
            acc += w * static_cast<double>(x[from + i]) * z;
            z *= step;
            wz *= wstep;
        }
        return acc;
    }

    template <typename Sample>
    std::vector<Sample> shifted(const std::vector<Sample>& x, double fs, double shift_hz, size_t block = 64) {
        frequency_shifter<Sample> sh({fs, static_cast<Sample>(shift_hz)});
        std::vector<Sample>       y(x.size());
        for (size_t i = 0; i < x.size(); i += block) {
            sh.process_block(&x[i], &y[i], std::min(block, x.size() - i));
        }
        return y;
    }

    /// Deterministic uniform noise in [-1, 1) (a 64-bit LCG: same stream on
    /// every target, no <random> distribution differences), one sample at a
    /// time so a long run needs no buffer.
    class lcg_noise {
      public:
        explicit lcg_noise(std::uint64_t seed)
            : m_state(seed) {}

        double next() {
            m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>(m_state >> 11) * 0x1.0p-52 - 1.0;
        }

      private:
        std::uint64_t m_state;
    };

    template <typename Sample>
    std::vector<Sample> noise(size_t n, std::uint64_t seed) {
        std::vector<Sample> x(n);
        lcg_noise           gen(seed);
        for (auto& v : x) {
            v = static_cast<Sample>(gen.next());
        }
        return x;
    }

    template <typename Sample>
    class frequency_shifter_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(frequency_shifter_test, sample_types);

    // A tone at f comes out at f + shift: the wanted line carries the input's
    // level, and the image at f - shift and the input's own f sit far below
    // it. Image rejection is a property of the Hilbert pair at f, so it does
    // not depend on the shift (measured identical at +2, +5 and -5 Hz), and
    // float and double agree to 0.01 dB. Measured (dB):
    //
    //   f (Hz)       30     100     300    1000    4000
    //   48 kHz    44.25   55.67   44.80   49.00   44.34
    //   16 kHz    62.20   44.80   45.72   53.99   >150 (fs/4: exactly 90 deg)
    //
    // (16 kHz at f reads 48 kHz at 3f: the design is fixed in normalized
    // frequency.) Each floor below is its measured value less 1 dB (100 dB
    // for the fs/4 point, where float rounding is the limit: 150.89). Gain of
    // the wanted line: within 0.0002 dB of the input's, both precisions. The
    // input's own line: >= 130.95 dB below the wanted one in float, >= 222.26
    // in double.
    TYPED_TEST(frequency_shifter_test, ToneLandsAtShiftedFrequencySingleSideband) {
        struct point {
            double fs;
            double f;
            double min_rejection_db;
        };
        const point points[] = {
            {48000.0, 30.0, 43.25},   {48000.0, 100.0, 54.67},  {48000.0, 300.0, 43.80}, {48000.0, 1000.0, 48.00},
            {48000.0, 4000.0, 43.34}, {16000.0, 30.0, 61.20},   {16000.0, 100.0, 43.80}, {16000.0, 300.0, 44.72},
            {16000.0, 1000.0, 52.99}, {16000.0, 4000.0, 100.0},
        };
        for (const auto& p : points) {
            const auto settle = static_cast<size_t>(p.fs / 2.0);
            const auto n      = settle + static_cast<size_t>(p.fs);
            const auto x      = tone<TypeParam>(p.f, p.fs, n);
            for (const double s : {2.0, 5.0, -5.0}) {
                const auto   y       = shifted(x, p.fs, s);
                const double want    = std::abs(dft_at(y, settle, p.f + s, p.fs));
                const double image   = std::abs(dft_at(y, settle, p.f - s, p.fs));
                const double carrier = std::abs(dft_at(y, settle, p.f, p.fs));
                const double gain_db = 20.0 * std::log10(want / (p.fs / 4.0)); // unit cosine, Hann: |X| = N / 4
                const double rej_db  = 20.0 * std::log10(want / image);
                const double car_db  = 20.0 * std::log10(want / carrier);
                std::printf("fs %5.0f f %6.0f shift %+2.0f: gain %+.4f dB, image rejection %.2f dB, "
                            "input line %.2f dB below\n",
                            p.fs, p.f, s, gain_db, rej_db, car_db);
                EXPECT_LT(std::abs(gain_db), 0.005) << p.fs << " Hz, f " << p.f << ", shift " << s;
                EXPECT_GT(rej_db, p.min_rejection_db) << p.fs << " Hz, f " << p.f << ", shift " << s;
                EXPECT_GT(car_db, 110.0) << p.fs << " Hz, f " << p.f << ", shift " << s;
            }
        }
    }

    // Shift 0 is a bypass: the output IS the input, sample for sample, out of
    // place and in place, from construction and after running shifted.
    TYPED_TEST(frequency_shifter_test, ZeroShiftIsSampleExactIdentity) {
        const auto x = noise<TypeParam>(48000, 7);

        frequency_shifter<TypeParam> sh({48000.0, TypeParam(0)});
        std::vector<TypeParam>       y(x.size());
        for (size_t i = 0; i < x.size(); i += 64) {
            sh.process_block(&x[i], &y[i], 64);
        }
        EXPECT_EQ(y, x);

        // Run shifted for a while, then drop to 0 mid-stream, in place.
        frequency_shifter<TypeParam> live({48000.0, TypeParam(5)});
        std::vector<TypeParam>       z = x;
        live.process_block(z.data(), z.data(), 24000);
        ASSERT_TRUE(live.set_shift_hz(TypeParam(0)));
        live.process_block(&z[24000], &z[24000], 24000);
        for (size_t i = 24000; i < x.size(); ++i) {
            ASSERT_EQ(z[i], x[i]) << "sample " << i;
        }
    }

    // The caller's block partition does not change a single output sample:
    // the oscillator renormalizes on its own 64-sample clock, not per call.
    TYPED_TEST(frequency_shifter_test, BlockPartitionDoesNotChangeTheOutput) {
        const auto x   = noise<TypeParam>(9600, 3);
        const auto ref = shifted(x, 48000.0, 5.0, 64);
        for (const size_t block : {size_t{1}, size_t{7}, size_t{100}, size_t{9600}}) {
            EXPECT_EQ(shifted(x, 48000.0, 5.0, block), ref) << "block " << block;
        }
    }

    // The shifted path's group delay, from the phase slope of the output
    // line over f +- 1 Hz (frequency_shifter::latency() quotes these). It is
    // that of the analytic pair, between the in-phase chain's and the
    // quadrature chain's (by freqz at 48 kHz, 100 Hz: 2.5613 and 2.6323 ms).
    // Measured, identical in float and double to 0.0001 ms:
    //
    //   f (Hz)        100      200      300     1000
    //   48 kHz     2.5969   1.3757   0.9363   0.2902   ms
    //   16 kHz     2.8088   1.4357   0.9653   0.2996   ms
    TYPED_TEST(frequency_shifter_test, GroupDelay) {
        struct row {
            double fs;
            double ms[4];
        };
        const double freqs[] = {100.0, 200.0, 300.0, 1000.0};
        const row    rows[]  = {
            {48000.0, {2.5969, 1.3757, 0.9363, 0.2902}},
            {16000.0, {2.8088, 1.4357, 0.9653, 0.2996}},
        };
        const double s = 5.0;
        for (const auto& r : rows) {
            const auto settle = static_cast<size_t>(r.fs / 2.0);
            const auto n      = settle + static_cast<size_t>(r.fs);
            auto       phase  = [&](double f) {
                const auto x = tone<TypeParam>(f, r.fs, n);
                const auto y = shifted(x, r.fs, s);
                return std::arg(dft_at(y, settle, f + s, r.fs) / dft_at(x, settle, f, r.fs));
            };
            for (size_t k = 0; k < 4; ++k) {
                const double dphi =
                    std::remainder(phase(freqs[k] + 1.0) - phase(freqs[k] - 1.0), 2.0 * std::numbers::pi);
                const double gd_ms = -dphi / (2.0 * std::numbers::pi * 2.0) * 1000.0;
                std::printf("fs %5.0f group delay at %4.0f Hz: %.4f ms\n", r.fs, freqs[k], gd_ms);
                EXPECT_NEAR(gd_ms, r.ms[k], 0.01) << r.fs << " Hz, " << freqs[k] << " Hz";
            }
        }
    }

    // float32 tracks the double shifter over 10 s of full-band noise at
    // 48 kHz, through a live shift change (+5 -> -3 Hz) at 5 s. Measured on
    // the x86_64 host: max |float - double| 4.402e-05 (input in [-1, 1)),
    // error power -98.75 dB re the output.
    //
    // Streamed block by block: the noise is generated per 64-sample block and
    // the statistics accumulate as it goes, so the test holds 1.5 KB of block
    // buffers on the stack; its peak heap, measured on the host with a
    // counting operator new, is 872 bytes, gtest's bookkeeping included.
    // (Whole-signal buffers, 480000 samples in double and float in and out,
    // were 11.5 MB: the Cortex-M55 leg's heap, the rest of 2 MB of ISRAM,
    // threw bad_alloc.) The same stream as the buffered version: on the host
    // the statistics are bit-identical to it.
    TEST(FrequencyShifterCrossPrecision, FloatTracksDouble) {
        constexpr size_t          n     = 480000;
        constexpr size_t          block = 64;
        lcg_noise                 gen(11);
        frequency_shifter<double> d({48000.0, 5.0});
        frequency_shifter<float>  f({48000.0, 5.0F});
        std::array<double, block> xd{};
        std::array<double, block> yd{};
        std::array<float, block>  xf{};
        std::array<float, block>  yf{};
        double                    max_diff = 0.0;
        double                    ref_sq   = 0.0;
        double                    diff_sq  = 0.0;
        for (size_t i = 0; i < n; i += block) {
            if (i == n / 2) {
                d.set_shift_hz(-3.0);
                f.set_shift_hz(-3.0F);
            }
            for (size_t k = 0; k < block; ++k) {
                xd[k] = gen.next();
                xf[k] = static_cast<float>(xd[k]);
            }
            d.process_block(xd.data(), yd.data(), block);
            f.process_block(xf.data(), yf.data(), block);
            for (size_t k = 0; k < block; ++k) {
                const double e = static_cast<double>(yf[k]) - yd[k];
                max_diff       = std::max(max_diff, std::abs(e));
                ref_sq += yd[k] * yd[k];
                diff_sq += e * e;
            }
        }
        const double rel_db = 10.0 * std::log10(diff_sq / ref_sq);
        std::printf("float - double over 10 s: max |diff| %.3e, error power %.2f dB re output\n", max_diff, rel_db);
        EXPECT_LT(max_diff, 2e-4) << "measured 4.402e-05";
        EXPECT_LT(rel_db, -90.0) << "measured -98.75 dB";
    }

    // The float oscillator holds its amplitude over 10 minutes: the RMS of
    // a 1 kHz tone shifted +5 Hz, in the last second against the second
    // second. Measured: 0.707106225 and 0.707106224, a drift of -5.548e-09 dB,
    // both 5.6e-7 under a unit cosine's 0.707106781 (the rotator's magnitude
    // sags by up to 1.45e-6 between renormalizations; unrenormalized it ends
    // the 10 minutes at 0.625 of unity, -4.1 dB). Host only: 28.8 M samples is
    // too long for the emulated selections, whose float legs run the tone,
    // bypass and float-tracks-double tests instead.
    TEST(FrequencyShifterDrift, Float32OscillatorAmplitudeOverTenMinutes) {
        const double       fs     = 48000.0;
        const size_t       period = 48; // 1 kHz at 48 kHz, exactly
        const auto         cycle  = tone<float>(1000.0, fs, period);
        std::vector<float> x(64 * period); // 64 blocks of 48 = 48 blocks of 64
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = cycle[i % period];
        }
        frequency_shifter<float> sh({fs, 5.0F});
        std::vector<float>       y(x.size());
        const auto               second   = static_cast<size_t>(fs);
        const size_t             total    = 600 * second;
        double                   first_sq = 0.0;
        double                   last_sq  = 0.0;
        for (size_t t = 0; t < total; t += x.size()) {
            sh.process_block(x.data(), y.data(), x.size());
            for (size_t i = 0; i < y.size(); ++i) {
                const size_t k = t + i;
                if (k >= second && k < 2 * second) {
                    first_sq += static_cast<double>(y[i]) * static_cast<double>(y[i]);
                }
                else if (k >= total - second) {
                    last_sq += static_cast<double>(y[i]) * static_cast<double>(y[i]);
                }
            }
        }
        const double first = std::sqrt(first_sq / static_cast<double>(second));
        const double last  = std::sqrt(last_sq / static_cast<double>(second));
        const double drift = 20.0 * std::log10(last / first);
        std::printf("output RMS second 2: %.9f, second 600: %.9f, drift %.3e dB (unit cosine: %.9f)\n", first, last,
                    drift, std::sqrt(0.5));
        EXPECT_LT(std::abs(drift), 1e-5) << "measured -5.548e-09 dB";
        EXPECT_NEAR(last, std::sqrt(0.5), 1e-5) << "measured 0.707106224";
    }

    TEST(FrequencyShifterConfigValidation, RejectsBadConfigs) {
        using fsf       = frequency_shifter<float>;
        using fsd       = frequency_shifter<double>;
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_THROW(fsf({0.0, 5.0F}), std::invalid_argument);
        EXPECT_THROW(fsf({-48000.0, 5.0F}), std::invalid_argument);
        EXPECT_THROW(fsf({std::numeric_limits<double>::quiet_NaN(), 5.0F}), std::invalid_argument);
        EXPECT_THROW(fsf({std::numeric_limits<double>::infinity(), 5.0F}), std::invalid_argument);
        EXPECT_THROW(fsf({1e300, 5.0F}), std::invalid_argument); // infinite as float
        EXPECT_THROW(fsf({48000.0, nan}), std::invalid_argument);
        EXPECT_THROW(fsf({48000.0, inf}), std::invalid_argument);
        EXPECT_THROW(fsf({48000.0, 24000.0F}), std::invalid_argument);
        EXPECT_THROW(fsf({48000.0, -24000.0F}), std::invalid_argument);
        EXPECT_THROW(fsd({16000.0, 8000.0}), std::invalid_argument);
        EXPECT_NO_THROW(fsf({48000.0, -5.0F}));
        EXPECT_NO_THROW(fsd({16000.0, 0.0}));

        // A rejected live change leaves the shift as it was.
        fsf sh({48000.0, 5.0F});
        EXPECT_FALSE(sh.set_shift_hz(nan));
        EXPECT_FALSE(sh.set_shift_hz(24000.0F));
        EXPECT_EQ(sh.shift_hz(), 5.0F);
        EXPECT_TRUE(sh.set_shift_hz(-2.0F));
        EXPECT_EQ(sh.shift_hz(), -2.0F);
    }

    TEST(FrequencyShifterRtContract, PostConstructionEntryPointsAreNoexcept) {
        using fsf = frequency_shifter<float>;
        using fsd = frequency_shifter<double>;
        static_assert(noexcept(std::declval<fsf&>().process_block(nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<fsf&>().set_shift_hz(0.0F)));
        static_assert(noexcept(std::declval<fsf&>().reset()));
        static_assert(noexcept(std::declval<const fsf&>().shift_hz()));
        static_assert(noexcept(fsf::latency()));
        static_assert(noexcept(std::declval<fsd&>().process_block(nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<fsd&>().set_shift_hz(0.0)));
        static_assert(noexcept(std::declval<fsd&>().reset()));
        using hil = tap::mu::allpass_hilbert<float>;
        float re  = 0.0F;
        float im  = 0.0F;
        static_assert(noexcept(std::declval<hil&>().process(0.0F, re, im)));
        static_assert(noexcept(std::declval<hil&>().reset()));
        static_assert(fsf::latency() == 0);
        SUCCEED();
    }

} // namespace
