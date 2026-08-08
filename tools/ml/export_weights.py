#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Export trained suppressor weights (.npz) to the flat float32 binary
consumed by tap::mu::load_nn_suppressor_weights. MUNN0002: magic, then
five uint32 geometry fields (sample_rate, hop, bands, dense, gru), then
the eight arrays in nn_suppressor_weights declaration order. Geometry is
read from the npz's 'geometry' entry when present (written by
train_suppressor.py), else defaults to the original 16 kHz / hop-64
model's.

    python3 tools/ml/export_weights.py suppressor.npz suppressor.munn
"""
from __future__ import annotations

import sys

import numpy as np

DEFAULT_GEOMETRY = (16000, 64, 22, 64, 96)  # rate, hop, bands, dense, gru


def order(geometry) -> list[tuple[str, tuple[int, ...]]]:
    _, _, bands, dense, gru = (int(v) for v in geometry)
    features = 2 * bands
    return [
        ("dense_in.weight", (dense, features)),
        ("dense_in.bias", (dense,)),
        ("gru.weight_ih_l0", (3 * gru, dense)),
        ("gru.weight_hh_l0", (3 * gru, gru)),
        ("gru.bias_ih_l0", (3 * gru,)),
        ("gru.bias_hh_l0", (3 * gru,)),
        ("dense_out.weight", (bands, gru)),
        ("dense_out.bias", (bands,)),
    ]


ORDER = order(DEFAULT_GEOMETRY)  # legacy alias for the fixed 16 kHz geometry


def export(npz_path: str, out_path: str) -> None:
    w = np.load(npz_path)
    geometry = tuple(int(v) for v in w["geometry"]) if "geometry" in w else DEFAULT_GEOMETRY
    with open(out_path, "wb") as f:
        f.write(b"MUNN0002")
        f.write(np.asarray(geometry, dtype="<u4").tobytes())
        for name, shape in order(geometry):
            a = np.asarray(w[name], dtype="<f4")
            assert a.shape == shape, f"{name}: {a.shape} != {shape}"
            f.write(a.tobytes(order="C"))


if __name__ == "__main__":
    export(sys.argv[1], sys.argv[2])
    print(f"wrote {sys.argv[2]}")
