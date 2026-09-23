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

#include <cstdio>
#include <cstring>
#include <type_traits>
#include <typeinfo>

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
