#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Measure room impulse responses and round-trip latency with an exponential sine sweep.

Phase 0 of the anti-howl PoC: before a real room is measured, the rig's own
round trip (DAC -> cable -> ADC) must be known, because it is the part of the
feedback path that is not the room. One tool does both, validated on a
synthetic loopback by tools/fixtures/test_measure_rir.py.

  sweep OUT.wav      Write the excitation (float32 WAV) and OUT.json, the
                     sidecar holding every parameter. The deconvolution
                     rebuilds the playback buffer from the sidecar, so the
                     excitation and its inverse can never disagree.
  deconvolve REC.wav --sweep OUT.json
                     Recover one IR per recorded channel, report latency,
                     peak-to-noise, clipping and Schroeder T20/T30, and write
                     each IR as a 32-bit PCM mono WAV that
                     `make_rir_fixtures.py --from-wav` imports unchanged.
  loopback --device NAME --out-ch A --in-ch B --blocksize 64
                     Live round-trip latency over an electrical loopback
                     cable, one fresh PortAudio stream per repeat.
  measure --device NAME --out-ch ... --in-ch ...
                     Live room measurement: sweep, play+record, save the raw
                     recording, deconvolve. `--list-devices` lists devices.

Channel numbers on the command line are 1-based everywhere (the numbering of
interface labels, Max's adc~/dac~ and sounddevice's channel mappings). For
`deconvolve` they name columns of the recording WAV; for `loopback`/`measure`
they name device channels.

How every step is processed, documented here once:

  1. Excitation (Farina 2000). x(t) = A sin(2 pi f1 L (exp(t/L) - 1)),
     L = T / ln(f2/f1), 0 <= t < T, A from --amplitude-db (dBFS peak).
     Half-Hann fade-in (default 50 ms) and fade-out (10 ms). One period is
     pre-silence + sweep + post-silence; --repeats N writes N periods back to
     back; --channels C writes the same period to C output channels. The
     --short preset (1 s sweep, 0.1 s pre, 0.5 s post) is the loopback one.
  2. Alignment. Recording sample 0 is taken to be playback sample 0 (true for
     sounddevice.playrec, and for a Max patch that starts play~ and record~
     on the same bang). Nothing else is aligned: every delay the rig adds
     appears in the IR, which is the point.
  3. Synchronous averaging. With --repeats N the recording is cut into N
     periods which are averaged sample by sample (uncorrelated noise drops
     by 10 log10 N dB); a short recording is zero-padded with a warning.
  4. Deconvolution against the FULL playback period, silences included, so
     IR index 0 is zero delay:  H = R conj(P) / (|P|^2 + eps(f)),  FFT
     length the next power of two >= len(R) + len(P). Kirkeby-style
     regularization: eps(f) = ref (b_in + (b_out - b_in) w(f)), ref the
     median of |P|^2 over [f1, f2], b_in = 1e-6 (-60 dB), b_out = 10
     (+10 dB); w = 0 on [f1, f2], rising to 1 over one third of an octave
     outside each band edge along a raised cosine in log-frequency (w = 1 at
     DC and anywhere past the transitions). eps is real and positive, so the
     weighting |P|^2 / (|P|^2 + eps) is zero-phase and moves no delay.
     Harmonic-distortion products arrive at negative time (the end of the
     circular buffer) and are discarded: the IR is H's first --ir-length
     seconds (default 2.0, clipped to the post-silence).
  5. Latency. Peak = largest |h|. Parabolic interpolation over the three
     samples around it (sign-normalized, so an inverted rig still works)
     refines it to a fraction of a sample. For the loopback channel an
     independent estimate fits the phase slope of H over 1-10 kHz: the IR is
     cut to +/-10 ms around the peak (circularly, so a peak near index 0 is
     not truncated), Tukey(0.5)-tapered, its spectrum de-rotated by the
     integer peak index, and the unwrapped phase fitted as a + b w by
     |H|^2-weighted least squares; latency = peak index - b. The two should
     agree within a fraction of a sample; the group-delay one is the
     electrical round trip used below (the parabolic one if the fit fails).
  6. Acoustic channels (all others, when a loopback channel exists): onset =
     first sample with |h| >= peak - 20 dB; acoustic delay = onset - round
     trip, in ms and in metres at 343 m/s. The `*_acoustic.wav` IR is the IR
     advanced by round(round trip) samples (an integer shift, so the
     waveform is untouched; the fractional remainder is in the report).
  7. Peak-to-noise = 20 log10(|peak| / RMS of the last 10 % of the IR).
     Clipping is flagged if any raw recorded sample has |x| >= 0.999.
  8. Decay (Schroeder 1965), broadband and in octave bands 125-8000 Hz
     (3rd-order Butterworth band-pass, causal). Energy e = h^2; noise power
     Pn = mean of e over the last 10 %; the 10 ms moving-average envelope is
     truncated where it first comes within 5 dB of Pn after its maximum; the
     EDC is the backward integral of (e - Pn) up to that point. T20 fits the
     EDC from -5 to -25 dB, T30 from -5 to -35 dB, by least squares, RT =
     -60 / slope. The range is "available" only if the envelope peak stands
     at least |bottom| + 10 dB above Pn (35 dB for T20, 45 dB for T30, the
     ISO 3382-1 rule that the fit bottom clear the noise by 10 dB);
     otherwise the value is NaN (null in the JSON) — never extrapolated.
     The loopback channel is not a room: no decay figures for it.
  9. Outputs, per channel: STEM_chK_ir.wav (and STEM_chK_ir_acoustic.wav),
     32-bit integer PCM mono, normalized to peak 0.5; `wav_scale` in the
     report is the factor applied (the true IR is wav / wav_scale).
     STEM_report.json holds everything; --plot PNG draws the IR in dB,
     the Schroeder curves and the magnitude responses.

