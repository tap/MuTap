#!/usr/bin/env python3
"""kws_audio — decode, resample, label and augment, deterministically.

One resampler for every source and for TTS output: scipy.signal.resample_poly
with the manifest's window; the shipping decimate.h is never in the training
path. Every random draw is a function of the manifest seed and the clip id
alone (`draw_rng`), never of a process-shared generator, so the lock does not
depend on --jobs, chunking or stage order.
"""
from __future__ import annotations

import hashlib
import math
import pathlib
from fractions import Fraction

import numpy as np
import scipy
import scipy.signal
import soundfile

RATE = 16000
DECODER_ID = f"soundfile-{soundfile.__version__}-libsndfile-{soundfile.__libsndfile_version__}"


def resampler_id(window: tuple[str, float]) -> str:
    w = "-".join(str(v) for v in window)
    return f"resample_poly-{scipy.__version__}-{w}"


# ---------------------------------------------------------------- decode / resample / pcm


def read_audio(path: pathlib.Path) -> tuple[np.ndarray, int]:
    """Float64 mono (channel 0 of a multichannel file) and its sample rate."""
    x, fs = soundfile.read(str(path), dtype="float64", always_2d=True)
    return np.ascontiguousarray(x[:, 0]), int(fs)


def resample(x: np.ndarray, fs_in: int, fs_out: int = RATE, window: tuple[str, float] = ("kaiser", 5.0)) -> np.ndarray:
    if fs_in == fs_out:
        return np.asarray(x, dtype=np.float64)
    frac = Fraction(fs_out, fs_in)
    return scipy.signal.resample_poly(np.asarray(x, dtype=np.float64), frac.numerator, frac.denominator,
                                      window=tuple(window))


def to_int16(x: np.ndarray) -> np.ndarray:
    return np.clip(np.round(np.asarray(x, dtype=np.float64) * 32768.0), -32768, 32767).astype("<i2")


def from_int16(x16: np.ndarray) -> np.ndarray:
    return np.asarray(x16, dtype=np.float64) / 32768.0


def pcm_sha256(x16: np.ndarray) -> str:
    return hashlib.sha256(np.ascontiguousarray(x16, dtype="<i2").tobytes()).hexdigest()


