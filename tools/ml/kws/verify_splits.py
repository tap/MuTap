#!/usr/bin/env python3
"""verify_splits — the five named split-disjointness rules over a lock (wake-word plan §6 M4).

The splits are client-id-disjoint and clip-id-disjoint (an approximation of
speaker-disjoint, recorded in the card). This script proves that over the
build-emitted lock, rule by rule, and refuses on any violation: every finding
names its rule (R1–R5) and the clip ids involved. Each rule has a
planted-violation fixture under `fixtures/planted/<Rn-name>/` (a `lock.json`
plus `expect.json` naming the rule and the planted clips), and `--self-test`
asserts each fixture is rejected by exactly its rule.

    python3 tools/ml/kws/verify_splits.py --lock L [--manifest M] [--rule R2]
    python3 tools/ml/kws/verify_splits.py --self-test

Exit status: 0 clean, 1 on any violation (or a failed self-test), 2 when the
inputs cannot be verified at all (a lock whose manifest_hash is not the
manifest's, a clip whose source the manifest does not list).

The rules
---------
R1  client-id disjointness across Common Voice and MSWC.
R2  no decoded-PCM sha256 of any dev, eval or hold-out clip in a training or
    augmentation pool of any source (a dry clip's pcm_sha256 appears in one
    split only, whatever its source); every augmentation draw (`noise`, `rir`,
    `context`) names clips of the drawing clip's own split; and no training or
    dev noise file in any hold-out mixture (a `holdout`-share take's draw names
    an `eval-noise` clip).
R3  TTS voice + speaker-id disjointness.
R4  AMI participant disjointness (and a meeting never spans splits).
R5  music artist disjointness after MUSAN/FMA de-duplication.

The person-key conventions (what M4b's adapters fill in)
--------------------------------------------------------
The rules read only `Clip` fields, so a lock verifies without its manifest.
R1, R3 and R4 concern sources that do not exist at M4a; their adapters follow
these conventions, and a clip carrying the marker field is checked whatever
its source kind:

* Common Voice / MSWC (R1): `Clip.key` is the Common Voice `client_id` and
  `Clip.extra["client_id"]` repeats it. An MSWC clip that could not be joined
  to Common Voice carries `extra["client_id"] = None` and
  `extra["unjoinable"] = True`; such a clip may only be in the train split
  (the plan excludes unjoinable clips from dev and eval).
* Piper TTS (R3): `Clip.material == "tts"`, `Clip.speaker` is the speaker id
  (a string; "0" for a single-speaker voice), `Clip.extra["voice"]` is the
  voice name and `extra["voice_sha256"]` its model digest. The person key is
  (voice, speaker id). Positives and TTS negatives share it.
* AMI (R4): `Clip.extra["participants"]` is the list of participant ids the
  clip's audio contains (the meeting's participant set for a whole-meeting
  clip), `extra["meeting"]` the meeting id. Every participant is a person key.
* Music (R5): `Clip.material == "music"`, `Clip.extra["artist"]` the
  artist/composer (MUSAN's ANNOTATIONS column, FMA's artist name), and
  `extra["track"]` the track title where one exists. The artist key is
  compared case-folded with whitespace collapsed, across sources; a music clip
  without an artist falls back to its split key (the file id), as the
  assignment rule does.

Everything else — Speech Commands, MUSAN noise and speech, SLR28 — is covered
by R2 alone (its split key is a file or speaker hash, and its leaks are
decoded-PCM leaks).
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import re
import sys
from typing import Callable

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from kws_manifest import Clip, Lock, Manifest, load_manifest, manifest_hash, read_lock  # noqa: E402

RULES = ("R1", "R2", "R3", "R4", "R5")
PLANTED_DIR = pathlib.Path(__file__).resolve().parent / "fixtures" / "planted"
HOLDOUT_NOISE_SHARE = "eval-noise"
FIXTURE_NAME = re.compile(r"^R[1-5]-")


class VerifyError(RuntimeError):
    """The inputs cannot be verified at all (as opposed to a rule violation)."""


@dataclasses.dataclass(frozen=True)
class Violation:
    rule: str
    message: str
    clip_ids: tuple[str, ...]

    def __str__(self) -> str:
        return f"{self.rule}: {self.message} — clips: {', '.join(self.clip_ids)}"


# ---------------------------------------------------------------- helpers


def _dry(lock: Lock) -> list[Clip]:
    return [c for c in lock.clips if c.variant == 0]


def _by_id(lock: Lock) -> dict[str, Clip]:
    """Dry clip by id; a clip id may appear once per variant, so the variant-0 row is the identity."""
    return {c.id: c for c in lock.clips if c.variant == 0}


def _spanning(groups: dict[str, list[Clip]], rule: str, what: str) -> list[Violation]:
    """One violation per person key whose clips fall in more than one split."""
    out: list[Violation] = []
    for key in sorted(groups):
        clips = groups[key]
        splits = sorted({c.split for c in clips})
        if len(splits) > 1:
            out.append(Violation(rule, f"{what} {key!r} appears in splits {', '.join(splits)}",
                                 tuple(sorted(c.id for c in clips))))
    return out


def _norm(text: str) -> str:
    return " ".join(str(text).casefold().split())


def _ref_id(ref: str) -> str:
    """A draw names clips by id; strip a `#<variant>` suffix if one is present."""
    return ref.split("#", 1)[0]


# ---------------------------------------------------------------- the rules


def rule_r1(lock: Lock) -> list[Violation]:
    """Client-id disjointness across Common Voice and MSWC."""
    out: list[Violation] = []
    groups: dict[str, list[Clip]] = {}
    for c in _dry(lock):
        if "client_id" not in c.extra:
            continue
        cid = c.extra["client_id"]
        if cid is None:
            if not c.extra.get("unjoinable"):
                out.append(Violation("R1", "client_id is null but the clip is not flagged unjoinable",
                                     (c.id,)))
            if c.split != "train":
                out.append(Violation("R1", f"unjoinable clip (no client id) in split {c.split!r}; only train "
                                     "may hold an unjoinable clip", (c.id,)))
            continue
        cid = str(cid)
        if c.key != cid:
            out.append(Violation("R1", f"split key {c.key!r} is not the clip's client_id {cid!r}", (c.id,)))
        groups.setdefault(cid, []).append(c)
    out.extend(_spanning(groups, "R1", "client_id"))
    return out


def rule_r2(lock: Lock) -> list[Violation]:
    """No decoded-PCM leak across splits, same-split draws, hold-out mixtures from eval-noise only."""
    out: list[Violation] = []
    by_sha: dict[str, list[Clip]] = {}
    for c in _dry(lock):
        if c.pcm_sha256:
            by_sha.setdefault(c.pcm_sha256, []).append(c)
    for sha in sorted(by_sha):
        clips = by_sha[sha]
        splits = sorted({c.split for c in clips})
        if len(splits) > 1:
            out.append(Violation("R2", f"decoded-PCM sha256 {sha[:12]}… appears in splits "
                                 f"{', '.join(splits)}", tuple(sorted(c.id for c in clips))))
    index = _by_id(lock)
    for c in sorted(lock.clips, key=lambda c: (c.id, c.variant)):
        if not c.draw:
            continue
        refs: list[tuple[str, str]] = []
        for field in ("noise", "rir"):
            if c.draw.get(field):
                refs.append((field, _ref_id(str(c.draw[field]))))
        for ref in c.draw.get("context") or ():
            refs.append(("context", _ref_id(str(ref))))
        for field, ref in refs:
            target = index.get(ref)
            if target is None:
                out.append(Violation("R2", f"variant {c.variant} draw.{field} names {ref!r}, which is not a "
                                     "dry clip of this lock, so the leak check cannot run", (c.id, ref)))
            elif c.share == "holdout":
                if target.share != HOLDOUT_NOISE_SHARE:
                    out.append(Violation("R2", f"hold-out mixed take (variant {c.variant}) draws its {field} "
                                         f"from {ref!r} of share {target.share!r} (split {target.split!r}); "
                                         f"a hold-out mixture may only use {HOLDOUT_NOISE_SHARE} files",
                                         (c.id, ref)))
            elif target.split != c.split:
                out.append(Violation("R2", f"variant {c.variant} (split {c.split!r}) draws its {field} from "
                                     f"{ref!r} of split {target.split!r}; draws come from the clip's own "
                                     "split", (c.id, ref)))
    return out


def rule_r3(lock: Lock) -> list[Violation]:
    """TTS voice + speaker-id disjointness."""
    out: list[Violation] = []
    groups: dict[str, list[Clip]] = {}
    for c in _dry(lock):
        if c.material != "tts":
            continue
        voice = c.extra.get("voice")
        if not voice or c.speaker is None or c.speaker == "":
            out.append(Violation("R3", "TTS clip without extra.voice and speaker, so its person key is "
                                 "unknown", (c.id,)))
            continue
        groups.setdefault(f"{voice}/{c.speaker}", []).append(c)
    out.extend(_spanning(groups, "R3", "voice/speaker"))
    return out


def rule_r4(lock: Lock) -> list[Violation]:
    """AMI participant disjointness; a meeting never spans splits."""
    out: list[Violation] = []
    people: dict[str, list[Clip]] = {}
    meetings: dict[str, list[Clip]] = {}
    for c in _dry(lock):
        if "participants" not in c.extra:
            continue
        parts = c.extra["participants"] or []
        if not parts:
            out.append(Violation("R4", "clip lists no participants, so its person keys are unknown", (c.id,)))
        for p in parts:
            people.setdefault(str(p), []).append(c)
        if c.extra.get("meeting") is not None:
            meetings.setdefault(str(c.extra["meeting"]), []).append(c)
    out.extend(_spanning(people, "R4", "participant"))
    out.extend(_spanning(meetings, "R4", "meeting"))
    return out


def rule_r5(lock: Lock) -> list[Violation]:
    """Music artist disjointness after de-duplication (the artist key compared across sources)."""
    groups: dict[str, list[Clip]] = {}
    for c in _dry(lock):
        if c.material != "music":
            continue
        artist = c.extra.get("artist")
        key = f"artist={_norm(artist)}" if artist else f"file={c.key}"
        groups.setdefault(key, []).append(c)
    return _spanning(groups, "R5", "music key")


RULE_FUNCTIONS: dict[str, Callable[[Lock], list[Violation]]] = {
    "R1": rule_r1, "R2": rule_r2, "R3": rule_r3, "R4": rule_r4, "R5": rule_r5,
}


# ---------------------------------------------------------------- driver


def check_manifest(lock: Lock, manifest: Manifest) -> None:
    """Refuse a lock that was not built from this manifest, or that names a source it lacks."""
    expected = manifest_hash(manifest)
    if lock.manifest_hash != expected:
        raise VerifyError(f"lock.manifest_hash {lock.manifest_hash} is not the manifest's {expected}: "
                          "the lock was not built from this manifest")
    known = {s.id for s in manifest.sources}
    unknown = sorted({c.source for c in lock.clips} - known)
    if unknown:
        raise VerifyError(f"lock names sources the manifest does not list: {', '.join(unknown)}")


def verify(lock: Lock, manifest: Manifest | None = None, rules: tuple[str, ...] = RULES) -> list[Violation]:
    """Every violation of the requested rules, ordered by rule; empty means the lock is clean."""
    if manifest is not None:
        check_manifest(lock, manifest)
    out: list[Violation] = []
    for rule in rules:
        if rule not in RULE_FUNCTIONS:
            raise VerifyError(f"unknown rule {rule!r}; the rules are {', '.join(RULES)}")
        out.extend(RULE_FUNCTIONS[rule](lock))
    return out


def planted_fixtures(root: pathlib.Path = PLANTED_DIR) -> list[tuple[pathlib.Path, dict]]:
    """Every planted fixture directory (named `R<n>-<what>`) with its expectation ({"rule", "clips"})."""
    if not root.is_dir():
        raise VerifyError(f"no planted fixtures under {root}")
    out = []
    for d in sorted(p for p in root.iterdir() if p.is_dir() and FIXTURE_NAME.match(p.name)):
        expect = d / "expect.json"
        if not expect.is_file() or not (d / "lock.json").is_file():
            raise VerifyError(f"planted fixture {d.name} needs lock.json and expect.json")
        out.append((d, json.loads(expect.read_text(encoding="utf-8"))))
    if not out:
        raise VerifyError(f"no planted fixtures under {root}")
    return out


def self_test(root: pathlib.Path = PLANTED_DIR, out=sys.stdout) -> list[str]:
    """Run every planted fixture; returns the failures (a fixture not rejected by exactly its rule)."""
    failures: list[str] = []
    fixtures = planted_fixtures(root)
    covered = set()
    for d, expect in fixtures:
        lock = read_lock(d / "lock.json")
        manifest_path = d / "manifest.json"
        manifest = load_manifest(manifest_path) if manifest_path.is_file() else None
        violations = verify(lock, manifest)
        rules_hit = sorted({v.rule for v in violations})
        reported = {cid for v in violations for cid in v.clip_ids}
        missing = sorted(set(expect["clips"]) - reported)
        ok = rules_hit == [expect["rule"]] and not missing
        covered.add(expect["rule"])
        status = "ok" if ok else "FAIL"
        print(f"{status:4} {d.name:24} expected {expect['rule']}, rejected by {rules_hit or ['nothing']}"
              + (f", planted clips not reported: {missing}" if missing else ""), file=out)
        for v in violations:
            print(f"     {v}", file=out)
        if not ok:
            failures.append(d.name)
    uncovered = sorted(set(RULES) - covered)
    if uncovered:
        failures.append(f"no planted fixture for {', '.join(uncovered)}")
        print(f"FAIL no planted fixture for {', '.join(uncovered)}", file=out)
    return failures


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--lock", type=pathlib.Path, help="the build-emitted lock.json")
    ap.add_argument("--manifest", type=pathlib.Path,
                    help="the manifest the lock was built from (checked by hash when given)")
    ap.add_argument("--rule", choices=RULES, action="append",
                    help="run one rule (repeatable); default: all five")
    ap.add_argument("--self-test", action="store_true",
                    help="run every planted fixture under fixtures/planted/ and require each to be rejected "
                         "by exactly its rule")
    args = ap.parse_args(argv)

    try:
        if args.self_test:
            failures = self_test()
            if failures:
                print(f"self-test FAILED: {', '.join(failures)}")
                return 1
            print("self-test ok: every planted fixture is rejected by exactly its rule")
            return 0
        if args.lock is None:
            ap.error("--lock is required (or --self-test)")
        lock = read_lock(args.lock)
        manifest = load_manifest(args.manifest) if args.manifest is not None else None
        rules = tuple(args.rule) if args.rule else RULES
        violations = verify(lock, manifest, rules)
    except VerifyError as e:
        print(f"verify_splits: cannot verify: {e}", file=sys.stderr)
        return 2
    for v in violations:
        print(v)
    if violations:
        rules_hit = ", ".join(sorted({v.rule for v in violations}))
        print(f"verify_splits: {len(violations)} violation(s) of {rules_hit} over {len(lock.clips)} "
              "lock rows")
        return 1
    print(f"verify_splits: {', '.join(rules)} clean over {len(lock.clips)} lock rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
