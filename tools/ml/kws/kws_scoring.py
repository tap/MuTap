#!/usr/bin/env python3
"""kws_scoring — the evaluation harness's scoring semantics, defined as numbers (wake-word plan §6 M5).

- A detector yields one score per completed front-end hop: score[t] belongs to frame t, which is complete
  when sample (t + 1) * hop - 1 arrives (log_mel.h's alignment), so an endpoint at sample e has hop index
  h = e // hop and the first frame that can see the whole keyword is h.
- The hit window of a positive is frames [h - T, h + L + T], both ends inclusive: T the manifest's
  endpoint tolerance in hops, L the §7 detection-latency ceiling (20 hops = 200 ms). One hit per utterance.
- The reference decision stage (the numbers `kws.h` carries at M6 and must match, pinned there): the score
  is smoothed by a trailing moving average over W hops, an event fires at hop t when the smoothed score
  crosses the threshold upward (s[t] >= theta and s[t-1] < theta, or t = 0 and s[0] >= theta) and at
  least R hops have passed since the previous event (t - last >= R): a crossing exactly R hops after an
  event fires, one R - 1 hops after it merges into it — the refractory period, under which false accepts
  merge.
- A false accept is an event on a negative stream; FA/h = events / H with H the negative streams' decoded
  duration in hours. Every FA/h carries H, the exact two-sided 95 % Poisson interval on the count
  (chi-square form), and at zero events the one-sided 95 % upper bound ln 20 / H. Recall carries its
  Wilson 95 % interval.

Checked against hand-computed values in `_self_check` (run this file).
"""
from __future__ import annotations

import dataclasses
import math
from typing import Sequence

import numpy as np
from scipy import stats

LATENCY_CEILING_HOPS = 20  # the §7 ceiling: detection within 20 hops (200 ms at hop 160 / 16 kHz)


@dataclasses.dataclass(frozen=True)
class Scoring:
    """The scoring numbers; everything a report prints beside its figures."""

    hop: int = 160                 # samples per hop at the manifest geometry
    sample_rate: int = 16000
    tolerance_hops: int = 3        # T (the manifest's label rule)
    latency_hops: int = LATENCY_CEILING_HOPS  # L
    smoothing_hops: int = 10       # W: trailing moving average (100 ms at the reference hop)
    refractory_hops: int = 100     # R: 1 s at the reference hop

    def __post_init__(self) -> None:
        if self.hop < 1 or self.sample_rate < 1 or self.tolerance_hops < 0 or self.latency_hops < 0 \
                or self.smoothing_hops < 1 or self.refractory_hops < 1:
            raise ValueError(f"invalid scoring numbers: {self}")

    def endpoint_hop(self, endpoint_sample: int) -> int:
        return int(endpoint_sample) // self.hop

    def hit_window(self, endpoint_hop: int) -> tuple[int, int]:
        """[h - T, h + L + T], inclusive."""
        return endpoint_hop - self.tolerance_hops, endpoint_hop + self.latency_hops + self.tolerance_hops

    def hours(self, samples: int) -> float:
        return samples / self.sample_rate / 3600.0

    def to_dict(self) -> dict:
        return dataclasses.asdict(self)


@dataclasses.dataclass(frozen=True)
class Positive:
    """One keyword utterance inside a stream: its id and its endpoint (stream-relative sample and hop)."""

    id: str
    endpoint_sample: int
    endpoint_hop: int


def smooth(scores: np.ndarray, window: int) -> np.ndarray:
    """Trailing moving average over `window` hops (shorter at the start: the mean of what exists)."""
    s = np.asarray(scores, dtype=np.float64)
    if s.ndim != 1:
        raise ValueError("scores must be one value per hop")
    if window <= 1 or s.size == 0:
        return s.copy()
    c = np.cumsum(np.concatenate([[0.0], s]))
    n = np.arange(1, s.size + 1)
    lo = np.maximum(0, n - window)
    return (c[n] - c[lo]) / (n - lo)


def decide(scores: np.ndarray, scoring: Scoring, threshold: float) -> list[int]:
    """Event hops under the reference decision stage: upward threshold crossings of the smoothed score
    that come at least `refractory_hops` (R) after the previous event — t - last >= R, so a crossing
    exactly R hops after an event fires and one R - 1 hops after it merges."""
    s = smooth(scores, scoring.smoothing_hops)
    events: list[int] = []
    last = -scoring.refractory_hops - 1
    prev_above = False
    for t in range(s.size):
        above = bool(s[t] >= threshold)
        if above and not prev_above and t - last >= scoring.refractory_hops:
            events.append(t)
            last = t
        prev_above = above
    return events


def hits(events: Sequence[int], positives: Sequence[Positive], scoring: Scoring) -> dict[str, bool]:
    """One hit per utterance: whether any event falls inside its window."""
    out: dict[str, bool] = {}
    ev = np.asarray(sorted(events), dtype=np.int64)
    for p in positives:
        lo, hi = scoring.hit_window(p.endpoint_hop)
        i = int(np.searchsorted(ev, lo, side="left"))
        out[p.id] = bool(i < ev.size and ev[i] <= hi)
    return out