def write_pcm(path: pathlib.Path, x16: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    soundfile.write(str(path), np.asarray(x16, dtype="<i2"), RATE, subtype="PCM_16")


def read_pcm(path: pathlib.Path) -> np.ndarray:
    x16, fs = soundfile.read(str(path), dtype="int16", always_2d=True)
    if fs != RATE:
        raise ValueError(f"{path}: pcm tier file at {fs} Hz, expected {RATE}")
    return np.ascontiguousarray(x16[:, 0]).astype("<i2")


# ---------------------------------------------------------------- labels


def endpoint(x: np.ndarray, trim_db: float, window_ms: float, fs: int = RATE) -> int | None:
    """Keyword end sample: the end of the last `window_ms` window whose RMS is within `trim_db` of the loudest.

    Non-overlapping windows; returns the sample index just past the last such window, clipped to len(x);
    None for a silent clip.
    """
    win = max(1, int(round(window_ms * 1e-3 * fs)))
    n = len(x) // win
    if n == 0:
        return None
    frames = np.asarray(x[:n * win], dtype=np.float64).reshape(n, win)
    rms = np.sqrt(np.mean(frames * frames, axis=1))
    peak = float(rms.max())
    if peak <= 0.0:
        return None
    above = np.nonzero(rms >= peak * 10.0 ** (-trim_db / 20.0))[0]
    last = int(above[-1])
    return min((last + 1) * win, len(x))


def transform_endpoint(e: int, speed: float = 1.0, rir_delay: int = 0, offset: int = 0) -> int:
    """e' = round(e / f), e'' = e' + d, e''' = e'' + offset — never re-trimmed."""
    return int(round(e / speed)) + int(rir_delay) + int(offset)


def rir_onset(h: np.ndarray, db: float = 40.0) -> int:
    """The direct-path index: the first sample within `db` of the RIR's peak (make_rir_fixtures.py's rule)."""
    a = np.abs(np.asarray(h, dtype=np.float64))
    return int(np.argmax(a >= a.max() * 10.0 ** (-db / 20.0)))


# ---------------------------------------------------------------- deterministic draws


def draw_rng(seed: int, clip_id: str, variant: int = 0) -> np.random.Generator:
    """A generator that depends on (manifest seed, clip id, variant) and nothing else."""
    digest = hashlib.sha256(f"{seed}\0{clip_id}\0{variant}".encode()).digest()
    return np.random.Generator(np.random.PCG64(int.from_bytes(digest[:8], "little")))


def split_hash(salt: str, key: str) -> float:
    """Uniform in [0, 1) from the committed salt and the split key (client id, file id, artist, ...)."""
    digest = hashlib.sha256(f"{salt}\0{key}".encode()).digest()
    return int.from_bytes(digest[:8], "little") / 2.0 ** 64


def assign_split(salt: str, key: str, fractions: dict[str, float]) -> str:
    u = split_hash(salt, key)
    acc = 0.0
    for name in ("train", "dev", "eval"):
        acc += fractions[name]
        if u < acc:
            return name
    return "eval"


# ---------------------------------------------------------------- augmentation primitives


def rms(x: np.ndarray) -> float:
    x = np.asarray(x, dtype=np.float64)
    return float(np.sqrt(np.mean(x * x))) if x.size else 0.0


def gain_db(x: np.ndarray, db: float) -> np.ndarray:
    return np.asarray(x, dtype=np.float64) * 10.0 ** (db / 20.0)


def change_speed(x: np.ndarray, factor: float, window: tuple[str, float] = ("kaiser", 5.0)) -> tuple[np.ndarray, float]:
    """Time-scale by resampling: a factor f > 1 is faster (shorter). Returns (y, the exact rational factor used)."""
    frac = Fraction(factor).limit_denominator(1000)
    up, down = frac.denominator, frac.numerator  # faster = fewer output samples
    y = scipy.signal.resample_poly(np.asarray(x, dtype=np.float64), up, down, window=tuple(window))
    return y, float(down) / float(up)


def crop_or_tile(noise: np.ndarray, n: int, rng: np.random.Generator) -> np.ndarray:
    noise = np.asarray(noise, dtype=np.float64)
    if noise.size == 0:
        return np.zeros(n)
    if noise.size >= n:
        start = int(rng.integers(0, noise.size - n + 1))
        return noise[start:start + n]
    reps = int(math.ceil(n / noise.size)) + 1
    tiled = np.tile(noise, reps)
    start = int(rng.integers(0, noise.size))
    return tiled[start:start + n]


def mix_snr(x: np.ndarray, noise: np.ndarray, snr_db: float, rng: np.random.Generator) -> np.ndarray:
    """x plus noise scaled to the requested SNR (RMS over the whole of x against RMS of the noise segment)."""
    x = np.asarray(x, dtype=np.float64)
    seg = crop_or_tile(noise, x.size, rng)
    sx, sn = rms(x), rms(seg)
    if sx <= 0.0 or sn <= 0.0:
        return x.copy()
    return x + seg * (sx / sn) / 10.0 ** (snr_db / 20.0)


def convolve_rir(x: np.ndarray, h: np.ndarray) -> tuple[np.ndarray, int]:
    """Full convolution, then normalized to x's RMS; returns (y, direct-path delay in samples)."""
    x = np.asarray(x, dtype=np.float64)
    h = np.asarray(h, dtype=np.float64)
    y = scipy.signal.fftconvolve(x, h)[:x.size + len(h) - 1]
    sx, sy = rms(x), rms(y)
    if sy > 0.0 and sx > 0.0:
        y = y * (sx / sy)
    return y, rir_onset(h)


def peak_normalize(x: np.ndarray, peak: float = 0.99) -> np.ndarray:
    m = float(np.max(np.abs(x))) if x.size else 0.0
    return x if m <= peak or m == 0.0 else x * (peak / m)
