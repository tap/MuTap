#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""kws_holdout — the recorded hold-out's record (M4c's `holdout.json`), verified before it is scored.

`holdout.json` (version 1) is the committed record of the hold-out: its talkers (pseudonym, consent form
version, permitted uses) and its utterances (a FLAC path relative to `<store>/holdout/`, the file's sha256,
the talker, the microphone path, distance, SNR, phrase and the 16 kHz endpoint sample annotated on the
close-microphone take). The audio never enters git; the store's `holdout/` tier holds the FLACs.

The plan's rule (§6 M5): the harness "refuses a hold-out whose file hashes do not match the committed
holdout.json rows". `load_holdout` refuses any schema deviation by name, like `kws_manifest`;
`verify_holdout` refuses, by file, a FLAC that is missing or whose sha256 differs from its row, a FLAC
under the tier that no row names, two rows that resolve to one file, and any utterance whose talker row
is missing, lacks a consent_form_version, or whose permitted_uses do not include both "evaluation" and
"m7-replay". One file is one utterance: a path is accepted only in its canonical spelling (no '.' or '..'
segment, no '//'), and a sha256 listed under two rows is refused at load.
`holdout_set_id` is the sha256 over the sorted per-utterance FLAC hashes — the lock's hold-out set id.
`streams_from_holdout` makes one positive stream per utterance (share "positives", subshare "holdout"),
decoded through soundfile; a FLAC that is not 16 kHz is refused rather than resampled here (the hold-out
is decoded to the pcm tier by the builder's one resampler, never by the harness).

    python3 tools/ml/kws/kws_holdout.py --holdout holdout.json --store DIR    # verify, print the set id
"""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import pathlib
import sys
from typing import Any

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_audio  # noqa: E402
from kws_manifest import check_keys  # noqa: E402
from kws_scoring import Positive, Scoring  # noqa: E402
from kws_store import Store, StoreError, resolve_store, sha256_file  # noqa: E402
from kws_streams import HOLDOUT_SHARE, POSITIVE_SHARE, Stream  # noqa: E402

HOLDOUT_JSON_VERSION = 1
REQUIRED_USES = ("evaluation", "m7-replay")
FLAC_SUFFIX = ".flac"


class HoldoutError(ValueError):
    """A hold-out record or tier the harness refuses; the message names the file, talker or field."""


def _is_real(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _is_int(v: Any) -> bool:
    return isinstance(v, int) and not isinstance(v, bool)


@dataclasses.dataclass(frozen=True)
class Talker:
    pseudonym: str
    consent_form_version: str | None = None   # verify_holdout refuses a talker without one
    permitted_uses: tuple[str, ...] = ()      # verify_holdout requires REQUIRED_USES


@dataclasses.dataclass(frozen=True)
class Utterance:
    file: str                 # relative FLAC path under <store>/holdout/
    sha256: str
    talker: str               # a talkers[] pseudonym
    microphone_path: str
    distance_m: float
    snr_db: float | None      # None for a clean (unmixed) take
    phrase: str
    endpoint_sample: int      # 16 kHz sample index of the keyword end (the trim rule on the close take)

    @property
    def id(self) -> str:
        return f"{HOLDOUT_SHARE}/{self.file[:-len(FLAC_SUFFIX)]}"


@dataclasses.dataclass(frozen=True)
class Holdout:
    talkers: tuple[Talker, ...]
    utterances: tuple[Utterance, ...]
    holdout_version: int = HOLDOUT_JSON_VERSION
    path: pathlib.Path | None = None

    def talker(self, pseudonym: str) -> Talker | None:
        for t in self.talkers:
            if t.pseudonym == pseudonym:
                return t
        return None

    def to_dict(self) -> dict[str, Any]:
        return {"holdout_version": self.holdout_version,
                "talkers": [{**dataclasses.asdict(t), "permitted_uses": list(t.permitted_uses)}
                            for t in self.talkers],
                "utterances": [dataclasses.asdict(u) for u in self.utterances]}

    @classmethod
    def from_dict(cls, d: Any, path: pathlib.Path | None = None) -> "Holdout":
        if not isinstance(d, dict):
            raise HoldoutError(f"holdout: expected an object, got {type(d).__name__}")
        unknown = sorted(set(d) - {"holdout_version", "talkers", "utterances"})
        if unknown:
            raise HoldoutError(f"holdout: unknown top-level field(s) {unknown}")
        missing = sorted(k for k in ("holdout_version", "talkers", "utterances") if k not in d)
        if missing:
            raise HoldoutError(f"holdout: missing top-level field(s) {missing}")
        if d["holdout_version"] != HOLDOUT_JSON_VERSION:
            raise HoldoutError(f"holdout_version {d['holdout_version']!r}, expected {HOLDOUT_JSON_VERSION}")
        for key in ("talkers", "utterances"):
            if isinstance(d[key], (str, dict)) or not isinstance(d[key], list):
                raise HoldoutError(f"holdout.{key}: expected a list")
        talkers = []
        for i, t in enumerate(d["talkers"]):
            where = f"talker {t.get('pseudonym', i)!r}" if isinstance(t, dict) else f"talkers[{i}]"
            check_keys(Talker, t, where, HoldoutError)
            uses = t.get("permitted_uses", [])
            if isinstance(uses, str) or not isinstance(uses, list) \
                    or not all(isinstance(u, str) for u in uses):
                raise HoldoutError(f"{where}.permitted_uses: expected a list of strings, got {uses!r}")
            version = t.get("consent_form_version")
            if version is not None and not isinstance(version, str):
                raise HoldoutError(f"{where}.consent_form_version: expected a string, got {version!r}")
            if not isinstance(t["pseudonym"], str) or not t["pseudonym"]:
                raise HoldoutError(f"{where}: pseudonym must be a non-empty string")
            talkers.append(Talker(pseudonym=t["pseudonym"], consent_form_version=version,
                                  permitted_uses=tuple(uses)))
        utterances = []
        for i, u in enumerate(d["utterances"]):
            where = f"utterance {u.get('file', i)!r}" if isinstance(u, dict) else f"utterances[{i}]"
            check_keys(Utterance, u, where, HoldoutError)
            utterances.append(Utterance(**u))
        return cls(talkers=tuple(talkers), utterances=tuple(utterances),
                   holdout_version=int(d["holdout_version"]), path=path)


def _check_file(where: str, file: Any) -> None:
    """A relative POSIX path in its one canonical spelling: no '..' or '.' segment, no empty segment
    (leading '/', '//', trailing '/'), no backslash — checked on the raw string, since PurePosixPath drops
    '.' segments and collapses '//', so two spellings of one file could otherwise pass as two rows."""
    if not isinstance(file, str) or not file or "\\" in file:
        raise HoldoutError(f"{where}: file must be a relative POSIX path under the hold-out tier (no '..', "
                           f"no '.', no leading '/', no '\\\\'), got {file!r}")
    segments = file.split("/")
    if any(seg in ("", ".", "..") for seg in segments) \
            or file != pathlib.PurePosixPath(file).as_posix():
        raise HoldoutError(f"{where}: file must be a relative POSIX path under the hold-out tier (no '..', "
                           f"no '.', no leading '/', no '\\\\'), got {file!r}")
    if not file.endswith(FLAC_SUFFIX):
        raise HoldoutError(f"{where}: file {file!r} is not a {FLAC_SUFFIX} master")


def validate(h: Holdout) -> None:
    """Every schema-level rule: types, canonical paths, hex digests, unique files (by spelling and by
    digest) and pseudonyms."""
    seen_talkers: set[str] = set()
    for t in h.talkers:
        if t.pseudonym in seen_talkers:
            raise HoldoutError(f"talker {t.pseudonym!r}: duplicate pseudonym")
        seen_talkers.add(t.pseudonym)
    seen_files: set[str] = set()
    seen_digests: dict[str, str] = {}
    for u in h.utterances:
        where = f"utterance {u.file!r}"
        _check_file(where, u.file)
        if u.file in seen_files:
            raise HoldoutError(f"{where}: listed twice")
        seen_files.add(u.file)
        if not isinstance(u.sha256, str) or len(u.sha256) != 64 \
                or any(c not in "0123456789abcdef" for c in u.sha256):
            raise HoldoutError(f"{where}: sha256 must be 64 lowercase hex characters")
        # one file, one utterance: the same bytes under a second spelling (a case-folded path on a
        # case-insensitive filesystem, which resolve() does not unify) would double the recall
        # denominator and hash twice into the set id
        if u.sha256 in seen_digests:
            raise HoldoutError(f"{where}: sha256 already recorded for utterance {seen_digests[u.sha256]!r} "
                               "(one file is one utterance)")
        seen_digests[u.sha256] = u.file
        for name in ("talker", "microphone_path", "phrase"):
            if not isinstance(getattr(u, name), str) or not getattr(u, name):
                raise HoldoutError(f"{where}: {name} must be a non-empty string")
        if not _is_real(u.distance_m) or u.distance_m < 0.0:
            raise HoldoutError(f"{where}: distance_m must be a non-negative number, got {u.distance_m!r}")
        if u.snr_db is not None and not _is_real(u.snr_db):
            raise HoldoutError(f"{where}: snr_db must be a finite number or null, got {u.snr_db!r}")
        if not _is_int(u.endpoint_sample) or u.endpoint_sample < 0:
            raise HoldoutError(f"{where}: endpoint_sample must be a non-negative integer (16 kHz), got "
                               f"{u.endpoint_sample!r}")


def load_holdout(path: str | pathlib.Path) -> Holdout:
    path = pathlib.Path(path)
    with path.open(encoding="utf-8") as f:
        doc = json.load(f)
    try:
        h = Holdout.from_dict(doc, path=path)
    except HoldoutError:
        raise
    except (TypeError, KeyError, AttributeError) as e:
        raise HoldoutError(f"{path}: does not match holdout.json version {HOLDOUT_JSON_VERSION}: "
                           f"{e}") from None
    validate(h)
    return h


def save_holdout(h: Holdout, path: str | pathlib.Path) -> None:
    text = json.dumps(h.to_dict(), indent=1, ensure_ascii=False) + "\n"
    pathlib.Path(path).write_text(text, encoding="utf-8")


# ---------------------------------------------------------------- the tier against the record


def holdout_path(store: Store, u: Utterance) -> pathlib.Path:
    tier = store.holdout().resolve()
    p = (tier / u.file)
    if not p.resolve().is_relative_to(tier) or p.resolve() == tier:
        raise HoldoutError(f"utterance {u.file!r}: escapes the hold-out tier {tier}")
    return p


def verify_holdout(holdout: Holdout, store: Store) -> None:
    """Refuse, by file or talker, a tier that does not match the record or a talker without consent."""
    tier = store.holdout()
    if not tier.is_dir():
        raise HoldoutError(f"hold-out tier {tier} does not exist — the FLAC masters are placed there by "
                           "hand, never downloaded")
    if not holdout.utterances:
        raise HoldoutError("holdout.json lists no utterances")
    listed: dict[pathlib.Path, str] = {}
    for u in holdout.utterances:
        p = holdout_path(store, u)
        resolved = p.resolve()
        if resolved in listed:
            raise HoldoutError(f"utterance {u.file!r}: names the same file as utterance "
                               f"{listed[resolved]!r} ({resolved})")
        listed[resolved] = u.file
        if not p.is_file():
            raise HoldoutError(f"utterance {u.file!r}: {p} is missing from the hold-out tier")
        digest = sha256_file(p)
        if digest != u.sha256:
            raise HoldoutError(f"utterance {u.file!r}: sha256 {digest} != the recorded {u.sha256} — the "
                               "file under the hold-out tier is not the one holdout.json describes")
        t = holdout.talker(u.talker)
        if t is None:
            raise HoldoutError(f"utterance {u.file!r}: talker {u.talker!r} has no talkers[] row")
        if not t.consent_form_version:
            raise HoldoutError(f"utterance {u.file!r}: talker {u.talker!r} has no consent_form_version")
        lacking = [use for use in REQUIRED_USES if use not in t.permitted_uses]
        if lacking:
            raise HoldoutError(f"utterance {u.file!r}: talker {u.talker!r} permitted_uses "
                               f"{list(t.permitted_uses)} lack {lacking}")
    stray = sorted(str(p.relative_to(tier.resolve())) for p in tier.resolve().rglob("*" + FLAC_SUFFIX)
                   if p.resolve() not in listed)
    if stray:
        raise HoldoutError(f"hold-out tier {tier} holds {len(stray)} FLAC(s) no holdout.json row names: "
                           f"{stray[:5]}{' ...' if len(stray) > 5 else ''}")


def holdout_set_id(holdout: Holdout) -> str:
    """sha256 over the sorted per-utterance FLAC sha256s, one per line, each file once (validate() refuses
    a digest listed twice; the set here keeps the id defined if it was bypassed): the plan's hold-out set
    id."""
    return hashlib.sha256("\n".join(sorted({u.sha256 for u in holdout.utterances})).encode()).hexdigest()


def streams_from_holdout(holdout: Holdout, store: Store, scoring: Scoring) -> list[Stream]:
    """One positive stream per utterance (the room take as recorded), share "positives", subshare
    "holdout"; the audio is decoded lazily through soundfile and must already be 16 kHz."""
    streams: list[Stream] = []
    for u in holdout.utterances:
        p = holdout_path(store, u)
        positive = Positive(id=u.id, endpoint_sample=int(u.endpoint_sample),
                            endpoint_hop=scoring.endpoint_hop(int(u.endpoint_sample)))

        def load(p: pathlib.Path = p, u: Utterance = u) -> np.ndarray:
            x, fs = kws_audio.read_audio(p)
            if fs != kws_audio.RATE:
                raise HoldoutError(f"utterance {u.file!r}: {fs} Hz, the harness scores {kws_audio.RATE} Hz "
                                   "audio (its endpoint_sample is a 16 kHz index); decode the hold-out to "
                                   "the pcm tier with the builder's resampler first")
            return np.asarray(x, dtype=np.float64)

        streams.append(Stream(id=u.id, share=POSITIVE_SHARE, positives=[positive], negative_samples=0,
                              subshare=HOLDOUT_SHARE, load=load, members=[u.file]))
    return streams


# ---------------------------------------------------------------- CLI


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Verify a hold-out record against the store's hold-out tier "
                                             "and print its set id.")
    ap.add_argument("--holdout", required=True, help="holdout.json")
    ap.add_argument("--store", help="the feature store (or $MUTAP_KWS_STORE); no default")
    args = ap.parse_args(argv)
    try:
        h = load_holdout(args.holdout)
        store = Store(resolve_store(args.store))
        verify_holdout(h, store)
    except (HoldoutError, StoreError, ValueError, OSError) as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2
    print(f"hold-out verified: {len(h.utterances)} utterances from {len(h.talkers)} talkers under "
          f"{store.holdout()}; set id {holdout_set_id(h)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
