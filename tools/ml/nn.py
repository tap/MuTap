# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The learned residual suppressor's network, and its numpy reference.

Architecture (RNNoise-sized, chosen for the embedded targets — ~51k
parameters, one evaluation per 4 ms frame ≈ 13 MMAC/s at 16 kHz):

    features(44) -> dense(64, tanh) -> GRU(96) -> dense(22, sigmoid)

The numpy forward pass here is the REFERENCE inference: the trainer
(train_suppressor.py) exports weights as an .npz consumed by this module,
the benchmark runs it, and the C++ header implementation must match it to
float precision (the parity test drives both on the same features).

GRU convention: PyTorch's (reset/update/new gates, bias split b_ih/b_hh,
sigmoid gates, tanh candidate) — the C++ side mirrors this exactly.
"""
from __future__ import annotations

import numpy as np

INPUT_DIM = 44
DENSE_DIM = 64
GRU_DIM = 96
OUTPUT_DIM = 22


def _sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-x))


class SuppressorNet:
    """Stateful numpy inference over per-frame feature vectors."""

    def __init__(self, weights_file: str):
        w = np.load(weights_file)
        self.w = {k: w[k].astype(np.float64) for k in w.files}
        self.h = np.zeros(GRU_DIM)

    def reset(self) -> None:
        self.h = np.zeros(GRU_DIM)

    def step(self, feat: np.ndarray) -> np.ndarray:
        w = self.w
        d = np.tanh(w["dense_in.weight"] @ feat + w["dense_in.bias"])
        # PyTorch GRU: gates ordered r, z, n in the stacked matrices.
        gi = w["gru.weight_ih_l0"] @ d + w["gru.bias_ih_l0"]
        gh = w["gru.weight_hh_l0"] @ self.h + w["gru.bias_hh_l0"]
        r = _sigmoid(gi[:GRU_DIM] + gh[:GRU_DIM])
        z = _sigmoid(gi[GRU_DIM : 2 * GRU_DIM] + gh[GRU_DIM : 2 * GRU_DIM])
        n = np.tanh(gi[2 * GRU_DIM :] + r * gh[2 * GRU_DIM :])
        self.h = (1.0 - z) * n + z * self.h
        return _sigmoid(w["dense_out.weight"] @ self.h + w["dense_out.bias"])

    def process(self, feats: np.ndarray) -> np.ndarray:
        self.reset()
        return np.stack([self.step(f) for f in feats])


class KalmanNnSystem:
    """Benchmark system: MuTap linear Kalman canceller + learned band-gain
    suppressor (the hybrid this directory exists to evaluate)."""

    name = "mutap-kalman+nn"

    def __init__(self, lib, block_size: int, partitions: int, weights_file: str):
        import features as _features
        import mutap_ffi as _ffi

        self._features = _features
        self._canceller = _ffi.KalmanCanceller(lib, block_size, partitions)
        self._net = SuppressorNet(weights_file)
        self._bmat = _features.band_matrix()

    def process(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        e = self._canceller.process(x, y)
        yhat = y - e
        feats = self._features.features_from(e, yhat, self._bmat)
        gains = self._net.process(feats)
        out = self._features.apply_gains(e, gains, self._bmat)
        return np.concatenate([out, e[len(out):]]) if len(out) < len(e) else out[: len(e)]
