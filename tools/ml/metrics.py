# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""One shared meter for every system in the ML AEC benchmark.

Mirrors the test rig's protocol metrics (tests/test_aec.cpp,
tests/support/echo_scenario.h) and adds two things an offline comparison
against black-box systems needs:

- **Delay compensation.** Overlap-add systems (DTLN-aec) and the chain's
  causal gain filter delay their output; metrics are computed after
  removing each system's constant latency, found by cross-correlating the
  output against the near end over the near-end-only segment.
- **Near-end preservation.** The rig's suppression metric charges any
  near-end damage to the residual, but a dedicated segment (near end only,
  far end silent) reads it directly: output SHOULD equal v; deviation is
  distortion. Reported as an SDR in dB (higher = more transparent;
  a pure linear canceller measures +inf, capped for reporting).
"""
from __future__ import annotations

import dataclasses
import json
import pathlib

import numpy as np

MEASURE_TAIL_BLOCKS = 400  # single-talk windows measure the last 400 blocks
RECOVERY_SKIP_BLOCKS = 200
NEAR_ONLY_SKIP = 2048  # samples; lets the previous segment's echo tail
                       # (room length 1024) die before measuring transparency
MAX_DELAY = 1024
SDR_CAP_DB = 80.0


@dataclasses.dataclass
class Scenario:
    x: np.ndarray  # far end
    v: np.ndarray  # near end
    d: np.ndarray  # true echo
    y: np.ndarray  # mic = d + v
    block: int
    bounds: dict[str, tuple[int, int]]  # segment -> [start, end) in samples
    meta: dict

    @classmethod
    def load(cls, folder: str | pathlib.Path) -> "Scenario":
        folder = pathlib.Path(folder)
        meta = json.loads((folder / "manifest.json").read_text())
        block = meta["block_size"]
        arrays = {n: np.fromfile(folder / f"{n}.f64", dtype="<f8") for n in ("x", "v", "d", "y")}
        bounds = {}
        pos = 0
        for seg in meta["segments"]:
            n = seg["blocks"] * block
            bounds[seg["name"]] = (pos, pos + n)
            pos += n
        return cls(**arrays, block=block, bounds=bounds, meta=meta)


def find_delay(scn: Scenario, e: np.ndarray, max_delay: int = MAX_DELAY) -> int:
    """Constant output latency: the lag minimizing the residual energy
    ||e(shifted) - v||^2 over the near-end-only segment (where output
    should be v). Minimizing the residual — not maximizing a raw
    correlation — matters on pitch-periodic material, whose
    autocorrelation has near-tie peaks at every period multiple."""
    lo, hi = scn.bounds["near_only"]
    lo += NEAR_ONLY_SKIP
    w = (hi - lo) - max_delay  # reference window short enough that every
    v = scn.v[lo : lo + w]     # lag stays inside the segment
    resid = [float(np.sum((e[lo + k : lo + k + w] - v) ** 2)) for k in range(max_delay + 1)]
    return int(np.argmin(resid))


def _db(num: float, den: float, cap: float = SDR_CAP_DB) -> float:
    if den <= 0.0:
        return cap
    return min(10.0 * np.log10(num / den), cap)


@dataclasses.dataclass
class SystemMetrics:
    delay_samples: int
    st_erle_db: float          # converge segment, last 400 blocks
    dt_suppression_db: float   # double-talk, true echo vs residual
    dt_output_snr_db: float    # double-talk, near end vs residual at the output
    rec_erle_db: float         # recovery, after 200 blocks
    ne_sdr_db: float           # near-end-only transparency

    def as_dict(self):
        return dataclasses.asdict(self)


def measure(scn: Scenario, e_raw: np.ndarray) -> SystemMetrics:
    delay = find_delay(scn, e_raw)
    e = np.concatenate((e_raw[delay:], np.zeros(delay)))

    b = scn.block

    lo, hi = scn.bounds["converge"]
    m0 = hi - MEASURE_TAIL_BLOCKS * b
    st_erle = _db(float(np.sum(scn.y[m0:hi] ** 2)), float(np.sum(e[m0:hi] ** 2)))

    lo, hi = scn.bounds["double_talk"]
    resid = e[lo:hi] - scn.v[lo:hi]
    dt_sup = _db(float(np.sum(scn.d[lo:hi] ** 2)), float(np.sum(resid**2)))
    dt_near = _db(float(np.sum(scn.v[lo:hi] ** 2)), float(np.sum(resid**2)))

    lo, hi = scn.bounds["recovery"]
    m0 = lo + RECOVERY_SKIP_BLOCKS * b
    rec_erle = _db(float(np.sum(scn.y[m0:hi] ** 2)), float(np.sum(e[m0:hi] ** 2)))

    lo, hi = scn.bounds["near_only"]
    lo += NEAR_ONLY_SKIP
    dist = e[lo:hi] - scn.v[lo:hi]
    ne_sdr = _db(float(np.sum(scn.v[lo:hi] ** 2)), float(np.sum(dist**2)))

    return SystemMetrics(
        delay_samples=delay,
        st_erle_db=st_erle,
        dt_suppression_db=dt_sup,
        dt_output_snr_db=dt_near,
        rec_erle_db=rec_erle,
        ne_sdr_db=ne_sdr,
    )
