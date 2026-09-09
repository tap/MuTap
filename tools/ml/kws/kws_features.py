#!/usr/bin/env python3
"""kws_features — the feature *values* MuTap's keyword spotter is trained on.

Ownership (wake-word plan §5): DspTap's log_mel.h owns the formulas; this
module owns the values. `Geometry` mirrors tap::dsp::log_mel_geometry field
for field and is carried by the manifest, the shard headers and (M6) the MUKW
weights. Training features come from the **shipping front end** through
DspTap's C ABI bridge (`dsptap_py.LogMel`), computed in double and stored
float32 (decided 8 September 2026). The numpy restatement in the submodule
(tools/reference/make_frontend_reference.py) is the oracle: it is imported
here for two checks only — the parity `self_check` and `assert_band_support`,
because log_mel.h zero-fills a band with no FFT bin inside it silently.

Measured 2026-09-09 on the M0 Mac (bridge vs numpy, the reference signal):
reference geometry 1.5e-14 (log) / 3.4e-14 (PCEN); DspTap's TUNED geometry
1.6e-15 / 5.3e-15. DspTap's CI measured the same C++ against the same
vectors at 6.8e-14 / 1.5e-13 on Linux GCC x86-64 (no FMA contraction, where
Apple clang on arm64 contracts), so SELF_CHECK_TOLERANCE follows DspTap's
tuned-geometry double pin, 5e-13 — rounding-level; a formula drift shows up
at 1e-6 or worse.

The bridge library (`submodules/dsptap/build_capi/`) is keyed to the submodule
commit it was compiled from: `ensure_bridge` writes `.dsptap_commit` beside it
after a build and rebuilds (Release, the type DspTap's CI and ours use) when
the marker is absent or names another commit, so `dsptap_commit()` — the
value the lock and every shard header record — is the commit the C ABI was
actually built from. A submodule with uncommitted changes to tracked files is
refused: no commit describes it.
"""
from __future__ import annotations

import dataclasses
import importlib.util
import pathlib
import shutil
import subprocess
import sys
from typing import Any

import numpy as np

KWS_FEATURES_VERSION = 1  # this module's schema: Geometry fields, as_array layout, self-check record
SELF_CHECK_TOLERANCE = 5e-13

ROOT = pathlib.Path(__file__).resolve().parents[3]
DSPTAP = ROOT / "submodules" / "dsptap"
REFERENCE_SCRIPT = DSPTAP / "tools" / "reference" / "make_frontend_reference.py"
BRIDGE_DIR = DSPTAP / "notebooks"
BRIDGE_BUILD = DSPTAP / "build_capi"       # where dsptap_py looks for the C ABI library (its `_BUILD`)
BRIDGE_MARKER_NAME = ".dsptap_commit"      # beside the library: the submodule commit it was built from


class BridgeError(RuntimeError):
    """The C ABI bridge cannot be tied to a DspTap commit (a dirty submodule, or an unkeyed build)."""


@dataclasses.dataclass(frozen=True)
class Pcen:
    """tap::dsp::pcen_params, field for field."""

    enabled: bool = False
    smoother: float = 0.025
    alpha: float = 0.98
    delta: float = 2.0
    power: float = 0.5
    epsilon: float = 1e-6

    def valid(self) -> bool:
        return (0.0 < self.smoother <= 1.0 and 0.0 <= self.alpha <= 1.0 and self.delta > 0.0
                and 0.0 < self.power <= 1.0 and self.epsilon > 0.0)


