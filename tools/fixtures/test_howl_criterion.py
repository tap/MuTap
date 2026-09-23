# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Oracle for howl_criterion.py: a simulated acoustic loop with a known limit.

Run from the repository root:

    python3 -m unittest tools/fixtures/test_howl_criterion.py -v

The loop (numpy, block-wise, block = loop delay so it is causal):

    mic      m = h_mouth * voice + F * (y + track) + room noise
    chain    c = m - F_hat * (y + track)            (no canceller: F_hat = 0)
             e = H_chain * c                         (in-chain reverb, or identity)
    speaker  y[n] = clip tanh(G(t) e[n - D] / clip)  (D = 5 ms unless stated)

so the loop closes through R = F - F_hat (F without a canceller) and e is
the tool's documented input (the mic, when dry). F is a loudspeaker
band-limit (2nd-order HP 80 Hz, 4th-order LP 16 kHz) times a room: rooms
come from both generator families, random decaying FIRs (0.3 s, RT60
0.25-0.5 s, seeded) and the committed pyroomacoustics fixtures
tests/fixtures/rir_*.h, plus "rtR-S" rooms with a chosen RT60 for the
reverberation cases. The mouth-to-mic path decays at the room's RT60,
which comes from measure_rir.py's own Schroeder code (imported, read-only)
in the report schema --rt60 reads. With a canceller, R is a diffuse,
room-like FIR scaled 20 dB below F (textbook MSG 20 dB higher), or zero
(the ideal canceller). The voice is a looped 8.4 s phrase of held harmonic
notes (220/330/440 Hz, 1-3 s, 30-cent 5.5 Hz vibrato) with unvoiced bursts
and gaps; the track is low-passed noise plus a looped four-chord pad (held
chords are persistent narrowband peaks too). Every scenario starts with
10 s at a constant low gain: the calibration window.

Ground truth: the textbook MSG = -20 log10 max|R|, and beside it the
Nyquist limit of this loop (the gain at which G R(w) e^{-jwD} first
reaches +1), which is what the ramp actually crosses. Every threshold
below is a measured number, written beside the assertion; closed-loop
numbers are asserted per room with margin and as medians over rooms. The
full sweeps behind the comments ran the same functions over all nine
rooms (and the rt rooms) in a scratch harness.

The frequency-shifter oracle (class ShifterOracle) puts an SSB shifter - the
review's Niemitalo 4+4 IIR allpass-pair Hilbert, coefficients read from
_afc_poc/phase0-data/review/dl_iir.h - in the forward path. That loop is
time-variant, so its reference limit is bisected (80 s unit-RMS white
near-end, MuTap's rule: any 64-sample block RMS >= 100, speaker limited at
1000). It takes ~10 min on 12 cores and runs only with HOWL_SLOW=1:

    HOWL_SLOW=1 python3 -m unittest tools/fixtures/test_howl_criterion.py -v
"""

import functools
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np
import scipy.signal
from scipy.io import wavfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import howl_criterion as hc  # noqa: E402
import measure_rir  # noqa: E402  (read-only use: its Schroeder T20/T30 give the rooms' RT60)

FS = 48000
D = 240  # loop delay, samples (5 ms); also the simulation block
N_FFT, HOP = hc.Params().geometry(FS)
WINDOW_SUM = float(scipy.signal.get_window("hann", N_FFT).sum())


def report(msg: str) -> None:
    print(f"\n    [measured] {msg}", file=sys.stderr, flush=True)


# --------------------------------------------------------------------------
# Program material


@functools.lru_cache(maxsize=None)
def voice_phrase() -> np.ndarray:
    """One 8.4 s sung phrase: held notes with vibrato, unvoiced bursts, gaps."""
    phrase = [("note", 220.0, 2.0), ("gap", 0.2), ("noise", 0.15), ("note", 330.0, 1.5),
              ("note", 440.0, 3.0), ("gap", 0.3), ("noise", 0.2), ("note", 330.0, 1.05)]
    rng = np.random.default_rng(1234)
    parts = []
    for item in phrase:
        if item[0] == "gap":
            parts.append(np.zeros(int(item[1] * FS)))
        elif item[0] == "noise":
            n = int(item[1] * FS)
            b, a = scipy.signal.butter(4, [2000, 8000], btype="band", fs=FS)
            parts.append(scipy.signal.lfilter(b, a, rng.standard_normal(n)) * np.hanning(n) * 0.5)
        else:
            f0, d = item[1], item[2]
            t = np.arange(int(d * FS)) / FS
            depth = 0.30 / 12.0 * np.clip((t - 0.3) / 0.3, 0.0, 1.0)
            phase = 2 * np.pi * np.cumsum(f0 * 2.0 ** (depth * np.sin(2 * np.pi * 5.5 * t))) / FS
            x = np.zeros(len(t))
            h = 1
            while h * f0 < 5000:
                formant = 1 + 2 * np.exp(-(((h * f0 - 800) / 400) ** 2)) + np.exp(-(((h * f0 - 2500) / 600) ** 2))
                x += formant / h * np.sin(h * phase)
                h += 1
            parts.append(0.2 * x * np.minimum(1.0, np.minimum(t / 0.03, (d - t) / 0.05)))
    return np.concatenate(parts)


@functools.lru_cache(maxsize=None)
def pad_loop() -> np.ndarray:
    """Four held chords, 2 s each, six harmonics per note."""
    chords = [[110.0, 138.6, 164.8], [146.8, 185.0, 220.0], [123.5, 155.6, 185.0], [164.8, 207.7, 246.9]]
    t = np.arange(int(2.0 * FS)) / FS
    env = np.minimum(1.0, np.minimum(t / 0.05, (2.0 - t) / 0.08))
    out = []
    for ch in chords:
        x = sum(np.sin(2 * np.pi * f * h * t + h) / h for f in ch for h in range(1, 7))
        out.append(x * env)
    return np.concatenate(out) * 0.06


def tile(x: np.ndarray, n: int) -> np.ndarray:
    return np.tile(x, -(-n // len(x)))[:n]


def program(dur_s: float, seed: int) -> tuple[np.ndarray, np.ndarray]:
    n = int(dur_s * FS)
    rng = np.random.default_rng(seed)
    b, a = scipy.signal.butter(2, 1500, fs=FS)
    track = scipy.signal.lfilter(b, a, rng.standard_normal(n)) * 0.15 + tile(pad_loop(), n)
    return tile(voice_phrase(), n), track


def gaps_gate(dur_s: float, gaps: list[tuple[float, float]]) -> np.ndarray:
    t = np.arange(int(dur_s * FS)) / FS
    g = np.ones_like(t)
    for a, b in gaps:
        g *= np.clip(np.maximum((a - t) / 0.01, (t - b) / 0.01), 0.0, 1.0)
    return g


# --------------------------------------------------------------------------
# Rooms and the loop


def loudspeaker(room: np.ndarray) -> np.ndarray:
    x = np.concatenate([room, np.zeros(int(0.02 * FS))])
    sos = np.concatenate([scipy.signal.butter(2, 80, "high", fs=FS, output="sos"),
                          scipy.signal.butter(4, 16000, fs=FS, output="sos")])
    return scipy.signal.sosfilt(sos, x)


def decaying_room(rng: np.random.Generator, rt60: float, length_s: float) -> np.ndarray:
    d0 = int(rng.integers(60, 200))
    n = int(length_s * FS)
    t = np.arange(n - d0) / FS
    tail = rng.standard_normal(n - d0) * 10 ** (-3 * t / rt60)
    tail[0] = 0.0
    f = np.zeros(n)
    f[d0:] = tail * 0.05
    f[d0] += 1.0
    return f / np.sqrt(np.sum(f * f))


@functools.lru_cache(maxsize=None)
def room(name: str) -> np.ndarray:
    """'synthN' (random decaying FIR, seed N, RT60 0.25-0.5 s), 'rtR-S' (RT60 R s,
    seed S; the reverberation experiments) or a committed fixture name."""
    if name.startswith("rt"):
        rt, seed = name[2:].split("-")
        f = decaying_room(np.random.default_rng(int(seed)), float(rt), min(1.0, 0.8 * float(rt) + 0.1))
    elif name.startswith("synth"):
        rng = np.random.default_rng(int(name[5:]))
        rt60 = rng.uniform(0.25, 0.5)
        d0 = int(rng.integers(60, 200))
        n = int(0.3 * FS)
        t = np.arange(n - d0) / FS
        tail = rng.standard_normal(n - d0) * 10 ** (-3 * t / rt60)
        tail[0] = 0.0
        f = np.zeros(n)
        f[d0:] = tail * 0.05
        f[d0] += 1.0
        f /= np.sqrt(np.sum(f * f))
    else:
        txt = (REPO / "tests" / "fixtures" / f"rir_{name}.h").read_text()
        body = txt[txt.index("= {") + 3 : txt.index("};")]
        f = np.array([float(v[:-1]) for v in re.findall(r"[-+0-9.eE]+f", body)])
    return loudspeaker(f)


@functools.lru_cache(maxsize=None)
def rt60_report(name: str) -> dict:
    """The room's decay in measure_rir.py's report schema (one acoustic channel)."""
    f = room(name)
    dec = measure_rir.schroeder(f, FS)
    rep = {"per_channel": {"1": {"role": "acoustic", "T20_s": dec["T20"], "T30_s": dec["T30"],
                                 "octave_bands": measure_rir.octave_decay(f, FS)}}}
    return json.loads(json.dumps(measure_rir._jsonable(rep)))


@functools.lru_cache(maxsize=None)
def room_rt60(name: str) -> float:
    """Broadband T30 of F (T20 where T30 is undefined): the mouth path's tail uses it too."""
    r = rt60_report(name)["per_channel"]["1"]
    return float(r["T30_s"] if r["T30_s"] is not None else r["T20_s"])


