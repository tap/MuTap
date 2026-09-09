#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The M5 pass for the evaluation harness (wake-word plan §6 M5), items 1-4 of the M5 brief.

    .venv/bin/python -m unittest tools/ml/kws/test_kws_eval.py                # from the repo root
    .venv/bin/python -m unittest discover -s tools/ml/kws -p 'test_*.py' -v   # the kws-dataset CI job

1. The planted-event oracle: synthetic silent streams scored by a PlantedDetector, with known positive
   endpoints and known event placements, against hand-computed recall and FA/h, exact to the utterance —
   the per-utterance hit map asserted through the harness's own decide/hits path, with events inside a
   window, at its two inclusive edges and one hop outside each, the plants deliberately not mirror-symmetric
   about the window (one at the lower edge and one below it, two at the upper edge and two above it), so a
   rigid shift of the window in either direction changes the aggregates as well as the map; negative streams
   totalling exactly 0.5 h with four planted events of which two fall inside one refractory period (3
   counted -> 6.0 FA/h, Poisson [1.237, 17.535]); the refractory boundary as a number (a crossing exactly R
   hops after an event fires, R - 1 merges); one hit per utterance when two events fall in one window; a
   share with zero events reporting ln 20 / H; a positive-stream event outside every window counted as
   spurious and never as a false accept; absent shares reported absent; the self-checks of kws_scoring and
   kws_detectors run here so their hand-computed edge values are part of CI.
2. A deliberately mis-accounted variant is refused by name: a negative stream whose declared
   negative_samples differs from its audio length (or is not an integer); a positive whose endpoint lies
   beyond its stream (and one whose window ends beyond its hops); duplicate stream ids; a positive id used
   twice, within one stream or across two; a positive stream with non-zero (or negative) negative_samples —
   each by `kws_eval.check_streams` / `evaluate` and by `kws_streams.validate_streams` (one rule set:
   the harness delegates to `kws_streams.validate_stream`). Scores handed to `evaluate` are re-checked
   (NaN, a wrong length, a value above 1, an unknown stream id); a detector returning a complex or bool
   array is refused, not cast; thresholds outside [0, 1] or repeated are refused.
3. The report round-trips (to_dict -> JSON -> from_dict, and through report.json on disk) and the markdown
   carries every figure with its hours; `from_dict` refuses an unknown field and a wrong version by name.
4. Toy end to end: the toy rebuilt into a temporary store (as test_kws does), streams assembled from the
   committed expected lock, the BandEnergyBaseline through the bridge, a sweep; the hours equal the sum of
   the eval negatives' decoded lengths (bit-identical to `hours_per_share` and to the lock's summary), the
   positives count the eval positives, and the report's eval_set_ids equal the lock's; the `sweep` CLI on
   the same store writes a report carrying the lock's eval_set_id and the packing bound, and refuses a
   `--grid` of -1 or 1 by name.

