#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Offline howl criterion for maximum-stable-gain measurements.

The anti-howl PoC measures the maximum stable gain of a real acoustic
loop (mic -> canceller chain -> loudspeaker -> room -> mic) by ramping
the bus gain 1 dB per 2 s and noting where it howls. The protocol
requires the howl decision to be made offline, on the recording,
independently of the product's own howl detector (which is under test).
This tool is that decision: "a narrowband peak >= 20 dB over the
program's spectral envelope, sustained >= 500 ms".

Two subcommands:

  analyze E.wav --program voice.wav --program track.wav
          --rt60 ROOM_report.json [--chain-rt60 S] [--dechirp]
          [--ramp START_DB,RATE_DB_PER_S,T0_S | --gain-log CSV]
          [--calibrate START,END] [--json OUT] [--plot PNG]
                 Apply the criterion to one run. E.wav is the chain's
                 output e before the bus gain - the speaker feed divided by
                 G(t), tapped inside the chain (--channel picks it from a
                 multichannel file). Each --program is a dry stem as it was
                 played (mono, or mixed to mono; resampled to the
                 recording's rate if needed). --rt60 is the room's decay and
                 --chain-rt60 the chain's own reverb (step 5); without
                 --rt60 the tool warns, because program stops then
                 false-trigger in reverberant rooms.

What to record. The loop the ramp is probing closes through whatever the
chain leaves of the room path: with a canceller holding F_hat, the chain
output is e = v / (1 - G (F - F_hat)), so the program's stems arrive at e
at fixed levels while that residual loop is stable, and a howl shows up
as its instability. With the chain bypassed, e is the mic. The MIC of a
chain-on run is only a cross-check: it hears v (1 + G F ...), which grows
with the gain long before anything is unstable, and a static calibration
cannot explain that growth. On the simulated loop with an ideal canceller
(no limit at all) the mic gave 13-34 false events per ramp, the first at
+21.46 .. +25.68 dB over the room's MSG, while e gave none.

  aggregate RUN1.json RUN2.json ...
                 Median and range of the runs' limits (the protocol's
                 three repeats per condition).

