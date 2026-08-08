#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Parity: tap::mu::nn_suppressor (C++, double) vs the numpy reference.

Random weights, random signals; the C++ output must match the numpy
pipeline (features.py analysis + nn.py network + features.py synthesis)
to float64 depth over everything after the first frame. Run after any
change to features.py, nn.py or include/mutap/nn_suppressor.h:

    python3 tools/ml/test_parity.py [--build-dir build-ml]
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import export_weights  # noqa: E402
import features  # noqa: E402
import nn  # noqa: E402


def random_weights(rng: np.random.Generator) -> dict[str, np.ndarray]:
    return {name: rng.standard_normal(shape).astype(np.float32) * 0.3
            for name, shape in export_weights.ORDER}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=str(pathlib.Path(__file__).resolve().parents[2] / "build-ml"))
    args = ap.parse_args()
    infer = pathlib.Path(args.build_dir) / "tools/ml/nn_infer"
    if not infer.exists():
        raise SystemExit(f"{infer} missing; configure with -DMUTAP_BUILD_ML_TOOLS=ON")

    rng = np.random.default_rng(3)
    n = 64 * 200
    e = rng.standard_normal(n)
    yhat = rng.standard_normal(n) * 0.5

    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        w = random_weights(rng)
        np.savez(td / "w.npz", **w)
        export_weights.export(str(td / "w.npz"), str(td / "w.munn"))
        e.astype("<f8").tofile(td / "e.f64")
        yhat.astype("<f8").tofile(td / "yhat.f64")
        subprocess.run([str(infer), str(td / "w.munn"), str(td / "e.f64"),
                        str(td / "yhat.f64"), str(td / "out.f64")], check=True)
        cpp = np.fromfile(td / "out.f64", dtype="<f8")

    net = nn.SuppressorNet.__new__(nn.SuppressorNet)
    net.w = {k: v.astype(np.float64) for k, v in w.items()}
    net.gru = nn.GRU_DIM
    net.h = np.zeros(nn.GRU_DIM)
    bmat = features.band_matrix()
    feats = features.features_from(e, yhat, bmat).astype(np.float64)
    gains = net.process(feats)
    ref = features.apply_gains(e, gains, bmat)

    # Both sides now assemble identical frame sequences (features.stft
    # prepends the streaming warm-up frame); the C++ output stream simply
    # trails the time-aligned numpy reference by one block.
    m = min(len(cpp) - 64, len(ref))
    err = np.max(np.abs(cpp[64 : 64 + m] - ref[:m]))
    scale = np.max(np.abs(ref[:m]))
    rel = err / scale
    print(f"max abs err {err:.3e} (rel {rel:.3e}) over {m} samples")
    if rel > 1e-6:
        print("PARITY FAIL")
        return 1
    print("parity OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
