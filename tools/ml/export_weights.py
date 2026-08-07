#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Export trained suppressor weights (.npz) to the flat float32 binary
consumed by tap::mu::load_nn_suppressor_weights (magic MUNN0001, arrays
in nn_suppressor_weights declaration order).

    python3 tools/ml/export_weights.py suppressor.npz suppressor.munn
"""
from __future__ import annotations

import sys

import numpy as np

ORDER = [
    ("dense_in.weight", (64, 44)),
    ("dense_in.bias", (64,)),
    ("gru.weight_ih_l0", (288, 64)),
    ("gru.weight_hh_l0", (288, 96)),
    ("gru.bias_ih_l0", (288,)),
    ("gru.bias_hh_l0", (288,)),
    ("dense_out.weight", (22, 96)),
    ("dense_out.bias", (22,)),
]


def export(npz_path: str, out_path: str) -> None:
    w = np.load(npz_path)
    with open(out_path, "wb") as f:
        f.write(b"MUNN0001")
        for name, shape in ORDER:
            a = np.asarray(w[name], dtype="<f4")
            assert a.shape == shape, f"{name}: {a.shape} != {shape}"
            f.write(a.tobytes(order="C"))


if __name__ == "__main__":
    export(sys.argv[1], sys.argv[2])
    print(f"wrote {sys.argv[2]}")