Requires: numpy, scipy (WAV I/O reads 16/24/32-bit int and float32 via
scipy.io.wavfile; octave filters). matplotlib only for --plot; sounddevice
(`python3 -m pip install --user sounddevice`) only for loopback / measure /
--list-devices — both imported lazily.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys

import numpy as np
from scipy.io import wavfile
from scipy import signal

SPEED_OF_SOUND = 343.0  # m/s
CLIP_LEVEL = 0.999
ONSET_DB = 20.0  # acoustic onset: first sample within this of the peak
NOISE_TAIL = 0.10  # fraction of the IR window used as the noise estimate
REG_IN = 1e-6  # Kirkeby eps inside [f1, f2], relative to median in-band |P|^2
REG_OUT = 10.0  # Kirkeby eps outside, same reference
REG_TRANSITION_OCT = 1.0 / 3.0
GD_BAND = (1000.0, 10000.0)  # group-delay fit band, Hz
GD_HALF_WINDOW_S = 0.010
ENVELOPE_S = 0.010
TRUNC_DB = 5.0
OCTAVES = (125, 250, 500, 1000, 2000, 4000, 8000)
SIDECAR_VERSION = 1

SHORT_PRESET = dict(duration=1.0, pre=0.1, post=0.5)


# --------------------------------------------------------------------------- excitation


def sweep_params(
    fs: int = 48000,
    f1: float = 20.0,
    f2: float = 20000.0,
    duration: float = 8.0,
    amplitude_db: float = -6.0,
    fade_in: float = 0.05,
    fade_out: float = 0.01,
    pre: float = 0.5,
    post: float = 3.0,
    repeats: int = 1,
    channels: int = 1,
) -> dict:
    """Every parameter of an excitation, validated; this dict IS the sidecar."""
    if not 0 < f1 < f2 < fs / 2:
        raise ValueError(f"need 0 < f1 < f2 < fs/2 (f1={f1}, f2={f2}, fs={fs})")
    if duration <= 0 or pre < 0 or post <= 0 or repeats < 1 or channels < 1:
        raise ValueError("duration and post must be > 0, pre >= 0, repeats and channels >= 1")
    if fade_in + fade_out > duration:
        raise ValueError("fades longer than the sweep")
    p = dict(
        tool="tools/fixtures/measure_rir.py",
        version=SIDECAR_VERSION,
        fs=int(fs),
        f1=float(f1),
        f2=float(f2),
        duration_s=float(duration),
        amplitude_dbfs=float(amplitude_db),
        fade_in_s=float(fade_in),
        fade_out_s=float(fade_out),
        pre_s=float(pre),
        post_s=float(post),
        repeats=int(repeats),
        channels=int(channels),
    )
    p["sweep_samples"] = int(round(duration * fs))
    p["pre_samples"] = int(round(pre * fs))
    p["post_samples"] = int(round(post * fs))
    p["period_samples"] = p["pre_samples"] + p["sweep_samples"] + p["post_samples"]
    p["total_samples"] = p["period_samples"] * p["repeats"]
    return p


def make_period(p: dict) -> np.ndarray:
    """One playback period (pre-silence + faded sweep + post-silence), float64 mono."""
    fs, n = p["fs"], p["sweep_samples"]
    t = np.arange(n) / fs
    rate = math.log(p["f2"] / p["f1"])
    big_l = p["duration_s"] / rate
    amp = 10.0 ** (p["amplitude_dbfs"] / 20.0)
    x = amp * np.sin(2.0 * np.pi * p["f1"] * big_l * np.expm1(t / big_l))
    n_in = int(round(p["fade_in_s"] * fs))
    n_out = int(round(p["fade_out_s"] * fs))
    if n_in > 0:
        x[:n_in] *= 0.5 * (1.0 - np.cos(np.pi * np.arange(n_in) / n_in))
    if n_out > 0:
        x[n - n_out :] *= 0.5 * (1.0 + np.cos(np.pi * (np.arange(n_out) + 1) / n_out))
    return np.concatenate([np.zeros(p["pre_samples"]), x, np.zeros(p["post_samples"])])


def make_playback(p: dict) -> np.ndarray:
    """The whole excitation file: (total_samples, channels) float32."""
    period = make_period(p)
    mono = np.tile(period, p["repeats"]).astype(np.float32)
    return np.repeat(mono[:, None], p["channels"], axis=1)