@dataclasses.dataclass(frozen=True)
class Geometry:
    """tap::dsp::log_mel_geometry, field for field. window is "hann" or "sqrt_hann"."""

    sample_rate: float = 16000.0
    frame: int = 400
    hop: int = 160
    fft_size: int = 512
    bands: int = 40
    fmin_hz: float = 20.0
    fmax_hz: float = 7600.0
    window: str = "hann"
    preemphasis: float = 0.0
    log_floor: float = 1e-10
    log_shift: float = 5.0
    log_scale: float = 5.0
    pcen: Pcen = Pcen()

    def valid(self) -> bool:
        """log_mel_geometry::valid(), restated."""
        pow2 = self.fft_size >= 4 and (self.fft_size & (self.fft_size - 1)) == 0
        return (self.sample_rate > 0.0 and self.hop >= 1 and self.frame >= self.hop and pow2
                and self.fft_size >= self.frame and self.bands >= 1 and self.fmin_hz >= 0.0
                and self.fmax_hz > self.fmin_hz and self.fmax_hz <= 0.5 * self.sample_rate
                and self.log_floor > 0.0 and self.log_scale > 0.0
                and self.window in ("hann", "sqrt_hann") and self.pcen.valid())

    def hop_index(self, sample: int) -> int:
        """The first frame that covers `sample`: frame t is complete when sample (t+1)*hop - 1 arrives."""
        return int(sample) // self.hop

    # -- JSON (the manifest) -------------------------------------------------
    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        return d

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "Geometry":
        d = dict(d)
        pcen = d.pop("pcen", {}) or {}
        g = cls(**d, pcen=Pcen(**pcen))
        if not g.valid():
            raise ValueError(f"invalid log_mel geometry: {g}")
        return g

    # -- the shard header (18 doubles, in this order) ---------------------------
    ARRAY_FIELDS = ("sample_rate", "frame", "hop", "fft_size", "bands", "fmin_hz", "fmax_hz", "sqrt_hann",
                    "preemphasis", "log_floor", "log_shift", "log_scale", "pcen_enabled", "pcen_smoother",
                    "pcen_alpha", "pcen_delta", "pcen_power", "pcen_epsilon")

    def as_array(self) -> np.ndarray:
        p = self.pcen
        return np.asarray([self.sample_rate, self.frame, self.hop, self.fft_size, self.bands, self.fmin_hz,
                           self.fmax_hz, 1.0 if self.window == "sqrt_hann" else 0.0, self.preemphasis,
                           self.log_floor, self.log_shift, self.log_scale, 1.0 if p.enabled else 0.0,
                           p.smoother, p.alpha, p.delta, p.power, p.epsilon], dtype=np.float64)

    @classmethod
    def from_array(cls, a: np.ndarray) -> "Geometry":
        a = np.asarray(a, dtype=np.float64)
        if a.shape != (len(cls.ARRAY_FIELDS),):
            raise ValueError(f"geometry array has shape {a.shape}, expected ({len(cls.ARRAY_FIELDS)},)")
        g = cls(sample_rate=float(a[0]), frame=int(a[1]), hop=int(a[2]), fft_size=int(a[3]), bands=int(a[4]),
                fmin_hz=float(a[5]), fmax_hz=float(a[6]), window="sqrt_hann" if a[7] != 0.0 else "hann",
                preemphasis=float(a[8]), log_floor=float(a[9]), log_shift=float(a[10]), log_scale=float(a[11]),
                pcen=Pcen(enabled=a[12] != 0.0, smoother=float(a[13]), alpha=float(a[14]), delta=float(a[15]),
                          power=float(a[16]), epsilon=float(a[17])))
        if not g.valid():
            raise ValueError(f"invalid log_mel geometry in shard header: {g}")
        return g


REFERENCE = Geometry()


# -- the submodule's oracle and bridge ----------------------------------------------------------------


_REFERENCE_MODULE = None


def reference_module():
    """The numpy restatement of log_mel.h, imported from the DspTap submodule (the oracle)."""
    global _REFERENCE_MODULE
    if _REFERENCE_MODULE is None:
        spec = importlib.util.spec_from_file_location("make_frontend_reference", REFERENCE_SCRIPT)
        if spec is None or spec.loader is None:
            raise ImportError(f"cannot import the DspTap reference at {REFERENCE_SCRIPT}; is the submodule checked out?")
        mod = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = mod  # dataclasses resolve annotations through sys.modules
        spec.loader.exec_module(mod)
        _REFERENCE_MODULE = mod
    return _REFERENCE_MODULE


def _git(*args: str) -> str:
    out = subprocess.run(["git", "-C", str(DSPTAP), *args], capture_output=True, text=True, check=True)
    return out.stdout.strip()


def dsptap_head() -> str:
    """The submodule's checked-out commit (`git rev-parse HEAD`)."""
    return _git("rev-parse", "HEAD")


def dsptap_dirty() -> list[str]:
    """Tracked files with uncommitted changes in the submodule (`git status --porcelain -uno`)."""
    return [line for line in _git("status", "--porcelain", "-uno").splitlines() if line.strip()]


def _bridge_library(build_dir: pathlib.Path) -> pathlib.Path | None:
    """The C ABI library if it exists under build_dir, looked up the way dsptap_py._lib_path does."""
    stem = "dsptap_capi"
    names = {"linux": f"lib{stem}.so", "darwin": f"lib{stem}.dylib", "win32": f"{stem}.dll"}
    name = next(v for k, v in names.items() if sys.platform.startswith(k))
    for cand in (build_dir / name, build_dir / "Release" / name, build_dir / "Debug" / name):
        if cand.exists():
            return cand
    return None


