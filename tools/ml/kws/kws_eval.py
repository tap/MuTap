#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""kws_eval — the evaluation harness: threshold sweep, per-share false accepts per hour, the committed
report (wake-word plan §6 M5).

Scores every stream once (`detector.score`, or `PlantedDetector.score_stream` for the oracle), then per
threshold runs the reference decision stage of `kws_scoring` (`decide`): hits and spurious events on the
positive streams, events on the negative streams per share. Recall = hits / positives with its Wilson 95 %
interval, FRR = 1 - recall; per negative share events, hours H, FA/h, the exact two-sided 95 % Poisson
interval, and at zero events the one-sided bound ln 20 / H. A share absent from the streams is absent in
the report — never 0 FA/h. Every FA/h figure is printed with its H.

    kws_eval.py sweep   --manifest M --lock L --store DIR [--detector band-energy] [--thresholds N]
                        [--grid N] [--smoothing W] [--refractory R] [--max-stream-s S] --out DIR
    kws_eval.py holdout --holdout holdout.json --manifest M --store DIR [--lock L] ... --out DIR

`sweep` assembles the eval streams from the lock (`kws_streams.streams_from_lock`), `holdout` verifies
the hold-out's file hashes and consent rows (`kws_holdout.verify_holdout`) and scores its utterances,
beside the lock's eval streams when `--lock` is given, so the hold-out recall lands beside the eval-tts
recall in one report. Both write DIR/report.json and DIR/report.md.

The harness refuses a mis-accounted stream before any figure is computed (`check_stream`, which is
`kws_streams.validate_stream` applied to the audio the scoring pass decodes — one rule set, not a copy, so
a stream built by hand cannot mis-account and nothing is decoded twice), a detector whose score count is
not the stream's hop count, a score that is not a real number or lies outside [0, 1], a threshold outside
[0, 1] or repeated, and scores supplied for a stream it was not given.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import math
import pathlib
import sys
import time
from collections.abc import Mapping
from typing import Any, Iterable, Iterator, Sequence

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import kws_streams  # noqa: E402
from kws_manifest import check_keys  # noqa: E402
from kws_scoring import (Scoring, decide, hits, poisson_interval, smooth, spurious,  # noqa: E402
                         wilson_interval, zero_event_bound)
from kws_streams import (DEFAULT_MAX_STREAM_S, EVAL_SHARES, POSITIVE_SHARE, STREAM_SHARES,  # noqa: E402
                         SUBSHARES, Stream)

REPORT_VERSION = 1
# the report's FA/h columns, in this order (the plan: speech, music and TTS speech separately; then noise)
NEGATIVE_SHARES = ("eval-speech", "eval-music", "eval-tts", "eval-noise")
assert set(NEGATIVE_SHARES) == set(EVAL_SHARES)
DEFAULT_THRESHOLDS = 50
ABSENT = "absent"


class EvalError(ValueError):
    """A stream, detector output or report the harness refuses, with the reason."""


def _check_keys(cls: type, d: Any, where: str) -> None:
    check_keys(cls, d, where, EvalError)   # kws_manifest's schema-key check, raising the harness's error


# ---------------------------------------------------------------- the report


@dataclasses.dataclass
class RecallFigure:
    """Hits over positives with the Wilson 95 % interval; `recall` is None when there is no positive."""

    hits: int
    positives: int
    recall: float | None
    interval: list[float] | None
    frr: float | None

    @classmethod
    def of(cls, n_hits: int, n_positives: int) -> "RecallFigure":
        if n_positives <= 0:
            return cls(hits=0, positives=0, recall=None, interval=None, frr=None)
        lo, hi = wilson_interval(n_hits, n_positives)
        return cls(hits=int(n_hits), positives=int(n_positives), recall=n_hits / n_positives,
                   interval=[float(lo), float(hi)], frr=1.0 - n_hits / n_positives)

    @classmethod
    def from_dict(cls, d: dict[str, Any], where: str) -> "RecallFigure":
        _check_keys(cls, d, where)
        return cls(hits=int(d["hits"]), positives=int(d["positives"]),
                   recall=None if d["recall"] is None else float(d["recall"]),
                   interval=None if d["interval"] is None else [float(v) for v in d["interval"]],
                   frr=None if d["frr"] is None else float(d["frr"]))


