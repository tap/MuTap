// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// MuTap's half of DspTap's Stage 4 engine contract (tap/DspTap#35;
// DspTap docs/fft-design.md, "MuTap bump checklist").
//
// THE ABI TAG. Every MuTap class that holds a basic_real_fft<Sample> by value
// (partitioned_fdaf, partitioned_fdkf, pem_afc, residual_suppressor,
// nn_suppressor) is defined inside tap::mu::inline TAP_DSP_FFT_ABI, so the
// build's float FFT engine is part of its mangled name (mutap/fft.h has the
// why). Pinned two ways, on every leg that runs the battery:
//   - at compile time, by naming each class through the tag namespace
//     explicitly (tap::mu::TAP_DSP_FFT_ABI::partitioned_fdaf): qualified
//     lookup into a namespace finds only what is declared there, so a class
//     moved back to plain tap::mu stops this file compiling, as does a
//     forward declaration of one in plain tap::mu (tap::mu::partitioned_fdaf
//     would then name two templates);
//   - at run time, by typeid: the type's name contains this build's tag
//     (tap::dsp::k_real_fft_abi_tag) and neither of the other two, for both
//     profiles and for the two second-level embedders (aec_chain and
//     aec_chain_nn), which carry the tag through their template arguments.
//     The bare-metal legs build with RTTI (the toolchain files pass no
//     -fno-rtti), so this runs on the M33 and M55 too.
//
// THE SIZE GATE. basic_real_fft's constructor states supports_size(n) as a
// debug-only precondition; in a release build a size outside the engine's
// range is undefined behaviour (under CMSIS-DSP on the Cortex-M55, a
// HardFault at the first transform). Every MuTap path that turns a
// configuration number into an FFT size therefore passes it through
// fft_detail::checked_fft_size (mutap/fft.h), which throws
// std::invalid_argument, MuTap's config-error convention. One test per path
// sweeps the power-of-two configurations from below to above every engine's
// range and requires construction to succeed exactly where
// basic_real_fft<Sample>::supports_size says so, in both profiles; the rows
// the plan names (16 and 8192 rejected under CMSIS, accepted by the
// split-radix engine) are stated explicitly per build. What each path maps
// to an FFT size:
//     partitioned_fdaf, partitioned_fdkf  N = 2 * block_size
//     pem_afc                             N = 2 * fdaf.block_size (its own gate,
//                                         checked before the core is built)
//     residual_suppressor                 N = analysis_blocks * block_size
//     nn_suppressor                       N = 2 * the weights' hop

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/fft.h"
#include "mutap/nn_chain.h"
#include "mutap/nn_suppressor.h"
#include "mutap/pem_afc.h"
#include "mutap/postfilter.h"

namespace {

    namespace tagged = tap::mu::TAP_DSP_FFT_ABI;

    // Compile-time half: each embedder IS the class declared in the tag
    // namespace, for both profiles.
    template <typename Sample>
    constexpr bool k_declared_in_tag =
        std::is_same_v<tap::mu::partitioned_fdaf<Sample>, tagged::partitioned_fdaf<Sample>>
        && std::is_same_v<tap::mu::partitioned_fdkf<Sample>, tagged::partitioned_fdkf<Sample>>
        && std::is_same_v<tap::mu::pem_afc<Sample>, tagged::pem_afc<Sample>>
        && std::is_same_v<tap::mu::residual_suppressor<Sample>, tagged::residual_suppressor<Sample>>
        && std::is_same_v<tap::mu::nn_suppressor<Sample>, tagged::nn_suppressor<Sample>>;
    static_assert(k_declared_in_tag<float>, "every float FFT embedder is defined inside the ABI tag");
    static_assert(k_declared_in_tag<double>, "every double FFT embedder is defined inside the ABI tag");

    constexpr const char* k_all_tags[] = {"fft_split_radix", "fft_cmsis", "fft_vdsp"};

    template <typename T>
    void expect_only_this_builds_tag() {
        const char* name = typeid(T).name();
        for (const char* tag : k_all_tags) {
            const bool expected = std::strcmp(tag, tap::dsp::k_real_fft_abi_tag) == 0;
            EXPECT_EQ(std::strstr(name, tag) != nullptr, expected) << name << " / " << tag;
        }
    }

    template <typename Sample>
    void expect_every_embedder_tagged() {
        expect_only_this_builds_tag<tap::mu::partitioned_fdaf<Sample>>();
        expect_only_this_builds_tag<tap::mu::partitioned_fdkf<Sample>>();
        expect_only_this_builds_tag<tap::mu::pem_afc<Sample>>();
        expect_only_this_builds_tag<
            tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>>();
        expect_only_this_builds_tag<tap::mu::residual_suppressor<Sample>>();
        expect_only_this_builds_tag<tap::mu::nn_suppressor<Sample>>();
        expect_only_this_builds_tag<tap::mu::aec_chain<Sample>>();
        expect_only_this_builds_tag<tap::mu::aec_chain_nn<Sample>>();
    }

