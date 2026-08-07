# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Benchmark system wrapper around the webrtc_aec3_infer CLI (WebRTC AEC3,
BSD-3 — the echo canceller of current Chrome). Build recipe: see README.md;
the CMake target appears when pkg-config finds webrtc-audio-processing >= 1.
"""
from __future__ import annotations

import pathlib
import subprocess
import tempfile

import numpy as np


class WebRtcAec3:
    name = "webrtc-aec3"

    def __init__(self, binary: str | pathlib.Path):
        self._bin = pathlib.Path(binary)
        if not self._bin.exists():
            raise FileNotFoundError(f"{binary}: build with -DMUTAP_BUILD_ML_TOOLS=ON "
                                    "and webrtc-audio-processing >= 1 on PKG_CONFIG_PATH")

    def process(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        with tempfile.TemporaryDirectory() as td:
            td = pathlib.Path(td)
            np.asarray(x, dtype="<f8").tofile(td / "x.f64")
            np.asarray(y, dtype="<f8").tofile(td / "y.f64")
            subprocess.run([str(self._bin), str(td / "x.f64"), str(td / "y.f64"),
                            str(td / "e.f64")], check=True)
            return np.fromfile(td / "e.f64", dtype="<f8")
