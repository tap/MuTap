#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""kws_streams — the audio streams the evaluation harness scores, assembled from a lock (plan §6 M5).

A `Stream` is one continuously running deployment: the detector scores it after a single reset, so the
smoothing window and the refractory period run across clip boundaries the way they do in a room.

- **Positive streams**: every variant-0 eval row with label 1 becomes one stream whose audio is the row's
  mixture — `kws_build.mixture` over the row's resolved draw, i.e. the raw concatenation of the drawn
  context and the keyword that `extract` featurized, so the stream has exactly the lock's `extra.frames`
  hops and the hit window [h - T, h + 20 + T] around the row's endpoint lies inside them. One `Positive`
  per stream; `negative_samples` is 0: a positive stream never enters the hours denominator.
- **Negative streams**: per eval share, that share's variant-0 rows with label 0 (eval-speech, eval-music,
  eval-tts) or label None (eval-noise), sorted by id and concatenated in that order into streams of at
  most `max_stream_s` seconds, a clip never split (a clip longer than `max_stream_s` stands alone, so the
  bound is on the packing, not on a single clip). `negative_samples` is the stream's length — the sum of
  its members' decoded lengths, the plan's FA/h denominator: hours = negative_samples / 16000 / 3600.

Audio is loaded lazily (`Stream.load`) so a corpus of hours is never resident at once; `samples()` decodes
on every call and caches nothing. `validate_streams` decodes every stream once and refuses, by name, any
accounting that does not match the audio; its per-stream rules are `validate_stream`, which takes the
decoded length, so a caller that already holds the audio (kws_eval's scoring pass) applies the same rules
without a second decode. Refusals are `StreamError`s; nothing here warns.

Measured 9 September 2026 on the M0 Mac (Apple silicon, CPython 3.12, numpy 2.5.3, soundfile 0.14.0),
the bring-up corpus (speech_commands_v2_bringup, 263,487 lock rows): the figures are in the M5 section
of README.md and in test_kws_streams.py's docstring; the toy fixture assembles in well under 0.1 s.
"""
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import sys
import time
from typing import Any, Callable, Sequence

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_audio  # noqa: E402
import kws_build  # noqa: E402
from kws_manifest import Clip, Lock, Manifest, load_manifest, manifest_hash, read_lock  # noqa: E402
from kws_scoring import Positive, Scoring  # noqa: E402
from kws_store import Store, StoreError, resolve_store  # noqa: E402

EVAL_SHARES = ("eval-speech", "eval-music", "eval-noise", "eval-tts")
POSITIVE_SHARE = "positives"
HOLDOUT_SHARE = "holdout"   # a subshare and an id prefix: a hold-out utterance is a positive stream
STREAM_SHARES = (POSITIVE_SHARE, *EVAL_SHARES)   # there is no hold-out negative share
SUBSHARES = ("eval-speech", "eval-tts", HOLDOUT_SHARE)   # where a positive stream's utterance came from
NEGATIVE_LABELS = {"eval-speech": 0, "eval-music": 0, "eval-tts": 0, "eval-noise": None}
DEFAULT_MAX_STREAM_S = 60.0


class StreamError(ValueError):
    """A stream, or a stream list, the harness refuses; the message names the stream and the rule."""


@dataclasses.dataclass
class Stream:
    """One continuously scored stream: its audio (eager or lazy), its positives and its hours accounting."""

    id: str                                   # unique across the list
    share: str                                # one of STREAM_SHARES
    positives: list[Positive]                 # stream-relative endpoints; empty for a negative stream
    negative_samples: int                     # samples in the hours denominator (0 for a positive stream)
    subshare: str | None = None               # a positive stream: one of SUBSHARES (required there)
    audio: np.ndarray | None = None           # float64, 16 kHz
    load: Callable[[], np.ndarray] | None = None  # lazy alternative to `audio`
    members: list[str] = dataclasses.field(default_factory=list)  # the lock rows the audio is made of

    def samples(self) -> np.ndarray:
        """The stream's audio as a one-dimensional float64 array (decoded on every call when lazy)."""
        if self.audio is not None:
            x = np.asarray(self.audio, dtype=np.float64)
        elif self.load is not None:
            x = np.asarray(self.load(), dtype=np.float64)
        else:
            raise StreamError(f"stream {self.id!r}: has neither audio nor a loader")
        if x.ndim != 1:
            raise StreamError(f"stream {self.id!r}: audio must be one-dimensional, got shape {x.shape}")
        return x


# ---------------------------------------------------------------- assembly from a lock


def require_scoring_matches(manifest: Manifest, scoring: Scoring) -> None:
    """The scoring numbers the lock's rows were cut for: hop and rate from the geometry, T from the label
    rule, L the ceiling `extract` reserved after every positive. Any other value scores windows the lock
    never guaranteed to fit inside a positive's rows, so it is refused rather than reinterpreted."""
    g = manifest.recipe.geometry
    if scoring.hop != g.hop:
        raise StreamError(f"scoring.hop {scoring.hop} != the manifest geometry's hop {g.hop}")
    if scoring.sample_rate != kws_audio.RATE or int(g.sample_rate) != kws_audio.RATE:
        raise StreamError(f"scoring.sample_rate {scoring.sample_rate} and geometry.sample_rate "
                          f"{g.sample_rate:g} must both be the pcm tier's {kws_audio.RATE} Hz")
    want_t = manifest.recipe.label.tolerance_hops
    if scoring.tolerance_hops != want_t:
        raise StreamError(f"scoring.tolerance_hops {scoring.tolerance_hops} != the manifest's label "
                          f"tolerance {want_t} (the hit window is a manifest rule, plan §6 M4 \"Label "
                          "data\")")
    if scoring.latency_hops != kws_build.LATENCY_CEILING_HOPS:
        raise StreamError(f"scoring.latency_hops {scoring.latency_hops} != the "
                          f"{kws_build.LATENCY_CEILING_HOPS} hops `extract` reserved after every positive "
                          "(the §7 ceiling)")


def trailing_samples(manifest: Manifest) -> int:
    """`extract`'s n_trail: (L + T) hops plus one frame of context after the keyword."""
    g = manifest.recipe.geometry
    return (kws_build.LATENCY_CEILING_HOPS + manifest.recipe.label.tolerance_hops) * g.hop + g.frame


def _pack(rows: Sequence[Clip], max_samples: int) -> list[list[Clip]]:
    """Greedy in the given order: a clip joins the open group unless that would exceed max_samples; a
    clip that alone exceeds it forms its own group (never split)."""
    groups: list[list[Clip]] = []
    have = 0
    for c in rows:
        if groups and have + c.length > max_samples:
            groups.append([])
            have = 0
        if not groups:
            groups.append([])
        groups[-1].append(c)
        have += c.length
    return groups


def streams_from_lock(manifest: Manifest, lock: Lock, store: Store, scoring: Scoring,
                      shares: Sequence[str] = EVAL_SHARES,
                      max_stream_s: float = DEFAULT_MAX_STREAM_S) -> list[Stream]:
    """The positive streams (every eval positive of the selected shares) followed by the negative streams
    of each selected share, in `shares` order. Audio is lazy; nothing is decoded here."""
    require_scoring_matches(manifest, scoring)
    unknown = [s for s in shares if s not in EVAL_SHARES]
    if unknown or not shares:
        raise StreamError(f"shares {list(shares)}: must be a non-empty subset of {list(EVAL_SHARES)}")
    if len(set(shares)) != len(shares):
        raise StreamError(f"shares {list(shares)}: a share is named twice")
    if not (max_stream_s > 0.0):
        raise StreamError(f"max_stream_s must be positive, got {max_stream_s!r}")
    want = manifest_hash(manifest)
    if lock.manifest_hash != want:
        raise StreamError(f"the lock was built from manifest {lock.manifest_hash}, this manifest hashes to "
                          f"{want}: assemble streams from the lock of the manifest whose store tiers exist")
    build = kws_build.Build(manifest, store)
    n_trail = trailing_samples(manifest)
    clips = {c.id: c for c in lock.clips if c.variant == 0}
    eval_rows = [c for c in clips.values() if c.split == "eval" and c.share in shares]
    # kws_build's loaders take dict rows; only the eval rows and the rows their draws reference are ever
    # read, so only those are converted (asdict over every variant-0 row of the bring-up lock, 166,758
    # rows, took 0.83 s of a 0.9 s assembly, measured 9 September 2026; the needed 11,105 take 0.05 s)
    by_id: dict[str, dict[str, Any]] = {}

    def row_of(clip: Clip) -> dict[str, Any]:
        if clip.id not in by_id:
            by_id[clip.id] = dataclasses.asdict(clip)
        return by_id[clip.id]

    streams: list[Stream] = []
    for c in sorted((c for c in eval_rows if c.label == 1), key=lambda c: c.id):
        if c.endpoint_sample is None:
            raise StreamError(f"row {c.id}: an eval positive without an endpoint_sample")
        if c.share not in SUBSHARES:
            raise StreamError(f"row {c.id}: a positive in share {c.share!r}; positives come from "
                              f"{list(SUBSHARES)}")
        row = row_of(c)
        refs = kws_build.draw_refs(row.get("draw"))
        missing = [i for i in refs if i not in clips]
        if missing:
            raise StreamError(f"row {c.id}: its draw names {missing}, not variant-0 rows of the lock")
        for i in refs:
            row_of(clips[i])
        positive = Positive(id=c.id, endpoint_sample=int(c.endpoint_sample),
                            endpoint_hop=scoring.endpoint_hop(int(c.endpoint_sample)))

        def load_positive(row: dict[str, Any] = row) -> np.ndarray:
            y = kws_build._read_clip(build, row)
            return kws_build.mixture(build, y, row, by_id, n_trail)

        streams.append(Stream(id=c.id, share=POSITIVE_SHARE, positives=[positive], negative_samples=0,
                              subshare=c.share, load=load_positive, members=[c.id]))
    max_samples = int(round(max_stream_s * kws_audio.RATE))
    for share in shares:
        want_label = NEGATIVE_LABELS[share]
        rows = sorted((c for c in eval_rows if c.share == share and c.label != 1), key=lambda c: c.id)
        for c in rows:
            if c.label != want_label:
                raise StreamError(f"row {c.id}: label {c.label!r} in share {share!r}; a {share} negative "
                                  f"carries label {want_label!r}")
            if c.length <= 0:
                raise StreamError(f"row {c.id}: decoded length {c.length} — nothing to score")
        for n, group in enumerate(_pack(rows, max_samples)):
            member_rows = [row_of(c) for c in group]

            def load_negative(member_rows: list[dict[str, Any]] = member_rows) -> np.ndarray:
                return np.concatenate([kws_build._read_clip(build, r) for r in member_rows])

            streams.append(Stream(id=f"{share}/stream-{n:04d}", share=share, positives=[],
                                  negative_samples=int(sum(c.length for c in group)), load=load_negative,
                                  members=[c.id for c in group]))
    return streams


# ---------------------------------------------------------------- accounting


def hours_per_share(streams: Sequence[Stream], scoring: Scoring) -> dict[str, float]:
    """The hours denominator of every negative share present: sum of negative_samples / rate / 3600."""
    samples: dict[str, int] = {}
    for s in streams:
        if s.share != POSITIVE_SHARE:
            samples[s.share] = samples.get(s.share, 0) + int(s.negative_samples)
    return {share: scoring.hours(n) for share, n in samples.items()}


def positives_per_subshare(streams: Sequence[Stream]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for s in streams:
        if s.share == POSITIVE_SHARE:
            key = s.subshare or "unspecified"
            counts[key] = counts.get(key, 0) + len(s.positives)
    return counts


def validate_stream(s: Stream, n_samples: int, scoring: Scoring,
                    seen_positives: set[str] | None = None) -> None:
    """One stream's accounting against its decoded length `n_samples`; every per-stream rule of
    `validate_streams` (which decodes and calls this), so a caller already holding the audio applies the
    same rules without a second decode. Refuses, by stream id: an unknown share; a negative_samples that
    is not an integer; an empty stream; a positive stream with negative_samples != 0, with no positives,
    or with a subshare outside SUBSHARES; a positive whose endpoint lies at or beyond the audio, whose
    endpoint_hop disagrees with its endpoint_sample, whose window ends beyond the stream's hops, or whose
    id is already in `seen_positives` (added here, so one set threaded through a list refuses an id used
    twice within a stream or across streams); a negative stream with positives or whose negative_samples
    != n_samples."""
    if s.share not in STREAM_SHARES:
        raise StreamError(f"stream {s.id!r}: unknown share {s.share!r} (known: {list(STREAM_SHARES)})")
    if isinstance(s.negative_samples, bool) or not isinstance(s.negative_samples, (int, np.integer)):
        raise StreamError(f"stream {s.id!r}: negative_samples must be an integer, got "
                          f"{s.negative_samples!r}")
    n = int(n_samples)
    if n == 0:
        raise StreamError(f"stream {s.id!r}: empty stream (no audio)")
    hops = n // scoring.hop
    if s.share == POSITIVE_SHARE:
        if s.negative_samples != 0:
            raise StreamError(f"stream {s.id!r}: a positive stream with negative_samples "
                              f"{s.negative_samples} (it must be 0: positives never enter the hours "
                              "denominator)")
        if not s.positives:
            raise StreamError(f"stream {s.id!r}: a positive stream with no positives")
        if s.subshare not in SUBSHARES:
            raise StreamError(f"stream {s.id!r}: subshare {s.subshare!r} is not one of {list(SUBSHARES)} "
                              "(a positive stream names the share its utterance came from)")
        for p in s.positives:
            if seen_positives is not None:
                if p.id in seen_positives:
                    raise StreamError(f"stream {s.id!r}: positive {p.id!r} appears twice (one hit per "
                                      "utterance needs unique ids)")
                seen_positives.add(p.id)
            if not 0 <= p.endpoint_sample < n:
                raise StreamError(f"stream {s.id!r}: positive {p.id!r} endpoint sample "
                                  f"{p.endpoint_sample} lies beyond its {n} samples of audio")
            if p.endpoint_hop != scoring.endpoint_hop(p.endpoint_sample):
                raise StreamError(f"stream {s.id!r}: positive {p.id!r} endpoint_hop {p.endpoint_hop} != "
                                  f"{scoring.endpoint_hop(p.endpoint_sample)} = endpoint_sample "
                                  f"{p.endpoint_sample} // {scoring.hop}")
            lo, hi = scoring.hit_window(p.endpoint_hop)
            if hi >= hops:
                raise StreamError(f"stream {s.id!r}: positive {p.id!r} window [{lo}, {hi}] ends beyond "
                                  f"the stream's {hops} hops (the (L + T) hops after the keyword are "
                                  "missing)")
    else:
        if s.positives:
            raise StreamError(f"stream {s.id!r}: a {s.share} stream carrying {len(s.positives)} "
                              "positive(s)")
        if s.negative_samples != n:
            raise StreamError(f"stream {s.id!r}: negative_samples {s.negative_samples} != its "
                              f"{n} decoded samples (the hours denominator is the decoded duration)")


def validate_streams(streams: Sequence[Stream], scoring: Scoring) -> None:
    """Refuse, by name, a stream list whose accounting does not match its audio.

    Decodes every stream once. Refusals: no streams at all; a non-Stream; a duplicate stream id; and,
    per stream, every rule of `validate_stream` (an unknown share; a non-integer negative_samples; an
    empty stream; a positive stream with negative_samples != 0, with no positives, or with a subshare
    outside SUBSHARES (None included); a positive whose endpoint lies at or beyond the end of the audio,
    whose endpoint_hop disagrees with its endpoint_sample, or whose window [h - T, h + L + T] ends beyond
    the stream's hops; a positive id used twice; a negative stream with positives, or whose
    negative_samples != len(audio)).
    """
    if not streams:
        raise StreamError("no streams to score")
    seen_ids: set[str] = set()
    seen_positives: set[str] = set()
    for s in streams:
        if not isinstance(s, Stream):
            raise StreamError(f"expected a Stream, got {type(s).__name__}")
        if s.id in seen_ids:
            raise StreamError(f"stream {s.id!r}: duplicate stream id")
        seen_ids.add(s.id)
        validate_stream(s, s.samples().size, scoring, seen_positives)


# ---------------------------------------------------------------- CLI: the accounting of a lock


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Assemble the evaluation streams of a lock and print the "
                                             "per-share accounting (stream counts, hours from decoded "
                                             "lengths).")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--lock", required=True, help="the lock.json the streams are assembled from")
    ap.add_argument("--store", help="the feature store (or $MUTAP_KWS_STORE); no default")
    ap.add_argument("--shares", nargs="+", default=list(EVAL_SHARES), help="the eval shares to assemble")
    ap.add_argument("--max-stream-s", type=float, default=DEFAULT_MAX_STREAM_S)
    ap.add_argument("--validate", action="store_true",
                    help="decode every stream and check the accounting against the audio")
    args = ap.parse_args(argv)
    try:
        manifest = load_manifest(args.manifest)
        store = Store(resolve_store(args.store))
        t0 = time.perf_counter()
        lock = read_lock(args.lock)
        t_lock = time.perf_counter() - t0
        scoring = Scoring(hop=manifest.recipe.geometry.hop,
                          tolerance_hops=manifest.recipe.label.tolerance_hops)
        t0 = time.perf_counter()
        streams = streams_from_lock(manifest, lock, store, scoring, args.shares, args.max_stream_s)
        t_assemble = time.perf_counter() - t0
        print(f"lock: {len(lock.clips)} rows read in {t_lock:.2f} s; {len(streams)} streams assembled in "
              f"{t_assemble:.3f} s")
        for share, n in sorted(positives_per_subshare(streams).items()):
            print(f"positives from {share}: {n}")
        counts = {s.share: 0 for s in streams}
        for s in streams:
            counts[s.share] += 1
        for share, h in hours_per_share(streams, scoring).items():
            print(f"{share}: {counts[share]} streams, {h:.4f} h")
        if args.validate:
            t0 = time.perf_counter()
            validate_streams(streams, scoring)
            print(f"validated (every stream decoded) in {time.perf_counter() - t0:.1f} s")
    except (StreamError, StoreError, ValueError, OSError) as e:
        print(f"refused: {e}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
