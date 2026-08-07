# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Shared feature/target definitions for the learned residual suppressor.

This module is the single source of truth for the analysis geometry: the
dataset generator (make_dataset.py), the trainer (train_suppressor.py) and
the C++ inference header must all agree bit-for-bit on what a "band
energy" and a "feature vector" are. Keep it dependency-light (numpy only)
and keep every constant explicit — the C++ side hardcodes the same
numbers and the parity test compares against this file's output.

Geometry (matching the canceller: block 64 @ 16 kHz):
- STFT: frame 128, hop 64, sqrt-Hann analysis and synthesis (perfect
  reconstruction at 50% overlap), 65 rfft bins.
- 22 triangular bands, ERB-spaced band edges over 0..8 kHz (RNNoise-style
  band-energy compression: enough resolution for gain shaping, few enough
  parameters for a tiny GRU).

Features per frame (44): log10 band energies of the canceller output E
and of the echo estimate Yhat, affinely normalized. Target per frame
(22): the oracle band gain sqrt(V/E energy) clipped to [0, 1] — V is the
clean near end, available because the simulator knows it; a per-band loss
weight de-emphasizes frames where E carries almost no energy.
"""
from __future__ import annotations

import numpy as np

FRAME = 128
HOP = 64
BINS = FRAME // 2 + 1  # 65
NUM_BANDS = 22
SAMPLE_RATE = 16000.0

LOG_FLOOR = 1e-10
FEAT_SHIFT = 5.0  # feature = (log10(energy + floor) + shift) / scale
FEAT_SCALE = 5.0
WEIGHT_FLOOR_DB = -50.0  # frames/bands quieter than this (rel. unit RMS) get ~0 loss weight


def _erb(f_hz: np.ndarray) -> np.ndarray:
    return 24.7 * (4.37 * f_hz / 1000.0 + 1.0)


def band_edges_hz(num_bands: int = NUM_BANDS, fmax: float = SAMPLE_RATE / 2) -> np.ndarray:
    """num_bands+2 edge frequencies, uniformly spaced on the ERB-rate scale."""
    erb_rate = lambda f: 21.4 * np.log10(1.0 + 0.00437 * f)  # noqa: E731
    inv = lambda r: (10.0 ** (r / 21.4) - 1.0) / 0.00437  # noqa: E731
    r = np.linspace(0.0, erb_rate(fmax), num_bands + 2)
    return inv(r)


def band_matrix(num_bands: int = NUM_BANDS, bins: int = BINS) -> np.ndarray:
    """(num_bands, bins) triangular weights, rows summing over their support."""
    freqs = np.arange(bins) * SAMPLE_RATE / FRAME
    edges = band_edges_hz(num_bands)
    w = np.zeros((num_bands, bins))
    for b in range(num_bands):
        lo, mid, hi = edges[b], edges[b + 1], edges[b + 2]
        up = (freqs > lo) & (freqs <= mid)
        down = (freqs > mid) & (freqs < hi)
        w[b, up] = (freqs[up] - lo) / max(mid - lo, 1e-9)
        w[b, down] = (hi - freqs[down]) / max(hi - mid, 1e-9)
    w[0, 0] = 1.0  # DC belongs to the first band
    return w


def stft(x: np.ndarray) -> np.ndarray:
    """(frames, BINS) complex spectra; sqrt-Hann window, hop HOP.

    STREAMING-consistent framing: a HOP of zero prehistory is prepended,
    so frame t is [block t-1, block t] — exactly the frame the C++
    suppressor assembles on its t-th process_block call (block -1 = the
    zeros its buffers start from). Parity of the GRU state trajectory
    depends on this warm-up frame existing in both implementations."""
    win = np.sqrt(np.hanning(FRAME + 1)[:FRAME])
    xp = np.concatenate([np.zeros(HOP), x])
    n_frames = max(0, (len(xp) - FRAME) // HOP + 1)
    out = np.empty((n_frames, BINS), dtype=np.complex128)
    for t in range(n_frames):
        out[t] = np.fft.rfft(xp[t * HOP : t * HOP + FRAME] * win)
    return out


def istft(spec: np.ndarray, n: int) -> np.ndarray:
    """Inverse of stft (drops the zero-prehistory pad): time-aligned with
    the analyzed signal. The C++ stream instead EMITS one block late (its
    t-th call returns the sample span ending at block t-1); the offline
    meters delay-compensate, so both conventions measure identically."""
    win = np.sqrt(np.hanning(FRAME + 1)[:FRAME])
    out = np.zeros(n + HOP + FRAME)
    for t in range(spec.shape[0]):
        out[t * HOP : t * HOP + FRAME] += np.fft.irfft(spec[t]) * win
    return out[HOP : HOP + n]


def band_energies(spec: np.ndarray, bmat: np.ndarray | None = None) -> np.ndarray:
    if bmat is None:
        bmat = band_matrix()
    return (np.abs(spec) ** 2) @ bmat.T


def features_from(e: np.ndarray, yhat: np.ndarray, bmat: np.ndarray | None = None) -> np.ndarray:
    """(frames, 44) normalized log band energies of E and Yhat."""
    if bmat is None:
        bmat = band_matrix()
    be = band_energies(stft(e), bmat)
    by = band_energies(stft(yhat), bmat)
    n = min(len(be), len(by))
    f = np.concatenate([np.log10(be[:n] + LOG_FLOOR), np.log10(by[:n] + LOG_FLOOR)], axis=1)
    return ((f + FEAT_SHIFT) / FEAT_SCALE).astype(np.float32)


def targets_from(e: np.ndarray, v: np.ndarray, bmat: np.ndarray | None = None):
    """Oracle gains (frames, 22) in [0,1] and loss weights (frames, 22).

    Weight ~1 where the canceller output band is audible, fading to 0 in
    near-silence (a gain applied to nothing should not train anything)."""
    if bmat is None:
        bmat = band_matrix()
    be = band_energies(stft(e), bmat)
    bv = band_energies(stft(v), bmat)
    n = min(len(be), len(bv))
    g = np.sqrt(bv[:n] / (be[:n] + LOG_FLOOR))
    g = np.clip(g, 0.0, 1.0).astype(np.float32)
    level_db = 10.0 * np.log10(be[:n] + LOG_FLOOR)
    w = np.clip((level_db - WEIGHT_FLOOR_DB) / 20.0, 0.0, 1.0).astype(np.float32)
    return g, w


def apply_gains(e: np.ndarray, gains: np.ndarray, bmat: np.ndarray | None = None) -> np.ndarray:
    """Synthesis reference: interpolate band gains to bins, apply to E's
    STFT, resynthesize. The C++ inference must match this (parity test)."""
    if bmat is None:
        bmat = band_matrix()
    spec = stft(e)
    n = min(len(spec), len(gains))
    # bin gain = band-weight-normalized interpolation of band gains
    norm = bmat.sum(axis=0)
    norm[norm == 0.0] = 1.0
    bin_gain = (gains[:n] @ bmat) / norm
    return istft(spec[:n] * bin_gain, n * HOP + (FRAME - HOP))
