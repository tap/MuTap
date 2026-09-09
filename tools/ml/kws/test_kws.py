#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The M4a pass (wake-word plan §6 M4a), one test per item, over the committed toy fixture.

    .venv/bin/python -m unittest tools/ml/kws/test_kws.py            # from the repo root
    .venv/bin/python -m unittest discover -s tools/ml/kws -p 'test_*.py' -v   # the kws-dataset CI job

1. The toy manifest rebuilds into an empty store (a temporary directory, never the developer's
   MUTAP_KWS_STORE) and reproduces the committed lock's identity fields exactly (`compare_locks`) and
   its features within FEATURE_TOLERANCE.
2. The toy lock is byte-identical between `--jobs 1` and `--jobs 4`.
3. A corrupted archive (a manifest with a wrong sha256; a copied toy tarball with one byte changed) is
   refused by `fetch`, and `decode` refuses an unverified archive — as do augment, extract and shard.
4. Each planted leak fixture is rejected by `verify_splits.py` by rule name (R1–R5); the toy lock is clean.
5. The planted unlicensed clip and the planted CC BY-NC track fail the card's check; the committed toy
   card is exactly what the card renders from the committed lock.
6. The self-check holds at the reference geometry (and at the toy manifest's geometry, which is the
   reference); a build at another geometry writes both records into its lock.
7. A toy source whose origin is in the repository but whose `redistributable` is false is refused.
Plus the refusals the builder implements under the "refuse, never warn" rule (the manifest schema and
every recipe knob, escaping ids and file names, a non-audio archive member, a silent noise mix, a
contract-version mismatch, an empty RIR pool, a shard stamped by another DspTap commit), the bridge's
commit marker, the synth stage (a mocked piper, and the committed hand-run record), the text I/O
encodings, and the two repository entries the milestone adds: the THIRD_PARTY_NOTICES.md rows and the
`kws-dataset` CI job.

The two toy builds run once per test process (setUpClass), through the CLI in a subprocess so the
`--jobs 4` path spawns a real process pool. Measured 9 September 2026 on the M0 Mac (Apple silicon,
CPython 3.12.14, numpy 2.5.3, scipy 1.18.1, soundfile 0.14.0, DspTap 5ca3b1cd, the C ABI built Release),
this suite's setUpClass over the committed expected outputs: `all --jobs 4` 2.65 s wall, `all --jobs 1`
0.82 s wall; 23 decoded clips, 17 draws, 29 featurized rows, 6924 frames, 3 shards.
"""
from __future__ import annotations

import contextlib
import copy
import dataclasses
import hashlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from fractions import Fraction
from unittest import mock

import numpy as np
import soundfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import kws_audio  # noqa: E402
import kws_build  # noqa: E402
import kws_dataset_card as card  # noqa: E402
import kws_features  # noqa: E402
import kws_sources  # noqa: E402
import verify_splits  # noqa: E402
from kws_manifest import (Archive, Lock, ManifestError, compare_locks, load_manifest,  # noqa: E402
                          manifest_hash, read_lock, write_lock)
from kws_store import ENV_VAR, Store, StoreError, sha256_file  # noqa: E402

REPO_ROOT = HERE.parents[2]
PYTHON = sys.executable
BUILD = HERE / "kws_build.py"
CARD = HERE / "kws_dataset_card.py"
VERIFY = HERE / "verify_splits.py"
TOY = HERE / "fixtures" / "toy"
MANIFEST = TOY / "manifest.json"
EXPECTED = TOY / "expected"
HAND_RUN = HERE / "fixtures" / "synth_hand_run"
SPLITS = ("train", "dev", "eval")
TTS_FIELDS = ("voice", "voice_sha256", "length_scale", "noise_scale", "noise_w", "text_variant", "text",
              "source_rate", "endpoint_dry")

# Feature agreement between a rebuild and the committed shards:
#     |rebuilt - expected| <= FEATURE_TOLERANCE * max(1, |expected|).
# Measured 9 September 2026 on the M0 Mac: a rebuild reproduces the committed shards bit for bit (max
# difference 0.0; the per-shard sha256 matches), as it must on the machine and toolchain that produced
# them. The 1e-6 relative bound covers ONLY the front end's double arithmetic on another toolchain (CI's
# ubuntu-latest, g++ instead of clang): FMA contraction and libm rounding differ at the 1e-15 level, which
# after the float32 store can flip a value by one ulp — 1.2e-7 at |F| in [1, 2), 15x inside the bound; the
# log-mel values here lie in [-1.0, 1.9] (measured [-1.0000, 1.8115]), so the bound is effectively absolute.
# An int16 one-LSB rounding difference in a rendered variant (resample_poly, fftconvolve, then int16
# rounding of the augmented tier) is OUTSIDE the bound and meant to fail: measured 9 September 2026 on the
# M0 Mac at the toy geometry over the 69 pcm/augmented WAVs of a toy build (probe: flip one sample by
# one LSB at the centre of a frame, re-extract, compare float32-stored features), one flipped sample moves
# the worst feature by 4.9e-6 (loudest frame) to 1.0e-1 in non-silent frames and by 0.27-0.41 in a
# digitally silent frame (the value leaves log_floor's -1.0) — every case >= 4.9x the bound. A failure at
# that magnitude is a render-determinism event, not front-end drift; the failure message says which row.
FEATURE_TOLERANCE = 1e-6


def build_env(store: pathlib.Path) -> dict[str, str]:
    """The subprocess environment: the store is the temporary one, whatever the developer's shell says."""
    env = dict(os.environ)
    env[ENV_VAR] = str(store)
    return env


