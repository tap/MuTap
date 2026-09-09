#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The M5 stream, detector and hold-out tests (wake-word plan §6 M5; the brief's owner-B items).

    .venv/bin/python -m unittest tools/ml/kws/test_kws_streams.py            # from the repo root
    .venv/bin/python -m unittest discover -s tools/ml/kws -p 'test_*.py' -v   # the kws-dataset CI job

- Streams from the toy lock (`ToyStreams`): the toy is rebuilt into a temporary store as test_kws does;
  the streams assembled from the committed expected lock have the expected counts, members and hours
  (hours = the decoded lengths, and equal to the lock's own eval-negative hours where it featurizes
  them); the positive stream's mixture is exactly what `extract` featurized — its frame count equals the
  lock's `extra.frames`, its features through the bridge equal the committed eval shard's rows — and
  its hit window lies inside the stream; the packing bound; the assembly's refusals.
- `validate_streams` refuses every mis-accounting of the brief's item 2 by name (`Validation`).
- The band-energy baseline's alignment on a planted 1 kHz burst (`Detectors`; the measured numbers are
  beside the assertions), its band selection, and the planted detector's contract.
- The hold-out (`HoldoutRecord`, the brief's item 5): a synthetic holdout.json and a FLAC written by
  soundfile verify; one altered byte is refused naming the file; a missing talker row, an absent
  consent_form_version and permitted_uses lacking either required use are each refused by name; the set
  id changes when a row is dropped; the schema, path and tier refusals — including one file under two
  spellings ('./' or a case-folded path) or one sha256 under two rows, so a file is one utterance; and
  `kws_eval.py holdout` end to end (a report whose holdout_set_id is the record's, refused with nothing
  written once a byte is flipped).
- The three CLIs refuse a missing store (no --store, no MUTAP_KWS_STORE) with `refused:` and rc 2, never a
  traceback.

Measured 9 September 2026 on the M0 Mac (Apple silicon, CPython 3.12.14, numpy 2.5.3, scipy 1.18.1,
soundfile 0.14.0, the C ABI built Release): the toy rebuild in setUpClass (`all --jobs 1`, a subprocess)
0.87 s wall; the whole file 1.5 s. On the bring-up corpus (263,487 lock rows) the same day: read_lock 1.7 s,
streams_from_lock 0.08 s for 412 streams (195 positives, 179 eval-speech streams = 2.9513 h, 38 eval-noise
streams = 0.5021 h, hours from the decoded lengths; 0.83 s before the asdict conversion was narrowed to the
rows the loaders read), validate_streams 1.5 s (every stream decoded), every
positive mixture's frame count equal to its lock row's `extra.frames`, and the band-energy baseline at
8,461 s of audio scored per second (decode + score 4,478 s/s), 1.04 GB peak RSS (the lock dominates).
"""
from __future__ import annotations

import contextlib
import copy
import dataclasses
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import numpy as np
import soundfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import kws_audio  # noqa: E402
import kws_detectors  # noqa: E402
import kws_features  # noqa: E402
import kws_eval  # noqa: E402  (owner A's harness: the holdout CLI is exercised on this fixture)
import kws_holdout  # noqa: E402
import kws_streams  # noqa: E402
from kws_manifest import load_manifest, manifest_hash, read_lock  # noqa: E402
from kws_scoring import Positive, Scoring  # noqa: E402
from kws_store import ENV_VAR, Store, sha256_file  # noqa: E402

REPO_ROOT = HERE.parents[2]
PYTHON = sys.executable
BUILD = HERE / "kws_build.py"
TOY = HERE / "fixtures" / "toy"
MANIFEST = TOY / "manifest.json"
EXPECTED = TOY / "expected"
RATE = kws_audio.RATE
# The same relative bound test_kws.py uses for a rebuild against the committed shards: bit-identical on
# the M0 Mac (measured 0.0 on 9 September 2026), 1e-6 to cover another toolchain's FMA / libm rounding.
FEATURE_TOLERANCE = 1e-6


def run_build(stage: str, store: pathlib.Path, jobs: int = 1) -> subprocess.CompletedProcess:
    env = dict(os.environ)
    env[ENV_VAR] = str(store)  # the temporary store, whatever the developer's shell says
    return subprocess.run([PYTHON, str(BUILD), stage, "--manifest", str(MANIFEST), "--store", str(store),
                           "--jobs", str(jobs)], capture_output=True, text=True, check=False, env=env,
                          cwd=str(REPO_ROOT))


def toy_scoring(manifest) -> Scoring:
    return Scoring(hop=manifest.recipe.geometry.hop, tolerance_hops=manifest.recipe.label.tolerance_hops)


def positive_stream(stream_id: str, n: int, endpoint: int, scoring: Scoring, **kw) -> kws_streams.Stream:
    p = Positive(id=stream_id + "/kw", endpoint_sample=endpoint, endpoint_hop=scoring.endpoint_hop(endpoint))
    fields = {"id": stream_id, "share": "positives", "positives": [p], "negative_samples": 0,
              "subshare": "eval-speech", "audio": np.zeros(n)}
    fields.update(kw)
    return kws_streams.Stream(**fields)


def negative_stream(stream_id: str, n: int, share: str = "eval-speech", **kw) -> kws_streams.Stream:
    fields = {"id": stream_id, "share": share, "positives": [], "negative_samples": n, "audio": np.zeros(n)}
    fields.update(kw)
    return kws_streams.Stream(**fields)


class ToyStreams(unittest.TestCase):
    """Streams from the committed toy lock over a temporary rebuild of the toy store."""

    tmp: tempfile.TemporaryDirectory
    store: Store
    seconds: float

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-streams-")
        root = pathlib.Path(cls.tmp.name) / "store"
        root.mkdir()
        t0 = time.time()
        r = run_build("all", root)
        cls.seconds = time.time() - t0
        if r.returncode != 0:
            cls.tmp.cleanup()
            raise AssertionError(f"toy build failed (rc {r.returncode}):\n{r.stdout}\n{r.stderr}")
        cls.store = Store(root)
        cls.manifest = load_manifest(MANIFEST)
        cls.lock = read_lock(EXPECTED / "lock.json")
        cls.scoring = toy_scoring(cls.manifest)
        cls.streams = kws_streams.streams_from_lock(cls.manifest, cls.lock, cls.store, cls.scoring)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_1_counts_members_and_hours_from_decoded_lengths(self) -> None:
        streams = self.streams
        self.assertEqual(self.lock.manifest_hash, manifest_hash(self.manifest))
        by_id = {c.id: c for c in self.lock.clips if c.variant == 0}
        eval_rows = [c for c in by_id.values() if c.split == "eval"]
        positives = [s for s in streams if s.share == "positives"]
        self.assertEqual([s.id for s in positives], sorted(c.id for c in eval_rows if c.label == 1))
        self.assertEqual(len(positives), 1)  # the toy's one eval positive (marvin)
        p = positives[0]
        row = by_id[p.id]
        self.assertEqual(p.subshare, "eval-speech")
        self.assertEqual(p.negative_samples, 0)
        self.assertEqual(p.members, [p.id])
        self.assertIsNone(p.audio)  # lazy
        self.assertEqual(p.positives, [Positive(p.id, row.endpoint_sample, row.endpoint_sample // 160)])
        self.assertEqual(p.positives[0].endpoint_hop, row.extra["endpoint_hop"])
        # negatives: per share, sorted by id, one stream each at the default 60 s bound
        shares = [s.share for s in streams if s.share != "positives"]
        self.assertEqual(shares, ["eval-speech", "eval-music", "eval-noise"])  # no eval-tts in the toy
        for s in streams:
            if s.share == "positives":
                continue
            want = sorted(c.id for c in eval_rows if c.share == s.share and c.label != 1)
            self.assertEqual(s.members, want, s.id)
            self.assertEqual(s.id, f"{s.share}/stream-0000")
            self.assertEqual(s.negative_samples, sum(by_id[i].length for i in s.members))
            self.assertEqual(s.positives, [])
        hours = kws_streams.hours_per_share(streams, self.scoring)
        self.assertEqual(hours, {"eval-speech": 48000 / RATE / 3600, "eval-music": 32000 / RATE / 3600,
                                 "eval-noise": 32000 / RATE / 3600})
        # the featurized eval negatives (speech + music; noise is never featurized) are the lock's own hours
        self.assertAlmostEqual(hours["eval-speech"] + hours["eval-music"],
                               self.lock.summary["splits"]["eval"]["negative"]["hours"], places=15)
        self.assertEqual(kws_streams.positives_per_subshare(streams), {"eval-speech": 1})
        kws_streams.validate_streams(streams, self.scoring)  # decodes every stream: the accounting holds
        # a negative stream is the concatenation of its members' pcm-tier clips, in member order
        speech = next(s for s in streams if s.share == "eval-speech")
        x = speech.samples()
        self.assertEqual(x.dtype, np.float64)
        parts = [kws_audio.from_int16(kws_audio.read_pcm(
            self.store.pcm(by_id[i].source, kws_audio.DECODER_ID, kws_audio.resampler_id(
                self.manifest.recipe.resampler.window)) / (i[len(by_id[i].source) + 1:] + ".wav")))
            for i in speech.members]
        np.testing.assert_array_equal(x, np.concatenate(parts))
        print(f"\ntoy streams: {len(streams)} streams from {len(eval_rows)} eval rows; toy rebuild "
              f"{self.seconds:.2f} s wall")

    def test_2_positive_mixture_is_what_extract_featurized(self) -> None:
        p = next(s for s in self.streams if s.share == "positives")
        row = next(c for c in self.lock.clips if c.id == p.id and c.variant == 0)
        x = p.samples()
        self.assertEqual(x.size, row.extra["mixture_samples"])
        g = self.manifest.recipe.geometry
        det = kws_detectors.BandEnergyBaseline(g)
        score = det.score(x)
        self.assertEqual(score.shape, (row.extra["frames"],))  # the lock's frame count for this row
        self.assertEqual(score.shape[0], kws_detectors.frames_for(x.size, g.hop))
        self.assertTrue(np.all((0.0 <= score) & (score <= 1.0)))
        lo, hi = self.scoring.hit_window(p.positives[0].endpoint_hop)
        self.assertGreaterEqual(lo, 0)
        self.assertLess(hi, score.shape[0])  # the window `extract` reserved lies inside the rows
        # and the features through the bridge are the committed eval shard's rows for this clip
        with np.load(EXPECTED / "features" / "eval" / "shard-0000.npz") as shard:
            ids = [str(i) for i in shard["clip_ids"]]
            k = ids.index(p.id)
            a, b = int(shard["clip_offsets"][k]), int(shard["clip_offsets"][k + 1])
            expected = shard["features"][a:b].astype(np.float64)
            self.assertEqual(int(shard["clip_endpoints"][k]), p.positives[0].endpoint_hop)
        got = det.features(x).astype(np.float32).astype(np.float64)
        self.assertEqual(got.shape, expected.shape)
        diff = np.abs(got - expected)
        self.assertTrue(np.all(diff <= FEATURE_TOLERANCE * np.maximum(1.0, np.abs(expected))),
                        f"max |bridge - shard| = {diff.max():.3e} (measured 0.0 on the M0 Mac)")

    def test_3_the_packing_bound_never_splits_a_clip(self) -> None:
        # the toy's eval-speech negatives are three 1 s clips: at a 1 s bound each stands alone, at 2 s
        # the first two share a stream, and a bound below one clip still yields whole clips
        for max_s, want in ((1.0, [1, 1, 1]), (2.0, [2, 1]), (0.5, [1, 1, 1]), (60.0, [3])):
            streams = kws_streams.streams_from_lock(self.manifest, self.lock, self.store, self.scoring,
                                                    shares=("eval-speech",), max_stream_s=max_s)
            speech = [s for s in streams if s.share == "eval-speech"]
            self.assertEqual([len(s.members) for s in speech], want, max_s)
            self.assertEqual([s.id for s in speech],
                             [f"eval-speech/stream-{n:04d}" for n in range(len(want))])
            self.assertEqual(sum(s.negative_samples for s in speech), 48000)  # the hours never change
            self.assertEqual(sorted(i for s in speech for i in s.members),
                             [i for s in speech for i in s.members])  # id order across the streams
            kws_streams.validate_streams(streams, self.scoring)

    def test_4_the_assembly_refuses_a_mismatched_scoring_share_or_lock(self) -> None:
        m, lock, store, sc = self.manifest, self.lock, self.store, self.scoring
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.streams_from_lock(m, lock, store, dataclasses.replace(sc, hop=80))
        self.assertIn("scoring.hop 80", str(cm.exception))
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.streams_from_lock(m, lock, store, dataclasses.replace(sc, tolerance_hops=5))
        self.assertIn("tolerance_hops 5", str(cm.exception))
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.streams_from_lock(m, lock, store, dataclasses.replace(sc, latency_hops=10))
        self.assertIn("latency_hops 10", str(cm.exception))
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.streams_from_lock(m, lock, store, sc, shares=("eval-speech", "dev"))
        self.assertIn("'dev'", str(cm.exception))
        with self.assertRaises(kws_streams.StreamError):
            kws_streams.streams_from_lock(m, lock, store, sc, max_stream_s=0.0)
        other = copy.deepcopy(lock)
        other.manifest_hash = "0" * 64
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.streams_from_lock(m, other, store, sc)
        self.assertIn("built from manifest " + "0" * 64, str(cm.exception))
        # a share the lock does not have simply yields no streams of it (reported as absent downstream)
        only_tts = kws_streams.streams_from_lock(m, lock, store, sc, shares=("eval-tts",))
        self.assertEqual(only_tts, [])

    def test_5_the_cli_refuses_a_missing_store_by_name(self) -> None:
        argv = ["--manifest", str(MANIFEST), "--lock", str(EXPECTED / "lock.json")]
        err = io.StringIO()
        with mock.patch.dict(os.environ), contextlib.redirect_stderr(err):
            os.environ.pop(ENV_VAR, None)
            self.assertEqual(kws_streams.main(argv), 2)
        self.assertTrue(err.getvalue().startswith("refused: no feature store"), err.getvalue())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(kws_streams.main(argv + ["--store", str(self.store.root)]), 0)


class Validation(unittest.TestCase):
    """The brief's item 2: every mis-accounting refused by name by validate_streams."""

    def setUp(self) -> None:
        self.sc = Scoring()  # hop 160, T 3, L 20: the window of an endpoint at 9000 is [53, 79]

    def _refused(self, streams, *needles: str) -> None:
        with self.assertRaises(kws_streams.StreamError) as cm:
            kws_streams.validate_streams(streams, self.sc)
        for needle in needles:
            self.assertIn(needle, str(cm.exception))

    def test_a_valid_list_passes_eager_or_lazy(self) -> None:
        lazy = negative_stream("n1", 16000, audio=None, load=lambda: np.zeros(16000))
        kws_streams.validate_streams([positive_stream("p1", 16000, 9000, self.sc), lazy], self.sc)

    def test_negative_samples_must_equal_the_decoded_length(self) -> None:
        self._refused([negative_stream("n1", 16000, negative_samples=16001)], "stream 'n1'",
                      "negative_samples 16001 != its 16000 decoded samples")
        self._refused([negative_stream("n1", 16000, negative_samples=-16000)], "stream 'n1'",
                      "negative_samples")
        self._refused([negative_stream("n1", 16000, negative_samples=16000.0)], "must be an integer")

    def test_a_positive_beyond_its_stream_is_refused(self) -> None:
        self._refused([positive_stream("p1", 16000, 16001, self.sc)], "positive 'p1/kw'", "beyond its 16000")
        # the endpoint is inside the audio but the window [h - T, h + L + T] is not: 100 hops, window to 118
        self._refused([positive_stream("p1", 16000, 15200, self.sc)], "window [92, 118]",
                      "beyond the stream's 100 hops")
        ok = positive_stream("p1", 16000, 12000, self.sc)  # window [72, 98] fits
        kws_streams.validate_streams([ok], self.sc)
        bad_hop = positive_stream("p1", 16000, 9000, self.sc)
        bad_hop.positives = [Positive("p1/kw", 9000, 57)]
        self._refused([bad_hop], "endpoint_hop 57 != 56")

    def test_duplicate_ids_are_refused(self) -> None:
        self._refused([negative_stream("n1", 16000), negative_stream("n1", 16000)], "stream 'n1'",
                      "duplicate")
        a = positive_stream("p1", 16000, 9000, self.sc)
        b = positive_stream("p2", 16000, 9000, self.sc)
        b.positives = list(a.positives)
        self._refused([a, b], "positive 'p1/kw' appears twice")

    def test_a_positive_stream_with_negative_hours_is_refused(self) -> None:
        self._refused([positive_stream("p1", 16000, 9000, self.sc, negative_samples=-16000)], "stream 'p1'",
                      "positive stream with negative_samples -16000")
        self._refused([positive_stream("p1", 16000, 9000, self.sc, negative_samples=16000)],
                      "positive stream with negative_samples 16000")

    def test_empty_unknown_and_malformed_streams_are_refused(self) -> None:
        self._refused([], "no streams")
        self._refused([negative_stream("n1", 0)], "stream 'n1'", "empty stream")
        self._refused([negative_stream("n1", 16000, share="train")], "unknown share 'train'")
        self._refused([negative_stream("n1", 16000, audio=None)], "neither audio nor a loader")
        self._refused([negative_stream("n1", 16000, audio=np.zeros((2, 8000)))], "one-dimensional")
        self._refused([positive_stream("p1", 16000, 9000, self.sc, positives=[])], "no positives")
        self._refused([positive_stream("p1", 16000, 9000, self.sc, subshare="eval-music")],
                      "subshare 'eval-music'")
        neg = negative_stream("n1", 16000)
        neg.positives = [Positive("x", 9000, 56)]
        self._refused([neg], "eval-speech stream carrying 1 positive")


class Detectors(unittest.TestCase):
    """The band-energy baseline through the bridge, and the planted detector."""

    def test_band_energy_alignment_on_a_planted_1khz_burst(self) -> None:
        # Measured 9 September 2026 on the M0 Mac at the reference geometry (bursts ending at 24000, 24080,
        # 24159, 24001, 8400 in digital silence), h = e // 160: argmax <= h + 1; s[h] >= 0.321757 (the
        # minimum, for a burst ending early in its hop) and s[h + 1] >= 0.961318 (the offset edge splatters
        # across every band); s[h + 2] is 0.0 except for the burst ending late in its hop (24159), where it
        # reads 0.979018; 0 from h + 3 on; 0 before hop start // 160 and > 0 at it.
        g = kws_features.Geometry()
        det = kws_detectors.BandEnergyBaseline(g)
        self.assertEqual(det.name, "band-energy")
        self.assertEqual(det.bands, list(range(5, 27)))  # centres 329.7 .. 2901.9 Hz
        self.assertEqual(det.params["bands"], det.bands)
        self.assertEqual(det.params["stored_path"], "log")
        self.assertEqual(det.params["band_centres_hz"][0], 329.698)  # band 5's centre, rounded to 3 places
        self.assertEqual(len(det.params["band_centres_hz"]), 22)
        for start, end in ((16000, 24000), (16000, 24080), (16000, 24159), (16003, 24001), (8000, 8400)):
            x = np.zeros(48000)
            t = np.arange(start, end)
            x[start:end] = 0.5 * np.sin(2.0 * np.pi * 1000.0 * t / g.sample_rate)
            s = det.score(x)
            h = end // g.hop
            self.assertEqual(s.shape, (300,))
            self.assertEqual(s.dtype, np.float64)
            self.assertLessEqual(int(np.argmax(s)), h + 1, (start, end))
            self.assertGreaterEqual(s[h], 0.3, (start, end, s[h]))          # measured minimum 0.321757
            self.assertGreaterEqual(s[h + 1], 0.3, (start, end, s[h + 1]))  # measured minimum 0.961318
            self.assertLessEqual(s[h + 2], 0.99, (start, end, s[h + 2]))    # measured 0.979018 at most
            self.assertTrue(np.all(s[h + 3:] == 0.0), (start, end))
            self.assertTrue(np.all(s[:start // g.hop] == 0.0), (start, end))
            self.assertGreater(s[start // g.hop], 0.0)
        # the frame count is n // hop for every length, through the bridge
        for n in (0, 159, 160, 161, 400, 62555):
            self.assertEqual(det.score(np.zeros(n)).shape, (n // g.hop,), n)
            self.assertEqual(kws_detectors.frames_for(n, g.hop), n // g.hop)
        # a PCEN geometry scores through the plain-log path: identical scores, and the params say so
        pcen = dataclasses.replace(g, pcen=dataclasses.replace(g.pcen, enabled=True))
        det_pcen = kws_detectors.BandEnergyBaseline(pcen)
        x = np.random.default_rng(1).standard_normal(16000) * 0.1
        np.testing.assert_array_equal(det_pcen.score(x), det.score(x))
        self.assertFalse(det_pcen.params["geometry"]["pcen"]["enabled"])

    def test_band_energy_refuses_a_band_with_no_centre(self) -> None:
        g = kws_features.Geometry()
        with self.assertRaises(kws_detectors.DetectorError) as cm:
            kws_detectors.BandEnergyBaseline(g, 7200.0, 7300.0)  # the top centre is 7119.6 Hz
        self.assertIn("no mel band centre lies in [7200.0, 7300.0] Hz", str(cm.exception))
        with self.assertRaises(kws_detectors.DetectorError):
            kws_detectors.BandEnergyBaseline(g, 3000.0, 300.0)
        with self.assertRaises(kws_detectors.DetectorError):
            kws_detectors.BandEnergyBaseline(g).score(np.zeros((2, 800)))

    def test_planted_detector(self) -> None:
        pd = kws_detectors.PlantedDetector({"a": [(10, 0.5), (3, 0.9)], "b": []}, hop=160)
        self.assertEqual(pd.name, "planted")
        self.assertEqual(pd.params, {"hop": 160, "planted": {"a": [[3, 0.9], [10, 0.5]], "b": []}})
        s = pd.score_stream("a", 20 * 160 + 159)
        self.assertEqual(s.shape, (20,))
        self.assertEqual((s[3], s[10], s.sum()), (0.9, 0.5, 1.4))
        self.assertEqual(pd.score_stream("b", 800).tolist(), [0.0] * 5)
        self.assertEqual(pd.score_stream("unplanted", 800).tolist(), [0.0] * 5)
        with self.assertRaises(kws_detectors.DetectorError) as cm:
            pd.score_stream("a", 10 * 160)  # hop 10 is beyond a 10-hop stream
        self.assertIn("planted hop 10 is beyond the stream's 10 hops", str(cm.exception))
        with self.assertRaises(kws_detectors.DetectorError):
            pd.score(np.zeros(1600))
        for bad in ({"a": [(3, 1.5)]}, {"a": [(-1, 0.5)]}, {"a": [(3, 0.5), (3, 0.6)]}, {"a": [(3,)]}):
            with self.assertRaises(kws_detectors.DetectorError):
                kws_detectors.PlantedDetector(bad, 160)
        with self.assertRaises(kws_detectors.DetectorError):
            kws_detectors.PlantedDetector({}, 0)


class HoldoutRecord(unittest.TestCase):
    """The brief's item 5, over a synthetic holdout.json and a temporary store."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-holdout-")
        self.dir = pathlib.Path(self.tmp.name)
        self.store = Store(self.dir / "store")
        self.store.holdout().mkdir(parents=True)
        self.sc = Scoring()
        self.files = {}
        for talker, rel, endpoint in (("T01", "T01/room-a/take-001.flac", 24000),
                                      ("T02", "T02/room-b/take-007.flac", 30000)):
            self.files[rel] = self._write_flac(rel, endpoint, RATE)
        self.doc = {
            "holdout_version": kws_holdout.HOLDOUT_JSON_VERSION,
            "talkers": [{"pseudonym": "T01", "consent_form_version": "consent-v1",
                         "permitted_uses": ["evaluation", "m7-replay"]},
                        {"pseudonym": "T02", "consent_form_version": "consent-v1",
                         "permitted_uses": ["m7-replay", "evaluation", "publication"]}],
            "utterances": [
                {"file": "T01/room-a/take-001.flac", "sha256": self.files["T01/room-a/take-001.flac"],
                 "talker": "T01", "microphone_path": "close", "distance_m": 0.3, "snr_db": None,
                 "phrase": "marvin", "endpoint_sample": 24000},
                {"file": "T02/room-b/take-007.flac", "sha256": self.files["T02/room-b/take-007.flac"],
                 "talker": "T02", "microphone_path": "room", "distance_m": 2.0, "snr_db": 10.0,
                 "phrase": "marvin", "endpoint_sample": 30000}]}

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _write_flac(self, rel: str, endpoint: int, fs: int) -> str:
        p = self.store.holdout() / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        x = np.zeros(3 * fs)
        t = np.arange(endpoint - 8000, endpoint)
        x[endpoint - 8000:endpoint] = 0.4 * np.sin(2.0 * np.pi * 440.0 * t / fs)
        soundfile.write(str(p), x, fs, format="FLAC", subtype="PCM_16")
        return sha256_file(p)

    def _holdout(self, doc: dict) -> kws_holdout.Holdout:
        path = self.dir / "holdout.json"
        path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        return kws_holdout.load_holdout(path)

    def _refused(self, doc: dict, *needles: str, at_load: bool = False) -> None:
        with self.assertRaises(kws_holdout.HoldoutError) as cm:
            h = self._holdout(doc)
            if at_load:
                self.fail("refusal expected at load")
            kws_holdout.verify_holdout(h, self.store)
        for needle in needles:
            self.assertIn(needle, str(cm.exception))

    def test_5a_a_matching_tier_verifies_and_yields_positive_streams(self) -> None:
        h = self._holdout(self.doc)
        kws_holdout.verify_holdout(h, self.store)
        set_id = kws_holdout.holdout_set_id(h)
        self.assertRegex(set_id, r"^[0-9a-f]{64}$")
        streams = kws_holdout.streams_from_holdout(h, self.store, self.sc)
        self.assertEqual([s.id for s in streams],
                         ["holdout/T01/room-a/take-001", "holdout/T02/room-b/take-007"])
        for s, u in zip(streams, h.utterances):
            self.assertEqual((s.share, s.subshare, s.negative_samples, s.members),
                             ("positives", "holdout", 0, [u.file]))
            self.assertEqual(s.positives, [Positive(s.id, u.endpoint_sample, u.endpoint_sample // 160)])
            self.assertEqual(s.samples().size, 3 * RATE)
        kws_streams.validate_streams(streams, self.sc)
        self.assertEqual(kws_streams.positives_per_subshare(streams), {"holdout": 2})
        # the round trip and the CLI
        kws_holdout.save_holdout(h, self.dir / "again.json")
        self.assertEqual(kws_holdout.load_holdout(self.dir / "again.json").to_dict(), h.to_dict())
        self.assertEqual(kws_holdout.main(["--holdout", str(self.dir / "holdout.json"),
                                           "--store", str(self.store.root)]), 0)

    def test_5b_one_altered_byte_in_a_flac_is_refused_naming_the_file(self) -> None:
        p = self.store.holdout() / "T02/room-b/take-007.flac"
        data = bytearray(p.read_bytes())
        data[len(data) // 2] ^= 0x01
        p.write_bytes(bytes(data))
        self._refused(self.doc, "utterance 'T02/room-b/take-007.flac'", "sha256", "!= the recorded")
        self.assertEqual(kws_holdout.main(["--holdout", str(self.dir / "holdout.json"),
                                           "--store", str(self.store.root)]), 2)

    def test_5c_talker_and_consent_refusals_by_name(self) -> None:
        missing = copy.deepcopy(self.doc)
        missing["utterances"][1]["talker"] = "T99"
        self._refused(missing, "utterance 'T02/room-b/take-007.flac'", "talker 'T99' has no talkers[] row")
        no_version = copy.deepcopy(self.doc)
        del no_version["talkers"][0]["consent_form_version"]
        self._refused(no_version, "utterance 'T01/room-a/take-001.flac'",
                      "talker 'T01' has no consent_form_version")
        null_version = copy.deepcopy(self.doc)
        null_version["talkers"][0]["consent_form_version"] = None
        self._refused(null_version, "talker 'T01' has no consent_form_version")
        for dropped in ("evaluation", "m7-replay"):
            doc = copy.deepcopy(self.doc)
            doc["talkers"][1]["permitted_uses"].remove(dropped)
            self._refused(doc, "utterance 'T02/room-b/take-007.flac'", "talker 'T02' permitted_uses",
                          f"lack ['{dropped}']")
        no_uses = copy.deepcopy(self.doc)
        del no_uses["talkers"][1]["permitted_uses"]
        self._refused(no_uses, "lack ['evaluation', 'm7-replay']")

    def test_5d_the_set_id_changes_when_a_row_is_dropped(self) -> None:
        full = kws_holdout.holdout_set_id(self._holdout(self.doc))
        dropped = copy.deepcopy(self.doc)
        del dropped["utterances"][1]
        self.assertNotEqual(kws_holdout.holdout_set_id(self._holdout(dropped)), full)
        reordered = copy.deepcopy(self.doc)
        reordered["utterances"].reverse()
        self.assertEqual(kws_holdout.holdout_set_id(self._holdout(reordered)), full)  # order-free

    def test_5d2_one_file_is_one_utterance(self) -> None:
        # the same FLAC under a second spelling is refused by name at load: a './' or '//' spelling by the
        # path rule, a case-folded spelling (which APFS resolves to the file, Linux does not) by its digest
        # being already recorded — so neither the recall denominator nor the set id can count a file twice
        first = self.doc["utterances"][0]
        for spelling in ("./" + first["file"], first["file"].replace("room-a/", "room-a//"),
                         first["file"].replace("T01/", "T01/./")):
            doc = copy.deepcopy(self.doc)
            doc["utterances"].append({**first, "file": spelling})
            self._refused(doc, repr(spelling), "relative POSIX path", at_load=True)
        doc = copy.deepcopy(self.doc)
        doc["utterances"].append({**first, "file": first["file"].replace("take-001", "TAKE-001")})
        self._refused(doc, "utterance 'T01/room-a/TAKE-001.flac'", "sha256 already recorded for utterance "
                      "'T01/room-a/take-001.flac'", at_load=True)
        # the set id counts each digest once even on a record that bypassed validate()
        h = self._holdout(self.doc)
        doubled = dataclasses.replace(h, utterances=h.utterances + (h.utterances[0],))
        self.assertEqual(kws_holdout.holdout_set_id(doubled), kws_holdout.holdout_set_id(h))
        # and two rows that resolve to one file are refused by verify_holdout even if both spellings pass
        # the path rule (a symlink beside the master); the digest rule fires first at load, so the row
        # carries a wrong digest to reach verify
        link = self.store.holdout() / "T01/room-a/alias.flac"
        link.symlink_to(self.store.holdout() / "T01/room-a/take-001.flac")
        doc = copy.deepcopy(self.doc)
        doc["utterances"].append({**first, "file": "T01/room-a/alias.flac", "sha256": "0" * 64})
        self._refused(doc, "utterance 'T01/room-a/alias.flac'", "names the same file as utterance "
                      "'T01/room-a/take-001.flac'")

    def test_5f_the_holdout_cli_scores_a_verified_record_and_refuses_an_altered_one(self) -> None:
        # kws_eval.py holdout: verify, then score through the band-energy baseline; the report carries the
        # record's set id and its two positives; after one flipped byte nothing is written
        self._holdout(self.doc)
        out = self.dir / "report"
        argv = ["holdout", "--holdout", str(self.dir / "holdout.json"), "--manifest", str(MANIFEST),
                "--store", str(self.store.root), "--thresholds", "5", "--out", str(out)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(kws_eval.main(argv), 0)
        report = kws_eval.read_report(out / "report.json")
        self.assertEqual(report.holdout_set_id, kws_holdout.holdout_set_id(kws_holdout.load_holdout(
            self.dir / "holdout.json")))
        self.assertEqual(report.positives, {"holdout": 2})
        self.assertEqual(report.hours, {})   # no lock: no negative share, no FA/h column that is not absent
        self.assertIsNone(report.max_stream_s)
        self.assertTrue((out / "report.md").is_file())
        p = self.store.holdout() / "T02/room-b/take-007.flac"
        data = bytearray(p.read_bytes())
        data[len(data) // 2] ^= 0x01
        p.write_bytes(bytes(data))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(kws_eval.main(argv[:-1] + [str(out / "altered")]), 1)
        self.assertIn("utterance 'T02/room-b/take-007.flac': sha256", err.getvalue())
        self.assertFalse((out / "altered").exists())
        # and the verifier CLI refuses a missing store by name, rc 2, never a traceback
        err = io.StringIO()
        with mock.patch.dict(os.environ), contextlib.redirect_stderr(err):
            os.environ.pop(ENV_VAR, None)
            self.assertEqual(kws_holdout.main(["--holdout", str(self.dir / "holdout.json")]), 2)
        self.assertTrue(err.getvalue().startswith("refused: no feature store"), err.getvalue())

    def test_5e_schema_path_and_tier_refusals(self) -> None:
        def mutate(fn):
            doc = copy.deepcopy(self.doc)
            fn(doc)
            return doc
        first = lambda d: d["utterances"][0]  # noqa: E731
        self._refused(mutate(lambda d: first(d).update(bogus=1)), "utterance 'T01/room-a/take-001.flac'",
                      "unknown field(s) ['bogus']", at_load=True)
        self._refused(mutate(lambda d: first(d).pop("phrase")), "missing required field(s) ['phrase']",
                      at_load=True)
        self._refused(mutate(lambda d: d["talkers"][0].update(email="x")), "talker 'T01'", "['email']",
                      at_load=True)
        self._refused(mutate(lambda d: d.update(holdout_version=2)), "holdout_version 2", at_load=True)
        self._refused(mutate(lambda d: d.update(extra=1)), "unknown top-level field(s) ['extra']",
                      at_load=True)
        for bad in ("../outside.flac", "/abs/take.flac", "T01\\take.flac", "T01/take.wav", ""):
            self._refused(mutate(lambda d, bad=bad: first(d).update(file=bad)), repr(bad), at_load=True)
        self._refused(mutate(lambda d: first(d).update(sha256="abc")), "64 lowercase hex", at_load=True)
        self._refused(mutate(lambda d: first(d).update(endpoint_sample=-1)), "endpoint_sample", at_load=True)
        self._refused(mutate(lambda d: first(d).update(file="T02/room-b/take-007.flac")), "listed twice",
                      at_load=True)
        self._refused(mutate(lambda d: d["talkers"].append(dict(d["talkers"][0]))), "duplicate pseudonym",
                      at_load=True)
        # the tier: a missing file, a stray FLAC no row names, no tier at all
        self._refused(mutate(lambda d: d["utterances"].pop(1)), "holds 1 FLAC(s) no holdout.json row names",
                      "T02/room-b/take-007.flac")
        (self.store.holdout() / "T02/room-b/take-007.flac").unlink()
        self._refused(self.doc, "utterance 'T02/room-b/take-007.flac'", "is missing from the hold-out tier")
        # a listed FLAC that is not 16 kHz is refused when its stream is decoded, by name
        sha = self._write_flac("T02/room-b/take-007.flac", 30000, 22050)
        doc = mutate(lambda d: d["utterances"][1].update(sha256=sha))
        h = self._holdout(doc)
        kws_holdout.verify_holdout(h, self.store)
        streams = kws_holdout.streams_from_holdout(h, self.store, self.sc)
        with self.assertRaises(kws_holdout.HoldoutError) as cm:
            streams[1].samples()
        self.assertIn("utterance 'T02/room-b/take-007.flac': 22050 Hz", str(cm.exception))
        with self.assertRaises(kws_holdout.HoldoutError):
            kws_holdout.verify_holdout(h, Store(self.dir / "no-such-store"))


if __name__ == "__main__":
    unittest.main()
