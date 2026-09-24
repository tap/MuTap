#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The oracle for tools/fixtures/measure_rir.py: synthetic rigs with known answers, no audio device.

    python3 -m unittest tools/fixtures/test_measure_rir.py -v        # from the repo root

1. Synthetic loopback: the playback through a known fractional delay (FFT phase ramp) cascaded
   with a 63-tap linear-phase low-pass FIR (group delay 31 samples, the DAC+ADC stand-in), plus
   noise at -80 dBFS -> both latency estimates recover the true total delay.
2. Synthetic room: an exponentially decaying noise tail behind a direct-path spike, RT60 0.4 s and
   1.2 s, behind the same electrical path -> in-band IR error, T20/T30, octave bands, acoustic
   delay, and the round-trip-removed IR.
3. Nonlinearity: tanh soft clip on the recording (and, for contrast, on the loudspeaker feed) ->
   the causal IR stays accurate and the distortion products land at negative time.
4. Synchronous averaging over 4 repeats raises peak-to-noise by ~10 log10(4) dB.
5. Decay figures are NaN (null) when the range is not available, never extrapolated.
6. WAV I/O: 16/24/32-bit int and float32 recordings read correctly; the IR WAV is 32-bit PCM mono
   that `make_rir_fixtures.py --from-wav` imports unchanged.
7. The CLI end to end on files (sweep -> synthesized recording -> deconvolve) via subprocess.

