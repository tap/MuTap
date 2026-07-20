#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
#
# The reciprocal comparison direction: score every subject with WebRTC/
# Microsoft's OWN metric, AECMOS, instead of our metrics — "how does our
# algorithm look under their test?" AECMOS is the neural MOS predictor
# from the ICASSP AEC Challenge; it returns an echo-annoyance MOS and a
# near-end degradation MOS (both 1..5, higher better) from the far-end,
# microphone and enhanced signals.
#
# Each subject is driven through the compiled comparison tool's file mode
# (mutap_aec_compare --wav <subject> far mic enh), so MuTap, Speex and
# WebRTC are scored on identical clips by identical code.
#
# DATA, best first:
#   --real-dir DIR    a directory of <scen>_{far,mic}.wav pairs — the real
#                     AEC-Challenge blind clips (ideal, calibrated).
#   --speech-dir DIR  real speech clips (>= 2 speakers, e.g. CMU ARCTIC);
#                     the script builds far/near from distinct speakers and
#                     an image-source room (pyroomacoustics) for the echo.
#                     Calibrated, since AECMOS is trained on real speech.
#   (neither)         a SYNTHETIC resonator set — absolutes uncalibrated,
#                     RELATIVE ordering only. A last resort.
# The AEC-Challenge blind clips are Git-LFS objects some proxies will not
# authorize; --speech-dir is the calibrated path when they are blocked.
#
# Usage:
#   python3 aecmos_eval.py --tool <path/to/mutap_aec_compare> \
#       --model <path/to/AECMOS Run_..._Stage_0.onnx> \
#       (--real-dir DIR | --speech-dir DIR) \
#       [--subjects mutap,speex,webrtc] [--out results/aecmos.json]
#
# The AECMOS transform/inference below reproduces AEC-Challenge's
# AECMOS/AECMOS_local/aecmos.py (MIT) without its torch dependency
# (torch was used only to allocate the GRU h0 tensor).
import argparse
import json
import os
import subprocess
import sys
import wave

import numpy as np


# --------------------------------------------------------------------------
# AECMOS estimator (slim, torch-free port of the AEC-Challenge reference).
# --------------------------------------------------------------------------
class AECMOS:
    def __init__(self, model_path):
        import onnxruntime as ort

        self.model_path = model_path
        self.max_len = 20
        self.hop_fraction = 0.5
        base = os.path.basename(model_path)
        if "Run_1663915512_Stage_0" in base:
            self.sampling_rate, self.dft_size, self.hidden = 16000, 512, (4, 1, 64)
            self.need_marker = True
        elif "Run_1663829550_Stage_0" in base:
            self.sampling_rate, self.dft_size, self.hidden = 16000, 512, (4, 1, 64)
            self.need_marker = False
        elif "Run_1668423760_Stage_0" in base:
            self.sampling_rate, self.dft_size, self.hidden = 48000, 1536, (4, 1, 96)
            self.need_marker = True
        else:
            raise ValueError("unsupported AECMOS model: " + base)
        self.session = ort.InferenceSession(model_path)
        self.input_name = self.session.get_inputs()[0].name

    def _mel(self, x):
        import librosa

        mel = librosa.feature.melspectrogram(
            y=x, sr=self.sampling_rate, n_fft=self.dft_size + 1,
            hop_length=int(self.hop_fraction * self.dft_size), n_mels=160)
        return ((librosa.power_to_db(mel, ref=np.max) + 40) / 40).T

    def score(self, talk_type, lpb, mic, enh):
        n = min(len(lpb), len(mic), len(enh))
        seg = self.max_len * self.sampling_rate
        lpb, mic, enh = lpb[:n][:seg], mic[:n][:seg], enh[:n][:seg]
        lpb, mic, enh = self._mel(lpb), self._mel(mic), self._mel(enh)
        if self.need_marker:
            ne_st, fe_st = (1, 0) if talk_type == "nst" else (0, 1) if talk_type == "st" else (0, 0)
            mic = np.concatenate((mic, np.ones((20, mic.shape[1])) * (1 - fe_st), np.zeros((20, mic.shape[1]))), 0)
            lpb = np.concatenate((lpb, np.ones((20, lpb.shape[1])) * (1 - ne_st), np.zeros((20, lpb.shape[1]))), 0)
            enh = np.concatenate((enh, np.ones((20, enh.shape[1])), np.zeros((20, enh.shape[1]))), 0)
        feats = np.expand_dims(np.stack((lpb, mic, enh)).astype(np.float32), 0)
        h0 = np.zeros(self.hidden, dtype=np.float32)
        out = self.session.run([], {self.input_name: feats, "h0": h0})[0]
        return float(out[0]), float(out[1])  # echo_mos, degradation_mos


