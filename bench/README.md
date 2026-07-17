# MuTap benchmarks

Host-side performance baselines for the AEC hot path (the SampleRateTap
`bench/` pattern). These exist so that **every optimization has a
measured before/after on the same machine** — absolute numbers below are
reference points from one container, not gates. Wall-clock benches are
never asserted in CI; the deterministic instruction-count ratchet for
the embedded targets arrives with the M55 milestone.

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

## Scalar baselines (reference container, 2.8 GHz x86, GCC -O2, medians of 5)

| layer | 48 kHz f64 | 48 kHz f32 | 16 kHz f64 | 16 kHz f32 |
|---|---|---|---|---|
| fdkf       | 62.7 µs | 54.1 µs | 35.5 µs | 31.1 µs |
| suppressor | 115.7 µs | 104.7 µs | 116.5 µs | 100.5 µs |
| shadow     | 22.6 µs | 19.9 µs | 22.4 µs | 19.7 µs |
| **chain**  | **208.4 µs** | **192.2 µs** | **182.3 µs** | **173.6 µs** |

Per-block real-time budget: 5.33 ms at 48 kHz, 16 ms at 16 kHz — the
full chain costs **3.9 % / 1.1 %** of budget (f64) on this host.

Two findings the table pins down for the optimization work ahead:

1. **The suppressor dominates, not the Kalman core** — ~55 % of the
   chain at 48 kHz, and its cost is block-size-bound, not rate-bound
   (identical at both rates: the analysis machinery scales with the
   2048-sample analysis window, which is the same at block 256
   everywhere). It is optimization target #1.
2. **Scalar float32 buys only ~10–15 % on x86** — same-width scalar
   ALUs. The float32 payoff is the vector width on M55 Helium and
   Hexagon HVX, which is exactly why the parity gates
   (`tests/test_float32.cpp`) were landed before this harness.
