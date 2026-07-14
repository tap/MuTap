/// @file mutap.h
/// @brief Umbrella header for the MuTap adaptive audio-cleaning library.
///
/// MuTap is a header-only C++20 library of portable adaptive filters for
/// audio cleaning. The first tool is an acoustic feedback (howling)
/// canceller built on FDAF-PEM-AFROW: a partitioned-block frequency-domain
/// adaptive filter whose update is decorrelated from the near-end source by
/// prediction-error-method prewhitening. One float-parameterized core runs
/// on every target — desktop (Max/MSP, the float64 golden model), ARM
/// Cortex-M55 (Helium) and Qualcomm Hexagon (float HVX).
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#pragma once

#define MUTAP_VERSION_MAJOR 0
#define MUTAP_VERSION_MINOR 1
#define MUTAP_VERSION_PATCH 0

#include "mutap/fdaf.h"
#include "mutap/fft.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"