Every threshold below was measured first; the measured value sits in a comment beside it.
Set MEASURE_RIR_VERBOSE=1 to print the measured numbers.
"""

import contextlib
import importlib.util
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
import wave

import numpy as np
from scipy import signal
from scipy.io import wavfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import measure_rir as m  # noqa: E402

FS = 48000
FIR = signal.firwin(63, 20000, fs=FS)  # linear phase, group delay 31 samples
FIR_DELAY = 31.0
ELECTRICAL = 317.4  # samples of fractional delay ahead of the FIR
DIRECT = 240  # acoustic direct-path delay, samples (5 ms)


def note(msg: str) -> None:
    if os.environ.get("MEASURE_RIR_VERBOSE"):
        print(msg, file=sys.stderr)


def frac_delay(x: np.ndarray, d: float, extra: int) -> np.ndarray:
    """Delay by d samples (any real) with an FFT phase ramp; output len(x) + extra."""
    n = x.size + extra
    nfft = 1 << int(math.ceil(math.log2(n)))
    spec = np.fft.rfft(x, nfft)
    spec *= np.exp(-2j * np.pi * np.arange(spec.size) * d / nfft)
    spec[-1] = spec[-1].real
    return np.fft.irfft(spec, nfft)[:n]


def room_ir(rt60: float, seed: int, n: int) -> np.ndarray:
    """Direct spike at DIRECT, then Gaussian noise whose amplitude falls 60 dB in rt60."""
    rng = np.random.default_rng(1000 + seed)
    h = np.zeros(n)
    h[DIRECT] = 1.0
    k = np.arange(n - DIRECT - 1)
    h[DIRECT + 1 :] = 0.05 * rng.standard_normal(k.size) * 10.0 ** (-3.0 * k / (rt60 * FS))
    return h


def electrical(play: np.ndarray, d: float = ELECTRICAL) -> np.ndarray:
    return signal.fftconvolve(frac_delay(play, d, 2000), FIR)[: play.size]


def loopback_recording(p: dict, d: float, seed: int, noise_db: float = -80.0) -> np.ndarray:
    play = np.tile(m.make_period(p), p["repeats"])
    rng = np.random.default_rng(seed)
    return electrical(play, d) + rng.standard_normal(play.size) * 10.0 ** (noise_db / 20.0)


def room_recording(p: dict, rt60: float, seed: int, noise_db: float = -80.0,
                   drive: float | None = None, where: str = "rec",
                   ir_seconds: float = 2.0) -> tuple[np.ndarray, np.ndarray]:
    """Two columns: [electrical loopback, room]. Room channel scaled to peak 0.5 before any
    nonlinearity. Returns (recording, true full IR of the room channel incl. the rig)."""
    play = np.tile(m.make_period(p), p["repeats"])
    elec = electrical(play)
    ir = room_ir(rt60, seed, int(ir_seconds * FS))
    y2 = signal.fftconvolve(elec, ir)[: play.size]
    k = 0.5 / np.max(np.abs(y2))
    ir *= k
    y2 *= k
    if drive is not None and where == "spk":  # memoryless loudspeaker soft clip, then the room
        y2 = signal.fftconvolve(electrical(np.tanh(drive * play) / drive), ir)[: play.size]
    rng = np.random.default_rng(seed)
    y = np.stack([elec, y2], 1) + rng.standard_normal((play.size, 2)) * 10.0 ** (noise_db / 20.0)
    if drive is not None and where == "rec":  # soft clip on the recording (preamp/ADC)
        y[:, 1] = np.tanh(drive * y[:, 1]) / drive
    rig = signal.fftconvolve(frac_delay(np.r_[1.0], ELECTRICAL, 4096)[:4096], FIR)
    return y, signal.fftconvolve(rig, ir)


def band_error_db(h: np.ndarray, true: np.ndarray, lo: float, hi: float,
                  fit_gain: bool = False) -> float:
    """Error energy of h against true over [lo, hi] Hz, relative to the true energy there."""
    n = h.size
    hs, ts = np.fft.rfft(h), np.fft.rfft(true[:n])
    f = np.fft.rfftfreq(n, 1.0 / FS)
    s = (f >= lo) & (f <= hi)
    g = np.vdot(ts[s], hs[s]).real / np.vdot(ts[s], ts[s]).real if fit_gain else 1.0
    return 10.0 * math.log10(np.sum(np.abs(hs[s] - g * ts[s]) ** 2) / np.sum(np.abs(ts[s]) ** 2))


class Loopback(unittest.TestCase):
    def test_latency_equals_true_delay(self):
        p = m.sweep_params(**m.SHORT_PRESET)
        pk_err, gd_err, agree = [], [], []
        for frac in (0.4, 0.0, 0.25, 0.5, 0.75, 0.9):
            for seed in (0, 1):
                d = 317.0 + frac
                r = m.analyze(loopback_recording(p, d, seed), FS, p, 0.5,
                              loopback_channel=1)["report"]["per_channel"]["1"]
                pk_err.append(abs(r["peak_interp_samples"] - (d + FIR_DELAY)))
                gd_err.append(abs(r["group_delay_samples"] - (d + FIR_DELAY)))
                agree.append(abs(r["latency_disagreement_samples"]))
        note(f"loopback: max |group-delay err| {max(gd_err):.3g}, max |parabolic err| "
             f"{max(pk_err):.4f}, max disagreement {max(agree):.4f} samples")
        # Measured: group delay worst 5.76e-6 samples on these 6 fractions x 2 seeds; 9e-6 over
        # 20 fractions x 3 seeds at -80 dBFS noise (8.7e-5 at -60 dBFS, 8.7e-4 at -40 dBFS).
        self.assertLess(max(gd_err), 1e-4)
        # Measured: parabolic worst 0.0700 samples here, 0.0749 over 20 fractions — its bias on a
        # band-limited peak, largest near quarter-sample offsets and zero at 0 and 0.5.
        self.assertLess(max(pk_err), 0.1)
        self.assertLess(max(agree), 0.1)  # measured 0.0700: agree within a fraction of a sample

    def test_default_sweep_and_inverted_polarity(self):
        p = m.sweep_params()
        d = ELECTRICAL
        rec = -loopback_recording(p, d, 3)  # an inverting rig
        r = m.analyze(rec, FS, p, 2.0, loopback_channel=1)["report"]["per_channel"]["1"]
        note(f"default sweep, inverted: gd err {r['group_delay_samples'] - d - FIR_DELAY:.3g}, "
             f"PNR {r['peak_to_noise_db']:.1f} dB")
        self.assertLess(r["peak_value"], 0)
        # Measured: |err| 4.85e-7 samples (8 s sweep, -80 dBFS noise), PNR 117.1 dB.
        self.assertAlmostEqual(r["group_delay_samples"], d + FIR_DELAY, delta=1e-4)
        self.assertGreater(r["peak_to_noise_db"], 100.0)


class SyntheticRoom(unittest.TestCase):
    def test_ir_and_decay(self):
        p = m.sweep_params()
        for rt60 in (0.4, 1.2):
            errs, t20, t30, bands, delays = [], [], [], [], []
            for seed in (0, 1, 2):
                rec, true = room_recording(p, rt60, seed)
                res = m.analyze(rec, FS, p, 2.0, loopback_channel=1)
                r = res["report"]["per_channel"]["2"]
                errs.append(band_error_db(res["irs"][2], true, p["f1"], p["f2"]))
                t20.append(r["T20_s"] / rt60 - 1.0)
                t30.append(r["T30_s"] / rt60 - 1.0)
                bands += [v["T30"] / rt60 - 1.0 for v in r["octave_bands"].values()]
                delays.append(r["acoustic_delay_ms"] * FS / 1000.0 - DIRECT)
                self.assertEqual(int(np.argmax(np.abs(res["acoustic"][2]))), DIRECT)
                self.assertFalse(r["clipped"])
            note(f"room rt60 {rt60}: in-band err {np.round(errs, 1)} dB, T20 rel "
                 f"{np.round(t20, 4)}, T30 rel {np.round(t30, 4)}, octave T30 median |rel| "
                 f"{np.median(np.abs(bands)):.4f} max {np.max(np.abs(bands)):.4f}, "
                 f"onset err {np.round(delays, 2)} samples")
            with self.subTest(rt60=rt60):
                # Measured over seeds 0-3, [f1, f2]: -47.4/-54.5/-55.9/-53.5 dB (0.4 s),
                # -50.9/-58.2/-57.0/-54.7 dB (1.2 s).
                self.assertLess(max(errs), -42.0)
                self.assertLess(float(np.median(errs)), -47.0)
                # Measured over 6 seeds: broadband |T20 - RT| <= 1.05 %, |T30 - RT| <= 0.98 %.
                self.assertLess(max(abs(x) for x in t20 + t30), 0.03)
                # Measured seeds 0-2, 7 bands: median |T30 - RT| 2.02 % (0.4 s), 1.46 % (1.2 s);
                # worst 13.06 % (the 125 Hz band of the 0.4 s room: few modes, filter ringing).
                # Over 6 seeds: medians 2.12 % / 1.20 %, worst 13.06 %.
                self.assertLess(float(np.median(np.abs(bands))), 0.05)
                self.assertLess(float(np.max(np.abs(bands))), 0.25)
                # Measured over 12 seeds: onset -1.4 or -4.4 samples. The 20-dB onset rule reads
                # the band-limited direct spike's pre-ringing (linear-phase FIR + 20 kHz band
                # edge), so it lands a few samples early.
                self.assertLess(max(abs(x) for x in delays), 6.0)

    def test_decay_nan_when_range_unavailable(self):
        p = m.sweep_params()
        rec, _ = room_recording(p, 0.4, 0, noise_db=-40.0)
        r = m.analyze(rec, FS, p, 2.0, loopback_channel=1)["report"]["per_channel"]["2"]
        # Measured: decay range 41.1 dB — past the 35 dB T20 needs, short of the 45 dB T30 needs.
        note(f"-40 dBFS noise: range {r['decay_range_db']:.1f}, T20 {r['T20_s']}, T30 {r['T30_s']}")
        self.assertTrue(math.isnan(r["T30_s"]))
        self.assertAlmostEqual(r["T20_s"], 0.4, delta=0.4 * 0.05)  # measured 0.3947 s
        rec, _ = room_recording(p, 0.4, 0, noise_db=-30.0)
        r = m.analyze(rec, FS, p, 2.0, loopback_channel=1)["report"]["per_channel"]["2"]
        # Measured: decay range 31.1 dB — neither fit range is available.
        self.assertTrue(math.isnan(r["T20_s"]) and math.isnan(r["T30_s"]))


class Nonlinearity(unittest.TestCase):
    def test_soft_clip_keeps_causal_ir(self):
        # tanh(2x)/2 puts the 3rd harmonic of a 0.5-peak sine at -23.5 dB (measured).
        p = m.sweep_params()
        lin, _ = room_recording(p, 0.4, 0)
        res_lin = m.analyze(lin, FS, p, 2.0, loopback_channel=1)
        neg_n = int(2.0 * FS)

        def negative_time_db(res):
            return 10.0 * math.log10(np.sum(res["full"][2][-neg_n:] ** 2)
                                     / np.sum(res["irs"][2] ** 2))

        neg_lin = negative_time_db(res_lin)
        for where in ("rec", "spk"):
            rec, true = room_recording(p, 0.4, 0, drive=2.0, where=where)
            res = m.analyze(rec, FS, p, 2.0, loopback_channel=1)
            r = res["report"]["per_channel"]["2"]
            err = band_error_db(res["irs"][2], true, p["f1"], p["f2"])
            err_g = band_error_db(res["irs"][2], true, p["f1"], p["f2"], fit_gain=True)
            neg = negative_time_db(res)
            note(f"tanh on {where}: err {err:.1f} dB, gain-fitted {err_g:.1f} dB, T30 rel "
                 f"{r['T30_s'] / 0.4 - 1:.4f}, negative-time energy {neg:.1f} dB "
                 f"(linear {neg_lin:.1f} dB)")
            with self.subTest(where=where):
                if where == "rec":
                    # Measured seeds 0-3: -23.5/-25.7/-24.0/-23.9 dB; gain-fitted
                    # -29.6/-31.5/-30.0/-30.1 dB (a nonlinearity after the room is not the
                    # Hammerstein case the ESS separates cleanly, so some leaks in-band).
                    self.assertLess(err, -20.0)
                    self.assertLess(err_g, -26.0)
                else:
                    # Measured seeds 0-3: -14.4 dB, all of it the fundamental's compression
                    # (best gain 0.811); gain-fitted -33.1..-33.2 dB.
                    self.assertLess(err_g, -29.0)
                # Measured: T30 within -0.17..+0.5 % (rec) and +0.1..+2.0 % (spk), seeds 0-3.
                self.assertAlmostEqual(r["T30_s"], 0.4, delta=0.4 * 0.04)
                # Measured: the last 2 s of the circular IR (negative time) hold -43.5 dB of the
                # causal IR's energy linear, -27.7 dB (rec) and -22.2 dB (spk) clipped: the
                # distortion products are there, and outside the causal IR.
                self.assertGreater(neg - neg_lin, 10.0)


class Averaging(unittest.TestCase):
    def test_four_repeats_gain_six_db(self):
        gains = []
        for seed in range(4):
            pnr = {}
            for rep in (1, 4):
                p = m.sweep_params(repeats=rep)
                r = m.analyze(loopback_recording(p, ELECTRICAL, seed, -40.0), FS, p, 2.0,
                              loopback_channel=1)["report"]["per_channel"]["1"]
                pnr[rep] = r["peak_to_noise_db"]
            gains.append(pnr[4] - pnr[1])
        note(f"averaging x4: PNR gain {np.round(gains, 2)} dB, median {np.median(gains):.3f}")
        # Measured seeds 0-3 (these): 5.67/6.22/6.26/5.84 dB, median 6.029; seeds 0-7 median 6.142
        # (10 log10 4 = 6.02). The short preset's 50 ms noise window spreads 3.6..9.8 dB, so
        # this uses the default sweep's 2 s IR window.
        self.assertGreater(float(np.median(gains)), 5.3)
        self.assertLess(float(np.median(gains)), 6.8)


def write_wav24(path, x: np.ndarray) -> None:
    q = np.clip(np.round(x * 8388608.0), -8388608, 8388607).astype("<i4")
    raw = q.reshape(-1, 1).view(np.uint8).reshape(q.size, 4)[:, :3].tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(x.shape[1] if x.ndim == 2 else 1)
        w.setsampwidth(3)
        w.setframerate(FS)
        w.writeframes(raw)


def load_make_rir_fixtures():
    path = HERE / "make_rir_fixtures.py"
    spec = importlib.util.spec_from_file_location("make_rir_fixtures", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class WavIO(unittest.TestCase):
    def test_formats(self):
        p = m.sweep_params(**m.SHORT_PRESET)
        x = np.stack([loopback_recording(p, ELECTRICAL, 0), 0.3 * loopback_recording(p, 100.0, 1)],
                     1)
        with tempfile.TemporaryDirectory() as tmp:
            cases = {
                "int16": (lambda f: wavfile.write(f, FS, np.round(x * 32767).astype(np.int16)),
                          1.0 / 32768),
                "int24": (lambda f: write_wav24(f, x), 1.0 / 8388608),
                "int32": (lambda f: wavfile.write(f, FS, np.round(x * 2147483647).astype(np.int32)),
                          1.0 / 2147483648),
                "float32": (lambda f: wavfile.write(f, FS, x.astype(np.float32)), 1e-7),
            }
            for name, (writer, lsb) in cases.items():
                with self.subTest(fmt=name):
                    path = os.path.join(tmp, f"{name}.wav")
                    writer(path)
                    fs, y = m.read_wav(path)
                    self.assertEqual(fs, FS)
                    self.assertEqual(y.shape, x.shape)
                    self.assertLess(float(np.max(np.abs(y - x))), 1.5 * lsb)
                    r = m.analyze(y, fs, p, 0.5, loopback_channel=1)["report"]["per_channel"]["1"]
                    note(f"{name}: read err {np.max(np.abs(y - x)) / lsb:.3f} LSB, latency err "
                         f"{r['group_delay_samples'] - ELECTRICAL - FIR_DELAY:.3g} samples")
                    # Measured: latency error -4.57e-6 samples (int16), -3.34e-6 (int24, int32,
                    # float32); read error 1.000 / 0.500 / 0.999 / 0.298 LSB against the 1.5 above.
                    self.assertAlmostEqual(r["group_delay_samples"], ELECTRICAL + FIR_DELAY,
                                           delta=1e-4)

    def test_clipping_flag(self):
        p = m.sweep_params(**m.SHORT_PRESET)
        x = loopback_recording(p, ELECTRICAL, 0)
        x[5000] = 0.9995
        rep = m.analyze(x, FS, p, 0.5)["report"]
        self.assertTrue(rep["per_channel"]["1"]["clipped"])
        self.assertTrue(any("clipping" in w for w in rep["warnings"]))

    def test_ir_wav_imports_into_fixtures(self):
        p = m.sweep_params()
        rec, _ = room_recording(p, 0.4, 0)
        res = m.analyze(rec, FS, p, 2.0, loopback_channel=1)
        with tempfile.TemporaryDirectory() as tmp:
            m.write_outputs(res, FS, pathlib.Path(tmp), "t")
            rep = json.loads((pathlib.Path(tmp) / "t_report.json").read_text())
            for name, key in (("t_ch2_ir.wav", "wav_scale"),
                              ("t_ch2_ir_acoustic.wav", "acoustic_wav_scale")):
                with wave.open(os.path.join(tmp, name), "rb") as w:
                    self.assertEqual(w.getnchannels(), 1)
                    self.assertEqual(w.getsampwidth(), 4)
                    self.assertEqual(w.getframerate(), FS)
                    q = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int32)
                self.assertAlmostEqual(np.max(np.abs(q)) / 2147483648.0, 0.5, delta=1e-9)
                ir = res["irs"][2] if key == "wav_scale" else res["acoustic"][2]
                back = q / 2147483648.0 / rep["per_channel"]["2"][key]
                self.assertLess(float(np.max(np.abs(back - ir))), 1e-8 * np.max(np.abs(ir)))
            mrf = load_make_rir_fixtures()
            mrf.OUT_DIR = pathlib.Path(tmp)
            with open(os.devnull, "w") as null, contextlib.redirect_stdout(null):
                mrf.from_wav(os.path.join(tmp, "t_ch2_ir_acoustic.wav"), "measuretest",
                             "synthetic")
            header = (pathlib.Path(tmp) / "rir_measuretest.h").read_text()
            self.assertIn(f"k_rir_measuretest_taps = {mrf.TAPS}", header)


class Cli(unittest.TestCase):
    def test_sweep_then_deconvolve(self):
        script = str(HERE / "measure_rir.py")
        with tempfile.TemporaryDirectory() as tmp:
            t = pathlib.Path(tmp)
            out = subprocess.run([sys.executable, script, "sweep", str(t / "s.wav"), "--short",
                                  "--channels", "2"], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            fs, sw = wavfile.read(t / "s.wav")
            side = json.loads((t / "s.json").read_text())
            self.assertEqual((fs, sw.dtype, sw.shape), (FS, np.float32, (side["total_samples"], 2)))
            self.assertEqual(side["period_samples"], int(0.1 * FS) + FS + int(0.5 * FS))
            self.assertAlmostEqual(float(np.max(np.abs(sw))), 10 ** (-6 / 20), delta=1e-3)
            self.assertEqual(float(np.max(np.abs(sw[: side["pre_samples"]]))), 0.0)
            p = m.load_sidecar(t / "s.json")
            play = m.make_period(p)
            np.testing.assert_allclose(sw[:, 1], play.astype(np.float32))
            # the recording: [room, loopback] — loopback deliberately not column 1
            rng = np.random.default_rng(7)
            elec = electrical(play)
            ir = room_ir(0.15, 0, int(0.5 * FS))
            room = signal.fftconvolve(elec, ir)[: play.size]
            room *= 0.5 / np.max(np.abs(room))
            rec = np.stack([room, elec], 1) + 1e-4 * rng.standard_normal((play.size, 2))
            wavfile.write(t / "rec.wav", FS, rec.astype(np.float32))
            out = subprocess.run([sys.executable, script, "deconvolve", str(t / "rec.wav"),
                                  "--sweep", str(t / "s.json"), "--loopback-channel", "2",
                                  "--plot", str(t / "rec.png")], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertIn("electrical round trip", out.stdout)
            note(out.stdout)
            rep = json.loads((t / "rec_report.json").read_text(),
                             parse_constant=lambda c: self.fail(f"non-JSON constant {c}"))
            self.assertEqual(rep["ir_length_s"], 0.5)  # 2.0 s default clipped to the post-silence
            self.assertTrue(any("clipped to the post-silence" in w for w in rep["warnings"]))
            note(f"CLI round-trip err {rep['round_trip_samples'] - ELECTRICAL - FIR_DELAY:.3g} "
                 f"samples, acoustic delay {rep['per_channel']['1']['acoustic_delay_ms']:.4f} ms")
            # Measured: round-trip error -1.93e-6 samples; acoustic delay 4.9708 ms (onset 1.4
            # samples early, see SyntheticRoom).
            self.assertAlmostEqual(rep["round_trip_samples"], ELECTRICAL + FIR_DELAY, delta=1e-4)
            self.assertAlmostEqual(rep["per_channel"]["1"]["acoustic_delay_ms"], 5.0, delta=0.125)
            for f in ("rec_ch1_ir.wav", "rec_ch1_ir_acoustic.wav", "rec_ch2_ir.wav", "rec.png"):
                self.assertTrue((t / f).exists(), f)
            self.assertFalse((t / "rec_ch2_ir_acoustic.wav").exists())
            with wave.open(str(t / "rec_ch1_ir.wav"), "rb") as w:
                self.assertEqual((w.getsampwidth(), w.getnframes()), (4, int(0.5 * FS)))


class FakeSoundDevice:
    """Stands in for the sounddevice module: a loopback rig whose round trip moves by one block
    on every other stream, so the live code paths run with no audio device and no sound. It does
    not exercise PortAudio itself."""

    def __init__(self, room: bool = False):
        self.calls, self.room = [], room
        self.devices = [
            dict(name="Built-in Mic", max_input_channels=1, max_output_channels=0,
                 default_low_input_latency=0.002, default_low_output_latency=0.01),
            dict(name="Fake Interface USB", max_input_channels=4, max_output_channels=4,
                 default_low_input_latency=0.0015, default_low_output_latency=0.0025),
        ]

    def query_devices(self, device=None):
        return self.devices if device is None else self.devices[device]

    def playrec(self, data, **kw):
        self.calls.append(kw)
        bs = kw.get("blocksize") or 0
        d = ELECTRICAL + bs * (len(self.calls) % 2)
        x = np.asarray(data, dtype=np.float64)[:, 0]
        loop = signal.fftconvolve(frac_delay(x, d, 2000), FIR)[: x.size]
        cols = []
        for ch in kw["input_mapping"]:
            if self.room and ch == 3:  # input 3 is the microphone, the others are loopbacks
                y = signal.fftconvolve(loop, room_ir(0.15, 0, int(0.5 * FS)))[: x.size]
                cols.append(0.5 * y / np.max(np.abs(y)))
            else:
                cols.append(loop)
        rng = np.random.default_rng(len(self.calls))
        return (np.stack(cols, 1) + 1e-4 * rng.standard_normal((x.size, len(cols)))).astype(
            np.float32)

    def get_stream(self):
        class _S:
            latency = (0.0015, 0.0025)

        return _S()


class LivePathsWithFakeDevice(unittest.TestCase):
    def run_main(self, fake, argv):
        had, saved = "sounddevice" in sys.modules, sys.modules.get("sounddevice")
        sys.modules["sounddevice"] = fake
        try:
            with open(os.devnull, "w") as null, contextlib.redirect_stdout(null):
                m.main(argv)
        finally:
            if not had:
                del sys.modules["sounddevice"]
            else:
                sys.modules["sounddevice"] = saved

    def test_loopback(self):
        fake = FakeSoundDevice()
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "lb.json")
            self.run_main(fake, ["loopback", "--device", "fake", "--out-ch", "2", "--in-ch", "4",
                                 "--blocksize", "32", "--repeats", "4", "--out", out])
            res = json.loads(pathlib.Path(out).read_text())
        self.assertEqual(len(fake.calls), 4)  # one fresh playrec (stream) per repeat
        for kw in fake.calls:
            self.assertEqual((kw["device"], kw["input_mapping"], kw["output_mapping"],
                              kw["blocksize"], kw["latency"]), ((1, 1), [4], [2], 32, "low"))
        g = res["group_delay_samples"]
        # The fake alternates ELECTRICAL + 32 and ELECTRICAL (+ the FIR's 31).
        self.assertAlmostEqual(g["min"], ELECTRICAL + FIR_DELAY, delta=1e-3)
        self.assertAlmostEqual(g["max"], ELECTRICAL + FIR_DELAY + 32, delta=1e-3)
        self.assertEqual(res["input_device"], "Fake Interface USB")
        self.assertAlmostEqual(res["portaudio_reported_samples"]["median"], 0.004 * FS)
        self.assertEqual(len(res["repeats"]), 4)

    def test_measure(self):
        fake = FakeSoundDevice(room=True)
        with tempfile.TemporaryDirectory() as tmp:
            self.run_main(fake, ["measure", "--device", "fake", "--out-ch", "1", "2",
                                 "--in-ch", "3", "4", "--loopback-channel", "4", "--short",
                                 "--out-dir", tmp, "--stem", "r"])
            t = pathlib.Path(tmp)
            rep = json.loads((t / "r_report.json").read_text())
            fs, rec = wavfile.read(t / "r_recording.wav")
            self.assertEqual((fs, rec.dtype, rec.shape[1]), (FS, np.float32, 2))
            for f in ("r_sweep.wav", "r_sweep.json", "r_ch1_ir.wav", "r_ch1_ir_acoustic.wav",
                      "r_ch2_ir.wav"):
                self.assertTrue((t / f).exists(), f)
        self.assertEqual(fake.calls[0]["output_mapping"], [1, 2])
        self.assertEqual(fake.calls[0]["input_mapping"], [3, 4])
        self.assertEqual(rep["loopback_channel"], 2)  # device input 4 = recording column 2
        self.assertAlmostEqual(rep["per_channel"]["1"]["acoustic_delay_ms"], 5.0, delta=0.125)

    def test_device_not_found(self):
        fake = FakeSoundDevice()
        with self.assertRaises(SystemExit):
            self.run_main(fake, ["loopback", "--device", "mic", "--out-ch", "1", "--in-ch", "1"])


if __name__ == "__main__":
    unittest.main()
