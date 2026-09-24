/// @file
/// @brief Re-export of the shared tap::dsp real FFT into the tap::mu namespace.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The real FFT used to live here as a vendored copy of Ooura's C with a
// wrapper. It now lives in DspTap (tap::dsp), consumed via the
// submodules/dsptap submodule and shared with the rest of the family:
// header-only, one numeric contract (Ooura's packing, exp(+i) sign and
// unnormalized inverse) over an engine selected per build. The double profile
// always runs the split-radix engine (tap/dsp/fft/split_radix.h, DspTap's
// C++20 port of Ooura's rdft, bit-identical to the C it replaced); the float
// profile runs the same engine unless the build selects CMSIS-DSP Helium
// (TAP_DSP_FFT_CMSIS, the default on the bare-metal Cortex-M55; FFT sizes
// 32 ... 4096 only) or Apple vDSP (TAP_DSP_FFT_ACCELERATE, which MuTap's root
// CMakeLists.txt turns off by default; see there). This header keeps the
// historical include path (`mutap/fft.h`) and the unqualified names
// (`real_fft`, `real_fft32`, `basic_real_fft`) working inside tap::mu, and
// carries MuTap's side of DspTap's Stage 4 contract: the ABI tag and the
// configured-size gate below.

#pragma once

#include <cstddef>
#include <stdexcept>

#include "tap/dsp/fft.h"

namespace tap::mu {

    using tap::dsp::basic_real_fft;
    using tap::dsp::real_fft;
    using tap::dsp::real_fft32;

    // THE ABI TAG (DspTap Stage 4, tap/DspTap#35; docs/fft-design.md, "MuTap
    // bump checklist"). basic_real_fft<float>'s object layout follows the
    // float engine the build selected, and so does the layout of every MuTap
    // class that holds a basic_real_fft<Sample> by value: partitioned_fdaf
    // (fdaf.h), partitioned_fdkf (fd_kalman.h), pem_afc (pem_afc.h),
    // residual_suppressor (postfilter.h) and nn_suppressor (nn_suppressor.h).
    // Their member functions are weak symbols in every image that
    // instantiates them, and a dynamic loader may coalesce weak definitions
    // across images (macOS dyld binds them to one definition; on ELF a
    // plugin resolves against the executable's exports first), so two
    // images built with different engines, loaded into one process, could
    // run one image's code over the other's layout.
    // Each of the five is therefore defined inside
    //
    //     namespace tap::mu::inline TAP_DSP_FFT_ABI { ... }
    //
    // (TAP_DSP_FFT_ABI is fft_split_radix, fft_cmsis or fft_vdsp, set by
    // tap/dsp/fft.h), which puts the engine in every mangled name and changes
    // nothing else: an inline namespace's members are members of tap::mu for
    // lookup, so `tap::mu::partitioned_fdaf<float>` and every unqualified use
    // resolve exactly as before, and no layout or generated code moves.
    // aec_chain and aec_chain_nn are not tagged themselves: their Canceller
    // and Post are template arguments, so the tag reaches their mangled names
    // through the arguments. The double instantiations carry the tag too
    // (the tag keys on the float default; double always runs the split-radix
    // engine), which costs nothing but a longer name.
    //
    // THE RULE IS TRANSITIVE. The hazard belongs to any class whose object
    // layout depends on the float engine and whose mangled name does not
    // carry it: one that holds a basic_real_fft<float>, any of the five
    // above at <float>, or an aec_chain<float, ...> by value — directly or
    // through std::optional / std::array / std::variant / a member struct —
    // without naming it as a template argument. Such a class must itself be
    // defined inside the tag (or take the embedder as a template argument,
    // as aec_chain does). Holders of the double profiles only are exempt:
    // the tag is keyed on the float default, and double always runs the
    // split-radix engine, so their layout is the same in every build (the
    // C ABI's MutapFdaf / MutapAfc / MutapAec in tools/capi, the ITU dump,
    // and MuTap-Max's externals, whose `engine` structs hold <double>
    // instances behind a unique_ptr, are all of this kind — nothing there
    // needs a tag). Test-local float wrappers (tests/support/itu_chain.h's
    // compliance_dut<float>, test_float32.cpp's f32_chain) are untagged and
    // harmless: they live in one test image with one engine.
    //
    // tests/test_fft_engine_contract.cpp pins the tag on every leg for the
    // five (by qualified name at compile time, by typeid at run time); it
    // cannot see a wrapper someone adds later, so the transitive rule is
    // enforced by review. Two more rules follow:
    //   - never forward-declare a tagged class in plain tap::mu
    //     (`template <typename> class partitioned_fdaf;` there declares a
    //     DIFFERENT class; the test's qualified names stop compiling if a
    //     header it includes does so, clang reporting an ambiguous name and
    //     g++ a parse error; CI's clang-format job greps for the header that
    //     forward-declares without ever including the definition, the case
    //     no compiler sees); include the header instead;
    //   - anything else whose code depends on the selected engine's numbers
    //     lives inside the tag as well: checked_fft_size below reads the
    //     engine's size range, so two differently built images must not
    //     share its definition either.

} // namespace tap::mu

