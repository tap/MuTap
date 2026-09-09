#!/usr/bin/env python3
"""Tests for verify_splits.py: the clean synthetic lock passes all five rules, every planted fixture is
rejected by exactly its rule (in memory and as committed), the committed fixtures are what the generator
writes, and the CLI's exit statuses hold.

    .venv/bin/python -m unittest tools/ml/kws/test_verify_splits.py
"""
from __future__ import annotations

import copy
import io
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "fixtures" / "planted"))

import make_planted_fixtures as planted  # noqa: E402
import verify_splits as vs  # noqa: E402
from kws_features import Geometry  # noqa: E402
from kws_manifest import (Archive, Manifest, Origin, Recipe, Source, manifest_hash, read_lock,  # noqa: E402
                          save_manifest, validate, write_lock)

PYTHON = sys.executable
SCRIPT = HERE / "verify_splits.py"


def _rules_hit(violations: list[vs.Violation]) -> list[str]:
    return sorted({v.rule for v in violations})


def _manifest(source_ids: tuple[str, ...]) -> Manifest:
    """A minimal valid manifest over the named sources (no archive behind it; only its hash is used)."""
    sources = tuple(Source(id=s, kind="speech_commands_v2", release="v0.02",
                           origin=Origin(url="https://example.invalid/" + s, obtain="none: synthetic"),
                           archive=Archive(file=s + ".tar.gz", sha256="0" * 64, size=1), licence="CC-BY-4.0",
                           attribution="synthetic", terms_accepted="none", terms_verified="2026-09-09",
                           redistributable=False, roles=("train", "dev", "eval-speech"), pcm_exact=True)
                    for s in source_ids)
    recipe = Recipe(phrase="marvin", near_miss=(), geometry=Geometry())
    m = Manifest(name="planted-test", sources=sources, recipe=recipe)
    validate(m)
    return m


