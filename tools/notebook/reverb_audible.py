#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright 2026 MuTap contributors
"""Audible limit of the simulated loop with a Dattorro plate behind the canceller.

The reverb suites (tests/test_reverb_stage*.cpp) measure runaway limits by
bisection. This driver measures the AUDIBLE limit the way the karaoke one
(karaoke_audible.py) does, with the reverb in the chain: it runs
karaoke_ramp_dump with --reverb (the suites' forward path, tests/support/
reverb_rig.h: tap::mu::reverb_mix over a vendored plate, the shift inside the
stage on the library's frequency_shifter) under the protocol's ramp (30 s
warm-up 20 dB under exact_msg_db, then 1 dB per 2 s, the live PEM + FD-Kalman
canceller adapting from a cold start), and applies tools/fixtures/
howl_criterion.py by PROTOCOL.md section 7.3's table:

    dry, canceller          c             --rt60 ROOM
    + shift                 c             --rt60 ROOM --dechirp
    + reverb only           chain output  --rt60 ROOM --chain-rt60 T     (c as the cross-check)
    + shift + reverb        c             --rt60 ROOM --chain-rt60 T --dechirp

c is the canceller output (the dump's channel 0), the chain output its
channel 3 (after the shift and the reverb, before the gain). ROOM is the
band-limited cabin fixture's T30 at 4096 taps (karaoke_audible.py's
cabin_t30); T is the plate's own T30 for the row's decay, damping and return,
from the dump's --reverb-ir-only impulse response through measure_rir.py's
schroeder(). Analysing c with the plate's T30 in the hold is what counts a
reverb tail recirculating through the shifter: it is part of c's loop.

Configurations (the product-chain candidates; --plate picks the plate):
    dry       no canceller (the audible open-loop reference)
    plain     the canceller alone
    shift2/5  the canceller + a 2 / 5 Hz shift (library shifter, in the stage)
    rev_D_W   + the plate at decay D, wet W
    bus2/5_D_W  + shift on the whole bus, then the plate (the chain's slot order)
    dry2/5_D_W  + shifted_dry_mix: only the dry path shifted, the plate fed the unshifted bus
for D in --decays and W in --wets.

Printed per configuration and delay: each seed's audible limit and the
ramp's runaway gain, both in dB over the dry loop's exact_msg_db, and their
medians; for reverb-only rows also the cross-check on c. --merge prints the
tables from earlier --json records.

--control adds, for the shift-only rows, the same analysis of c with each
plate's T30 in the criterion's hold (--chain-rt60), the flags the product
rows get: without it the shift-only and the shift + plate rows are judged
by different holds.

Disk and resumption: a run's dump is ~90 MB of float WAV (two minutes, four
channels) and its mono splits as much again, so each run's WAVs are deleted
once the criterion has read them (--keep-wav keeps them), and each run's
record is written beside it (PREFIX.record.json) as soon as it finishes; a
run whose record exists is not run again. --recover rebuilds the records of
runs that finished their analysis without one (the criterion's JSON and the
gain log are kept): exact_msg_db from a one-block dump with the run's
arguments, the runaway gain from the gain log by the dump's own rule (the
log stops 2 s after the runaway block, unless the ramp reached its end).

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_KARAOKE_DUMP=ON
    cmake --build build --target karaoke_ramp_dump
    python3 tools/notebook/reverb_audible.py --dump build/tools/notebook/karaoke_ramp_dump --jobs 4

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
import time

import numpy as np
from scipy.io import wavfile

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE.parent / "fixtures"
sys.path.insert(0, str(FIXTURES))
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True
import karaoke_audible  # noqa: E402  (cabin_t30, plain_pass_limit, fmt)
import measure_rir  # noqa: E402


def configs(decays: list[float], wets: list[float], plate: str) -> dict:
    """name -> (dump arguments, kind); kind picks section 7.3's row."""
    out = {
        "dry": (["--no-canceller"], "plain"),
        "plain": ([], "plain"),
        "shift2": (["--lib-shift", "--shift-hz", "2"], "shift"),
        "shift5": (["--lib-shift", "--shift-hz", "5"], "shift"),
    }
    for d in decays:
        for w in wets:
            rv = ["--reverb", plate, "--decay", f"{d:g}", "--wet", f"{w:g}"]
            out[f"rev_{d:g}_{w:g}"] = (rv, "reverb")
            for hz in (2, 5):
                out[f"bus{hz}_{d:g}_{w:g}"] = (rv + ["--shift-hz", str(hz), "--topology", "bus"], "product")
                out[f"dry{hz}_{d:g}_{w:g}"] = (rv + ["--shift-hz", str(hz), "--topology", "dry"], "product")
    return out