def write_sweep(out_wav: pathlib.Path, p: dict) -> pathlib.Path:
    out_wav = pathlib.Path(out_wav)
    out_wav.parent.mkdir(parents=True, exist_ok=True)
    data = make_playback(p)
    wavfile.write(str(out_wav), p["fs"], data if p["channels"] > 1 else data[:, 0])
    sidecar = out_wav.with_suffix(".json")
    side = dict(p, wav=out_wav.name)
    sidecar.write_text(json.dumps(side, indent=2) + "\n")
    return sidecar


def load_sidecar(path: pathlib.Path) -> dict:
    side = json.loads(pathlib.Path(path).read_text())
    keys = ("fs", "f1", "f2", "duration_s", "amplitude_dbfs", "fade_in_s", "fade_out_s",
            "pre_s", "post_s", "repeats", "channels")
    missing = [k for k in keys if k not in side]
    if missing:
        raise SystemExit(f"{path}: not a measure_rir sidecar (missing {missing})")
    p = sweep_params(side["fs"], side["f1"], side["f2"], side["duration_s"],
                     side["amplitude_dbfs"], side["fade_in_s"], side["fade_out_s"],
                     side["pre_s"], side["post_s"], side["repeats"], side["channels"])
    if p["period_samples"] != side.get("period_samples", p["period_samples"]):
        raise SystemExit(f"{path}: period length disagrees with its own parameters")
    return p


# --------------------------------------------------------------------------- WAV I/O


def read_wav(path) -> tuple[int, np.ndarray]:
    """Any 8/16/24/32-bit int or float WAV -> (fs, float64 array (frames, channels))."""
    fs, x = wavfile.read(str(path))
    if x.dtype == np.uint8:
        y = (x.astype(np.float64) - 128.0) / 128.0
    elif x.dtype == np.int16:
        y = x.astype(np.float64) / 32768.0
    elif x.dtype == np.int32:  # 32-bit, and 24-bit (scipy left-justifies it into int32)
        y = x.astype(np.float64) / 2147483648.0
    elif np.issubdtype(x.dtype, np.floating):
        y = x.astype(np.float64)
    else:
        raise SystemExit(f"{path}: unsupported sample type {x.dtype}")
    if y.ndim == 1:
        y = y[:, None]
    return int(fs), y


def write_ir_wav(path, ir: np.ndarray, fs: int) -> float:
    """32-bit integer PCM mono, peak 0.5. Returns the scale applied (true IR = wav / scale)."""
    peak = float(np.max(np.abs(ir)))
    scale = 0.5 / peak if peak > 0 else 1.0
    q = np.round(np.asarray(ir, dtype=np.float64) * scale * 2147483648.0)
    wavfile.write(str(path), int(fs), np.clip(q, -2147483648, 2147483647).astype(np.int32))
    return scale


# --------------------------------------------------------------------------- deconvolution


def regularization(freqs: np.ndarray, f1: float, f2: float, ref: float) -> np.ndarray:
    """Kirkeby eps(f): REG_IN*ref on [f1, f2], REG_OUT*ref outside, raised-cosine in log f."""
    w = np.ones_like(freqs)
    inside = (freqs >= f1) & (freqs <= f2)
    w[inside] = 0.0
    lo = (freqs > 0) & (freqs < f1)
    u = np.log2(f1 / freqs[lo]) / REG_TRANSITION_OCT
    w[lo] = 0.5 * (1.0 - np.cos(np.pi * np.minimum(u, 1.0)))
    hi = freqs > f2
    u = np.log2(freqs[hi] / f2) / REG_TRANSITION_OCT
    w[hi] = 0.5 * (1.0 - np.cos(np.pi * np.minimum(u, 1.0)))
    return ref * (REG_IN + (REG_OUT - REG_IN) * w)


def average_repeats(rec: np.ndarray, p: dict) -> tuple[np.ndarray, list[str]]:
    """(frames, ch) -> one averaged period (period, ch). Warnings for short input."""
    warnings = []
    n, per = p["repeats"], p["period_samples"]
    if n == 1:
        return rec, warnings
    need = n * per
    if rec.shape[0] < need:
        warnings.append(f"recording has {rec.shape[0]} frames, {need} needed for "
                        f"{n} repeats: zero-padded")
        rec = np.pad(rec, ((0, need - rec.shape[0]), (0, 0)))
    return rec[:need].reshape(n, per, rec.shape[1]).mean(axis=0), warnings


def deconvolve_full(rec: np.ndarray, p: dict) -> np.ndarray:
    """Circular regularized deconvolution of every column; returns (nfft, ch) float64."""
    period = make_period(p)
    nfft = 1 << int(math.ceil(math.log2(rec.shape[0] + period.size)))
    pf = np.fft.rfft(period, nfft)
    p2 = np.abs(pf) ** 2
    freqs = np.fft.rfftfreq(nfft, 1.0 / p["fs"])
    band = (freqs >= p["f1"]) & (freqs <= p["f2"])
    eps = regularization(freqs, p["f1"], p["f2"], float(np.median(p2[band])))
    inv = np.conj(pf) / (p2 + eps)
    rf = np.fft.rfft(rec, nfft, axis=0)
    return np.fft.irfft(rf * inv[:, None], nfft, axis=0)


