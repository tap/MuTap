/* MuTap C ABI — FFI surface over the float64 adaptive filters.
 *
 * Build the shared library with -DMUTAP_BUILD_CAPI=ON. This header is the
 * contract for C/ctypes/cffi consumers (the notebooks re-declare the same
 * prototypes); it must stay in sync with mutap_capi.cpp.
 *
 * Three families, mirroring the C++ API:
 *   mutap_fdaf_*  — the naive partitioned-block FDAF (mutap::partitioned_fdaf<double>);
 *                   an open-loop echo canceller, and the biased-baseline
 *                   comparator for closed-loop feedback demos.
 *   mutap_afc_*   — the PEM-prewhitened feedback canceller
 *                   (mutap::pem_afc<double> with the speech-cascade
 *                   near-end model).
 *   mutap_aec_*   — the full acoustic-echo-cancellation chain
 *                   (mutap::aec_chain<double>): raw FD-Kalman canceller +
 *                   residual suppressor + comfort noise + initial receive
 *                   guard, built from the ITU compliance preset
 *                   (mutap::aec_chain_preset).
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
typedef struct MutapAec  MutapAec;

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

/* Same canceller with the frequency-warped (music/tonal) near-end model
 * instead of the speech cascade. lambda in (-1, 1) is the warping
 * coefficient (0 selects the room-robust default 0.5; pass a tiny epsilon
 * for a genuinely-zero warp); order 0 selects the default 16. Pass
 * ipc_step_scaling nonzero: the warped whitener needs the IPC step scale
 * to stay room-robust in the closed loop (include/mutap/lpc.h).
 * The returned handle is interchangeable with mutap_afc_create's. */
MutapAfc* mutap_afc_create_warped(size_t block_size, size_t partitions, double step_size,
                                  double relative_regularization, int ipc_step_scaling, double transient_freeze_ratio,
                                  double lambda, size_t order);

/* PEM canceller on the frequency-domain KALMAN core (fd_kalman.h, the v2
 * update): no step size, no IPC options — the per-bin state uncertainty
 * and near-end PSD replace the whole adaptation-control stack.
 * transient_floor_ratio 0 (recommended) optimizes added stable gain; a
 * ratio ~8 hardens a fixed-gain deployment against near-end bursts at a
 * measured ASG cost. warped selects the frequency-warped near-end model
 * (lambda/order as in mutap_afc_create_warped; ignored when warped = 0).
 * mutap_afc_set_step_size is a no-op on these handles; otherwise they are
 * interchangeable with mutap_afc_create's. */
MutapAfc* mutap_afc_create_kalman(size_t block_size, size_t partitions, double transient_floor_ratio, int warped,
                                  double lambda, size_t order);
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

/* The measured AEC chain (mutap::aec_chain<double>), configured by the
 * ITU compliance preset (mutap::aec_chain_preset): the raw FD-Kalman
 * canceller — deliberately NOT PEM; open-loop AEC has an exogenous far
 * end and the predictor refit measurably floors the misalignment — plus
 * the coherence-driven residual suppressor, comfort noise matched to the
 * near-end noise floor, and the initial receive guard. This is the
 * configuration the compliance battery certifies at 48 and 16 kHz
 * (docs/itu-compliance.md); the preset rescales its time constants for
 * any (block_size, sample_rate) geometry.
 *   sample_rate      Hz; scales the preset's time constants
 *   comfort_noise    nonzero: fill suppressed bins to the noise floor
 *   receive_guard    nonzero: switched < 14 dB send loss until initial
 *                    convergence certifies, then latched off permanently
 * The cleaned output e trails the canceller-only path by one extra block
 * (the suppressor's constrained causal gain filter). */
MutapAec* mutap_aec_create(size_t block_size, size_t partitions, double sample_rate, int comfort_noise,
                           int receive_guard);
void      mutap_aec_destroy(MutapAec* h);

/* One block: x = far-end reference (to the loudspeaker), y = microphone,
 * e = the cleaned send signal. */
void   mutap_aec_process(MutapAec* h, const double* x, const double* y, double* e);
size_t mutap_aec_block_size(const MutapAec* h);
/* Fraction of mic power the echo estimate explains, 0..1 (the suppressor's
 * convergence statistic; the receive guard certifies on it). */
double mutap_aec_echo_explained(const MutapAec* h);
/* Nonzero once the initial receive guard has certified convergence
 * (always nonzero when the guard is disabled). */
int  mutap_aec_converged(const MutapAec* h);
void mutap_aec_set_adaptation(MutapAec* h, int enabled);
void mutap_aec_reset(MutapAec* h);
/* Deep copy, as mutap_fdaf_clone. */
MutapAec* mutap_aec_clone(const MutapAec* h);

#ifdef __cplusplus
}
#endif
