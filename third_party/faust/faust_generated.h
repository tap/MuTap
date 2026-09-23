/// @file faust_generated.h
/// @brief Every vendored FAUST class, float and double, in one include.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

// FAUSTFLOAT (FAUST's interface sample type) is a macro the generated headers
// read at inclusion: the _f32 classes are included with float and the _f64
// classes with double, so each class computes AND exchanges samples and
// parameters in its own precision. Any outer definition is restored after.
//
// The generated headers define namespace-scope static tables (the
// oscillators' 65536-entry sine/cosine tables) and so cost static storage in
// every translation unit that includes them. Host-only: MuTap's cross and
// bare-metal test builds do not compile this.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "faust_shim.h"

#pragma push_macro("FAUSTFLOAT")
#undef FAUSTFLOAT

MUTAP_FAUST_BEGIN_GENERATED

#define FAUSTFLOAT float
#include "generated/dattorro_f32.hpp"
#include "generated/dattorro_paper_f32.hpp"
#include "generated/icc_howl_detect_f32.hpp"
#include "generated/icc_suppressor_f32.hpp"
#undef FAUSTFLOAT

#define FAUSTFLOAT double
#include "generated/dattorro_f64.hpp"
#include "generated/dattorro_paper_f64.hpp"
#include "generated/icc_howl_detect_f64.hpp"
#include "generated/icc_suppressor_f64.hpp"
#undef FAUSTFLOAT

MUTAP_FAUST_END_GENERATED

#pragma pop_macro("FAUSTFLOAT")
