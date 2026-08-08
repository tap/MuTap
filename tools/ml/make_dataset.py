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

Geometry-parameterized (features.Geometry): --rate 16000 trains the
benchmark geometry (hop 64, 22 bands), --rate 48000 the mutap.aec~
native geometry (hop 256, 26 bands; LibriSpeech is spectrally upsampled
3x — band-limited to 8 kHz, which the synthetic materials compensate
above). Near-end material is a MIX (--near-materials): speech plus the
rig's synthetic families (speech-envelope AR, voiced, music), ported
from tests/support/closed_loop.h with their frequencies pinned in Hz —
the measured fix for the hybrid's off-domain double-talk gap.

Per example (default 10 s):
- far end   x: utterances of one speaker; near end: drawn material,
  gated to ~50% activity so single-talk, double-talk and silence all occur
- room      : exponential-decay noise RIR (--taps), random direct delay
              (0..~5 ms) and decay; echo scaled to SER in [-6, +6] dB
- speaker   : optional tanh saturation BEFORE the room (the canceller's
              reference is the clean x, as in a real patch)
- mic noise : white, ~40 dB down
- benchmark mode (--scenarios): emits the protocol segment layout as
  f64 + a manifest for run_benchmark.py's meter.

Usage:
    python3 tools/ml/make_dataset.py --corpus .../train-clean-5 \
        --out shards48 --examples 400 --rate 48000 [--build-dir build-ml]
    python3 tools/ml/make_dataset.py --corpus .../dev-clean-2 \
        --scenarios scen48 --examples 3 --seed 7 --rate 48000
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

EXAMPLE_SECONDS = 10.0


def scan_corpus(root: pathlib.Path) -> dict[str, list[pathlib.Path]]:
    """speaker id -> flac files (LibriSpeech layout: speaker/chapter/*.flac)."""
    speakers: dict[str, list[pathlib.Path]] = {}
    for f in sorted(root.rglob("*.flac")):
        speakers.setdefault(f.relative_to(root).parts[0], []).append(f)
    if not speakers:
        raise SystemExit(f"no .flac under {root}")
    return speakers


def resample(x: np.ndarray, fs_in: int, fs_out: int) -> np.ndarray:
    """Spectral resampling (exact for the 16k -> 48k integer ratio)."""
    if fs_in == fs_out:
        return x
    n_out = int(round(len(x) * fs_out / fs_in))
    spec = np.fft.rfft(x)
    out_spec = np.zeros(n_out // 2 + 1, dtype=complex)
    m = min(len(spec), len(out_spec))
    out_spec[:m] = spec[:m]
    return np.fft.irfft(out_spec, n_out) * (n_out / len(x))


def load_speech(files: list[pathlib.Path], n: int, fs: int, rng: np.random.Generator) -> np.ndarray:
    """Concatenate random utterances to n samples at fs, unit RMS."""
    import soundfile as sf

    out = np.zeros(n)
    pos = 0
    while pos < n:
        audio, file_fs = sf.read(files[rng.integers(len(files))], dtype="float64")
        audio = resample(audio, file_fs, fs)
        take = min(len(audio), n - pos)
        out[pos : pos + take] = audio[:take]
        pos += take
    rms = np.sqrt(np.mean(out**2)) + 1e-12
    return out / rms


# ---------------------------------------------------------------- synthetic
# Numpy ports of the rig's near-end material generators
# (tests/support/closed_loop.h), frequencies pinned in Hz so the material
# is the same program at any sample rate. No bit-parity with the C++
# mt19937 versions is needed — training wants the distribution, and the
# benchmark scenarios for these materials still come from the C++ dump.

def _ar_filter(w: np.ndarray, fs: int) -> np.ndarray:
    """The rig's AR(4) speech-envelope coloring: resonators at 480 and
    1760 Hz (the 16 kHz reference's 0.03 and 0.11 cycles/sample)."""
    out = np.empty_like(w)
    s1 = [0.0, 0.0]
    s2 = [0.0, 0.0]
    a1 = [-2.0 * 0.97 * np.cos(2 * np.pi * 480.0 / fs), 0.97**2]
    a2 = [-2.0 * 0.95 * np.cos(2 * np.pi * 1760.0 / fs), 0.95**2]
    for i in range(len(w)):
        v = w[i] - a1[0] * s1[0] - a1[1] * s1[1]
        s1[1], s1[0] = s1[0], v
        v = v - a2[0] * s2[0] - a2[1] * s2[1]
        s2[1], s2[0] = s2[0], v
        out[i] = v
    return out


def ar_material(n: int, fs: int, rng: np.random.Generator) -> np.ndarray:
    x = _ar_filter(rng.standard_normal(n), fs)
    return x / (np.sqrt(np.mean(x**2)) + 1e-12)


def voiced_material(n: int, fs: int, rng: np.random.Generator) -> np.ndarray:
    period = int(round(fs / 100.0))  # 100 Hz pitch, like the rig's 160 @ 16k
    w = np.zeros(n)
    w[::period] = np.sqrt(period)
    w += 0.01 * rng.standard_normal(n)
    x = _ar_filter(w, fs)
    return x / (np.sqrt(np.mean(x**2)) + 1e-12)


def music_material(n: int, fs: int, rng: np.random.Generator) -> np.ndarray:
    f0 = np.array([55.0, 69.296, 82.407])  # low A-major chord, as the rig
    t = np.arange(n)
    x = np.zeros(n)
    phase = rng.uniform(0.0, 2 * np.pi, size=(3, 4))
    for voice in range(3):
        for h in range(1, 5):
            x += (1.0 / h) * np.sin(2 * np.pi * f0[voice] * h / fs * t + phase[voice][h - 1])
    x += 0.01 * rng.standard_normal(n)
    return x / (np.sqrt(np.mean(x**2)) + 1e-12)


def gate_activity(v: np.ndarray, fs: int, rng: np.random.Generator,
                  target_activity: float = 0.5) -> np.ndarray:
    """Random on/off gating (0.5..2 s segments, 50 ms fades) to create
    explicit single-talk / double-talk / silence structure."""
    out = v.copy()
    fade = int(0.05 * fs)
    ramp = np.linspace(0.0, 1.0, fade)
    pos = 0
    while pos < len(v):
        seg = int(rng.uniform(0.5, 2.0) * fs)
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


def fft_convolve(x: np.ndarray, h: np.ndarray, n: int) -> np.ndarray:
    m = 1 << int(np.ceil(np.log2(len(x) + len(h))))
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(h, m), m)[:n]