class CleanLock(unittest.TestCase):
    def test_clean_lock_passes_all_five_rules(self) -> None:
        lock = planted.clean_lock()
        self.assertEqual(vs.verify(lock), [])
        for rule in vs.RULES:
            self.assertEqual(vs.verify(lock, rules=(rule,)), [], rule)

    def test_clean_lock_exercises_every_convention(self) -> None:
        """The clean lock is only a meaningful pass if every rule actually had person keys to compare."""
        rows = planted.clean_lock().clips
        clips = [c for c in rows if c.variant == 0]
        self.assertTrue(any("client_id" in c.extra for c in clips))          # R1
        self.assertTrue(any(c.extra.get("unjoinable") for c in clips))       # R1's unjoinable branch
        self.assertTrue(any(c.share == "holdout" and c.draw for c in rows))  # R2's hold-out half
        self.assertTrue(any(c.draw and c.draw.get("rir") for c in rows))     # R2's draws
        self.assertTrue(any(c.material == "tts" for c in clips))            # R3
        self.assertTrue(any("participants" in c.extra for c in clips))       # R4
        self.assertTrue(any(c.material == "music" and c.extra.get("artist") for c in clips))  # R5

    def test_clean_lock_survives_the_json_round_trip(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "lock.json"
            write_lock(planted.clean_lock(), path)
            self.assertEqual(vs.verify(read_lock(path)), [])


class PlantedFixtures(unittest.TestCase):
    def test_each_planted_lock_is_rejected_by_exactly_its_rule(self) -> None:
        locks = planted.planted_locks()
        self.assertEqual(set(r for r, _, _ in locks.values()), set(vs.RULES))
        for name, (rule, lock, clip_ids) in locks.items():
            with self.subTest(fixture=name):
                violations = vs.verify(lock)
                self.assertEqual(_rules_hit(violations), [rule])
                reported = {cid for v in violations for cid in v.clip_ids}
                self.assertTrue(set(clip_ids) <= reported, f"not reported: {set(clip_ids) - reported}")
                self.assertEqual(vs.verify(lock, rules=(rule,)), violations)
                others = tuple(r for r in vs.RULES if r != rule)
                self.assertEqual(vs.verify(lock, rules=others), [])

    def test_committed_fixtures_match_the_generator(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            written = planted.write_fixtures(pathlib.Path(tmp))
            self.assertTrue(written)
            for fresh in written:
                committed = vs.PLANTED_DIR / fresh.relative_to(tmp)
                self.assertTrue(committed.is_file(), f"missing committed fixture {committed}")
                self.assertEqual(committed.read_bytes(), fresh.read_bytes(),
                                 f"{committed} differs from make_planted_fixtures.py's output; regenerate it")

    def test_self_test_over_the_committed_fixtures(self) -> None:
        out = io.StringIO()
        self.assertEqual(vs.self_test(out=out), [], out.getvalue())
        self.assertIn("R2-holdout-noise", out.getvalue())

    def test_self_test_catches_a_fixture_that_is_not_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp) / "R1-clean"
            d.mkdir()
            write_lock(planted.clean_lock(), d / "lock.json")
            (d / "expect.json").write_text('{"rule": "R1", "clips": []}\n')
            failures = vs.self_test(pathlib.Path(tmp), out=io.StringIO())
            self.assertIn("R1-clean", failures)


class RuleDetails(unittest.TestCase):
    """The branches of each rule that the planted fixtures do not cover."""

    def test_r1_unjoinable_clip_outside_train(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.extra.get("unjoinable"))
        c.split, c.share = "dev", "dev"
        violations = vs.verify(lock)
        self.assertEqual(_rules_hit(violations), ["R1"])
        self.assertEqual(violations[0].clip_ids, (c.id,))

    def test_r1_key_must_be_the_client_id(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.extra.get("client_id") == "clientB")
        c.key = "something-else"
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R1"])

    def test_r2_train_draw_from_a_dev_pool(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.variant == 1 and c.split == "train")
        c.draw["noise"] = "musan_noise/noise/free-sound/noise-free-sound-0002"  # a dev noise file
        violations = vs.verify(lock)
        self.assertEqual(_rules_hit(violations), ["R2"])
        self.assertIn("musan_noise/noise/free-sound/noise-free-sound-0002", violations[0].clip_ids)

    def test_r2_context_from_another_split(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.variant == 1 and c.split == "dev")
        c.draw["context"] = ["sc/house/cccc_nohash_0"]  # an eval negative
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R2"])

    def test_r2_draw_naming_an_unknown_clip_is_a_violation(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.variant == 1 and c.split == "train")
        c.draw["rir"] = "slr28/simulated_rirs/largeroom/Room999/Room999-00001"
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R2"])

    def test_r2_holdout_take_from_dev_noise(self) -> None:
        lock = planted.clean_lock()
        take = next(c for c in lock.clips if c.share == "holdout" and c.variant == 1)
        take.draw["noise"] = "musan_noise/noise/free-sound/noise-free-sound-0002"
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R2"])

    def test_r2_ignores_augmented_rows_without_a_sha(self) -> None:
        lock = planted.clean_lock()
        for c in lock.clips:
            if c.variant > 0:
                self.assertEqual(c.pcm_sha256, "")
        self.assertEqual(vs.verify(lock, rules=("R2",)), [])

    def test_r3_tts_clip_without_a_person_key(self) -> None:
        lock = planted.clean_lock()
        c = next(c for c in lock.clips if c.material == "tts")
        c.speaker = None
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R3"])

    def test_r4_meeting_spanning_splits(self) -> None:
        lock = planted.clean_lock()
        lock.clips.append(copy.deepcopy(next(c for c in lock.clips if c.extra.get("meeting") == "IS1000a")))
        lock.clips[-1].id = "ami/IS1000a/seg-0002"
        lock.clips[-1].pcm_sha256 = "f" * 64
        lock.clips[-1].split, lock.clips[-1].share = "eval", "eval-speech"
        violations = vs.verify(lock)
        self.assertEqual(_rules_hit(violations), ["R4"])
        self.assertTrue(any("meeting" in v.message for v in violations))

    def test_r5_music_without_an_artist_falls_back_to_the_file_id(self) -> None:
        lock = planted.clean_lock()
        for c in lock.clips:
            if c.material == "music":
                c.extra.pop("artist", None)
        self.assertEqual(vs.verify(lock), [])
        a = next(c for c in lock.clips if c.id == "musan_music/music/fma/music-fma-0002")
        b = next(c for c in lock.clips if c.id == "musan_music/music/fma/music-fma-0001")
        a.key = b.key
        self.assertEqual(_rules_hit(vs.verify(lock)), ["R5"])


class ManifestChecks(unittest.TestCase):
    def test_lock_from_another_manifest_is_refused(self) -> None:
        lock = planted.clean_lock()
        m = _manifest(tuple(sorted({c.source for c in lock.clips})))
        with self.assertRaises(vs.VerifyError):
            vs.verify(lock, m)
        lock.manifest_hash = manifest_hash(m)
        self.assertEqual(vs.verify(lock, m), [])

    def test_lock_naming_an_unlisted_source_is_refused(self) -> None:
        lock = planted.clean_lock()
        m = _manifest(("sc",))
        lock.manifest_hash = manifest_hash(m)
        with self.assertRaisesRegex(vs.VerifyError, "ami"):
            vs.verify(lock, m)

    def test_unknown_rule_is_refused(self) -> None:
        with self.assertRaises(vs.VerifyError):
            vs.verify(planted.clean_lock(), rules=("R9",))


class Cli(unittest.TestCase):
    def _run(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run([PYTHON, str(SCRIPT), *args], capture_output=True, text=True, check=False)

    def test_exit_statuses(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            clean = tmp_path / "clean.json"
            lock = planted.clean_lock()
            write_lock(lock, clean)
            r = self._run("--lock", str(clean))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("clean", r.stdout)

            r = self._run("--lock", str(vs.PLANTED_DIR / "R2-holdout-noise" / "lock.json"))
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("R2:", r.stdout)
            self.assertIn("eval-noise", r.stdout)

            r = self._run("--lock", str(vs.PLANTED_DIR / "R2-holdout-noise" / "lock.json"), "--rule", "R1")
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            r = self._run("--lock", str(vs.PLANTED_DIR / "R5-music-artist" / "lock.json"), "--rule", "R5")
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)

            m = _manifest(tuple(sorted({c.source for c in lock.clips})))
            manifest_path = tmp_path / "manifest.json"
            save_manifest(m, manifest_path)
            r = self._run("--lock", str(clean), "--manifest", str(manifest_path))
            self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
            self.assertIn("manifest_hash", r.stderr)
            lock.manifest_hash = manifest_hash(m)
            write_lock(lock, clean)
            r = self._run("--lock", str(clean), "--manifest", str(manifest_path))
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

            r = self._run("--self-test")
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("self-test ok", r.stdout)

            r = self._run()
            self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main()
