#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""ML AEC benchmark: MuTap's shipped engines vs DTLN-aec, one shared meter.

Builds what it needs (C ABI + scenario dump; cmake), dumps the AEC
double-talk protocol's signals for a grid of (material, seed) scenarios,
runs every system on identical signals, and reports the rig's metrics
(plus delay compensation and near-end preservation — metrics.py).

Usage:
    python3 tools/ml/run_benchmark.py --dtln-dir /path/to/dtln-aec/pretrained_models \
        [--build-dir build-ml] [--workdir /tmp/aec-bench] [--materials ar,music] \
        [--seeds 2,12,22] [--dtln-sizes 128,512] [--json results.json]

Without --dtln-dir only the MuTap systems run (no network needed). The DTLN
models are MIT-licensed; clone https://github.com/breizhn/DTLN-aec for them.
The signals are treated as 16 kHz audio for DTLN (the rig is
sample-rate-agnostic; its geometry — block 64, 1024-tap room — reads
naturally at 16 kHz: 4 ms blocks, a 64 ms echo path).
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import metrics  # noqa: E402
import mutap_ffi  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BLOCK = 64
PARTITIONS = 16
SAMPLE_RATE = 16000.0


def build(build_dir: pathlib.Path) -> None:
    subprocess.run(
        ["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DMUTAP_BUILD_CAPI=ON",
         "-DMUTAP_BUILD_ML_TOOLS=ON", "-DMUTAP_BUILD_TESTS=OFF"],
        check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build_dir), "--target",
                    "mutap_capi", "aec_scenario_dump"], check=True, capture_output=True)


def dump_scenario(build_dir: pathlib.Path, workdir: pathlib.Path,
                  material: str, seed: int, room: str) -> pathlib.Path:
    out = workdir / f"{material}-s{seed}-{room}"
    if not (out / "manifest.json").exists():
        out.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(build_dir / "tools/ml/aec_scenario_dump"),
                        str(out), material, str(seed), room], check=True)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=str(REPO_ROOT / "build-ml"))
    ap.add_argument("--workdir", default="/tmp/aec-bench")
    ap.add_argument("--dtln-dir", default=None,
                    help="directory with dtln_aec_{N}_{1,2}.tflite (omit to skip DTLN)")
    ap.add_argument("--dtln-sizes", default="128,512")
    ap.add_argument("--materials", default="ar,music")
    ap.add_argument("--seeds", default="2,12,22")
    ap.add_argument("--room", default="studio")
    ap.add_argument("--json", default=None, help="write full results as JSON")
    args = ap.parse_args()

    build_dir = pathlib.Path(args.build_dir)
    workdir = pathlib.Path(args.workdir)
    print(f"building C ABI + scenario dump in {build_dir} ...", flush=True)
    build(build_dir)
    lib = mutap_ffi.load_library(build_dir)

    def systems():
        yield ("mutap-kalman-linear",
               lambda: mutap_ffi.KalmanCanceller(lib, BLOCK, PARTITIONS))
        yield ("mutap-chain",
               lambda: mutap_ffi.AecChain(lib, BLOCK, PARTITIONS, SAMPLE_RATE))
        if args.dtln_dir:
            import dtln_aec
            for size in args.dtln_sizes.split(","):
                prefix = str(pathlib.Path(args.dtln_dir) / f"dtln_aec_{size.strip()}")
                yield (f"dtln-aec-{size.strip()}", lambda p=prefix: dtln_aec.DtlnAec(p))

    results: list[dict] = []
    for material in args.materials.split(","):
        for seed in (int(s) for s in args.seeds.split(",")):
            folder = dump_scenario(build_dir, workdir, material.strip(), seed, args.room)
            scn = metrics.Scenario.load(folder)
            for name, make in systems():
                e = make().process(scn.x, scn.y)
                m = metrics.measure(scn, e)
                results.append({"material": material.strip(), "seed": seed,
                                "room": args.room, "system": name, **m.as_dict()})
                print(f"  {material:>6} seed {seed:2d} {name:>20}: "
                      f"stERLE {m.st_erle_db:6.1f}  dtSUP {m.dt_suppression_db:6.1f}  "
                      f"recERLE {m.rec_erle_db:6.1f}  neSDR {m.ne_sdr_db:6.1f}  "
                      f"(delay {m.delay_samples})", flush=True)

    # Median-over-seeds summary table per (material, system).
    import statistics
    keys = sorted({(r["material"], r["system"]) for r in results})
    print("\nmedians over seeds "
          "(stERLE / dtSUP / recERLE / neSDR dB):")
    for material, system in keys:
        rows = [r for r in results if r["material"] == material and r["system"] == system]
        med = {k: statistics.median(r[k] for r in rows)
               for k in ("st_erle_db", "dt_suppression_db", "rec_erle_db", "ne_sdr_db")}
        print(f"  {material:>6} {system:>20}: {med['st_erle_db']:6.1f} / "
              f"{med['dt_suppression_db']:6.1f} / {med['rec_erle_db']:6.1f} / {med['ne_sdr_db']:6.1f}")

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(results, indent=2))
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