# --------------------------------------------------------------------------
# Signal generation (synthetic fallback) and WAV I/O.
# --------------------------------------------------------------------------
def speechlike(n, fs, seed):
    rng = np.random.default_rng(seed)
    exc = rng.standard_normal(n)
    f0, r = 320.0, 0.95
    a1 = -2.0 * r * np.cos(2 * np.pi * f0 / fs)
    a2 = r * r
    y = np.zeros(n)
    y1 = y2 = 0.0
    for i in range(n):
        y0 = exc[i] - a1 * y1 - a2 * y2
        y2, y1 = y1, y0
        y[i] = y0
    t = np.arange(n) / fs
    env = 0.575 + 0.425 * np.sin(2 * np.pi * 3.3 * t)
    env[np.sin(2 * np.pi * 0.31 * t) > 0.7] *= 0.15
    y *= env
    y *= 0.06 / (np.sqrt(np.mean(y ** 2)) + 1e-12)
    return y.astype(np.float32)


def make_rir(fs, delay_ms, rt60_ms, len_ms, coupling_db, seed):
    rng = np.random.default_rng(seed)
    n = int(fs * len_ms / 1000)
    d0 = int(fs * delay_ms / 1000)
    h = np.zeros(n)
    tau = rt60_ms / 1000 / 6.9078
    t = (np.arange(n) - d0) / fs
    m = np.arange(n) >= d0
    h[m] = rng.standard_normal(n)[m] * np.exp(-t[m] / tau)
    if d0 < n:
        h[d0] += 1.5
    h *= (10 ** (-coupling_db / 20)) / (np.sqrt(np.sum(h ** 2)) + 1e-12)
    return h.astype(np.float32)


