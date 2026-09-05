#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Parity: tap::mu::nn_suppressor (C++) vs the numpy reference.

The C++ output must match the numpy pipeline (features.py analysis +
nn.py network + features.py synthesis) — to float64 depth in the double
profile, and to a measured, pinned depth in the float32 embedded profile.
Weights are either random at a chosen geometry or a trained model
(tools/ml/pretrained/*.munn, whose geometry rides along). CI runs all
four combinations; run after any change to features.py, nn.py or
include/mutap/nn_suppressor.h:

    python3 tools/ml/test_parity.py [--build-dir build-ml] [--profile double|float]
                                    [--weights model.munn | --geometry 16k|48k]
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


def random_weights(rng: np.random.Generator, geometry) -> dict[str, np.ndarray]:
    w = {name: rng.standard_normal(shape).astype(np.float32) * 0.3
         for name, shape in export_weights.order(geometry)}
    w["geometry"] = np.asarray(geometry, dtype="<u4")
    return w


# Pinned depths (relative to the output peak), measured 2026-09 after the
# Nyquist-bin contract fix (before it, the 48 kHz cases disagreed by 3e-2):
# double 1.6e-8 / 2.0e-8 / 2.9e-8 (random 16k / random 48k / pretrained v2
# 48k) — float64 rounding through the STFT and the GRU; float 2.5e-7 /
# 2.0e-7 / 2.9e-7 — float32 rounding, with the GRU recurrence the place it
# accumulates. Both pinned at 1e-6, over 3x the worst measured case.
TOLERANCE = {"double": 1e-6, "float": 1e-6}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=str(pathlib.Path(__file__).resolve().parents[2] / "build-ml"))
    ap.add_argument("--profile", choices=["double", "float"], default="double")
    ap.add_argument("--weights", help="trained .munn to drive both sides with (else random)")
    ap.add_argument("--geometry", choices=["16k", "48k"], default="16k",
                    help="geometry for random weights (ignored with --weights)")
    args = ap.parse_args()
    infer = pathlib.Path(args.build_dir) / "tools/ml/nn_infer"
    if not infer.exists():
        raise SystemExit(f"{infer} missing; configure with -DMUTAP_BUILD_ML_TOOLS=ON")

    rng = np.random.default_rng(3)
    if args.weights:
        w = nn.load_munn(args.weights)
        label = pathlib.Path(args.weights).name
    else:
        geometry = features.GEOM16 if args.geometry == "16k" else features.GEOM48
        w = random_weights(rng, geometry.as_array())
        label = f"random {args.geometry}"
    geom = features.Geometry.from_array(w["geometry"])
    n = geom.hop * 200
    e = rng.standard_normal(n)
    yhat = rng.standard_normal(n) * 0.5

    with tempfile.TemporaryDirectory() as td:
        td = pathlib.Path(td)
        np.savez(td / "w.npz", **w)
        export_weights.export(str(td / "w.npz"), str(td / "w.munn"))
        e.astype("<f8").tofile(td / "e.f64")
        yhat.astype("<f8").tofile(td / "yhat.f64")
        cmd = [str(infer), str(td / "w.munn"), str(td / "e.f64"), str(td / "yhat.f64"), str(td / "out.f64")]
        if args.profile == "float":
            cmd.append("--float")
        subprocess.run(cmd, check=True)
        cpp = np.fromfile(td / "out.f64", dtype="<f8")

    net = nn.SuppressorNet.__new__(nn.SuppressorNet)
    net.w = {k: v.astype(np.float64) for k, v in w.items() if k != "geometry"}
    net.gru = geom.gru
    net.h = np.zeros(geom.gru)
    bmat = features.band_matrix(geom)
    feats = features.features_from(e, yhat, bmat, geom).astype(np.float64)
    gains = net.process(feats)
    ref = features.apply_gains(e, gains, bmat, geom)

    # Both sides assemble identical frame sequences (features.stft prepends
    # the streaming warm-up frame); the C++ output stream simply trails the
    # time-aligned numpy reference by one block.
    hop = geom.hop
    m = min(len(cpp) - hop, len(ref))
    err = np.max(np.abs(cpp[hop : hop + m] - ref[:m]))
    scale = np.max(np.abs(ref[:m]))
    rel = err / scale
    tol = TOLERANCE[args.profile]
    print(f"[{args.profile}] {label}: max abs err {err:.3e} (rel {rel:.3e}) over {m} samples, bound {tol:.1e}")
    if rel > tol:
        print("PARITY FAIL")
        return 1
    print("parity OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