def run_build(stage: str, manifest: pathlib.Path, store: pathlib.Path, jobs: int = 1,
              python_args: tuple[str, ...] = (),
              env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run([PYTHON, *python_args, str(BUILD), stage, "--manifest", str(manifest), "--store",
                           str(store), "--jobs", str(jobs)], capture_output=True, text=True, check=False,
                          env=env or build_env(store), cwd=str(REPO_ROOT))


def features_dir(store: pathlib.Path) -> pathlib.Path:
    """Where the build put its features: by the hash of the manifest built, not the expected lock's."""
    return store / "features" / manifest_hash(load_manifest(MANIFEST))


def toy_doc() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def write_doc(doc: dict, path: pathlib.Path) -> pathlib.Path:
    path.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    return path


def rewrite_tarball(src: pathlib.Path, dest: pathlib.Path, replace: dict[str, bytes]) -> None:
    """A copy of a toy tarball with some members' bytes replaced (same names, same layout)."""
    with tarfile.open(src) as tin, tarfile.open(dest, "w:gz") as tout:
        for m in tin:
            data = tin.extractfile(m).read() if m.isreg() else b""
            data = replace.get(m.name, data)
            info = tarfile.TarInfo(m.name)
            info.size, info.mtime, info.mode, info.type = len(data), 0, m.mode, m.type
            tout.addfile(info, io.BytesIO(data) if m.isreg() else None)


def store_only(doc: dict, index: int, store: pathlib.Path, archive_bytes: bytes) -> dict:
    """The toy source `index` as a store-only source (a human placed `archive_bytes` under archives/<id>/)."""
    src = doc["sources"][index]
    src["origin"] = {"url": "https://example.invalid/" + src["archive"]["file"],
                     "obtain": "place the tarball under archives/<id>/ by hand"}
    src["redistributable"] = False
    dest = store / "archives" / src["id"] / src["archive"]["file"]
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(archive_bytes)
    src["archive"]["sha256"] = sha256_file(dest)
    src["archive"]["size"] = dest.stat().st_size
    return src


class ToyBuild(unittest.TestCase):
    """Items 1 and 2: two builds of the toy manifest into two empty temporary stores."""

    tmp: tempfile.TemporaryDirectory
    stores: dict[int, pathlib.Path]
    seconds: dict[int, float]

    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-toy-")
        cls.stores = {}
        cls.seconds = {}
        for jobs in (4, 1):
            store = pathlib.Path(cls.tmp.name) / f"store-jobs{jobs}"
            store.mkdir()
            t0 = time.time()
            r = run_build("all", MANIFEST, store, jobs)
            cls.seconds[jobs] = time.time() - t0
            if r.returncode != 0:
                cls.tmp.cleanup()
                raise AssertionError(f"toy build --jobs {jobs} failed (rc {r.returncode}):\n"
                                     f"{r.stdout}\n{r.stderr}")
            cls.stores[jobs] = store

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmp.cleanup()

    def test_1_rebuild_reproduces_the_committed_lock_and_features(self) -> None:
        manifest = load_manifest(MANIFEST)
        expected = read_lock(EXPECTED / "lock.json")
        actual = read_lock(features_dir(self.stores[4]) / "lock.json")
        pcm_exact = {s.id for s in manifest.sources if s.pcm_exact}
        self.assertEqual(compare_locks(expected, actual, pcm_exact), [])
        exp_sha = {(c.id, c.variant): c.pcm_sha256 for c in expected.clips}
        act_sha = {(c.id, c.variant): c.pcm_sha256 for c in actual.clips}
        worst = 0.0
        for split in SPLITS:
            with np.load(EXPECTED / "features" / split / "shard-0000.npz") as e, \
                    np.load(features_dir(self.stores[4]) / split / "shard-0000.npz") as a:
                for name in ("clip_ids", "clip_offsets", "clip_labels", "clip_endpoints", "clip_variants",
                             "geometry"):
                    np.testing.assert_array_equal(a[name], e[name], err_msg=f"{split}/{name}")
                self.assertEqual(a["geometry"].dtype, np.float64)  # the README's shard table: float64 (18)
                self.assertEqual(a["geometry"].shape, (len(kws_features.Geometry.ARRAY_FIELDS),))
                header_e, header_a = json.loads(str(e["header"])), json.loads(str(a["header"]))
                # the DspTap commit is derived: the rebuilt header names this checkout's submodule
                # commit, the committed one names the commit the expected shards were cut at (they
                # are reported, never compared — a DspTap pin move that leaves log_mel.h alone keeps
                # the features, and the tolerance below is the check that matters)
                self.assertEqual(header_a.pop("dsptap_commit"), kws_features.dsptap_commit())
                cut_at = header_e.pop("dsptap_commit")
                self.assertEqual(header_a, header_e)
                self.assertEqual(header_a["stored_path"], manifest.recipe.stored_path)  # from the geometry
                fe, fa = e["features"].astype(np.float64), a["features"].astype(np.float64)
                self.assertEqual(fa.shape, fe.shape)
                self.assertEqual(a["features"].dtype, np.float32)
                diff = np.abs(fa - fe)
                bound = FEATURE_TOLERANCE * np.maximum(1.0, np.abs(fe))
                worst = max(worst, float(diff.max()))
                if not np.all(diff <= bound):
                    row, band = np.unravel_index(int(np.argmax(diff)), diff.shape)
                    i = int(np.searchsorted(e["clip_offsets"], row, side="right") - 1)
                    clip_id, variant = str(e["clip_ids"][i]).split("#")[0], int(e["clip_variants"][i])
                    frame = row - int(e["clip_offsets"][i])
                    render = ""
                    if variant:
                        same = exp_sha.get((clip_id, variant)) == act_sha.get((clip_id, variant))
                        verdict = "equals" if same else "DIFFERS FROM"
                        render = (f"; the rebuilt variant's pcm_sha256 {verdict} the committed lock's (a "
                                  "differing digest is a render-determinism event, an equal one front-end "
                                  "drift)")
                    self.fail(f"{split}: max |rebuilt - expected| = {diff.max():.3e} exceeds "
                              f"{FEATURE_TOLERANCE:.0e} (measured 0.0 on the M0 Mac) at clip {clip_id} "
                              f"variant {variant}, frame {frame}, band {band}{render}")
        # the shards' own digests are derived; report them beside the measured difference
        digests = {s.split: s.sha256 for s in actual.shards}
        same = sum(digests[s.split] == s.sha256 for s in expected.shards)
        print(f"\ntoy rebuild: max |feature difference| {worst:.3e}; {same}/{len(expected.shards)} shard "
              f"digests identical; expected shards cut at DspTap {cut_at[:8]}, this checkout "
              f"{kws_features.dsptap_commit()[:8]}; --jobs 4 {self.seconds[4]:.2f} s, "
              f"--jobs 1 {self.seconds[1]:.2f} s wall")

    def test_2_lock_is_byte_identical_between_jobs_1_and_4(self) -> None:
        lock4 = (features_dir(self.stores[4]) / "lock.json").read_bytes()
        lock1 = (features_dir(self.stores[1]) / "lock.json").read_bytes()
        self.assertEqual(lock4, lock1)
        for split in SPLITS:  # and the shards, since the arrays and the zip timestamps are fixed
            self.assertEqual(sha256_file(features_dir(self.stores[4]) / split / "shard-0000.npz"),
                             sha256_file(features_dir(self.stores[1]) / split / "shard-0000.npz"), split)

    def test_2b_the_build_left_the_expected_tiers_and_nothing_in_the_developer_store(self) -> None:
        store = self.stores[4]
        for tier in ("archives", "extracted", "pcm", "features"):
            self.assertTrue((store / tier).is_dir(), tier)
        lock = read_lock(features_dir(store) / "lock.json")
        self.assertEqual(lock.summary["decoded_clips"], 23)
        self.assertEqual(lock.summary["featurized_rows"], 29)
        self.assertEqual({s.split: s.frames for s in lock.shards}, {"train": 3962, "dev": 2072, "eval": 890})
        self.assertNotEqual(str(store), os.environ.get(ENV_VAR, ""))

    def test_2c_the_lock_records_the_applied_speed_and_the_cards_class_balance_vocabulary(self) -> None:
        lock = read_lock(features_dir(self.stores[1]) / "lock.json")
        sped = [c for c in lock.clips if c.draw and c.draw["speed"] != 1.0]
        self.assertGreater(len(sped), 0)
        for c in sped:  # the recorded speed is the rational change_speed applies, and it round-trips
            frac = Fraction(c.draw["speed"]).limit_denominator(kws_build.SPEED_DENOMINATOR)
            self.assertEqual(frac.numerator / frac.denominator, c.draw["speed"], c.id)
            self.assertEqual(kws_build.applied_speed(c.draw["speed"]), c.draw["speed"], c.id)
            if c.label == 1:  # e' from the applied factor; the row's length is the gain+speed file's
                self.assertEqual(c.endpoint_sample, kws_audio.transform_endpoint(
                    c.extra["endpoint_dry"], c.draw["speed"], c.draw["rir_delay"], c.draw["offset"]))
                dry_length = next(d.length for d in lock.clips if d.id == c.id and d.variant == 0)
                self.assertEqual(c.length, kws_build.rendered_length(dry_length, c.draw["speed"], None))
        # the builder and the card share one class-balance vocabulary: no key falls into the sorted tail
        self.assertEqual(card.class_balance_columns(lock.class_balance),
                         ["source", "origin", "render", "keyword", "split", "share", "label", "count",
                          "hours"])
        self.assertEqual({r["origin"] for r in lock.class_balance}, {"real"})
        self.assertEqual({r["render"] for r in lock.class_balance}, {"dry", "augmented"})

    def test_2d_a_treated_positive_has_its_context_treated_too(self) -> None:
        # The draw's noise covers the whole mixture: the 100 ms before the keyword is not silent context
        # (the toy contexts include Speech Commands clips with silent tails; with the noise over the
        # mixture, a window right before the keyword carries the drawn noise's energy).
        store = self.stores[1]
        manifest = load_manifest(MANIFEST)
        build = kws_build.Build(manifest, Store(store))
        state = json.loads((build.features_dir / kws_build.STATE_FILE).read_text(encoding="utf-8"))
        by_id = {c["id"]: c for c in state["clips"] if c["variant"] == 0}
        g = manifest.recipe.geometry
        n_trail = (kws_build.LATENCY_CEILING_HOPS + manifest.recipe.label.tolerance_hops) * g.hop + g.frame
        treated = [c for c in state["clips"] if c["label"] == 1 and c["variant"] and c["draw"]["noise"]]
        self.assertGreater(len(treated), 0)
        for c in treated:
            y = kws_audio.from_int16(kws_audio.read_pcm(build.augmented_path(c["id"], c["variant"])))
            x = kws_build.mixture(build, y, c, by_id, n_trail)
            self.assertEqual(x.size, c["extra"]["mixture_samples"])
            off = int(c["draw"]["offset"])
            before = x[off - 1600:off]
            self.assertGreater(float(np.sqrt(np.mean(before ** 2))), 0.0, c["id"])
            self.assertLessEqual(float(np.abs(x).max()), 0.99 + 1e-12)

    def test_2e_later_stages_refuse_an_unverified_archive(self) -> None:
        # item 3's other half: augment, extract and shard re-check every source's marker, not only decode
        store = self.stores[1]
        markers = list((store / "archives").glob("*/.verified"))
        self.assertGreater(len(markers), 0)
        for m in markers:
            m.unlink()
        try:
            for stage in ("shard", "augment"):
                r = run_build(stage, MANIFEST, store)
                self.assertEqual(r.returncode, 2, stage + r.stdout + r.stderr)
                self.assertIn("refused:", r.stderr)
                self.assertIn("not verified", r.stderr)
                self.assertIn("run `fetch` first", r.stderr)
                self.assertRegex(r.stderr, r"source '[A-Za-z0-9._-]+'")
        finally:
            self.assertEqual(run_build("fetch", MANIFEST, store).returncode, 0)


class CorruptedArchive(unittest.TestCase):
    """Item 3, and the other refusals of decode and the stage ledger."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-corrupt-")
        self.dir = pathlib.Path(self.tmp.name)
        self.store = self.dir / "store"
        self.store.mkdir()
        self.doc = toy_doc()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _manifest(self, doc: dict) -> pathlib.Path:
        return write_doc(doc, self.dir / "manifest.json")

    def test_3a_wrong_sha256_in_the_manifest_is_refused_by_fetch(self) -> None:
        doc = copy.deepcopy(self.doc)
        src = doc["sources"][0]
        src["archive"]["sha256"] = "0" * 64
        r = run_build("fetch", self._manifest(doc), self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("refused:", r.stderr)
        self.assertIn(src["id"], r.stderr)
        self.assertIn("sha256", r.stderr)
        self.assertFalse((self.store / "archives" / src["id"] / ".verified").exists())

    def test_3b_one_changed_byte_is_refused_by_fetch_and_decode_refuses_the_unverified_archive(self) -> None:
        doc = copy.deepcopy(self.doc)
        src = doc["sources"][0]
        # the same manifest row as a store-only source (a human placed the archive), with one byte changed
        data = bytearray((TOY / src["archive"]["file"]).read_bytes())
        data[len(data) // 2] ^= 0x01
        store_only(doc, 0, self.store, bytes(data))
        src["archive"]["sha256"] = self.doc["sources"][0]["archive"]["sha256"]  # the manifest keeps the truth
        src["archive"]["size"] = self.doc["sources"][0]["archive"]["size"]
        dest = self.store / "archives" / src["id"] / src["archive"]["file"]
        manifest = self._manifest(doc)
        r = run_build("fetch", manifest, self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn(src["id"], r.stderr)
        self.assertIn("sha256", r.stderr)
        self.assertFalse((dest.parent / ".verified").exists())
        r = run_build("decode", manifest, self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("not verified", r.stderr)
        self.assertIn("run `fetch` first", r.stderr)
        self.assertFalse((self.store / "extracted" / src["id"]).exists())

    def test_3c_decode_refuses_to_run_before_fetch(self) -> None:
        r = run_build("decode", MANIFEST, self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("run `fetch` first", r.stderr)

    def test_3d_a_contract_version_mismatch_is_refused_by_every_stage_after_fetch(self) -> None:
        doc = copy.deepcopy(self.doc)
        bumped = kws_features.contract_version() + 1
        doc["recipe"]["log_mel_contract_version"] = bumped
        manifest = self._manifest(doc)
        r = run_build("fetch", manifest, self.store)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)  # fetch does not consult the contract
        r = run_build("decode", manifest, self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("refused:", r.stderr)
        self.assertIn("recipe.log_mel_contract_version", r.stderr)
        self.assertIn(str(bumped), r.stderr)
        self.assertIn(f"dsptap_log_mel_contract_version() {kws_features.contract_version()}", r.stderr)
        self.assertFalse((self.store / "features").exists())

    def test_3e_an_empty_same_split_rir_pool_is_refused_at_augment_by_name(self) -> None:
        doc = copy.deepcopy(self.doc)
        doc["recipe"]["augment"]["rt60_s"] = [5.0, 6.0]  # the toy's RIRs measure 0.27 s and 0.32 s
        r = run_build("all", self._manifest(doc), self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("refused: clip ", r.stderr)
        self.assertRegex(r.stderr, r"variant \d+: no (train|dev)-split RIR inside rt60_s \[5\.0, 6\.0\]")
        self.assertIn("measured RT60s available", r.stderr)

    def test_3f_decode_refuses_a_non_16k_clip_under_pcm_exact(self) -> None:
        src = self.dir / "clip.wav"
        soundfile.write(str(src), np.zeros(22050) + 0.1, 22050, subtype="PCM_16")
        ctx = {"manifest": str(MANIFEST), "store": str(self.store)}
        task = {"id": "toy/x", "source": "speech_commands_v0.02_toy", "relative": "x", "path": str(src),
                "pcm_exact": True, "want_endpoint": False, "material": "speech"}
        with self.assertRaises(kws_build.BuildError) as cm:
            kws_build._decode_one(ctx, task)
        self.assertIn("clip toy/x", str(cm.exception))
        self.assertIn("declared pcm_exact", str(cm.exception))

    def test_3g_a_non_audio_member_of_a_hash_valid_archive_is_refused_by_decode_by_clip(self) -> None:
        doc = copy.deepcopy(self.doc)
        member = "./marvin/00176480_nohash_0.wav"
        planted = self.dir / "planted.tar.gz"
        rewrite_tarball(TOY / doc["sources"][0]["archive"]["file"], planted, {member: b"this is not a wav"})
        src = store_only(doc, 0, self.store, planted.read_bytes())
        manifest = self._manifest(doc)
        r = run_build("fetch", manifest, self.store)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        for jobs in (1, 4):
            r = run_build("decode", manifest, self.store, jobs)
            self.assertEqual(r.returncode, 2, f"--jobs {jobs}: " + r.stdout + r.stderr)
            self.assertIn("refused:", r.stderr)
            self.assertIn(f"clip {src['id']}/marvin/00176480_nohash_0: cannot decode", r.stderr)
            self.assertNotIn("Traceback", r.stderr)
        # a positive shorter than one label window is refused under that rule, not as "silent"
        short = np.full(8, 1000, dtype="<i2")
        buf = io.BytesIO()
        soundfile.write(buf, short, 16000, subtype="PCM_16", format="WAV")
        rewrite_tarball(TOY / self.doc["sources"][0]["archive"]["file"], planted, {member: buf.getvalue()})
        doc2 = copy.deepcopy(self.doc)
        store_only(doc2, 0, self.store, planted.read_bytes())
        manifest = self._manifest(doc2)
        self.assertEqual(run_build("fetch", manifest, self.store).returncode, 0)
        r = run_build("decode", manifest, self.store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("8 samples is shorter than one 10 ms label window (160 samples)", r.stderr)

    def test_3h_shard_refuses_rows_extracted_under_another_dsptap_commit(self) -> None:
        for stage in ("fetch", "decode", "augment", "extract"):
            r = run_build(stage, MANIFEST, self.store)
            self.assertEqual(r.returncode, 0, stage + r.stdout + r.stderr)
        state = json.loads((self.store / "features" / manifest_hash(load_manifest(MANIFEST)) / "clips.json")
                           .read_text(encoding="utf-8"))
        self.assertEqual(state["dsptap_commit"], kws_features.dsptap_commit())
        spoof = "0" * 40
        with mock.patch.object(kws_features, "dsptap_commit", lambda: spoof):
            err = io.StringIO()
            with mock.patch("sys.stderr", err):
                rc = kws_build.main(["shard", "--manifest", str(MANIFEST), "--store", str(self.store)])
        self.assertEqual(rc, 2, err.getvalue())
        self.assertIn(f"extract ran at DspTap {state['dsptap_commit']}, this checkout is {spoof}",
                      err.getvalue())
        self.assertIn("rerun extract", err.getvalue())
        self.assertFalse((self.store / "features" / state["manifest_hash"] / "lock.json").exists())


class ManifestRefusals(unittest.TestCase):
    """Every recipe knob and schema deviation is refused by name at load (refuse, never warn)."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-manifest-")
        self.dir = pathlib.Path(self.tmp.name)
        self.doc = toy_doc()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _refused(self, mutate, *needles: str) -> str:
        doc = copy.deepcopy(self.doc)
        mutate(doc)
        path = write_doc(doc, self.dir / "manifest.json")
        with self.assertRaises(ManifestError) as cm:
            load_manifest(path)
        for needle in needles:
            self.assertIn(needle, str(cm.exception))
        return str(cm.exception)

    def test_stored_path_must_be_the_path_the_geometry_computes(self) -> None:
        def pcen_label(d):
            d["recipe"]["stored_path"] = "pcen"
        def log_label(d):
            d["recipe"]["stored_path"] = "log"
            d["recipe"]["geometry"]["pcen"]["enabled"] = True
        self._refused(pcen_label, "recipe.stored_path 'pcen'", "pcen.enabled=False")
        self._refused(log_label, "recipe.stored_path 'log'", "pcen.enabled=True")

    def test_every_augment_and_label_knob_is_validated(self) -> None:
        def set_aug(k, v):
            def f(d):
                d["recipe"]["augment"][k] = v
            return f
        self._refused(set_aug("mic_model", "pico-v1"), "recipe.augment.mic_model 'pico-v1'", "['none']")
        self._refused(set_aug("rt60_s", [1.0, 0.2]), "recipe.augment.rt60_s", "reversed")
        self._refused(set_aug("gain_db", [6.0, -6.0]), "recipe.augment.gain_db", "reversed")
        self._refused(set_aug("speed", [0.0, 0.5]), "recipe.augment.speed", "positive")
        self._refused(set_aug("speed", [-0.5, 0.5]), "recipe.augment.speed")
        self._refused(set_aug("rt60_s", [-0.1, 1.0]), "recipe.augment.rt60_s", "below 0")
        self._refused(set_aug("snr_db", "loud"), "recipe.augment.snr_db", "expected [lo, hi]")
        self._refused(set_aug("snr_db", [0.0]), "recipe.augment.snr_db", "expected [lo, hi]")
        self._refused(set_aug("draws_per_positive", 1.5), "draws_per_positive", "integers")
        self._refused(set_aug("noisy_per_negative", True), "noisy_per_negative", "integers")

        def tol(d):
            d["recipe"]["label"]["tolerance_hops"] = 3.5
        self._refused(tol, "recipe.label.tolerance_hops", "integer")
        for value in (0.0, -1.0, 1.5):
            def ctx(d, value=value):
                d["recipe"]["context_s"] = value
            self._refused(ctx, "recipe.context_s", "2 s")

    def test_ids_paths_and_file_names_are_confined_to_the_store(self) -> None:
        def bad_id(d):
            d["sources"][0]["id"] = "../../outside"
        def dot_id(d):
            d["sources"][0]["id"] = ".."
        def bad_file(d):
            d["sources"][0]["archive"]["file"] = "../../../x.tar.gz"
        def bad_path(d):
            d["sources"][0]["origin"]["path"] = "tools/ml"
        self._refused(bad_id, "source '../../outside'", "single path segment")
        self._refused(dot_id, "single path segment")
        self._refused(bad_file, "archive.file must be a bare file name")
        self._refused(bad_path, "must lie under tools/ml/kws/fixtures/")
        # belt and braces: the store refuses the same values even when validate() is bypassed
        store = Store(self.dir / "store")
        (self.dir / "victim").mkdir()
        (self.dir / "victim" / "victim.txt").write_text("keep me", encoding="utf-8")
        with self.assertRaises(StoreError) as cm:
            store.extracted("../victim")
        self.assertIn("escapes the store", str(cm.exception))
        for escaping_id in ("../../outside", "..", ".", "", "a/b", "victim/../../victim"):
            with self.assertRaises(StoreError, msg=escaping_id):
                store.archives(escaping_id)
        self.assertEqual(store.pcm("ok", "dec", "res"), store.root / "pcm" / "ok" / "dec-res")
        toy = load_manifest(MANIFEST).sources[0]
        escaping = dataclasses.replace(toy, archive=Archive(file="../../../x.tar.gz",
                                                            sha256=toy.archive.sha256, size=toy.archive.size))
        with self.assertRaises(StoreError) as cm:
            store.fetch(escaping, REPO_ROOT)
        self.assertIn("archive.file", str(cm.exception))
        with self.assertRaises(StoreError):
            store.archive_path(escaping)
        self.assertTrue((self.dir / "victim" / "victim.txt").exists())
        self.assertFalse((self.dir / "store").exists())

    def test_schema_deviations_are_refused_by_name_not_as_tracebacks(self) -> None:
        def unknown_field(d):
            d["sources"][0]["licence_url"] = "https://example.invalid"
        def missing_licence(d):
            del d["sources"][0]["licence"]
        def missing_sources(d):
            del d["sources"]
        def bogus_geometry(d):
            d["recipe"]["geometry"]["bogus"] = 1
        def bogus_recipe(d):
            d["recipe"]["bogus"] = 1
        def bogus_pcen(d):
            d["recipe"]["geometry"]["pcen"]["gamma"] = 1
        self._refused(unknown_field, "source 'speech_commands_v0.02_toy'", "licence_url")
        self._refused(missing_licence, "source 'speech_commands_v0.02_toy'",
                      "missing required field(s) ['licence']")
        self._refused(missing_sources, "missing top-level field(s) ['sources']")
        self._refused(bogus_geometry, "recipe.geometry", "bogus")
        self._refused(bogus_recipe, "recipe", "bogus")
        self._refused(bogus_pcen, "recipe.geometry.pcen", "gamma")
        doc = copy.deepcopy(self.doc)
        unknown_field(doc)
        store = self.dir / "store"
        store.mkdir()
        r = run_build("fetch", write_doc(doc, self.dir / "manifest.json"), store)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertIn("refused: source 'speech_commands_v0.02_toy'", r.stderr)
        self.assertIn("licence_url", r.stderr)
        self.assertNotIn("Traceback", r.stderr)

    def test_the_tts_block_names_its_piper_source(self) -> None:
        def dead_piper(d):
            d["sources"].append({**d["sources"][0], "id": "voice", "kind": "piper", "roles": ["train"],
                                 "origin": {"url": "https://example.invalid/v", "obtain": "by hand"},
                                 "redistributable": False, "pcm_exact": False, "options": {}})
        def no_source(d):
            d["recipe"]["tts"] = {"voices": [{"id": "v", "onnx": "v.onnx", "config": "v.json",
                                              "sha256": "0" * 64}],
                                  "texts": {"positive": ["marvin"]}}
        self._refused(dead_piper, "kind 'piper' but recipe.tts is absent")
        self._refused(no_source, "recipe.tts voice 'v'", "missing ['source']")


