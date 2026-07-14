/* MuTap C ABI — FFI surface over the float64 adaptive filters.
 *
 * Build the shared library with -DMUTAP_BUILD_CAPI=ON. This header is the
 * contract for C/ctypes/cffi consumers (the notebooks re-declare the same
 * prototypes); it must stay in sync with mutap_capi.cpp.
 *
 * Two families, mirroring the C++ API:
 *   mutap_fdaf_*  — the naive partitioned-block FDAF (mutap::partitioned_fdaf<double>);
 *                   an open-loop echo canceller, and the biased-baseline
 *                   comparator for closed-loop feedback demos.
 *   mutap_afc_*   — the PEM-prewhitened feedback canceller
 *                   (mutap::pem_afc<double> with the speech-cascade
 *                   near-end model).
 *
 * All process calls handle EXACTLY the configured block size per call.
 * Errors: create returns NULL on invalid configuration. Every function
 * tolerates a NULL handle (no-op / zero return), so an unchecked failed
 * create degrades to silence, not a crash. Not thread-safe per handle.
 */
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MutapFdaf MutapFdaf;
typedef struct MutapAfc  MutapAfc;

/* ABI/version probe: MUTAP_VERSION_MAJOR*10000 + MINOR*100 + PATCH. */
unsigned mutap_version(void);

/* Shared knobs (both create functions):
 *   block_size          power of 2, >= 4; samples per process call
 *   partitions          filter length = block_size * partitions
 *   step_size           NLMS mu, in (0, 2); 0 selects the default 0.5
 *   relative_regularization  fraction of mean bin power added to the
 *                       normalizer; < 0 selects the default 1e-2
 *   ipc_step_scaling    nonzero: scale mu by IPC^2 (Wiener-flavored VSS)
 *   transient_freeze_ratio   skip blocks whose error power exceeds this
 *                       multiple of the smoothed error power; 0 disables */

MutapFdaf* mutap_fdaf_create(size_t block_size, size_t partitions, double step_size, double relative_regularization,
                             int ipc_step_scaling, double transient_freeze_ratio);
void       mutap_fdaf_destroy(MutapFdaf* h);

/* One block: error = desired - filter(input); adapts unless frozen. */
void   mutap_fdaf_process(MutapFdaf* h, const double* input, const double* desired, double* error);
void   mutap_fdaf_impulse_response(MutapFdaf* h, double* dest); /* filter_length() taps */
size_t mutap_fdaf_filter_length(const MutapFdaf* h);
size_t mutap_fdaf_block_size(const MutapFdaf* h);
double mutap_fdaf_ipc(const MutapFdaf* h);
void   mutap_fdaf_set_step_size(MutapFdaf* h, double mu);
void   mutap_fdaf_set_adaptation(MutapFdaf* h, int enabled);
void   mutap_fdaf_reset(MutapFdaf* h);
/* Deep copy (filter state, adaptation state): probe a converged filter
 * destructively without losing it. NULL in -> NULL out. */
MutapFdaf* mutap_fdaf_clone(const MutapFdaf* h);

MutapAfc* mutap_afc_create(size_t block_size, size_t partitions, double step_size, double relative_regularization,
                           int ipc_step_scaling, double transient_freeze_ratio);
void      mutap_afc_destroy(MutapAfc* h);

/* One block: e = y - F_hat(u); u = loudspeaker signal, y = microphone. */
void   mutap_afc_process(MutapAfc* h, const double* u, const double* y, double* e);
void   mutap_afc_impulse_response(MutapAfc* h, double* dest); /* filter_length() taps */
size_t mutap_afc_filter_length(const MutapAfc* h);
size_t mutap_afc_block_size(const MutapAfc* h);
double mutap_afc_ipc(const MutapAfc* h); /* of the PREWHITENED pair */
void   mutap_afc_set_step_size(MutapAfc* h, double mu);
void   mutap_afc_set_adaptation(MutapAfc* h, int enabled);
void   mutap_afc_reset(MutapAfc* h);
/* Deep copy, as mutap_fdaf_clone. */
MutapAfc* mutap_afc_clone(const MutapAfc* h);

#ifdef __cplusplus
}
#endif