def random_rir(fs: int, taps: int, rng: np.random.Generator) -> np.ndarray:
    delay = int(rng.integers(0, int(0.005 * fs)))  # up to ~5 ms bulk delay
    decay = rng.uniform(0.005, 0.016) * fs         # 5..16 ms decay constant
    h = rng.standard_normal(taps) * np.exp(-np.arange(taps) / decay)
    h[:delay] = 0.0
    h[delay] = np.abs(h[delay]) + 0.5  # a distinct direct-path arrival
    return h / np.sqrt(np.sum(h**2))


def draw_near(kind: str, speakers, far_spk: str, n: int, fs: int, rng: np.random.Generator) -> np.ndarray:
    if kind == "speech":
        ids = [s for s in speakers if s != far_spk] or list(speakers)
        return load_speech(speakers[ids[rng.integers(len(ids))]], n, fs, rng)
    if kind == "ar":
        return ar_material(n, fs, rng)
    if kind == "voiced":
        return voiced_material(n, fs, rng)
    if kind == "music":
        return music_material(n, fs, rng)
    raise SystemExit(f"unknown near-end material '{kind}'")


def synthesize(speakers, rng: np.random.Generator, fs: int, taps: int, materials: list[str]):
    """One mixture: returns dict with x, near, noise, echo, y."""
    n = int(EXAMPLE_SECONDS * fs)
    ids = list(speakers)
    far_spk = ids[rng.integers(len(ids))]

    x = load_speech(speakers[far_spk], n, fs, rng)
    kind = materials[rng.integers(len(materials))]
    near = gate_activity(draw_near(kind, speakers, far_spk, n, fs, rng), fs, rng)

    drive = float(rng.choice([0.0, 1.0, 2.0, 3.0], p=[0.3, 0.3, 0.2, 0.2]))
    spk = np.tanh(drive * x) / drive if drive > 0.0 else x

    h = random_rir(fs, taps, rng)
    echo = fft_convolve(spk, h, n)
    ser_db = rng.uniform(-6.0, 6.0)  # near-end : echo power ratio
    echo_rms = np.sqrt(np.mean(echo**2)) + 1e-12
    near_rms = np.sqrt(np.mean(near**2)) + 1e-12
    echo *= (near_rms / echo_rms) * 10.0 ** (-ser_db / 20.0)

    noise = rng.standard_normal(n) * 10.0 ** (rng.uniform(-50.0, -35.0) / 20.0)
    y = echo + near + noise
    return {"x": x, "near": near, "noise": noise, "echo": echo, "y": y, "material": kind}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", default=None, help="shard directory (training mode)")
    ap.add_argument("--scenarios", default=None, help="scenario directory (benchmark mode)")
    ap.add_argument("--examples", type=int, default=300)
    ap.add_argument("--per-shard", type=int, default=50)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--rate", type=int, default=16000, choices=(16000, 48000))
    ap.add_argument("--taps", type=int, default=None, help="room length (default: 1024@16k, 2048@48k)")
    ap.add_argument("--near-materials", default="speech",
                    help="comma list drawn per example: speech,ar,voiced,music")
    ap.add_argument("--build-dir", default=str(pathlib.Path(__file__).resolve().parents[2] / "build-ml"))
    args = ap.parse_args()
    if (args.out is None) == (args.scenarios is None):
        ap.error("exactly one of --out / --scenarios")

    geom = features.GEOM48 if args.rate == 48000 else features.GEOM16
    block = geom.hop
    taps = args.taps or (2048 if args.rate == 48000 else 1024)
    partitions = taps // block
    materials = [m.strip() for m in args.near_materials.split(",")]

    speakers = scan_corpus(pathlib.Path(args.corpus))
    rng = np.random.default_rng(args.seed)
    lib = mutap_ffi.load_library(pathlib.Path(args.build_dir))
    bmat = features.band_matrix(geom)

    if args.scenarios:
        # Benchmark mode: protocol layout, no mic noise (metric cleanliness),
        # echo at 0 dB to the near end as in the rig, saturation off — the
        # HARDEST fair comparison for a speech-trained model is clean speech.
        outroot = pathlib.Path(args.scenarios)
        seg_blocks = {"converge": 1500, "double_talk": 600, "recovery": 600, "near_only": 600}
        for i in range(args.examples):
            n_total = sum(seg_blocks.values()) * block
            ids = list(speakers)
            far_spk = ids[rng.integers(len(ids))]
            x = load_speech(speakers[far_spk], n_total, args.rate, rng)
            v = np.zeros(n_total)
            b0 = seg_blocks["converge"] * block
            b1 = b0 + seg_blocks["double_talk"] * block
            b2 = b1 + seg_blocks["recovery"] * block
            v[b0:b1] = draw_near("speech", speakers, far_spk, b1 - b0, args.rate, rng)
            v[b2:] = draw_near("speech", speakers, far_spk, n_total - b2, args.rate, rng)
            x[b2:] = 0.0  # near-end-only tail
            h = random_rir(args.rate, taps, rng)
            d = fft_convolve(x, h, n_total)
            scale = 1.0 / (np.sqrt(np.mean(d[:b0] ** 2)) + 1e-12)  # echo at unit RMS
            d *= scale
            y = d + v
            folder = outroot / f"speech-s{args.seed}-{i}"
            folder.mkdir(parents=True, exist_ok=True)
            for name, arr in (("x", x), ("v", v), ("d", d), ("y", y), ("path", h * scale)):
                arr.astype("<f8").tofile(folder / f"{name}.f64")
            (folder / "manifest.json").write_text(json.dumps({
                "block_size": block, "taps": taps, "material": "speech",
                "sample_rate": args.rate,
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
        mix = synthesize(speakers, rng, args.rate, taps, materials)
        canceller = mutap_ffi.KalmanCanceller(lib, block, partitions)
        e = canceller.process(mix["x"], mix["y"])
        yhat = mix["y"] - e
        keep = mix["near"] + mix["noise"]  # what the suppressor must pass through
        f = features.features_from(e, yhat, bmat, geom)
        g, w = features.targets_from(e, keep, bmat, geom)
        n = min(len(f), len(g))
        feats.append(f[:n])
        gains.append(g[:n])
        weights.append(w[:n])
        if (i + 1) % args.per_shard == 0 or i + 1 == args.examples:
            np.savez_compressed(outdir / f"shard-{shard:04d}.npz",
                                features=np.concatenate(feats),
                                gains=np.concatenate(gains),
                                weights=np.concatenate(weights),
                                geometry=geom.as_array())
            print(f"shard-{shard:04d}: {sum(len(f) for f in feats)} frames "
                  f"({i + 1}/{args.examples} examples)", flush=True)
            feats, gains, weights = [], [], []
            shard += 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