def plate_t30(dump: str, work: pathlib.Path, plate: str, decay: float, damping: float, ret: str) -> float:
    prefix = work / f"plate_{plate}_d{decay:g}_m{damping:g}"
    subprocess.run([dump, "--out", str(prefix), "--reverb", plate, "--decay", f"{decay:g}", "--damping",
                    f"{damping:g}", "--reverb-ir-only"], check=True)
    fs, ir = wavfile.read(str(prefix) + ".reverb.wav")
    ir = ir.astype(np.float64)
    mono = ir[:, 0] if ret == "L" else 0.5 * (ir[:, 0] + ir[:, 1])
    return float(measure_rir.schroeder(mono, fs)["T30"])


def criterion(prefix: pathlib.Path, wav: str, programs: list[str], room_t30: float, chain_t30: float | None,
              dechirp: bool, tag: str) -> dict:
    cmd = [sys.executable, str(FIXTURES / "howl_criterion.py"), "analyze", wav, "--channel", "0"]
    for p in programs:
        cmd += ["--program", p]
    cmd += ["--gain-log", str(prefix) + ".gain.csv", "--rt60", f"{room_t30:.6f}"]
    if chain_t30 is not None:
        cmd += ["--chain-rt60", f"{chain_t30:.6f}"]
    if dechirp:
        cmd += ["--dechirp"]
    out = str(prefix) + f".{tag}.howl.json"
    cmd += ["--json", out]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    return json.loads(pathlib.Path(out).read_text())


# The dump's ramp defaults (karaoke_ramp_dump.cpp): the gain log has one row per block.
FS = 48000
BLOCK = 64
WARMUP_S, START_BELOW, MAX_OVER, RATE, TAIL_S = 30.0, 20.0, 40.0, 0.5, 2.0


def run_args(name: str, spec: tuple, delay: int, seed: int) -> list:
    return ["--delay", str(delay), "--seed", str(seed), "--material", "held", *spec[0]]


def prefix_of(work: pathlib.Path, name: str, delay: int, seed: int) -> pathlib.Path:
    return work / f"held_{name}_d{delay}_s{seed}"


def runaway_from_gain_log(csv: str):
    """The dump stops tail_s after the runaway block; a log that reaches max_blocks never ran away."""
    rows = np.loadtxt(csv, delimiter=",", skiprows=1)
    max_blocks = int(WARMUP_S * FS / BLOCK) + int((START_BELOW + MAX_OVER) / RATE * FS / BLOCK) + 1
    tail = int(TAIL_S * FS / BLOCK)
    return None if rows.shape[0] >= max_blocks else float(rows[rows.shape[0] - 1 - tail, 1])


