#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Clean-licensed training data for the learned residual suppressor.

Synthesizes echo scenarios from a speech corpus (LibriSpeech / Mini
LibriSpeech, CC BY 4.0 — every license in this pipeline is attribution or
freer), runs MuTap's LINEAR canceller over each mixture through the C ABI,
and writes (features, oracle gains, loss weights) shards. The learned
suppressor trains on what the real canceller actually leaves behind —
convergence transients, double-talk wobble, saturation harmonics —
not on an idealized residual model.

Per example (default 10 s @ 16 kHz):
- far end   x: utterances of one speaker; near end v: another speaker,
  gated to ~50% activity so single-talk, double-talk and silence all occur
- room      : 1024-tap exponential-decay noise RIR, random direct delay
              (0..16 ms) and decay; echo scaled to SER in [-6, +6] dB
- speaker   : optional tanh saturation BEFORE the room (the canceller's
              reference is the clean x, as in a real patch: the linear
              filter cannot explain the harmonics — that residual is the
              suppressor's whole job)
- mic noise : white, ~40 dB down
- benchmark mode (--scenarios): emits the same protocol segment layout as
  aec_scenario_dump (converge/double_talk/recovery/near_only) as f64 + a
  manifest, so run_benchmark.py's meter reads speech scenarios too.

Usage:
    python3 tools/ml/make_dataset.py --corpus /path/to/LibriSpeech/train-clean-5 \
        --out /path/to/shards --examples 300 [--build-dir build-ml]
    python3 tools/ml/make_dataset.py --corpus /path/to/LibriSpeech/dev-clean-2 \
        --scenarios /path/to/scenarios --examples 3 --seed 7
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import features  # noqa: E402
import mutap_ffi  # noqa: E402

BLOCK = 64
PARTITIONS = 16
FS = 16000
EXAMPLE_SECONDS = 10.0


def scan_corpus(root: pathlib.Path) -> dict[str, list[pathlib.Path]]:
    """speaker id -> flac files (LibriSpeech layout: speaker/chapter/*.flac)."""
    speakers: dict[str, list[pathlib.Path]] = {}
    for f in sorted(root.rglob("*.flac")):
        speakers.setdefault(f.relative_to(root).parts[0], []).append(f)
    if not speakers:
        raise SystemExit(f"no .flac under {root}")
    return speakers


def load_speech(files: list[pathlib.Path], n: int, rng: np.random.Generator) -> np.ndarray:
    """Concatenate random utterances to n samples, unit RMS over voiced parts."""
    import soundfile as sf

    out = np.zeros(n)
    pos = 0
    while pos < n:
        audio, fs = sf.read(files[rng.integers(len(files))], dtype="float64")
        assert fs == FS, f"expected {FS} Hz, got {fs}"
        take = min(len(audio), n - pos)
        out[pos : pos + take] = audio[:take]
        pos += take
    rms = np.sqrt(np.mean(out**2)) + 1e-12
    return out / rms


def gate_activity(v: np.ndarray, rng: np.random.Generator, target_activity: float = 0.5) -> np.ndarray:
    """Random on/off gating (0.5..2 s segments, 50 ms fades) to create
    explicit single-talk / double-talk / silence structure."""
    out = v.copy()
    fade = int(0.05 * FS)
    ramp = np.linspace(0.0, 1.0, fade)
    pos = 0
    while pos < len(v):
        seg = int(rng.uniform(0.5, 2.0) * FS)
        seg = min(seg, len(v) - pos)
        if rng.uniform() > target_activity:
            g = np.zeros(seg)
        else:
            g = np.ones(seg)
            g[:fade] = ramp[: min(fade, seg)]
            g[-fade:] *= ramp[::-1][-min(fade, seg):]
        out[pos : pos + seg] *= g
        pos += seg
    return out


def random_rir(rng: np.random.Generator, taps: int = 1024) -> np.ndarray:
    delay = int(rng.integers(0, 256))
    decay = rng.uniform(80.0, 250.0)
    h = rng.standard_normal(taps) * np.exp(-np.arange(taps) / decay)
    h[:delay] = 0.0
    h[delay] = np.abs(h[delay]) + 0.5  # a distinct direct-path arrival
    return h / np.sqrt(np.sum(h**2))


def synthesize(speakers: dict[str, list[pathlib.Path]], rng: np.random.Generator,
               with_near: bool = True):
    """One mixture: returns dict with x, near, noise, echo, y."""
    n = int(EXAMPLE_SECONDS * FS)
    ids = list(speakers)
    far_spk = ids[rng.integers(len(ids))]
    near_spk = ids[rng.integers(len(ids))]
    while len(ids) > 1 and near_spk == far_spk:
        near_spk = ids[rng.integers(len(ids))]

    x = load_speech(speakers[far_spk], n, rng)
    near = gate_activity(load_speech(speakers[near_spk], n, rng), rng) if with_near else np.zeros(n)

    drive = float(rng.choice([0.0, 1.0, 2.0, 3.0], p=[0.3, 0.3, 0.2, 0.2]))
    spk = np.tanh(drive * x) / drive if drive > 0.0 else x

    h = random_rir(rng)
    echo = np.convolve(spk, h)[:n]
    ser_db = rng.uniform(-6.0, 6.0)  # near-end : echo power ratio
    echo_rms = np.sqrt(np.mean(echo**2)) + 1e-12
    near_rms = np.sqrt(np.mean(near**2)) + 1e-12 if with_near else 1.0
    echo *= (near_rms / echo_rms) * 10.0 ** (-ser_db / 20.0)

    noise = rng.standard_normal(n) * 10.0 ** (rng.uniform(-50.0, -35.0) / 20.0)
    y = echo + near + noise
    return {"x": x, "near": near, "noise": noise, "echo": echo, "y": y}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", default=None, help="shard directory (training mode)")
    ap.add_argument("--scenarios", default=None, help="scenario directory (benchmark mode)")
    ap.add_argument("--examples", type=int, default=300)
    ap.add_argument("--per-shard", type=int, default=50)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--build-dir", default=str(pathlib.Path(__file__).resolve().parents[2] / "build-ml"))
    args = ap.parse_args()
    if (args.out is None) == (args.scenarios is None):
        ap.error("exactly one of --out / --scenarios")

    speakers = scan_corpus(pathlib.Path(args.corpus))
    rng = np.random.default_rng(args.seed)
    lib = mutap_ffi.load_library(pathlib.Path(args.build_dir))
    bmat = features.band_matrix()

    if args.scenarios:
        # Benchmark mode: protocol layout, no mic noise (metric cleanliness),
        # echo at 0 dB to the near end as in the rig, saturation off — the
        # HARDEST fair comparison for a speech-trained model is clean speech.
        outroot = pathlib.Path(args.scenarios)
        seg_blocks = {"converge": 1500, "double_talk": 600, "recovery": 600, "near_only": 600}
        for i in range(args.examples):
            n_total = sum(seg_blocks.values()) * BLOCK
            ids = list(speakers)
            far_spk = ids[rng.integers(len(ids))]
            near_spk = ids[rng.integers(len(ids))]
            while len(ids) > 1 and near_spk == far_spk:
                near_spk = ids[rng.integers(len(ids))]
            x = load_speech(speakers[far_spk], n_total, rng)
            v = np.zeros(n_total)
            b0 = seg_blocks["converge"] * BLOCK
            b1 = b0 + seg_blocks["double_talk"] * BLOCK
            b2 = b1 + seg_blocks["recovery"] * BLOCK
            v[b0:b1] = load_speech(speakers[near_spk], b1 - b0, rng)
            v[b2:] = load_speech(speakers[near_spk], n_total - b2, rng)
            x[b2:] = 0.0  # near-end-only tail
            h = random_rir(rng)
            d = np.convolve(x, h)[:n_total]
            d *= 1.0 / (np.sqrt(np.mean(d[:b0] ** 2)) + 1e-12)  # echo at unit RMS, like the rig
            h_scaled = h / (np.sqrt(np.mean(np.convolve(x, h)[:b0] ** 2)) + 1e-12)
            y = d + v
            folder = outroot / f"speech-s{args.seed}-{i}"
            folder.mkdir(parents=True, exist_ok=True)
            for name, arr in (("x", x), ("v", v), ("d", d), ("y", y), ("path", h_scaled)):
                arr.astype("<f8").tofile(folder / f"{name}.f64")
            (folder / "manifest.json").write_text(json.dumps({
                "block_size": BLOCK, "taps": len(h), "material": "speech",
                "seed": args.seed * 1000 + i, "room": "random",
                "segments": [{"name": k, "blocks": nb} for k, nb in seg_blocks.items()],
            }, indent=2))
            print(f"scenario {folder}")
        return 0

    outdir = pathlib.Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    feats, gains, weights = [], [], []
    shard = 0
    for i in range(args.examples):
        mix = synthesize(speakers, rng)
        canceller = mutap_ffi.KalmanCanceller(lib, BLOCK, PARTITIONS)
        e = canceller.process(mix["x"], mix["y"])
        yhat = mix["y"] - e
        keep = mix["near"] + mix["noise"]  # what the suppressor must pass through
        f = features.features_from(e, yhat, bmat)
        g, w = features.targets_from(e, keep, bmat)
        n = min(len(f), len(g))
        feats.append(f[:n])
        gains.append(g[:n])
        weights.append(w[:n])
        if (i + 1) % args.per_shard == 0 or i + 1 == args.examples:
            np.savez_compressed(outdir / f"shard-{shard:04d}.npz",
                                features=np.concatenate(feats),
                                gains=np.concatenate(gains),
                                weights=np.concatenate(weights))
            print(f"shard-{shard:04d}: {sum(len(f) for f in feats)} frames "
                  f"({i + 1}/{args.examples} examples)", flush=True)
            feats, gains, weights = [], [], []
            shard += 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
