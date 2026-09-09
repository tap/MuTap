#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""The dataset card's tests: a small synthetic manifest + lock renders, its tables agree with the lock,
and the plan's two planted cases — an unlicensed clip and a CC BY-NC track — each make `--check` fail
with a message naming the clip (wake-word plan §6 M4a pass item 5).

    .venv/bin/python -m unittest tools/ml/kws/test_dataset_card.py
"""
from __future__ import annotations

import contextlib
import copy
import csv
import io
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_dataset_card as card  # noqa: E402
from kws_features import Geometry  # noqa: E402
from kws_manifest import (  # noqa: E402
    Archive,
    Clip,
    Lock,
    Manifest,
    Origin,
    Recipe,
    Shard,
    Source,
    eval_set_id,
    manifest_hash,
    read_lock,
    save_manifest,
    validate,
    write_lock,
)

SHA = "0123456789abcdef" * 4
SECOND = 16000
NOISE_1 = "musan_noise/noise/free-sound/noise-0001"
NOISE_2 = "musan_noise/noise/free-sound/noise-0002"
MUSIC_1 = "musan_music/music/fma/music-fma-0001"
MUSIC_2 = "musan_music/music/fma/music-fma-0002"
RIR_1 = "slr28/simulated_rirs/smallroom/Room001/Room001-00001"


def make_source(sid: str, kind: str, licence: str, **kw) -> Source:
    origin = Origin(url=f"https://example.invalid/{sid}", obtain="download by hand")
    base = dict(id=sid, kind=kind, release=f"{sid}-release", origin=origin,
                archive=Archive(file=f"{sid}.tar.gz", sha256=SHA, size=1234), licence=licence,
                attribution=f"{sid} attribution text", terms_accepted="the upstream terms",
                terms_verified="2026-09-09", redistributable=False, roles=("train", "dev"), pcm_exact=True)
    base.update(kw)
    return Source(**base)


def make_manifest() -> Manifest:
    sources = (
        make_source("sc", "speech_commands_v2", "CC-BY-4.0", roles=("train", "dev", "eval-speech")),
        make_source("musan_music", "musan", "CC-BY-3.0 (per subset)", roles=("train", "eval-music"),
                    options={"partition": "music"}),
        make_source("musan_noise", "musan", "CC-BY-4.0", roles=("aug", "eval-noise"),
                    options={"partition": "noise"}),
        make_source("slr28", "openslr_28_simulated", "Apache-2.0", roles=("aug",), pcm_exact=False),
    )
    recipe = Recipe(phrase="marvin", near_miss=("marvel",), geometry=Geometry())
    m = Manifest(name="card-test", sources=sources, recipe=recipe)
    validate(m)
    return m


def clip(cid: str, source: str, material: str, split: str, share: str, label: int | None, length: int,
         variant: int = 0, **extra) -> Clip:
    endpoint = 8000 if label == 1 else None
    draw = None
    if variant:
        draw = {"gain_db": -3.0, "snr_db": 10.0, "noise": NOISE_1, "speed": 1.0, "rir": None,
                "rir_delay": 0, "context": [], "offset": 0}
    return Clip(id=cid, source=source, material=material, split=split, share=share, label=label,
                endpoint_sample=endpoint, length=length, key=cid.split("/")[-1], pcm_sha256=SHA,
                variant=variant, draw=draw, extra=dict(extra))


def make_lock(manifest: Manifest) -> Lock:
    clips = [
        clip("sc/marvin/aaa_nohash_0", "sc", "speech", "train", "train", 1, SECOND, keyword="marvin"),
        clip("sc/marvin/aaa_nohash_0", "sc", "speech", "train", "train", 1, SECOND, variant=1,
             keyword="marvin"),
        clip("sc/marvin/bbb_nohash_0", "sc", "speech", "dev", "dev", 1, SECOND, keyword="marvin"),
        clip("sc/marvin/ccc_nohash_0", "sc", "speech", "eval", "eval-speech", 1, SECOND, keyword="marvin"),
        clip("sc/sheila/aaa_nohash_0", "sc", "speech", "train", "train", 0, SECOND, keyword="sheila"),
        clip("sc/marvel/ddd_nohash_0", "sc", "speech", "eval", "eval-speech", 0, SECOND, keyword="marvel",
             near_miss=True),
        clip(MUSIC_1, "musan_music", "music", "train", "train", 0, 2 * SECOND, licence="CC-BY-3.0",
             attribution="Artist A — Track 1 (CC BY 3.0)", artist="Artist A"),
        clip(MUSIC_2, "musan_music", "music", "eval", "eval-music", 0, 2 * SECOND, licence="CC-BY-4.0",
             attribution="Artist B — Track 2 (CC BY 4.0)", artist="Artist B"),
        clip(NOISE_1, "musan_noise", "noise", "train", "aug", None, 3 * SECOND, licence="CC0-1.0",
             attribution="freesound user X (CC0)"),
        clip(NOISE_2, "musan_noise", "noise", "eval", "eval-noise", None, 3 * SECOND, licence="CC0-1.0",
             attribution="freesound user Y (CC0)"),
        clip(RIR_1, "slr28", "rir", "train", "aug", None, 4000),
    ]
    shards = [Shard(split="train", file="train/shard-0000.npz", sha256=SHA, frames=500, clips=5),
              Shard(split="dev", file="dev/shard-0000.npz", sha256=SHA, frames=100, clips=1),
              Shard(split="eval", file="eval/shard-0000.npz", sha256=SHA, frames=500, clips=4)]
    toolchain = {"python": "3.12.14", "numpy": "2.5.3", "scipy": "1.18.1", "soundfile": "0.14.0",
                 "decoder": "soundfile-0.14.0-libsndfile-1.2.2",
                 "resampler": "resample_poly-1.18.1-kaiser-5.0", "dsptap_commit": "deadbeef",
                 "contract_version": 1, "kws_features_version": 1}
    self_check = {"geometry": Geometry().to_dict(), "tolerance": 1e-13, "max_abs_diff_log": 1.5e-14,
                  "max_abs_diff_pcen": 3.4e-14}
    return Lock(manifest_hash=manifest_hash(manifest), clips=clips, shards=shards,
                summary={"train": {"positive": {"count": 2, "hours": 2 / 3600}}}, class_balance=[],
                eval_set_id={s: eval_set_id(clips, s) for s in ("eval-speech", "eval-music", "eval-noise")},
                holdout_set_id=None, toolchain=toolchain, self_check=self_check)


class CardFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest = make_manifest()
        self.lock = make_lock(self.manifest)
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.manifest_path = self.dir / "manifest.json"
        self.lock_path = self.dir / "lock.json"
        save_manifest(self.manifest, self.manifest_path)
        write_lock(self.lock, self.lock_path)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_main(self, *extra: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = card.main(["--manifest", str(self.manifest_path), "--lock", str(self.lock_path), *extra])
        return rc, out.getvalue(), err.getvalue()


class TestRender(CardFixture):
    def test_renders_both_files_and_tables_agree_with_the_lock(self) -> None:
        out_dir = self.dir / "out"
        rc, stdout, stderr = self.run_main("--out-dir", str(out_dir))
        self.assertEqual(rc, 0, stderr)
        text = (out_dir / card.CARD_NAME).read_text()
        # per-source rows: sc has 5 dry clips of 1 s = 5/3600 h; musan music 2 x 2 s
        self.assertIn("| sc | speech_commands_v2 | sc-release | speech | train, dev, eval-speech | 5 | "
                      "0.0014 | CC-BY-4.0 | 2026-09-09 | no | yes |", text)
        self.assertIn("| musan_music | musan | musan_music-release | music | train, eval-music | 2 | "
                      "0.0011 | CC-BY-3.0 (per subset) | 2026-09-09 | no | yes |", text)
        # split x class: train positives = 1 dry + 1 augmented
        self.assertIn("| train | positive | 1 | 0.0003 | 1 | 0.0003 | 2 | 0.0006 |", text)
        self.assertIn("| eval | negative | 2 | 0.0008 | 0 | 0.0000 | 2 | 0.0008 |", text)
        # partition: the eval noise share, the augmentation RIR pool
        self.assertIn("| musan_noise | noise | eval-noise | eval | 1 | 0.0008 |", text)
        self.assertIn("| slr28 | rir | aug | train | 1 | 0.0001 |", text)
        # split-key rule per source and the client-id approximation
        self.assertIn("| sc | speech_commands_v2 | the official hash split", text)
        self.assertIn("**Client-id approximation.**", text)
        # the phrase, the resampler, the self-check, the GPL tooling rows, the privacy statement
        self.assertIn("- phrase: `marvin`", text)
        self.assertIn("`scipy.signal.resample_poly` with window (kaiser, 5.0),", text)
        self.assertIn("`decimate.h` is never in the training path", text)
        # the fixture's manifest geometry is the reference geometry, so the one record covers both
        self.assertIn("| reference = manifest | 16000.0 Hz / 400 / 160 / 512 / 40 bands | 1e-13 | 1.5e-14 | "
                      "3.4e-14 |", text)
        self.assertNotIn("No self-check record ran at the manifest geometry", text)
        self.assertIn("| piper-tts | not run in this build", text)
        self.assertIn("| espeak-ng | not run in this build", text)
        self.assertIn("GPL-3.0-or-later", text)
        self.assertIn("## Hold-out: repository privacy statement", text)
        self.assertIn("**Withdrawal.**", text)
        self.assertIn("**History limit:**", text)
        self.assertIn(self.lock.eval_set_id["eval-music"], text)
        # class balance was derived (the fixture lock carries none) and lists the keyword column
        self.assertIn("recomputed from its clips", text)
        self.assertIn("| sc | real | dry | marvin | train | train | 1 | 1 | 0.0003 |", text)

        with (out_dir / card.ATTRIBUTION_NAME).open(newline="") as f:
            rows = list(csv.DictReader(f))
        self.assertEqual(tuple(rows[0].keys()), card.ATTRIBUTION_COLUMNS)
        source_rows = [r for r in rows if r["clip"] == ""]
        self.assertEqual([r["source"] for r in source_rows], ["sc", "musan_music", "musan_noise", "slr28"])
        per_file = [r for r in rows if r["clip"]]
        self.assertEqual(len(per_file), 4)  # the two music tracks and the two noise files carry attribution
        track = next(r for r in per_file if r["clip"] == MUSIC_2)
        self.assertEqual(track["licence"], "CC-BY-4.0")
        self.assertEqual(track["attribution"], "Artist B — Track 2 (CC BY 4.0)")
        self.assertEqual(track["roles"], "eval-music")
        # the augmented variant never gets its own attribution row
        self.assertFalse(any("#" in r["clip"] for r in per_file))

    def test_render_is_deterministic(self) -> None:
        self.assertEqual(card.render_card(self.manifest, self.lock),
                         card.render_card(self.manifest, self.lock))
        self.assertEqual(card.render_attribution(self.manifest, self.lock),
                         card.render_attribution(self.manifest, self.lock))

    def test_lock_class_balance_is_rendered_verbatim(self) -> None:
        self.lock.class_balance = [{"source": "sc", "origin": "real", "render": "dry", "keyword": "marvin",
                                    "split": "train", "share": "train", "label": 1, "count": 1,
                                    "hours": 1 / 3600}]
        text = card.render_card(self.manifest, self.lock)
        self.assertIn("The lock's class-balance table, verbatim.", text)
        self.assertIn("| source | origin | render | keyword | split | share | label | count | hours |", text)
        self.assertIn("| sc | real | dry | marvin | train | train | 1 | 1 | 0.0003 |", text)
        # the builder's rows and the card's fallback share one vocabulary: no key falls into the sorted tail
        self.assertEqual(card.class_balance_columns(self.lock.class_balance),
                         card.class_balance_columns(card.derived_class_balance(self.lock)))

    def test_self_check_at_another_geometry_is_labelled_and_the_gap_stated(self) -> None:
        other = Geometry(bands=32, fmax_hz=7000.0)
        self.lock.self_check = {"geometry": other.to_dict(), "tolerance": 1e-13, "max_abs_diff_log": 1e-15,
                                "max_abs_diff_pcen": 2e-15}
        text = card.render_card(self.manifest, self.lock)
        self.assertIn("| other | 16000.0 Hz / 400 / 160 / 512 / 32 bands |", text)
        self.assertIn("No self-check record ran at the manifest geometry", text)

    def test_check_mode_passes_and_writes_nothing(self) -> None:
        rc, stdout, stderr = self.run_main("--check", "--out-dir", str(self.dir / "never"))
        self.assertEqual(rc, 0, stderr)
        self.assertIn("pass the licence checks", stdout)
        self.assertFalse((self.dir / "never").exists())

    def test_lock_of_another_manifest_is_refused(self) -> None:
        self.lock.manifest_hash = "f" * 64
        write_lock(self.lock, self.lock_path)
        rc, _, stderr = self.run_main("--check")
        self.assertEqual(rc, 1)
        self.assertIn("lock.manifest_hash", stderr)


class TestPlantedCases(CardFixture):
    """Plan M4a pass item 5: the planted unlicensed clip and the planted CC BY-NC track fail the card test."""

    def test_planted_unlicensed_clip_fails_check_naming_the_clip(self) -> None:
        planted = "sc/sheila/aaa_nohash_0"
        for c in self.lock.clips:
            if c.id == planted:
                c.extra["unlicensed"] = True
        write_lock(self.lock, self.lock_path)
        rc, _, stderr = self.run_main("--check")
        self.assertEqual(rc, 1)
        self.assertIn(f"clip {planted}: flagged unlicensed", stderr)
        # rendering refuses too, and writes nothing
        out_dir = self.dir / "out"
        rc, _, stderr = self.run_main("--out-dir", str(out_dir))
        self.assertEqual(rc, 1)
        self.assertIn(planted, stderr)
        self.assertFalse((out_dir / card.CARD_NAME).exists())
        self.assertFalse((out_dir / card.ATTRIBUTION_NAME).exists())
        with self.assertRaises(card.CardError) as cm:
            card.render(self.manifest, self.lock, out_dir)
        self.assertIn(planted, str(cm.exception))

    def test_planted_cc_by_nc_track_fails_check_naming_the_clip(self) -> None:
        for c in self.lock.clips:
            if c.id == MUSIC_1:
                c.extra["licence"] = "CC BY-NC 3.0"
                c.extra["attribution"] = "Artist A — Track 1 (CC BY-NC 3.0)"
        write_lock(self.lock, self.lock_path)
        rc, _, stderr = self.run_main("--check")
        self.assertEqual(rc, 1)
        self.assertIn(f"clip {MUSIC_1}: licence 'CC BY-NC 3.0' carries a non-commercial (NC) term", stderr)
        # the other music track (CC BY 4.0) is not named
        self.assertNotIn(MUSIC_2, stderr)

    def test_cc_by_sa_passes_the_gate_and_is_stated_per_file(self) -> None:
        # The gate is the README's rule — non-empty, not flagged unlicensed, no NC term — and nothing
        # stricter: a CC BY-SA track passes and its id is rendered per file, so the card states it
        # accurately (whether SA material is admitted corpus-wide is a plan-level policy, not the card's).
        for c in self.lock.clips:
            if c.id == MUSIC_1:
                c.extra["licence"] = "CC-BY-SA-3.0"
        self.assertEqual(card.check_licences(self.manifest, self.lock), [])
        text = card.render_attribution(self.manifest, self.lock)
        self.assertIn(f"{MUSIC_1},musan,musan_music-release,music,CC-BY-SA-3.0,", text)

    def test_source_level_nc_licence_names_every_clip_of_the_source(self) -> None:
        sources = list(self.manifest.sources)
        sources[3] = make_source("slr28", "openslr_28_simulated", "CC-BY-NC-SA-4.0", roles=("aug",),
                                 pcm_exact=False)
        m = Manifest(name=self.manifest.name, sources=tuple(sources), recipe=self.manifest.recipe)
        validate(m)  # the manifest validator does not police NC: the card does
        lock = copy.deepcopy(self.lock)
        lock.manifest_hash = manifest_hash(m)
        failures = card.check_licences(m, lock)
        self.assertEqual(len(failures), 1)
        self.assertIn(f"clip {RIR_1}: licence 'CC-BY-NC-SA-4.0'", failures[0])

    def test_empty_source_licence_names_the_clip(self) -> None:
        # load_manifest already refuses an empty licence; check_licences refuses it too, per clip, in
        # case a lock is checked against a manifest object built by hand.
        sources = list(self.manifest.sources)
        sources[0] = make_source("sc", "speech_commands_v2", "", roles=("train", "dev", "eval-speech"))
        m = Manifest(name=self.manifest.name, sources=tuple(sources), recipe=self.manifest.recipe)
        failures = card.check_licences(m, self.lock)
        self.assertEqual(len(failures), 6)  # the five sc dry clips and the one augmented variant
        self.assertTrue(all("has an empty licence" in f for f in failures))
        self.assertIn("clip sc/marvel/ddd_nohash_0:", failures[0])

    def test_clip_from_an_unknown_source_is_refused(self) -> None:
        self.lock.clips.append(clip("ghost/x", "ghost", "speech", "train", "train", 0, SECOND))
        failures = card.check_licences(self.manifest, self.lock)
        self.assertEqual(len(failures), 1)
        self.assertIn("clip ghost/x: its source 'ghost' is not in the manifest", failures[0])

    def test_nc_token_does_not_match_cc0_apache_or_inc(self) -> None:
        for ok in ("CC0-1.0", "Apache-2.0", "CC-BY-4.0", "Some Inc. licence", "SYNC-1.0"):
            self.assertIsNone(card._NC_TOKEN.search(ok), ok)
        for bad in ("CC-BY-NC-4.0", "CC BY-NC-SA 3.0", "cc-by-nc", "CC-BY-NC-ND-4.0"):
            self.assertIsNotNone(card._NC_TOKEN.search(bad), bad)

    def test_written_lock_round_trips_the_planted_flags(self) -> None:
        self.lock.clips[0].extra["unlicensed"] = True
        write_lock(self.lock, self.lock_path)
        self.assertTrue(read_lock(self.lock_path).clips[0].extra["unlicensed"])


if __name__ == "__main__":
    unittest.main()