@dataclasses.dataclass
class ShareFigure:
    """Events on one negative share: FA/h = events / hours with the exact Poisson 95 % interval, and at zero
    events the one-sided 95 % bound ln 20 / H (None otherwise)."""

    events: int
    hours: float
    streams: int
    fa_per_hour: float
    interval: list[float]
    zero_event_bound: float | None

    @classmethod
    def of(cls, events: int, hours: float, streams: int) -> "ShareFigure":
        lo, hi = poisson_interval(events, hours)
        return cls(events=int(events), hours=float(hours), streams=int(streams), fa_per_hour=events / hours,
                   interval=[float(lo), float(hi)],
                   zero_event_bound=zero_event_bound(hours) if events == 0 else None)

    @classmethod
    def from_dict(cls, d: dict[str, Any], where: str) -> "ShareFigure":
        _check_keys(cls, d, where)
        return cls(events=int(d["events"]), hours=float(d["hours"]), streams=int(d["streams"]),
                   fa_per_hour=float(d["fa_per_hour"]), interval=[float(v) for v in d["interval"]],
                   zero_event_bound=None if d["zero_event_bound"] is None else float(d["zero_event_bound"]))


@dataclasses.dataclass
class Row:
    """One threshold of the sweep."""

    threshold: float
    recall: RecallFigure                  # over every positive
    spurious: int                         # events on positive streams inside no window (never in FA/h)
    by_subshare: dict[str, RecallFigure]  # recall per subshare (eval-speech, eval-tts, holdout) present
    shares: dict[str, ShareFigure]        # per negative share present

    def to_dict(self) -> dict[str, Any]:
        return {"threshold": self.threshold, "recall": dataclasses.asdict(self.recall),
                "spurious": self.spurious,
                "by_subshare": {k: dataclasses.asdict(v) for k, v in self.by_subshare.items()},
                "shares": {k: dataclasses.asdict(v) for k, v in self.shares.items()}}

    @classmethod
    def from_dict(cls, d: dict[str, Any], where: str) -> "Row":
        _check_keys(cls, d, where)
        for k in ("by_subshare", "shares"):
            if not isinstance(d[k], dict):
                raise EvalError(f"{where}.{k}: expected an object")
        return cls(threshold=float(d["threshold"]),
                   recall=RecallFigure.from_dict(d["recall"], f"{where}.recall"), spurious=int(d["spurious"]),
                   by_subshare={k: RecallFigure.from_dict(v, f"{where}.by_subshare[{k}]")
                                for k, v in d["by_subshare"].items()},
                   shares={k: ShareFigure.from_dict(v, f"{where}.shares[{k}]")
                           for k, v in d["shares"].items()})


@dataclasses.dataclass
class Report:
    """The committed report: provenance, the scoring numbers, hours and counts, one Row per threshold."""

    scoring: dict[str, Any]           # Scoring.to_dict()
    detector: dict[str, Any]          # {"name": str, "params": dict}
    hours: dict[str, float]           # per negative share present: sum of negative_samples / rate / 3600
    negative_streams: dict[str, int]  # per negative share present
    positives: dict[str, int]         # per subshare present
    rows: list[Row]
    manifest_name: str | None = None
    manifest_hash: str | None = None
    eval_set_id: dict[str, str] = dataclasses.field(default_factory=dict)  # per share, from the lock
    holdout_set_id: str | None = None
    # {"log_mel_contract_version", "dsptap_commit"} of the front end the detector ran through
    front_end: dict[str, Any] = dataclasses.field(default_factory=dict)
    wall_s: float | None = None       # measured wall time of the scoring + sweep, when the CLI ran it
    # the negative-stream packing bound (kws_streams.streams_from_lock's max_stream_s) the streams were
    # assembled with, when they came from a lock: every stream is decided after a fresh reset, so the
    # t = 0 rule and the refractory restart make every FA/h row depend on it by at most streams / H
    max_stream_s: float | None = None
    report_version: int = REPORT_VERSION

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["rows"] = [r.to_dict() for r in self.rows]
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Report":
        _check_keys(cls, d, "report")
        if int(d["report_version"]) != REPORT_VERSION:
            raise EvalError(f"report: report_version {d['report_version']}, expected {REPORT_VERSION}")
        if not isinstance(d["rows"], list):
            raise EvalError("report.rows: expected a list")
        return cls(scoring=dict(d["scoring"]), detector=dict(d["detector"]),
                   hours={k: float(v) for k, v in d["hours"].items()},
                   negative_streams={k: int(v) for k, v in d["negative_streams"].items()},
                   positives={k: int(v) for k, v in d["positives"].items()},
                   rows=[Row.from_dict(r, f"report.rows[{i}]") for i, r in enumerate(d["rows"])],
                   manifest_name=d.get("manifest_name"), manifest_hash=d.get("manifest_hash"),
                   eval_set_id=dict(d.get("eval_set_id") or {}), holdout_set_id=d.get("holdout_set_id"),
                   front_end=dict(d.get("front_end") or {}),
                   wall_s=None if d.get("wall_s") is None else float(d["wall_s"]),
                   max_stream_s=None if d.get("max_stream_s") is None else float(d["max_stream_s"]),
                   report_version=int(d["report_version"]))

    def markdown(self) -> str:
        return markdown(self)


