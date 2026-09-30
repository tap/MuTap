// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Contract tests for tests/support/portable_random.h: the fixtures' random
// variates are one sequence on every host, and on libstdc++ that sequence is
// std::normal_distribution / std::bernoulli_distribution / generate_canonical
// bit for bit (so every number measured on libstdc++ before the header still
// holds).
#include <cstddef>
#include <cstdint>
#include <random>

#include <gtest/gtest.h>

#include "support/portable_random.h"

namespace {

    using mutap_test::bernoulli;
    using mutap_test::canonical;
    using mutap_test::normal;

    // canonical() is exact integer-to-binary arithmetic (every product is a
    // power-of-two scaling), so its values are pinned to the bit everywhere.
    TEST(PortableRandom, CanonicalIsPinnedToTheBitOnEveryHost) {
        std::mt19937 gen(5);
        const double expected[] = {0x1.c40919c71a917p-5, 0x1.a9a3cd7bbdd0ap-1, 0x1.747771d8d3ae3p-2,
                                   0x1.f579d093d6543p-1, 0x1.6fe82e47d0884p-4, 0x1.96421efa726dp-2};
        for (const double e : expected) {
            EXPECT_EQ(canonical<double>(gen), e);
        }
    }

    // The draws are pinned exactly; a variate may differ in its last bits
    // where std::log is not correctly rounded or V1^2 + V2^2 is contracted.
    TEST(PortableRandom, NormalSequenceIsPinnedOnEveryHost) {
        std::mt19937   gen(5);
        normal<double> dist;
        const double   expected[] = {0.10779446491934412,  -0.030636274259069458, -0.19970652250066045,
                                     -0.79326673457454833, -0.20216368165165433,  -2.2068542825343695,
                                     1.0701633291067083,   0.50065424607233389};
        for (const double e : expected) {
            EXPECT_NEAR(dist(gen), e, 1e-14);
        }

        std::mt19937  gen_f(5);
        normal<float> dist_f(0.0f, 0.3f);
        const float   expected_f[] = {0.0298930928f, 0.0334482454f, -0.166886151f, -0.359191209f};
        for (const float e : expected_f) {
            EXPECT_NEAR(dist_f(gen_f), e, 1e-6f);
        }
    }

    TEST(PortableRandom, BernoulliSequenceIsPinnedOnEveryHost) {
        std::mt19937 gen(5);
        const bool   expected[] = {true,  false, true,  false, true, true,  true, true,
                                   false, false, false, false, true, false, true, true};
        for (const bool e : expected) {
            EXPECT_EQ(bernoulli(gen, 0.5), e);
        }
    }

#if defined(__GLIBCXX__)
    constexpr unsigned k_seeds[] = {0U, 1U, 2U, 5U, 77U, 0xB0B0U};

    TEST(PortableRandom, MatchesLibstdcxxBitForBit) {
        constexpr size_t k_draws = 20000; // x 6 seeds x 4 variates: seconds under emulation
        for (const unsigned seed : k_seeds) {
            std::mt19937                     a(seed);
            std::mt19937                     b(seed);
            normal<double>                   mine(0.0, 1.0);
            std::normal_distribution<double> theirs(0.0, 1.0);
            for (size_t i = 0; i < k_draws; ++i) {
                ASSERT_EQ(mine(a), theirs(b)) << "normal<double>, seed " << seed << ", draw " << i;
            }

            std::mt19937                    af(seed);
            std::mt19937                    bf(seed);
            normal<float>                   mine_f(0.0f, 0.3f);
            std::normal_distribution<float> theirs_f(0.0f, 0.3f);
            for (size_t i = 0; i < k_draws; ++i) {
                ASSERT_EQ(mine_f(af), theirs_f(bf)) << "normal<float>, seed " << seed << ", draw " << i;
            }

            std::mt19937                ab(seed);
            std::mt19937                bb(seed);
            std::bernoulli_distribution theirs_b(0.5);
            for (size_t i = 0; i < k_draws; ++i) {
                ASSERT_EQ(bernoulli(ab, 0.5), theirs_b(bb)) << "bernoulli, seed " << seed << ", draw " << i;
            }

            std::mt19937 ac(seed);
            std::mt19937 bc(seed);
            for (size_t i = 0; i < k_draws; ++i) {
                ASSERT_EQ(canonical<double>(ac), (std::generate_canonical<double, 53>(bc)))
                    << "canonical<double>, seed " << seed << ", draw " << i;
            }
        }
    }
#else
    TEST(PortableRandom, MatchesLibstdcxxBitForBit) {
        GTEST_SKIP() << "not libstdc++: the pinned-sequence tests above carry the contract here";
    }
#endif

} // namespace