def wav_write(path, x, fs):
    x16 = np.clip(x, -1, 1)
    x16 = (x16 * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(x16.tobytes())


def wav_read(path):
    import librosa
    y, sr = librosa.load(path, sr=None, mono=True)
    return y.astype(np.float32), sr


def make_synthetic_set(work, fs=16000, secs=10.0):
    """Three scenarios (st/nst/dt), each a (far.wav, mic.wav) pair with a
    known synthetic echo path. Labelled synthetic — see the module note."""
    os.makedirs(work, exist_ok=True)
    n = int(secs * fs)
    rir = make_rir(fs, 3.0, 60.0, 64.0, 12.0, 7)
    far = speechlike(n, fs, 0xFA00)
    near = speechlike(n, fs, 0xBEEF)
    echo = np.convolve(far, rir)[:n]
    clips = {}
    for scen, (f, m) in {
        "st": (far, echo),                       # far-end single talk
        "nst": (np.zeros(n, np.float32), near),  # near-end single talk
        "dt": (far, echo + near),                # double talk
    }.items():
        fp = os.path.join(work, f"{scen}_far.wav")
        mp = os.path.join(work, f"{scen}_mic.wav")
        wav_write(fp, f, fs)
        wav_write(mp, m, fs)
        clips[scen] = (fp, mp)
    return clips


def _concat_speaker(speech_dir, speaker, want_n, fs):
    """Concatenate a speaker's clips (real speech) to ~want_n samples,
    normalized to -24 dBFS RMS. Tiles if the material is short."""
    import librosa
    files = sorted(f for f in os.listdir(speech_dir) if speaker in f and f.endswith(".wav"))
    if not files:
        raise SystemExit(f"no clips for speaker {speaker} in {speech_dir}")
    segs = []
    total = 0
    while total < want_n:
        for fn in files:
            y, _ = librosa.load(os.path.join(speech_dir, fn), sr=fs, mono=True)
            segs.append(y)
            total += len(y)
            if total >= want_n:
                break
    x = np.concatenate(segs)[:want_n].astype(np.float32)
    x *= 0.06 / (np.sqrt(np.mean(x ** 2)) + 1e-12)
    return x


def _room_rir(fs):
    """A small-room image-source RIR (pyroomacoustics) for the echo path;
    falls back to the synthetic exponential path if pra is unavailable."""
    try:
        import pyroomacoustics as pra
        room = pra.ShoeBox([4.0, 3.0, 2.5], fs=fs, materials=pra.Material(0.3), max_order=12)
        room.add_source([1.0, 1.0, 1.2])                       # loudspeaker
        room.add_microphone([2.4, 2.1, 1.2])                   # mic ~1.8 m away
        room.compute_rir()
        rir = np.asarray(room.rir[0][0], dtype=np.float32)
        return rir[: int(0.2 * fs)], "pyroomacoustics ShoeBox (RT60~0.3 s)"
    except Exception as e:  # pragma: no cover
        return make_rir(fs, 3.0, 120.0, 100.0, 8.0, 7), f"synthetic exp path (pra unavailable: {e})"


def make_realspeech_set(speech_dir, work, fs=16000, secs=10.0, erl_db=6.0):
    """Three scenarios from REAL speech (distinct speakers for far/near)
    through a simulated room. AECMOS is trained on speech, so this yields
    calibrated absolutes — unlike the synthetic resonator set. Not the
    Microsoft blind clips (LFS-blocked here), but real speech through a
    real room, which is what the calibration needs."""
    from scipy.signal import fftconvolve
    os.makedirs(work, exist_ok=True)
    n = int(secs * fs)
    speakers = sorted({fn.rsplit("_a", 1)[0] for fn in os.listdir(speech_dir) if fn.endswith(".wav")})
    if len(speakers) < 2:
        raise SystemExit(f"need >=2 speakers in {speech_dir}, found {speakers}")
    far = _concat_speaker(speech_dir, speakers[0], n, fs)
    near = _concat_speaker(speech_dir, speakers[1], n, fs)
    rir, rir_desc = _room_rir(fs)
    echo = fftconvolve(far, rir)[:n].astype(np.float32)
    # Set the echo coupling to a defined ERL below the far end (the RIR sets
    # the reverberant *shape*; erl_db sets the overall level, as the
    # AEC-Challenge synthetic generator does).
    echo *= (10 ** (-erl_db / 20.0)) * np.sqrt(np.mean(far ** 2)) / (np.sqrt(np.mean(echo ** 2)) + 1e-12)
    print(f"real-speech set: far={speakers[0]}, near={speakers[1]}, echo path = {rir_desc}, ERL {erl_db:.0f} dB")
    clips = {}
    for scen, (f, m) in {
        "st": (far, echo),
        "nst": (np.zeros(n, np.float32), near),
        "dt": (far, echo + near),
    }.items():
        fp = os.path.join(work, f"{scen}_far.wav")
        mp = os.path.join(work, f"{scen}_mic.wav")
        wav_write(fp, f, fs)
        wav_write(mp, m, fs)
        clips[scen] = (fp, mp)
    return clips


def discover_real_set(real_dir):
    """Expect files named <scen>_far.wav / <scen>_mic.wav for scen in
    st/nst/dt (rename the AEC-Challenge farend/mic clips accordingly)."""
    clips = {}
    for scen in ("st", "nst", "dt"):
        fp = os.path.join(real_dir, f"{scen}_far.wav")
        mp = os.path.join(real_dir, f"{scen}_mic.wav")
        if os.path.exists(fp) and os.path.exists(mp):
            clips[scen] = (fp, mp)
    return clips


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True, help="path to mutap_aec_compare")
    ap.add_argument("--model", required=True, help="path to an AECMOS *.onnx model")
    ap.add_argument("--subjects", default="mutap,speex,webrtc")
    ap.add_argument("--real-dir", default=None, help="dir of <scen>_{far,mic}.wav (e.g. AEC-Challenge); highest priority")
    ap.add_argument("--speech-dir", default=None, help="dir of real speech clips (>=2 speakers) -> real-speech eval set")
    ap.add_argument("--work", default="/tmp/mutap_aecmos")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    subjects = [s for s in args.subjects.split(",") if s]
    # Data source, best first: an explicit far/mic corpus (the real
    # AEC-Challenge blind set), else real speech through a simulated room
    # (calibrated), else the synthetic resonator set (relative only).
    if args.real_dir:
        kind, clips = "real-corpus", discover_real_set(args.real_dir)
    elif args.speech_dir:
        kind, clips = "real-speech", make_realspeech_set(args.speech_dir, args.work)
    else:
        kind, clips = "synthetic", make_synthetic_set(args.work)
    if not clips:
        print("no clips found", file=sys.stderr)
        return 2
    calibrated = kind != "synthetic"

    mos = AECMOS(args.model)
    os.makedirs(args.work, exist_ok=True)
    rows = []
    label = {"real-corpus": args.real_dir, "real-speech": "real speech + simulated room (calibrated)",
             "synthetic": "SYNTHETIC eval set — relative only"}[kind]
    # Each backend adds its own processing latency; the tool latency-
    # compensates approximately, but a residual few-ms lag between enh and
    # mic — differing per subject — would confound AECMOS (its mel hop is
    # 16 ms). Remove it: measure each subject's residual lag once on the
    # near-end scenario (where enh ~ mic, so cross-correlation is clean)
    # and shift that subject's enh to zero lag in every scenario.
    def best_lag(a, b, max_ms=40, fs=16000):
        m = min(len(a), len(b))
        a = a[:m] - np.mean(a[:m])
        b = b[:m] - np.mean(b[:m])
        w = int(max_ms * fs / 1000)
        xc = np.correlate(a, b, "full")
        c = len(b) - 1
        seg = xc[c - w: c + w + 1]
        return (np.argmax(seg) - w)  # samples enh leads(+)/lags(-) mic

    print(f"\nAECMOS scores ({label}); model {os.path.basename(args.model)}")
    print(f"  {'subject':<10} {'scenario':<8} {'echoMOS':>8} {'degMOS':>8}   (lag-aligned)")
    for subj in subjects:
        enh_paths, sigs, lag = {}, {}, 0
        ok = True
        for scen, (fp, mp) in clips.items():
            enh = os.path.join(args.work, f"{subj}_{scen}_enh.wav")
            r = subprocess.run([args.tool, "--wav", subj, fp, mp, enh], capture_output=True, text=True)
            if r.returncode != 0:
                print(f"  {subj:<10} {scen:<8}   (skipped: {r.stderr.strip() or 'tool error'})")
                ok = False
                break
            lpb, _ = wav_read(fp)
            mic, _ = wav_read(mp)
            en, _ = wav_read(enh)
            sigs[scen] = (lpb, mic, en)
        if not ok:
            continue
        if "nst" in sigs:
            _, mic, en = sigs["nst"]
            lag = int(best_lag(en, mic))  # enh leads mic by `lag` samples
        for scen, (lpb, mic, en) in sigs.items():
            ea = np.roll(en, -lag) if lag else en  # undo enh's lead/lag -> align to mic
            echo_mos, deg_mos = mos.score(scen, lpb, mic, ea)
            rows.append({"subject": subj, "scenario": scen, "echo_mos": echo_mos, "deg_mos": deg_mos})
            print(f"  {subj:<10} {scen:<8} {echo_mos:8.3f} {deg_mos:8.3f}   (lag {lag:+d})")

    if args.out:
        os.makedirs(os.path.dirname(args.out), exist_ok=True)
        with open(args.out, "w") as f:
            json.dump({"kind": kind, "calibrated": calibrated, "model": os.path.basename(args.model), "rows": rows}, f, indent=2)
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