def format_hours(hours: float) -> str:
    return f"{hours:.4g} h"


def format_recall(f: RecallFigure | None) -> str:
    if f is None or f.positives == 0 or f.recall is None or f.interval is None:
        return ABSENT
    return f"{f.recall:.3f} [{f.interval[0]:.3f}, {f.interval[1]:.3f}] ({f.hits}/{f.positives})"


def format_fa(f: ShareFigure | None) -> str:
    """FA/h with its interval, the zero-event bound when there was no event, and always its hours."""
    if f is None:
        return ABSENT
    bound = f" <= {f.zero_event_bound:.2f}" if f.zero_event_bound is not None else ""
    return (f"{f.fa_per_hour:.2f} [{f.interval[0]:.2f}, {f.interval[1]:.2f}]{bound} "
            f"({f.events} in {format_hours(f.hours)})")


def markdown(r: Report) -> str:
    """The committed table: one row per threshold; speech, music, TTS speech and noise FA/h as separate
    columns (`absent` when the streams had no such share), eval-tts recall beside the hold-out recall."""
    s = r.scoring
    lines = ["# KWS evaluation report", ""]
    lines.append(f"- manifest: {r.manifest_name or ABSENT}"
                 + (f" (`{r.manifest_hash}`)" if r.manifest_hash else ""))
    lines.append(f"- detector: `{r.detector.get('name', '?')}` "
                 f"{json.dumps(r.detector.get('params', {}), sort_keys=True)}")
    lines.append(f"- scoring: hop {s['hop']} at {s['sample_rate']} Hz; T = {s['tolerance_hops']} hops, "
                 f"L = {s['latency_hops']} hops (hit window [h - T, h + L + T]); "
                 f"W = {s['smoothing_hops']} hops, R = {s['refractory_hops']} hops")
    fe = r.front_end
    lines.append(f"- front end: log_mel_contract_version {fe.get('log_mel_contract_version', ABSENT)}, "
                 f"DspTap `{fe.get('dsptap_commit', ABSENT)}`")
    ids = ", ".join(f"{k} `{v}`" for k, v in sorted(r.eval_set_id.items())) or ABSENT
    lines.append(f"- eval_set_id: {ids}")
    lines.append(f"- holdout_set_id: `{r.holdout_set_id}`" if r.holdout_set_id
                 else f"- holdout_set_id: {ABSENT}")
    packed = f", packed at <= {r.max_stream_s:g} s" if r.max_stream_s is not None else ""
    hours = ", ".join(f"{sh} H = {format_hours(r.hours[sh])} ({r.negative_streams.get(sh, 0)} streams"
                      f"{packed})" if sh in r.hours else f"{sh} {ABSENT}" for sh in NEGATIVE_SHARES)
    lines.append(f"- hours: {hours}")
    pos = ", ".join(f"{sub} {r.positives[sub]}" if sub in r.positives else f"{sub} {ABSENT}"
                    for sub in SUBSHARES)
    lines.append(f"- positives: {pos}")
    if r.wall_s is not None:
        lines.append(f"- wall time: {r.wall_s:.1f} s (scoring and sweep)")
    lines.append("")
    lines.append("| threshold | recall [95 %] (hits/positives) | FRR | spurious | FA/h speech | FA/h music "
                 "| FA/h TTS | FA/h noise | recall eval-tts | recall hold-out |")
    lines.append("|---|---|---|---|---|---|---|---|---|---|")
    for row in r.rows:
        frr = ABSENT if row.recall.frr is None else f"{row.recall.frr:.3f}"
        # the threshold as its shortest exact repr: quantiles 1 - 1e-8 apart stay distinct
        cells = [repr(row.threshold), format_recall(row.recall), frr, str(row.spurious)]
        cells += [format_fa(row.shares.get(sh)) for sh in NEGATIVE_SHARES]
        cells += [format_recall(row.by_subshare.get("eval-tts")),
                  format_recall(row.by_subshare.get("holdout"))]
        lines.append("| " + " | ".join(cells) + " |")
    lines.append("")
    lines.append("FA/h cells read `rate [Poisson 95 % lo, hi] (events in H)`; at zero events `<= ln 20 / H` "
                 "is the one-sided 95 % bound. Spurious events lie on positive streams outside every hit "
                 "window and never enter FA/h. Every negative stream is decided after a fresh reset, so each "
                 "FA/h row depends on the stream packing by at most streams / H (the threshold-0 row). The "
                 "eval-tts and hold-out recalls stand beside each other as a descriptive gap figure, never a "
                 "pass.")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- streams and scores


