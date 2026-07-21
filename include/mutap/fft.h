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

} // namespace tap::mu
