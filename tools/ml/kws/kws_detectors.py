#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""kws_detectors — the detector contract the evaluation harness scores, and its two M5 detectors.

A detector maps a 16 kHz stream to one score per completed front-end hop: score[t] belongs to frame t,
complete when sample (t + 1) * hop - 1 arrives (log_mel.h's alignment), so a stream of n samples yields
n // hop scores — the frame count the shipping front end returns for n samples (measured through the
bridge on 9 September 2026 for n in {0, 159, 160, 161, 400, 16000, 48000, 62555, 1000000}: exactly
n // 160 every time). Scores are float64 in [0, 1].

- `BandEnergyBaseline` is the plan's trivial sanity detector: the mean, over the mel bands whose centre
  lies in [band_lo_hz, band_hi_hz], of the shipping front end's plain-log feature, clipped to [0, 1]. It
  runs through the C ABI bridge (`kws_features.FrontEnd`), so it scores what ships. It is the sanity
  curve, never the pass.
- `PlantedDetector` is the oracle's detector: zeros except the planted (hop, score) pairs per stream id,
  so the harness's recall and FA/h can be checked against hand-computed values.
"""
from __future__ import annotations

import dataclasses
import pathlib
import sys
from typing import Any, Protocol, Sequence

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_features  # noqa: E402

DEFAULT_BAND_LO_HZ = 300.0
DEFAULT_BAND_HI_HZ = 3000.0


class DetectorError(ValueError):
    """A detector configuration or call the harness refuses."""


class Detector(Protocol):
    """One score per completed hop, float64 in [0, 1], len = n_samples // hop."""

    name: str
    params: dict[str, Any]

    def score(self, x: np.ndarray) -> np.ndarray: ...


def frames_for(n_samples: int, hop: int) -> int:
    """The front end's frame count for n_samples samples after a reset: n // hop (the module docstring)."""
    if hop < 1:
        raise DetectorError(f"hop must be positive, got {hop}")
    return int(n_samples) // int(hop)


def band_centres_hz(g: kws_features.Geometry) -> np.ndarray:
    """The centre frequency of every mel band: the reference's mel edges (HTK scale, bands + 2 points
    from fmin to fmax), the inner ones being the triangle peaks log_mel.h uses."""
    ref = kws_features.reference_module()
    edges = ref.mel_to_hz(np.linspace(ref.hz_to_mel(g.fmin_hz), ref.hz_to_mel(g.fmax_hz), g.bands + 2))
    return np.asarray(edges[1:-1], dtype=np.float64)


class BandEnergyBaseline:
    """The trivial band-energy detector through the shipping front end's log path."""

    name = "band-energy"

    def __init__(self, geometry: kws_features.Geometry, band_lo_hz: float = DEFAULT_BAND_LO_HZ,
                 band_hi_hz: float = DEFAULT_BAND_HI_HZ):
        if not (0.0 <= band_lo_hz < band_hi_hz):
            raise DetectorError(f"band [{band_lo_hz}, {band_hi_hz}] Hz must satisfy 0 <= lo < hi")
        # the plain-log feature: the same geometry with PCEN off (the log affine keeps ~[0, 1] for band
        # energies in [1e-5, 1] under the reference constants log_floor 1e-10, shift 5, scale 5)
        log_geometry = dataclasses.replace(geometry, pcen=dataclasses.replace(geometry.pcen, enabled=False))
        self._fe = kws_features.FrontEnd(log_geometry)
        centres = band_centres_hz(log_geometry)
        bands = [b for b in range(log_geometry.bands) if band_lo_hz <= centres[b] <= band_hi_hz]
        if not bands:
            raise DetectorError(f"no mel band centre lies in [{band_lo_hz}, {band_hi_hz}] Hz at geometry "
                                f"{log_geometry.bands} bands {log_geometry.fmin_hz}-{log_geometry.fmax_hz} "
                                f"Hz (centres {np.round(centres, 1).tolist()})")
        self.geometry = log_geometry
        self.bands = bands
        self.params: dict[str, Any] = {
            "geometry": log_geometry.to_dict(), "stored_path": "log",
            "band_lo_hz": float(band_lo_hz), "band_hi_hz": float(band_hi_hz),
            "bands": list(bands), "band_centres_hz": [round(float(centres[b]), 3) for b in bands],
        }

    @property
    def hop(self) -> int:
        return self.geometry.hop

    def features(self, x: np.ndarray) -> np.ndarray:
        """The front end's (frames, bands) log features for a whole stream after one reset."""
        x = np.asarray(x, dtype=np.float64)
        if x.ndim != 1:
            raise DetectorError(f"a stream is one-dimensional, got shape {x.shape}")
        return self._fe.extract(x)

    def score(self, x: np.ndarray) -> np.ndarray:
        feats = self.features(x)
        if feats.shape[0] == 0:
            return np.zeros(0, dtype=np.float64)
        return np.clip(feats[:, self.bands].mean(axis=1), 0.0, 1.0).astype(np.float64)


class PlantedDetector:
    """Zeros except the planted (hop, score) pairs of each stream id; a stream absent from the plan scores
    all zeros. `score_stream` refuses a planted hop at or beyond the stream's frame count."""

    name = "planted"

    def __init__(self, planted: dict[str, Sequence[tuple[int, float]]], hop: int):
        if hop < 1:
            raise DetectorError(f"hop must be positive, got {hop}")
        self.hop = int(hop)
        self.planted: dict[str, list[tuple[int, float]]] = {}
        for stream_id, pairs in planted.items():
            seen: set[int] = set()
            out: list[tuple[int, float]] = []
            for pair in pairs:
                if len(pair) != 2:
                    raise DetectorError(f"stream {stream_id!r}: expected (hop, score) pairs, got {pair!r}")
                h, s = pair
                if isinstance(h, bool) or not isinstance(h, (int, np.integer)) or h < 0:
                    raise DetectorError(f"stream {stream_id!r}: planted hop {h!r} must be a non-negative "
                                        "integer")
                if not (0.0 <= float(s) <= 1.0):
                    raise DetectorError(f"stream {stream_id!r}: planted score {s!r} at hop {h} is outside "
                                        "[0, 1]")
                if int(h) in seen:
                    raise DetectorError(f"stream {stream_id!r}: hop {h} is planted twice")
                seen.add(int(h))
                out.append((int(h), float(s)))
            self.planted[stream_id] = sorted(out)
        planted_doc = {k: [[h, s] for h, s in v] for k, v in self.planted.items()}
        self.params: dict[str, Any] = {"hop": self.hop, "planted": planted_doc}

    def score_stream(self, stream_id: str, n_samples: int) -> np.ndarray:
        n = frames_for(n_samples, self.hop)
        out = np.zeros(n, dtype=np.float64)
        for h, s in self.planted.get(stream_id, []):
            if h >= n:
                raise DetectorError(f"stream {stream_id!r}: planted hop {h} is beyond the stream's {n} hops "
                                    f"({n_samples} samples at hop {self.hop})")
            out[h] = s
        return out

    def score(self, x: np.ndarray) -> np.ndarray:
        raise DetectorError("PlantedDetector scores by stream id: call score_stream(stream_id, n_samples)")


# ---------------------------------------------------------------- self-check: alignment on a planted burst


def _self_check() -> None:
    """A 1 kHz burst ending at sample e in digital silence, through the bridge at the reference geometry.

    Measured 9 September 2026 on the M0 Mac (bursts ending at 24000, 24080, 24159, 24001 and 8400), with
    h = e // 160: argmax(score) <= h + 1; the score at h is at least 0.3218 (a burst ending early in a hop
    fills only part of frame h) and at h + 1 at least 0.9613 (the offset edge splatters across every
    band); at h + 2 it is 0.0 except for the burst ending late in its hop (24159), which reads 0.979018;
    from h + 3 on it is 0 again, and it is 0 before hop start // 160, the first frame that reaches the
    burst's onset. The plain 1 kHz interior reads ~0.33 on the 22-band mean: a tone excites two bands.
    """
    g = kws_features.Geometry()
    det = BandEnergyBaseline(g)
    assert det.bands == list(range(5, 27)), det.bands   # centres 329.7 .. 2901.9 Hz at the reference geometry
    hop = g.hop
    for start, end in ((16000, 24000), (16000, 24159), (8000, 8400)):
        x = np.zeros(48000)
        t = np.arange(start, end)
        x[start:end] = 0.5 * np.sin(2.0 * np.pi * 1000.0 * t / g.sample_rate)
        s = det.score(x)
        assert s.shape == (frames_for(x.size, hop),), s.shape
        h = end // hop
        assert int(np.argmax(s)) <= h + 1
        assert s[h] >= 0.3 and s[h + 1] >= 0.3, (end, h, s[h], s[h + 1])   # measured minima 0.3218, 0.9613
        assert s[h + 2] <= 0.99, (end, h, s[h + 2])   # measured 0.979018 for 24159, 0.0 for the others
        assert np.all(s[h + 3:] == 0.0), (end, h)
        assert np.all(s[:start // hop] == 0.0) and s[start // hop] > 0.0, (start, h)
    pd = PlantedDetector({"a": [(3, 0.9), (10, 0.5)]}, hop)
    got = pd.score_stream("a", 20 * hop)
    assert got.shape == (20,) and got[3] == 0.9 and got[10] == 0.5 and got.sum() == 1.4
    assert pd.score_stream("b", 5 * hop).sum() == 0.0
    try:
        pd.score_stream("a", 10 * hop)
    except DetectorError as e:
        assert "beyond" in str(e)
    else:
        raise AssertionError("a planted hop beyond the stream was not refused")
    print("kws_detectors: self-check ok")


if __name__ == "__main__":
    _self_check()
