# MuTap benchmarks

Host-side performance baselines for the AEC hot path (the SampleRateTap
`bench/` pattern). These exist so that **every optimization has a
measured before/after on the same machine** — absolute numbers below are
reference points from one container, not gates. Wall-clock benches are
never asserted in CI; the **deterministic instruction-count ratchet**
(`icount/`, below) is the per-target gate.

## Build and run

```sh
cmake -B build -DMUTAP_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mutap_bench
./build/bench/mutap_bench --benchmark_min_time=0.3s \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

Before/after workflow: run with `--benchmark_out=before.json` on the
unmodified tree, apply the change, run again with `after.json`, and
diff with Google Benchmark's `tools/compare.py before.json after.json`
(or just compare the median rows — the corpus and warmup are
deterministic, so per-block work is identical across runs).

## What is measured

Four layers, each at both certified geometries (block 256; 8 partitions
at 48 kHz, 4 at 16 kHz) in double and float32:

- **fdkf** — `partitioned_fdkf::process_block` at the certified
  canceller config, adaptation live, warmed to steady state (the
  gradient constraint runs every block; that is the honest cost).
- **suppressor** — `residual_suppressor::process_block` alone.
- **shadow** — the rescue's 2-partition comparator canceller alone.
- **chain** — the full certified `aec_chain` (canceller + suppressor +
  both rescue triggers; float32 also carries the narrowband guard):
  what a deployment pays per block.

Inputs cycle through a precomputed stationary noise-echo corpus, so no
rescue trigger or guard ever fires inside the timed loop. Items
processed are samples: Google Benchmark's `items_per_second / fs` is
the x-realtime figure.

## Scalar baselines (reference container, 2.8 GHz x86, GCC Release: -O3 -DNDEBUG, medians of 5, idle machine)

| layer | 48 kHz f64 | 48 kHz f32 | 16 kHz f64 | 16 kHz f32 |
|---|---|---|---|---|
| fdkf       | 39.3 µs | 36.9 µs | 22.7 µs | 21.0 µs |
| suppressor | 78.9 µs | 77.1 µs | 82.0 µs | 78.2 µs |
| shadow     | 14.2 µs | 13.3 µs | 14.7 µs | 14.2 µs |
| **chain**  | **138.4 µs** | **131.3 µs** | **119.5 µs** | **114.4 µs** |

Per-block real-time budget: 5.33 ms at 48 kHz, 16 ms at 16 kHz — the
full chain costs **2.6 % / 0.75 %** of budget (f64) on this host.

**Record the load average with every run.** The first committed table
was measured while a test suite ran concurrently and read ~35 %
pessimistic across the board; the numbers above are from an idle
machine (load < 0.4). Only same-machine, same-load comparisons mean
anything.

Two findings the table pins down for the optimization work ahead:

1. **The suppressor dominates, not the Kalman core** — ~57 % of the
   chain at 48 kHz, and its cost is block-size-bound, not rate-bound
   (identical at both rates: the analysis machinery scales with the
   2048-sample analysis window, which is the same at block 256
   everywhere). It is optimization target #1.
2. **Scalar float32 buys only ~5–10 % on x86** — same-width scalar
   ALUs. The float32 payoff is the vector width on M55 Helium and
   Hexagon HVX, which is exactly why the parity gates
   (`tests/test_float32.cpp`) were landed before this harness.

## Per-stage suppressor profile (48 kHz f64, hand-inlined instrumentation)

| stage | µs | share |
|---|---|---|
| window slides + buffer builds | 5.7 | 7 % |
| 3 forward analysis FFTs (2048-pt) | 20.3 | 26 % |
| per-bin estimator/gain pass | 19.6 | 25 % |
| gain-constraint FFT pair + causal cut | 17.9 | 23 % |
| gain application + min-stats | 4.3 | 6 % |
| output inverse FFT | 9.6 | 12 % |

Six 2048-point transforms per 256-sample block are ~62 % of the
suppressor: the vectorization phase should hit the FFT and the two
O(bins) passes first.

## Measured non-wins (recorded so they are not re-litigated)

- **Gain-constraint decimation** (skip the constraint FFT pair while
  the smoothed gains stay within a relative tolerance of the last
  constrained set): behaviorally clean at every tolerance tried
  (0.01–0.1), but the skip **never engages on active signal** — with
  fresh data each block the estimator-variance wiggle exceeds 1 % per
  block at some bin essentially always (measured 8000/8000 rebuilds).
  It would only pay on an idle channel, which no battery row or bench
  measures. Rejected.
- **FTZ/DAZ (denormal flushing)**: no measurable effect on host at
  either the converged (deep-residual) or active operating point, in
  either precision. The smoothed estimator floors sit above the
  denormal range in practice.

## Instruction-count ratchet (`icount/`) — the per-target gate

Wall-clock is noise on shared runners and meaningless under emulation, so
the actual regression gate for the embedded targets counts **executed
guest instructions** under QEMU's TCG plugin — a deterministic, noise-free
number. The `icount-ratchet` CI job gates every push two-sided at ±3 %:
a regression fails, and an *improvement* beyond tolerance also fails (so a
stale, too-high baseline can never let a future regression hide in the
slack — the winning commit must re-record).

Scenarios mirror the wall-clock layers — `fdkf`, `suppressor`, `shadow`,
`chain` — plus `nn_suppressor` (the learned post engine at its two trained
geometries, hop 256 at 48 kHz and hop 64 at 16 kHz) at both certified
geometries (`_48k`, `_16k`), all **float32**
(the deployment precision; double is soft-float on the M55 and not the
optimization target — the float32 parity gates in
[`tests/test_float32.cpp`](../tests/test_float32.cpp) are the correctness
oracle). One bare-metal binary per scenario (no argv on the target;
`MUTAP_SC_LAYER`/`MUTAP_SC_RATE` select at compile time), each running a
warmed, allocation-free timed loop over a self-contained deterministic
corpus so the count is stable across toolchains and re-runs.

Targets: **m55** (`qemu-system-arm -M mps3-an547`, semihosting — Helium
MVE is the float32 vector story) and **hexagon** (`qemu-hexagon`,
user-mode — HVX). Baselines live in [`baselines.json`](baselines.json),
one dict per target.

Run one target locally (needs the cross toolchain + QEMU + the plugin):

```sh
# build the plugin once, against a qemu-plugin.h matching your QEMU 8.2.x
gcc -shared -fPIC $(pkg-config --cflags glib-2.0) -I<hdr-dir> \
    -o /tmp/libinsncount.so tools/qemu_insn_plugin/insn_count.c

