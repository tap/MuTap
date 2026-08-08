# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""ctypes bindings for the MuTap systems the ML benchmark compares.

Loads the C ABI shared library (tools/capi, built with -DMUTAP_BUILD_CAPI=ON)
and exposes the two shipped configurations as block processors:

- ``KalmanCanceller`` — the linear PEM-Kalman canceller alone
  (``mutap_afc_create_kalman``, the engine test_aec.cpp measures), run
  open-loop with the far end as reference.
- ``AecChain`` — the full certified chain (``mutap_aec_create``): raw
  FD-Kalman canceller + coherence residual suppressor (+ optional comfort
  noise / receive guard), via the ITU preset.
"""
from __future__ import annotations

import ctypes
import pathlib

import numpy as np

_F64P = ctypes.POINTER(ctypes.c_double)


def load_library(build_dir: pathlib.Path) -> ctypes.CDLL:
    for rel in ("tools/capi/libmutap_capi.so", "tools/capi/libmutap_capi.dylib"):
        p = build_dir / rel
        if p.exists():
            return ctypes.CDLL(str(p))
    raise FileNotFoundError(f"libmutap_capi not found under {build_dir}; configure with -DMUTAP_BUILD_CAPI=ON")


def _block_iter(n_total: int, block: int):
    for start in range(0, n_total - n_total % block, block):
        yield start


class KalmanCanceller:
    """Linear PEM-Kalman canceller (speech predictor), open loop."""

    name = "mutap-kalman-linear"

    def __init__(self, lib: ctypes.CDLL, block_size: int, partitions: int):
        self._lib = lib
        lib.mutap_afc_create_kalman.restype = ctypes.c_void_p
        lib.mutap_afc_create_kalman.argtypes = [
            ctypes.c_size_t, ctypes.c_size_t, ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_size_t,
        ]
        lib.mutap_afc_process.argtypes = [ctypes.c_void_p, _F64P, _F64P, _F64P]
        lib.mutap_afc_destroy.argtypes = [ctypes.c_void_p]
        self._h = lib.mutap_afc_create_kalman(block_size, partitions, 0.0, 0, 0.0, 0)
        if not self._h:
            raise RuntimeError("mutap_afc_create_kalman failed")
        self._block = block_size

    def process(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        x = np.ascontiguousarray(x, dtype=np.float64)
        y = np.ascontiguousarray(y, dtype=np.float64)
        e = np.zeros_like(y)
        b = self._block
        for i in _block_iter(len(y), b):
            self._lib.mutap_afc_process(
                self._h,
                x[i:].ctypes.data_as(_F64P),
                y[i:].ctypes.data_as(_F64P),
                e[i : i + b].ctypes.data_as(_F64P),
            )
        return e

    def __del__(self):
        if getattr(self, "_h", None):
            self._lib.mutap_afc_destroy(self._h)


class AecChain:
    """Full AEC chain: FD-Kalman canceller + residual suppressor."""

    def __init__(self, lib: ctypes.CDLL, block_size: int, partitions: int, sample_rate: float,
                 comfort_noise: bool = False, receive_guard: bool = False,
                 nn_weights: str | None = None):
        self._lib = lib
        lib.mutap_aec_create.restype = ctypes.c_void_p
        lib.mutap_aec_create.argtypes = [
            ctypes.c_size_t, ctypes.c_size_t, ctypes.c_double, ctypes.c_int, ctypes.c_int,
        ]
        lib.mutap_aec_create_nn.restype = ctypes.c_void_p
        lib.mutap_aec_create_nn.argtypes = [
            ctypes.c_size_t, ctypes.c_size_t, ctypes.c_double, ctypes.c_int, ctypes.c_int, ctypes.c_char_p,
        ]
        lib.mutap_aec_process.argtypes = [ctypes.c_void_p, _F64P, _F64P, _F64P]
        lib.mutap_aec_destroy.argtypes = [ctypes.c_void_p]
        if nn_weights is not None:
            self._h = lib.mutap_aec_create_nn(block_size, partitions, sample_rate, int(comfort_noise),
                                              int(receive_guard), str(nn_weights).encode())
        else:
            self._h = lib.mutap_aec_create(block_size, partitions, sample_rate, int(comfort_noise),
                                           int(receive_guard))
        if not self._h:
            raise RuntimeError("mutap_aec_create failed (nn: check weights path / hop == block_size)")
        self._block = block_size
        self.name = ("mutap-chain" + ("-nn" if nn_weights else "")
                     + ("+cn" if comfort_noise else "") + ("+guard" if receive_guard else ""))

    def process(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        x = np.ascontiguousarray(x, dtype=np.float64)
        y = np.ascontiguousarray(y, dtype=np.float64)
        e = np.zeros_like(y)
        b = self._block
        for i in _block_iter(len(y), b):
            self._lib.mutap_aec_process(
                self._h,
                x[i:].ctypes.data_as(_F64P),
                y[i:].ctypes.data_as(_F64P),
                e[i : i + b].ctypes.data_as(_F64P),
            )
        return e

    def __del__(self):
        if getattr(self, "_h", None):
            self._lib.mutap_aec_destroy(self._h)