def spurious(events: Sequence[int], positives: Sequence[Positive], scoring: Scoring) -> int:
    """Events on a positive stream that fall inside no utterance's window (reported, never in FA/h)."""
    n = 0
    for t in events:
        if not any(lo <= t <= hi for lo, hi in (scoring.hit_window(p.endpoint_hop) for p in positives)):
            n += 1
    return n


# ---------------------------------------------------------------- intervals


def poisson_interval(events: int, hours: float, confidence: float = 0.95) -> tuple[float, float]:
    """Exact two-sided Poisson interval on the rate (events per hour), chi-square form:
    lower = chi2.ppf(a/2, 2k) / 2 / H (0 at k = 0), upper = chi2.ppf(1 - a/2, 2k + 2) / 2 / H."""
    if hours <= 0.0:
        raise ValueError("the hours denominator must be positive")
    if events < 0:
        raise ValueError("events must be non-negative")
    a = 1.0 - confidence
    lo = 0.0 if events == 0 else float(stats.chi2.ppf(a / 2.0, 2 * events)) / 2.0 / hours
    hi = float(stats.chi2.ppf(1.0 - a / 2.0, 2 * events + 2)) / 2.0 / hours
    return lo, hi


def zero_event_bound(hours: float, confidence: float = 0.95) -> float:
    """The one-sided upper bound on the rate when no event was seen: -ln(1 - confidence) / H = ln 20 / H."""
    if hours <= 0.0:
        raise ValueError("the hours denominator must be positive")
    return -math.log(1.0 - confidence) / hours


def wilson_interval(successes: int, trials: int, confidence: float = 0.95) -> tuple[float, float]:
    """Wilson score interval for a proportion."""
    if trials <= 0:
        raise ValueError("trials must be positive")
    if not 0 <= successes <= trials:
        raise ValueError("successes must be within [0, trials]")
    z = float(stats.norm.ppf(1.0 - (1.0 - confidence) / 2.0))
    p = successes / trials
    denom = 1.0 + z * z / trials
    centre = (p + z * z / (2.0 * trials)) / denom
    half = z * math.sqrt(p * (1.0 - p) / trials + z * z / (4.0 * trials * trials)) / denom
    return max(0.0, centre - half), min(1.0, centre + half)


# ---------------------------------------------------------------- self-check against hand-computed values


def _self_check() -> None:
    sc = Scoring()
    assert sc.endpoint_hop(9000) == 56 and sc.hit_window(56) == (53, 79)
    # smoothing: the mean of the last W values, shorter at the start
    s = smooth(np.array([1.0, 0.0, 0.0, 0.0]), 2)
    assert np.allclose(s, [1.0, 0.5, 0.0, 0.0])
    # decision: an upward crossing fires once; a second crossing inside R is merged; after R it fires again
    x = np.zeros(400)
    x[10:20] = 1.0
    x[30:40] = 1.0    # inside the refractory period of the first event
    x[200:210] = 1.0  # after it
    ev = decide(x, Scoring(smoothing_hops=1), 0.5)
    assert ev == [10, 200], ev
    # the refractory boundary as a number: a crossing exactly R = 100 hops after an event fires, one at
    # R - 1 hops merges
    x = np.zeros(400)
    x[10], x[110] = 1.0, 1.0
    assert decide(x, Scoring(smoothing_hops=1), 0.5) == [10, 110]
    x = np.zeros(400)
    x[10], x[109] = 1.0, 1.0
    assert decide(x, Scoring(smoothing_hops=1), 0.5) == [10]
    # hits: the window edges are inclusive, one hit per utterance
    p = [Positive("a", 9000, 56), Positive("b", 20000, 125)]
    assert hits([53], p, sc) == {"a": True, "b": False}
    assert hits([52], p, sc) == {"a": False, "b": False}
    assert hits([79, 80], p, sc)["a"] is True and hits([80], p, sc)["a"] is False
    assert spurious([52, 53, 300], p, sc) == 2
    # intervals: k = 3 events in 0.5 h -> 6.0 FA/h, exact 95 % [1.24, 17.53] (chi2 0.025,6 = 1.237; 0.975,8 = 17.535)
    lo, hi = poisson_interval(3, 0.5)
    assert abs(lo - 1.2373) < 0.01, lo  # chi2(0.025, 6) / 2 = 0.6187 per 0.5 h
    assert abs(hi - 17.535) < 0.01, hi
    # k = 0 in 3.0 h: one-sided bound ln 20 / 3 = 0.9986 FA/h; two-sided upper chi2(0.975, 2)/2/3 = 1.229
    assert abs(zero_event_bound(3.0) - 0.99858) < 1e-4
    assert abs(poisson_interval(0, 3.0)[1] - 1.2296) < 1e-3
    # Wilson: 190/200 -> [0.9104, 0.9726]; 1/1 -> [0.2065, 1.0]
    lo, hi = wilson_interval(190, 200)
    assert abs(lo - 0.9104) < 5e-4 and abs(hi - 0.9726) < 5e-4, (lo, hi)
    lo, hi = wilson_interval(1, 1)
    assert abs(lo - 0.2065) < 5e-4 and hi == 1.0, (lo, hi)
    print("kws_scoring: self-check ok")


if __name__ == "__main__":
    _self_check()