def one_run(dump: str, work: pathlib.Path, name: str, spec: tuple, delay: int, seed: int, room_t30: float,
            chain_t30: dict, keep_wav: bool = False, control: bool = False) -> dict:
    extra, kind = spec
    t0 = time.perf_counter()
    prefix = prefix_of(work, name, delay, seed)
    record = pathlib.Path(str(prefix) + ".record.json")
    if record.exists():
        return json.loads(record.read_text())
    out = subprocess.run([dump, "--out", str(prefix), *run_args(name, spec, delay, seed)], check=True,
                         capture_output=True, text=True)
    meta = json.loads(out.stdout)
    fs, x = wavfile.read(str(prefix) + ".wav")
    voice = str(prefix) + ".voice.wav"
    wavfile.write(voice, fs, x[:, 1].copy())
    t_chain = None
    if meta.get("reverb", "none") != "none":
        t_chain = chain_t30[(meta["reverb"], float(meta["decay"]), float(meta["damping"]), meta["return"])]
    rec = dict(name=name, kind=kind, delay=delay, seed=seed, exact=meta["exact_msg_db"],
               max_f=meta["theoretical_msg_db"], runaway=meta["runaway_db"], chain_t30=t_chain)
    c_wav = str(prefix) + ".c.wav"
    wavfile.write(c_wav, fs, x[:, 0].copy())
    if kind == "reverb":
        chain_wav = str(prefix) + ".chain.wav"
        wavfile.write(chain_wav, fs, x[:, 3].copy())
        h = criterion(prefix, chain_wav, [voice], room_t30, t_chain, False, "chain")
        hc = criterion(prefix, c_wav, [voice], room_t30, t_chain, False, "c")
        rec.update(limit=h.get("limit_db"), limit_c=hc.get("limit_db"), warnings=h.get("warnings", []),
                   warnings_c=hc.get("warnings", []))
    else:
        h = criterion(prefix, c_wav, [voice], room_t30, t_chain, kind in ("shift", "product"), "c")
        rec.update(limit=h.get("limit_db"), limit_plain=karaoke_audible.plain_pass_limit(h),
                   warnings=h.get("warnings", []))
        if kind == "shift" and control:
            # The control: the same c analysed with each plate T30 in the hold, as the
            # product rows are, so shift alone and shift + plate meet one criterion.
            rec["limit_ctl"] = {}
            for t in sorted(set(chain_t30.values())):
                hc = criterion(prefix, c_wav, [voice], room_t30, t, True, f"ctl{t:.3f}")
                rec["limit_ctl"][f"{t:.4f}"] = hc.get("limit_db")
    rec["seconds"] = time.perf_counter() - t0
    if not keep_wav:
        for w in pathlib.Path(work).glob(prefix.name + ".*wav"):
            if not w.name.endswith(".ir.wav"):
                w.unlink()
    record.write_text(json.dumps(rec))
    return rec


