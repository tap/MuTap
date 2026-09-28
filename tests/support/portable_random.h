// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Portable random variates for the test fixtures: one scenario on every host.
//
// std::mt19937 is specified to the bit by the standard, but the
// distributions are not: std::normal_distribution and
// std::bernoulli_distribution are implementation-defined, so libstdc++
// (Linux, the Arm cross legs), libc++ (macOS, Hexagon) and the MSVC STL
// (Windows) turn the same seed into different rooms and different near-end
// signals. A single-seed closed-loop canary then runs a different trajectory
// per standard library, and a chaotic one can pass on one and not the other
// for reasons that have nothing to do with the arithmetic under test (the
// float burst canary did, at DspTap d9c1e33: tests/test_adaptation_control.cpp).
//
// These variates are defined here instead, over the raw mt19937 words:
//
//  - canonical<Real>(gen): the standard's generate_canonical
//    ([rand.util.canonical]) for mt19937, k = ceil(digits / 32) words,
//    S = sum_i word_i * 2^(32 i), S / 2^(32 k), accumulated in Real, with
//    a result that rounds up to 1 clamped to the largest Real below 1
//    (LWG 2524). k = 2 for double, 1 for float.
//  - normal<Real>: the polar method (Marsaglia & Bray 1964; Knuth, TAOCP
//    vol. 2, 3.4.1, Algorithm P): V1 = 2 U1 - 1, V2 = 2 U2 - 1,
//    S = V1^2 + V2^2, rejected unless 0 < S <= 1, both variates scaled by
//    sqrt(-2 ln S / S). The call returns V2's variate and keeps V1's for the
//    next call.
//  - bernoulli(gen, p): canonical<double>(gen) < p.
//
// That order (V2 first, V1 cached; 0 < S <= 1; the clamp) is the one
// libstdc++ uses, so every sequence here equals the std:: distribution on
// libstdc++ bit for bit (tests/test_portable_random.cpp pins it there):
// every number measured on the Linux hosts and the Arm legs before this
// header existed still describes the same scenario, and the other hosts now
// run it too. What stays per host is the last bits: std::log and std::sqrt
// (sqrt is correctly rounded; log need not be) and fp-contraction of
// V1^2 + V2^2 can move a variate by an ulp, never the sequence of draws.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace mutap_test {

    /// The standard's generate_canonical for std::mt19937, spelled out.
    template <typename Real>
    Real canonical(std::mt19937& gen) {
        static_assert(std::numeric_limits<Real>::radix == 2);
        constexpr int  k   = (std::numeric_limits<Real>::digits + 31) / 32;
        constexpr Real r   = Real(4294967296.0); // 2^32, the range of one word
        Real           sum = Real(0);
        Real           tmp = Real(1);
        for (int i = 0; i < k; ++i) {
            sum += static_cast<Real>(static_cast<std::uint32_t>(gen())) * tmp;
            tmp *= r;
        }
        Real u = sum / tmp;
        if (u >= Real(1)) {
            u = std::nextafter(Real(1), Real(0));
        }
        return u;
    }

    /// Gaussian variates by the polar method (see the file comment).
    template <typename Real>
    class normal {
      public:
        explicit normal(Real mean = Real(0), Real stddev = Real(1))
            : mean_(mean)
            , stddev_(stddev) {}

        Real operator()(std::mt19937& gen) {
            Real z;
            if (saved_available_) {
                saved_available_ = false;
                z                = saved_;
            }
            else {
                Real v1;
                Real v2;
                Real s;
                do {
                    v1 = Real(2) * canonical<Real>(gen) - Real(1);
                    v2 = Real(2) * canonical<Real>(gen) - Real(1);
                    s  = v1 * v1 + v2 * v2;
                } while (s > Real(1) || s == Real(0));
                const Real scale = std::sqrt(Real(-2) * std::log(s) / s);
                saved_           = v1 * scale;
                saved_available_ = true;
                z                = v2 * scale;
            }
            return z * stddev_ + mean_;
        }

      private:
        Real mean_;
        Real stddev_;
        Real saved_           = Real(0);
        bool saved_available_ = false;
    };

    /// True with probability p (see the file comment).
    inline bool bernoulli(std::mt19937& gen, double p) {
        return canonical<double>(gen) < p;
    }

} // namespace mutap_test