class PlantedLeaks(unittest.TestCase):
    """Item 4."""

    def test_4_each_planted_fixture_is_rejected_by_its_rule(self) -> None:
        out = io.StringIO()
        self.assertEqual(verify_splits.self_test(out=out), [], out.getvalue())
        fixtures = verify_splits.planted_fixtures()
        self.assertEqual({expect["rule"] for _, expect in fixtures}, set(verify_splits.RULES))
        for d, expect in fixtures:
            with self.subTest(fixture=d.name):
                violations = verify_splits.verify(read_lock(d / "lock.json"))
                self.assertEqual(sorted({v.rule for v in violations}), [expect["rule"]])
                reported = {cid for v in violations for cid in v.clip_ids}
                self.assertTrue(set(expect["clips"]) <= reported, set(expect["clips"]) - reported)

    def test_4b_the_toy_lock_is_clean_on_all_five_rules(self) -> None:
        r = subprocess.run([PYTHON, str(VERIFY), "--lock", str(EXPECTED / "lock.json"), "--manifest",
                            str(MANIFEST)], capture_output=True, text=True, check=False)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("R1, R2, R3, R4, R5 clean", r.stdout)
        self.assertEqual(verify_splits.verify(read_lock(EXPECTED / "lock.json"), load_manifest(MANIFEST)), [])