/// The float engine's size range in this build, as text, selected by the same
/// defines tap/dsp/fft.h selects the engine by (a string literal: no code).
#if defined(TAP_DSP_FFT_CMSIS)
#define MUTAP_FFT_FLOAT_RANGE "32 ... 4096"
#elif defined(TAP_DSP_FFT_ACCELERATE)
#define MUTAP_FFT_FLOAT_RANGE "4 ... 2^20"
#else
#define MUTAP_FFT_FLOAT_RANGE "4 ... 2^30"
#endif

/// The tail of every configured-FFT-size error message
/// (fft_detail::checked_fft_size, below): the engine this build selected for
/// float (TAP_DSP_FFT_ABI_NAME, DspTap's literal of k_real_fft_abi_tag) and
/// its range, the double profile's range, and the CMSIS numbers, all as one
/// string literal.
#define MUTAP_FFT_SIZE_RANGES                                                                                           \
    " is not an FFT size this build supports (basic_real_fft::supports_size: the float engine is " TAP_DSP_FFT_ABI_NAME \
    ", " MUTAP_FFT_FLOAT_RANGE "; double runs split-radix, 4 ... 2^30; CMSIS-DSP on the"                                \
    " Cortex-M55 takes 32 ... 4096)"

// The literals above restate DspTap's numbers; these keep them honest.
static_assert(tap::dsp::basic_real_fft<double>::k_min_size == 4
                  && tap::dsp::basic_real_fft<double>::k_max_size == (std::size_t{1} << 30),
              "MUTAP_FFT_SIZE_RANGES states the double range as 4 ... 2^30");
#if defined(TAP_DSP_FFT_CMSIS)
static_assert(tap::dsp::basic_real_fft<float>::k_min_size == 32 && tap::dsp::basic_real_fft<float>::k_max_size == 4096,
              "MUTAP_FFT_FLOAT_RANGE states the CMSIS range as 32 ... 4096");
#elif defined(TAP_DSP_FFT_ACCELERATE)
static_assert(tap::dsp::basic_real_fft<float>::k_min_size == 4
                  && tap::dsp::basic_real_fft<float>::k_max_size == (std::size_t{1} << 20),
              "MUTAP_FFT_FLOAT_RANGE states the vDSP range as 4 ... 2^20");
#else
static_assert(tap::dsp::basic_real_fft<float>::k_min_size == 4
                  && tap::dsp::basic_real_fft<float>::k_max_size == (std::size_t{1} << 30),
              "MUTAP_FFT_FLOAT_RANGE states the split-radix range as 4 ... 2^30");
#endif

// Inside the tag: the check's code depends on the selected engine's range.
namespace tap::mu::inline TAP_DSP_FFT_ABI {

    namespace fft_detail {

        /// The configuration gate for an FFT size (DspTap Stage 4: "supports_size
        /// is the MANDATORY gate wherever N comes from configuration"). Returns
        /// n when basic_real_fft<Sample> supports it in this build, and throws
        /// std::invalid_argument(message) otherwise, from the constructor of
        /// the class that asked, before any buffer is sized from n (MuTap's
        /// config-error convention). Without it a size outside the engine's
        /// range reaches basic_real_fft's constructor, whose
        /// precondition is a debug-only assertion: in a release build that is
        /// undefined behaviour, and under CMSIS-DSP on the Cortex-M55 a
        /// HardFault at the first transform.
        ///
        /// The ranges (basic_real_fft<Sample>::k_min_size / k_max_size, powers
        /// of two): split-radix (double always; float by default) 4 ... 2^30;
        /// vDSP (float, TAP_DSP_FFT_ACCELERATE) 4 ... 2^20; CMSIS-DSP (float,
        /// TAP_DSP_FFT_CMSIS, the Cortex-M55 default) 32 ... 4096. What a
        /// MuTap block size maps to: N = 2 * block_size for the cancellers,
        /// analysis_blocks * block_size for the residual suppressor, and
        /// 2 * the trained hop for the learned suppressor.
        ///
        /// The message is a string literal from the call site, naming the
        /// class and how n is derived, followed by MUTAP_FFT_SIZE_RANGES:
        /// MuTap's config-error form, the one every validated() in
        /// include/mutap throws (a const char* literal; nothing in the
        /// library formats text). Observed on the way, not the reason: a
        /// first version that formatted the size with std::to_string grew
        /// the one-TU Cortex-M33 ratchet workloads past GCC's
        /// large-unit-insns budget, which then rationed inlining inside the
        /// split-radix engine (+0.37 % on fdkf and shadow, no MuTap loop
        /// changed; bench/README.md).
        /// @param n       the FFT size the caller is about to construct
        /// @param message the exception text, a literal
        template <typename Sample>
        std::size_t checked_fft_size(std::size_t n, const char* message) {
            if (!basic_real_fft<Sample>::supports_size(n)) {
                throw std::invalid_argument(message);
            }
            return n;
        }

    } // namespace fft_detail

} // namespace tap::mu::inline TAP_DSP_FFT_ABI