def _samples(stream: Stream) -> np.ndarray:
    """The stream's audio (`Stream.samples`, decoded on every call when lazy), its refusals as EvalErrors."""
    try:
        return stream.samples()
    except kws_streams.StreamError as e:
        raise EvalError(str(e)) from None


def _detector_name(detector: Any) -> str:
    return str(getattr(detector, "name", type(detector).__name__))


def _require_stream(s: Any) -> None:
    if not isinstance(s, Stream):
        raise EvalError(f"expected a Stream, got {type(s).__name__}")


def check_stream(s: Stream, n_samples: int, scoring: Scoring, seen_positives: set[str] | None = None) -> None:
    """One stream's accounting against its decoded length: `kws_streams.validate_stream` (one rule set, not
    a copy), its StreamError re-raised as EvalError. Refuses, by stream id: an unknown share; a non-integer
    negative_samples; an empty stream; a positive stream without positives, with negative_samples != 0 or
    with a subshare outside SUBSHARES; a positive whose endpoint lies beyond its audio, whose endpoint_hop
    is not the endpoint's hop, whose window ends beyond the stream's hops, or whose id is already in
    `seen_positives` (one hit per utterance needs unique ids across the list); a negative stream with
    positives or whose negative_samples != n_samples."""
    _require_stream(s)
    try:
        kws_streams.validate_stream(s, n_samples, scoring, seen_positives)
    except kws_streams.StreamError as e:
        raise EvalError(str(e)) from None


def check_streams(streams: Sequence[Stream], scoring: Scoring) -> dict[str, int]:
    """The harness's accounting preconditions over a stream list — no streams at all, a duplicate stream
    id, and `check_stream` per stream against its decoded audio with one seen-positives set; returns the
    sample count per stream id. Decodes every stream once: `score_streams` makes the same checks on the
    audio it decodes to score, so this is the path for scores supplied as a bare dict."""
    if not streams:
        raise EvalError("no streams to score")
    n_samples: dict[str, int] = {}
    seen_positives: set[str] = set()
    for s in streams:
        _require_stream(s)
        if s.id in n_samples:
            raise EvalError(f"duplicate stream id {s.id!r}")
        n = int(_samples(s).size)
        check_stream(s, n, scoring, seen_positives)
        n_samples[s.id] = n
    return n_samples


def check_scores(stream_id: str, y: Any, n_samples: int, scoring: Scoring, detector_name: str = "?") \
        -> np.ndarray:
    """The score contract, refused by stream and detector name: a one-dimensional array of real numbers
    (a float or integer dtype; complex, bool, object and string arrays are refused rather than cast, since
    a dropped imaginary part would otherwise surface only as numpy's warning) with exactly
    n_samples // hop finite values in [0, 1]. Returns the scores as float64."""
    y = np.asarray(y)
    if not (np.issubdtype(y.dtype, np.floating) or np.issubdtype(y.dtype, np.integer)):
        raise EvalError(f"stream {stream_id!r}: detector {detector_name!r} returned dtype {y.dtype}; scores "
                        "must be real numbers (float64 in [0, 1])")
    y = y.astype(np.float64)
    n_hops = int(n_samples) // scoring.hop
    if y.ndim != 1 or y.size != n_hops:
        raise EvalError(f"stream {stream_id!r}: detector {detector_name!r} returned {y.shape} scores for "
                        f"{n_hops} hops ({n_samples} samples at hop {scoring.hop})")
    if y.size and (not np.all(np.isfinite(y)) or y.min() < 0.0 or y.max() > 1.0):
        raise EvalError(f"stream {stream_id!r}: scores must be finite and within [0, 1]")
    return y


def score_stream(stream: Stream, detector: Any, scoring: Scoring, audio: np.ndarray | None = None) \
        -> np.ndarray:
    """One score per completed hop: `detector.score_stream(id, n)` when the detector plants scores, else
    `detector.score(audio)`; the result checked by `check_scores`. `audio` is the stream's samples when
    the caller already loaded them (lazy streams decode on every `samples()` call)."""
    if audio is None:
        audio = _samples(stream)
    n_samples = int(audio.size)
    if hasattr(detector, "score_stream"):
        y = detector.score_stream(stream.id, n_samples)
    else:
        y = detector.score(audio)
    return check_scores(stream.id, y, n_samples, scoring, _detector_name(detector))


