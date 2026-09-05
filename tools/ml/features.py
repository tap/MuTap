# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Shared feature/target definitions for the learned residual suppressor.

This module is the single source of truth for the analysis geometry: the
dataset generator (make_dataset.py), the trainer (train_suppressor.py) and
the C++ inference header must all agree bit-for-bit on what a "band
energy" and a "feature vector" are. Keep it dependency-light (numpy only)
and keep every constant explicit — the C++ side (mutap/nn_suppressor.h)
implements the same definitions parameterized by the same geometry, and
the parity test compares against this file's output.

Geometry is a value, not a constant: a model is trained at one
(sample_rate, hop, bands) triple, carried with its weights (MUNN0002).
The reference instance GEOM16 (16 kHz, hop 64, 22 bands) matches the
original benchmark model; GEOM48 (48 kHz, hop 256, 26 bands) matches
mutap.aec~'s default block size. Analysis:
- STFT: frame 2*hop, hop hop, sqrt-Hann analysis and synthesis (perfect
  reconstruction at 50% overlap), hop+1 rfft bins.
- ERB-spaced triangular bands over 0..sample_rate/2 (RNNoise-style
  band-energy compression).

Features per frame (2*bands): log10 band energies of the canceller
output E and of the echo estimate Yhat, affinely normalized. Target per
frame (bands): the oracle band gain sqrt(V/E energy) clipped to [0, 1] —
V is the clean near end, available because the simulator knows it; a
per-band loss weight de-emphasizes frames where E carries almost no
energy.
"""
from __future__ import annotations

import dataclasses

import numpy as np

LOG_FLOOR = 1e-10
FEAT_SHIFT = 5.0  # feature = (log10(energy + floor) + shift) / scale
FEAT_SCALE = 5.0
WEIGHT_FLOOR_DB = -50.0  # frames/bands quieter than this (rel. unit RMS) get ~0 loss weight


@dataclasses.dataclass(frozen=True)
class Geometry:
    sample_rate: float = 16000.0
    hop: int = 64
    bands: int = 22
    dense: int = 64
    gru: int = 96

    @property
    def frame(self) -> int:
        return 2 * self.hop

    @property
    def bins(self) -> int:
        return self.hop + 1

    @property
    def features(self) -> int:
        return 2 * self.bands

    def as_array(self) -> np.ndarray:
        return np.asarray([int(self.sample_rate), self.hop, self.bands, self.dense, self.gru],
                          dtype="<u4")

    @classmethod
    def from_array(cls, a) -> "Geometry":
        rate, hop, bands, dense, gru = (int(v) for v in a)
        return cls(float(rate), hop, bands, dense, gru)


GEOM16 = Geometry()
GEOM48 = Geometry(sample_rate=48000.0, hop=256, bands=26)

# Legacy module-level constants (the 16 kHz reference geometry).
FRAME = GEOM16.frame
HOP = GEOM16.hop
BINS = GEOM16.bins
NUM_BANDS = GEOM16.bands
SAMPLE_RATE = GEOM16.sample_rate


def band_edges_hz(num_bands: int, fmax: float) -> np.ndarray:
    """num_bands+2 edge frequencies, uniformly spaced on the ERB-rate scale."""
    erb_rate = lambda f: 21.4 * np.log10(1.0 + 0.00437 * f)  # noqa: E731
    inv = lambda r: (10.0 ** (r / 21.4) - 1.0) / 0.00437  # noqa: E731
    r = np.linspace(0.0, erb_rate(fmax), num_bands + 2)
    edges = inv(r)
    # The top edge is fmax EXACTLY, never the ERB round trip of it: that
    # round trip lands a few 1e-12 above fmax at 16 kHz and below it at
    # 48 kHz (and differs between numpy and libm), which used to decide
    # whether the Nyquist bin was covered at all. Coverage is a contract,
    # not a rounding accident — see band_matrix().
    edges[-1] = fmax
    return edges


def band_matrix(geom: Geometry = GEOM16) -> np.ndarray:
    """(bands, bins) triangular weights, rows summing over their support."""
    freqs = np.arange(geom.bins) * geom.sample_rate / geom.frame
    edges = band_edges_hz(geom.bands, geom.sample_rate / 2)
    w = np.zeros((geom.bands, geom.bins))
    for b in range(geom.bands):
        lo, mid, hi = edges[b], edges[b + 1], edges[b + 2]
        up = (freqs > lo) & (freqs <= mid)
        down = (freqs > mid) & (freqs < hi)
        w[b, up] = (freqs[up] - lo) / max(mid - lo, 1e-9)
        w[b, down] = (hi - freqs[down]) / max(hi - mid, 1e-9)
    w[0, 0] = 1.0  # DC belongs to the first band
    w[-1, -1] = 1.0  # and the Nyquist bin to the last (a triangle's weight is 0 at its edge)
    return w


def _window(geom: Geometry) -> np.ndarray:
    return np.sqrt(np.hanning(geom.frame + 1)[: geom.frame])


def stft(x: np.ndarray, geom: Geometry = GEOM16) -> np.ndarray:
    """(frames, bins) complex spectra; sqrt-Hann window, hop geom.hop.

    STREAMING-consistent framing: a hop of zero prehistory is prepended,
    so frame t is [block t-1, block t] — exactly the frame the C++
    suppressor assembles on its t-th process_block call (block -1 = the
    zeros its buffers start from). Parity of the GRU state trajectory
    depends on this warm-up frame existing in both implementations."""
    win = _window(geom)
    xp = np.concatenate([np.zeros(geom.hop), x])
    n_frames = max(0, (len(xp) - geom.frame) // geom.hop + 1)
    out = np.empty((n_frames, geom.bins), dtype=np.complex128)
    for t in range(n_frames):
        out[t] = np.fft.rfft(xp[t * geom.hop : t * geom.hop + geom.frame] * win)
    return out


def istft(spec: np.ndarray, n: int, geom: Geometry = GEOM16) -> np.ndarray:
    """Inverse of stft (drops the zero-prehistory pad): time-aligned with
    the analyzed signal. The C++ stream instead EMITS one block late (its
    t-th call returns the sample span ending at block t-1); the offline
    meters delay-compensate, so both conventions measure identically."""
    win = _window(geom)
    out = np.zeros(n + geom.hop + geom.frame)
    for t in range(spec.shape[0]):
        out[t * geom.hop : t * geom.hop + geom.frame] += np.fft.irfft(spec[t]) * win
    return out[geom.hop : geom.hop + n]


def band_energies(spec: np.ndarray, bmat: np.ndarray) -> np.ndarray:
    return (np.abs(spec) ** 2) @ bmat.T


def features_from(e: np.ndarray, yhat: np.ndarray, bmat: np.ndarray | None = None,
                  geom: Geometry = GEOM16) -> np.ndarray:
    """(frames, 2*bands) normalized log band energies of E and Yhat."""
    if bmat is None:
        bmat = band_matrix(geom)
    be = band_energies(stft(e, geom), bmat)
    by = band_energies(stft(yhat, geom), bmat)
    n = min(len(be), len(by))
    f = np.concatenate([np.log10(be[:n] + LOG_FLOOR), np.log10(by[:n] + LOG_FLOOR)], axis=1)
    return ((f + FEAT_SHIFT) / FEAT_SCALE).astype(np.float32)


def targets_from(e: np.ndarray, v: np.ndarray, bmat: np.ndarray | None = None,
                 geom: Geometry = GEOM16):
    """Oracle gains (frames, bands) in [0,1] and loss weights (frames, bands).

    Weight ~1 where the canceller output band is audible, fading to 0 in
    near-silence (a gain applied to nothing should not train anything)."""
    if bmat is None:
        bmat = band_matrix(geom)
    be = band_energies(stft(e, geom), bmat)
    bv = band_energies(stft(v, geom), bmat)
    n = min(len(be), len(bv))
    g = np.sqrt(bv[:n] / (be[:n] + LOG_FLOOR))
    g = np.clip(g, 0.0, 1.0).astype(np.float32)
    level_db = 10.0 * np.log10(be[:n] + LOG_FLOOR)
    w = np.clip((level_db - WEIGHT_FLOOR_DB) / 20.0, 0.0, 1.0).astype(np.float32)
    return g, w


def apply_gains(e: np.ndarray, gains: np.ndarray, bmat: np.ndarray | None = None,
                geom: Geometry = GEOM16) -> np.ndarray:
    """Synthesis reference: interpolate band gains to bins, apply to E's
    STFT, resynthesize. The C++ inference must match this (parity test)."""
    if bmat is None:
        bmat = band_matrix(geom)
    spec = stft(e, geom)
    n = min(len(spec), len(gains))
    # bin gain = band-weight-normalized interpolation of band gains
    norm = bmat.sum(axis=0)
    norm[norm == 0.0] = 1.0
    bin_gain = (gains[:n] @ bmat) / norm
    return istft(spec[:n] * bin_gain, n * geom.hop + (geom.frame - geom.hop), geom)
