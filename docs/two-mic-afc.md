# Two microphones on one shared reference

*Measured September 2026 in simulation: four room pairs, band-limited
through a loudspeaker model. Host: macOS 15 x86_64, AppleClang, Release,
on a shared machine at load average 5–50 ([Hosts](#hosts)). Every ASG
number is a median over five seed sets with 10 s probes unless stated.
Nothing here has been heard in a real room. Every number below comes from
`tests/test_two_mic.cpp`, the `MUTAP_SLOW` sweep in
`tests/test_two_mic_sweep.cpp`, or tap/MuTap#73's description and review
comment; [Provenance](#provenance) says which.*

The question is the two-mic go/no-go: when two singers each have a mic, and
each mic has its own canceller on one shared speaker reference, how much
added stable gain (ASG) survives against one mic alone?

## Headline: no row the criterion counts falls below ⅔

**The criterion.** The two-mic ASG median must be at least ⅔ of the
**mean** of the two single-mic ASG medians, each mic measured alone in the
same position. "Unison" means a unison that beats (160 against 161 samples,
300 and 298.1 Hz, a 1.9 Hz beat). The phase-locked unison is kept as a
labelled worst-case row; [Unison](#phase-locked-and-beating-unison) says
why.

| Singers | Rows | Minimum ratio | Median ratio | Median gap, two-mic − mean single (dB) | Minimum gap (dB) | Below ⅔ |
|---|---|---|---|---|---|---|
| separated pitches (300 / 200 Hz) | 60 | 0.733 | 0.935 | −0.82 | −4.36 | 0 |
| speech | 60 | 0.967 | 1.047 | +0.97 | −0.66 | 0 |
| unison, beating 1.9 Hz | 8 | 0.782 | — † | — † | — † | 0 |
| unison, phase-locked (worst case) | 60 | 0.647 | 0.911 | −1.33 | −4.43 | 1 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; 10 s probes; five seed
sets.*

† The beating unison has 8 rows; the sweep printed each row but no summary.
All 8 are under [Unison](#phase-locked-and-beating-unison).

- **All 128 counted rows pass.** These are the 120 separated and speech rows
  of the grid and the 8 beating-unison rows.
- **Phase-locked unison: 59 of 60 pass.** That makes 179 of the 180 grid rows.
- **The one phase-locked row below the line:** `mt5+mt9`, S1, −10 dB
  leakage, canceller alone.
  - 10 s probe: 0.647 (two-mic +6.37 against single-mic +10.50 / +9.17).
  - 40 s probe: 0.653 (+6.45 against +10.58 / +9.17).
  - The same row with a beating unison reads 0.975.
- **Against the larger single-mic median** instead of the mean, 5 rows are
  below ⅔. All five are the phase-locked unison at S1:

| Pair | Leakage | Forward path | Ratio vs larger, 10 s | Ratio vs larger, 40 s |
|---|---|---|---|---|
| mt5+mt9 | −10 dB | canceller | 0.607 | 0.609 |
| mt5+mt9 | −10 dB | + aux −12 dB | 0.651 | 0.654 |
| mt5+mt9 | −20 dB | + aux −12 dB | 0.634 | 0.637 |
| rehearsal | −10 dB | canceller | 0.630 | 0.631 |
| rehearsal | −20 dB | canceller | 0.607 | 0.608 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; five seed sets.*

Rehearsal with no leakage sits on that stricter line: 0.668 at 10 s and
0.667 at 40 s.

## The loop

`tests/support/two_mic_loop.h`, `mutap_test::multi_mic_loop<Sample>`. It is
host-only and double precision for every ASG number.

- **Mics.** Each mic hears its own near end plus the mono speaker feed.
  - The feed is delayed by the electrical delay d, then passes through the
    mic's own band-limited 1024-tap path F_m.
  - What the room hears is clipped at 1000.
  - d is 480 samples (S1, 10 ms) or 960 (S3, 20 ms) at 48 kHz.
- **Cancellers.** Each mic has its own
  `pem_afc<double, speech_predictor, partitioned_fdkf>`: block 64,
  16 partitions, default configuration.
- **One shared reference, built as `afc_chain` builds it** (tap/MuTap#68).
  - The reference u lags the speaker feed by one block plus
    `reference_delay = d − 64 − 32`.
  - That is the chain's aligned delay with a 32-sample margin.
  - Each canceller's taps therefore model F_m delayed by the margin.
- **Forward path.** The bus is the sum of the cancellers' error signals.
  1. Optionally, the bus passes the 2 Hz `iir_ssb_shifter` from
     `tests/support/decorrelated_loop.h`.
  2. Then comes the forward gain K.
  3. Optionally, an aux feed (a backing track) is added: white noise at
     −12 dB relative to the unit-RMS singer. It is added after the gain, so
     the reference carries it.
- **Timeline check.** With one mic and no canceller, the loop equals
  `closed_loop_sim` at forward delay d sample for sample: max |bus − sim| = 0
  over 2000 blocks (`TwoMicLoop.OneMicDryLoopIsClosedLoopSim`).

### Room pairs

No fixture has a second physical mic position, so each pair is built from
what exists (`two_mic::room_pair`):

- **cabin, rehearsal, hall.** One 4096-tap image-source fixture read at two
  truncations, taps [0, 1024) and [256, 1280).
  - Each window is re-normalized to unit energy, then band-limited.
  - The second mic therefore hears the room from 5.3 ms on, with no direct
    path.
- **mt5+mt9.** Two `random_decaying_rir` seeds: independent paths.

### Singers and leakage

| Singers | Singer 0 | Singer 1 |
|---|---|---|
| unison (phase-locked) | `voiced_near_end`, 160-sample period (300 Hz) | the same, another seed |
| unison (beating) | 160 samples (300 Hz) | 161 samples (298.1 Hz) |
| separated | 160 samples (300 Hz) | 240 samples (200 Hz) |
| speech | `ar_near_end` | `ar_near_end`, another seed |

Each mic also hears the other singer through a leakage path:

- The path is a direct arrival plus a 256-tap decaying random tail 6 dB
  below it, at unit energy.
- Mic 0 hears singer 1 at 96 samples (2 ms). Mic 1 hears singer 0 at 144
  samples (3 ms). The tails use different seeds.
- The leaked signal is scaled so that its RMS sits exactly the stated level
  (−10 dB, −20 dB, or none) below the mic's own singer, on that signal.

### The ASG rule

The rule is `tests/support/karaoke_asg.h`'s, applied to the bus:

1. Converge the cancellers for 1500 blocks at `exact_msg_db(ΣF, d)` − 6 on
   seed s.
2. Bisect K to 0.1 dB on seed s + 10. Each probe gets a fresh copy of the
   converged cancellers and a fresh loop.
3. Howling means a bus block with RMS ≥ 100 (the 40 dB rule).
4. Seed sets are 2, 22, 42, 62, 82. Medians are upper medians.

- **Two-mic ASG** is the two-mic chain's limit minus the dry two-mic loop's
  limit. The dry loop has both mics at unit gain, no cancellers, a plain
  forward path and no aux. It is bisected on the same probe material,
  leakage included, and the same probe length.
- **Single-mic ASG, the reference.** Each mic alone: the same path, delay
  and forward path, and its own singer only. The second singer is absent,
  so nothing leaks. It is measured against its own dry loop.
- **The dry two-mic loop** sits at `exact_msg_db(F_0 + F_1, d)` +0.08 dB
  (S1) and +0.16 dB (S3), in the cabin and in mt5+mt9 (seed 2, 10 s,
  `TwoMicLoop.DryTwoMicLimitIsTheSummedPaths`). Over all 600 dry bisections
  of the grid, the limits sit 0.00 to 0.39 dB above `exact_msg_db`.

## The grid

`TwoMicSweep.Conditions` measured 180 conditions:

- **cabin and mt5+mt9, full factorial (144):** S1 / S3 × three singer pairs
  × three leakage levels × four forward paths (canceller; + 2 Hz shift;
  + aux at −12 dB; + both).
- **rehearsal and hall, canceller alone (36).**

Per factor, the median and minimum of the per-condition ratio (two-mic
median over the mean single-mic median):

| Factor | Level | Rows | Median ratio | Minimum ratio | Median gap (dB) | Minimum gap (dB) |
|---|---|---|---|---|---|---|
| pair | cabin | 72 | 0.999 | 0.718 | −0.02 | −4.36 |
| pair | hall | 18 | 0.949 | 0.711 | −0.83 | −3.78 |
| pair | mt5+mt9 | 72 | 0.987 | 0.647 | −0.19 | −4.43 |
| pair | rehearsal | 18 | 1.023 | 0.700 | +0.25 | −3.04 |
| delay | S1 (480) | 90 | 0.999 | 0.647 | −0.02 | −3.65 |
| delay | S3 (960) | 90 | 0.996 | 0.733 | −0.07 | −4.43 |
| leakage | none | 60 | 1.013 | 0.718 | +0.25 | −3.77 |
| leakage | −20 dB | 60 | 1.003 | 0.699 | +0.05 | −3.65 |
| leakage | −10 dB | 60 | 0.949 | 0.647 | −0.73 | −4.43 |
| forward | canceller | 72 | 0.992 | 0.647 | −0.14 | −4.43 |
| forward | + aux −12 dB | 36 | 0.951 | 0.699 | −0.60 | −3.43 |
| forward | + 2 Hz | 36 | 0.962 | 0.774 | −0.62 | −3.65 |
| forward | + 2 Hz + aux | 36 | 1.046 | 0.857 | +0.79 | −2.33 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; 10 s probes; five seed
sets. The unison rows here are phase-locked.*

What the tables show:

- **S1 and S3 are indistinguishable at the median** (0.999 and 0.996).
- **A second independent voice helps.** On speech the two-mic ASG is at or
  above the single-mic ASG in most rows (median ratio 1.047, +0.97 dB). The
  other singer, arriving through the shared reference, is excitation that is
  uncorrelated with each canceller's own near end. Unison removes exactly
  that.
- **Separated pitches are not free at S3.** Cabin and mt5+mt9 at S3, −10 dB
  leakage, canceller alone read 0.734 and 0.733, as low as the unison rows
  at S3.

## Phase-locked and beating unison

`voiced_near_end` places its pulses at n % period == 0 for every seed; the
seed only moves the −40 dB noise floor. Two seeds at one period are
therefore sample-aligned copies of one note. That is the coherent worst
case, and real duets beat. So the criterion's unison is the beating one,
and the phase-locked rows stay as the labelled floor.

`TwoMicSweep.DetunedUnison`, canceller alone:

| Pair | d | Leakage | Mic 0 alone | Mic 1 alone | Two-mic | Ratio vs mean | Ratio vs larger |
|---|---|---|---|---|---|---|---|
| cabin | 480 | −10 dB | +9.71 | +11.55 | +9.53 | 0.897 | 0.825 |
| cabin | 480 | none | +9.71 | +11.55 | +10.59 | 0.996 | 0.916 |
| cabin | 960 | −10 dB | +16.49 | +15.78 | +13.94 | 0.864 | 0.845 |
| cabin | 960 | none | +16.49 | +15.78 | +14.73 | 0.913 | 0.893 |
| mt5+mt9 | 480 | −10 dB | +10.50 | +10.32 | +10.15 | 0.975 | 0.967 |
| mt5+mt9 | 480 | none | +10.50 | +10.32 | +11.82 | 1.135 | 1.126 |
| mt5+mt9 | 960 | −10 dB | +16.74 | +15.77 | +12.71 | 0.782 | 0.759 |
| mt5+mt9 | 960 | none | +16.74 | +15.77 | +13.58 | 0.836 | 0.812 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; 10 s probes; five seed
sets; ASG medians in dB.*

The beating unison was measured on these 8 conditions only: two pairs,
canceller alone, no shift and no aux.

## What leakage costs

Change in the two-mic ASG against the same condition with no leakage, over
the grid:

| Singers | Leakage | Rows | Median (dB) | Minimum (dB) | Maximum (dB) |
|---|---|---|---|---|---|
| unison (phase-locked) | −20 dB | 20 | −0.09 | −2.29 | +0.88 |
| unison (phase-locked) | −10 dB | 20 | −1.02 | −2.46 | +1.14 |
| separated | −20 dB | 20 | −0.18 | −2.37 | +1.67 |
| separated | −10 dB | 20 | −1.14 | −2.37 | +0.62 |
| speech | −20 dB | 20 | −0.18 | −0.79 | +0.97 |
| speech | −10 dB | 20 | −0.31 | −1.76 | +0.88 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; 10 s probes; five seed
sets.*

Leakage at −10 dB costs about 1 dB of two-mic ASG on held notes. At −20 dB
it costs almost nothing.

## The forward path

Change against the canceller alone, cabin and mt5+mt9 only. The two-mic
column takes each condition over the three leakage levels; the single-mic
column is the mean of the two mics.

| Singers | Variant | Rows | Two-mic median (dB) | Two-mic range (dB) | Single-mic median (dB) | Single-mic range (dB) |
|---|---|---|---|---|---|---|
| unison (phase-locked) | + 2 Hz | 12 | +4.00 | −0.79 .. +7.56 | +3.16 | −0.09 .. +6.15 |
| unison (phase-locked) | + aux −12 dB | 12 | −1.41 | −3.59 .. +0.79 | −0.90 | −5.32 .. +2.15 |
| unison (phase-locked) | + 2 Hz + aux | 12 | +3.21 | +0.62 .. +8.53 | +1.91 | −3.12 .. +6.24 |
| separated | + 2 Hz | 12 | +4.48 | +1.58 .. +6.33 | +4.20 | +0.44 .. +6.64 |
| separated | + aux −12 dB | 12 | −0.13 | −2.02 .. +2.37 | −0.29 | −2.81 .. +1.93 |
| separated | + 2 Hz + aux | 12 | +5.10 | +2.64 .. +7.73 | +2.64 | −1.36 .. +6.06 |
| speech | + 2 Hz | 12 | +0.00 | −0.62 .. +0.79 | +0.07 | −0.04 .. +0.40 |
| speech | + aux −12 dB | 12 | −0.57 | −1.85 .. −0.09 | −0.42 | −1.19 .. −0.00 |
| speech | + 2 Hz + aux | 12 | −0.97 | −1.85 .. +0.09 | −0.54 | −0.79 .. +0.44 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; 10 s probes; five seed
sets; runaway ASG.*

- **The 2 Hz shift raises runaway ASG on held notes by about 4 dB**, for one
  mic and for two. Speech is unchanged.
- **The −12 dB white aux lowers runaway ASG in most rows**, for one mic and
  for two. This is unexplained. The karaoke suite's 0 dB aux raised it
  ([karaoke-afc.md](karaoke-afc.md)), with a different reference model and a
  different level.
- **With the shift and the aux together**, the two-mic ASG exceeds the
  single-mic ASG in most rows (median ratio 1.046).
- These are runaway limits. The audible limit with the shift was not
  measured here.

## Probe length

`TwoMicSweep.ProbeConvergence`: cabin, −10 dB leakage, canceller alone, ASG
medians in dB. "Converged at" is the shortest probe within 0.25 dB of the
40 s median, the karaoke rule.

| d | Singers | Who | 5 s | 10 s | 20 s | 40 s | Converged at |
|---|---|---|---|---|---|---|---|
| 480 | speech | two-mic | +21.57 | +21.21 | +21.12 | +21.21 | 10 s |
| 480 | speech | mic 0 | +19.82 | +19.63 | +19.19 | +19.10 | 20 s |
| 480 | speech | mic 1 | +22.10 | +21.91 | +21.74 | +21.30 | 40 s |
| 480 | unison | two-mic | +8.13 | +8.21 | +8.29 | +8.29 | 5 s |
| 480 | unison | mic 0 | +9.63 | +9.71 | +9.79 | +9.79 | 5 s |
| 480 | unison | mic 1 | +10.33 | +10.32 | +10.40 | +10.40 | 5 s |
| 960 | speech | two-mic | +23.53 | +23.24 | +23.23 | +22.53 | 40 s |
| 960 | speech | mic 0 | +20.28 | +19.91 | +19.38 | +19.10 | 40 s |
| 960 | speech | mic 1 | +20.89 | +20.96 | +20.33 | +20.16 | 20 s |
| 960 | unison | two-mic | +13.87 | +14.02 | +14.10 | +14.18 | 10 s |
| 960 | unison | mic 0 | +16.88 | +16.49 | +15.86 | +15.85 | 20 s |
| 960 | unison | mic 1 | +16.59 | +16.75 | +16.83 | +16.90 | 10 s |

*macOS 15 x86_64, AppleClang, Release, load 5–50; five seed sets; the
unison is phase-locked.*

- **10 s is not converged on every row.** Against 40 s, held-note medians
  differ by at most 0.64 dB and speech medians by at most 0.81 dB. Speech
  drifts down to 40 s, as in the karaoke sweep.
- **The ratio moves less.** 10 s against 40 s differs by at most 0.029 here.
- **The rows nearest the line, re-run at 40 s** (`TwoMicSweep.LongProbeCheck`,
  held-note unison at S1), move the ratio by at most 0.007:

| Pair | Leakage | Forward path | Ratio, 10 s | Ratio, 40 s |
|---|---|---|---|---|
| mt5+mt9 | −10 dB | canceller | 0.647 | 0.653 |
| mt5+mt9 | −10 dB | + aux −12 dB | 0.718 | 0.720 |
| mt5+mt9 | −20 dB | canceller | 0.790 | 0.795 |
| mt5+mt9 | −20 dB | + aux −12 dB | 0.699 | 0.702 |
| mt5+mt9 | none | canceller | 0.844 | 0.849 |
| mt5+mt9 | none | + aux −12 dB | 0.737 | 0.739 |
| rehearsal | −10 dB | canceller | 0.726 | 0.727 |
| rehearsal | −20 dB | canceller | 0.700 | 0.702 |
| rehearsal | none | canceller | 0.769 | 0.770 |
| hall | −10 dB | canceller | 0.711 | 0.717 |
| hall | −20 dB | canceller | 0.719 | 0.726 |
| hall | none | canceller | 0.895 | 0.900 |

*macOS 15 x86_64, AppleClang, Release, load 5–50; five seed sets; ratio
against the mean single-mic median.*

## Convergence from cold (smoke)

`TwoMicSmoke.BothMicsConvergeOnTheSharedReference`: float, S1, mt5+mt9,
−10 dB leakage, the bus loop at `exact_msg_db(F_0 + F_1)` − 6, 1500 blocks
(2 s) from cold, five seed sets. Per mic, over the five sets:

| Singers | Uncertainty ratio (dB) | Misalignment (dB) | Peak bus RMS |
|---|---|---|---|
| speech | −17.98 .. −17.67 (medians −17.80 / −17.89) | −5.99 .. −4.50 (medians −4.58 / −5.69) | ≤ 3.78 |
| unison (phase-locked) | −13.65 .. −10.34 (medians −10.98 / −12.89) | +8.36 .. +14.89, not asserted | ≤ 3.14 |

*macOS 15 x86_64, AppleClang, Release.*

On the held note both cancellers converge by the uncertainty ratio, but the
impulse response is wrong. The estimate carries the closed-loop bias that
`afc_chain` documents after a held note, not the room. That is not an ASG
claim.

## Cost per block

`TwoMicSweep.Cost`: ns per 64-sample block, wall clock, median of 3 × 3000
blocks, on recorded loop signals.

| Load average | Type | One canceller | Two cancellers + bus | Ratio |
|---|---|---|---|---|
| ~4 | float | 470522 | 931900 | ×1.981 |
| ~4 | double | 471502 | 930436 | ×1.973 |
| ~35 | float | 948100 | 1931235 | ×2.037 |
| ~35 | double | 966852 | 2064040 | ×2.135 |

*macOS 15 x86_64, AppleClang, Release, shared machine.*

- A second canceller about doubles the cost: ×1.97 to ×2.14.
- The block budget at 48 kHz is 1333333 ns.
- `pem_afc.h`'s own figure for one canceller on this machine, unloaded, is
  324621.6 ns (double). No unloaded two-mic measurement was possible: the
  machine was shared throughout. The embedded cost is not measured.

## Caveats

- **One RIR at two truncations.** Three of the four pairs are one
  image-source fixture read at taps [0, 1024) and [256, 1280): the second
  mic has no direct path. Only mt5+mt9 has independent paths. No physically
  placed second mic has been simulated.
- **No second singer in the single-mic reference.** "Each mic alone" is
  measured with its own singer only and no leakage. One mic with the other
  singer still leaking in was not measured.
- **Runaway limits only.** No audible limit was measured for any two-mic
  row.
- **The shift and aux rows exist on two pairs only**, cabin and mt5+mt9.
  Rehearsal and hall ran the canceller alone. The beating unison ran on
  8 conditions, canceller alone.
- **The simulator is not cross-checked against `afc_chain`.**
  `multi_mic_loop` reproduces the chain's timeline, and the one-mic dry loop
  matches `closed_loop_sim` exactly. It has not been run against the real
  `afc_chain` in a loop.
- **The single-mic numbers are not karaoke-suite numbers.** This loop feeds
  the cancellers the chain's aligned reference, which lags the speaker by
  one block; the karaoke loop feeds the canceller the current speaker block.
  Cabin, S1, held note, canceller alone: +9.71 here against the karaoke
  suite's +11.63.
- **The sweep ran on the tree before tap/MuTap#67.** It was measured at
  864e596. #67 then replaced the harness Hilbert pair in
  `decorrelated_loop.h` with the library's `allpass_hilbert`. After the
  rebase only the smoke suite was re-run, and every value it prints was
  identical. The 2 Hz shift rows have not been re-measured on main.
- **Closed-loop ASG is chaotic.** Single seeds vary by several dB, so every
  number is a five-seed median. The grid prints the per-seed values.

## Hosts

Every number in this document was measured on **macOS 15 x86_64,
AppleClang, Release**. The machine was shared with other jobs throughout,
at load average 5 to 50. The karaoke document shows that identical signals
give rows several dB apart on another host
([karaoke-afc.md, Hosts](karaoke-afc.md#hosts)). No second host has
measured the two-mic loop. Quote the host with the number.

## Provenance

The plumbing and smoke tests run by default (about 6 s):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/tests/mutap_tests --gtest_filter='TwoMic*'
```

| Test | What it printed |
|---|---|
| `TwoMicLoop.OneMicDryLoopIsClosedLoopSim` | the timeline check (max difference 0) |
| `TwoMicLoop.ReferenceLagsTheSpeakerByBlockPlusDelay` | nothing; it asserts the reference timing |
| `TwoMicLoop.DryTwoMicLimitIsTheSummedPaths` | the dry two-mic limit against `exact_msg_db` |
| `TwoMicSmoke.BothMicsConvergeOnTheSharedReference` | the smoke table |

The sweep is behind `MUTAP_SLOW=1`. It measures and prints; it asserts
nothing about ASG.

```sh
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=4 TWO_MIC_RAW=two_mic.csv \
    build/tests/mutap_tests --gtest_filter='TwoMicSweep.*'
```

`TWO_MIC_RAW=<csv>` appends every finished bisection to the file, and a
rerun resumes from it: a job already in the file is not run again. A table
can therefore be reprinted from a kept file without rerunning (each test
then reports "0 to run").

| Test | Bisections | Wall time (4 threads) | Printed |
|---|---|---|---|
| `Conditions` | 2100 | 12462 + 2747 s (two invocations) | the 180-row grid, the single-mic rows per seed set, the dry loops |
| `ProbeConvergence` | 480 | 6998 s | the probe-length table |
| `LongProbeCheck` | 175 | 1549 + 1499 s | the 40 s re-run of the 12 rows nearest the line |
| `DetunedUnison` | 160 | 629 s | the beating-unison table |
| `Cost` | — | — | the cost table |

The per-factor, leakage and forward-path tables above are aggregates of the
`Conditions` grid rows. No test in the repo prints them; they were computed
from its output when tap/MuTap#73 was written. Environment overrides for
other grids (`TWO_MIC_PAIRS`, `TWO_MIC_DELAYS`, `TWO_MIC_SINGERS`,
`TWO_MIC_LEAKS`, `TWO_MIC_FWD`, `TWO_MIC_PROBE`) are documented at the top
of `tests/test_two_mic_sweep.cpp`.