Processing, in order (analyze):
  1. Alignment, by GCC-PHAT between the recording's calibration segment
     and each stem over a +/-2 s lag window (--max-lag). Joint by
     default: stems play in sync and differ only by their acoustic paths,
     so the stem with the highest peak-to-rival ratio (its peak over the
     best peak >= 50 ms away) anchors, and every other stem is searched
     within +/-0.1 s of the anchor's lag. --independent-align searches
     every stem over the whole window. Per stem: the lag (recording =
     stem delayed by lag), the PHAT peak, its ratio to the median |GCC|,
     the peak-to-rival ratio (inside the +/-0.1 s window for non-anchor
     stems), whether it anchored, and its independent lag; a ratio below
     2.0 is written as a warning ("alignment ambiguous").
  2. Two analysis passes, each running steps 3-7 on its own STFT (Hann
     window, frame times at window centres; M(k,t) is the recording's
     power, P_s(k,t) each aligned stem's):
       long   N = 8192, hop 1024 at 48 kHz (5.86 Hz bins, 21.3 ms): the
              protocol's resolution, which separates a howl from the
              program's harmonics.
       short  N = 2048, hop 256 (23.4 Hz, 5.3 ms), for drifting howls
              (a frequency shifter in the loop makes a recirculating
              component climb, e.g. +5 Hz per 10 ms pass = 500 Hz/s).
              --no-short-pass turns it off.
       dechirped (--dechirp) the long pass again at +/-250, +/-500 and
              +/-800 Hz/s: each frame is dechirped about its centre (the
              window becomes w(t) exp(-j pi r t^2)), so a component
              sweeping at r reads as a stationary tone; the stems are
              dechirped identically, E is computed per rate from them with
              the plain long pass's fitted transfer and noise floor (a
              per-rate fit left pad partials the voice masked in
              calibration unexplained: false events), and linking expects
              the track to move r hop_s / bin_hz bins per hop. Worst loss
              between grid points -1.42 dB (650 Hz/s; -2.10 at 1000 Hz/s,
              -4.78 at 1200) against -5.68 / -7.68 dB at 500 / 800 Hz/s
              for the plain pass. It costs ~4.6x the analysis. The short
              pass is off by default when the bank is on: over nine rooms
              it added nothing the bank missed (one frame earlier in two
              limits). Recommended for any run with a frequency shifter
              or other time-variant element in the loop.
     At other rates N and the hop scale with fs. The short pass keeps the
     long pass's durations (500 ms, one long hop of dropout, +/-1 long hop
     of time max filter where one is used), links at +/-1 bin per hop
     (4.4 kHz/s; 800 Hz/s would be 0.18 bins/hop, below the centroid's
     jitter), and clamps the narrowband window of step 6 to at least
     +/-6 bins (141 Hz), so the Hann main lobe (+/-2 bins) stays a
     minority of it. The long pass's parameters are the protocol's.
  3. Calibration (--calibrate START,END; by default the gain map's
     constant-gain warm-up - [0, T0] of --ramp, or [0, the last row still
     at the first gain] of --gain-log, at least 5 s - and without a gain
     map the protocol's 0-30 s; the gain must be >= 15 dB below any limit
     there). Per bin, a static
     power transfer per stem is fitted by non-negative least squares in
     the power domain over the calibration frames,
     M(k,t) ~= sum_s a_s(k) P_s(k,t), then smoothed with a 5-bin moving
     average (+/-2 bins). The noise floor N(k) is the 10th percentile of
     M(k,.) over the calibration frames. The calibration segment must
     contain every stem's spectral material (a looped phrase satisfies
     this when the segment covers one loop): the fit only learns bins
     each stem excited, and a bin a stem never excited gets no transfer,
     so that stem is unexplained there later. The fraction of analysis
     bins with a non-zero fitted transfer is reported per stem (before and
     after the smoothing). Anything else loud in the segment is absorbed
     into the transfer at its bins (see the limits below). An event whose
     onset falls inside the calibration window is rejected - it cannot
     set the limit - and reported apart under "events_in_calibration".
  4. Explained power, E_expl(k,t) = sum_s a_s(k) maxfilt(P_s)(k,t), where
     maxfilt is a max filter over +/-2 bins (absorbs small frequency
     misalignment and vibrato).
  5. Reverberation hold (with --rt60). The program's energy does not stop
     at the mic when the stems stop: the room decays it. So
       E_hold(k,t) = max(E_expl(k,t), E_hold(k,t-1) * 10^(-6 hop_s / T(k)))
       E(k,t)      = E_hold(k,t) + N(k)
     where T(k) = max(room RT60 at bin k, --chain-rt60) times --rt60-scale
     (default 1.0; a safety factor): a reverb inside the chain decays at e
     at its own rate, which is not in the room's RT60. --rt60 takes a number of seconds, or the report JSON
     that measure_rir.py deconvolve writes: per octave band 125-8000 Hz
     its octave T30, else the channel's broadband T30, else its broadband
     T20 (--rt60-channel picks the channel; default the first acoustic
     one). T(k) is interpolated in log frequency between octave centres
     and held outside 125-8000 Hz; the values and their sources go into
     the JSON. Without --rt60 the time max filter spans +/-1 long hop
     instead (the tool's first behaviour) and a warning is written to the
     JSON and stdout.
  6. A bin is flagged when 10 log10(M/E) >= 20 dB (--excess-db) AND it
     is a narrowband peak of M: >= 10 dB (--peak-db) above the median of
     M over +/-1/3 octave around k in that frame (window clamped to at
     least +/-8 bins in the long pass, +/-6 in the short one; evaluated
     exactly per candidate bin, or on the grid of step 9 when a frame has
     > 128 candidates). Flagged bins in a frame separated by at most the
     link tolerance (3 bins long, 1 bin short) form one cluster, i.e. one
     peak, located at the M-weighted centroid over the cluster's span (a
     drifting component smears over several bins per frame, and a program
     harmonic it crosses can split its flagged run; the centroid keeps
     the linking of step 7 on it). The peak's excess is the largest
     10 log10(M/E) among its bins.
  7. Events. Peaks are linked across frames into tracks: a peak extends
     the nearest active track whose last peak is within the tolerance per
     elapsed hop (long pass +/-3 bins, --drift-bins; 3 bins/hop follows
     ~800 Hz/s at 48 kHz), allowing one long hop of missing frames
     (--max-dropout). A track's duration is (last frame - first frame + 1)
     hops; it is a qualifying event when the duration is >= 500 ms
     (--min-duration). Per event: onset (the first frame's time), end,
     duration, start/end/min/max frequency, the maximum excess
     10 log10(M/E), the frequency track, and "passes": which pass found
     it.
  8. Merging. An event qualifies if either pass qualifies it. Events that
     overlap in time and in frequency (within one short-pass bin) merge
     into one: earliest onset, latest end, the union of their frequency
     ranges and passes (e.g. ["long", "long@+500"]), the dechirp rates
     that found it ("rates_hz_per_s"), and the parts under "components".
  9. --no-program mode: no stems; E(k,t) is the recording's own median
     over +/-1/3 octave around k excluding +/-3 bins around the centre,
     evaluated exactly below bin 128 and on a 1/24-octave grid above
     (log-interpolated between grid points). WARNING: this mode
     FALSE-TRIGGERS ON HELD NOTES AND TONAL PADS - a held sung note is a
     persistent narrowband peak over its own envelope. Use it only for
     runs excited by broadband noise, never on a musical program.
 10. Gain mapping. --ramp START_DB,RATE_DB_PER_S,T0_S gives the bus gain
     START_DB before T0_S and START_DB + RATE*(t - T0_S) after (a
     continuous ramp; 1 dB per 2 s is RATE 0.5). --gain-log CSV reads
     rows "time_s,gain_db" (non-numeric lines are skipped), linearly
     interpolated and held at the ends; write two rows per step to
     describe a stepped ramp. The run's LIMIT is the bus gain at the
     onset of the first qualifying event.

Outputs: a JSON record (--json, default REC.howl.json beside the
recording) with every parameter, warnings, the alignment lags, the
calibration summary, per-pass statistics, all qualifying events and the
limit; a summary on stdout; and with --plot the long pass's spectrogram
with its flagged tracks overlaid and the qualifying events marked (orange
where only the short pass found them), so a human can check the
criterion was not fooled by the program.

The thresholds (20 dB, 500 ms, 10 dB, +/-3 bins) are the protocol's
parameters; they are exposed as options with those defaults and are
written into the JSON.

Known limits, measured on the simulated loop in test_howl_criterion.py
(nine rooms from both generator families, plus eight synthetic rooms with
RT60 0.4/0.6/0.8/1.2 s, two seeds each; the mouth-to-mic path decays at
the room's RT60; 10 s warm-up, then 1 dB / 2 s):
  - What the limit means: the onset of the first frame of the first
    qualifying event. Dry loop, 5 ms: +/- the textbook MSG
    (-20 log10 max|F|) -0.296 .. +2.123 dB (median +1.368), and from the
    loop's Nyquist limit -0.367 .. +1.117 dB (median +0.306). Negative
    values are ringing > 20 dB over the program for > 500 ms while the
    loop is still (just) stable. Limit - Nyquist per loop delay, dry
    5/10/20/30 ms: medians +0.306 / +0.141 / +0.537 / +0.254 dB, overall
    -1.581 .. +1.216; with a canceller (residual R = F - F_hat, 20 dB
    ASG), analysed at e: medians -0.338 / +0.086 / +0.136 / -0.016 dB,
    overall -1.264 .. +1.515. Longer delays did not make the limit
    systematically later.
  - Program stops without --rt60: the room's decay of a held note or pad
    is unexplained. Four program gaps per 40 s at MSG-6 (one inside the
    calibration window) gave 0/0, 8/5, 25/26 and 36/38 false events outside
    the calibration window (long pass; two seeds) in rooms with RT60
    0.4/0.6/0.8/1.2 s. With the hold at the measured RT60: 0 in all eight
    (longest flagged track 43-149 ms). At 0.5x RT60: 0/0, 2/1, 9/8, 15/22.
    At 2x: 0.
  - A reverb inside the chain (RT60 1.5 s, wet 0.3) with the hold at the
    room's RT60 only: 15-30 false events per 40 s (+4-11 inside the
    calibration window) in the nine rooms; with --chain-rt60 1.5: 0.
  - What the hold costs: it can mask a howl that starts where the program
    just was. On the ramp (before the warm-up was added) its limits moved
    -0.170 .. +0.032 dB against no hold in seventeen rooms, except one
    (-1.184 dB, earlier: dropping the +1-frame look-ahead of the old time
    max filter let near-unstable ringing qualify sooner); at 2x RT60 the
    limits moved a further 0 .. +0.074 dB. With the warm-up: -0.013 ..
    +0.011 dB in the nine rooms.
  - Closed-loop ringing is not in the room's RT60: near a loop resonance
    the loop lengthens the decay (one fixture room at MSG-6: ~140 dB/s at
    1664 Hz against the room's ~500 dB/s), and such a tail stayed flagged
    for up to 341 ms even at 2x RT60. It did not reach 500 ms here.
  - Drifting components. Against a noise-like envelope, a component
    drifting 500 Hz/s reads 5.69 dB lower per bin than a stationary tone
    in the long window and 6.41 dB lower in the short one (4x wider bins),
    and 0.36 dB lower in the dechirped long pass. Detection levels (dB over
    the long pass's median envelope, two rooms), plain long pass / with
    --dechirp: 0 Hz/s 24 / 24; 100 Hz/s 20-23 / 20-23; 250 Hz/s 27-29 /
    25; 500 Hz/s 28-29 / 24; 800 Hz/s never (up to 36) / 21-23. Even a
    stationary tone needs 24 dB, not 20: the level is referenced to the
    median envelope, and the rule wants 20 dB in every frame.
  - A frequency shifter in the loop (the review's IIR allpass-pair SSB,
    +2 and +5 Hz, 10 and 20 ms, nine rooms): against the bisected limit
    of the same loop (80 s white-noise probes, MuTap's 64-sample-block
    40 dB rule; converged to within 0.088 dB of the 40 s probe) the
    criterion called the limit 0.317 .. 4.531 dB LOW, median 1.363 dB
    (with --dechirp 0.339 .. 4.531, median 1.715; never later, earlier in
    9 of 36). Below runaway, program partials recirculating through the
    shifter (climbing by the shift each pass) stay > 20 dB over the
    envelope for > 500 ms. Shift-condition limits measured with this
    criterion therefore understate the shifter's stable gain against a
    white-noise MSG by about that much; say which definition a number
    uses.
  - Alignment. GCC-PHAT weights every frequency bin equally, so any
    broadband copy of a stem in the recording (electrical crosstalk, a
    noise source replaying it) competes with the acoustic path however
    quiet it is (a -70 dB copy won once in the test harness), and a
    periodic program (bar-length loops, chord changes) makes rival peaks
    at its period. At e behind a canceller the track arrives only through
    the residual R: its peak-to-rival ratio was 1.03-5.54 there (2.49-26.73
    at the mic or in a dry loop), and aligned independently it went 2 s
    off in one run of 36, at the pad period, and the unexplained pad set a
    limit 20.411 dB low. Joint alignment (the default) put all 36 on R's
    strongest tap (ratio 2.49-8.10 inside the window, no warnings; that
    run's limit then +0.581 dB from Nyquist). Check the reported lags.
  - Calibration coverage and absorption: see step 3. Where two stems
    share a frequency the per-bin fit gives it to the louder one: the
    voice's held 440 Hz note left the pad's 440 Hz partial with no
    transfer in some bins, which only the +/-2-bin smoothing covered. A
    700 ms tone
    +30 dB over the program inside the calibration window produced no
    event at all (absorbed into the fitted transfer), and an identical
    tone at the same frequency 16 s later was then missed too (0 events
    against 1 without it; two rooms). Keep the warm-up clean.

Requires: numpy, scipy; matplotlib only for --plot. Python >= 3.10.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import pathlib
import sys
from dataclasses import dataclass

import numpy as np
import scipy.fft
import scipy.ndimage
import scipy.signal
from scipy.io import wavfile
from scipy.optimize import nnls

REF_FS = 48000
REF_N_FFT = 8192
REF_HOP = 1024
CHUNK_FRAMES = 256
MIN_HALF_WIDTH_BINS = 8
EXACT_BELOW_BIN = 128
GRID_STEPS_PER_OCTAVE = 24
MAX_EXACT_CANDIDATES = 128
NO_PROGRAM_EXCLUDE_BINS = 3
ALIGN_RIVAL_S = 0.05  # rival GCC peaks are sought more than this far from the best
ALIGN_MIN_RIVAL_RATIO = 2.0  # below this the alignment is reported as ambiguous
JOINT_WINDOW_S = 0.1  # stems play in sync: their lags differ only by acoustic paths
OCTAVES = (125, 250, 500, 1000, 2000, 4000, 8000)  # measure_rir.py's bands
SHORT_N_FFT = 2048  # short pass, at 48 kHz (scaled with fs): 23.4 Hz bins
SHORT_HOP = 256  # 5.3 ms
SHORT_DRIFT_BINS = 1.0  # per hop: follows 4.4 kHz/s (800 Hz/s is 0.18 bins/hop, below centroid jitter)
DECHIRP_BANK = (-800.0, -500.0, -250.0, 250.0, 500.0, 800.0)  # plus 0, the plain long pass
SHORT_MIN_HALF_WIDTH_BINS = 6  # 141 Hz: the Hann main lobe (+/-2 bins) stays a minority of the window
NO_RT60_WARNING = (
    "no --rt60: the explained power has no reverberation hold, so the room's decay after the program "
    "stops counts against it. On the simulated loop (four program gaps per 40 s at MSG-6) rooms with "
    "RT60 0.4/0.6/0.8/1.2 s gave 0-0/5-8/25-26/36-38 false events. Supply --rt60 (seconds, or the "
    "report JSON of measure_rir.py deconvolve)."
)


@dataclass
class Params:
    """The criterion's parameters (defaults are the protocol's)."""

    excess_db: float = 20.0
    peak_db: float = 10.0
    min_duration_s: float = 0.5
    drift_bins: int = 3
    max_dropout_frames: int = 1
    envelope_octaves: float = 1.0 / 3.0
    maxfilt_bins: int = 2
    maxfilt_frames: int = 1
    smooth_bins: int = 2
    noise_percentile: float = 10.0
    max_lag_s: float = 2.0
    calibrate_s: tuple[float, float] = (0.0, 30.0)  # the protocol's constant-gain warm-up
    fmin_hz: float = 40.0
    fmax_hz: float | None = None  # default 0.45 fs
    n_fft: int | None = None  # default: 8192 scaled by fs/48000
    hop: int | None = None  # default: 1024 scaled by fs/48000
    rt60: dict | None = None  # load_rt60(); None = no reverberation hold
    rt60_scale: float = 1.0
    chain_rt60: float | None = None  # the chain's own reverb: the hold uses max(room, chain) per band
    short_pass: bool | None = None  # None: on unless the dechirp bank is on (the bank covers what it adds)
    joint_align: bool = True  # align the clearest stem, search the others within +/-JOINT_WINDOW_S of it
    dechirp_rates: tuple[float, ...] = ()  # the long pass repeated at these chirp rates (Hz/s); see --dechirp

    def geometry(self, fs: int) -> tuple[int, int]:
        n_fft = self.n_fft or int(round(REF_N_FFT * fs / REF_FS))
        hop = self.hop or int(round(REF_HOP * fs / REF_FS))
        return n_fft, hop


# --------------------------------------------------------------------------
# I/O


def read_wav(path: str | pathlib.Path, channel: int | None = None) -> tuple[int, np.ndarray]:
    """Read a WAV as float64 in [-1, 1]; one channel, or the mean of all."""
    fs, x = wavfile.read(str(path))
    if x.dtype == np.int16:
        x = x.astype(np.float64) / 32768.0
    elif x.dtype == np.int32:
        x = x.astype(np.float64) / 2147483648.0
    elif x.dtype == np.uint8:
        x = (x.astype(np.float64) - 128.0) / 128.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 2:
        if channel is None:
            x = x.mean(axis=1)
        else:
            if not 0 <= channel < x.shape[1]:
                raise SystemExit(f"{path}: channel {channel} out of range (has {x.shape[1]})")
            x = x[:, channel]
    elif channel not in (None, 0):
        raise SystemExit(f"{path}: mono file, channel {channel} requested")
    return int(fs), x


def resample_to(x: np.ndarray, fs_from: int, fs_to: int) -> np.ndarray:
    if fs_from == fs_to:
        return x
    g = math.gcd(fs_from, fs_to)
    return scipy.signal.resample_poly(x, fs_to // g, fs_from // g)


# --------------------------------------------------------------------------
# Signal processing


def n_frames(n_samples: int, n_fft: int, hop: int) -> int:
    return 0 if n_samples < n_fft else 1 + (n_samples - n_fft) // hop


def stft_power(x: np.ndarray, n_fft: int, hop: int, lo: int = 0, hi: int | None = None, rate: float = 0.0,
               fs: float = REF_FS) -> np.ndarray:
    """|STFT|^2 (Hann, unnormalized) of frames [lo, hi) as float32 (frames, bins). With rate (Hz/s) each
    frame is dechirped about its centre first: a component sweeping at that rate reads as a stationary
    tone at its mid-frame frequency (the window becomes w(t) exp(-j pi rate t^2), t from the centre)."""
    total = n_frames(len(x), n_fft, hop)
    hi = total if hi is None else min(hi, total)
    lo = max(0, lo)
    win = scipy.signal.get_window("hann", n_fft)
    if rate:
        tau = (np.arange(n_fft) - n_fft / 2) / fs
        win = win * np.exp(-1j * np.pi * rate * tau * tau)
    out = np.empty((max(0, hi - lo), n_fft // 2 + 1), dtype=np.float32)
    view = np.lib.stride_tricks.sliding_window_view(x, n_fft)
    for c0 in range(lo, hi, CHUNK_FRAMES):
        c1 = min(hi, c0 + CHUNK_FRAMES)
        frames = view[c0 * hop : (c1 - 1) * hop + 1 : hop] * win
        if rate:
            spec = scipy.fft.fft(frames, axis=1, workers=-1)[:, : n_fft // 2 + 1]
        else:
            spec = scipy.fft.rfft(frames, axis=1, workers=-1)
        out[c0 - lo : c1 - lo] = (spec.real**2 + spec.imag**2).astype(np.float32)
    return out


def gcc_phat_lag(rec: np.ndarray, stem: np.ndarray, fs: int, seg: tuple[int, int], max_lag: int,
                 window: tuple[int, int] | None = None) -> dict:
    """Lag L (samples) maximizing |GCC-PHAT| with rec[n] ~ stem[n - L], |L| <= max_lag (and, with window,
    lo <= L <= hi; rivals are then sought inside the window too)."""
    s0, s1 = seg
    x = rec[s0:s1]
    y0 = s0 - max_lag
    y = np.zeros(len(x) + 2 * max_lag)
    src0, src1 = max(0, y0), min(len(stem), y0 + len(y))
    if src1 > src0:
        y[src0 - y0 : src1 - y0] = stem[src0:src1]
    nfft = scipy.fft.next_fast_len(len(x) + len(y))
    cross = np.conj(scipy.fft.rfft(x, nfft)) * scipy.fft.rfft(y, nfft)
    mag = np.abs(cross)
    floor = 1e-9 * (mag.max() if mag.size and mag.max() > 0 else 1.0)
    c = scipy.fft.irfft(cross / np.maximum(mag, floor), nfft)[: 2 * max_lag + 1]
    ac = np.abs(c)
    if window is not None:
        lags = max_lag - np.arange(ac.size)
        ac = np.where((lags >= window[0]) & (lags <= window[1]), ac, 0.0)
    j = int(np.argmax(ac))
    lag = max_lag - j
    peak = float(ac[j])
    med = float(np.median(ac[ac > 0] if window is not None else ac)) or 1e-30  # over the searched lags only
    far = int(round(ALIGN_RIVAL_S * fs))  # a rival peak this far away would misplace the stem
    rival = np.concatenate([ac[: max(0, j - far)], ac[j + far + 1 :]])
    rival = rival[rival > 0] if window is not None else rival
    rival_peak = float(rival.max()) if rival.size else 0.0
    rival_j = int(np.flatnonzero(ac == rival_peak)[0]) if rival.size else j
    return {
        "lag_samples": int(lag),
        "lag_s": lag / fs,
        "polarity": 1 if c[j] >= 0 else -1,
        "gcc_phat_peak": peak,
        "gcc_phat_peak_to_median": peak / med,
        "gcc_phat_peak_to_rival": peak / rival_peak if rival_peak > 0 else float("inf"),
        "rival_lag_samples": int(max_lag - rival_j),
    }


def shift(stem: np.ndarray, lag: int, length: int) -> np.ndarray:
    """out[n] = stem[n - lag], zero outside, len(out) = length."""
    out = np.zeros(length)
    d0, d1 = max(0, lag), min(length, lag + len(stem))
    if d1 > d0:
        out[d0:d1] = stem[d0 - lag : d1 - lag]
    return out


def envelope_windows(n_bins: int, octaves: float, min_half_width: int) -> tuple[np.ndarray, np.ndarray]:
    """Per bin k the half-open window [lo, hi) spanning +/- octaves around k (at least +/-min_half_width)."""
    k = np.arange(n_bins, dtype=np.float64)
    lo = np.floor(k * 2.0 ** (-octaves)).astype(np.int64)
    hi = np.ceil(k * 2.0**octaves).astype(np.int64) + 1
    lo = np.minimum(lo, np.arange(n_bins) - min_half_width)
    hi = np.maximum(hi, np.arange(n_bins) + min_half_width + 1)
    return np.clip(lo, 0, n_bins), np.clip(hi, 0, n_bins)


def envelope_grid(n_bins: int) -> np.ndarray:
    """Evaluation grid for the no-program envelope: every bin below EXACT_BELOW_BIN, then 1/24 octave."""
    top = n_bins - 1
    geo = EXACT_BELOW_BIN * 2.0 ** (np.arange(0, 64 * GRID_STEPS_PER_OCTAVE) / GRID_STEPS_PER_OCTAVE)
    geo = np.round(geo[geo < top]).astype(np.int64)
    return np.unique(np.concatenate([np.arange(min(EXACT_BELOW_BIN, top)), geo, [top]]))


def grid_median(mc: np.ndarray, grid: np.ndarray, lo: np.ndarray, hi: np.ndarray, exclude: int) -> np.ndarray:
    """Median of mc (frames, bins) over each grid bin's window, log-interpolated to every bin."""
    vals = np.empty((mc.shape[0], len(grid)), dtype=np.float64)
    for i, g in enumerate(grid):
        a, b = int(lo[g]), int(hi[g])
        if exclude > 0:
            idx = np.r_[a : max(a, g - exclude), min(b, g + exclude + 1) : b]
            sel = mc[:, idx] if idx.size else mc[:, a:b]
        else:
            sel = mc[:, a:b]
        vals[:, i] = np.median(sel, axis=1)
    logv = np.log(np.maximum(vals, 1e-38))
    bins = np.arange(mc.shape[1])
    j = np.clip(np.searchsorted(grid, bins, side="right") - 1, 0, len(grid) - 2)
    w = np.clip((bins - grid[j]) / np.maximum(grid[j + 1] - grid[j], 1), 0.0, 1.0)
    return np.exp(logv[:, j] * (1.0 - w) + logv[:, j + 1] * w)


# --------------------------------------------------------------------------
# The criterion


def fit_calibration(m_cal: np.ndarray, p_cal: list[np.ndarray], band: np.ndarray,
                    p: Params) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per-bin NNLS power transfer a (stems, bins), smoothed; noise floor N (bins); the unsmoothed a."""
    n_bins = m_cal.shape[1]
    a = np.zeros((len(p_cal), n_bins))
    noise = np.percentile(m_cal, p.noise_percentile, axis=0).astype(np.float64)
    if p_cal:
        for k in np.flatnonzero(band):
            cols = np.stack([pc[:, k] for pc in p_cal], axis=1).astype(np.float64)
            scale = np.sqrt(np.sum(cols * cols, axis=0))
            ok = scale > 0
            if not ok.any():
                continue
            sol, _ = nnls(cols[:, ok] / scale[ok], m_cal[:, k].astype(np.float64))
            a[ok, k] = sol / scale[ok]
        raw = a.copy()
        width = 2 * p.smooth_bins + 1
        a = scipy.ndimage.uniform_filter1d(a, size=width, axis=1, mode="nearest")
    else:
        raw = a.copy()
    return a, noise, raw


def link_tracks(peaks: list[list[tuple[float, float, float]]], drift_bins: float, max_dropout: int,
                predicted_bins_per_hop: float = 0.0) -> list[dict]:
    """Link per-frame peaks (centroid bin, max M, excess_db) into tracks (see step 7)."""
    active: list[dict] = []
    done: list[dict] = []
    for t, frame_peaks in enumerate(peaks):
        still = []
        for tr in active:
            (still if t - tr["frames"][-1] <= 1 + max_dropout else done).append(tr)
        active = still
        used: set[int] = set()
        for k, _mag, exc in sorted(frame_peaks, key=lambda q: -q[2]):
            best, best_d = None, None
            for i, tr in enumerate(active):
                if i in used:
                    continue
                gap = t - tr["frames"][-1]
                d = abs(k - tr["bins"][-1] - predicted_bins_per_hop * gap)
                if gap >= 1 and d <= drift_bins * gap and (best_d is None or d < best_d):
                    best, best_d = i, d
            if best is None:
                active.append({"frames": [t], "bins": [k], "excess": [exc]})
                used.add(len(active) - 1)
            else:
                tr = active[best]
                tr["frames"].append(t)
                tr["bins"].append(k)
                tr["excess"].append(exc)
                used.add(best)
    return done + active


@dataclass
class PassGeometry:
    """One analysis pass: STFT geometry and the parameters that scale with it."""

    name: str
    n_fft: int
    hop: int
    drift_bins: float
    max_dropout_frames: int
    maxfilt_frames: int  # time max filter, used only without a reverberation hold
    min_half_width_bins: int
    rate: float = 0.0  # dechirp rate, Hz/s (0: the plain STFT)


def pass_geometries(fs: int, p: Params) -> list[PassGeometry]:
    n_fft, hop = p.geometry(fs)
    out = [PassGeometry("long", n_fft, hop, p.drift_bins, p.max_dropout_frames, p.maxfilt_frames,
                        MIN_HALF_WIDTH_BINS)]
    for r in p.dechirp_rates:
        if r:
            out.append(PassGeometry(f"long@{r:+g}", n_fft, hop, p.drift_bins, p.max_dropout_frames, p.maxfilt_frames,
                                    MIN_HALF_WIDTH_BINS, float(r)))
    if (not p.dechirp_rates) if p.short_pass is None else p.short_pass:
        s_fft = int(round(SHORT_N_FFT * fs / REF_FS))
        s_hop = int(round(SHORT_HOP * fs / REF_FS))
        per_long_hop = hop / s_hop  # dropout and time max filter keep the long pass's durations
        out.append(PassGeometry("short", s_fft, s_hop, SHORT_DRIFT_BINS,
                                int(round(p.max_dropout_frames * per_long_hop)),
                                int(round(p.maxfilt_frames * per_long_hop)), SHORT_MIN_HALF_WIDTH_BINS))
    return out


def load_rt60(spec: str, channel: str | None = None) -> dict:
    """--rt60: seconds, or the report JSON of measure_rir.py deconvolve (see step 5)."""
    try:
        value = float(spec)
    except ValueError:
        value = None
    if value is not None:
        if not value > 0:
            raise SystemExit(f"--rt60 {spec}: must be > 0 s")
        return {"source": "value", "channel": None, "bands_hz": list(OCTAVES),
                "rt60_s": [value] * len(OCTAVES), "band_source": ["value"] * len(OCTAVES)}
    return rt60_from_report(json.loads(pathlib.Path(spec).read_text()), channel, str(spec))


def rt60_from_report(rep: dict, channel: str | None = None, spec: str = "report") -> dict:
    """Per-octave RT60 from a measure_rir.py deconvolve report (octave T30, else broadband T30, else T20)."""
    per = rep.get("per_channel")
    if not isinstance(per, dict) or not per:
        raise SystemExit(f"--rt60 {spec}: no per_channel section (not a measure_rir.py deconvolve report?)")
    acoustic = sorted((c for c, r in per.items() if r.get("role") != "loopback"), key=lambda c: int(c))
    if channel is None:
        if not acoustic:
            raise SystemExit(f"--rt60 {spec}: no acoustic channel")
        channel = acoustic[0]
    if str(channel) not in per:
        raise SystemExit(f"--rt60 {spec}: no channel {channel} (has {sorted(per)})")
    r = per[str(channel)]
    bands = r.get("octave_bands") or {}
    rt, src = [], []
    for fc in OCTAVES:
        cands = ((bands.get(str(fc)) or {}).get("T30"), "octave T30"), (r.get("T30_s"), "broadband T30"), \
                (r.get("T20_s"), "broadband T20")
        for v, name in cands:
            if v is not None and np.isfinite(v) and v > 0:
                rt.append(float(v))
                src.append(name)
                break
        else:
            raise SystemExit(f"--rt60 {spec}: channel {channel} has no T30/T20 for {fc} Hz nor broadband")
    return {"source": str(spec), "channel": str(channel), "acoustic_channels": acoustic,
            "bands_hz": list(OCTAVES), "rt60_s": rt, "band_source": src}


def rt60_per_bin(freqs: np.ndarray, rt60: dict) -> np.ndarray:
    """RT60 per bin: log-frequency interpolation between octave centres, held outside 125-8000 Hz."""
    return np.interp(np.log2(np.maximum(freqs, 1.0)), np.log2(rt60["bands_hz"]), rt60["rt60_s"])


def merge_events(events: list[dict], tol_hz: float) -> list[dict]:
    """Merge events (from either pass) that overlap in time and, within tol_hz, in frequency."""
    n = len(events)
    parent = list(range(n))

    def root(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(n):
        a = events[i]
        for j in range(i + 1, n):
            b = events[j]
            if (a["onset_s"] <= b["onset_s"] + b["duration_s"] and b["onset_s"] <= a["onset_s"] + a["duration_s"]
                    and a["f_min_hz"] - tol_hz <= b["f_max_hz"] and b["f_min_hz"] - tol_hz <= a["f_max_hz"]):
                parent[root(j)] = root(i)
    groups: dict[int, list[dict]] = {}
    for i in range(n):
        groups.setdefault(root(i), []).append(events[i])
    out = []
    for grp in groups.values():
        grp.sort(key=lambda ev: ev["onset_s"])
        if len(grp) == 1:
            out.append(grp[0])
            continue
        first = grp[0]
        last = max(grp, key=lambda ev: ev["onset_s"] + ev["duration_s"])
        merged = dict(first)
        merged.update(
            onset_s=first["onset_s"],
            end_s=max(ev["end_s"] for ev in grp),
            duration_s=last["onset_s"] + last["duration_s"] - first["onset_s"],
            f_end_hz=last["f_end_hz"],
            f_min_hz=min(ev["f_min_hz"] for ev in grp),
            f_max_hz=max(ev["f_max_hz"] for ev in grp),
            max_excess_db=max(ev["max_excess_db"] for ev in grp),
            passes=sorted({p_ for ev in grp for p_ in ev["passes"]}),
            rates_hz_per_s=sorted({r_ for ev in grp for r_ in ev["rates_hz_per_s"]}),
            components=[{k: ev[k] for k in ("passes", "rates_hz_per_s", "onset_s", "duration_s", "f_start_hz",
                                            "f_end_hz", "max_excess_db")} for ev in grp],
        )
        out.append(merged)
    out.sort(key=lambda ev: ev["onset_s"])
    return out


def _analyze_pass(rec: np.ndarray, fs: int, aligned: list[np.ndarray] | None, p: Params, g: PassGeometry,
                  keep_spectrogram: bool, transfer: tuple[np.ndarray, np.ndarray, np.ndarray] | None = None) -> dict:
    """Steps 2-7 at one STFT geometry. aligned=None selects --no-program mode."""
    n_fft, hop = g.n_fft, g.hop
    fmax = p.fmax_hz if p.fmax_hz is not None else 0.45 * fs
    n_bins = n_fft // 2 + 1
    freqs = np.arange(n_bins) * fs / n_fft
    band = (freqs >= p.fmin_hz) & (freqs <= fmax)
    total = n_frames(len(rec), n_fft, hop)
    if total == 0:
        raise ValueError("recording shorter than one STFT frame")
    times = (np.arange(total) * hop + n_fft / 2) / fs
    hop_s = hop / fs
    cal_frames = np.flatnonzero((times >= p.calibrate_s[0]) & (times <= p.calibrate_s[1]))
    if len(cal_frames) < 8:
        raise ValueError(f"calibration segment {p.calibrate_s} s holds only {len(cal_frames)} frames")

    out: dict = {}
    m = stft_power(rec, n_fft, hop, rate=g.rate, fs=fs)
    lo_w, hi_w = envelope_windows(n_bins, p.envelope_octaves, g.min_half_width_bins)
    grid = envelope_grid(n_bins)
    hold = aligned is not None and (p.rt60 is not None or p.chain_rt60 is not None)
    if aligned is not None:
        c_lo, c_hi = int(cal_frames[0]), int(cal_frames[-1]) + 1
        if transfer is None:
            p_cal = [stft_power(x, n_fft, hop, c_lo, c_hi, rate=g.rate, fs=fs) for x in aligned]
            a, noise, a_raw = fit_calibration(m[c_lo:c_hi], p_cal, band, p)
        else:  # a dechirped pass: the room's power transfer and noise floor are the plain long pass's
            a, noise, a_raw = transfer
        out["_transfer"] = (a, noise, a_raw)
        out["calibration"] = {
            "frames": int(c_hi - c_lo),
            "segment_s": [float(times[c_lo]), float(times[c_hi - 1])],
            "bins_with_transfer_fraction": [float(np.mean(a[i, band] > 0)) for i in range(len(aligned))],
            "bins_with_transfer_fraction_unsmoothed": [float(np.mean(a_raw[i, band] > 0)) for i in range(len(aligned))],
        }
    else:
        out["calibration"] = None
    if hold:
        rt_bins = rt60_per_bin(freqs, p.rt60) if p.rt60 is not None else np.zeros(n_bins)
        if p.chain_rt60 is not None:
            rt_bins = np.maximum(rt_bins, p.chain_rt60)
        rt_bins = rt_bins * p.rt60_scale
        decay = 10.0 ** (-6.0 * hop_s / np.maximum(rt_bins, 1e-3))
        size = (1, 2 * p.maxfilt_bins + 1)
        halo = 0
    else:
        size = (2 * g.maxfilt_frames + 1, 2 * p.maxfilt_bins + 1)
        halo = g.maxfilt_frames
    held = np.zeros(n_bins)

    peaks: list[list[tuple[float, float, float]]] = [[] for _ in range(total)]
    band_idx = np.flatnonzero(band)
    b_lo, b_hi = int(band_idx[0]), int(band_idx[-1]) + 1
    thr_excess = 10.0 ** (p.excess_db / 10.0)
    thr_peak = 10.0 ** (p.peak_db / 10.0)
    e_all = np.empty(m.shape, dtype=np.float32) if keep_spectrogram else None
    for c0 in range(0, total, CHUNK_FRAMES):
        c1 = min(total, c0 + CHUNK_FRAMES)
        mc = m[c0:c1].astype(np.float64)
        if aligned is not None:
            ex = np.zeros(mc.shape)
            h0, h1 = max(0, c0 - halo), min(total, c1 + halo)
            for i, x in enumerate(aligned):
                ps = stft_power(x, n_fft, hop, h0, h1, rate=g.rate, fs=fs)
                ps = scipy.ndimage.maximum_filter(ps, size=size, mode="nearest")
                ex += a[i] * ps[c0 - h0 : c0 - h0 + (c1 - c0)].astype(np.float64)
            if hold:
                for r in range(c1 - c0):
                    held = np.maximum(ex[r], held * decay)
                    ex[r] = held
            e = ex + noise
        else:
            e = grid_median(m[c0:c1], grid, lo_w, hi_w, NO_PROGRAM_EXCLUDE_BINS)
        e = np.maximum(e, 1e-38)
        if e_all is not None:
            e_all[c0:c1] = e
        cand = np.zeros(mc.shape, dtype=bool)
        cand[:, b_lo:b_hi] = mc[:, b_lo:b_hi] >= thr_excess * e[:, b_lo:b_hi]
        n_cand = cand.sum(axis=1)
        many = np.flatnonzero(n_cand > MAX_EXACT_CANDIDATES)
        env_many = dict(zip(many, grid_median(mc[many], grid, lo_w, hi_w, 0))) if many.size else {}
        for r in np.flatnonzero(n_cand):
            row = mc[r]
            ks = np.flatnonzero(cand[r])
            if r in env_many:
                env_row = env_many[r][ks]
            else:
                env_row = np.array([np.median(row[lo_w[k] : hi_w[k]]) for k in ks])
            flagged = ks[row[ks] >= thr_peak * np.maximum(env_row, 1e-38)]
            if flagged.size == 0:
                continue
            # flagged runs closer than drift_bins merge into one cluster -> one
            # peak, at the M-weighted centroid of the cluster's span
            breaks = np.flatnonzero(np.diff(flagged) > int(math.floor(g.drift_bins)) + 1) + 1
            for run in np.split(flagged, breaks):
                span = np.arange(run[0], run[-1] + 1)
                centroid = float(np.sum(span * row[span]) / np.sum(row[span]))
                exc = float(np.max(10.0 * np.log10(row[run] / e[r, run])))
                peaks[c0 + r].append((centroid, float(np.max(row[run])), exc))

    tracks = link_tracks(peaks, g.drift_bins, g.max_dropout_frames, g.rate * hop_s / (fs / n_fft))
    bin_hz = fs / n_fft
    events = []
    longest_short = 0.0
    for tr in tracks:
        dur = (tr["frames"][-1] - tr["frames"][0] + 1) * hop_s
        if dur < p.min_duration_s - 1e-9:
            longest_short = max(longest_short, dur)
            continue
        f = np.array(tr["bins"]) * bin_hz
        events.append(
            {
                "onset_s": float(times[tr["frames"][0]]),
                "end_s": float(times[tr["frames"][-1]]),
                "duration_s": float(dur),
                "f_start_hz": float(f[0]),
                "f_end_hz": float(f[-1]),
                "f_min_hz": float(f.min()),
                "f_max_hz": float(f.max()),
                "max_excess_db": float(max(tr["excess"])),
                "passes": [g.name],
                "rates_hz_per_s": [g.rate],
                "track": {
                    "t_s": [round(float(times[t]), 4) for t in tr["frames"]],
                    "f_hz": [round(float(v), 2) for v in f],
                    "excess_db": [round(v, 2) for v in tr["excess"]],
                },
            }
        )
    events.sort(key=lambda ev: ev["onset_s"])
    out["events"] = events
    out["n_flagged_tracks"] = len(tracks)
    out["longest_nonqualifying_track_s"] = float(longest_short)
    out["parameters"] = {
        "window": "hann",
        "n_fft": n_fft,
        "hop": hop,
        "bin_hz": bin_hz,
        "hop_s": hop_s,
        "drift_bins_per_hop": g.drift_bins,
        "max_dropout_frames": g.max_dropout_frames,
        "time_maxfilt_frames": 0 if hold else g.maxfilt_frames,
        "envelope_min_half_width_bins": g.min_half_width_bins,
        "reverberation_hold": hold,
        "dechirp_rate_hz_per_s": g.rate,
    }
    if keep_spectrogram:
        out["_spectrogram"] = {"m": m, "e": e_all, "times": times, "freqs": freqs, "tracks": tracks}
    return out


def analyze(
    rec: np.ndarray,
    fs: int,
    stems: list[np.ndarray] | None,
    p: Params,
    stem_names: list[str] | None = None,
    keep_spectrogram: bool = False,
) -> dict:
    """Apply the criterion to a mic recording. stems=None selects --no-program mode."""
    cal0, cal1 = p.calibrate_s
    seg = (int(round(cal0 * fs)), min(len(rec), int(round(cal1 * fs))))
    result: dict = {
        "fs": fs,
        "duration_s": len(rec) / fs,
        "mode": "no-program" if stems is None else "program",
        "warnings": [],
    }
    aligned: list[np.ndarray] | None = None
    alignment = []
    if stems is not None:
        aligned = []
        max_lag = int(round(p.max_lag_s * fs))
        names = [stem_names[i] if stem_names else f"stem{i}" for i in range(len(stems))]
        infos = [gcc_phat_lag(rec, s_, fs, seg, max_lag) for s_ in stems]
        anchor = int(np.argmax([info["gcc_phat_peak_to_rival"] for info in infos]))
        if p.joint_align and len(stems) > 1:
            win = int(round(JOINT_WINDOW_S * fs))
            a_lag = infos[anchor]["lag_samples"]
            for i, s_ in enumerate(stems):
                if i != anchor:
                    free = infos[i]
                    infos[i] = gcc_phat_lag(rec, s_, fs, seg, max_lag, (a_lag - win, a_lag + win))
                    infos[i]["independent_lag_samples"] = free["lag_samples"]
        for i, (info, s_) in enumerate(zip(infos, stems)):
            info["stem"] = names[i]
            info["anchor"] = bool(p.joint_align and len(stems) > 1 and i == anchor)
            alignment.append(info)
            aligned.append(shift(s_, info["lag_samples"], len(rec)))
            if info["gcc_phat_peak_to_rival"] < ALIGN_MIN_RIVAL_RATIO:
                where = f" within +/-{JOINT_WINDOW_S * 1000:.0f} ms of the anchor" if "independent_lag_samples" in info else ""
                result["warnings"].append(
                    f"alignment of {info['stem']} is ambiguous: GCC-PHAT peak at lag {info['lag_samples']} is only "
                    f"{info['gcc_phat_peak_to_rival']:.2f}x the best peak >= {ALIGN_RIVAL_S * 1000:.0f} ms away{where} "
                    f"(lag {info['rival_lag_samples']}); check the lag against the geometry")
        if p.rt60 is None:
            result["warnings"].append(NO_RT60_WARNING)
    result["alignment"] = alignment

    geoms = pass_geometries(fs, p)
    passes = {}
    events = []
    for g in geoms:
        shared = passes["long"].get("_transfer") if (g.rate and "long" in passes) else None
        pr = _analyze_pass(rec, fs, aligned, p, g, keep_spectrogram, shared)
        passes[g.name] = pr
        events.extend(pr["events"])
    tol_hz = max(fs / g.n_fft for g in geoms)
    merged = merge_events(events, tol_hz)
    # an event starting inside the calibration window cannot set a limit (the
    # calibration has absorbed part of it, and the gain there is the start gain)
    in_cal = [stems is not None and cal0 <= e["onset_s"] <= cal1 for e in merged]  # no-program mode has no calibration
    result["events"] = [e for e, c in zip(merged, in_cal) if not c]
    result["events_in_calibration"] = [e for e, c in zip(merged, in_cal) if c]
    long_pass = passes["long"]
    result["n_frames"] = n_frames(len(rec), geoms[0].n_fft, geoms[0].hop)
    result["calibration"] = long_pass["calibration"]
    if result["calibration"] is not None:
        names = [al["stem"] for al in alignment]
        result["calibration"]["stems"] = [
            {"stem": names[i],
             "bins_with_transfer_fraction": long_pass["calibration"]["bins_with_transfer_fraction"][i],
             "bins_with_transfer_fraction_unsmoothed":
                 long_pass["calibration"]["bins_with_transfer_fraction_unsmoothed"][i]}
            for i in range(len(names))]
    result["n_flagged_tracks"] = sum(pr["n_flagged_tracks"] for pr in passes.values())
    result["longest_nonqualifying_track_s"] = max(pr["longest_nonqualifying_track_s"] for pr in passes.values())
    result["passes"] = {
        name: {k: pr[k] for k in ("n_flagged_tracks", "longest_nonqualifying_track_s", "parameters", "calibration")}
        | {"n_events": len(pr["events"])}
        for name, pr in passes.items()
    }
    lp = long_pass["parameters"]
    result["parameters"] = {
        "window": "hann",
        "n_fft": lp["n_fft"],
        "hop": lp["hop"],
        "bin_hz": lp["bin_hz"],
        "hop_s": lp["hop_s"],
        "excess_db": p.excess_db,
        "peak_db": p.peak_db,
        "min_duration_s": p.min_duration_s,
        "drift_bins_per_hop": p.drift_bins,
        "max_dropout_frames": p.max_dropout_frames,
        "envelope_octaves": p.envelope_octaves,
        "envelope_min_half_width_bins": MIN_HALF_WIDTH_BINS,
        "maxfilt_bins": p.maxfilt_bins,
        "maxfilt_frames": 0 if lp["reverberation_hold"] else p.maxfilt_frames,
        "transfer_smooth_bins": p.smooth_bins,
        "noise_percentile": p.noise_percentile,
        "max_lag_s": p.max_lag_s,
        "calibrate_s": list(p.calibrate_s),
        "fmin_hz": p.fmin_hz,
        "fmax_hz": p.fmax_hz if p.fmax_hz is not None else 0.45 * fs,
        "rt60": p.rt60,
        "rt60_scale": p.rt60_scale,
        "chain_rt60_s": p.chain_rt60,
        "short_pass": any(g.name == "short" for g in geoms),
        "dechirp_rates_hz_per_s": [0.0] + [float(r) for r in p.dechirp_rates if r],
        "merge_tolerance_hz": tol_hz,
    }
    if keep_spectrogram:
        for name, pr in passes.items():
            result["_spectrogram" if name == "long" else f"_spectrogram_{name}"] = pr["_spectrogram"]
    return result


# --------------------------------------------------------------------------
# Gain mapping


@dataclass
class GainMap:
    kind: str
    times: np.ndarray
    gains: np.ndarray
    spec: dict

    def __call__(self, t: float) -> float:
        return float(np.interp(t, self.times, self.gains))


def ramp_map(start_db: float, rate_db_per_s: float, t0_s: float, t_end: float = 1e6) -> GainMap:
    t1 = max(t_end, t0_s + 1.0)
    return GainMap(
        "ramp",
        np.array([t0_s, t1]),
        np.array([start_db, start_db + rate_db_per_s * (t1 - t0_s)]),
        {"kind": "ramp", "start_db": start_db, "rate_db_per_s": rate_db_per_s, "t0_s": t0_s},
    )


def gain_log_map(path: str | pathlib.Path) -> GainMap:
    rows = []
    with open(path, newline="") as fh:
        for rec in csv.reader(fh):
            if len(rec) < 2:
                continue
            try:
                rows.append((float(rec[0]), float(rec[1])))
            except ValueError:
                continue
    if not rows:
        raise SystemExit(f"{path}: no numeric time_s,gain_db rows")
    rows.sort(key=lambda r: r[0])
    arr = np.array(rows)
    return GainMap("gain-log", arr[:, 0], arr[:, 1], {"kind": "gain-log", "path": str(path), "rows": len(rows)})


def warmup_segment(gmap: GainMap | None, default_s: float = 30.0, min_s: float = 5.0,
                   tol_db: float = 0.05) -> tuple[float, float]:
    """Calibration default: the gain map's initial constant-gain segment, else 0..default_s."""
    if gmap is None:
        return (0.0, default_s)
    if gmap.kind == "ramp":
        end = float(gmap.times[0])  # T0: the ramp starts there
    else:
        moved = np.flatnonzero(np.abs(gmap.gains - gmap.gains[0]) > tol_db)
        # interpolation leaves the start gain right after the last row that still holds it
        end = float(gmap.times[moved[0] - 1]) if moved.size else float(gmap.times[-1])
    if end < min_s:
        raise SystemExit(f"the gain map holds its start gain for only {end:.2f} s (< {min_s} s): give --calibrate")
    return (0.0, end)


def apply_gain_map(result: dict, gmap: GainMap | None) -> dict:
    result["gain_map"] = None if gmap is None else gmap.spec
    for ev in result["events"]:
        ev["gain_at_onset_db"] = None if gmap is None else gmap(ev["onset_s"])
    first = result["events"][0] if result["events"] else None
    result["limit_onset_s"] = None if first is None else first["onset_s"]
    result["limit_db"] = None if (first is None or gmap is None) else first["gain_at_onset_db"]
    return result


def aggregate(runs: list[dict], names: list[str] | None = None) -> dict:
    limits = [r.get("limit_db") for r in runs]
    have = np.array([v for v in limits if v is not None], dtype=np.float64)
    out = {
        "runs": [
            {"run": (names[i] if names else i), "limit_db": limits[i], "limit_onset_s": runs[i].get("limit_onset_s")}
            for i in range(len(runs))
        ],
        "n_runs": len(runs),
        "n_with_limit": int(have.size),
        "median_db": float(np.median(have)) if have.size else None,
        "min_db": float(have.min()) if have.size else None,
        "max_db": float(have.max()) if have.size else None,
        "range_db": float(have.max() - have.min()) if have.size else None,
    }
    return out


# --------------------------------------------------------------------------
# Plot


def plot(result: dict, path: str, gmap: GainMap | None, fmax_plot: float | None) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    spec = result["_spectrogram"]
    m, times, freqs = spec["m"], spec["times"], spec["freqs"]
    bin_hz = freqs[1]
    fmax = fmax_plot or min(8000.0, result["parameters"]["fmax_hz"])
    kmax = int(min(len(freqs) - 1, fmax / bin_hz))
    step = max(1, m.shape[0] // 3000)
    img = 10.0 * np.log10(np.maximum(m[::step, : kmax + 1].T, 1e-30))
    top = np.percentile(img, 99.9)
    rows = 2 if gmap is not None else 1
    fig, axes = plt.subplots(rows, 1, figsize=(14, 8 if rows == 2 else 6), sharex=True, squeeze=False,
                             gridspec_kw={"height_ratios": [4, 1][:rows]})
    ax = axes[0][0]
    ax.imshow(img, origin="lower", aspect="auto", cmap="magma", vmin=top - 90, vmax=top,
              extent=[times[0], times[-1], 0, kmax * bin_hz])
    hop_s = result["parameters"]["hop_s"]
    for tr in spec["tracks"]:
        dur = (tr["frames"][-1] - tr["frames"][0] + 1) * hop_s
        if dur < result["parameters"]["min_duration_s"]:
            ax.plot(times[tr["frames"]], np.array(tr["bins"]) * bin_hz, color="cyan", lw=0.8, alpha=0.7)
    for ev in result["events"]:
        ax.plot(ev["track"]["t_s"], ev["track"]["f_hz"], color="lime" if "long" in ev["passes"] else "orange", lw=1.8)
        if ev.get("rates_hz_per_s") and any(ev["rates_hz_per_s"]):
            ax.annotate(",".join(f"{r:+g}" for r in ev["rates_hz_per_s"]) + " Hz/s", (ev["onset_s"], ev["f_start_hz"]),
                        color="orange", fontsize=7)
        ax.axvline(ev["onset_s"], color="lime", ls="--", lw=0.8)
    ax.set_ylabel("Hz")
    title = f"{result.get('recording', '')}  mode={result['mode']}  events={len(result['events'])}"
    if result.get("limit_db") is not None:
        title += f"  limit={result['limit_db']:.2f} dB at {result['limit_onset_s']:.2f} s"
    ax.set_title(title + "\n(cyan: flagged by the long pass, too short; green: qualifying events found by the plain "
                 "long pass; orange: found only by the short pass or a dechirped rate)")
    if gmap is not None:
        gx = axes[1][0]
        gx.plot(times, [gmap(t) for t in times], color="k")
        if result.get("limit_db") is not None:
            gx.axhline(result["limit_db"], color="g", ls="--")
            gx.axvline(result["limit_onset_s"], color="g", ls="--")
        gx.set_ylabel("bus gain dB")
        gx.set_xlabel("s")
    else:
        ax.set_xlabel("s")
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


# --------------------------------------------------------------------------
# CLI


def parse_pair(text: str, n: int, what: str) -> tuple[float, ...]:
    try:
        vals = tuple(float(v) for v in text.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{what}: expected {n} comma-separated numbers") from exc
    if len(vals) != n:
        raise argparse.ArgumentTypeError(f"{what}: expected {n} comma-separated numbers")
    return vals


def summarize(result: dict) -> str:
    lines = [f"recording: {result.get('recording', '-')}  fs {result['fs']}  "
             f"{result['duration_s']:.2f} s  mode {result['mode']}"]
    lines += [f"WARNING: {w}" for w in result.get("warnings", [])]
    rt = result["parameters"].get("rt60")
    if result["parameters"].get("chain_rt60_s") is not None:
        lines.append(f"  chain reverb in the hold: {result['parameters']['chain_rt60_s']:.3f} s")
    if rt is not None:
        lines.append(f"  reverberation hold: RT60 {', '.join(f'{f}:{v:.3f}' for f, v in zip(rt['bands_hz'], rt['rt60_s']))} s"
                     f" x {result['parameters']['rt60_scale']} (from {rt['source']})")
    for al in result["alignment"]:
        lines.append(f"  aligned {al['stem']}: lag {al['lag_samples']} samples ({al['lag_s'] * 1000:.2f} ms), "
                     f"PHAT peak/median {al['gcc_phat_peak_to_median']:.1f}, peak/rival {al['gcc_phat_peak_to_rival']:.2f}")
    for name, ps in result.get("passes", {}).items():
        pp = ps["parameters"]
        lines.append(f"  {name} pass (N {pp['n_fft']}, hop {pp['hop']}, hold {'on' if pp['reverberation_hold'] else 'off'}): "
                     f"flagged tracks {ps['n_flagged_tracks']}, events {ps['n_events']}, "
                     f"longest non-qualifying {ps['longest_nonqualifying_track_s'] * 1000:.0f} ms")
    cal = result.get("calibration")
    if cal is not None:
        seg = cal["segment_s"]
        lines.append(f"  calibration {seg[0]:.2f}-{seg[1]:.2f} s; analysis bins with a fitted transfer (unsmoothed): "
                     + ", ".join(f"{pathlib.Path(st['stem']).name} {100 * st['bins_with_transfer_fraction_unsmoothed']:.1f}%"
                                 for st in cal["stems"]))
    for ev in result.get("events_in_calibration", []):
        lines.append(f"  REJECTED (onset inside the calibration window): {ev['onset_s']:.3f} s "
                     f"{ev['f_start_hz']:.1f} Hz {ev['duration_s']:.3f} s")
    lines.append(f"  qualifying events after merging passes: {len(result['events'])}")
    for ev in result["events"]:
        g = ev.get("gain_at_onset_db")
        gtxt = "" if g is None else f"  gain {g:.2f} dB"
        lines.append(f"  event onset {ev['onset_s']:.3f} s  {ev['duration_s']:.3f} s  "
                     f"{ev['f_start_hz']:.1f} -> {ev['f_end_hz']:.1f} Hz  max excess {ev['max_excess_db']:.1f} dB"
                     f"  [{'+'.join(ev['passes'])}]{gtxt}")
    if result.get("limit_db") is not None:
        lines.append(f"LIMIT {result['limit_db']:.2f} dB (onset {result['limit_onset_s']:.3f} s)")
    elif result["events"]:
        lines.append(f"first event onset {result['limit_onset_s']:.3f} s (no gain map: no limit)")
    else:
        lines.append("no qualifying event: no limit reached")
    return "\n".join(lines)


def cmd_analyze(args: argparse.Namespace) -> int:
    if args.no_program == bool(args.program):
        raise SystemExit("give one or more --program stems, or --no-program (noise-excited runs only)")
    fs, rec = read_wav(args.recording, args.channel)
    stems = None
    names = None
    if args.program:
        stems, names = [], []
        for path in args.program:
            sfs, s = read_wav(path)
            stems.append(resample_to(s, sfs, fs))
            names.append(str(path))
    gmap = None
    if args.ramp is not None:
        gmap = ramp_map(*args.ramp)
    elif args.gain_log is not None:
        gmap = gain_log_map(args.gain_log)
    calibrate = tuple(args.calibrate) if args.calibrate is not None else warmup_segment(gmap)
    p = Params(
        excess_db=args.excess_db,
        peak_db=args.peak_db,
        min_duration_s=args.min_duration,
        drift_bins=args.drift_bins,
        max_dropout_frames=args.max_dropout,
        max_lag_s=args.max_lag,
        calibrate_s=calibrate,
        fmin_hz=args.fmin,
        fmax_hz=args.fmax,
        n_fft=args.n_fft,
        hop=args.hop,
        rt60=None if args.rt60 is None else load_rt60(args.rt60, args.rt60_channel),
        rt60_scale=args.rt60_scale,
        chain_rt60=args.chain_rt60,
        joint_align=args.joint_align,
        dechirp_rates=DECHIRP_BANK if args.dechirp else (),
        short_pass=args.short_pass,
    )
    result = analyze(rec, fs, stems, p, names, keep_spectrogram=args.plot is not None)
    result = {"tool": "howl_criterion", "recording": str(args.recording), "channel": args.channel, **result}
    apply_gain_map(result, gmap)
    if args.plot:
        plot(result, args.plot, gmap, args.plot_fmax)
    for key in [k for k in result if k.startswith("_spectrogram")]:
        result.pop(key)
    out = pathlib.Path(args.json) if args.json else pathlib.Path(str(args.recording) + ".howl.json")
    out.write_text(json.dumps(result, indent=1) + "\n")
    print(summarize(result))
    print(f"wrote {out}")
    return 0


def cmd_aggregate(args: argparse.Namespace) -> int:
    runs = [json.loads(pathlib.Path(p).read_text()) for p in args.runs]
    agg = aggregate(runs, [str(p) for p in args.runs])
    for r in agg["runs"]:
        v = r["limit_db"]
        print(f"  {r['run']}: " + ("no limit" if v is None else f"{v:.2f} dB"))
    if agg["median_db"] is None:
        print("no run reached a limit")
    else:
        print(f"median {agg['median_db']:.2f} dB, range {agg['min_db']:.2f} .. {agg['max_db']:.2f} dB "
              f"({agg['range_db']:.2f} dB) over {agg['n_with_limit']} of {agg['n_runs']} runs")
        if agg["n_with_limit"] < agg["n_runs"]:
            print("WARNING: some runs reached no limit; the median covers only the runs that did")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(agg, indent=1) + "\n")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    an = sub.add_parser("analyze", help="apply the criterion to one mic recording")
    an.add_argument("recording", help="the chain's output e before the bus gain (speaker feed / G(t)); "
                    "with the chain bypassed, the mic. The mic of a chain-on run is a cross-check only")
    an.add_argument("--channel", type=int, default=None, help="mic channel of a multichannel recording")
    an.add_argument("--program", action="append", default=[], help="dry program stem (repeatable)")
    an.add_argument("--no-program", action="store_true",
                    help="no stems: envelope = own 1/3-octave median; FALSE-TRIGGERS ON HELD NOTES, noise-excited runs only")
    an.add_argument("--calibrate", type=lambda s: parse_pair(s, 2, "--calibrate"), default=None,
                    metavar="START,END", help="calibration segment in s (default: the gain map's constant-gain "
                    "warm-up, else 0,30)")
    g = an.add_mutually_exclusive_group()
    g.add_argument("--ramp", type=lambda s: parse_pair(s, 3, "--ramp"), default=None,
                   metavar="START_DB,RATE_DB_PER_S,T0_S")
    g.add_argument("--gain-log", default=None, metavar="CSV", help="rows time_s,gain_db")
    an.add_argument("--excess-db", type=float, default=20.0)
    an.add_argument("--peak-db", type=float, default=10.0)
    an.add_argument("--min-duration", type=float, default=0.5, help="s")
    an.add_argument("--drift-bins", type=int, default=3, help="per hop")
    an.add_argument("--max-dropout", type=int, default=1, help="frames")
    an.add_argument("--max-lag", type=float, default=2.0, help="alignment lag window, s")
    an.add_argument("--fmin", type=float, default=40.0)
    an.add_argument("--fmax", type=float, default=None, help="default 0.45 fs")
    an.add_argument("--n-fft", type=int, default=None, help="default 8192 at 48 kHz, scaled with fs")
    an.add_argument("--hop", type=int, default=None, help="default 1024 at 48 kHz, scaled with fs")
    an.add_argument("--rt60", default=None, metavar="SECONDS|REPORT.json",
                    help="reverberation hold: RT60 in s, or measure_rir.py deconvolve's report JSON")
    an.add_argument("--rt60-channel", default=None, help="channel of the RT60 report (default first acoustic)")
    an.add_argument("--rt60-scale", type=float, default=1.0, help="safety factor on the hold's decay time")
    an.add_argument("--chain-rt60", type=float, default=None, metavar="SECONDS",
                    help="RT60 of reverb inside the chain; the hold uses max(room, chain) per band")
    an.add_argument("--independent-align", dest="joint_align", action="store_false", default=True,
                    help="align every stem on its own over the whole lag window (default: joint, see step 1)")
    an.add_argument("--short-pass", dest="short_pass", action="store_true", default=None,
                    help="also run the N=2048 pass for drifting howls (default: on, unless --dechirp)")
    an.add_argument("--no-short-pass", dest="short_pass", action="store_false")
    an.add_argument("--dechirp", action="store_true",
                    help="repeat the long pass dechirped at +/-250, +/-500, +/-800 Hz/s (drifting howls, e.g. a "
                    "frequency shifter in the loop; ~4.6x the analysis cost). Recommended for shift-condition runs")
    an.add_argument("--json", default=None, help="output JSON (default REC.howl.json)")
    an.add_argument("--plot", default=None, metavar="PNG")
    an.add_argument("--plot-fmax", type=float, default=None, help="plot frequency limit (default 8 kHz)")
    an.set_defaults(func=cmd_analyze)
    ag = sub.add_parser("aggregate", help="median and range of several runs' limits")
    ag.add_argument("runs", nargs="+")
    ag.add_argument("--json", default=None)
    ag.set_defaults(func=cmd_aggregate)
    # "--ramp -20,0.5,0" would read as an option to argparse; join it.
    raw = list(sys.argv[1:] if argv is None else argv)
    joined: list[str] = []
    i = 0
    while i < len(raw):
        if raw[i] in ("--ramp", "--calibrate") and i + 1 < len(raw) and raw[i + 1][:1] == "-" and raw[i + 1][1:2].isdigit():
            joined.append(f"{raw[i]}={raw[i + 1]}")
            i += 2
        else:
            joined.append(raw[i])
            i += 1
    args = ap.parse_args(joined)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