def parabolic_peak(h_full: np.ndarray, n0: int) -> tuple[float, float]:
    """Sub-sample peak position and signed value around integer peak n0 (circular)."""
    n = h_full.size
    s = 1.0 if h_full[n0] >= 0 else -1.0
    a, b, c = (s * h_full[(n0 - 1) % n], s * h_full[n0], s * h_full[(n0 + 1) % n])
    den = a - 2.0 * b + c
    if den >= 0:  # not a maximum; no refinement
        return float(n0), float(h_full[n0])
    d = 0.5 * (a - c) / den
    return n0 + d, s * (b - 0.25 * (a - c) * d)


def group_delay_latency(h_full: np.ndarray, n0: int, fs: int) -> float:
    """Latency (samples) from the |H|^2-weighted phase slope over GD_BAND around the peak."""
    half = int(round(GD_HALF_WINDOW_S * fs))
    idx = np.arange(n0 - half, n0 + half + 1)
    seg = np.take(h_full, idx, mode="wrap") * signal.windows.tukey(idx.size, 0.5)
    if h_full[n0] < 0:
        seg = -seg
    nfft = 1 << int(math.ceil(math.log2(max(8 * idx.size, 4096))))
    spec = np.fft.rfft(seg, nfft)
    w = 2.0 * np.pi * np.arange(spec.size) / nfft  # rad/sample
    spec = spec * np.exp(1j * w * half)  # de-rotate: phase ~ -w * (latency - n0)
    freqs = w * fs / (2.0 * np.pi)
    sel = (freqs >= GD_BAND[0]) & (freqs <= min(GD_BAND[1], 0.45 * fs))
    if np.count_nonzero(sel) < 8:
        return float("nan")
    phi = np.unwrap(np.angle(spec[sel]))
    wt = np.abs(spec[sel]) ** 2
    if not np.all(np.isfinite(phi)) or wt.sum() <= 0:
        return float("nan")
    a = np.vstack([np.ones(phi.size), w[sel]]).T
    sw = np.sqrt(wt)
    coef, *_ = np.linalg.lstsq(a * sw[:, None], phi * sw, rcond=None)
    return float(n0 - coef[1])


# --------------------------------------------------------------------------- decay


def schroeder(ir: np.ndarray, fs: int) -> dict:
    """T20/T30 by noise-compensated, truncated backward integration (see docstring step 8)."""
    e = np.asarray(ir, dtype=np.float64) ** 2
    n = e.size
    pn = float(np.mean(e[int(n * (1.0 - NOISE_TAIL)) :]))
    win = max(1, int(round(ENVELOPE_S * fs)))
    env = np.convolve(e, np.ones(win) / win, mode="same")
    tiny = 1e-30
    imax = int(np.argmax(env))
    floor = max(pn, tiny)
    range_db = 10.0 * math.log10(max(env[imax], tiny) / floor)
    below = np.nonzero(env[imax:] <= floor * 10.0 ** (TRUNC_DB / 10.0))[0]
    trunc = imax + int(below[0]) if below.size else n
    edc = np.cumsum((e[:trunc] - pn)[::-1])[::-1]
    edc = np.maximum(edc, tiny)
    edc_db = 10.0 * np.log10(edc / edc.max())
    t = np.arange(trunc) / fs
    out = dict(range_db=range_db, truncation_s=trunc / fs, edc_db=edc_db)
    for name, bottom in (("T20", -25.0), ("T30", -35.0)):
        out[name] = float("nan")
        if range_db < -bottom + 10.0:
            continue
        start = np.nonzero(edc_db <= -5.0)[0]
        stop = np.nonzero(edc_db <= bottom)[0]
        if start.size == 0 or stop.size == 0 or stop[0] - start[0] < 8:
            continue
        sl = slice(int(start[0]), int(stop[0]) + 1)
        slope = np.polyfit(t[sl], edc_db[sl], 1)[0]
        if slope < 0:
            out[name] = float(-60.0 / slope)
    return out


def octave_decay(ir: np.ndarray, fs: int) -> dict:
    bands = {}
    for fc in OCTAVES:
        hi = fc * math.sqrt(2.0)
        if hi >= 0.5 * fs:
            bands[str(fc)] = dict(T20=float("nan"), T30=float("nan"), range_db=float("nan"))
            continue
        sos = signal.butter(3, [fc / math.sqrt(2.0), hi], btype="bandpass", fs=fs, output="sos")
        r = schroeder(signal.sosfilt(sos, ir), fs)
        bands[str(fc)] = dict(T20=r["T20"], T30=r["T30"], range_db=r["range_db"])
    return bands


# --------------------------------------------------------------------------- analysis