def recover(dump: str, work: pathlib.Path, name: str, spec: tuple, delay: int, seed: int, chain_t30: dict):
    """Rebuild the record of a run whose analysis finished without one (see the module docstring)."""
    extra, kind = spec
    prefix = prefix_of(work, name, delay, seed)
    record = pathlib.Path(str(prefix) + ".record.json")
    c_json = pathlib.Path(str(prefix) + ".c.howl.json")
    if record.exists() or not c_json.exists() or (kind == "reverb" and
                                                  not pathlib.Path(str(prefix) + ".chain.howl.json").exists()):
        return None
    meta_prefix = work / f"meta_{name}_d{delay}_s{seed}"
    out = subprocess.run([dump, "--out", str(meta_prefix), *run_args(name, spec, delay, seed), "--warmup", "0",
                          "--start-below", "0", "--max-over", "0", "--tail", "0"], check=True, capture_output=True,
                         text=True)
    meta = json.loads(out.stdout)
    for f in pathlib.Path(work).glob(meta_prefix.name + ".*"):
        f.unlink()
    t_chain = None
    if meta.get("reverb", "none") != "none":
        t_chain = chain_t30[(meta["reverb"], float(meta["decay"]), float(meta["damping"]), meta["return"])]
    rec = dict(name=name, kind=kind, delay=delay, seed=seed, exact=meta["exact_msg_db"],
               max_f=meta["theoretical_msg_db"], runaway=runaway_from_gain_log(str(prefix) + ".gain.csv"),
               chain_t30=t_chain, recovered=True)
    hc = json.loads(c_json.read_text())
    if kind == "reverb":
        h = json.loads(pathlib.Path(str(prefix) + ".chain.howl.json").read_text())
        rec.update(limit=h.get("limit_db"), limit_c=hc.get("limit_db"), warnings=h.get("warnings", []),
                   warnings_c=hc.get("warnings", []))
    else:
        rec.update(limit=hc.get("limit_db"), limit_plain=karaoke_audible.plain_pass_limit(hc),
                   warnings=hc.get("warnings", []))
    record.write_text(json.dumps(rec))
    return rec


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def print_table(runs: list, order: list) -> None:
    fmt = karaoke_audible.fmt
    print("\nheld note, cabin (band-limited); dB over exact_msg_db (dry loop, phase-exact); audible = howl criterion "
          "(PROTOCOL 7.3), runaway = the ramp's 40 dB rule; per seed in seed order")
    for d in sorted({r["delay"] for r in runs}):
        for n in order:
            rs = sorted((r for r in runs if r["name"] == n and r["delay"] == d), key=lambda r: r["seed"])
            if not rs:
                continue
            aud = [None if r["limit"] is None else r["limit"] - r["exact"] for r in rs]
            run = [None if r["runaway"] is None else r["runaway"] - r["exact"] for r in rs]
            warn = sum(len(r["warnings"]) for r in rs)
            t = rs[0].get("chain_t30")
            print(f"d={d:4d} {n:13s} audible " + " ".join(fmt(a) for a in aud) + f"  median {fmt(med(aud))} | "
                  "runaway " + " ".join(fmt(a) for a in run) + f"  median {fmt(med(run))} | warnings {warn}"
                  + ("" if t is None else f" | chain T30 {t:.3f} s"))
            if any("limit_ctl" in r for r in rs):
                for t in sorted({t for r in rs for t in r.get("limit_ctl", {})}):
                    cc = [None if r.get("limit_ctl", {}).get(t) is None else r["limit_ctl"][t] - r["exact"]
                          for r in rs]
                    print(f"{'':20s}(control, c with --chain-rt60 {float(t):.3f}: " + " ".join(fmt(a) for a in cc)
                          + f"  median {fmt(med(cc))})")
            if rs[0]["kind"] == "reverb":
                cc = [None if r.get("limit_c") is None else r["limit_c"] - r["exact"] for r in rs]
                wc = sum(len(r.get("warnings_c", [])) for r in rs)
                print(f"{'':20s}(c cross-check: " + " ".join(fmt(a) for a in cc)
                      + f"  median {fmt(med(cc))}, warnings {wc})")
            # audible - runaway per seed, where both exist
            gap = [a - b for a, b in zip(aud, run) if a is not None and b is not None]
            if gap:
                print(f"{'':20s}(audible - runaway per seed: " + " ".join(fmt(g) for g in gap)
                      + f"  median {fmt(statistics.median(gap))})")

    # The two shift topologies against each other: per seed, dry-only shift - whole-bus shift.
    print("\ndry-only shift - whole-bus shift, per seed (same seed, delay, shift, decay and wet), dB")
    pairs = 0
    higher = 0
    higher_run = 0
    for d in sorted({r["delay"] for r in runs}):
        for n in order:
            if not n.startswith("bus"):
                continue
            other = "dry" + n[3:]
            b = {r["seed"]: r for r in runs if r["name"] == n and r["delay"] == d}
            o = {r["seed"]: r for r in runs if r["name"] == other and r["delay"] == d}
            seeds = sorted(set(b) & set(o))
            if not seeds:
                continue
            row = {}
            for key in ("limit", "runaway"):
                row[key] = [o[s][key] - b[s][key] for s in seeds if o[s][key] is not None and b[s][key] is not None]
            print(f"d={d:4d} {other:13s} - {n:13s} audible " + " ".join(fmt(x) for x in row["limit"])
                  + f"  median {fmt(med(row['limit']))} | runaway " + " ".join(fmt(x) for x in row["runaway"])
                  + f"  median {fmt(med(row['runaway']))}")
            if row["limit"] and row["runaway"]:
                pairs += 1
                higher += med(row["limit"]) > 0
                higher_run += med(row["runaway"]) > 0
    print(f"dry-only shift higher (median over seeds): audible in {higher} of {pairs} pairs, runaway in "
          f"{higher_run} of {pairs}")


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", help="path to the karaoke_ramp_dump executable")
    ap.add_argument("--work", default="reverb_audible_runs", help="directory for the runs (WAVs, JSON)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--seeds", default="2,22,42,62,82")
    ap.add_argument("--delays", default="480,960")
    ap.add_argument("--plate", default="shipped", choices=["shipped", "paper"])
    ap.add_argument("--decays", default="0.5,0.7")
    ap.add_argument("--wets", default="0.15,0.3")
    ap.add_argument("--configs", default=None, help="comma-separated subset of the configuration names")
    ap.add_argument("--json", default=None, help="write every run's record here")
    ap.add_argument("--merge", nargs="+", default=None, metavar="JSON", help="print the tables from earlier records")
    ap.add_argument("--keep-wav", action="store_true", help="keep each run's WAVs (about 180 MB a run)")
    ap.add_argument("--control", action="store_true",
                    help="also analyse the shift-only rows' c with each plate T30 in the hold (--chain-rt60)")
    ap.add_argument("--recover", action="store_true",
                    help="rebuild missing records from the work directory's criterion JSON and gain logs, then exit")
    args = ap.parse_args(argv)

    decays = [float(x) for x in args.decays.split(",")]
    wets = [float(x) for x in args.wets.split(",")]
    cfgs = configs(decays, wets, args.plate)
    order = list(cfgs)
    if args.merge:
        by_key = {}
        for path in args.merge:
            for r in json.loads(pathlib.Path(path).read_text()):
                by_key[(r["name"], r["delay"], r["seed"])] = r
        print_table(list(by_key.values()), order)
        return
    if not args.dump:
        ap.error("--dump is required unless --merge is given")
    names = args.configs.split(",") if args.configs else order

    work = pathlib.Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    t0 = time.perf_counter()
    room_t30 = karaoke_audible.cabin_t30(args.dump, work)
    print(f"band-limited cabin (4096 taps) T30 = {room_t30:.4f} s  (--rt60)")
    chain_t30 = {}
    for d in decays:
        key = (args.plate, d, 0.0005, "L")
        chain_t30[key] = plate_t30(args.dump, work, args.plate, d, 0.0005, "L")
        print(f"{args.plate} plate, decay {d:g}, damping 0.0005, L: T30 = {chain_t30[key]:.4f} s  (--chain-rt60)")

    seeds = [int(s) for s in args.seeds.split(",")]
    delays = [int(d) for d in args.delays.split(",")]
    if args.recover:
        got = [recover(args.dump, work, n, cfgs[n], d, s, chain_t30) for n in names for d in delays for s in seeds]
        print(f"recovered {sum(r is not None for r in got)} records")
        return
    runs = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(one_run, args.dump, work, n, cfgs[n], d, s, room_t30, chain_t30, args.keep_wav,
                            args.control)
                for n in names for d in delays for s in seeds]
        for f in concurrent.futures.as_completed(futs):
            runs.append(f.result())
    print_table(runs, order)
    timed = [r for r in runs if not r.get("recovered")]
    print(f"\n{len(runs)} runs ({len(runs) - len(timed)} from recovered records) on {args.jobs} jobs: wall "
          f"{time.perf_counter() - t0:.1f} s, per-run sum of this invocation's and earlier timed runs "
          f"{sum(r['seconds'] for r in timed):.1f} s")
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(runs, indent=1))


if __name__ == "__main__":
    main()