def _build_bridge(build_dir: pathlib.Path, verbose: bool = False) -> None:
    """cmake the C ABI into build_dir (Release, as DspTap's own CI builds it); output shown when verbose."""
    kw: dict[str, Any] = {"cwd": str(DSPTAP), "check": True}
    if not verbose:
        kw["capture_output"] = True
    subprocess.run(["cmake", "-B", str(build_dir), "-S", str(DSPTAP / "tools" / "capi"),
                    "-DCMAKE_BUILD_TYPE=Release"], **kw)
    subprocess.run(["cmake", "--build", str(build_dir), "--config", "Release", "--parallel"], **kw)


def ensure_bridge(build_dir: pathlib.Path | None = None, verbose: bool = False) -> str:
    """Make the C ABI library under build_dir the one built from the submodule's HEAD; returns that commit.

    Rebuilds when the library is missing or its `.dsptap_commit` marker is absent or names another commit
    (dsptap_py itself only builds when the library file is missing, so a pin move would otherwise keep a
    stale library under a fresh commit id). Refuses a submodule whose tracked files are modified.
    """
    build_dir = BRIDGE_BUILD if build_dir is None else build_dir
    head = dsptap_head()
    dirty = dsptap_dirty()
    if dirty:
        names = ", ".join(d.split()[-1] for d in dirty[:5])
        raise BridgeError(f"the DspTap submodule at {DSPTAP} has uncommitted changes to tracked files "
                          f"({names}): no commit describes the front end it would build; commit or revert "
                          "them")
    marker = build_dir / BRIDGE_MARKER_NAME
    recorded = marker.read_text(encoding="utf-8").strip() if marker.exists() else None
    if _bridge_library(build_dir) is None or recorded != head:
        if build_dir.exists():
            shutil.rmtree(build_dir)
        _build_bridge(build_dir, verbose=verbose)
        if _bridge_library(build_dir) is None:
            raise BridgeError(f"cmake finished but no dsptap_capi library appeared under {build_dir}")
        marker.write_text(head + "\n", encoding="utf-8")
    return head


def bridge_module():
    """dsptap_py, DspTap's ctypes bridge, over a library built from the submodule's HEAD (`ensure_bridge`)."""
    if "dsptap_py" not in sys.modules:
        ensure_bridge()
    if str(BRIDGE_DIR) not in sys.path:
        sys.path.insert(0, str(BRIDGE_DIR))
    import dsptap_py  # noqa: E402

    return dsptap_py


def to_reference_geometry(g: Geometry):
    """The same values as the reference script's own Geometry dataclass."""
    ref = reference_module()
    return ref.Geometry(sample_rate=g.sample_rate, frame=g.frame, hop=g.hop, fft_size=g.fft_size, bands=g.bands,
                        fmin_hz=g.fmin_hz, fmax_hz=g.fmax_hz, window=g.window, preemphasis=g.preemphasis,
                        log_floor=g.log_floor, log_shift=g.log_shift, log_scale=g.log_scale,
                        pcen=ref.Pcen(**dataclasses.asdict(g.pcen)))


def contract_version() -> int:
    """dsptap_log_mel_contract_version(): the formula-level contract the features were computed under."""
    return int(bridge_module().LogMel.contract_version())


def dsptap_commit() -> str:
    """The submodule commit the C ABI bridge was built from (the lock and every shard header record it).

    HEAD of the submodule, provided the bridge's `.dsptap_commit` marker names that same commit and the
    submodule's tracked files are unmodified; otherwise BridgeError — the value would not describe the
    library that computes the features.
    """
    head = dsptap_head()
    dirty = dsptap_dirty()
    if dirty:
        names = ", ".join(d.split()[-1] for d in dirty[:5])
        raise BridgeError(f"the DspTap submodule has uncommitted changes to tracked files ({names}): no "
                          "commit describes the front end")
    marker = BRIDGE_BUILD / BRIDGE_MARKER_NAME
    recorded = marker.read_text(encoding="utf-8").strip() if marker.exists() else None
    if recorded != head:
        raise BridgeError(f"the DspTap C ABI bridge under {BRIDGE_BUILD} was built at "
                          f"{recorded or 'an unrecorded commit'}, the submodule is at {head}: import "
                          "kws_features (ensure_bridge) to rebuild it before recording a commit")
    return head