class CardChecks(unittest.TestCase):
    """Item 5, over the toy lock."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="mutap-kws-card-")
        self.dir = pathlib.Path(self.tmp.name)
        self.lock = read_lock(EXPECTED / "lock.json")
        self.manifest = load_manifest(MANIFEST)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _check(self, lock) -> subprocess.CompletedProcess:
        path = self.dir / "lock.json"
        write_lock(lock, path)
        cmd = [PYTHON, str(CARD), "--manifest", str(MANIFEST), "--lock", str(path), "--check"]
        return subprocess.run(cmd, capture_output=True, text=True, check=False)

    def test_5_planted_unlicensed_clip_and_nc_track_fail_the_card(self) -> None:
        r = self._check(self.lock)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        dry = [c for c in self.lock.clips if c.variant == 0]
        speech = next(c for c in dry if c.source == "speech_commands_v0.02_toy")
        music = next(c for c in dry if c.material == "music" and c.split == "train")
        self.assertEqual(music.extra["licence"], "CC-BY")  # the per-file id the musan adapter classified

        unlicensed = copy.deepcopy(self.lock)
        next(c for c in unlicensed.clips if c.id == speech.id and c.variant == 0).extra["unlicensed"] = True
        r = self._check(unlicensed)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(f"clip {speech.id}: flagged unlicensed", r.stderr)

        nc = copy.deepcopy(self.lock)
        planted = next(c for c in nc.clips if c.id == music.id and c.variant == 0)
        planted.extra["licence"] = "CC-BY-NC-3.0"
        r = self._check(nc)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn(f"clip {music.id}: licence 'CC-BY-NC-3.0' carries a non-commercial (NC) term", r.stderr)
        with self.assertRaises(card.CardError):
            card.render(self.manifest, nc, self.dir / "never")
        self.assertFalse((self.dir / "never").exists())

    def test_5b_the_committed_toy_card_is_the_render_of_the_committed_lock(self) -> None:
        self.assertEqual((EXPECTED / card.CARD_NAME).read_text(encoding="utf-8"),
                         card.render_card(self.manifest, self.lock))
        self.assertEqual((EXPECTED / card.ATTRIBUTION_NAME).read_text(encoding="utf-8"),
                         card.render_attribution(self.manifest, self.lock))
        # the toy's build ran no synth, so the card cites the committed hand-run record
        text = card.render_card(self.manifest, self.lock)
        self.assertIn("fixtures/synth_hand_run/lock_excerpt.json", text)
        self.assertIn("| piper-tts | 1.8.0 | GPL-3.0-or-later |", text)


class SelfCheck(unittest.TestCase):
    """Item 6."""

    def test_6_self_check_holds_at_the_reference_and_manifest_geometries(self) -> None:
        # measured 2026-09-09 on the M0 Mac: 1.5e-14 (log) / 3.4e-14 (PCEN) at the reference geometry,
        # which the toy manifest shares; DspTap's CI measured the tuned geometry at 6.8e-14 / 1.5e-13 on
        # Linux GCC x86-64 (no FMA contraction), so SELF_CHECK_TOLERANCE follows DspTap's pin, 5e-13
        rec = kws_features.self_check(kws_features.REFERENCE)
        self.assertLess(rec["max_abs_diff_log"], kws_features.SELF_CHECK_TOLERANCE)
        self.assertLess(rec["max_abs_diff_pcen"], kws_features.SELF_CHECK_TOLERANCE)
        g = load_manifest(MANIFEST).recipe.geometry
        rec_m = kws_features.self_check(g)
        self.assertLess(rec_m["max_abs_diff_log"], kws_features.SELF_CHECK_TOLERANCE)
        self.assertLess(rec_m["max_abs_diff_pcen"], kws_features.SELF_CHECK_TOLERANCE)
        lock = read_lock(EXPECTED / "lock.json")
        self.assertEqual(lock.self_check["reference"]["geometry"], kws_features.REFERENCE.to_dict())
        self.assertLess(lock.self_check["reference"]["max_abs_diff_log"], kws_features.SELF_CHECK_TOLERANCE)
        if g == kws_features.REFERENCE:
            # one record; the card labels it "reference = manifest"
            self.assertNotIn("manifest", lock.self_check)
        else:
            self.assertEqual(lock.self_check["manifest"]["geometry"], g.to_dict())

    def test_6b_a_build_at_another_geometry_records_both_self_checks(self) -> None:
        # measured 9 September 2026 on the M0 Mac at 64 bands / fft 1024 / fmax 7000: 2.4e-14 (log) /
        # 1.5e-13 (PCEN), inside the 5e-13 pin
        doc = toy_doc()
        doc["recipe"]["geometry"].update({"bands": 64, "fft_size": 1024, "fmax_hz": 7000.0})
        with tempfile.TemporaryDirectory(prefix="mutap-kws-geometry-") as tmp:
            store = pathlib.Path(tmp) / "store"
            store.mkdir()
            manifest = write_doc(doc, pathlib.Path(tmp) / "manifest.json")
            r = run_build("all", manifest, store)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            lock = read_lock(store / "features" / manifest_hash(load_manifest(manifest)) / "lock.json")
        self.assertEqual(set(lock.self_check), {"reference", "manifest"})
        self.assertEqual(lock.self_check["manifest"]["geometry"]["bands"], 64)
        for rec in lock.self_check.values():
            self.assertLess(rec["max_abs_diff_log"], kws_features.SELF_CHECK_TOLERANCE)
            self.assertLess(rec["max_abs_diff_pcen"], kws_features.SELF_CHECK_TOLERANCE)
        rows, at_manifest = card.self_check_rows(lock, doc["recipe"]["geometry"])
        self.assertEqual(sorted(r[0] for r in rows), ["manifest", "reference"])  # the lock sorts its keys
        self.assertTrue(at_manifest)


class Redistributable(unittest.TestCase):
    """Item 7."""

    def test_7_in_repository_origin_with_redistributable_false_is_refused(self) -> None:
        doc = toy_doc()
        doc["sources"][0]["redistributable"] = False
        with tempfile.TemporaryDirectory(prefix="mutap-kws-redist-") as tmp:
            path = write_doc(doc, pathlib.Path(tmp) / "manifest.json")
            with self.assertRaises(ManifestError) as cm:
                load_manifest(path)
            self.assertIn(doc["sources"][0]["id"], str(cm.exception))
            self.assertIn("redistributable: true", str(cm.exception))
            store = pathlib.Path(tmp) / "store"
            store.mkdir()
            r = run_build("fetch", path, store)
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn("redistributable", r.stderr)
            self.assertFalse((store / "archives").exists())


class Primitives(unittest.TestCase):
    """The audio and adapter refusals the builder relies on."""

    def test_mix_snr_refuses_silence_instead_of_returning_the_input(self) -> None:
        rng = kws_audio.draw_rng(1, "t#noise", 1)
        noise = kws_audio.draw_rng(2, "n").standard_normal(32000) * 0.1
        with self.assertRaises(ValueError) as cm:
            kws_audio.mix_snr(np.zeros(16000), noise, 5.0, rng)
        self.assertIn("zero RMS", str(cm.exception))
        with self.assertRaises(ValueError) as cm:
            kws_audio.mix_snr(np.ones(16000) * 0.1, np.zeros(32000), 5.0, rng)
        self.assertIn("noise crop", str(cm.exception))
        # a noise clip that is not silent overall but whose drawn crop lands on a silent stretch
        partly = np.concatenate([np.zeros(20000), noise[:1000]])
        seed = next(s for s in range(100)
                    if int(kws_audio.draw_rng(s, "t#noise", 1).integers(0, partly.size - 16000 + 1)) <= 4000)
        with self.assertRaises(ValueError):
            kws_audio.mix_snr(np.ones(16000) * 0.1, partly, 5.0, kws_audio.draw_rng(seed, "t#noise", 1))
        # the reference span: the SNR is measured against it, the noise covers all of x
        x = np.concatenate([np.zeros(8000), np.ones(8000) * 0.1])
        y = kws_audio.mix_snr(x, noise, 0.0, kws_audio.draw_rng(3, "t#noise", 1), ref=slice(8000, 16000))
        self.assertAlmostEqual(kws_audio.rms(y[:8000] - x[:8000]), 0.1, delta=0.02)

    def test_keyword_list_member_is_confined_to_the_archive(self) -> None:
        source = load_manifest(MANIFEST).source("speech_commands_keywords")
        recipe = load_manifest(MANIFEST).recipe
        with tempfile.TemporaryDirectory(prefix="mutap-kws-keywords-") as tmp:
            root = pathlib.Path(tmp)
            (root / source.options["member"]).write_text("marvin\nsheila\n", encoding="utf-8")
            listing = kws_sources.list_keyword_list(source, root, recipe)
            self.assertEqual(listing.keywords, ["marvin", "sheila"])
            for member in ("/etc/hosts", "../../../../../../../../etc/hosts", "../x"):
                bad = dataclasses.replace(source, options={"member": member})
                with self.assertRaises(kws_sources.SourceError) as cm:
                    kws_sources.list_keyword_list(bad, root, recipe)
                self.assertIn("options.member", str(cm.exception))
                self.assertIn(member, str(cm.exception))

    def test_tts_synthesis_draws_use_their_own_generator_key(self) -> None:
        a = kws_audio.draw_rng(1, "tts/v/0/positive-000-000", 0).uniform()
        b = kws_audio.draw_rng(1, "tts/v/0/positive-000-000#tts", 0).uniform()
        self.assertNotEqual(a, b)

    def test_speed_draws_round_trip_through_the_rational(self) -> None:
        rng = np.random.Generator(np.random.PCG64(1))
        for drawn in np.round(rng.uniform(0.9, 1.1, 2000), 6):
            applied = kws_build.applied_speed(float(drawn))
            self.assertEqual(kws_build.applied_speed(applied), applied)
            _, used = kws_audio.change_speed(np.zeros(1600), applied)
            self.assertEqual(used, applied)


class Bridge(unittest.TestCase):
    """The C ABI bridge is keyed to the submodule commit it was built from."""

    def test_dsptap_commit_is_the_marked_head_of_a_clean_submodule(self) -> None:
        head = kws_features.dsptap_head()
        self.assertEqual(kws_features.dsptap_commit(), head)
        marker = kws_features.BRIDGE_BUILD / kws_features.BRIDGE_MARKER_NAME
        self.assertEqual(marker.read_text(encoding="utf-8").strip(), head)
        self.assertEqual(kws_features.dsptap_dirty(), [])

    def test_ensure_bridge_rebuilds_on_a_stale_or_missing_marker(self) -> None:
        head = kws_features.dsptap_head()
        calls: list[pathlib.Path] = []

        def fake_build(build_dir: pathlib.Path, verbose: bool = False) -> None:
            calls.append(build_dir)
            build_dir.mkdir(parents=True, exist_ok=True)
            names = {"linux": "libdsptap_capi.so", "darwin": "libdsptap_capi.dylib",
                     "win32": "dsptap_capi.dll"}
            (build_dir / next(v for k, v in names.items() if sys.platform.startswith(k))).write_bytes(b"")

        with tempfile.TemporaryDirectory(prefix="mutap-kws-bridge-") as tmp:
            build_dir = pathlib.Path(tmp) / "build_capi"
            marker = build_dir / kws_features.BRIDGE_MARKER_NAME
            with mock.patch.object(kws_features, "_build_bridge", fake_build):
                self.assertEqual(kws_features.ensure_bridge(build_dir), head)   # missing: built
                self.assertEqual(len(calls), 1)
                self.assertEqual(marker.read_text(encoding="utf-8").strip(), head)
                self.assertEqual(kws_features.ensure_bridge(build_dir), head)   # fresh: untouched
                self.assertEqual(len(calls), 1)
                marker.write_text("d3c110961207b3f52a46390f4c39772220e40203\n", encoding="utf-8")
                self.assertEqual(kws_features.ensure_bridge(build_dir), head)   # stale: rebuilt
                self.assertEqual(len(calls), 2)
                self.assertEqual(marker.read_text(encoding="utf-8").strip(), head)
            # and dsptap_commit refuses to name HEAD over a bridge built at another commit
            marker.write_text("d3c110961207b3f52a46390f4c39772220e40203\n", encoding="utf-8")
            with mock.patch.object(kws_features, "BRIDGE_BUILD", build_dir):
                with self.assertRaises(kws_features.BridgeError) as cm:
                    kws_features.dsptap_commit()
            self.assertIn("built at d3c11096", str(cm.exception))
            self.assertIn(head, str(cm.exception))


class Synth(unittest.TestCase):
    """The synth stage: the committed hand-run record, and the invocation over a mocked piper."""

    def test_hand_run_record_is_a_lock_excerpt_the_card_and_verify_splits_accept(self) -> None:
        manifest = load_manifest(HAND_RUN / "manifest.json")
        record = read_lock(HAND_RUN / "lock_excerpt.json")
        self.assertEqual(record.manifest_hash, manifest_hash(manifest))
        voices = {v["id"]: v for v in manifest.recipe.tts["voices"]}
        self.assertEqual(len(voices), 1)  # one pinned voice, as the M4a deliverable says
        rows = [c for c in record.clips if c.material == "tts"]
        # the excerpt: every TTS row, plus the dry rows their draws reference (R2 checks a draw's context
        # against the lock's dry clips), and nothing else
        referenced = {ref for c in rows for ref in kws_build.draw_refs(c.draw)} - {c.id for c in rows}
        self.assertEqual({c.id for c in record.clips if c.material != "tts"}, referenced)
        self.assertTrue(all(c.variant == 0 for c in record.clips if c.material != "tts"))
        self.assertGreater(sum(1 for c in rows if c.label == 1), 0)
        self.assertGreater(sum(1 for c in rows if c.label == 0), 0)
        for c in rows:
            self.assertEqual(c.speaker, "0")  # a single-speaker voice: verify_splits' R3 convention
            self.assertEqual(c.source, voices[c.extra["voice"]]["source"])
            self.assertTrue(c.id.startswith(c.source + "/"), c.id)
            self.assertEqual(manifest.source(c.source).kind, "piper")
            for field in TTS_FIELDS:
                self.assertIn(field, c.extra, c.id)
            self.assertEqual(c.extra["voice_sha256"], voices[c.extra["voice"]]["sha256"])
            self.assertEqual(c.extra["source_rate"], 22050)
            self.assertEqual(c.key, f"{c.extra['voice']}:0")
            if c.label == 1:
                self.assertIsNotNone(c.endpoint_sample)
                self.assertIsNotNone(c.extra["endpoint_dry"])
        self.assertEqual(card.check_licences(manifest, record), [])
        self.assertEqual(verify_splits.verify(record, manifest), [])
        for key in ("piper-tts", "onnxruntime", "espeak-ng", "resampler", "dsptap_commit"):
            self.assertIn(key, record.toolchain)
        self.assertRegex(record.toolchain["piper-tts"], r"^\d+\.\d+")
        self.assertRegex(record.toolchain["onnxruntime"], r"^\d+\.\d+")
        self.assertIn("embedded in piper-tts", record.toolchain["espeak-ng"])
        self.assertTrue(record.toolchain["resampler"].startswith("resample_poly-"))
        self.assertIn(card.HAND_RUN_RECORD, (HAND_RUN / "lock_excerpt.json",))
        self.assertFalse(list(HAND_RUN.glob("*.wav")))  # audio stays in the store

    def test_synth_over_a_mocked_piper_emits_rows_the_card_and_verify_splits_accept(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mutap-kws-synth-") as tmp:
            d = pathlib.Path(tmp)
            store = d / "store"
            store.mkdir()
            # a voice archive placed by hand: dummy model bytes, a real sha256
            onnx = b"not a real model, but a pinned one"
            buf = io.BytesIO()
            with tarfile.open(fileobj=buf, mode="w:gz") as tf:
                for name, data in (("fake/fake.onnx", onnx), ("fake/fake.onnx.json", b"{}")):
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    tf.addfile(info, io.BytesIO(data))
            doc = toy_doc()
            voice_src = copy.deepcopy(doc["sources"][0])
            voice_src.update({"id": "piper_fake", "kind": "piper", "release": "fake", "licence": "CC0-1.0",
                              "attribution": "a fake voice", "terms_accepted": "none",
                              "redistributable": False, "roles": ["train", "dev", "eval-tts"],
                              "pcm_exact": False, "options": {},
                              "origin": {"url": "https://example.invalid/fake", "obtain": "by hand"},
                              "archive": {"file": "fake.tar.gz", "sha256": "0" * 64, "size": 1}})
            doc["sources"].append(voice_src)
            store_only(doc, len(doc["sources"]) - 1, store, buf.getvalue())
            doc["recipe"]["tts"] = {
                "voices": [{"id": "fake", "source": "piper_fake", "onnx": "fake/fake.onnx",
                            "config": "fake/fake.onnx.json", "sha256": hashlib.sha256(onnx).hexdigest()}],
                "texts": {"positive": ["marvin"], "negative": ["sheila", "the weather is fine today"]},
                "positives_per_speaker": 2}
            manifest = write_doc(doc, d / "manifest.json")
            for stage in ("fetch", "decode"):
                r = run_build(stage, manifest, store)
                self.assertEqual(r.returncode, 0, stage + r.stdout + r.stderr)
            fake_piper = d / "piper"
            fake_piper.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            calls: list[list[str]] = []

            def fake_run(cmd, **kw):
                calls.append(list(cmd))
                out = pathlib.Path(cmd[cmd.index("--output_file") + 1])
                length = 22050 + 2205 * len(kw.get("input", ""))
                t = np.arange(length) / 22050.0
                soundfile.write(str(out), 0.3 * np.sin(2 * np.pi * 220.0 * t), 22050, subtype="PCM_16")
                return subprocess.CompletedProcess(cmd, 0, "", "")

            versions = {"piper-tts": "9.9.9-mock", "onnxruntime": "8.8.8-mock", "espeak-ng": "mock"}
            build = kws_build.Build(load_manifest(manifest), Store(store))
            quiet = contextlib.redirect_stdout(io.StringIO())  # the stages' progress lines
            with quiet, mock.patch.object(kws_build.shutil, "which", lambda name: str(fake_piper)), \
                    mock.patch.object(kws_build.subprocess, "run", fake_run), \
                    mock.patch.object(kws_build, "piper_versions", lambda piper: versions):
                kws_build.stage_synth(build)
            self.assertEqual(len(calls), 6)  # 1 positive text x 2 + 2 negative texts x 2
            for cmd in calls:
                self.assertEqual(cmd[0], str(fake_piper))
                self.assertIn("--length_scale", cmd)
                self.assertNotIn("--speaker", cmd)  # a single-speaker voice
            with quiet:
                for stage in ("augment", "extract", "shard"):
                    kws_build.run_stage(build, stage)
            lock = read_lock(build.features_dir / "lock.json")
            rows = [c for c in lock.clips if c.material == "tts"]
            self.assertEqual(len(rows), 6 + sum(1 for c in lock.clips if c.material == "tts" and c.variant))
            dry = [c for c in rows if c.variant == 0]
            self.assertEqual(len(dry), 6)
            for c in dry:
                self.assertEqual(c.source, "piper_fake")
                self.assertTrue(c.id.startswith("piper_fake/fake/0/"), c.id)
                self.assertEqual(c.speaker, "0")
                self.assertEqual(c.key, "fake:0")
                for field in TTS_FIELDS:
                    self.assertIn(field, c.extra, c.id)
                self.assertEqual(c.extra["source_rate"], 22050)
                self.assertIn(c.share, ("train", "dev", "eval-tts"))
                voice_source = build.manifest.source("piper_fake")
                self.assertEqual(c.share, kws_build.share_for(voice_source, c.split, "tts"))
            self.assertEqual(card.check_licences(build.manifest, lock), [])
            self.assertEqual(verify_splits.verify(lock, build.manifest), [])
            self.assertEqual(lock.toolchain["piper-tts"], "9.9.9-mock")
            self.assertEqual(lock.toolchain["onnxruntime"], "8.8.8-mock")
            text = card.render_card(build.manifest, lock)
            self.assertIn("| piper-tts | 9.9.9-mock | GPL-3.0-or-later |", text)
            self.assertIn("| ONNX Runtime | 8.8.8-mock | MIT |", text)
            self.assertNotIn("synth_hand_run", text)  # this build ran synth itself; no citation needed
            self.assertIn("| piper_fake | synthetic |", text)


class Encodings(unittest.TestCase):
    """The lock and every text file are UTF-8 whatever the locale says."""

    def test_the_committed_lock_reads_the_same_as_utf8_bytes(self) -> None:
        raw = json.loads((EXPECTED / "lock.json").read_bytes().decode("utf-8"))
        manifest = load_manifest(MANIFEST)
        self.assertEqual(compare_locks(read_lock(EXPECTED / "lock.json"), Lock.from_dict(raw),
                                       {s.id for s in manifest.sources if s.pcm_exact}), [])
        clips = read_lock(EXPECTED / "lock.json").clips
        self.assertTrue(any("“" in c.extra.get("attribution", "") for c in clips))

    def test_decode_runs_under_an_ascii_locale(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mutap-kws-locale-") as tmp:
            store = pathlib.Path(tmp) / "store"
            store.mkdir()
            env = build_env(store)
            env.update({"LC_ALL": "C", "LANG": "C", "PYTHONCOERCECLOCALE": "0", "PYTHONUTF8": "0",
                        "PYTHONIOENCODING": "ascii"})
            for stage in ("fetch", "decode"):
                r = run_build(stage, MANIFEST, store, python_args=("-X", "utf8=0"), env=env)
                self.assertEqual(r.returncode, 0, stage + r.stdout + r.stderr)
            state = json.loads((store / "features" / manifest_hash(load_manifest(MANIFEST)) / "clips.json")
                               .read_bytes().decode("utf-8"))
            self.assertTrue(any("“" in c["extra"].get("attribution", "") for c in state["clips"]))


class RepositoryEntries(unittest.TestCase):
    """The THIRD_PARTY_NOTICES.md entry and the CI job the milestone adds."""

    def test_third_party_notices_carry_the_subprocess_tools_and_the_toy_fixture(self) -> None:
        text = (REPO_ROOT / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")
        self.assertIn("Nothing here is GPL- or LGPL-encumbered", " ".join(text.split()))
        self.assertIn("piper-tts", text)
        self.assertIn("espeak-ng", text)
        self.assertIn("GPL-3.0-or-later", text)
        self.assertIn("never redistributed", text)
        self.assertIn("tools/ml/kws/fixtures/toy/", text)
        for p in sorted(TOY.iterdir()):
            if p.suffix in (".gz", ".zip"):
                self.assertIn(f"`{p.name}`", text)
        self.assertIn("fixtures/toy/expected/ATTRIBUTION.csv", text)

    def test_ci_runs_this_suite(self) -> None:
        text = (REPO_ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        self.assertIn("kws-dataset:", text)
        self.assertIn("tools/ml/kws/requirements.txt", text)
        self.assertIn("python -m unittest discover -s tools/ml/kws -p 'test_*.py' -v", text)
        self.assertIn("kws_features.py --ensure-bridge", text)

    def test_toy_fixture_is_within_budget_and_holds_its_expected_outputs(self) -> None:
        archives = sum(p.stat().st_size for p in TOY.iterdir() if p.suffix in (".gz", ".zip"))
        self.assertLessEqual(archives, 1_000_000)  # measured 662,293 bytes on 9 September 2026
        for split in SPLITS:
            self.assertTrue((EXPECTED / "features" / split / "shard-0000.npz").is_file(), split)
        self.assertTrue((EXPECTED / "lock.json").is_file())
        requirements = (HERE / "requirements.txt").read_text(encoding="utf-8")
        self.assertIn("Python >= 3.12", requirements)
        self.assertIn("Python ≥ 3.12", (HERE / "README.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
