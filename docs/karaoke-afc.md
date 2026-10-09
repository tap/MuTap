# Feedback cancellation for in-cabin karaoke

*Measured September 2026 in simulation: the cabin RIR fixture and four other
rooms, band-limited through a loudspeaker model. Host: Linux x86-64, GCC
13.3, Release ([Hosts](#hosts)). Nothing here has been heard in a real car.
Every number below comes from a gated test, the `MUTAP_SLOW` sweep, or the
`karaoke_audible.py` driver; [Provenance](#provenance) gives the exact
commands.*

The use case: a microphone and loudspeakers share a car cabin, and the mic
signal is amplified back into the cabin so a passenger can sing. Three things
make this the hardest case the library targets, and they compound:

- The near-end source is sustained and pitched. That is the worst material
  for closed-loop identification.
- The loop latency is short, because nobody wants to hear their own voice late.
- The speaker sits an arm's length from the mic.

**The product is canceller-first.** At the anti-howl PoC's latencies,
S1 = 10 ms and S3 = 20 ms, the default chain is the canceller alone
(PEM + FD-Kalman). A forward-path frequency shift is a studied,
material-dependent option. Phase 1's blind listening test on real singing
decides whether it goes in.

## Headline: the canceller's audible limit

Setup:

- Held 300 Hz note, band-limited cabin.
- PEM + FD-Kalman (speech cascade, default configuration), adapting from a
  cold start.
- The anti-howl protocol's gain ramp: 30 s warm-up 20 dB under the open-loop
  limit, then +1 dB per 2 s.
- The offline howl criterion (`tools/fixtures/howl_criterion.py`) runs on the
  canceller output c.
- Values are dB over the dry loop's phase-exact open-loop limit
  (`exact_msg_db`: S1 −6.897, S3 −6.860; `theoretical_msg_db`, max|F|,
  −7.054).
- Medians over five seeds (2, 22, 42, 62, 82).
- Host: Linux x86-64, GCC 13.3, Release (see [Hosts](#hosts)).

| Chain | S1 audible | S1 runaway (ramp) | S1 runaway (bisected, 40 s) | S3 audible | S3 runaway (ramp) | S3 runaway (bisected, 40 s) |
|---|---|---|---|---|---|---|
| dry (no canceller) | +0.18 | +1.08 | 0 (the reference) | +0.21 | +1.62 | 0 (the reference) |
| **canceller** | **+14.99** | +21.49 | +11.72 | **+10.24** | +21.17 | +18.31 |
| canceller + 2 Hz shift † | +5.35 | +18.86 | +14.79 | +6.37 | +20.60 | +16.38 |
| canceller + 5 Hz shift † | +4.88 | +20.18 | +17.17 | +0.81 | +19.45 | +16.90 |
| canceller + backing track (aux) | +3.59 | +22.76 | +14.53 | +23.45 | +24.65 | +16.82 |
| canceller + 2 Hz + backing track | +19.55 | +20.48 | — | +22.05 | +23.08 | — |
| canceller + 5 Hz + backing track | +19.88 | +20.49 | — | +21.21 | +22.41 | — |

Per seed:

| Chain | Delay | Audible, per seed | Runaway (ramp), per seed |
|---|---|---|---|
| canceller | S1 | +16.31 +14.99 +15.85 +13.11 +14.42 | +20.53 +21.49 +21.91 +21.21 +22.03 |
| canceller | S3 | +13.30 +0.20 +10.24 +11.33 +5.36 | +21.17 +21.76 +18.74 +20.19 +23.37 |

† **Worst case for the shift.** The held note is a perfectly periodic
synthetic note with a 40 dB floor between its harmonics. Partials that
recirculate through the shifter stand out against that floor, climbing by
the shift on each pass. The criterion counts them as audible, and that
decision is deliberate. Any claim about the shift on singing needs real sung
recordings and the ABX test.

How to read the columns:

- **Audible** is where the criterion finds the first narrowband peak at
  least 20 dB over the program's envelope that lasts at least 500 ms.
- **Runaway (ramp)** is where the same ramp hits the harness's 40 dB rule.
- **Runaway (bisected)** is the test suites' measurement. The canceller
  converges at MSG − 6 and is then held for a 40 s probe, and the limit is
  reported as ASG against the dry loop bisected on the same probe (the
  like-for-like rule).

The two runaway columns measure different protocols. A slowly ramped,
continuously adapting canceller holds more than one converged at MSG − 6 and
then probed: at S1, +21.49 against +11.72. Treat the difference as a
protocol effect, not an error (see HANDOFF, open issues).

The canceller alone gives up about 6.5 dB (S1) and about 11 dB (S3) between
audible and runaway. With the shift the gap is 13–19 dB. Add the backing
track and the shifted chains become audible within about 1 dB of runaway.

## How it is measured

- **Loop.** `tests/support/decorrelated_loop.h`: mic → canceller → forward
  delay → optional decorrelator → gain → optional aux feed → speaker → room.
  - The canceller's reference is what the speaker plays.
  - The forward delay is 480 samples (S1) or 960 (S3) at 48 kHz, plus 128
    (2.7 ms) as a regression row.
- **Rooms.**
  - The cabin fixture (`tests/fixtures/rir_cabin.h`, image-source model,
    1.9 × 1.45 × 0.95 m, mic at the mirror, speaker in the footwell).
  - Two more image-source fixtures, studio and rehearsal.
  - Two `random_decaying_rir` rooms, mt5 and mt9, so both generator families
    are covered.
  - Each room is its first 1024 taps at unit energy, then passed through
    `band_limited()` (80 Hz highpass, 16 kHz lowpass).
  - Block 64, 16 partitions, double precision.
- **Materials.**
  - The held note is `voiced_near_end` with a 160-sample pitch period.
  - The speech-envelope material is `ar_near_end`.
  - The backing track is white noise at the singer's level (0 dB).
- **Bisected runaway (the tests).** `tests/support/karaoke_asg.h`:
  - Converge for 1500 blocks at `exact_msg_db` − 6.
  - Bisect the forward gain, each probe on a fresh copy of the converged
    canceller. Howling means a 64-sample block 40 dB over the unit-RMS near end.
  - ASG is the chain's limit minus the dry loop's limit, bisected on the same
    probe material and length.
  - Medians over five seed sets.
- **Audible (the driver).**
  - `tools/notebook/karaoke_ramp_dump.cpp` runs the same loop under the ramp.
  - `tools/notebook/karaoke_audible.py` applies the criterion with
    `--rt60 0.0328` and `--dechirp`.
  - 0.0328 s is the T30 of the band-limited cabin at 4096 taps, from
    `measure_rir.py`'s `schroeder()`. The 1024-tap path has too little decay
    range for a T30.

**Probe length.** It is chosen by convergence: for each row, the shortest
probe whose five-seed median lies within 0.25 dB of the 40 s probe's.
Medians of the bisected ASG, from the sweep:

| Cabin, held note | 0.8 s | 5 s | 10 s | 20 s | 40 s |
|---|---|---|---|---|---|
| canceller, 2.7 ms | −2.10 | −1.63 | −1.55 | −1.55 | −1.55 |
| + 5 Hz, 2.7 ms | +13.54 | +14.01 | +14.09 | +13.56 | +14.09 |
| canceller, S1 | +10.47 | +11.56 | +11.64 | +11.72 | +11.72 |
| + 2 Hz, S1 | +13.54 | +14.64 | +14.72 | +14.79 | +14.79 |
| + 5 Hz, S1 | +16.01 | +17.10 | +17.18 | +17.26 | +17.17 |
| + aux, S1 | +13.28 | +14.38 | +14.45 | +14.53 | +14.53 |
| naive + 5 Hz, S1 | +3.96 | −0.48 | +5.05 | −0.32 | −15.00 |
| canceller, S3 | +16.43 | +18.37 | +18.52 | +18.67 | +18.31 |
| + 2 Hz, S3 | +15.63 | +16.79 | +16.49 | +17.00 | +16.38 |
| + 5 Hz, S3 | +14.40 | +16.52 | +16.76 | +16.83 | +16.90 |
| + aux, S3 | +14.32 | +16.35 | +16.58 | +16.74 | +16.82 |
| naive + 5 Hz, S3 | +5.97 | +8.00 | +8.23 | +8.21 | +8.47 |

What the table shows:

- **The old probe was too short.** The branch's 0.8 s probe over-reads the
  dry open loop by 0.55 dB (2.7 ms), 1.25 dB (S1) and 2.50 dB (S3). Like for
  like, the canceller rows' 0.8 s medians sit −2.50 to +2.61 dB from their
  40 s values: the held note reads low, the speech-envelope material reads
  high.
- **One row never converges.** The naive core behind the shift at S1 does
  not settle with probe length.
- **The speech-envelope rows drift down to 40 s.** At 2.7 ms and at S3 they
  are still moving, so they are reported, not gated.

## The canceller alone

This is the product default. Held note, runaway ASG, 20 s probes:

| Room | S1 | S3 |
|---|---|---|
| cabin | +11.72 | +18.67 |
| studio | +11.11 | +18.41 |
| rehearsal | +11.20 | +16.75 |
| mt5 | +11.02 | +18.13 |
| mt9 | +12.16 | +17.87 |

On the speech-envelope material the cabin reads +20.42 (S1) and +20.51 (S3)
at 40 s. The S3 value is still drifting: the 20 s probe read +21.57.

## The frequency shift: an option, not the default

A single-sideband shift of the amplified signal (Schroeder 1964) moves every
partial by the same number of Hz. That decorrelates the loudspeaker signal
from the singer.

**The shifter.** Olli Niemitalo's 4+4 IIR allpass-pair Hilbert transformer
and a rotation, in `decorrelated_loop.h`. The Hilbert pair is the library's
(`tap::mu::allpass_hilbert`, `include/mutap/frequency_shifter.h`); the
rotation is the exact per-sample phase ramp. The library's shippable shifter,
`tap::mu::frequency_shifter`, runs the same pair with a renormalized
recursive oscillator; its measured numbers are in that header.

- Image rejection of a +5 Hz shift: 44.3 dB at 30 Hz, 55.7 at 100 Hz, 44.8
  at 300 Hz, 49.0 at 1 kHz, 46.8 at 5 kHz.
- Group delay: 2.56 ms at 100 Hz, 1.39 at 200, 0.94 at 300, 0.29 at 1 kHz.
- The shifter is causal but not latency-free. Its delay adds to the loop's,
  and the forward delay is not shortened to make room for it. At the held
  note, a shifted loop is therefore 0.94 ms longer than a plain one.
- It replaced the branch's 65-tap FIR, which was partly double sideband at
  the held note.

**Runaway.** Held note, 20 s probes. Each cell is the per-seed difference
(shift − canceller), median:

| Room | S1, 5 Hz | S1, 2 Hz | S3, 5 Hz | S3, 2 Hz |
|---|---|---|---|---|
| cabin | +5.80 | +3.43 | **−1.85** | **−2.02** |
| studio | +5.62 | +5.98 | **−0.70** | +1.41 |
| rehearsal | +4.83 | +4.31 | +1.49 | **−0.26** |
| mt5 | +8.88 | +5.80 | +3.16 | +3.43 |
| mt9 | +8.35 | +6.59 | +3.34 | +3.25 |

- At S1 the shift raises the runaway limit in every room.
- At S3 the direction depends on the room. The cabin loses at both shifts
  (−1.85 at 5 Hz, at 40 s as at 20 s), the studio at 5 Hz and rehearsal at
  2 Hz; mt5 and mt9 gain. The S3 direction is recorded, not gated.

**Audible, worst case.** On the bare held note the shift *lowers* the
audible limit, well below the canceller alone: from +14.99 to +5.35 (2 Hz)
and +4.88 (5 Hz) at S1, and from +10.24 to +6.37 and +0.81 at S3. The
criterion flags the recirculating partials first. This is the worst case
described under the headline table.

One run carries an early event that is probably a detector artifact
(2 Hz, S1, seed 22):

- The criterion's −7.02 comes from an event that only a dechirped pass
  found: 0.55 s near 4.95 kHz (pass long@−500) in the run's `.howl.json`.
- The plain long pass first fires at +9.15.
- It moves the median: +5.35 as the criterion counts it, +6.55 from the
  plain pass's first events.

**Recovery with the backing track.** With a white backing track at the
singer's level, the shifted chains are audible within about 1 dB of
runaway: +19.55 and +19.88 at S1, +22.05 and +21.21 at S3 (2 and 5 Hz).

The shift is **not free perceptually**. Partials move by equal Hz, not by
equal ratio, so a sung voice goes slightly inharmonic and detunes against an
unshifted backing track. That, together with the worst-case audible numbers
above, is why the listening test decides.

## The backing track is free excitation

A backing track is uncorrelated with the singer, so it excites the
identification for free. That works only when the canceller's reference is
tapped after the track is mixed into the speaker feed.

- **Runaway, cabin, 40 s.** The track raises the limit from +11.72 to +14.53
  at S1, and lowers it at S3, from +18.31 to +16.82.
- **Other rooms, 20 s.** S1: studio +13.57, rehearsal +12.08, mt5 +15.15,
  mt9 +14.53, above the canceller alone in every room. S3: studio +16.83,
  rehearsal +14.81, mt5 +16.90, mt9 +15.41, below the canceller alone in
  every room.
- **Audible, cabin.** Both delays split. S1: four seeds are audible early
  (−0.54 to +4.20) and one late (+15.65), for a median of +3.59. S3: four
  seeds are audible within a few tenths of a dB of runaway (+22.38 to
  +25.78) and one early (+5.16), for a median of +23.45. Nothing explains
  the early seeds yet (HANDOFF, open issues).

## Delay modulation

The delay wobbles ±8 samples at 1.3 Hz, a gentler decorrelator with no pitch
artifact. Held note, 20 s probes, runaway ASG:

| Room | S1 | S3 |
|---|---|---|
| cabin | +13.83 | +16.74 |
| studio | +13.22 | +18.23 |
| rehearsal | +13.22 | +17.89 |
| mt5 | +13.39 | +18.93 |
| mt9 | +12.95 | +20.07 |

At S1 that is +0.79 to +2.37 dB over the canceller alone, comparing room
medians, and below the 5 Hz shift in every room. Its audible limit has not
been measured.

## Decorrelation does not replace PEM

The naive (un-prewhitened) NLMS core behind the same 5 Hz shift, 40 s
probes, against PEM + FD-Kalman behind the shift. Each PEM − naive cell is a
per-seed median of the chain limits:

| Room | S1 naive ASG | S1 PEM − naive | S3 naive ASG | S3 PEM − naive |
|---|---|---|---|---|
| cabin | −15.00 | +28.56 | +8.47 | +8.09 |
| studio | −13.51 | +29.97 | +6.27 | +12.39 |
| rehearsal | −15.00 | +31.46 | +5.49 | +11.95 |
| mt5 | −15.00 | +35.16 | +7.06 | +14.59 |
| mt9 | −6.04 | +26.19 | +6.88 | +14.24 |

- −15.00 is the probe floor.
- At S3 the naive core no longer runs away at the floor. PEM still leads by
  at least 8.09 dB everywhere.
- Decorrelation shrinks the bias term, and prewhitening removes what is left.

## Low-latency regression row (2.7 ms)

This is the branch's original setting, kept as a regression row. It is not a
PoC scenario. At 2.7 ms the held note is the wall: the canceller alone has no
useful gain, and a 5 Hz shift rescues it. Cabin, 40 s probes:

| Chain | Runaway ASG |
|---|---|
| canceller | −1.55 |
| + 2 Hz | +10.75 |
| + 5 Hz | +14.09 |
| + aux | +16.99 |
| naive + 5 Hz | −15.00, the floor |

The gated test reads canceller −1.50, 5 Hz +13.50 and PEM − naive +28.50
(5 s probes, 0.5 dB bisection).

On the raw (unbanded) cabin at 20 s the same row reads canceller +1.25 and
5 Hz +16.28. The branch's single-seed +0.6 → +18.4 came from that unbanded
path, measured against max|F| with 0.8 s probes. Band-limiting moves the
raw S1 and S3 numbers too: canceller +12.50 and +18.48, 5 Hz +19.71 and
+21.20.

## Reverb in the loop: deferred

The branch also carried `tap::mu::spectral_reverb`, a per-bin
frequency-domain reverb whose decay is shaped from the canceller's F̂. It
does not land with this work, for three reasons:

- Re-measured under these rules, the shaped decay lost to a flat decay
  everywhere.
- No reverb-with-canceller bisection converged by 40 s.
- The class also needs the FFT ABI tag.

It returns with phase 2's reverb-integration item. HANDOFF records what was
measured and the code's location. That item's first part put the FAUST
Dattorro plates behind the canceller and measured them with converged
probes ([A reverb behind the canceller](reverb-afc.md)); the spectral
reverb comes back on the same harness in the next part.

## Reading these numbers

- **Closed-loop ASG is chaotic.** Single seeds vary by several dB, which is
  why everything here is a five-seed median with the per-seed spread
  available in the sweep's output.
- **The simulation is deliberately worst-case in one way.** The held note is
  perfectly periodic.
- **It is optimistic in others:**
  - a static path;
  - no road noise;
  - no moving occupants;
  - modeled rooms and a modeled loudspeaker band;
  - double precision rather than the float32 the embedded targets run.
- **The shift's audible numbers are the least transferable.** They depend on
  the program's spectrum, which is why they carry the worst-case label.

## Hosts

Every ASG number above was measured on **Linux x86-64, GCC 13.3, Release**,
on tap/MuTap#66's portable random variates: the `MUTAP_SLOW` sweep and all
70 ramp runs of `karaoke_audible.py` (commit e14aaec), and the gated test's
readings (#66). The wall-clock times under [Provenance](#provenance) are the
Intel Mac's.

The portable variates make the *signals* identical on every host: the rooms,
the near ends and the backing track. The closed loop is chaotic, though, so
platform arithmetic still moves single rows. A second host, **macOS 15
x86_64, AppleClang, Release**, measured the headline on the same five seeds
(tap/MuTap#71):

| Chain (macOS) | S1 audible | S1 runaway (ramp) | S3 audible | S3 runaway (ramp) |
|---|---|---|---|---|
| canceller | +14.96 | +21.43 | +11.65 | +21.86 |
| canceller + 2 Hz shift | +5.50 | +19.65 | +7.25 | +20.32 |
| canceller + 5 Hz shift | +1.65 | +19.49 | +1.30 | +18.26 |
| canceller + backing track (aux) | +3.59 | +23.01 | +23.79 ‡ | +24.53 |
| canceller + 2 Hz + backing track | +19.56 | +20.44 | +22.39 | +23.85 |
| canceller + 5 Hz + backing track | +19.18 | +19.95 | +22.16 | +22.58 |

‡ Median over four seeds: at seed 42 the criterion found no qualifying event
before the ramp ran away (at +24.53).

Identical signals give rows up to 3.23 dB apart (S1, 5 Hz, audible: Linux
+4.88, macOS +1.65), and rows that do not converge move further (the 2.7 ms
gated row's PEM − naive: +28.50 on Linux, +22.25 on macOS). Every direction
this document claims agrees on both hosts, and every gated claim passes on
both. A quoted number is only meaningful with its host.

## Provenance

The gated claims run in about 3 minutes on the Intel Mac (2:46 and 2:56 in
two runs with the S1 shift row's earlier 10 s probe; 3:13.6 with its 20 s
probe):

- the shifter's image rejection and group delay;
- the 2.7 ms regression row;
- S1: canceller alone, and the shift raising runaway.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_KARAOKE_DUMP=ON
cmake --build build
build/tests/mutap_tests --gtest_filter='AfcDecorrelation.*'
```

Every number above was re-measured on Linux x86-64 (GCC 13.3, Release) when
the tests' random fixtures moved to `tests/support/portable_random.h` (one
scenario on every host); the first measurement, on the Mac, ran the other
standard library's rooms and signals. There the sweep's three tests took
98, 57 and 8 minutes on 4 threads.

The sweep (every bisected table above) takes 1:16 wall-clock on 11 threads
of the 12-thread Intel Mac:

```sh
MUTAP_SLOW=1 build/tests/mutap_tests --gtest_filter='AfcDecorrelationSweep.*'
```

The audible table takes about 3 minutes per run (dump plus criterion) and
needs numpy and scipy. The seed batches are deterministic, so one invocation
with `--seeds 2,22,42,62,82` gives the same table:

```sh
D=build/tools/notebook/karaoke_ramp_dump
python3 tools/notebook/karaoke_audible.py --dump $D --work runs --jobs 4 --seeds 2,22,42 --json a.json
python3 tools/notebook/karaoke_audible.py --dump $D --work runs --jobs 4 --seeds 62,82 \
    --configs dry,plain,shift2,shift5 --json b.json
python3 tools/notebook/karaoke_audible.py --dump $D --work runs --jobs 4 --seeds 62,82 \
    --configs aux,shift2_aux,shift5_aux --json c.json
python3 tools/notebook/karaoke_audible.py --merge a.json b.json c.json --work runs
```