cmake -B build-m55 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DMUTAP_BUILD_TESTS=OFF -DMUTAP_BUILD_ICOUNT_BENCH=ON
cmake --build build-m55 -j

python3 scripts/icount.py --target m55 \
    --build-dir build-m55 --plugin /tmp/libinsncount.so
```

Targets: `m55` (MPS3 AN547, CMSIS Helium FFT), `m33` (MPS2+ AN505 — the
Raspberry Pi Pico 2 W class, the split-radix FFT engine, no MVE) and `hexagon`.

**Seeding / re-recording:** a new target starts with an empty dict, so the
job reports each scenario's count and fails with `NO BASELINE`. Capture
those counts by running the same command with `--update` in the target's
environment (or the CI job's) and commit `bench/baselines.json`. After an
intentional optimization, re-record the same way — the two-sided gate
requires it.

### Recorded baselines and updates

Every seeding or `--update` commit adds one row per (key, scenario) it
touched, with the run whose log the counts were taken from and the
toolchain/QEMU pair, because the counts are only comparable within one such
pair. `before` is the committed baseline the row replaced (`—` for a seed);
`main SHA` is the commit on `main` whose run measured the numbers, or
**pending** when the numbers had to be taken from the PR's own run because
they cannot exist on `main` before the change does (the routing-change case;
the cell is filled by the follow-up that pins the squash). Entries before
this table was started (the 2026-07 seeds, the M2 m33 seeds, the Hexagon
`nn_suppressor` seeds from tap/MuTap#45) are recorded in their commit
messages only.

**2026-09-23 — DspTap ae0c027 (Stage 2b, tap/DspTap#31), tap/MuTap#54.**
Reason: DspTap ae0c027 routes the float FFT to the C++20 split-radix port;
outputs are byte-identical (fingerprints identical on every leg) but the port
compiles to fewer instructions than the vendored C on GCC 13.2 (Cortex-M) and
Hexagon clang; DspTap re-recorded its own baselines in #31 for the same
reason. Improvement, re-recorded per DspTap D11 so the gate stays tight.
Counts taken from the ratchet job of the PR's `pull_request` run
[35866422618](https://github.com/tap/MuTap/actions/runs/35866422618)
(head `2571c24`); the `push` run
[35866417101](https://github.com/tap/MuTap/actions/runs/35866417101) and the
two runs of the PR's first head `b01c4e4` printed the same 30 counts. The m55
rows move by tens of instructions (CMSIS is untouched; the FFT wrapper lost
two unused workspace vectors, as DspTap recorded for its own m55 key) and are
re-recorded with the others so all three keys sit at +0.00 % on one run; the
m55 and m33 `nn_suppressor` counts were already −0.10 … −0.19 % below their
committed values on the previous `main` run (35804748507, pin `b08f6c6`) and
are brought to the measured numbers by the same `--update`. Toolchain/QEMU: m33 and m55
`arm-none-eabi-gcc 13.2.1 (15:13.2.rel1-2)`, `qemu-system-arm 8.2.2
(1:8.2.2+ds-0ubuntu1.18)`; hexagon
`clang+llvm-19.1.5-cross-hexagon-unknown-linux-musl`, `qemu-hexagon` built
from `qemu-8.2.2.tar.xz` with `--enable-plugins` (the job's pinned source).
`main SHA`: `74840f1` (the #54 squash). Cross-check: the m33 and m55
workloads rebuilt and counted locally on the same toolchain/QEMU pair
(`15:13.2.rel1-2`, `1:8.2.2+ds-0ubuntu1.18`, the plugin built against the
job's pinned `qemu-plugin.h`) reproduce all 20 committed counts exactly;
hexagon has no local rig and is confirmed by the PR's next ratchet run.

| key | scenario | before | after | delta |
|---|---|---:|---:|---:|
| m55 | chain_16k | 360,481,402 | 360,469,416 | −0.00 % |
| m55 | chain_48k | 417,303,122 | 417,305,870 | +0.00 % |
| m55 | fdkf_16k | 80,854,232 | 80,854,249 | +0.00 % |
| m55 | fdkf_48k | 139,974,811 | 139,974,824 | +0.00 % |
| m55 | nn_suppressor_16k | 572,321,325 | 571,653,908 | −0.12 % |
| m55 | nn_suppressor_48k | 202,455,391 | 202,250,798 | −0.10 % |
| m55 | shadow_16k | 51,274,139 | 51,274,158 | +0.00 % |
| m55 | shadow_48k | 51,279,103 | 51,279,122 | +0.00 % |
| m55 | suppressor_16k | 228,296,904 | 228,296,924 | +0.00 % |
| m55 | suppressor_48k | 224,696,582 | 224,696,602 | +0.00 % |
| m33 | chain_16k | 707,703,396 | 699,424,715 | −1.17 % |
| m33 | chain_48k | 832,971,429 | 823,396,754 | −1.15 % |
| m33 | fdkf_16k | 178,361,160 | 173,967,046 | −2.46 % |
| m33 | fdkf_48k | 306,029,768 | 299,830,854 | −2.03 % |
| m33 | nn_suppressor_16k | 1,036,433,821 | 1,034,777,566 | −0.16 % |
| m33 | nn_suppressor_48k | 375,501,198 | 374,743,684 | −0.20 % |
| m33 | shadow_16k | 114,513,960 | 111,021,150 | −3.05 % |
| m33 | shadow_48k | 114,515,211 | 111,022,401 | −3.05 % |
| m33 | suppressor_16k | 435,040,766 | 430,123,690 | −1.13 % |
| m33 | suppressor_48k | 431,364,983 | 426,448,046 | −1.14 % |
| hexagon | chain_16k | 403,151,374 | 402,079,750 | −0.27 % |
| hexagon | chain_48k | 470,317,154 | 468,543,290 | −0.38 % |
| hexagon | fdkf_16k | 92,600,036 | 91,898,877 | −0.76 % |
| hexagon | fdkf_48k | 162,071,335 | 160,681,664 | −0.86 % |
| hexagon | nn_suppressor_16k | 465,537,141 | 458,552,230 | −1.50 % |
| hexagon | nn_suppressor_48k | 154,160,506 | 152,482,121 | −1.09 % |
| hexagon | shadow_16k | 57,863,067 | 57,506,164 | −0.62 % |
| hexagon | shadow_48k | 57,863,050 | 57,506,147 | −0.62 % |
| hexagon | suppressor_16k | 255,190,544 | 255,177,206 | −0.01 % |
| hexagon | suppressor_48k | 251,521,827 | 251,494,761 | −0.01 % |

The two m33 `shadow` rows crossed the −3 % band, which is what turned the
job red on #54 and forced this record; the DspTap FFT plan had predicted
0 % for this bump (same instruction stream as the C), and this table is the
measured correction to that prediction.

**2026-09-23 — DspTap 0db95b6 (Stage 6 + Stage 4 + D6, tap/DspTap#34 / #35 / #36), tap/MuTap#58: no re-record.**
Stage 4 made the engine a template parameter and added the ABI tag; neither
changes an instruction of any transform, and MuTap's size gate adds one
predicate per constructor; D6 changed only comments under DspTap's
`include/`. Measured against the committed baselines on the PR's
`pull_request` run
[35918701246](https://github.com/tap/MuTap/actions/runs/35918701246)
(head `82ebabc`, pin `6f6f77f`; m33 and m55 reproduced locally on the same
toolchain/QEMU pair count for count, and re-measured locally at `0db95b6`:
identical count for count), in instructions:

| key | chain 16k / 48k | fdkf 16k / 48k | nn_suppressor 16k / 48k | shadow 16k / 48k | suppressor 16k / 48k |
|---|---:|---:|---:|---:|---:|
| m55 | −42 / −38 | −12 / −8 | −1,029 / −4,824 | −14 / −14 | −16 / −16 |
| m33 | +25,423 / +33,871 | +20 / +20 | −16 / −3,182 | +20 / +20 | +3,237 / +3,237 |
| hexagon | +189 / +189 | +214 / +214 | +2 / +2 | +214 / +214 | +8 / +8 |

Every cell is within ±0.005 %, so nothing is re-recorded. One lesson for
reading deltas of this size, measured on the way: the first form of the size
gate built its exception message with `std::string` / `std::to_string`, and
that alone moved m33 `fdkf` and `shadow` by +0.37 % (`shadow_16k`
+406,589, ~3 instructions per sample over 528 blocks) with `run()` and every
MuTap loop disassembling identically. The extra library code in the
translation unit made GCC 13.2 re-decide its inlining inside the
split-radix engine (`split_radix_rdft<float>::cftleaf`, 2,060 → 1,465
disassembly lines). The review of #58 measured the mechanism: recompiling
that `shadow_16k` object with only `--param large-unit-insns=1000000`
restores `cftleaf` (2,723 lines) and takes the count from 111,427,739 back to
111,309,467, while `-fno-inline-functions` changes nothing. Each workload is
one TU of roughly GCC's `large-unit-insns` budget (default 10,000); once a
change grows the TU past it, GCC rations inlining inside DspTap's engine.
The pin bump alone, before any MuTap change, read m33 `suppressor` +0.06 %
for the same reason. **So the m33 key has a TU-composition noise floor of
about 0.4 %**: a move of that size with no loop change is that budget until
shown otherwise. It is not the DspTap #35 class (register allocation in the
harness's `main`, fixed by a non-inlined factory); nothing on the harness
side reaches inlining inside the engine, and pinning the `--param` in
`bench/icount/CMakeLists.txt` would move the gate away from what a Release
build emits, so it is recorded here instead. (The size gate's message is a
literal because that is MuTap's config-error form; `include/mutap/fft.h`.)
The workloads compile at CMake's Release flags, `-O3 -DNDEBUG`.

**2026-09-25 — DspTap public `tap/dsp/math.h` (tap/DspTap#37), tap/MuTap#60: no re-record.**
The pin adds a header and `nn_suppressor.h` builds its sqrt-Hann window
through `tap::dsp::periodic_hann`, the identical expression (constructor
code). Measured locally, main (`edf160e`, pin `0db95b6`) against the PR head
on the committed toolchain/QEMU pair: every m55 and m33 row is identical
count for count (0 instructions on all 20 rows), so nothing is re-recorded.
The pin is DspTap `0c5bf59`, the #37 squash on DspTap `main` (the same tree
as the PR head `63cdfdf` that #60 merged with).

## FFT backend (Arm Helium)

The **m55** baselines record the CMSIS-DSP Helium FFT, which is the default on
the bare-metal M55 profile (`docs/optimization.md`) — ~42% fewer instructions
on every layer than the previous Ooura numbers. The ratchet therefore gates the
deployed backend. The split-radix float32 path is still available on the M55 with
`-DTAP_DSP_FFT_CMSIS=OFF` (kept alive by a dedicated CI leg, not by this ratchet).
The **hexagon** baselines are unaffected by that swap — Hexagon stays on the
scalar split-radix engine (the vendored Ooura C until DspTap `b08f6c6`, its
bit-identical C++20 port from DspTap `bbfa48d`; the port's counts are the
2026-09-23 rows above).