    template <typename Sample>
    using fft = tap::mu::basic_real_fft<Sample>;

    /// Constructs `make()` and reports whether it was accepted. A rejection
    /// must be the size gate's std::invalid_argument, whose text names this
    /// build's float engine (k_real_fft_abi_tag) and carries the ranges, the
    /// CMSIS numbers included (any other exception fails the test).
    template <typename Make>
    bool accepted(Make make) {
        try {
            (void)make();
            return true;
        }
        catch (const std::invalid_argument& e) {
            const std::string what = e.what();
            EXPECT_NE(what.find(MUTAP_FFT_SIZE_RANGES), std::string::npos) << what;
            EXPECT_NE(what.find("CMSIS-DSP on the Cortex-M55 takes 32 ... 4096"), std::string::npos) << what;
            // The engine this build selected, by name (the float profile's).
            EXPECT_NE(what.find(tap::dsp::k_real_fft_abi_tag), std::string::npos) << what;
            return false;
        }
    }

    /// The largest power of two a size_t holds: twice it wraps to 0, which
    /// the gate must reject rather than pass on to the allocations.
    constexpr std::size_t k_top_power = std::numeric_limits<std::size_t>::max() / 2 + 1;

    template <typename Sample>
    bool fdaf_accepts(std::size_t block) {
        typename tap::mu::partitioned_fdaf<Sample>::config cfg;
        cfg.block_size = block;
        cfg.partitions = 1;
        return accepted([&] { return tap::mu::partitioned_fdaf<Sample>(cfg); });
    }

    template <typename Sample>
    bool fdkf_accepts(std::size_t block) {
        typename tap::mu::partitioned_fdkf<Sample>::config cfg;
        cfg.block_size = block;
        cfg.partitions = 1;
        return accepted([&] { return tap::mu::partitioned_fdkf<Sample>(cfg); });
    }

    template <typename Sample>
    bool pem_afc_accepts(std::size_t block) {
        typename tap::mu::pem_afc<Sample>::config cfg;
        cfg.fdaf.block_size             = block;
        cfg.fdaf.partitions             = 1;
        cfg.analysis_window             = std::max<std::size_t>(1024, 2 * block);
        cfg.predictor.analysis_capacity = cfg.analysis_window;
        return accepted([&] { return tap::mu::pem_afc<Sample>(cfg); });
    }

    template <typename Sample>
    bool suppressor_accepts(std::size_t block, std::size_t analysis_blocks) {
        typename tap::mu::residual_suppressor<Sample>::config cfg;
        cfg.block_size      = block;
        cfg.analysis_blocks = analysis_blocks;
        return accepted([&] { return tap::mu::residual_suppressor<Sample>(cfg); });
    }

    /// Zero weights at a geometry with the given hop (the network's values
    /// play no part in construction).
    tap::mu::nn_suppressor_weights weights_at_hop(std::size_t hop) {
        tap::mu::nn_suppressor_weights w;
        w.geometry.hop                = hop;
        const tap::mu::nn_geometry& g = w.geometry;
        w.dense_in_w.assign(g.dense * g.features(), 0.0F);
        w.dense_in_b.assign(g.dense, 0.0F);
        w.gru_w_ih.assign(3 * g.gru * g.dense, 0.0F);
        w.gru_w_hh.assign(3 * g.gru * g.gru, 0.0F);
        w.gru_b_ih.assign(3 * g.gru, 0.0F);
        w.gru_b_hh.assign(3 * g.gru, 0.0F);
        w.dense_out_w.assign(g.bands * g.gru, 0.0F);
        w.dense_out_b.assign(g.bands, 0.0F);
        return w;
    }

    template <typename Sample>
    bool nn_accepts(std::size_t hop) {
        typename tap::mu::nn_suppressor<Sample>::config cfg;
        cfg.weights = weights_at_hop(hop);
        return accepted([&] { return tap::mu::nn_suppressor<Sample>(cfg); });
    }

    /// Block sizes 4 ... 4096: FFT sizes 8 ... 8192 for the cancellers,
    /// below and above the CMSIS range and inside the split-radix one.
    std::vector<std::size_t> block_sweep() {
        std::vector<std::size_t> b;
        for (std::size_t n = 4; n <= 4096; n *= 2) {
            b.push_back(n);
        }
        return b;
    }

    template <typename Sample>
    void expect_cancellers_follow_the_range() {
        for (const std::size_t b : block_sweep()) {
            const bool expected = fft<Sample>::supports_size(2 * b);
            EXPECT_EQ(fdaf_accepts<Sample>(b), expected) << "partitioned_fdaf block " << b;
            EXPECT_EQ(fdkf_accepts<Sample>(b), expected) << "partitioned_fdkf block " << b;
            EXPECT_EQ(pem_afc_accepts<Sample>(b), expected) << "pem_afc block " << b;
        }
        // Above every engine's range, and a block size whose FFT size wraps
        // to 0: rejected by the gate, before any buffer is sized from it.
        EXPECT_FALSE(fdaf_accepts<Sample>(std::size_t{1} << 30));
        EXPECT_FALSE(fdkf_accepts<Sample>(std::size_t{1} << 30));
        EXPECT_FALSE(fdaf_accepts<Sample>(k_top_power));
        EXPECT_FALSE(fdkf_accepts<Sample>(k_top_power));
    }

