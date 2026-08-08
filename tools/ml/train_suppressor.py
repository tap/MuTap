#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Train the learned residual suppressor on make_dataset.py shards.

    python3 tools/ml/train_suppressor.py --data /path/to/shards \
        --out suppressor_weights.npz [--epochs 20] [--seq 500] [--batch 32]

Loss: loss-weighted MSE on sqrt-compressed gains (the RNNoise objective's
core): errors on audible bands count, near-silent bands don't, and the
sqrt compression spends resolution where gains hurt (deep suppression).
Exports weights as an .npz matching nn.SuppressorNet's naming.
"""
from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import torch
from torch import nn as tnn

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import nn as ref_nn  # noqa: E402


class SuppressorNet(tnn.Module):
    def __init__(self, n_features=ref_nn.INPUT_DIM, n_dense=ref_nn.DENSE_DIM,
                 n_gru=ref_nn.GRU_DIM, n_bands=ref_nn.OUTPUT_DIM):
        super().__init__()
        self.dense_in = tnn.Linear(n_features, n_dense)
        self.gru = tnn.GRU(n_dense, n_gru, batch_first=True)
        self.dense_out = tnn.Linear(n_gru, n_bands)

    def forward(self, x):
        d = torch.tanh(self.dense_in(x))
        h, _ = self.gru(d)
        return torch.sigmoid(self.dense_out(h))


def load_shards(folder: pathlib.Path):
    feats, gains, weights, geometry = [], [], [], None
    for f in sorted(folder.glob("shard-*.npz")):
        z = np.load(f)
        feats.append(z["features"])
        gains.append(z["gains"])
        weights.append(z["weights"])
        g = tuple(int(v) for v in z["geometry"]) if "geometry" in z else (16000, 64, 22, 64, 96)
        assert geometry in (None, g), f"mixed geometries: {geometry} vs {g} in {f}"
        geometry = g
    return (np.concatenate(feats), np.concatenate(gains), np.concatenate(weights), geometry)


def to_sequences(a: np.ndarray, seq: int) -> np.ndarray:
    n = (len(a) // seq) * seq
    return a[:n].reshape(-1, seq, a.shape[-1])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--seq", type=int, default=500)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--val-frac", type=float, default=0.1)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    feats, gains, weights, geometry = load_shards(pathlib.Path(args.data))
    rate, hop, bands, dense, gru = geometry
    print(f"geometry: {rate} Hz, hop {hop}, {bands} bands, dense {dense}, gru {gru}")
    x = to_sequences(feats, args.seq)
    g = to_sequences(gains, args.seq)
    w = to_sequences(weights, args.seq)
    print(f"{len(feats)} frames -> {len(x)} sequences of {args.seq}")

    rng = np.random.default_rng(args.seed)
    order = rng.permutation(len(x))
    n_val = max(1, int(len(x) * args.val_frac))
    val_idx, tr_idx = order[:n_val], order[n_val:]

    def tensors(idx):
        return (torch.from_numpy(x[idx]), torch.from_numpy(np.sqrt(g[idx])), torch.from_numpy(w[idx]))

    model = SuppressorNet(n_features=2 * bands, n_dense=dense, n_gru=gru, n_bands=bands)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    best_val = float("inf")

    for epoch in range(args.epochs):
        model.train()
        perm = rng.permutation(tr_idx)
        total = 0.0
        for i in range(0, len(perm), args.batch):
            xb, gb, wb = tensors(perm[i : i + args.batch])
            opt.zero_grad()
            pred = torch.sqrt(model(xb) + 1e-9)
            loss = (wb * (pred - gb) ** 2).mean()
            loss.backward()
            opt.step()
            total += float(loss.detach()) * len(xb)
        model.eval()
        with torch.no_grad():
            xv, gv, wv = tensors(val_idx)
            val = float((wv * (torch.sqrt(model(xv) + 1e-9) - gv) ** 2).mean())
        marker = ""
        if val < best_val:
            best_val = val
            state = {k: v.detach().numpy().copy() for k, v in model.state_dict().items()}
            np.savez(args.out, **state, geometry=np.asarray(geometry, dtype="<u4"))
            # checkpoint: best-so-far survives an interrupted run
            marker = "  *"
        print(f"epoch {epoch + 1:3d}: train {total / len(tr_idx):.5f}  val {val:.5f}{marker}", flush=True)

    n_params = sum(v.size for v in state.values())
    print(f"wrote {args.out} ({n_params} parameters, best val {best_val:.5f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
