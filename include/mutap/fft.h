/// @file
/// @brief Re-export of the shared tap::dsp real FFT into the tap::mu namespace.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The real FFT (the Ooura wrapper with its CMSIS-DSP Helium / Apple vDSP float32
// backends) used to live here as a vendored copy. It now lives in DspTap
// (tap::dsp), consumed via the submodules/dsptap submodule — one wrapper shared
// with AmbiTap and the rest of the family. This header keeps the historical
// include path (`mutap/fft.h`) and the unqualified names (`real_fft`,
// `real_fft32`, `basic_real_fft`) working inside tap::mu.

#pragma once

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
    //     (its size range, say) lives inside the tag as well.

} // namespace tap::mu
