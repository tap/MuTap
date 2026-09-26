#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Audible limit of the simulated karaoke loop, by the offline howl criterion.

The test suites (tests/test_afc_decorrelation.cpp) measure each loop's limit
by bisection with the harness's runaway rule. With a frequency shifter in the
loop the ear objects first: program partials recirculating through the
shifter ring well below runaway. This driver measures the AUDIBLE limit the
way the anti-howl PoC's protocol measures a room: it runs
karaoke_ramp_dump (the tests' loop under a 1 dB / 2 s gain ramp after a 30 s
warm-up, the live PEM + FD-Kalman canceller adapting from a cold start) for
every configuration and seed, splits the dump's stems into mono files, and
applies tools/fixtures/howl_criterion.py to the canceller output c:

    howl_criterion.py analyze RUN.wav --channel 0 --program voice.wav
        [--program track.wav] --gain-log RUN.gain.csv --rt60 T30 --dechirp

--rt60 is the T30 of the band-limited cabin fixture at its full 4096 taps,
measured by measure_rir.py's schroeder() (the 1024-tap path in the loop is
its first 21 ms and has too little decay range for a T30). --dechirp is on
for every configuration, as the criterion recommends with a shifter.

Printed per configuration and delay: each seed's audible limit and the
ramp's own runaway gain, both as dB over the dry loop's phase-exact
open-loop MSG (exact_msg_db), and their medians. The "dry" configuration
(no canceller) is the audible open-loop reference.

Build the dump first:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_KARAOKE_DUMP=ON
    cmake --build build --target karaoke_ramp_dump
    python3 tools/notebook/karaoke_audible.py --dump build/tools/notebook/karaoke_ramp_dump

Requires numpy and scipy (the criterion's own requirements).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import pathlib
import statistics
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE.parent / "fixtures"
sys.path.insert(0, str(FIXTURES))
import measure_rir  # noqa: E402

# name -> (dump arguments, whether the track stem is a program)
CONFIGS = {
    "dry": (["--no-canceller"], False),
    "plain": ([], False),
    "shift2": (["--shift-hz", "2"], False),
    "shift5": (["--shift-hz", "5"], False),
    "aux": (["--aux-db", "0"], True),
    "shift2_aux": (["--shift-hz", "2", "--aux-db", "0"], True),
    "shift5_aux": (["--shift-hz", "5", "--aux-db", "0"], True),
}


def cabin_t30(dump: str, work: pathlib.Path) -> float:
    prefix = work / "cabin4096"
    subprocess.run([dump, "--out", str(prefix), "--taps", "4096", "--ir-only"], check=True)
    fs, ir = wavfile.read(str(prefix) + ".ir.wav")
    return float(measure_rir.schroeder(ir.astype(np.float64), fs)["T30"])


def one_run(dump: str, work: pathlib.Path, name: str, delay: int, seed: int, material: str, t30: float) -> dict:
    extra, with_track = CONFIGS[name]
    prefix = work / f"{material}_{name}_d{delay}_s{seed}"
    out = subprocess.run([dump, "--out", str(prefix), "--delay", str(delay), "--seed", str(seed), "--material",
                          material, *extra], check=True, capture_output=True, text=True)
    meta = json.loads(out.stdout)
    fs, x = wavfile.read(str(prefix) + ".wav")
    wavfile.write(str(prefix) + ".voice.wav", fs, x[:, 1].copy())
    cmd = [sys.executable, str(FIXTURES / "howl_criterion.py"), "analyze", str(prefix) + ".wav", "--channel", "0",
           "--program", str(prefix) + ".voice.wav"]
    if with_track:
        wavfile.write(str(prefix) + ".track.wav", fs, x[:, 2].copy())
        cmd += ["--program", str(prefix) + ".track.wav"]
    cmd += ["--gain-log", str(prefix) + ".gain.csv", "--rt60", f"{t30:.6f}", "--dechirp", "--json",
            str(prefix) + ".howl.json"]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    howl = json.loads(pathlib.Path(str(prefix) + ".howl.json").read_text())
    return dict(name=name, delay=delay, seed=seed, material=material, exact=meta["exact_msg_db"],
                max_f=meta["theoretical_msg_db"], runaway=meta["runaway_db"], limit=howl.get("limit_db"),
                warnings=howl.get("warnings", []))


def fmt(v: float | None) -> str:
    return "  none" if v is None else f"{v:+6.2f}"


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True, help="path to the karaoke_ramp_dump executable")
    ap.add_argument("--work", default="karaoke_audible_runs", help="directory for the runs (WAVs, JSON)")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--seeds", default="2,22,42")
    ap.add_argument("--delays", default="480,960")
    ap.add_argument("--material", default="held", choices=["held", "ar"])
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--json", default=None, help="write every run's record here")
    args = ap.parse_args(argv)

    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    t30 = cabin_t30(args.dump, work)
    print(f"band-limited cabin (4096 taps) T30 = {t30:.4f} s  (--rt60)")

    seeds = [int(s) for s in args.seeds.split(",")]
    delays = [int(d) for d in args.delays.split(",")]
    names = args.configs.split(",")
    runs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(one_run, args.dump, work, n, d, s, args.material, t30) for n in names for d in delays
                for s in seeds]
        for f in concurrent.futures.as_completed(futs):
            runs.append(f.result())

    print(f"\nmaterial {args.material}; dB over exact_msg_db (dry loop, phase-exact); "
          "audible = howl criterion, runaway = the ramp's 40 dB rule")
    for d in delays:
        for n in names:
            rs = sorted((r for r in runs if r["name"] == n and r["delay"] == d), key=lambda r: r["seed"])
            aud = [None if r["limit"] is None else r["limit"] - r["exact"] for r in rs]
            run = [None if r["runaway"] is None else r["runaway"] - r["exact"] for r in rs]
            med_a = statistics.median([a for a in aud if a is not None]) if any(a is not None for a in aud) else None
            med_r = statistics.median([a for a in run if a is not None]) if any(a is not None for a in run) else None
            warn = sum(len(r["warnings"]) for r in rs)
            print(f"d={d:4d} {n:11s} audible " + " ".join(fmt(a) for a in aud) + f"  median {fmt(med_a)}"
                  f" | runaway " + " ".join(fmt(a) for a in run) + f"  median {fmt(med_r)}"
                  f" | exact {rs[0]['exact']:+.3f} max|F| {rs[0]['max_f']:+.3f} | warnings {warn}")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(runs, indent=1))


if __name__ == "__main__":
    main()