class Scored(Mapping):
    """What `score_streams` returns: the scores keyed by stream id — a read-only mapping, so
    `scored[id]`, `scored.values()` and `default_thresholds(scored, ...)` read as with a dict — plus the
    sample count every stream was checked and scored at. `evaluate` takes it as already verified (each
    stream was decoded once, its accounting checked against that decode and the score contract enforced)
    and re-runs only the accounting rules against the recorded counts, without decoding, so a stream
    edited after scoring still cannot mis-account."""

    def __init__(self, scores: dict[str, np.ndarray], n_samples: dict[str, int]):
        if set(scores) != set(n_samples):
            raise EvalError("Scored: scores and sample counts must cover the same stream ids")
        self.scores = dict(scores)
        self.n_samples = {k: int(v) for k, v in n_samples.items()}

    def __getitem__(self, stream_id: str) -> np.ndarray:
        return self.scores[stream_id]

    def __iter__(self) -> Iterator[str]:
        return iter(self.scores)

    def __len__(self) -> int:
        return len(self.scores)


def score_streams(streams: Sequence[Stream], detector: Any, scoring: Scoring) -> Scored:
    """Every stream decoded once, checked (`check_stream` with one seen-positives set; no streams at all
    and a duplicate stream id refused) and scored (`score_stream`)."""
    if not streams:
        raise EvalError("no streams to score")
    scores: dict[str, np.ndarray] = {}
    n_samples: dict[str, int] = {}
    seen_positives: set[str] = set()
    for s in streams:
        _require_stream(s)
        if s.id in scores:
            raise EvalError(f"duplicate stream id {s.id!r}")
        x = _samples(s)
        check_stream(s, int(x.size), scoring, seen_positives)
        scores[s.id] = score_stream(s, detector, scoring, audio=x)
        n_samples[s.id] = int(x.size)
    return Scored(scores, n_samples)


def default_thresholds(stream_scores: Iterable[np.ndarray] | Mapping[str, np.ndarray], scoring: Scoring,
                       n: int = DEFAULT_THRESHOLDS) -> list[float]:
    """0, 1 and the n quantiles (linspace(0, 1, n)) of the per-stream maximum smoothed score, deduplicated
    and sorted — so the sweep's thresholds land where the streams' scores are. Each is clipped to [0, 1]
    and rounded to 12 decimals: a moving average of scores at 1.0 carries ~1e-14 of summation noise
    (measured max 1.0000000000000455 over the 412 bring-up streams, 92 of them above 1.0 and 302 at
    exactly 1.0, BandEnergyBaseline, W = 10, 9 September 2026), which is not a second threshold."""
    if n < 2:
        raise EvalError(f"default_thresholds: n must be at least 2, got {n}")
    if isinstance(stream_scores, Mapping):
        stream_scores = stream_scores.values()
    maxes = [float(smooth(y, scoring.smoothing_hops).max()) for y in stream_scores if np.asarray(y).size]
    values = {0.0, 1.0}
    if maxes:
        values.update(threshold_value(q) for q in np.quantile(np.asarray(maxes), np.linspace(0.0, 1.0, n)))
    return sorted(values)


def threshold_value(v: float) -> float:
    """A sweep threshold: clipped to [0, 1], rounded to 12 decimals (float noise is not a threshold)."""
    return min(1.0, max(0.0, round(float(v), 12)))


# ---------------------------------------------------------------- the sweep


PROVENANCE_FIELDS = ("manifest_name", "manifest_hash", "eval_set_id", "holdout_set_id", "front_end",
                     "wall_s", "max_stream_s")