def assert_band_support(g: Geometry) -> None:
    """Refuse a geometry that leaves any band without an FFT bin — log_mel.h would zero-fill it silently."""
    ref = reference_module()
    w = ref.mel_weights(g.sample_rate, g.fft_size, g.bands, g.fmin_hz, g.fmax_hz)
    empty = [b for b in range(g.bands) if not np.any(w[b] > 0.0)]
    if empty:
        raise ValueError(f"geometry leaves band(s) {empty} with no FFT bin inside them "
                         f"(bands={g.bands}, fft_size={g.fft_size}, {g.fmin_hz}-{g.fmax_hz} Hz): "
                         "log_mel.h would zero-fill them silently; choose fewer bands or a larger FFT")


class FrontEnd:
    """The shipping front end at one geometry, through the bridge: double in, double out."""

    def __init__(self, g: Geometry):
        if not g.valid():
            raise ValueError(f"invalid log_mel geometry: {g}")
        assert_band_support(g)
        bridge = bridge_module()
        p = g.pcen
        self._g = g
        self._fe = bridge.LogMel(g.sample_rate, g.frame, g.hop, g.fft_size, g.bands, g.fmin_hz, g.fmax_hz,
                                 g.window == "sqrt_hann", g.preemphasis,
                                 log=(g.log_floor, g.log_shift, g.log_scale),
                                 pcen=({"smoother": p.smoother, "alpha": p.alpha, "delta": p.delta,
                                        "power": p.power, "epsilon": p.epsilon} if p.enabled else None))

    @property
    def geometry(self) -> Geometry:
        return self._g

    @property
    def latency(self) -> int:
        return int(self._fe.latency)

    def reset(self) -> None:
        self._fe.reset()

    def extract(self, x: np.ndarray) -> np.ndarray:
        """Features for a whole signal after a reset: (frames, bands) float64, frame t ending at (t+1)*hop - 1."""
        self._fe.reset()
        return self.stream(x)

    def stream(self, x: np.ndarray) -> np.ndarray:
        """Features for the next chunk with the state carried over (a continuously running deployment)."""
        return np.asarray(self._fe.process(np.asarray(x, dtype=np.float64)), dtype=np.float64)


def self_check(g: Geometry) -> dict[str, Any]:
    """The bridge against the numpy oracle at geometry g, both paths, on the reference signal.

    Returns the record the lock carries; raises if either path exceeds SELF_CHECK_TOLERANCE.
    """
    ref = reference_module()
    x = ref.test_signal()
    record: dict[str, Any] = {"geometry": g.to_dict(), "tolerance": SELF_CHECK_TOLERANCE}
    for path in ("log", "pcen"):
        gg = dataclasses.replace(g, pcen=dataclasses.replace(g.pcen, enabled=(path == "pcen")))
        got = FrontEnd(gg).extract(x)
        want = ref.features(x, to_reference_geometry(gg))
        n = min(got.shape[0], want.shape[0])
        if got.shape[1] != want.shape[1] or n == 0:
            raise AssertionError(f"self-check shape mismatch at {gg}: bridge {got.shape}, numpy {want.shape}")
        worst = float(np.abs(got[:n] - want[:n]).max())
        record[f"max_abs_diff_{path}"] = worst
        if not worst < SELF_CHECK_TOLERANCE:
            raise AssertionError(f"front-end self-check failed on the {path} path at {gg}: "
                                 f"max |bridge - numpy| = {worst:.3e} >= {SELF_CHECK_TOLERANCE:.0e}")
    return record


def main(argv: list[str] | None = None) -> int:
    import argparse
    import json

    ap = argparse.ArgumentParser(description="Run the front-end self-check at a geometry (default: the reference).")
    ap.add_argument("--geometry", help="JSON file or string with log_mel_geometry values (manifest recipe.geometry)")
    ap.add_argument("--ensure-bridge", action="store_true",
                    help="only (re)build the C ABI bridge for the submodule's HEAD, with cmake's output "
                         "shown, and print the commit it was built from (the CI job's readable build step)")
    args = ap.parse_args(argv)
    if args.ensure_bridge:
        print(f"dsptap_capi built from DspTap {ensure_bridge(verbose=True)} under {BRIDGE_BUILD}")
        return 0
    g = REFERENCE
    if args.geometry:
        text = pathlib.Path(args.geometry).read_text() if pathlib.Path(args.geometry).exists() else args.geometry
        g = Geometry.from_dict(json.loads(text))
    rec = self_check(g)
    print(json.dumps({"contract_version": contract_version(), "kws_features_version": KWS_FEATURES_VERSION,
                      "dsptap_commit": dsptap_commit(), **rec}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