def msg_of(h: np.ndarray) -> float:
    """Textbook MSG of a loop through h: -20 log10 max|H|."""
    return float(-20 * np.log10(np.max(np.abs(np.fft.rfft(h, 1 << 21)))))


def nyquist_of(h: np.ndarray, delay: int) -> float:
    """Gain at which G H(w) e^{-jw delay} first reaches +1 on the positive real axis."""
    nfft = 1 << 22
    w = np.arange(nfft // 2 + 1) * 2 * np.pi / nfft
    L = np.fft.rfft(h, nfft) * np.exp(-1j * w * delay)
    im = L.imag
    i = np.flatnonzero(np.signbit(im[:-1]) != np.signbit(im[1:]))
    re_ = L.real[i] + im[i] / (im[i] - im[i + 1]) * (L.real[i + 1] - L.real[i])
    return float(-20 * np.log10(max([L.real[0]] + list(re_[re_ > 0]))))


@functools.lru_cache(maxsize=None)
def msg_db(name: str) -> float:
    return msg_of(room(name))


@functools.lru_cache(maxsize=None)
def nyquist_db(name: str, delay: int = D) -> float:
    return nyquist_of(room(name), delay)


ASG_DB = 20.0  # the simulated canceller's added stable gain (textbook MSG of R minus that of F)


@functools.lru_cache(maxsize=None)
def residual(name: str) -> np.ndarray:
    """R = F - F_hat for a canceller whose error is a diffuse, room-like FIR (a known perturbation of F),
    scaled so that the textbook MSG of R is ASG_DB above that of F."""
    rng = np.random.default_rng([sum(map(ord, name)), 7])
    r = loudspeaker(decaying_room(rng, max(room_rt60(name), 0.1), 0.3))
    return r * 10 ** ((msg_of(r) - msg_db(name) - ASG_DB) / 20)


HOWL_BLOCK = 64  # MuTap's howl rule (tests/support/closed_loop.h): any 64-sample block RMS >= 100,
HOWL_RMS = 100.0  # i.e. 40 dB over a unit-RMS white near-end, with the speaker limited at 1000


@functools.lru_cache(maxsize=None)
def hilbert_coefficients() -> tuple[tuple[float, ...], tuple[float, ...]]:
    """Niemitalo 4+4 allpass-pair coefficients, read from the review's dl_iir.h (not included)."""
    txt = (REPO / "_afc_poc" / "phase0-data" / "review" / "dl_iir.h").read_text()
    grab = lambda name: tuple(float(v) for v in re.search(name + r"\[4\]\s*=\s*\{([^}]*)\}", txt).group(1).split(","))
    return grab("k_a"), grab("k_b")


class Shifter:
    """Single-sideband frequency shift by the IIR allpass-pair Hilbert, as dl_iir.h's shift_iir: the real
    branch is the k_b chain, the imaginary the k_a chain plus one sample of delay; each section is
    (a^2 - z^-2) / (1 - a^2 z^-2); out = re cos(w t) - im sin(w t). Stateful across blocks."""

    def __init__(self, shift_hz: float, fs: int = FS):
        k_a, k_b = hilbert_coefficients()
        sos = lambda ks: np.array([[k * k, 0.0, -1.0, 1.0, 0.0, -k * k] for k in ks])
        self.sos_re, self.sos_im = sos(k_b), sos(k_a)
        self.z_re, self.z_im = np.zeros((4, 2)), np.zeros((4, 2))
        self.im_prev = 0.0
        self.w = 2 * np.pi * shift_hz / fs
        self.phase = 0.0

    def __call__(self, x: np.ndarray) -> np.ndarray:
        re_, self.z_re = scipy.signal.sosfilt(self.sos_re, x, zi=self.z_re)
        im, self.z_im = scipy.signal.sosfilt(self.sos_im, x, zi=self.z_im)
        im_d = np.concatenate([[self.im_prev], im[:-1]])
        self.im_prev = im[-1]
        th = self.phase + self.w * np.arange(len(x))
        self.phase = (self.phase + self.w * len(x)) % (2 * np.pi)
        return re_ * np.cos(th) - im_d * np.sin(th)


def howls(name: str, gain_db: float, delay_ms: int, shift_hz: float, probe_s: float, seed: int = 77) -> bool:
    """One probe of the reference: unit-RMS white near-end, the dry loop F with the shifter in the forward
    path, speaker limited at 1000; MuTap's howl rule."""
    v = np.random.default_rng([seed, 3]).standard_normal(int(probe_s * FS))
    fwd = Shifter(shift_hz) if shift_hz else None
    return _loop(v, room(name), gain_db, int(round(delay_ms * FS / 1000)), 1000.0, fwd, HOWL_RMS)[2]


@functools.lru_cache(maxsize=None)
def bisected_msg(name: str, delay_ms: int, shift_hz: float, probe_s: float, tol_db: float = 0.05) -> float:
    """The largest gain (within tol_db) that did not howl over probe_s, bisected like measured_msg_db."""
    lo, hi = msg_db(name) - 10.0, msg_db(name) + 20.0
    assert not howls(name, lo, delay_ms, shift_hz, probe_s) and howls(name, hi, delay_ms, shift_hz, probe_s)
    while hi - lo > tol_db:
        mid = 0.5 * (lo + hi)
        lo, hi = (lo, mid) if howls(name, mid, delay_ms, shift_hz, probe_s) else (mid, hi)
    return lo


@functools.lru_cache(maxsize=None)
def chain_reverb(rt60: float = 1.5, wet: float = 0.3) -> np.ndarray:
    """In-chain reverb: dry + wet * exponential noise (unit energy, 10 ms predelay, RT60), as one FIR."""
    rng = np.random.default_rng(1500)
    n = int(0.8 * rt60 * FS)
    t = np.arange(n) / FS
    tail = rng.standard_normal(n) * 10 ** (-3 * t / rt60)
    tail /= np.sqrt(np.sum(tail * tail))
    h = np.zeros(int(0.01 * FS) + n)
    h[0] = 1.0
    h[int(0.01 * FS):] += wet * tail
    return h


def mouth_path(rng: np.random.Generator, rt60: float) -> np.ndarray:
    """Mouth simulator -> mic: direct path at 56 samples plus a room tail decaying at the room's RT60."""
    n = int(max(0.25, min(1.0, 0.8 * rt60 + 0.1)) * FS)
    h = np.zeros(n)
    h[56] = 1.0
    t = np.arange(n - 300) / FS
    h[300:] = rng.standard_normal(n - 300) * 10 ** (-3 * t / rt60) * 0.02
    return h


def simulate(voice, track, f, gain_db, seed, noise_rms=1e-4, mouth_rt60=0.35, delay=D, room_f=None, chain=None,
             clip=1.0, fwd=None):
    """The chain's output e (and, with room_f, the mic) of the closed loop; gain_db is a scalar or per sample.

    f is the path the loop closes through: the room F without a canceller (then e is the mic), the
    residual R = F - F_hat with one. The chain: c = mic - F_hat * y, e = chain * c (an in-chain FIR,
    e.g. a reverb; identity if None), speaker y = clip * tanh(G e[n - delay] / clip) + track.
    """
    # A stream of its own: seeded like program(), the room noise would replay the
    # track's noise (GCC-PHAT then locks onto that copy instead of the room path).
    rng = np.random.default_rng([seed, 1])
    n = len(voice)
    direct = scipy.signal.fftconvolve(voice, mouth_path(rng, mouth_rt60))[:n]
    direct += noise_rms * rng.standard_normal(n)
    open_mic = direct + scipy.signal.fftconvolve(track, f)[:n]
    if chain is not None:
        open_mic = scipy.signal.fftconvolve(open_mic, chain)[:n]
        f = scipy.signal.fftconvolve(chain, f)
    e, fb = _loop(open_mic, f, gain_db, delay, clip, fwd)
    if room_f is None:
        return e
    return e, direct + scipy.signal.fftconvolve(fb + track, room_f)[:n]


def _loop(open_mic, f, gain_db, D, clip, fwd=None, howl_rms=None):
    """e = open_mic + f * y_fb, y_fb[n] = clip tanh(G fwd(e)[n - D] / clip); block-wise (block = D), UPOLS.
    fwd: a stateful per-block forward-path process (a frequency shifter), or None. With howl_rms the run
    stops at the first 64-sample block of e whose RMS reaches it (MuTap's howl rule) and returns
    (e, fb, howled)."""
    n = len(open_mic)
    checked = 0
    fb = np.zeros(n)
    g = np.broadcast_to(10 ** (np.asarray(gain_db, dtype=np.float64) / 20), (n,))
    parts = -(-len(f) // D)
    fp = np.zeros(parts * D)
    fp[: len(f)] = f
    spec = np.fft.rfft(np.concatenate([fp.reshape(parts, D), np.zeros((parts, D))], axis=1), axis=1)
    spec_rev = spec[::-1].copy()  # oldest partition first, matching the delay line
    fdl = np.zeros((2 * parts, D + 1), dtype=complex)  # doubled: always a contiguous window
    mic = np.zeros(n)
    buf = np.zeros(2 * D)
    for b in range(n // D):
        s = b * D
        buf[:D] = buf[D:]
        if s >= D:
            blk = mic[s - D : s] if fwd is None else fwd(mic[s - D : s])
            buf[D:] = clip * np.tanh(g[s : s + D] * blk / clip)
        else:
            buf[D:] = 0.0
        fb[s : s + D] = buf[D:]
        i = b % parts
        fdl[i] = fdl[i + parts] = np.fft.rfft(buf)
        acc = np.einsum("pk,pk->k", fdl[i + 1 : i + 1 + parts], spec_rev)
        mic[s : s + D] = open_mic[s : s + D] + np.fft.irfft(acc, 2 * D)[D:]
        if howl_rms is not None:
            upto = (s + D) // HOWL_BLOCK * HOWL_BLOCK
            if upto > checked:
                blocks = mic[checked:upto].reshape(-1, HOWL_BLOCK)
                if not np.all(np.isfinite(blocks)) or np.max(np.sqrt(np.mean(blocks * blocks, axis=1))) >= howl_rms:
                    return mic, fb, True
                checked = upto
    return (mic, fb, False) if howl_rms is not None else (mic, fb)


def tone(n: int, t0: float, dur: float, f0: float, rate: float, amp: float) -> np.ndarray:
    """A sinusoid (linear chirp if rate != 0 Hz/s) with 5 ms ramps, placed at t0."""
    x = np.zeros(n)
    t = np.arange(int(dur * FS)) / FS
    ph = 2 * np.pi * (f0 * t + 0.5 * rate * t * t)
    x[int(t0 * FS) : int(t0 * FS) + len(t)] = amp * np.sin(ph) * np.minimum(1, np.minimum(t / 0.005, (dur - t) / 0.005))
    return x


def amp_over(e_level: float, db: float) -> float:
    """Amplitude of a stationary on-bin tone whose peak-bin power is db over e_level."""
    return 2.0 * np.sqrt(e_level * 10 ** (db / 10)) / WINDOW_SUM


def e_level(result: dict, f0: float, f1: float, t0: float, t1: float) -> float:
    """Median explained power E (the criterion's envelope) over a band and interval."""
    sp = result["_spectrogram"]
    k0, k1 = int(np.floor(f0 * N_FFT / FS)), int(np.ceil(f1 * N_FFT / FS)) + 1
    sel = (sp["times"] >= t0) & (sp["times"] <= t1)
    return float(np.median(sp["e"][sel][:, k0:k1]))


# --------------------------------------------------------------------------
# Cached runs (several tests share them)

RAMP_ROOMS = ("synth0", "synth1", "studio", "hall")
HELD_ROOMS = ("synth0", "synth1", "hall")
INTRUDER_ROOMS = ("synth0", "hall")
GAP_ROOMS = ("synth0", "synth1", "hall")
RT_ROOMS = ("rt0.6-10", "rt0.8-11")  # from the reverberation experiment's rooms (two seeds per RT60 there)
HOLD_SHIFT_ROOMS = ("synth1", "hall")
RESIDUAL_RUNS = (("synth0", 5), ("synth0", 10), ("synth0", 20), ("synth0", 30))
CHAIN_DELAY_MS = 20  # a chain with a reverb in it has latency; also 4x fewer simulation blocks
DRY_DELAY_RUNS = (("hall", 10), ("hall", 30))
IDEAL_ROOM = "synth0"
ALL_ROOMS = ("synth0", "synth1", "synth2", "synth3", "synth4", "studio", "rehearsal", "hall", "cabin")
RAMP_RATE = 0.5  # dB/s: 1 dB per 2 s
RAMP_SPAN = 20.0  # start at MSG - 20 dB
RAMP_PAST = 4.5  # run until MSG + 4.5 dB (Nyquist limits sit up to +2.2 dB above the textbook MSG)
IDEAL_PAST = 30.0  # the ideal canceller's ramp: to the room's MSG + 30 dB


RAMP_WARMUP = 10.0  # s at the start gain before the ramp: the calibration window
CAL = (0.0, 10.0)  # every scenario below starts with 10 s at a constant, low gain
CHAIN_ROOMS = ("synth0", "hall")


def params(name: str | None, hold: bool = True, short: bool = True, scale: float = 1.0,
           chain_rt60: float | None = None) -> hc.Params:
    """hold: the reverberation hold at the room's own RT60 (its measure_rir report)."""
    rt60 = hc.rt60_from_report(rt60_report(name)) if hold else None
    return hc.Params(rt60=rt60, rt60_scale=scale, short_pass=short, calibrate_s=CAL, chain_rt60=chain_rt60)


def loop_path(name: str, case: str) -> np.ndarray:
    """What the loop closes through: F ("dry", no canceller), R = F - F_hat ("residual"), or nothing
    ("ideal": F_hat = F, the loop never closes)."""
    return {"dry": room, "residual": residual}[case](name) if case != "ideal" else np.zeros(1)


@functools.lru_cache(maxsize=None)
def loop_limits(name: str, case: str, delay_ms: int) -> tuple[float, float]:
    """(textbook MSG, Nyquist limit) of the loop, dB."""
    lp = loop_path(name, case)
    return msg_of(lp), nyquist_of(lp, int(round(delay_ms * FS / 1000)))


@functools.lru_cache(maxsize=None)
def ramp_sim(name: str, delay_ms: int = 5, case: str = "dry"):
    """10 s at MSG - 20 dB, then 1 dB / 2 s to MSG + 4.5 dB (MSG of the loop path; for "ideal", the
    room's, to + 30 dB). Returns (voice, track, e, mic, start_db); e is the chain output (the mic when dry)."""
    ref = msg_db(name) if case == "ideal" else msg_of(loop_path(name, case))
    dur = RAMP_WARMUP + (RAMP_SPAN + (IDEAL_PAST if case == "ideal" else RAMP_PAST)) / RAMP_RATE
    voice, track = program(dur, 100)
    t = np.arange(len(voice)) / FS
    lp = loop_path(name, case)
    start = ref - RAMP_SPAN
    gain = start + RAMP_RATE * np.maximum(t - RAMP_WARMUP, 0.0)
    delay = int(round(delay_ms * FS / 1000))
    if case == "dry":
        e = simulate(voice, track, lp, gain, 100, mouth_rt60=room_rt60(name), delay=delay)
        return voice, track, e, e, start
    # same headroom over the program as the dry loop's speaker (clip 1.0 at the dry gains)
    clip = 10 ** ((ref + (IDEAL_PAST if case == "ideal" else 0.0) - msg_db(name)) / 20)
    e, mic = simulate(voice, track, lp, gain, 100, mouth_rt60=room_rt60(name), delay=delay, room_f=room(name),
                      clip=clip)
    return voice, track, e, mic, start


SHIFTS_HZ = (2.0, 5.0)
SHIFT_DELAYS_MS = (10, 20)
SHIFT_PROBE_S = 80.0  # the reference's probe length (see the convergence test)
SHIFT_PAST = 6.0  # the shifter ramp runs to the bisected limit + 6 dB


@functools.lru_cache(maxsize=None)
def shift_sim(name: str, delay_ms: int, shift_hz: float):
    """The dry loop with the shifter in the forward path: 10 s at the reference limit - 20 dB, then
    1 dB / 2 s to the reference + 6 dB. Returns (voice, track, e, start_db, reference_db)."""
    ref = bisected_msg(name, delay_ms, shift_hz, SHIFT_PROBE_S)
    dur = RAMP_WARMUP + (RAMP_SPAN + SHIFT_PAST) / RAMP_RATE
    voice, track = program(dur, 100)
    t = np.arange(len(voice)) / FS
    start = ref - RAMP_SPAN
    gain = start + RAMP_RATE * np.maximum(t - RAMP_WARMUP, 0.0)
    e = simulate(voice, track, room(name), gain, 100, mouth_rt60=room_rt60(name),
                 delay=int(round(delay_ms * FS / 1000)), fwd=Shifter(shift_hz))
    return voice, track, e, start, ref


@functools.lru_cache(maxsize=None)
def shift_run(name: str, delay_ms: int, shift_hz: float, bank: bool) -> dict:
    voice, track, e, start, _ = shift_sim(name, delay_ms, shift_hz)
    p = params(name)
    p.dechirp_rates = hc.DECHIRP_BANK if bank else ()
    p.short_pass = not bank
    res = hc.analyze(e, FS, [voice, track], p)
    return hc.apply_gain_map(res, hc.ramp_map(start, RAMP_RATE, RAMP_WARMUP))


@functools.lru_cache(maxsize=None)
def ramp_run(name: str, hold: bool = True, short: bool | None = None, delay_ms: int = 5, case: str = "dry",
             signal: str = "e") -> dict:
    voice, track, e, mic, start = ramp_sim(name, delay_ms, case)
    short = case != "ideal" if short is None else short  # the ideal-canceller demonstration: long pass only
    res = hc.analyze(e if signal == "e" else mic, FS, [voice, track], params(name, hold, short))
    return hc.apply_gain_map(res, hc.ramp_map(start, RAMP_RATE, RAMP_WARMUP))


@functools.lru_cache(maxsize=None)
def held_mic(name: str, offset_db: float) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """10 s calibration at MSG - 20, then 30 s at MSG + offset_db."""
    voice, track = program(40.0, 200)
    t = np.arange(len(voice)) / FS
    gain = np.where(t < 10.0, msg_db(name) - 20, msg_db(name) + offset_db)
    return voice, track, simulate(voice, track, room(name), gain, 200, mouth_rt60=room_rt60(name))


@functools.lru_cache(maxsize=None)
def held_run(name: str, offset_db: float, hold: bool = True, short: bool = True) -> dict:
    voice, track, mic = held_mic(name, offset_db)
    return hc.analyze(mic, FS, [voice, track], params(name, hold, short))


@functools.lru_cache(maxsize=None)
def intruder_base(name: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, dict]:
    """30 s at MSG - 10 dB (a stable loop), analysed with its envelopes kept."""
    voice, track = program(30.0, 300)
    mic = simulate(voice, track, room(name), msg_db(name) - 10, 300, mouth_rt60=room_rt60(name))
    return voice, track, mic, hc.analyze(mic, FS, [voice, track], params(name), keep_spectrogram=True)


BURST_F = 256 * FS / N_FFT  # 1500 Hz, on a bin centre
BURSTS = ((0.3, 14.0), (0.7, 20.0))  # (duration s, start s), +30 dB over E
CHIRP = (25.0, 6000.0, 500.0)  # start s, start Hz, Hz/s, for 1 s, +35 dB over E (long pass)
CHIRP_SHORT = (17.0, 6000.0, 800.0, 35.0)  # start s, start Hz, Hz/s, dB over E: the short pass's case


def chirp(n: int, base: dict, c0: float, f0: float, rate: float, db: float) -> np.ndarray:
    """1 s linear chirp, db over the long pass's median E along its band and interval."""
    return tone(n, c0, 1.0, f0, rate, amp_over(e_level(base, f0, f0 + rate, c0, c0 + 1.0), db))


@functools.lru_cache(maxsize=None)
def intruder_run(name: str) -> dict:
    voice, track, mic, base = intruder_base(name)
    extra = np.zeros(len(mic))
    for dur, t0 in BURSTS:
        extra += tone(len(mic), t0, dur, BURST_F, 0.0, amp_over(e_level(base, BURST_F, BURST_F, t0, t0 + dur), 30.0))
    extra += chirp(len(mic), base, *CHIRP, 35.0)
    extra += chirp(len(mic), base, *CHIRP_SHORT)
    return hc.analyze(mic + extra, FS, [voice, track], params(name))


BANK_CHIRPS = ((15.0, 6000.0, 500.0), (21.0, 6000.0, 800.0))  # start s, Hz, Hz/s; 1 s each
BANK_DB = 27.0  # over E: under the plain long pass's thresholds (28-29 dB / never), over the bank's (21-24)


@functools.lru_cache(maxsize=None)
def bank_run(name: str, bank: bool) -> dict:
    voice, track, mic, base = intruder_base(name)
    extra = sum(chirp(len(mic), base, *c, BANK_DB) for c in BANK_CHIRPS)
    p = params(name)
    p.dechirp_rates = hc.DECHIRP_BANK if bank else ()
    p.short_pass = None
    return hc.analyze(mic + extra, FS, [voice, track], p)


GAPS = [(4, 6), (14, 17), (24, 27), (33, 36)]


@functools.lru_cache(maxsize=None)
def gap_mic(name: str) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Both stems silent in four gaps (one inside the calibration), room noise on, MSG - 6 dB."""
    voice, track = program(40.0, 400)
    gate = gaps_gate(40.0, GAPS)
    voice, track = voice * gate, track * gate
    t = np.arange(len(voice)) / FS
    gain = np.where(t < 10.0, msg_db(name) - 20, msg_db(name) - 6)
    return voice, track, simulate(voice, track, room(name), gain, 400, noise_rms=3e-4, mouth_rt60=room_rt60(name))


@functools.lru_cache(maxsize=None)
def gap_run(name: str, hold: bool = True, short: bool = True, scale: float = 1.0) -> dict:
    voice, track, mic = gap_mic(name)
    return hc.analyze(mic, FS, [voice, track], params(name, hold, short, scale))


@functools.lru_cache(maxsize=None)
def chain_gap_run(name: str, chain_rt60: float | None) -> dict:
    """The gap scenario with a reverb in the chain (RT60 1.5 s, wet 0.3), 20 ms loop delay, no canceller, loop at
    MSG - 6 dB of the loop through chain * F; analysed at the chain output e."""
    voice, track = program(40.0, 400)
    gate = gaps_gate(40.0, GAPS)
    voice, track = voice * gate, track * gate
    t = np.arange(len(voice)) / FS
    lmsg = msg_of(scipy.signal.fftconvolve(chain_reverb(), room(name)))
    gain = np.where(t < 10.0, lmsg - 20, lmsg - 6)
    e = simulate(voice, track, room(name), gain, 400, noise_rms=3e-4, mouth_rt60=room_rt60(name),
                 chain=chain_reverb(), delay=int(CHAIN_DELAY_MS * FS / 1000))
    return hc.analyze(e, FS, [voice, track], params(name, chain_rt60=chain_rt60))


# --------------------------------------------------------------------------
# Tests. Unless a test says otherwise the analysis is the tool's full one:
# both passes, reverberation hold at the room's own RT60 (its measure_rir report).


def short_only(res: dict) -> list[dict]:
    return [e for e in res["events"] if e["passes"] == ["short"]]


class RampFindsTheLimit(unittest.TestCase):
    """1. Ramp 1 dB / 2 s from MSG - 20 dB to MSG + 3 dB."""

    def test_limit_lands_near_msg(self):
        err_msg, err_nyq = [], []
        for name in RAMP_ROOMS:
            res = ramp_run(name)
            self.assertIsNotNone(res["limit_db"], f"{name}: no limit found")
            err_msg.append(res["limit_db"] - msg_db(name))
            err_nyq.append(res["limit_db"] - nyquist_db(name))
            ev = res["events"][0]
            report(f"{name}: MSG {msg_db(name):.3f} dB, Nyquist - MSG {nyquist_db(name) - msg_db(name):+.3f} dB, "
                   f"limit - MSG {err_msg[-1]:+.3f} dB, limit - Nyquist {err_nyq[-1]:+.3f} dB, "
                   f"first event {ev['f_start_hz']:.1f} Hz [{'+'.join(ev['passes'])}], "
                   f"max excess {ev['max_excess_db']:.1f} dB")
        report(f"median limit - MSG {np.median(err_msg):+.3f} dB, median limit - Nyquist {np.median(err_nyq):+.3f} dB")
        # Measured (Intel Mac, numpy 2.2.6, scipy 1.15.3). limit - MSG per room:
        #   synth0 +1.208, synth1 -0.296, studio +0.816, hall +2.123; median +1.012.
        #   Nine-room sweep (adding synth2-4, rehearsal, cabin): -0.296 .. +2.123,
        #   median +1.368. The ramp crosses the loop's Nyquist limit, +0.071 ..
        #   +1.699 dB above the |F|-max MSG; limit - Nyquist per room +0.529, -0.367,
        #   +0.594, +0.779, median +0.561 (nine rooms -0.367 .. +1.117, median
        #   +0.306). Negative values: ringing > 20 dB over the program for
        #   > 500 ms while the loop is still (just) stable.
        for e in err_msg:
            self.assertGreater(e, -1.0)  # measured min -0.296
            self.assertLess(e, 3.0)  # measured max +2.123
        for e in err_nyq:
            self.assertGreater(e, -1.5)  # measured min -0.367
            self.assertLess(e, 1.5)  # measured max +0.779 (nine rooms +1.117)
        self.assertGreater(np.median(err_msg), 0.0)  # measured +1.012
        self.assertLess(np.median(err_msg), 2.2)  # measured +1.012
        self.assertLess(abs(np.median(err_nyq)), 1.0)  # measured +0.561

    def test_hold_and_short_pass_shift(self):
        """The full analysis against the first one (long pass, no hold) on the same recordings."""
        shifts = []
        for name in HOLD_SHIFT_ROOMS:
            full, first = ramp_run(name), ramp_run(name, hold=False, short=False)
            self.assertIn(hc.NO_RT60_WARNING, first["warnings"])
            self.assertEqual(full["warnings"], [])
            shifts.append(full["limit_db"] - first["limit_db"])
            report(f"{name}: limit {full['limit_db']:.3f} dB (hold + short pass) vs {first['limit_db']:.3f} dB "
                   f"(long pass, no hold): {shifts[-1]:+.3f} dB")
        # Measured: synth1 +0.011, hall -0.013 (one 21 ms frame either way).
        # Nine-room sweep: -0.013 .. +0.011. Earlier sweep without the warm-up
        # (seventeen rooms incl. RT60 0.4-1.2 s): -0.170 .. +0.032 dB except one
        # room at -1.184 (ringing qualified sooner without the old +1-frame
        # look-ahead); at 2x RT60 the limits moved a further 0 .. +0.074 dB.
        for d in shifts:
            self.assertLess(abs(d), 0.25)  # measured max |d| 0.013


class HeldNotesDoNotTrigger(unittest.TestCase):
    """2. Held notes and pads, 30 s at a fixed gain below MSG."""

    def check(self, offset_db: float) -> list[int]:
        counts = []
        for name in HELD_ROOMS:
            res = held_run(name, offset_db)
            counts.append(len(res["events"]))
            report(f"{name} MSG{offset_db:+.0f}: events {len(res['events'])} (short pass only: "
                   f"{len(short_only(res))}); longest non-qualifying track "
                   + ", ".join(f"{k} {v['longest_nonqualifying_track_s'] * 1000:.0f} ms" for k, v in res["passes"].items()))
        return counts

    def test_msg_minus_6_zero_events(self):
        # Measured: 0 events in every room; longest non-qualifying tracks long 0,
        # 0, 21 ms, short 0, 0, 16 ms; nine-room sweep 0 events, longest long
        # 0-43, short 0-16 ms.
        for name, n in zip(HELD_ROOMS, self.check(-6.0)):
            self.assertEqual(n, 0, name)
            for v in held_run(name, -6.0)["passes"].values():
                self.assertLess(v["longest_nonqualifying_track_s"], 0.3)  # measured max 21 ms (sweep 43)

    def test_msg_minus_3_reported(self):
        # Measured: 0 events in every room; nine-room sweep 0 events, longest long
        # 0-171 ms, short 0-48 ms. Asserted as the median over rooms.
        self.assertEqual(np.median(self.check(-3.0)), 0)


class BurstDuration(unittest.TestCase):
    """3. A 300 ms tone +30 dB over the envelope is no event; 700 ms is one."""

    def test_bursts(self):
        for name in INTRUDER_ROOMS:
            res = intruder_run(name)
            for dur, t0 in BURSTS:
                hits = [e for e in res["events"]
                        if t0 - 0.2 <= e["onset_s"] <= t0 + dur and abs(e["f_start_hz"] - BURST_F) < 30]
                report(f"{name}: {dur * 1000:.0f} ms burst -> {len(hits)} event(s)"
                       + "".join(f", duration {e['duration_s'] * 1000:.0f} ms, max excess {e['max_excess_db']:.1f} dB "
                                 f"[{'+'.join(e['passes'])}]" for e in hits))
                # Measured: 300 ms -> 0 events; 700 ms -> 1 event of 725 ms (max excess
                # 34.0 / 33.8 dB). The first analysis (long pass, no hold): 725-747 ms.
                self.assertEqual(len(hits), 1 if dur > 0.5 else 0, f"{name} {dur}")
                for e in hits:
                    self.assertGreater(e["duration_s"], 0.6)  # measured 0.725
                    self.assertLess(e["duration_s"], 0.9)  # measured 0.725
            windows = [(t0 - 0.5, t0 + dur) for dur, t0 in BURSTS] + [(c[0] - 0.3, c[0] + 1.0) for c in (CHIRP, CHIRP_SHORT)]
            others = [e for e in res["events"] if not any(a <= e["onset_s"] <= b for a, b in windows)]
            self.assertEqual(others, [], name)


class DriftingComponent(unittest.TestCase):
    """4. Drifting components: the long pass at 500 Hz/s, the short pass at 800 Hz/s."""

    def test_chirp_smear_loss(self):
        # In a 170 ms window a 500 Hz/s chirp smears over 85 Hz, so its peak bin
        # reads below a stationary tone of the same amplitude. Deterministic.
        x = tone(3 * FS, 1.0, 1.0, 6000.0, 500.0, 1.0)
        m = hc.stft_power(x, N_FFT, HOP)
        t = (np.arange(m.shape[0]) * HOP + N_FFT / 2) / FS
        loss = 10 * np.log10(np.median(m[(t > 1.2) & (t < 1.8)].max(axis=1)) / (WINDOW_SUM / 2) ** 2)
        report(f"500 Hz/s peak-bin loss vs stationary: {loss:.2f} dB")
        self.assertAlmostEqual(loss, -5.68, delta=0.1)  # measured -5.68 dB

    def events_near(self, res: dict, c0: float, f0: float) -> list[dict]:
        return [e for e in res["events"] if c0 - 0.3 <= e["onset_s"] <= c0 + 1.0 and e["f_min_hz"] > f0 - 100]

    def test_drift_500_long_pass(self):
        c0, f0, rate = CHIRP
        for name in INTRUDER_ROOMS:
            hits = self.events_near(intruder_run(name), c0, f0)
            report(f"{name}: 500 Hz/s at +35 dB -> {len(hits)} event(s)" + "".join(
                f", {e['f_start_hz']:.1f} -> {e['f_end_hz']:.1f} Hz over {e['duration_s'] * 1000:.0f} ms "
                f"[{'+'.join(e['passes'])}]" for e in hits))
            # Measured: one event per room, found by both passes: 6012.4 -> 6489.9
            # and 6012.5 -> 6489.3 Hz, 1024 ms. At +25 dB neither pass qualifies
            # (scratch sweep, two rooms): long from 28-29 dB, short from 29-30 dB.
            self.assertEqual(len(hits), 1, name)
            ev = hits[0]
            self.assertIn("long", ev["passes"])
            self.assertLess(ev["f_start_hz"], f0 + 60)  # measured max 6012.5
            self.assertGreater(ev["f_end_hz"], f0 + rate - 60)  # measured min 6489.3
            self.assertGreater(ev["duration_s"], 0.9)  # measured 1.024

    def test_drift_800_short_pass(self):
        c0, f0, rate, _ = CHIRP_SHORT
        for name in INTRUDER_ROOMS:
            hits = self.events_near(intruder_run(name), c0, f0)
            report(f"{name}: 800 Hz/s at +35 dB -> {len(hits)} event(s)" + "".join(
                f", {e['f_start_hz']:.1f} -> {e['f_end_hz']:.1f} Hz over {e['duration_s'] * 1000:.0f} ms "
                f"[{'+'.join(e['passes'])}]" for e in hits))
            # Measured: one event per room, short pass only: 5992.1 -> 6796.9 Hz
            # (span 804.7) over 997 ms, 6000.0 -> 6794.1 Hz (span 794.1) over
            # 1003 ms. The long pass does not qualify 800 Hz/s at any
            # level up to 36 dB (it cannot link 2.9 bins/hop); the short pass from 29-31 dB.
            self.assertEqual(len(hits), 1, name)
            ev = hits[0]
            self.assertEqual(ev["passes"], ["short"])
            self.assertGreater(ev["f_max_hz"] - ev["f_min_hz"], 0.6 * rate)  # measured min 794.1
            self.assertGreater(ev["duration_s"], 0.7)  # measured min 0.997


class ProgramGaps(unittest.TestCase):
    """5. Silence in both stems, room noise on, loop at MSG - 6 dB."""

    def test_no_events_in_gaps(self):
        for name in GAP_ROOMS:
            res = gap_run(name)
            report(f"{name}: events {len(res['events'])}; longest non-qualifying track "
                   + ", ".join(f"{k} {v['longest_nonqualifying_track_s'] * 1000:.0f} ms" for k, v in res["passes"].items()))
            # Measured: 0 events; longest long 256, 341, 320 ms, short 11, 16, 299 ms
            # (nine-room sweep: 0 events, long 64-341, short 11-299 ms). The 341
            # and 320 ms are closed-loop ringing, which the room's RT60 does not
            # cover (they stay at 2x RT60).
            self.assertEqual(len(res["events"]), 0, name)

    def test_reverberant_rooms_need_the_hold(self):
        for name in RT_ROOMS:
            res = gap_run(name)
            report(f"{name} (RT60 {room_rt60(name):.3f} s): events {len(res['events'])}; longest non-qualifying "
                   + ", ".join(f"{k} {v['longest_nonqualifying_track_s'] * 1000:.0f} ms" for k, v in res["passes"].items()))
            # Measured with the hold at the room's measured RT60: 0 events; longest
            # long 64 / 43 ms, short 16 / 16 ms. Sweep (RT60 0.4/0.6/0.8/1.2 s, two
            # seeds each): 0 events in all eight, longest long 43-149, short 11-27 ms.
            self.assertEqual(len(res["events"]), 0, name)
        without = gap_run(RT_ROOMS[-1], hold=False, short=False)
        report(f"{RT_ROOMS[-1]} without the hold: {len(without['events'])} events")
        # Measured: 26 (long pass, no hold; outside the calibration window). Sweep,
        # two seeds per RT60: 0.4 s 0/0, 0.6 s 8/5, 0.8 s 25/26, 1.2 s 36/38; with
        # the hold at 0.5x RT60 0/0, 2/1, 9/8, 15/22; at 1x and 2x, 0 everywhere.
        self.assertGreater(len(without["events"]), 10)


class ChainOutput(unittest.TestCase):
    """The analysis point: the chain output e, for dry and cancelled loops, at 5-30 ms loop delay."""

    def check(self, case: str, runs: tuple[tuple[str, int], ...]) -> list[float]:
        errs = []
        for name, d in runs:
            res = ramp_run(name, delay_ms=d, case=case)
            msg, nyq = loop_limits(name, case, d)
            self.assertIsNotNone(res["limit_db"], f"{name} {case} {d} ms: no limit")
            errs.append(res["limit_db"] - nyq)
            ambiguous = [w for w in res["warnings"] if w.startswith("alignment")]
            report(f"{name} {case} {d:2d} ms: Nyquist - MSG {nyq - msg:+.3f} dB, limit - Nyquist {errs[-1]:+.3f} dB"
                   f"{' (alignment warning)' if ambiguous else ''}")
        return errs

    def test_residual_loop_tracks_its_nyquist_limit(self):
        # R = F - F_hat has a textbook MSG 20 dB above F's (the canceller's ASG).
        errs = self.check("residual", RESIDUAL_RUNS)
        # Measured, limit - Nyquist(R): synth0 5/10/20/30 ms -1.010, -0.424, -0.345,
        # -0.859; hall 5 ms -0.351. Nine-room sweep: -1.264 .. +1.515; medians
        # 5 ms -0.338, 10 ms +0.086, 20 ms +0.136, 30 ms -0.016 (eight rooms: in
        # the ninth, synth4, the track, which reaches e only through R, aligned
        # 2 s off at 30 ms - a pad-period rival peak; the tool warned - and the
        # unexplained pad set a limit 20.411 dB low).
        for e in errs:
            self.assertGreater(e, -2.0)  # measured min -1.010 (sweep -1.264)
            self.assertLess(e, 2.0)  # measured max -0.345 (sweep +1.515)
        self.assertLess(abs(np.median(errs)), 1.0)  # measured -0.424

    def test_dry_loop_at_longer_delays(self):
        errs = self.check("dry", DRY_DELAY_RUNS)
        # Measured, limit - Nyquist: hall 10 / 30 ms +0.109, -1.581 (20 ms: +0.194).
        # Nine-room sweep: 5 ms -0.367 .. +1.117 (median +0.306), 10 ms -1.100 ..
        # +0.961 (+0.141), 20 ms -0.636 .. +0.889 (+0.537), 30 ms -1.581 .. +1.216
        # (+0.254). Longer delays did not make the limit systematically later.
        for e in errs:
            self.assertGreater(e, -2.0)  # measured min -1.581
            self.assertLess(e, 2.0)  # measured max +0.109 (sweep +1.216)

    def test_ideal_canceller_mic_false_triggers(self):
        """F_hat = F: the loop never closes, so there is no limit; the mic still hears v (1 + G F)."""
        name = IDEAL_ROOM
        at_e = ramp_run(name, case="ideal")
        at_mic = ramp_run(name, case="ideal", signal="mic")
        first = at_mic["events"][0] if at_mic["events"] else None
        report(f"{name} ideal canceller, ramp to the room's MSG + 30 dB: chain output {len(at_e['events'])} events; "
               f"mic {len(at_mic['events'])} events"
               + (f", the first at the room's MSG {at_mic['limit_db'] - msg_db(name):+.2f} dB ({first['f_start_hz']:.1f} Hz)"
                  if first else ""))
        # Measured: chain output 0 events in all nine rooms. Mic (not asserted): 19
        # events, the first at +25.66 dB (4142.6 Hz); nine rooms 13-34 false events,
        # the first at +21.46 .. +25.68 dB over the room's MSG (both passes).
        self.assertEqual(at_e["events"], [])
        self.assertEqual(at_e["events_in_calibration"], [])


class ReverbInChain(unittest.TestCase):
    """A reverb inside the chain (RT60 1.5 s, wet 0.3): the hold needs --chain-rt60."""

    def test_chain_rt60(self):
        for name in CHAIN_ROOMS:
            room_only, with_chain = chain_gap_run(name, None), chain_gap_run(name, 1.5)
            report(f"{name}: hold at the room's RT60 {room_rt60(name):.3f} s only: {len(room_only['events'])} events "
                   f"(+{len(room_only['events_in_calibration'])} in the calibration window); with --chain-rt60 1.5: "
                   f"{len(with_chain['events'])} events, longest non-qualifying "
                   + ", ".join(f"{k} {v['longest_nonqualifying_track_s'] * 1000:.0f} ms" for k, v in with_chain["passes"].items()))
            # Measured: room only synth0 22 (+5), hall 30 (+8); with --chain-rt60
            # 1.5, 0 events (longest long 64 / 85 ms, short 16 / 27 ms). Nine-room
            # sweep (20 ms delay): room only 15-30 events per 40 s (+4-11 in the
            # calibration window); with --chain-rt60 1.5, 0 in all nine (longest
            # long 64-85 ms, short 16-27 ms). At 5 ms delay: 16-33 (+3-12) and 0.
            self.assertGreater(len(room_only["events"]), 5)  # measured min 15 (20 ms delay)
            self.assertEqual(with_chain["events"], [])
            self.assertEqual(with_chain["events_in_calibration"], [])
            # Events starting inside the calibration window (here: tails in the gap
            # at 4-6 s) are reported apart and never set a limit. Measured 5 and 8.
            self.assertGreater(len(room_only["events_in_calibration"]), 0)
            self.assertTrue(all(e["onset_s"] > CAL[1] for e in room_only["events"]))


class JointAlignment(unittest.TestCase):
    """Stems play in sync: the clearest one anchors the others (+/-0.1 s)."""

    def test_track_behind_a_canceller(self):
        name, d = "synth4", 30  # the run where independent alignment put the track 2 s off
        joint = ramp_run(name, delay_ms=d, case="residual")
        voice, track, e, _, start = ramp_sim(name, d, "residual")
        p = params(name)
        p.joint_align = False
        indep = hc.analyze(e, FS, [voice, track], p)
        r_peak = int(np.argmax(np.abs(residual(name))))
        _, nyq = loop_limits(name, "residual", d)
        al, ai = joint["alignment"], indep["alignment"]
        report(f"{name} residual {d} ms: joint track lag {al[1]['lag_samples']} (R's strongest tap {r_peak}, ratio "
               f"{al[1]['gcc_phat_peak_to_rival']:.2f}, anchor {al[0]['stem']}), limit - Nyquist "
               f"{joint['limit_db'] - nyq:+.3f} dB; independent lag {ai[1]['lag_samples']} (ratio "
               f"{ai[1]['gcc_phat_peak_to_rival']:.2f})")
        # Measured: joint lag 78 = R's tap, ratio 2.49, limit - Nyquist +0.581 dB;
        # independent -96000 (ratio 1.07; its limit was 20.411 dB low). Sweep of
        # the 36 residual runs: independent 1 misaligned and 12 warnings, joint 0
        # and 0 (track ratio 2.49-8.10 inside the window, voice 26.3-43.0).
        self.assertTrue(al[0]["anchor"])
        self.assertEqual(al[1]["lag_samples"], r_peak)
        self.assertGreaterEqual(al[1]["gcc_phat_peak_to_rival"], hc.ALIGN_MIN_RIVAL_RATIO)  # measured 2.49
        self.assertLess(abs(joint["limit_db"] - nyq), 2.0)  # measured +0.581


class DechirpBank(unittest.TestCase):
    """--dechirp: the long pass at 0, +/-250, +/-500, +/-800 Hz/s."""

    def test_grid_loss(self):
        # Peak-bin power of a chirp against a stationary tone, best rate of the
        # bank (deterministic). Measured: worst -1.42 dB at 650 Hz/s (between
        # 500 and 800); -1.07 at 125, -1.09 at 375; -2.10 at 1000, -4.78 at 1200.
        rates = (0.0,) + hc.DECHIRP_BANK
        ref = None
        worst = 0.0
        for r in (0, 125, 375, 650, 800):
            t = np.arange(3 * FS) / FS
            x = np.cos(2 * np.pi * (6000.0 * t + 0.5 * r * t * t))
            best = -np.inf
            for g in rates:
                m = hc.stft_power(x, N_FFT, HOP, rate=g, fs=FS)
                tt = (np.arange(m.shape[0]) * HOP + N_FFT / 2) / FS
                v = float(np.median(m[(tt > 1) & (tt < 2)].max(axis=1)))
                ref = v if ref is None else ref
                best = max(best, 10 * np.log10(v / ref))
            worst = min(worst, best)
        report(f"worst best-of-bank loss over 0-800 Hz/s: {worst:.2f} dB")
        self.assertGreater(worst, -1.6)  # measured -1.42

    def test_bank_finds_what_the_default_misses(self):
        for name in INTRUDER_ROOMS:
            with_bank, default = bank_run(name, True), bank_run(name, False)
            for c0, f0, rate in BANK_CHIRPS:
                near = lambda r: [e for e in r["events"] if c0 - 0.3 <= e["onset_s"] <= c0 + 1.0 and e["f_min_hz"] > f0 - 100]
                hb, hd = near(with_bank), near(default)
                report(f"{name}: {rate:.0f} Hz/s at +{BANK_DB:.0f} dB: bank {len(hb)} event(s)"
                       + "".join(f" ({e['f_start_hz']:.1f} -> {e['f_end_hz']:.1f} Hz, {e['duration_s'] * 1000:.0f} ms, "
                                 f"rates {e['rates_hz_per_s']})" for e in hb) + f"; default {len(hd)}")
                # Measured: one event per chirp and room, found at 250-800 Hz/s (500 Hz/s:
                # 555-576 ms; 800 Hz/s: 747-1003 ms); the default analysis 0 at both
                # rates in both rooms (not asserted). Detection thresholds (dB over E, synth0 /
                # hall), bank: 0 Hz/s 24/24, 100 23/20, 250 25/25, 500 24/24,
                # 800 21/23; plain long pass 24/24, 23/20, 29/27, 29/28, never;
                # short pass 29/25, 30/27, 32/31, 30/29, 31/29.
                self.assertEqual(len(hb), 1, f"{name} {rate}")
                self.assertTrue(any(r_ for r_ in hb[0]["rates_hz_per_s"]))

    def test_bank_adds_no_false_events(self):
        for name in HELD_ROOMS[:2]:
            voice, track, e = held_mic(name, -3.0)
            p = params(name)
            p.dechirp_rates = hc.DECHIRP_BANK
            res = hc.analyze(e, FS, [voice, track], p)
            longest = max(v["longest_nonqualifying_track_s"] for k, v in res["passes"].items() if k.startswith("long@"))
            report(f"{name} MSG-3 with the bank: events {len(res['events'])}, longest dechirped track {longest * 1000:.0f} ms")
            # Measured: 0 events; longest 0 / 64 ms. Nine-room sweep: held MSG-6
            # and MSG-3 0 events (bank tracks 0-149 ms), gaps with the hold 0
            # events (85-320 ms), ramp limits unchanged except synth0 -0.011 dB.
            # Before the dechirped passes shared the plain pass's transfer, gaps
            # gave 5 false events in 3 rooms (pad partials the voice masked in
            # calibration).
            self.assertEqual(res["events"], [])
            self.assertLess(longest, 0.4)  # measured max 64 ms (sweep 320)


class Shifter_(unittest.TestCase):
    """The frequency shifter used in the oracle, and the reference bisection."""

    def test_ssb(self):
        worst = np.inf
        for f in (50, 200, 1000, 5000, 15000, 20000):
            sh = Shifter(5.0)
            x = np.sin(2 * np.pi * f * np.arange(4 * FS) / FS)
            y = np.concatenate([sh(x[i : i + 480]) for i in range(0, len(x), 480)])[FS:]
            spec = np.abs(np.fft.rfft(y * np.hanning(len(y))))
            k = lambda fr: int(round(fr * len(y) / FS))
            up, im = spec[k(f + 5) - 2 : k(f + 5) + 3].max(), spec[k(f - 5) - 2 : k(f - 5) + 3].max()
            gain = 20 * np.log10(up / (len(y) / 4))
            worst = min(worst, 20 * np.log10(up / im))
            self.assertAlmostEqual(gain, 0.0, delta=0.05)  # measured -0.00 dB
        report(f"shifter image suppression, 50 Hz - 20 kHz: >= {worst:.1f} dB")
        self.assertGreater(worst, 40.0)  # measured 44.3 dB (at 15-20 kHz)

    def test_bisection_matches_nyquist_without_shift(self):
        b = bisected_msg("synth0", 10, 0.0, 20.0)
        n = nyquist_db("synth0", 480)
        report(f"synth0 10 ms, no shift: bisected (20 s probe) - Nyquist {b - n:+.3f} dB")
        # Measured: +0.106 (5/10/20/40/80 s probes: +0.487/+0.223/+0.106/+0.047/
        # +0.018 - the finite probe reads high, converging on Nyquist).
        self.assertGreater(b - n, -0.06)  # the bisection's tolerance
        self.assertLess(b - n, 0.3)


@unittest.skipUnless(os.environ.get("HOWL_SLOW"), "set HOWL_SLOW=1 for the shifter-in-the-loop oracle (parallel; ~10 min on 12 cores)")
class ShifterOracle(unittest.TestCase):
    """A frequency shifter in the forward path: criterion limit against the bisected limit of the same loop."""

    def test_probe_length_convergence(self):
        for name in ("synth0", "hall"):
            for sh in (2.0, 5.0):
                vals = [bisected_msg(name, 10, sh, pr) - msg_db(name) for pr in (5.0, 10.0, 20.0, 40.0, 80.0)]
                report(f"{name} 10 ms shift {sh:g} Hz: bisected - MSG over 5/10/20/40/80 s probes: "
                       + ", ".join(f"{v:+.3f}" for v in vals))
                # Measured: synth0 2 Hz +5.234/+5.117/+5.117/+5.117/+5.029, 5 Hz
                # +7.168/+7.080 x4; hall 2 Hz +8.340/+8.340/+8.105/+8.105/+8.076,
                # 5 Hz +10.156/+10.068/+10.010 x3 (the 20 ms runs likewise).
                self.assertLess(abs(vals[-1] - vals[-2]), 0.15)  # measured max 0.088

    def test_criterion_against_bisected_limit(self):
        configs = [(n, d, sh) for n in ALL_ROOMS for d in SHIFT_DELAYS_MS for sh in SHIFTS_HZ]
        import multiprocessing  # independent, deterministic configurations: run them in parallel

        with multiprocessing.get_context("fork").Pool(min(len(configs), os.cpu_count() or 1)) as pool:
            rows = pool.map(_shift_errors, configs)
        errs = {"default": [], "bank": []}
        for (name, d, sh), (ref, e_default, e_bank) in zip(configs, rows):
            errs["default"].append(e_default)
            errs["bank"].append(e_bank)
            report(f"{name} {d} ms {sh:g} Hz: bisected - MSG {ref - msg_db(name):+.3f} dB; limit - bisected "
                   f"{e_default:+.3f} dB, with --dechirp {e_bank:+.3f} dB")
        for k, v in errs.items():
            report(f"{k}: median {np.median(v):+.3f}, range {min(v):+.3f} .. {max(v):+.3f} dB")
        # Measured (36 configurations): default median -1.363, range -4.531 ..
        # -0.317 dB; --dechirp median -1.715, range -4.531 .. -0.339, never later
        # than the default by more than +0.006 dB and earlier in 9 of 36 (by up to
        # 3.179 dB). Per condition, default / bank medians: 2 Hz 10 ms -0.573 /
        # -0.765, 2 Hz 20 ms -1.384 / -1.384, 5 Hz 10 ms -1.512 / -1.693, 5 Hz
        # 20 ms -2.141 / -2.813. The criterion calls the shifted loop BELOW its
        # bisected limit: recirculating program partials, climbing by the shift
        # each pass, stay > 20 dB over the envelope for > 500 ms below runaway.
        for v in errs.values():
            self.assertLess(np.median(v), 0.0)  # measured -1.363 / -1.715
            self.assertGreater(min(v), -5.5)  # measured -4.531
            self.assertLess(max(v), 0.5)  # measured -0.317
        for e_default, e_bank in zip(errs["default"], errs["bank"]):
            self.assertLess(e_bank - e_default, 0.1)  # measured max +0.006


def _shift_errors(cfg: tuple[str, int, float]) -> tuple[float, float, float]:
    name, d, sh = cfg
    ref = bisected_msg(name, d, sh, SHIFT_PROBE_S)
    return ref, shift_run(name, d, sh, False)["limit_db"] - ref, shift_run(name, d, sh, True)["limit_db"] - ref


class Calibration(unittest.TestCase):
    def test_warmup_from_gain_map(self):
        self.assertEqual(hc.warmup_segment(None), (0.0, 30.0))
        self.assertEqual(hc.warmup_segment(hc.ramp_map(-20.0, 0.5, 30.0)), (0.0, 30.0))
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "gain.csv"
            path.write_text("time_s,gain_db\n0,-20\n12.5,-20\n13.5,-19.5\n60,3\n")
            self.assertEqual(hc.warmup_segment(hc.gain_log_map(path)), (0.0, 12.5))
        with self.assertRaises(SystemExit):
            hc.warmup_segment(hc.ramp_map(-20.0, 0.5, 2.0))


class GainMapping(unittest.TestCase):
    """6. --ramp and --gain-log agree; aggregate is median and range."""

    def test_ramp_equals_gain_log(self):
        res = ramp_run(RAMP_ROOMS[0])
        start = msg_db(RAMP_ROOMS[0]) - RAMP_SPAN
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "gain.csv"
            rows = ["time_s,gain_db"] + [f"{t:.3f},{start + RAMP_RATE * t:.6f}" for t in np.arange(0, 60, 0.5)]
            path.write_text("\n".join(rows) + "\n")
            via_log = hc.apply_gain_map(json.loads(json.dumps(res)), hc.gain_log_map(path))
        via_ramp = hc.apply_gain_map(json.loads(json.dumps(res)), hc.ramp_map(start, RAMP_RATE, 0.0))
        report(f"ramp {via_ramp['limit_db']:.6f} dB, gain-log {via_log['limit_db']:.6f} dB")
        self.assertAlmostEqual(via_ramp["limit_db"], via_log["limit_db"], delta=1e-5)  # CSV has 6 decimals

    def test_aggregate(self):
        runs = [ramp_run(n) for n in RAMP_ROOMS[:3]]
        agg = hc.aggregate(runs)
        limits = [r["limit_db"] for r in runs]
        self.assertAlmostEqual(agg["median_db"], float(np.median(limits)), places=12)
        self.assertAlmostEqual(agg["range_db"], max(limits) - min(limits), places=12)
        self.assertEqual(agg["n_with_limit"], 3)
        with_missing = hc.aggregate(runs + [{"limit_db": None}])
        self.assertEqual((with_missing["n_runs"], with_missing["n_with_limit"]), (4, 3))


class NoProgramMode(unittest.TestCase):
    """7. --no-program on held-note material: documents why stems are the default."""

    def test_no_program_false_triggers(self):
        name = HELD_ROOMS[0]
        _, _, mic = held_mic(name, -6.0)
        with_program = held_run(name, -6.0)
        without = hc.analyze(mic[10 * FS : 30 * FS], FS, None, hc.Params(calibrate_s=CAL))  # 20 s at the fixed gain
        in_excerpt = [e for e in with_program["events"] if 10.0 <= e["onset_s"] < 30.0]
        report(f"{name} MSG-6, 20 s of held notes: program path {len(in_excerpt)} events, "
               f"--no-program {len(without['events'])} events")
        # Measured: program path 0; --no-program 137 events in these 20 s
        # (long pass alone, nine-room sweep: 109-137). Asserted only as "fewer".
        self.assertLess(len(in_excerpt), len(without["events"]))


class Rt60Input(unittest.TestCase):
    """--rt60's report reading, fallbacks and per-bin interpolation (deterministic)."""

    def test_report_fallbacks_and_interpolation(self):
        rep = {"per_channel": {
            "0": {"role": "loopback"},
            "2": {"role": "acoustic", "T30_s": None, "T20_s": 0.9, "octave_bands": {}},
            "1": {"role": "acoustic", "T30_s": 0.7, "T20_s": 0.75,
                  "octave_bands": {"125": {"T30": None, "T20": 1.1}, "250": {"T30": 0.8}, "500": {"T30": 0.6},
                                   "1000": {"T30": 0.5}, "2000": {"T30": 0.4}, "4000": {"T30": 0.3},
                                   "8000": {"T30": None}}}}}
        rt = hc.rt60_from_report(rep)
        self.assertEqual(rt["channel"], "1")  # the first acoustic channel
        self.assertEqual(rt["rt60_s"], [0.7, 0.8, 0.6, 0.5, 0.4, 0.3, 0.7])
        self.assertEqual(rt["band_source"][0], "broadband T30")  # octave T30 missing -> broadband T30
        self.assertEqual(hc.rt60_from_report(rep, "2")["rt60_s"], [0.9] * 7)  # -> broadband T20
        f = np.array([50.0, 125.0, 250.0 * 2 ** 0.5, 1000.0, 16000.0])
        np.testing.assert_allclose(hc.rt60_per_bin(f, rt), [0.7, 0.7, 0.7, 0.5, 0.7])
        self.assertEqual(hc.load_rt60("0.8")["rt60_s"], [0.8] * 7)


class Geometry(unittest.TestCase):
    def test_scaled_geometry(self):
        self.assertEqual(hc.Params().geometry(48000), (8192, 1024))
        self.assertEqual(hc.Params().geometry(44100), (7526, 941))  # 5.86 Hz, 21.3 ms
        short = hc.pass_geometries(48000, hc.Params())[1]
        self.assertEqual((short.n_fft, short.hop, short.drift_bins, short.max_dropout_frames, short.maxfilt_frames,
                          short.min_half_width_bins), (2048, 256, 1.0, 4, 4, 6))
        self.assertEqual(len(hc.pass_geometries(48000, hc.Params(short_pass=False))), 1)


class CommandLine(unittest.TestCase):
    """The CLI end to end on WAV files: alignment, JSON, plot, aggregate."""

    def test_cli(self):
        voice, track, mic, _ = intruder_base(INTRUDER_ROOMS[0])
        offset = 12000  # the recording starts 250 ms before the stems
        rec = np.concatenate([1e-4 * np.random.default_rng(9).standard_normal(offset), mic])
        rec = rec + np.concatenate([np.zeros(offset), tone(len(mic), 20.0, 0.7, BURST_F, 0.0, 0.3)])
        script = HERE / "howl_criterion.py"
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            wavfile.write(tmp / "rec.wav", FS, np.stack([rec, 0.5 * rec], axis=1).astype(np.float32))
            wavfile.write(tmp / "voice.wav", FS, voice.astype(np.float32))
            wavfile.write(tmp / "track.wav", FS, (32767 * track / np.max(np.abs(track))).astype(np.int16))
            (tmp / "room_report.json").write_text(json.dumps(rt60_report(INTRUDER_ROOMS[0])))
            t0 = time.time()
            out = subprocess.run(
                [sys.executable, str(script), "analyze", str(tmp / "rec.wav"), "--channel", "0",
                 "--program", str(tmp / "voice.wav"), "--program", str(tmp / "track.wav"),
                 "--ramp", "-30,0.5,10", "--rt60", str(tmp / "room_report.json"),
                 "--json", str(tmp / "run.json"), "--plot", str(tmp / "run.png")],
                capture_output=True, text=True, check=True)
            report(f"CLI analyze {time.time() - t0:.1f} s:\n" + out.stdout)
            run = json.loads((tmp / "run.json").read_text())
            lags = [a["lag_samples"] for a in run["alignment"]]
            track_peak = int(np.argmax(np.abs(room(INTRUDER_ROOMS[0]))))
            # Measured: voice lag 12056 (offset + the mouth path's 56-sample direct
            # tap), track lag 12132 (offset + F's strongest tap, 132 in synth0);
            # one event, the 700 ms tone, 768 ms long.
            self.assertEqual(lags[0], offset + 56)
            self.assertLessEqual(abs(lags[1] - (offset + track_peak)), 2)
            self.assertEqual(len(run["events"]), 1)
            self.assertAlmostEqual(run["limit_db"], -30 + 0.5 * (run["limit_onset_s"] - 10), places=9)
            self.assertEqual(run["parameters"]["calibrate_s"], [0.0, 10.0])  # the ramp's warm-up
            self.assertEqual([st["stem"] for st in run["calibration"]["stems"]],
                             [str(tmp / "voice.wav"), str(tmp / "track.wav")])
            self.assertEqual(run["parameters"]["excess_db"], 20.0)
            self.assertEqual(run["warnings"], [])
            self.assertEqual(run["parameters"]["rt60"]["rt60_s"], hc.rt60_from_report(rt60_report(INTRUDER_ROOMS[0]))["rt60_s"])
            self.assertEqual(sorted(run["passes"]), ["long", "short"])
            self.assertGreater((tmp / "run.png").stat().st_size, 10000)
            for i in range(3):
                (tmp / f"r{i}.json").write_text(json.dumps({"limit_db": [-3.0, 1.0, -1.5][i]}))
            subprocess.run([sys.executable, str(script), "aggregate", *(str(tmp / f"r{i}.json") for i in range(3)),
                            "--json", str(tmp / "agg.json")], capture_output=True, text=True, check=True)
            agg = json.loads((tmp / "agg.json").read_text())
            self.assertEqual((agg["median_db"], agg["min_db"], agg["max_db"], agg["range_db"]), (-1.5, -3.0, 1.0, 4.0))


if __name__ == "__main__":
    unittest.main()
