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
    // tests/test_fft_engine_contract.cpp pins the tag on every leg (by
    // qualified name at compile time, by typeid at run time). Two rules
    // follow:
    //   - never forward-declare a tagged class in plain tap::mu
    //     (`template <typename> class partitioned_fdaf;` there declares a
    //     DIFFERENT class, and the test's qualified names turn ambiguous
    //     and stop compiling if a header it includes does so); include the
    //     header instead;
    //   - anything else whose code depends on the selected engine's numbers
    //     lives inside the tag as well: checked_fft_size below reads the
    //     engine's size range, so two differently built images must not
    //     share its definition either.

} // namespace tap::mu

/// The tail of every configured-FFT-size error message
/// (fft_detail::checked_fft_size, below): the engines' ranges, as text.
#define MUTAP_FFT_SIZE_RANGES                                                                                          \
    " is not an FFT size this build supports (basic_real_fft::supports_size: split-radix 4 ... 2^30;"                  \
    " CMSIS-DSP on the Cortex-M55 32 ... 4096; vDSP 4 ... 2^20)"

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
        /// class and how n is derived, followed by MUTAP_FFT_SIZE_RANGES. It
        /// is deliberately not formatted here (no std::string, no
        /// to_string): a first version that composed it pulled enough
        /// library code into every translation unit that GCC 13.2 re-decided
        /// its inlining inside the split-radix engine's cftleaf in the
        /// Cortex-M33 ratchet binaries, +0.37 % on the fdkf and shadow
        /// scenarios with no MuTap loop changed (bench/README.md).
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