Every expected figure below is computed by hand in the comment beside its assertion (z = 1.959964 for the
Wilson interval; chi-square quantiles chi2(0.025, 6) = 1.2373, chi2(0.975, 8) = 17.5345, chi2(0.975, 2) =
7.3778) and asserted to 5e-4, the hand precision — the harness's own arithmetic is never the reference.
"""
from __future__ import annotations

import contextlib
import dataclasses
import io
import json
import pathlib
import sys
import tempfile
import unittest
import warnings
from typing import Callable

import numpy as np
import soundfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import kws_build  # noqa: E402
import kws_detectors  # noqa: E402
import kws_eval  # noqa: E402
import kws_features  # noqa: E402
import kws_scoring  # noqa: E402
import kws_streams  # noqa: E402
import test_kws  # noqa: E402
from kws_detectors import PlantedDetector  # noqa: E402
from kws_eval import EvalError, Report, check_streams, default_thresholds, evaluate  # noqa: E402
from kws_manifest import load_manifest, read_lock  # noqa: E402
from kws_scoring import Positive, Scoring  # noqa: E402
from kws_store import Store  # noqa: E402
from kws_streams import Stream, StreamError, validate_streams  # noqa: E402

RATE = 16000
HOP = 160
TOL = 5e-4  # the hand precision of the figures in the comments


def silent(n: int) -> Callable[[], np.ndarray]:
    """A lazily materialised silent stream (zeros are never touched, so a 0.5 h stream costs no RSS)."""
    return lambda: np.zeros(n, dtype=np.float64)


def positive_stream(stream_id: str, endpoints: list[int], seconds: float, scoring: Scoring,
                    subshare: str = "eval-speech") -> Stream:
    n = int(seconds * RATE)
    positives = [Positive(id=f"{stream_id}#{i}", endpoint_sample=e, endpoint_hop=scoring.endpoint_hop(e))
                 for i, e in enumerate(endpoints)]
    return Stream(id=stream_id, share="positives", positives=positives, negative_samples=0, subshare=subshare,
                  load=silent(n))


def negative_stream(stream_id: str, share: str, seconds: float) -> Stream:
    n = int(seconds * RATE)
    return Stream(id=stream_id, share=share, positives=[], negative_samples=n, load=silent(n))


# The oracle. Scoring: hop 160 at 16 kHz, T = 3, L = 20, W = 1 (the smoothed score is the raw score, so a
# planted 1.0 at hop t is an upward crossing at exactly t for every threshold in (0, 1]), R = 100.
# Positive streams are 4 s = 64000 samples = 400 hops; endpoint 24000 -> h = 24000 // 160 = 150, window
# [150 - 3, 150 + 20 + 3] = [147, 173]. The second utterance of "pos/two": endpoint 48000 -> h = 300,
# window [297, 323]. The plants around the window are not a mirror image of it: one at the lower edge and
# one below, two at the upper edge and two above, so shifting the window by k hops in either direction
# (any |k| <= 13, where "inside" leaves it) changes the hit count — a mirror-symmetric set would trade one
# edge hit for one outside hit and leave every aggregate unchanged.
ORACLE_SCORING = Scoring(hop=HOP, sample_rate=RATE, tolerance_hops=3, latency_hops=20, smoothing_hops=1,
                         refractory_hops=100)
ORACLE_PLANTED: dict[str, list[tuple[int, float]]] = {
    "pos/inside": [(160, 1.0)],            # inside [147, 173]                          -> hit
    "pos/edge-lo": [(147, 1.0)],           # the lower inclusive edge                   -> hit
    "pos/edge-hi": [(173, 1.0)],           # the upper inclusive edge                   -> hit
    "pos/edge-hi-2": [(173, 1.0)],         # the upper inclusive edge again             -> hit
    "pos/below": [(146, 1.0)],             # one hop below the window                   -> miss, spurious
    "pos/above": [(174, 1.0)],             # one hop above the window                   -> miss, spurious
    "pos/above-2": [(174, 1.0)],           # one hop above the window again             -> miss, spurious
    "pos/inside+late": [(160, 1.0), (300, 1.0)],  # a hit, then an event 140 hops later (past R) outside
                                                  # every window                         -> hit, spurious
    "pos/silent": [],                      # no event                                   -> miss
    "pos/two": [(160, 1.0), (310, 1.0)],   # two utterances, one event in each window    -> 2 hits
    # eval-speech: 1000 s + 800 s = 1800 s = 0.5 h exactly; events at 1000, 1050 (50 hops after the
    # first: inside R = 100, merged), 5000 and 20000 -> 4 planted, 3 counted
    "eval-speech/stream-0000": [(1000, 1.0), (1050, 1.0), (5000, 1.0)],
    "eval-speech/stream-0001": [(20000, 1.0)],
    # eval-music: 900 s = 0.25 h, no event
    "eval-music/stream-0000": [],
}
# the hit map the oracle must reproduce, utterance by utterance, at every threshold in (0, 1]
ORACLE_HITS = {"pos/inside#0": True, "pos/edge-lo#0": True, "pos/edge-hi#0": True, "pos/edge-hi-2#0": True,
               "pos/below#0": False, "pos/above#0": False, "pos/above-2#0": False, "pos/inside+late#0": True,
               "pos/silent#0": False, "pos/two#0": True, "pos/two#1": True}
ORACLE_POSITIVE_STREAMS = 10   # 11 utterances (pos/two carries two) on 10 streams


def oracle_streams(scoring: Scoring = ORACLE_SCORING) -> list[Stream]:
    return [positive_stream("pos/inside", [24000], 4.0, scoring),
            positive_stream("pos/edge-lo", [24000], 4.0, scoring),
            positive_stream("pos/edge-hi", [24000], 4.0, scoring),
            positive_stream("pos/edge-hi-2", [24000], 4.0, scoring),
            positive_stream("pos/below", [24000], 4.0, scoring),
            positive_stream("pos/above", [24000], 4.0, scoring),
            positive_stream("pos/above-2", [24000], 4.0, scoring),
            positive_stream("pos/inside+late", [24000], 4.0, scoring),
            positive_stream("pos/silent", [24000], 4.0, scoring),
            positive_stream("pos/two", [24000, 48000], 8.0, scoring, subshare="holdout"),
            negative_stream("eval-speech/stream-0000", "eval-speech", 1000.0),
            negative_stream("eval-speech/stream-0001", "eval-speech", 800.0),
            negative_stream("eval-music/stream-0000", "eval-music", 900.0)]


SPEECH_0, SPEECH_1, MUSIC_0 = 10, 11, 12   # indices of the negative streams in oracle_streams()


def oracle_report(thresholds: list[float] = (0.0, 0.5, 1.0)) -> Report:
    streams = oracle_streams()
    return evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, list(thresholds))


class PlantedEventOracle(unittest.TestCase):
    """Item 1."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.report = oracle_report()

    def test_hours_and_counts(self) -> None:
        r = self.report
        # 1000 s + 800 s = 28 800 000 samples / 16000 / 3600 = 0.5 h; 900 s = 0.25 h; TTS and noise absent
        self.assertEqual(r.hours, {"eval-speech": 0.5, "eval-music": 0.25})
        self.assertEqual(r.negative_streams, {"eval-speech": 2, "eval-music": 1})
        self.assertNotIn("eval-tts", r.hours)
        self.assertNotIn("eval-noise", r.hours)
        # 9 utterances from eval-speech streams, 2 from the hold-out stream
        self.assertEqual(r.positives, {"eval-speech": 9, "holdout": 2})
        self.assertEqual([row.threshold for row in r.rows], [0.0, 0.5, 1.0])
        self.assertEqual(r.scoring, ORACLE_SCORING.to_dict())
        self.assertEqual(r.detector["name"], "planted")
        self.assertIsNone(r.max_stream_s)   # hand-built streams: no packing bound to record

    def test_the_hit_map_utterance_by_utterance(self) -> None:
        # the harness's own path — score_stream -> decide -> hits — reproduces ORACLE_HITS for every
        # positive stream at thresholds 0.5 and 1.0, and is exactly the window's inclusive edges: 147 and
        # 173 hit, 146 and 174 do not
        det = PlantedDetector(ORACLE_PLANTED, HOP)
        for theta in (0.5, 1.0):
            got: dict[str, bool] = {}
            for s in oracle_streams():
                if s.share != "positives":
                    continue
                ev = kws_scoring.decide(det.score_stream(s.id, s.samples().size), ORACLE_SCORING, theta)
                got.update(kws_scoring.hits(ev, s.positives, ORACLE_SCORING))
            self.assertEqual(got, ORACLE_HITS, theta)
            for value in got.values():
                self.assertIs(type(value), bool)  # one hit per utterance, never a count
        # and the window itself, as numbers
        self.assertEqual(ORACLE_SCORING.hit_window(150), (147, 173))

    def test_recall_exact_to_the_utterance(self) -> None:
        for row in self.report.rows[1:]:  # thresholds 0.5 and 1.0 see the same crossings (planted score 1.0)
            # hits: inside, edge-lo, edge-hi, edge-hi-2, inside+late, two (x2) = 7 of 11;
            # misses: below, above, above-2, silent
            self.assertEqual((row.recall.hits, row.recall.positives), (7, 11), row.threshold)
            # 7/11 = 0.636364; Wilson: z^2/n = 3.841459/11 = 0.349224, denom = 1.349224,
            # centre = (0.636364 + 0.174612) / 1.349224 = 0.601070,
            # half = 1.959964 * sqrt(0.231405/11 + 3.841459/484) / 1.349224 = 1.959964 * 0.170218 / 1.349224
            #      = 0.247269 -> [0.353801, 0.848339]
            self.assertAlmostEqual(row.recall.recall, 7 / 11, places=12)
            self.assertAlmostEqual(row.recall.frr, 4 / 11, places=12)
            self.assertAlmostEqual(row.recall.interval[0], 0.35380, delta=TOL)
            self.assertAlmostEqual(row.recall.interval[1], 0.84834, delta=TOL)
            # spurious: below, above, above-2, and the late event of inside+late — never in any FA/h
            self.assertEqual(row.spurious, 4)
            # per subshare: eval-speech 5/9 (Wilson: z^2/n = 0.426829, denom 1.426829, centre
            # (0.555556 + 0.213414) / 1.426829 = 0.538930, half = 1.959964 * sqrt(0.246914/9 + 3.841459/324)
            # / 1.426829 = 1.959964 * 0.198219 / 1.426829 = 0.272285 -> [0.266645, 0.811215]);
            # hold-out 2/2 = 1.0 (z^2/n = 1.920729, denom 2.920729, centre 0.671191, half = 1.959964 *
            # sqrt(3.841459/16) / 2.920729 = 0.328810 -> [0.342381, 1.0])
            self.assertEqual(set(row.by_subshare), {"eval-speech", "holdout"})
            speech, hold = row.by_subshare["eval-speech"], row.by_subshare["holdout"]
            self.assertEqual((speech.hits, speech.positives, hold.hits, hold.positives), (5, 9, 2, 2))
            self.assertAlmostEqual(speech.interval[0], 0.26665, delta=TOL)
            self.assertAlmostEqual(speech.interval[1], 0.81122, delta=TOL)
            self.assertAlmostEqual(hold.interval[0], 0.34238, delta=TOL)
            self.assertEqual(hold.interval[1], 1.0)
            self.assertEqual(hold.recall, 1.0)

    def test_false_accepts_per_hour(self) -> None:
        for row in self.report.rows[1:]:
            self.assertEqual(set(row.shares), {"eval-speech", "eval-music"})
            speech = row.shares["eval-speech"]
            # 4 planted, 1050 merged into 1000's refractory period -> 3 events in 0.5 h = 6.0 FA/h;
            # exact Poisson 95 %: chi2(0.025, 6)/2/0.5 = 1.2373/1 = 1.2373, chi2(0.975, 8)/2/0.5 = 17.5345
            self.assertEqual((speech.events, speech.hours, speech.streams), (3, 0.5, 2))
            self.assertEqual(speech.fa_per_hour, 6.0)
            self.assertAlmostEqual(speech.interval[0], 1.2373, delta=TOL)
            self.assertAlmostEqual(speech.interval[1], 17.5345, delta=TOL)
            self.assertIsNone(speech.zero_event_bound)
            music = row.shares["eval-music"]
            # 0 events in 0.25 h: one-sided bound ln 20 / 0.25 = 2.995732 / 0.25 = 11.98293; the two-sided
            # interval [0, chi2(0.975, 2)/2/0.25] = [0, 7.3778/0.5] = [0, 14.7555]
            self.assertEqual((music.events, music.hours, music.streams, music.fa_per_hour), (0, 0.25, 1, 0.0))
            self.assertAlmostEqual(music.zero_event_bound, 11.9829, delta=TOL)
            self.assertEqual(music.interval[0], 0.0)
            self.assertAlmostEqual(music.interval[1], 14.7555, delta=TOL)

    def test_the_refractory_boundary_is_a_number(self) -> None:
        # decide: a crossing exactly R = 100 hops after an event fires, one R - 1 hops after it merges
        x = np.zeros(400)
        x[10], x[110] = 1.0, 1.0
        self.assertEqual(kws_scoring.decide(x, ORACLE_SCORING, 0.5), [10, 110])
        x = np.zeros(400)
        x[10], x[109] = 1.0, 1.0
        self.assertEqual(kws_scoring.decide(x, ORACLE_SCORING, 0.5), [10])
        # and through the harness, on its own negative stream: 900 s = 0.25 h of eval-noise
        for hops, n_events, fa, lo, hi in (
                # 2 events in 0.25 h = 8.0 FA/h: chi2(0.025, 4)/2/0.25 = 0.48442/0.5 = 0.96884,
                # chi2(0.975, 6)/2/0.25 = 14.44938/0.5 = 28.8988
                ((1000, 1100), 2, 8.0, 0.96884, 28.8988),
                # 1 event (1099 merges into 1000's period): 4.0 FA/h, chi2(0.025, 2)/2/0.25 = 0.10127,
                # chi2(0.975, 4)/2/0.25 = 22.2866
                ((1000, 1099), 1, 4.0, 0.10127, 22.2866)):
            streams = [negative_stream("eval-noise/stream-0000", "eval-noise", 900.0)]
            det = PlantedDetector({"eval-noise/stream-0000": [(h, 1.0) for h in hops]}, HOP)
            noise = evaluate(streams, det, ORACLE_SCORING, [0.5]).rows[0].shares["eval-noise"]
            self.assertEqual((noise.events, noise.hours, noise.fa_per_hour), (n_events, 0.25, fa), hops)
            self.assertAlmostEqual(noise.interval[0], lo, delta=TOL)
            self.assertAlmostEqual(noise.interval[1], hi, delta=TOL)

    def test_one_hit_per_utterance_when_two_events_fall_in_one_window(self) -> None:
        # R = 5: pos/inside carries events at 160 and 170, both inside [147, 173] and both decided (10 hops
        # apart > R); the utterance still counts one hit: 7 of 11, subshares unchanged, spurious unchanged.
        # On eval-speech the events at 1000 and 1050 no longer merge: 4 events in 0.5 h.
        sc = dataclasses.replace(ORACLE_SCORING, refractory_hops=5)
        planted = {**ORACLE_PLANTED, "pos/inside": [(160, 1.0), (170, 1.0)]}
        det = PlantedDetector(planted, sc.hop)
        ev = kws_scoring.decide(det.score_stream("pos/inside", 64000), sc, 0.5)
        self.assertEqual(ev, [160, 170])
        inside = next(s for s in oracle_streams(sc) if s.id == "pos/inside")
        got = kws_scoring.hits(ev, inside.positives, sc)
        self.assertEqual(got, {"pos/inside#0": True})
        self.assertIs(got["pos/inside#0"], True)
        row = evaluate(oracle_streams(sc), det, sc, [0.5]).rows[0]
        self.assertEqual((row.recall.hits, row.recall.positives, row.spurious), (7, 11, 4))
        self.assertEqual((row.by_subshare["eval-speech"].hits, row.by_subshare["holdout"].hits), (5, 2))
        self.assertEqual(row.shares["eval-speech"].events, 4)

    def test_threshold_zero_fires_once_per_stream_at_hop_0(self) -> None:
        # At theta = 0 the smoothed score is >= 0 from hop 0 on: one event at hop 0 per stream, then no
        # further upward crossing. Hop 0 lies inside no window here (every window starts at 147 or 297) ->
        # 0 hits, one spurious event per positive stream (10); one event per negative stream: 2 in 0.5 h
        # = 4.0 FA/h, 1 in 0.25 h = 4.0 FA/h — the stream count over H, the ceiling of what the packing
        # bound can add to any row.
        row = self.report.rows[0]
        self.assertEqual(row.threshold, 0.0)
        self.assertEqual((row.recall.hits, row.recall.positives, row.spurious),
                         (0, 11, ORACLE_POSITIVE_STREAMS))
        self.assertEqual(row.recall.recall, 0.0)
        self.assertEqual((row.shares["eval-speech"].events, row.shares["eval-speech"].fa_per_hour), (2, 4.0))
        self.assertEqual((row.shares["eval-music"].events, row.shares["eval-music"].fa_per_hour), (1, 4.0))
        # 1 event in 0.25 h: chi2(0.025, 2)/2/0.25 = 0.050636/0.5 = 0.10127; chi2(0.975, 4)/2/0.25 =
        # 11.14329/0.5 = 22.2866
        self.assertAlmostEqual(row.shares["eval-music"].interval[0], 0.10127, delta=TOL)
        self.assertAlmostEqual(row.shares["eval-music"].interval[1], 22.2866, delta=TOL)
        self.assertIsNone(row.shares["eval-music"].zero_event_bound)

    def test_smoothing_and_refractory_come_from_the_scoring(self) -> None:
        # W = 2: a single planted 1.0 at hop t gives smoothed 0.5 at t and t + 1; at theta 0.5 the event
        # still fires at t (0.5 >= 0.5), at theta 0.6 nothing fires: recall 0/11, spurious 0, 0 events.
        sc = dataclasses.replace(ORACLE_SCORING, smoothing_hops=2)
        r = evaluate(oracle_streams(sc), PlantedDetector(ORACLE_PLANTED, HOP), sc, [0.5, 0.6])
        self.assertEqual((r.rows[0].recall.hits, r.rows[0].spurious, r.rows[0].shares["eval-speech"].events),
                         (7, 4, 3))
        self.assertEqual((r.rows[1].recall.hits, r.rows[1].spurious, r.rows[1].shares["eval-speech"].events),
                         (0, 0, 0))
        # ln 20 / 0.5 = 5.99146
        self.assertAlmostEqual(r.rows[1].shares["eval-speech"].zero_event_bound, 5.99146, delta=TOL)
        # R = 40: the events at 1000 and 1050 no longer merge -> 4 events in 0.5 h = 8.0 FA/h
        sc = dataclasses.replace(ORACLE_SCORING, refractory_hops=40)
        r = evaluate(oracle_streams(sc), PlantedDetector(ORACLE_PLANTED, HOP), sc, [0.5])
        speech = r.rows[0].shares["eval-speech"]
        self.assertEqual((speech.events, speech.fa_per_hour), (4, 8.0))

    def test_default_thresholds_are_the_quantiles_of_the_stream_maxima(self) -> None:
        sc = ORACLE_SCORING  # W = 1: the per-stream maximum smoothed score is the planted maximum
        scores = [np.array([0.0, 0.2, 0.0]), np.array([0.4, 0.0]), np.array([0.0, 0.6]), np.array([0.8]),
                  np.array([0.9, 0.9, 0.1])]
        # n = 5 over 5 maxima: linspace(0, 1, 5) lands on every sorted maximum, plus 0 and 1
        self.assertEqual(default_thresholds(scores, sc, n=5), [0.0, 0.2, 0.4, 0.6, 0.8, 0.9, 1.0])
        # n = 3: the 0, 0.5 and 1 quantiles -> 0.2, 0.6, 0.9
        self.assertEqual(default_thresholds(scores, sc, n=3), [0.0, 0.2, 0.6, 0.9, 1.0])
        # a maximum of exactly 1.0 deduplicates against the fixed 1; a dict of scores is accepted too
        self.assertEqual(default_thresholds({"a": np.array([1.0, 0.0]), "b": np.array([0.0])}, sc, n=2),
                         [0.0, 1.0])
        # W = 2: a lone 1.0 smooths to 0.5, which is where the threshold lands
        self.assertEqual(default_thresholds([np.array([0.0, 1.0, 0.0])],
                                            dataclasses.replace(sc, smoothing_hops=2), n=2), [0.0, 0.5, 1.0])
        with self.assertRaisesRegex(EvalError, "n must be at least 2"):
            default_thresholds(scores, sc, n=1)
        # summation noise above 1.0 (a moving average of ones) is clipped, and it is not a second threshold
        noisy = [np.full(30, 1.0), np.array([1.0])]
        self.assertEqual(default_thresholds(noisy, dataclasses.replace(sc, smoothing_hops=10), n=5),
                         [0.0, 1.0])
        # the bring-up corpus's extreme (max over 412 streams, W = 10, measured 9 September 2026) and one
        # stream's noise
        self.assertEqual(kws_eval.threshold_value(1.0000000000000455), 1.0)
        self.assertEqual(kws_eval.threshold_value(1.0000000000000113), 1.0)
        self.assertEqual(kws_eval.threshold_value(0.15000000000000002), 0.15)

    def test_a_detector_that_breaks_the_score_contract_is_refused(self) -> None:
        class Short:
            name, params = "short", {}

            def score(self, x: np.ndarray) -> np.ndarray:
                return np.zeros(x.size // HOP - 1)

        class OutOfRange:
            name, params = "range", {}

            def score(self, x: np.ndarray) -> np.ndarray:
                return np.full(x.size // HOP, 1.5)

        class Complex:
            name, params = "complex", {}

            def score(self, x: np.ndarray) -> np.ndarray:
                return np.full(x.size // HOP, 0.2 + 0.9j)

        class Boolean:
            name, params = "bool", {}

            def score(self, x: np.ndarray) -> np.ndarray:
                return np.zeros(x.size // HOP, dtype=bool)

        class Strings:
            name, params = "strings", {}

            def score(self, x: np.ndarray) -> list[str]:
                return ["a"] * (x.size // HOP)

        streams = oracle_streams()[:1]
        with self.assertRaisesRegex(EvalError, r"'pos/inside'.*returned \(399,\) scores for 400 hops"):
            evaluate(streams, Short(), ORACLE_SCORING, [0.5])
        with self.assertRaisesRegex(EvalError, r"'pos/inside'.*within \[0, 1\]"):
            evaluate(streams, OutOfRange(), ORACLE_SCORING, [0.5])
        # a complex, bool or string array is refused by name, never cast (numpy's ComplexWarning would be
        # the only trace of a dropped imaginary part; here any warning is an error)
        with warnings.catch_warnings():
            warnings.simplefilter("error")
            for det in (Complex(), Boolean(), Strings()):
                with self.assertRaisesRegex(EvalError,
                                            rf"'pos/inside': detector '{det.name}' returned dtype"):
                    evaluate(streams, det, ORACLE_SCORING, [0.5])
        with self.assertRaisesRegex(EvalError, "thresholds must be a non-empty list"):
            evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [])
        with self.assertRaisesRegex(EvalError, "thresholds must be a non-empty list"):
            evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [float("nan")])
        # scores are within [0, 1], so a threshold outside it measures nothing; a repeated one is refused too
        with self.assertRaisesRegex(EvalError, r"thresholds must lie in \[0, 1\].*\[-1\.0, 1\.5\]"):
            evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [-1.0, 0.5, 1.5])
        with self.assertRaisesRegex(EvalError, r"duplicate threshold\(s\) \[0\.5\]"):
            evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [0.5, 0.5, 1.0])

    def test_supplied_scores_are_rechecked(self) -> None:
        # a bare dict of scores goes through the same contract as a detector's output, by stream id
        streams = oracle_streams()
        det = PlantedDetector(ORACLE_PLANTED, HOP)
        good = {s.id: det.score_stream(s.id, s.samples().size) for s in streams}
        r = evaluate(streams, det, ORACLE_SCORING, [0.5], scores=good)
        self.assertEqual(r.rows[0].recall.hits, 7)
        self.assertEqual(r.rows[0].shares["eval-speech"].events, 3)
        for name, bad in (("NaN", np.full(400, np.nan)), ("short", np.ones(5)),
                          ("above 1", np.full(400, 7.0)), ("complex", np.full(400, 1j))):
            with self.subTest(name):
                with self.assertRaisesRegex(EvalError, r"stream 'pos/inside'"):
                    evaluate(streams, det, ORACLE_SCORING, [0.5], scores={**good, "pos/inside": bad})
        with self.assertRaisesRegex(EvalError, r"unknown stream id\(s\) \['zzz'\]"):
            evaluate(streams, det, ORACLE_SCORING, [0.5], scores={**good, "zzz": np.zeros(400)})
        with self.assertRaisesRegex(EvalError, r"no scores for stream\(s\) \['pos/inside'\]"):
            evaluate(streams, det, ORACLE_SCORING, [0.5], scores={k: v for k, v in good.items()
                                                                  if k != "pos/inside"})
        # the Scored that score_streams returns is taken as verified, but a stream edited afterwards is
        # still refused against the recorded sample counts (no second decode)
        scored = kws_eval.score_streams(streams, det, ORACLE_SCORING)
        self.assertEqual(evaluate(streams, det, ORACLE_SCORING, [0.5], scores=scored), r)
        streams[SPEECH_0].negative_samples -= 1
        with self.assertRaisesRegex(EvalError, r"'eval-speech/stream-0000': negative_samples 15999999"):
            evaluate(streams, det, ORACLE_SCORING, [0.5], scores=scored)
        with self.assertRaisesRegex(EvalError, r"unknown stream id\(s\) \['eval-music/stream-0000'\]"):
            evaluate(oracle_streams()[:-1], det, ORACLE_SCORING, [0.5], scores=scored)


class SelfChecksRunInCI(unittest.TestCase):
    """The hand-computed edge values of kws_scoring (hit_window(56) == (53, 79); hits at 53 and 79, none at
    52 or 80; the refractory pair) and of kws_detectors (the planted 1 kHz burst through the bridge) live in
    their `_self_check`s; running them here makes them part of `unittest discover`."""

    def test_kws_scoring_self_check(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            kws_scoring._self_check()

    def test_kws_detectors_self_check(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            kws_detectors._self_check()


class MisaccountedStreamsAreRefused(unittest.TestCase):
    """Item 2: each variant refused by name — by the harness's own check, and by validate_streams."""

    @staticmethod
    def variants() -> list[tuple[str, str, list[Stream]]]:
        """(name, the id the refusal must name, the streams)."""
        sc = ORACLE_SCORING
        out = []
        s = oracle_streams()
        bad = negative_stream("eval-speech/stream-0000", "eval-speech", 1000.0)
        bad.negative_samples = 1000 * RATE - 1  # declared one sample short of its audio
        out.append(("negative_samples != audio length", "eval-speech/stream-0000",
                    s[:SPEECH_0] + [bad] + s[SPEECH_0 + 1:]))
        s = oracle_streams()
        s[SPEECH_0].negative_samples = float(1000 * RATE)  # the right length, but not an integer
        out.append(("negative_samples not an integer", "eval-speech/stream-0000", s))
        s = oracle_streams()
        s[SPEECH_0].negative_samples = True  # a bool is not an integer count either
        out.append(("negative_samples a bool", "eval-speech/stream-0000", s))
        s = oracle_streams()
        s[0] = positive_stream("pos/inside", [70000], 4.0, sc)  # endpoint 70000 beyond 64000 samples
        out.append(("endpoint beyond the stream", "pos/inside", s))
        s = oracle_streams()
        s[0] = positive_stream("pos/inside", [63000], 4.0, sc)  # h = 393: window [390, 416] beyond 400 hops
        out.append(("window beyond the stream's hops", "pos/inside", s))
        s = oracle_streams()
        s[1] = positive_stream("pos/inside", [24000], 4.0, sc)  # a second "pos/inside"
        out.append(("duplicate stream id", "pos/inside", s))
        s = oracle_streams()
        s[0].positives = [Positive("u", 16000, 100), Positive("u", 48000, 300)]  # one id, two utterances
        out.append(("positive id used twice in one stream", "pos/inside", s))
        s = oracle_streams()
        s[1].positives = list(s[0].positives)  # pos/edge-lo carries pos/inside's utterance id
        out.append(("positive id used twice across streams", "pos/edge-lo", s))
        s = oracle_streams()
        s[0].negative_samples = 64000  # a positive stream counting its audio as negative hours
        out.append(("positive stream with hours", "pos/inside", s))
        s = oracle_streams()
        s[0].negative_samples = -1  # negative hours
        out.append(("positive stream with negative hours", "pos/inside", s))
        s = oracle_streams()
        s[SPEECH_0].negative_samples = -1  # negative hours on a negative stream
        out.append(("negative stream with negative hours", "eval-speech/stream-0000", s))
        return out

    def test_the_oracle_itself_passes(self) -> None:
        n = check_streams(oracle_streams(), ORACLE_SCORING)
        self.assertEqual(n["pos/two"], 128000)
        self.assertEqual(n["eval-speech/stream-0000"], 16000000)
        validate_streams(oracle_streams(), ORACLE_SCORING)

    def test_refused_by_the_harness(self) -> None:
        for name, who, streams in self.variants():
            with self.subTest(name):
                with self.assertRaisesRegex(EvalError, repr(who)):
                    check_streams(streams, ORACLE_SCORING)
                with self.assertRaisesRegex(EvalError, repr(who)):
                    evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [0.5])

    def test_refused_by_validate_streams(self) -> None:
        for name, who, streams in self.variants():
            with self.subTest(name):
                with self.assertRaisesRegex(StreamError, repr(who).replace("+", r"\+")):
                    validate_streams(streams, ORACLE_SCORING)

    def test_a_duplicate_positive_id_would_miscount_so_it_is_refused_not_scored(self) -> None:
        # kws_scoring.hits keys its map by utterance id: two utterances under one id would collapse to one
        # entry against a denominator of two (recall 1/2 with both windows hit) — hence the refusal above,
        # here shown to name the id
        s = oracle_streams()
        s[0].positives = [Positive("u", 16000, 100), Positive("u", 48000, 300)]
        with self.assertRaisesRegex(EvalError, r"'pos/inside': positive 'u' appears twice"):
            evaluate(s, PlantedDetector({"pos/inside": [(100, 1.0), (300, 1.0)]}, HOP), ORACLE_SCORING,
                     [0.5])

    def test_other_refusals_name_the_stream(self) -> None:
        s = oracle_streams()
        s[0].share = "eval-holdout"
        with self.assertRaisesRegex(EvalError, r"'pos/inside': unknown share 'eval-holdout'"):
            check_streams(s, ORACLE_SCORING)
        s = oracle_streams()
        s[SPEECH_0] = negative_stream("eval-speech/stream-0000", "eval-speech", 0.0)
        with self.assertRaisesRegex(EvalError, r"'eval-speech/stream-0000': empty stream"):
            check_streams(s, ORACLE_SCORING)
        s = oracle_streams()
        s[SPEECH_0].positives = [Positive("x", 24000, 150)]  # a negative stream carrying a positive
        with self.assertRaisesRegex(EvalError,
                                    r"'eval-speech/stream-0000': a eval-speech stream carrying 1 positive"):
            check_streams(s, ORACLE_SCORING)
        s = oracle_streams()
        s[0].positives = [Positive("x", 24000, 151)]  # endpoint_hop is not the endpoint's hop
        with self.assertRaisesRegex(EvalError, r"'pos/inside': positive 'x' endpoint_hop 151 != 150"):
            check_streams(s, ORACLE_SCORING)
        s = oracle_streams()
        s[0].positives = []
        with self.assertRaisesRegex(EvalError, r"'pos/inside': a positive stream with no positives"):
            check_streams(s, ORACLE_SCORING)
        # a negative stream in a share the report has no column for is refused, never silently dropped
        # from the hours (a hold-out utterance is a positive stream with subshare "holdout", not a share)
        s = oracle_streams()
        s[SPEECH_0].share = "holdout"
        with self.assertRaisesRegex(EvalError, r"'eval-speech/stream-0000': unknown share 'holdout'"):
            check_streams(s, ORACLE_SCORING)
        with self.assertRaisesRegex(StreamError, r"'eval-speech/stream-0000': unknown share 'holdout'"):
            validate_streams(s, ORACLE_SCORING)
        s = oracle_streams()
        s[0].subshare = None  # a positive stream must say where its utterance came from
        with self.assertRaisesRegex(EvalError, r"'pos/inside': subshare None"):
            check_streams(s, ORACLE_SCORING)
        with self.assertRaisesRegex(StreamError, r"'pos/inside': subshare None"):
            validate_streams(s, ORACLE_SCORING)
        # no streams at all is a refusal, not a vacuous report; so is something that is not a Stream
        planted = PlantedDetector({}, HOP)
        for fn in (check_streams, lambda st, sc: kws_eval.score_streams(st, planted, sc), validate_streams):
            with self.assertRaisesRegex((EvalError, StreamError), "no streams to score"):
                fn([], ORACLE_SCORING)
        with self.assertRaisesRegex(EvalError, "no streams to score"):
            evaluate([], PlantedDetector({}, HOP), ORACLE_SCORING, [0.5])
        with self.assertRaisesRegex(EvalError, "expected a Stream, got dict"):
            check_streams([{"id": "x"}], ORACLE_SCORING)


class ReportRoundTrip(unittest.TestCase):
    """Item 3."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.report = oracle_report()
        cls.report.manifest_name = "oracle"
        cls.report.manifest_hash = "0" * 64
        cls.report.eval_set_id = {"eval-speech": "a" * 64, "eval-music": "b" * 64}
        cls.report.front_end = {"log_mel_contract_version": 1, "dsptap_commit": "c" * 40}

    def test_to_dict_from_dict_and_the_json_file(self) -> None:
        r = self.report
        d = json.loads(json.dumps(r.to_dict()))
        self.assertEqual(Report.from_dict(d), r)
        with tempfile.TemporaryDirectory(prefix="mutap-kws-eval-") as tmp:
            json_path, md_path = kws_eval.write_report(r, pathlib.Path(tmp))
            self.assertEqual(kws_eval.read_report(json_path), r)
            self.assertEqual(md_path.read_text(encoding="utf-8"), r.markdown())
        self.assertEqual(d["report_version"], kws_eval.REPORT_VERSION)
        self.assertEqual(d["hours"], {"eval-speech": 0.5, "eval-music": 0.25})
        self.assertEqual(d["eval_set_id"], r.eval_set_id)
        self.assertEqual(d["rows"][1]["shares"]["eval-speech"]["events"], 3)
        self.assertIsNone(d["rows"][1]["shares"]["eval-speech"]["zero_event_bound"])
        self.assertNotIn("eval-tts", d["rows"][1]["shares"])
        self.assertIsNone(d["max_stream_s"])
        # the packing bound is provenance: it round-trips and lands beside the stream counts
        with_bound = dataclasses.replace(r, max_stream_s=60.0)
        self.assertEqual(Report.from_dict(json.loads(json.dumps(with_bound.to_dict()))), with_bound)
        self.assertIn("- hours: eval-speech H = 0.5 h (2 streams, packed at <= 60 s), eval-music H = 0.25 h "
                      "(1 streams, packed at <= 60 s), eval-tts absent, eval-noise absent",
                      with_bound.markdown())

    def test_from_dict_refuses_by_name(self) -> None:
        d = self.report.to_dict()
        d["extra"] = 1
        with self.assertRaisesRegex(EvalError, r"report: unknown field\(s\) \['extra'\]"):
            Report.from_dict(d)
        d = self.report.to_dict()
        d["report_version"] = 99
        with self.assertRaisesRegex(EvalError, "report_version 99"):
            Report.from_dict(d)
        d = self.report.to_dict()
        del d["rows"][0]["shares"]["eval-speech"]["hours"]
        with self.assertRaisesRegex(EvalError, r"rows\[0\]\.shares\[eval-speech\]: missing required "
                                               r"field\(s\) \['hours'\]"):
            Report.from_dict(d)

    def test_markdown_carries_every_figure_with_its_hours(self) -> None:
        md = self.report.markdown()
        lines = md.splitlines()
        rows = [ln for ln in lines if ln.startswith("| ") and not ln.startswith("| threshold")]
        self.assertEqual(len(rows), 3)
        # the header lines
        self.assertIn("- manifest: oracle (`" + "0" * 64 + "`)", md)
        self.assertIn("- hours: eval-speech H = 0.5 h (2 streams), eval-music H = 0.25 h (1 streams), "
                      "eval-tts absent, eval-noise absent", md)
        self.assertIn("- positives: eval-speech 9, eval-tts absent, holdout 2", md)
        self.assertIn("- eval_set_id: eval-music `" + "b" * 64 + "`, eval-speech `" + "a" * 64 + "`", md)
        self.assertIn("- front end: log_mel_contract_version 1, DspTap `" + "c" * 40 + "`", md)
        self.assertIn("T = 3 hops, L = 20 hops", md)
        self.assertIn("W = 1 hops, R = 100 hops", md)
        # the 0.5 row: every FA/h figure with its interval and its hours; absent columns read "absent"
        cells = [c.strip() for c in rows[1].strip("|").split("|")]
        self.assertEqual(len(cells), 10)
        self.assertEqual(cells[0], "0.5")  # the shortest exact repr (thresholds 1 - 1e-8 apart stay distinct)
        self.assertEqual(cells[1], "0.636 [0.354, 0.848] (7/11)")
        self.assertEqual(cells[2], "0.364")
        self.assertEqual(cells[3], "4")
        self.assertEqual(cells[4], "6.00 [1.24, 17.53] (3 in 0.5 h)")
        self.assertEqual(cells[5], "0.00 [0.00, 14.76] <= 11.98 (0 in 0.25 h)")
        self.assertEqual(cells[6], "absent")   # eval-tts
        self.assertEqual(cells[7], "absent")   # eval-noise
        self.assertEqual(cells[8], "absent")   # eval-tts recall
        self.assertEqual(cells[9], "1.000 [0.342, 1.000] (2/2)")  # hold-out recall
        # the 0.0 row
        cells = [c.strip() for c in rows[0].strip("|").split("|")]
        # Wilson 0/11: centre = 0.174612 / 1.349224 = 0.129417, half = 1.959964 * sqrt(3.841459/484) /
        # 1.349224 = 0.129416 -> [0, 0.258833]
        self.assertEqual(cells[1], "0.000 [0.000, 0.259] (0/11)")
        # 2 in 0.5 h: chi2(0.025, 4)/2/0.5 = 0.4844, chi2(0.975, 6)/2/0.5 = 14.449
        self.assertEqual(cells[4], "4.00 [0.48, 14.45] (2 in 0.5 h)")
        self.assertEqual(cells[5], "4.00 [0.10, 22.29] (1 in 0.25 h)")
        # every FA/h cell that is not absent names its hours
        for row in rows:
            for cell in [c.strip() for c in row.strip("|").split("|")][4:8]:
                self.assertTrue(cell == "absent" or cell.endswith(" h)"), cell)

    def test_an_empty_positive_set_reports_absent_recall(self) -> None:
        streams = [s for s in oracle_streams() if s.share != "positives"]
        r = evaluate(streams, PlantedDetector(ORACLE_PLANTED, HOP), ORACLE_SCORING, [0.5])
        self.assertEqual(r.positives, {})
        self.assertIsNone(r.rows[0].recall.recall)
        self.assertEqual(Report.from_dict(json.loads(json.dumps(r.to_dict()))), r)
        rows = [ln for ln in r.markdown().splitlines()
                if ln.startswith("| ") and not ln.startswith("| threshold")]
        self.assertEqual(len(rows), 1)
        cells = [c.strip() for c in rows[0].strip("|").split("|")]
        self.assertEqual(cells[1:4], ["absent", "absent", "0"])
        self.assertEqual(cells[8:10], ["absent", "absent"])


class ToyEndToEnd(unittest.TestCase):
    """Item 4: the toy rebuilt into a temporary store, streams from the committed expected lock, the
    band-energy baseline through the bridge, a sweep — as a library call and through the `sweep` CLI."""

    def test_toy_sweep(self) -> None:
        manifest = load_manifest(test_kws.MANIFEST)
        lock = read_lock(test_kws.EXPECTED / "lock.json")
        with tempfile.TemporaryDirectory(prefix="mutap-kws-eval-toy-") as tmp:
            store = pathlib.Path(tmp) / "store"
            store.mkdir()
            r = test_kws.run_build("all", test_kws.MANIFEST, store, jobs=1)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            g = manifest.recipe.geometry
            scoring = Scoring(hop=g.hop, sample_rate=int(g.sample_rate),
                              tolerance_hops=manifest.recipe.label.tolerance_hops)
            streams = kws_streams.streams_from_lock(manifest, lock, Store(store), scoring)
            kws_streams.validate_streams(streams, scoring)
            detector = kws_detectors.BandEnergyBaseline(g)
            scores = kws_eval.score_streams(streams, detector, scoring)
            thresholds = default_thresholds(scores, scoring, n=10)
            report = evaluate(streams, detector, scoring, thresholds, scores=scores, provenance={
                "manifest_name": manifest.name, "manifest_hash": lock.manifest_hash,
                "eval_set_id": dict(lock.eval_set_id), "max_stream_s": kws_streams.DEFAULT_MAX_STREAM_S,
                "front_end": {"log_mel_contract_version": kws_features.contract_version(),
                              "dsptap_commit": kws_features.dsptap_commit()}})
            # hours: the sum of the eval negatives' decoded lengths, read back from the pcm tier
            build = kws_build.Build(manifest, Store(store))
            decoded: dict[str, int] = {}
            for c in lock.clips:
                if c.split == "eval" and c.variant == 0 and c.label != 1:
                    info = soundfile.info(str(build.pcm_path(c.source, c.id[len(c.source) + 1:])))
                    self.assertEqual(info.samplerate, RATE)
                    self.assertEqual(info.frames, c.length, c.id)
                    decoded[c.share] = decoded.get(c.share, 0) + info.frames
            self.assertEqual(set(decoded), {"eval-speech", "eval-music", "eval-noise"})  # the toy's shares
            self.assertEqual(sum(decoded.values()), 112000)  # 7 s: measured from the committed toy lock
            self.assertEqual(set(report.hours), set(decoded))
            # bit-identical: the integer sample sum divided once, as hours_per_share and the lock's summary
            # (which counts the featurized negatives: speech + music, never noise) compute it
            for share, n in decoded.items():
                self.assertEqual(report.hours[share], n / RATE / 3600.0, share)
            self.assertEqual(report.hours, kws_streams.hours_per_share(streams, scoring))
            self.assertEqual(scoring.hours(decoded["eval-speech"] + decoded["eval-music"]),
                             lock.summary["splits"]["eval"]["negative"]["hours"])
            # positives: the toy's one eval positive (label 1, share eval-speech, variant 0)
            n_pos = sum(1 for c in lock.clips if c.split == "eval" and c.variant == 0 and c.label == 1)
            self.assertEqual(n_pos, 1)
            self.assertEqual(report.positives, {"eval-speech": n_pos})
            self.assertEqual(report.eval_set_id, lock.eval_set_id)
            self.assertEqual(report.max_stream_s, 60.0)
            self.assertNotIn("eval-tts", report.hours)
            # the scores are one per completed hop; the positive's count is what extract featurized
            frames = {c.id: c.extra["frames"] for c in lock.clips
                      if c.split == "eval" and c.variant == 0 and c.label == 1}
            for s in streams:
                self.assertEqual(scores[s.id].size, s.samples().size // g.hop, s.id)
                self.assertEqual(scores.n_samples[s.id], s.samples().size, s.id)
                if s.share == "positives":
                    self.assertEqual(scores[s.id].size, frames[s.positives[0].id], s.id)
            self.assertEqual(len(report.rows), len(thresholds))
            for row in report.rows:
                self.assertEqual(set(row.shares), set(decoded))
                for share, f in row.shares.items():
                    self.assertEqual(f.hours, report.hours[share])
            self.assertEqual(Report.from_dict(json.loads(json.dumps(report.to_dict()))), report)
            md = report.markdown()
            self.assertIn("eval-tts absent", md)
            self.assertIn("packed at <= 60 s", md)
            self.assertIn(f"DspTap `{kws_features.dsptap_commit()}`", md)
            print(f"\ntoy sweep: {len(streams)} streams, {len(thresholds)} thresholds, hours {report.hours}")
            # the sweep CLI on the same store: the report carries the lock's eval_set_id, the same hours and
            # the packing bound it was run with
            out = pathlib.Path(tmp) / "sweep"
            argv = ["sweep", "--manifest", str(test_kws.MANIFEST), "--lock",
                    str(test_kws.EXPECTED / "lock.json"), "--store", str(store), "--thresholds", "10",
                    "--out", str(out)]
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(kws_eval.main(argv + ["--max-stream-s", "2"]), 0)
            cli = kws_eval.read_report(out / "report.json")
            self.assertEqual(cli.eval_set_id, lock.eval_set_id)
            self.assertEqual(cli.hours, report.hours)
            self.assertEqual(cli.max_stream_s, 2.0)
            self.assertEqual(cli.negative_streams["eval-speech"], 2)  # three 1 s clips at a 2 s bound
            self.assertIn("packed at <= 2 s", (out / "report.md").read_text(encoding="utf-8"))
            self.assertIsNotNone(cli.wall_s)
            # a --grid of -1 or 1 is refused by name before anything is decoded (1 would add only 0.0)
            for grid in ("-1", "1"):
                err = io.StringIO()
                with contextlib.redirect_stderr(err):
                    self.assertEqual(kws_eval.main(argv + ["--grid", grid, "--out", str(out / "never")]), 1)
                self.assertIn("--grid must be 0 (off) or at least 2", err.getvalue())
                self.assertFalse((out / "never").exists())


if __name__ == "__main__":
    unittest.main()
