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

## Scalar baselines (reference container, 2.8 GHz x86, GCC -O2, medians of 5, idle machine)

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
`chain` — at both certified geometries (`_48k`, `_16k`), all **float32**
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

**Seeding / re-recording:** a new target starts with an empty dict, so the
job reports each scenario's count and fails with `NO BASELINE`. Capture
those counts by running the same command with `--update` in the target's
environment (or the CI job's) and commit `bench/baselines.json`. After an
intentional optimization, re-record the same way — the two-sided gate
requires it.