def _verified_scores(streams: Sequence[Stream], detector: Any, scoring: Scoring,
                     scores: Scored | Mapping[str, Any] | None) -> Scored:
    """The scores `evaluate` sweeps, every stream's accounting checked: scored here when None; a `Scored`
    re-checked against its recorded sample counts without decoding; a bare mapping re-checked against a
    fresh decode (`check_streams`) and put through the score contract (`check_scores`). Scores for a
    stream id the list does not carry, or a stream without scores, are refused."""
    if scores is None:
        return score_streams(streams, detector, scoring)
    if isinstance(scores, Scored):
        n_samples = scores.n_samples
        if not streams:
            raise EvalError("no streams to score")
        seen_ids: set[str] = set()
        seen_positives: set[str] = set()
        for s in streams:
            _require_stream(s)
            if s.id in seen_ids:
                raise EvalError(f"duplicate stream id {s.id!r}")
            seen_ids.add(s.id)
            if s.id in n_samples:
                check_stream(s, n_samples[s.id], scoring, seen_positives)
    else:
        n_samples = check_streams(streams, scoring)
    missing = [s.id for s in streams if s.id not in scores]
    if missing:
        raise EvalError(f"no scores for stream(s) {missing[:5]}")
    unknown = sorted(set(scores) - {s.id for s in streams})
    if unknown:
        raise EvalError(f"scores for unknown stream id(s) {unknown[:5]}")
    if isinstance(scores, Scored):
        return scores
    name = _detector_name(detector)
    return Scored({s.id: check_scores(s.id, scores[s.id], n_samples[s.id], scoring, name) for s in streams},
                  n_samples)


def evaluate(streams: Sequence[Stream], detector: Any, scoring: Scoring, thresholds: Sequence[float],
             scores: Scored | Mapping[str, Any] | None = None,
             provenance: dict[str, Any] | None = None) -> Report:
    """Score once, sweep the thresholds, and fill the report.

    `scores`: None scores the streams here (`score_streams`); the `Scored` that `score_streams` returned is
    taken as verified and only its accounting is re-checked against the recorded sample counts (no second
    decode); a bare mapping of id -> scores is re-checked in full (the streams decoded once, the score
    contract enforced). `thresholds` must be finite, within [0, 1] (scores are) and distinct.
    `provenance` carries the report's header fields (PROVENANCE_FIELDS: manifest_name, manifest_hash,
    eval_set_id, holdout_set_id, front_end, wall_s, max_stream_s).
    """
    thresholds = [float(t) for t in thresholds]
    if not thresholds or not all(math.isfinite(t) for t in thresholds):
        raise EvalError(f"thresholds must be a non-empty list of finite numbers, got {thresholds}")
    outside = [t for t in thresholds if not 0.0 <= t <= 1.0]
    if outside:
        raise EvalError(f"thresholds must lie in [0, 1] (scores do), got {outside}")
    if len(set(thresholds)) != len(thresholds):
        repeated = sorted({t for t in thresholds if thresholds.count(t) > 1})
        raise EvalError(f"duplicate threshold(s) {repeated}")
    prov = dict(provenance or {})
    unknown = sorted(set(prov) - set(PROVENANCE_FIELDS))
    if unknown:
        raise EvalError(f"provenance: unknown field(s) {unknown}")
    scored = _verified_scores(streams, detector, scoring, scores)
    positive_streams = [s for s in streams if s.share == POSITIVE_SHARE]
    negative_streams = [s for s in streams if s.share != POSITIVE_SHARE]
    # hours: the integer sample sum per share divided once, as kws_streams.hours_per_share and the lock's
    # summary do (a running float sum differs from them by an ulp)
    samples: dict[str, int] = {}
    n_negative: dict[str, int] = {}
    for s in negative_streams:
        samples[s.share] = samples.get(s.share, 0) + int(s.negative_samples)
        n_negative[s.share] = n_negative.get(s.share, 0) + 1
    hours = {sh: scoring.hours(n) for sh, n in samples.items()}
    positives: dict[str, int] = {}
    for s in positive_streams:
        positives[s.subshare] = positives.get(s.subshare, 0) + len(s.positives)
    # the smoothed maximum bounds the events: below it no crossing exists, so the sweep skips those streams
    smoothed_max = {sid: (float(smooth(y, scoring.smoothing_hops).max()) if y.size else -math.inf)
                    for sid, y in scored.items()}
    rows: list[Row] = []
    for theta in sorted(thresholds):
        n_hits = 0
        n_spurious = 0
        sub_hits: dict[str, int] = {}
        for s in positive_streams:
            ev = decide(scored[s.id], scoring, theta) if smoothed_max[s.id] >= theta else []
            h = sum(hits(ev, s.positives, scoring).values())
            n_hits += h
            sub_hits[s.subshare] = sub_hits.get(s.subshare, 0) + h
            n_spurious += spurious(ev, s.positives, scoring)
        events: dict[str, int] = {}
        for s in negative_streams:
            ev = decide(scored[s.id], scoring, theta) if smoothed_max[s.id] >= theta else []
            events[s.share] = events.get(s.share, 0) + len(ev)
        rows.append(Row(threshold=theta, recall=RecallFigure.of(n_hits, sum(positives.values())),
                        spurious=n_spurious,
                        by_subshare={sub: RecallFigure.of(sub_hits.get(sub, 0), positives[sub])
                                     for sub in SUBSHARES if sub in positives},
                        shares={sh: ShareFigure.of(events.get(sh, 0), hours[sh], n_negative[sh])
                                for sh in NEGATIVE_SHARES if sh in hours}))
    return Report(scoring=scoring.to_dict(),
                  detector={"name": _detector_name(detector),
                            "params": dict(getattr(detector, "params", {}) or {})},
                  hours={sh: hours[sh] for sh in NEGATIVE_SHARES if sh in hours},
                  negative_streams={sh: n_negative[sh] for sh in NEGATIVE_SHARES if sh in hours},
                  positives={sub: positives[sub] for sub in SUBSHARES if sub in positives}, rows=rows, **prov)