    template <typename Sample>
    void expect_suppressor_follows_the_range() {
        for (const std::size_t b : block_sweep()) {
            for (const std::size_t a : {std::size_t{4}, std::size_t{8}}) {
                if (a * b > 8192) {
                    continue;
                }
                EXPECT_EQ(suppressor_accepts<Sample>(b, a), fft<Sample>::supports_size(a * b))
                    << "residual_suppressor block " << b << " x " << a;
            }
        }
        EXPECT_FALSE(suppressor_accepts<Sample>(k_top_power, 4));
    }

    template <typename Sample>
    void expect_nn_follows_the_range() {
        for (std::size_t hop = 16; hop <= 4096; hop *= 2) {
            EXPECT_EQ(nn_accepts<Sample>(hop), fft<Sample>::supports_size(2 * hop)) << "nn_suppressor hop " << hop;
        }
    }

} // namespace

TEST(FftEngineContract, EmbeddersAreDefinedInsideTheAbiTag) {
    // The static_asserts above are the test; this row records that they ran
    // and names the tag the binary was built under.
    EXPECT_TRUE(k_declared_in_tag<float>);
    EXPECT_TRUE(k_declared_in_tag<double>);
    std::printf("[ measured ] ABI tag: %s\n", tap::dsp::k_real_fft_abi_tag);
}

TEST(FftEngineContract, AbiTagIsInEveryEmbeddersMangledName) {
    expect_every_embedder_tagged<float>();
    expect_every_embedder_tagged<double>();
}

TEST(FftEngineContract, CancellerBlockSizeIsGatedBySupportsSize) {
    expect_cancellers_follow_the_range<float>();
    expect_cancellers_follow_the_range<double>();
}

TEST(FftEngineContract, SuppressorAnalysisSizeIsGatedBySupportsSize) {
    expect_suppressor_follows_the_range<float>();
    expect_suppressor_follows_the_range<double>();
}

TEST(FftEngineContract, LearnedSuppressorHopIsGatedBySupportsSize) {
    expect_nn_follows_the_range<float>();
    expect_nn_follows_the_range<double>();
}

// The plan's rows, stated per build rather than read from the predicate: the
// double profile always runs the split-radix engine (4 ... 2^30); the float
// profile runs CMSIS-DSP (32 ... 4096) on the Cortex-M55 leg, vDSP
// (4 ... 2^20) under TAP_DSP_FFT_ACCELERATE, the split-radix engine elsewhere.
TEST(FftEngineContract, ConfiguredSizesThisBuildRejects) {
    static_assert(fft<double>::k_min_size == 4 && fft<double>::k_max_size == (std::size_t{1} << 30));
    EXPECT_TRUE(fdaf_accepts<double>(8));    // N = 16
    EXPECT_TRUE(fdaf_accepts<double>(4096)); // N = 8192
#if defined(TAP_DSP_FFT_CMSIS)
    static_assert(fft<float>::k_min_size == 32 && fft<float>::k_max_size == 4096);
    EXPECT_FALSE(fdaf_accepts<float>(8));          // N = 16
    EXPECT_FALSE(fdaf_accepts<float>(4096));       // N = 8192
    EXPECT_TRUE(fdaf_accepts<float>(16));          // N = 32
    EXPECT_TRUE(fdaf_accepts<float>(2048));        // N = 4096
    EXPECT_FALSE(suppressor_accepts<float>(4, 4)); // N = 16
    EXPECT_FALSE(nn_accepts<float>(4096));         // N = 8192
#else
    EXPECT_TRUE(fdaf_accepts<float>(8));    // N = 16
    EXPECT_TRUE(fdaf_accepts<float>(4096)); // N = 8192
    EXPECT_TRUE(suppressor_accepts<float>(4, 4));
    EXPECT_TRUE(nn_accepts<float>(4096));
#endif
}

// pem_afc states its own gate before its core is constructed, so the
// rejection names pem_afc, not the core it wraps.
TEST(FftEngineContract, PemAfcRejectsBeforeItsCore) {
    tap::mu::pem_afc<float>::config cfg;
    cfg.fdaf.block_size             = std::size_t{1} << 30;
    cfg.fdaf.partitions             = 1;
    cfg.analysis_window             = 2 * cfg.fdaf.block_size;
    cfg.predictor.analysis_capacity = cfg.analysis_window;
    try {
        tap::mu::pem_afc<float> afc(cfg);
        ADD_FAILURE() << "an unsupported FFT size was accepted";
    }
    catch (const std::invalid_argument& e) {
        EXPECT_EQ(std::string(e.what()).rfind("pem_afc: ", 0), 0U) << e.what();
    }
}