def analyze(recording: np.ndarray, fs: int, p: dict, ir_length: float = 2.0,
            channels: list[int] | None = None, loopback_channel: int | None = None) -> dict:
    """Deconvolve and measure. Channels are 1-based recording columns.

    Returns dict(report=..., irs={ch: ir}, acoustic={ch: ir}, full={ch: circular IR}).
    """
    if fs != p["fs"]:
        raise SystemExit(f"recording is {fs} Hz, sweep is {p['fs']} Hz")
    rec = np.asarray(recording, dtype=np.float64)
    if rec.ndim == 1:
        rec = rec[:, None]
    nch = rec.shape[1]
    chans = list(channels) if channels else list(range(1, nch + 1))
    if loopback_channel is not None and loopback_channel not in chans:
        chans.append(loopback_channel)
    bad = [c for c in chans if not 1 <= c <= nch]
    if bad:
        raise SystemExit(f"channels {bad} out of range: recording has {nch}")
    warnings = []
    ir_len_s = min(float(ir_length), p["post_s"])
    if ir_len_s < ir_length:
        warnings.append(f"--ir-length {ir_length} s clipped to the post-silence {ir_len_s} s")
    ir_n = int(round(ir_len_s * fs))
    clipped = {c: bool(np.any(np.abs(rec[:, c - 1]) >= CLIP_LEVEL)) for c in chans}
    avg, w = average_repeats(rec[:, [c - 1 for c in chans]], p)
    warnings += w
    if avg.shape[0] < p["period_samples"]:
        warnings.append(f"recording ({avg.shape[0]} frames) shorter than one period "
                        f"({p['period_samples']}): zero-padded")
        avg = np.pad(avg, ((0, p["period_samples"] - avg.shape[0]), (0, 0)))
    full = deconvolve_full(avg, p)
    res = dict(irs={}, acoustic={}, full={})
    per = {}
    for j, c in enumerate(chans):
        hf = full[:, j]
        h = hf[:ir_n]
        n0 = int(np.argmax(np.abs(h)))
        pos, val = parabolic_peak(hf, n0)
        noise = float(np.sqrt(np.mean(h[int(ir_n * (1.0 - NOISE_TAIL)) :] ** 2)))
        rep = dict(
            role="loopback" if c == loopback_channel else "acoustic",
            peak_index=n0,
            peak_value=float(hf[n0]),
            peak_interp_samples=pos,
            peak_interp_ms=1000.0 * pos / fs,
            peak_interp_value=val,
            peak_to_noise_db=20.0 * math.log10(abs(hf[n0]) / noise) if noise > 0 else float("inf"),
            clipped=clipped[c],
        )
        if c == loopback_channel:
            gd = group_delay_latency(hf, n0, fs)
            rep.update(group_delay_samples=gd, group_delay_ms=1000.0 * gd / fs,
                       latency_disagreement_samples=gd - pos)
        else:
            dec = schroeder(h, fs)
            rep.update(T20_s=dec["T20"], T30_s=dec["T30"], decay_range_db=dec["range_db"],
                       octave_bands=octave_decay(h, fs))
        per[c] = rep
        res["irs"][c] = h
        res["full"][c] = hf
        if clipped[c]:
            warnings.append(f"channel {c}: recording reaches |x| >= {CLIP_LEVEL} (clipping)")
    round_trip = None
    if loopback_channel is not None:
        lb = per[loopback_channel]
        gd = lb["group_delay_samples"]
        round_trip = gd if math.isfinite(gd) else lb["peak_interp_samples"]
        for c in chans:
            if c == loopback_channel:
                continue
            rep = per[c]
            h = res["irs"][c]
            onset = int(np.argmax(np.abs(h) >= abs(rep["peak_value"]) * 10.0 ** (-ONSET_DB / 20.0)))
            shift = int(round(round_trip))
            ac_s = (onset - round_trip) / fs
            rep.update(onset_index=onset, acoustic_delay_ms=1000.0 * ac_s,
                       acoustic_distance_m=ac_s * SPEED_OF_SOUND, acoustic_shift_samples=shift,
                       acoustic_shift_remainder_samples=round_trip - shift)
            res["acoustic"][c] = np.take(res["full"][c], np.arange(shift, shift + ir_n),
                                         mode="wrap")
    report = dict(
        fs=fs,
        sweep=p,
        ir_length_s=ir_len_s,
        channels=chans,
        loopback_channel=loopback_channel,
        round_trip_samples=round_trip,
        round_trip_ms=None if round_trip is None else 1000.0 * round_trip / fs,
        regularization=dict(in_band=REG_IN, out_of_band=REG_OUT,
                            transition_octaves=REG_TRANSITION_OCT,
                            reference="median |P|^2 over [f1, f2]"),
        warnings=warnings,
        per_channel={str(c): per[c] for c in chans},
    )
    res["report"] = report
    return res