def write_report(report: Report, out: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    out.mkdir(parents=True, exist_ok=True)
    json_path, md_path = out / "report.json", out / "report.md"
    json_path.write_text(json.dumps(report.to_dict(), indent=1, sort_keys=True) + "\n", encoding="utf-8")
    md_path.write_text(report.markdown(), encoding="utf-8")
    return json_path, md_path


def read_report(path: pathlib.Path) -> Report:
    with pathlib.Path(path).open(encoding="utf-8") as f:
        return Report.from_dict(json.load(f))


# ---------------------------------------------------------------- CLI


def _check_args(args: argparse.Namespace) -> None:
    """The sweep options refused by name before anything is read or decoded."""
    if args.grid < 0 or args.grid == 1:
        raise EvalError(f"--grid must be 0 (off) or at least 2, got {args.grid}")
    if args.thresholds < 2:
        raise EvalError(f"--thresholds must be at least 2, got {args.thresholds}")
    if not args.max_stream_s > 0.0:
        raise EvalError(f"--max-stream-s must be positive, got {args.max_stream_s}")


def _scoring_from(manifest: Any, args: argparse.Namespace) -> Scoring:
    g = manifest.recipe.geometry
    return Scoring(hop=int(g.hop), sample_rate=int(round(g.sample_rate)),
                   tolerance_hops=int(manifest.recipe.label.tolerance_hops),
                   smoothing_hops=int(args.smoothing), refractory_hops=int(args.refractory))


def _detector_from(manifest: Any, name: str) -> Any:
    import kws_detectors  # B's module; imported here so the report and the oracle need no bridge
    if name == "band-energy":
        return kws_detectors.BandEnergyBaseline(manifest.recipe.geometry)
    raise EvalError(f"unknown detector {name!r} (known: band-energy)")


def _front_end_record() -> dict[str, Any]:
    import kws_features
    return {"log_mel_contract_version": kws_features.contract_version(),
            "dsptap_commit": kws_features.dsptap_commit()}


def _run(streams: list[Stream], detector: Any, scoring: Scoring, args: argparse.Namespace,
         provenance: dict[str, Any]) -> Report:
    """One decode per stream: `score_streams` checks every stream's accounting (the rules of
    kws_streams.validate_streams) on the audio it scores, and `evaluate` takes its `Scored` as verified."""
    t0 = time.time()
    scored = score_streams(streams, detector, scoring)
    thresholds = default_thresholds(scored, scoring, n=int(args.thresholds))
    if args.grid:  # a detector whose maxima saturate collapses the quantiles; an even grid fills the curve
        grid = {threshold_value(v) for v in np.linspace(0.0, 1.0, int(args.grid))}
        thresholds = sorted(set(thresholds) | grid)
    report = evaluate(streams, detector, scoring, thresholds, scores=scored, provenance=provenance)
    report.wall_s = time.time() - t0
    return report


def cmd_sweep(args: argparse.Namespace) -> int:
    from kws_manifest import load_manifest, manifest_hash, read_lock
    from kws_store import Store, resolve_store
    _check_args(args)
    manifest = load_manifest(args.manifest)
    lock = read_lock(args.lock)
    if lock.manifest_hash != manifest_hash(manifest):
        raise EvalError(f"lock {args.lock} was built from manifest hash {lock.manifest_hash}, the manifest's "
                        f"is {manifest_hash(manifest)}")
    store = Store(resolve_store(args.store))
    scoring = _scoring_from(manifest, args)
    streams = kws_streams.streams_from_lock(manifest, lock, store, scoring,
                                            max_stream_s=float(args.max_stream_s))
    detector = _detector_from(manifest, args.detector)
    provenance = {"manifest_name": manifest.name, "manifest_hash": lock.manifest_hash,
                  "eval_set_id": dict(lock.eval_set_id), "holdout_set_id": lock.holdout_set_id,
                  "front_end": _front_end_record(), "max_stream_s": float(args.max_stream_s)}
    report = _run(streams, detector, scoring, args, provenance)
    json_path, md_path = write_report(report, pathlib.Path(args.out))
    print(f"kws_eval: {len(streams)} streams, {len(report.rows)} thresholds, {report.wall_s:.1f} s -> "
          f"{json_path}, {md_path}")
    return 0


def cmd_holdout(args: argparse.Namespace) -> int:
    import kws_holdout
    from kws_manifest import load_manifest, manifest_hash, read_lock
    from kws_store import Store, resolve_store
    _check_args(args)
    manifest = load_manifest(args.manifest)
    store = Store(resolve_store(args.store))
    scoring = _scoring_from(manifest, args)
    holdout = kws_holdout.load_holdout(args.holdout)
    kws_holdout.verify_holdout(holdout, store)
    streams = kws_holdout.streams_from_holdout(holdout, store, scoring)
    provenance: dict[str, Any] = {"manifest_name": manifest.name, "manifest_hash": manifest_hash(manifest),
                                  "holdout_set_id": kws_holdout.holdout_set_id(holdout),
                                  "front_end": _front_end_record()}
    if args.lock:
        lock = read_lock(args.lock)
        if lock.manifest_hash != manifest_hash(manifest):
            raise EvalError(f"lock {args.lock} was built from manifest hash {lock.manifest_hash}, the "
                            f"manifest's is {manifest_hash(manifest)}")
        streams += kws_streams.streams_from_lock(manifest, lock, store, scoring,
                                                 max_stream_s=float(args.max_stream_s))
        provenance["eval_set_id"] = dict(lock.eval_set_id)
        provenance["max_stream_s"] = float(args.max_stream_s)
    detector = _detector_from(manifest, args.detector)
    report = _run(streams, detector, scoring, args, provenance)
    json_path, md_path = write_report(report, pathlib.Path(args.out))
    print(f"kws_eval: hold-out {provenance['holdout_set_id'][:12]}, {len(streams)} streams, "
          f"{len(report.rows)} thresholds, {report.wall_s:.1f} s -> {json_path}, {md_path}")
    return 0


def _common(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--store", default=None, help="the feature store (or MUTAP_KWS_STORE); no default")
    ap.add_argument("--detector", default="band-energy", help="band-energy (the sanity baseline)")
    ap.add_argument("--thresholds", type=int, default=DEFAULT_THRESHOLDS,
                    help=f"quantile count of default_thresholds (default {DEFAULT_THRESHOLDS})")
    ap.add_argument("--grid", type=int, default=0,
                    help="also sweep N evenly spaced thresholds on [0, 1] (default 0: the quantiles only)")
    ap.add_argument("--smoothing", type=int, default=Scoring.smoothing_hops,
                    help=f"W, the trailing moving average in hops (default {Scoring.smoothing_hops})")
    ap.add_argument("--refractory", type=int, default=Scoring.refractory_hops,
                    help=f"R, the refractory period in hops (default {Scoring.refractory_hops})")
    ap.add_argument("--max-stream-s", type=float, default=DEFAULT_MAX_STREAM_S,
                    help="negative clips are concatenated into streams of at most this many seconds "
                         f"(default {DEFAULT_MAX_STREAM_S:g}, kws_streams.DEFAULT_MAX_STREAM_S; recorded in "
                         "the report)")
    ap.add_argument("--out", required=True, help="directory for report.json and report.md")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command", required=True)
    sweep = sub.add_parser("sweep", help="the eval shares of a lock through a detector")
    sweep.add_argument("--manifest", required=True)
    sweep.add_argument("--lock", required=True)
    _common(sweep)
    hold = sub.add_parser("holdout",
                          help="verify a hold-out and score it (with the lock's eval shares if given)")
    hold.add_argument("--holdout", required=True, help="holdout.json")
    hold.add_argument("--manifest", required=True, help="for the geometry and the tolerance T")
    hold.add_argument("--lock", default=None, help="score the lock's eval shares in the same report")
    _common(hold)
    args = ap.parse_args(argv)
    try:
        return cmd_sweep(args) if args.command == "sweep" else cmd_holdout(args)
    except (EvalError, ValueError, RuntimeError, OSError) as e:
        print(f"kws_eval: {type(e).__name__}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