def _jsonable(x):
    if isinstance(x, dict):
        return {str(k): _jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [_jsonable(v) for v in x]
    if isinstance(x, (np.floating, float)):
        return float(x) if math.isfinite(x) else None
    if isinstance(x, np.integer):
        return int(x)
    if isinstance(x, np.bool_):
        return bool(x)
    return x


def summarize(report: dict) -> str:
    fs = report["fs"]
    lines = [f"fs {fs} Hz, IR window {report['ir_length_s']} s"]
    if report["round_trip_samples"] is not None:
        lines.append(f"electrical round trip: {report['round_trip_samples']:.3f} samples "
                     f"({report['round_trip_ms']:.4f} ms), "
                     f"from channel {report['loopback_channel']}")
    for c, r in report["per_channel"].items():
        s = (f"ch{c} [{r['role']}] peak @{r['peak_index']} ({r['peak_value']:+.4g}), "
             f"interp {r['peak_interp_samples']:.3f} samples ({r['peak_interp_ms']:.4f} ms), "
             f"PNR {r['peak_to_noise_db']:.1f} dB")
        if r["role"] == "loopback":
            s += (f", group delay {r['group_delay_samples']:.3f} samples "
                  f"({r['group_delay_ms']:.4f} ms), disagreement "
                  f"{r['latency_disagreement_samples']:+.3f} samples")
        else:
            s += f", T20 {r['T20_s']:.3f} s, T30 {r['T30_s']:.3f} s"
            if "acoustic_delay_ms" in r:
                s += (f", acoustic {r['acoustic_delay_ms']:.3f} ms = "
                      f"{r['acoustic_distance_m']:.3f} m")
        if r["clipped"]:
            s += "  ** CLIPPED **"
        lines.append(s)
    for w in report["warnings"]:
        lines.append(f"warning: {w}")
    return "\n".join(lines)


def plot(res: dict, fs: int, path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(3, 1, figsize=(10, 11))
    for c, h in res["irs"].items():
        t = 1000.0 * np.arange(h.size) / fs
        peak = np.max(np.abs(h))
        ax[0].plot(t, 20 * np.log10(np.abs(h) / peak + 1e-12), lw=0.5, label=f"ch{c}")
        r = res["report"]["per_channel"][str(c)]
        if r["role"] != "loopback":
            edc = schroeder(h, fs)["edc_db"]
            ax[1].plot(1000.0 * np.arange(edc.size) / fs, edc, label=f"ch{c}")
        spec = np.fft.rfft(h)
        f = np.fft.rfftfreq(h.size, 1.0 / fs)
        ax[2].semilogx(f[1:], 20 * np.log10(np.abs(spec[1:]) + 1e-12), lw=0.5, label=f"ch{c}")
    ax[0].set(xlabel="ms", ylabel="dB re peak", title="impulse response", ylim=(-120, 5))
    ax[1].set(xlabel="ms", ylabel="dB", title="Schroeder decay (noise-compensated)", ylim=(-70, 5))
    ax[2].set(xlabel="Hz", ylabel="dB", title="magnitude response", xlim=(10, fs / 2))
    for a in ax:
        a.grid(True, alpha=0.3)
        a.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(str(path), dpi=110)
    plt.close(fig)


def write_outputs(res: dict, fs: int, out_dir: pathlib.Path, stem: str) -> pathlib.Path:
    out_dir = pathlib.Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    report = res["report"]
    for c, h in res["irs"].items():
        r = report["per_channel"][str(c)]
        path = out_dir / f"{stem}_ch{c}_ir.wav"
        r["wav"] = path.name
        r["wav_scale"] = write_ir_wav(path, h, fs)
        if c in res["acoustic"]:
            apath = out_dir / f"{stem}_ch{c}_ir_acoustic.wav"
            r["acoustic_wav"] = apath.name
            r["acoustic_wav_scale"] = write_ir_wav(apath, res["acoustic"][c], fs)
    jpath = out_dir / f"{stem}_report.json"
    jpath.write_text(json.dumps(_jsonable(report), indent=2) + "\n")
    return jpath


def run_deconvolve(rec_path, sidecar, out_dir=None, stem=None, ir_length=2.0, channels=None,
                   loopback_channel=None, plot_path=None) -> dict:
    p = load_sidecar(sidecar)
    fs, rec = read_wav(rec_path)
    res = analyze(rec, fs, p, ir_length, channels, loopback_channel)
    rec_path = pathlib.Path(rec_path)
    jpath = write_outputs(res, fs, out_dir or rec_path.parent, stem or rec_path.stem)
    print(summarize(res["report"]))
    print(f"wrote {jpath}")
    if plot_path:
        plot(res, fs, plot_path)
        print(f"wrote {plot_path}")
    return res


# --------------------------------------------------------------------------- live (sounddevice)


def _sounddevice():
    try:
        import sounddevice as sd
    except ImportError:  # pragma: no cover - depends on the machine
        raise SystemExit("the live subcommands need sounddevice: "
                         "python3 -m pip install --user sounddevice")
    return sd


def find_device(sd, name: str, kind: str, need: int) -> int:
    key = "max_input_channels" if kind == "input" else "max_output_channels"
    hits = [(i, d) for i, d in enumerate(sd.query_devices()) if name.lower() in d["name"].lower()]
    ok = [i for i, d in hits if d[key] >= need]
    if not ok:
        found = ", ".join(f"{i}: {d['name']} ({d[key]} {kind})" for i, d in hits) or "none"
        raise SystemExit(f"no {kind} device matching '{name}' with >= {need} channels "
                         f"(matches: {found}); see --list-devices")
    return ok[0]


def list_devices() -> None:
    print(_sounddevice().query_devices())


def _open_devices(sd, args, need_in: int, need_out: int) -> tuple[int, int]:
    di = find_device(sd, args.device, "input", need_in)
    do = find_device(sd, args.out_device or args.device, "output", need_out)
    return di, do


def _playrec(sd, play: np.ndarray, fs: int, di: int, do: int, in_ch, out_ch, blocksize):
    kwargs = dict(samplerate=fs, input_mapping=list(in_ch), output_mapping=list(out_ch),
                  device=(di, do), latency="low", dtype="float32", blocking=True)
    if blocksize:
        kwargs["blocksize"] = int(blocksize)
    rec = sd.playrec(play, **kwargs)
    lat = sd.get_stream().latency  # (input, output) seconds, as PortAudio reports them
    return np.asarray(rec, dtype=np.float64), (float(lat[0]), float(lat[1]))


def cmd_loopback(args) -> None:
    sd = _sounddevice()
    di, do = _open_devices(sd, args, args.in_ch, args.out_ch)
    p = sweep_params(fs=args.fs, amplitude_db=args.amplitude_db, **SHORT_PRESET)
    play = make_playback(p)
    runs = []
    for k in range(args.repeats):
        rec, lat = _playrec(sd, play, p["fs"], di, do, [args.in_ch], [args.out_ch], args.blocksize)
        res = analyze(rec, p["fs"], p, ir_length=p["post_s"], loopback_channel=1)
        r = res["report"]["per_channel"]["1"]
        runs.append(dict(repeat=k, peak_interp_samples=r["peak_interp_samples"],
                         group_delay_samples=r["group_delay_samples"],
                         peak_to_noise_db=r["peak_to_noise_db"], clipped=r["clipped"],
                         portaudio_latency_s=dict(input=lat[0], output=lat[1]),
                         portaudio_latency_samples=(lat[0] + lat[1]) * p["fs"]))
        print(f"repeat {k}: {r['group_delay_samples']:.3f} samples (group delay), "
              f"{r['peak_interp_samples']:.3f} (peak), PortAudio reports "
              f"{(lat[0] + lat[1]) * p['fs']:.1f}, PNR {r['peak_to_noise_db']:.1f} dB")

    def stats(key):
        v = np.array([r[key] for r in runs], dtype=np.float64)
        return dict(min=float(np.min(v)), median=float(np.median(v)), max=float(np.max(v)),
                    min_ms=1000.0 * float(np.min(v)) / p["fs"],
                    median_ms=1000.0 * float(np.median(v)) / p["fs"],
                    max_ms=1000.0 * float(np.max(v)) / p["fs"])

    dev_in, dev_out = sd.query_devices(di), sd.query_devices(do)
    result = dict(
        input_device=dev_in["name"], output_device=dev_out["name"], fs=p["fs"],
        blocksize=args.blocksize, in_ch=args.in_ch, out_ch=args.out_ch, sweep=p,
        group_delay_samples=stats("group_delay_samples"),
        peak_interp_samples=stats("peak_interp_samples"),
        portaudio_reported_samples=stats("portaudio_latency_samples"),
        portaudio_device_defaults=dict(
            low_input_latency_s=dev_in["default_low_input_latency"],
            low_output_latency_s=dev_out["default_low_output_latency"]),
        repeats=runs,
    )
    out = pathlib.Path(args.out or f"loopback_bs{args.blocksize}.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(_jsonable(result), indent=2) + "\n")
    g = result["group_delay_samples"]
    print(f"round trip over {args.repeats} fresh streams: min {g['min']:.3f} / median "
          f"{g['median']:.3f} / max {g['max']:.3f} samples ({g['min_ms']:.4f} / "
          f"{g['median_ms']:.4f} / {g['max_ms']:.4f} ms); PortAudio reports median "
          f"{result['portaudio_reported_samples']['median']:.1f} samples")
    print(f"wrote {out}")


def cmd_measure(args) -> None:
    sd = _sounddevice()
    if args.loopback_channel is not None and args.loopback_channel not in args.in_ch:
        raise SystemExit("--loopback-channel must be one of --in-ch (a device input channel)")
    di, do = _open_devices(sd, args, max(args.in_ch), max(args.out_ch))
    p = _params_from_args(args, channels=len(args.out_ch))
    out_dir = pathlib.Path(args.out_dir)
    stem = args.stem
    write_sweep(out_dir / f"{stem}_sweep.wav", p)
    rec, lat = _playrec(sd, make_playback(p), p["fs"], di, do, args.in_ch, args.out_ch,
                        args.blocksize)
    rec_path = out_dir / f"{stem}_recording.wav"
    wavfile.write(str(rec_path), p["fs"], rec.astype(np.float32))
    print(f"wrote {rec_path} (PortAudio reports input {lat[0] * 1000:.2f} ms, "
          f"output {lat[1] * 1000:.2f} ms)")
    lb = None if args.loopback_channel is None else args.in_ch.index(args.loopback_channel) + 1
    run_deconvolve(rec_path, out_dir / f"{stem}_sweep.json", out_dir, stem, args.ir_length,
                   None, lb, args.plot)


# --------------------------------------------------------------------------- CLI


def _add_sweep_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--fs", type=int, default=48000)
    ap.add_argument("--f1", type=float, default=20.0)
    ap.add_argument("--f2", type=float, default=20000.0)
    ap.add_argument("--duration", type=float, default=8.0, help="sweep length, s")
    ap.add_argument("--amplitude-db", type=float, default=-6.0, help="peak, dBFS")
    ap.add_argument("--fade-in", type=float, default=0.05, help="half-Hann, s")
    ap.add_argument("--fade-out", type=float, default=0.01, help="half-Hann, s")
    ap.add_argument("--pre", type=float, default=0.5, help="pre-silence, s")
    ap.add_argument("--post", type=float, default=3.0, help="post-silence, s")
    ap.add_argument("--repeats", type=int, default=1, help="back-to-back periods to average")
    ap.add_argument("--short", action="store_true",
                    help="loopback preset: 1 s sweep, 0.1 s pre, 0.5 s post")


def _params_from_args(args, channels: int) -> dict:
    kw = dict(duration=args.duration, pre=args.pre, post=args.post)
    if args.short:
        kw.update(SHORT_PRESET)
    return sweep_params(fs=args.fs, f1=args.f1, f2=args.f2, amplitude_db=args.amplitude_db,
                        fade_in=args.fade_in, fade_out=args.fade_out, repeats=args.repeats,
                        channels=channels, **kw)


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list-devices", action="store_true", help="list audio devices and exit")
    sub = ap.add_subparsers(dest="cmd")

    s = sub.add_parser("sweep", help="write the excitation WAV + JSON sidecar")
    s.add_argument("out", help="output WAV (the sidecar is the same path with .json)")
    _add_sweep_args(s)
    s.add_argument("--channels", type=int, default=1, help="output channels carrying the sweep")

    d = sub.add_parser("deconvolve", help="recover IRs from a recording")
    d.add_argument("recording")
    d.add_argument("--sweep", required=True, help="the sweep's JSON sidecar")
    d.add_argument("--ir-length", type=float, default=2.0, help="s, clipped to the post-silence")
    d.add_argument("--channels", type=int, nargs="+", default=None,
                   help="1-based recording columns (default all)")
    d.add_argument("--loopback-channel", type=int, default=None,
                   help="1-based recording column that is an electrical loopback")
    d.add_argument("--out-dir", default=None, help="default: next to the recording")
    d.add_argument("--stem", default=None, help="default: the recording's stem")
    d.add_argument("--plot", default=None, metavar="PNG")

    lb = sub.add_parser("loopback", help="live round-trip latency over a loopback cable")
    lb.add_argument("--device", required=True, help="case-insensitive name substring")
    lb.add_argument("--out-device", default=None, help="output device if not --device")
    lb.add_argument("--out-ch", type=int, required=True, help="1-based device output channel")
    lb.add_argument("--in-ch", type=int, required=True, help="1-based device input channel")
    lb.add_argument("--blocksize", type=int, default=64)
    lb.add_argument("--repeats", type=int, default=10, help="fresh streams to open")
    lb.add_argument("--fs", type=int, default=48000)
    lb.add_argument("--amplitude-db", type=float, default=-6.0)
    lb.add_argument("--out", default=None, help="result JSON (default loopback_bsN.json)")

    m = sub.add_parser("measure", help="live room measurement")
    m.add_argument("--device", required=True, help="case-insensitive name substring")
    m.add_argument("--out-device", default=None, help="output device if not --device")
    m.add_argument("--out-ch", type=int, nargs="+", required=True, help="1-based output channels")
    m.add_argument("--in-ch", type=int, nargs="+", required=True, help="1-based input channels")
    m.add_argument("--loopback-channel", type=int, default=None,
                   help="the device input channel (one of --in-ch) that is a loopback cable")
    m.add_argument("--blocksize", type=int, default=0, help="0 = PortAudio's choice")
    m.add_argument("--ir-length", type=float, default=2.0)
    m.add_argument("--out-dir", default=".")
    m.add_argument("--stem", default="room")
    m.add_argument("--plot", default=None, metavar="PNG")
    _add_sweep_args(m)

    args = ap.parse_args(argv)
    if args.list_devices:
        list_devices()
        return
    if args.cmd == "sweep":
        p = _params_from_args(args, channels=args.channels)
        side = write_sweep(pathlib.Path(args.out), p)
        print(f"wrote {args.out} ({p['total_samples']} frames x {p['channels']} ch, "
              f"{p['total_samples'] / p['fs']:.2f} s) and {side}")
    elif args.cmd == "deconvolve":
        run_deconvolve(args.recording, args.sweep, args.out_dir, args.stem, args.ir_length,
                       args.channels, args.loopback_channel, args.plot)
    elif args.cmd == "loopback":
        cmd_loopback(args)
    elif args.cmd == "measure":
        cmd_measure(args)
    else:
        ap.print_help()
        sys.exit(2)


if __name__ == "__main__":
    main()
