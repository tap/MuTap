# Feedback cancellation for in-cabin karaoke

*Measured September 2026 in simulation: the cabin RIR fixture and four other
rooms, band-limited through a loudspeaker model. Nothing here has been heard
in a real car. Every number below comes from a gated test, the `MUTAP_SLOW`
sweep, or the `karaoke_audible.py` driver; [Provenance](#provenance) gives
the exact commands.*

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

| Chain | S1 audible | S1 runaway (ramp) | S1 runaway (bisected, 40 s) | S3 audible | S3 runaway (ramp) | S3 runaway (bisected, 40 s) |
|---|---|---|---|---|---|---|
| dry (no canceller) | +0.09 | +1.08 | 0 (the reference) | +0.13 | +1.62 | 0 (the reference) |
| **canceller** | **+16.16** | +20.71 | +11.63 | **+10.81** | +21.05 | +18.31 |
| canceller + 2 Hz shift † | +6.03 | +19.81 | +16.82 | +6.23 | +19.21 | +16.55 |
| canceller + 5 Hz shift † | +4.69 | +19.59 | +17.52 | +2.30 | +20.17 | +16.90 |
| canceller + backing track (aux) | +3.90 | +23.51 | +15.94 | +24.03 | +23.93 | +18.93 |
| canceller + 2 Hz + backing track | +20.54 | +20.13 | — | +22.56 | +23.38 | — |
| canceller + 5 Hz + backing track | +19.42 | +20.14 | — | +22.46 | +22.69 | — |

Per seed:

| Chain | Delay | Audible, per seed | Runaway (ramp), per seed |
|---|---|---|---|
| canceller | S1 | +13.21 +16.04 +16.16 +17.60 +16.63 | +20.71 +20.66 +20.53 +21.33 +21.10 |
| canceller | S3 | +13.17 +12.85 +4.98 +10.81 +6.22 | +19.24 +21.16 +21.22 +19.87 +21.05 |

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
then probed: at S1, +20.71 against +11.63. Treat the difference as a
protocol effect, not an error (see HANDOFF, open issues).

The canceller alone gives up about 4.5 dB (S1) and about 10 dB (S3) between
audible and runaway. With the shift the gap is 13–18 dB. Add the backing
track and the shifted chains become audible only at runaway.

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
| canceller, 2.7 ms | −0.25 | +0.21 | +0.29 | +0.29 | +0.29 |
| + 5 Hz, 2.7 ms | +12.67 | +13.13 | +13.21 | +13.21 | +13.21 |
| canceller, S1 | +10.38 | +11.47 | +11.55 | +11.63 | +11.63 |
| + 2 Hz, S1 | +15.65 | +16.75 | +16.83 | +16.90 | +16.82 |
| + 5 Hz, S1 | +16.88 | +18.07 | +17.44 | +18.05 | +17.52 |
| + aux, S1 | +14.69 | +15.78 | +15.86 | +15.94 | +15.94 |
| naive + 5 Hz, S1 | +4.93 | +2.69 | +5.93 | +6.01 | −0.59 |
| canceller, S3 | +16.51 | +18.19 | +18.43 | +18.14 | +18.31 |
| + 2 Hz, S3 | +15.55 | +17.58 | +17.02 | +16.30 | +16.55 |
| + 5 Hz, S3 | +15.02 | +17.05 | +16.93 | +17.00 | +16.90 |
| + aux, S3 | +16.43 | +18.46 | +18.69 | +18.85 | +18.93 |
| naive + 5 Hz, S3 | +5.53 | +7.56 | +7.79 | +7.95 | +8.03 |

What the table shows:

- **The old probe was too short.** The branch's 0.8 s probe over-reads the
  dry open loop by 0.55 dB (2.7 ms), 1.25 dB (S1) and 2.50 dB (S3). Like for
  like, the canceller rows' 0.8 s medians sit −2.50 to +2.88 dB from their
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
| cabin | +11.63 | +18.14 |
| studio | +10.15 | +17.71 |
| rehearsal | +11.11 | +17.10 |
| mt5 | +12.77 | +18.76 |
| mt9 | +11.72 | +17.08 |

On the speech-envelope material the cabin reads +20.24 (S1) and +20.77 (S3)
at 40 s. The S3 value is still drifting: the 20 s probe read +21.31.

## The frequency shift: an option, not the default

A single-sideband shift of the amplified signal (Schroeder 1964) moves every
partial by the same number of Hz. That decorrelates the loudspeaker signal
from the singer.

**The shifter.** Olli Niemitalo's 4+4 IIR allpass-pair Hilbert transformer
and a rotation, in `decorrelated_loop.h`.

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
| cabin | +6.68 | +5.10 | **−1.14** | **−1.93** |
| studio | +6.33 | +5.98 | +1.85 | +2.02 |
| rehearsal | +5.45 | +3.96 | +0.09 | +1.23 |
| mt5 | +5.01 | +5.98 | +1.85 | +1.41 |
| mt9 | +9.05 | +6.42 | +6.24 | +5.10 |

- At S1 the shift raises the runaway limit in every room.
- At S3 the direction depends on the room. The cabin loses (−1.41 at 40 s),
  rehearsal is about flat at 5 Hz, and the others gain. The S3 direction is
  recorded, not gated.

**Audible, worst case.** On the bare held note the shift *lowers* the
audible limit, well below the canceller alone: from +16.16 to +6.03 (2 Hz)
and +4.69 (5 Hz) at S1, and from +10.81 to +6.23 and +2.30 at S3. The
criterion flags the recirculating partials first. This is the worst case
described under the headline table.

One run carries an early event that is probably a detector artifact
(2 Hz, S1, seed 2):

- The criterion's −17.46 comes from events that only the dechirped passes
  found: 0.55 s at 5.23 kHz in the run's `.howl.json`.
- The plain long pass first fires at +6.79.
- The median is +6.03 either way.

**Recovery with the backing track.** With a white backing track at the
singer's level, the shifted chains are audible only at runaway: +20.54 and
+19.42 at S1, +22.56 and +22.46 at S3 (2 and 5 Hz).

The shift is **not free perceptually**. Partials move by equal Hz, not by
equal ratio, so a sung voice goes slightly inharmonic and detunes against an
unshifted backing track. That, together with the worst-case audible numbers
above, is why the listening test decides.

## The backing track is free excitation

A backing track is uncorrelated with the singer, so it excites the
identification for free. That works only when the canceller's reference is
tapped after the track is mixed into the speaker feed.

- **Runaway, cabin, 40 s.** The track raises the limit from +11.63 to +15.94
  (S1) and from +18.31 to +18.93 (S3).
- **Other rooms, 20 s.** S1: studio +13.57, rehearsal +11.99, mt5 +14.36,
  mt9 +14.09. S3: studio +15.95, rehearsal +14.73, mt5 +17.18, mt9 +17.26.
  At S3 that is below the canceller alone in studio, rehearsal and mt5.
- **Audible, cabin.** S3 is audible only at runaway (+24.03). S1 splits:
  four seeds are audible early (+1.64 to +6.63) and one only at runaway
  (+21.27), for a median of +3.90. Nothing explains that yet (HANDOFF, open
  issues).

## Delay modulation

The delay wobbles ±8 samples at 1.3 Hz, a gentler decorrelator with no pitch
artifact. Held note, 20 s probes, runaway ASG:

| Room | S1 | S3 |
|---|---|---|
| cabin | +12.33 | +17.35 |
| studio | +11.90 | +18.94 |
| rehearsal | +12.87 | +17.80 |
| mt5 | +12.33 | +18.32 |
| mt9 | +13.65 | +20.77 |

At S1 that is −0.44 to +1.93 dB over the canceller alone, comparing room
medians, and below the 5 Hz shift in every room. Its audible limit has not
been measured.

## Decorrelation does not replace PEM

The naive (un-prewhitened) NLMS core behind the same 5 Hz shift, 40 s
probes, against PEM + FD-Kalman behind the shift. Each PEM − naive cell is a
per-seed median of the chain limits:

| Room | S1 naive ASG | S1 PEM − naive | S3 naive ASG | S3 PEM − naive |
|---|---|---|---|---|
| cabin | −0.59 | +18.63 | +8.03 | +9.05 |
| studio | −5.68 | +22.24 | +2.14 | +17.49 |
| rehearsal | −15.00 | +30.94 | +7.60 | +10.72 |
| mt5 | −15.00 | +32.78 | +6.71 | +13.71 |
| mt9 | −15.00 | +33.49 | +7.24 | +15.73 |

- −15.00 is the probe floor.
- At S3 the naive core no longer runs away at the floor. PEM still leads by
  at least 9.05 dB everywhere.
- Decorrelation shrinks the bias term, and prewhitening removes what is left.

## Low-latency regression row (2.7 ms)

This is the branch's original setting, kept as a regression row. It is not a
PoC scenario. At 2.7 ms the held note is the wall: the canceller alone has no
useful gain, and a 5 Hz shift rescues it. Cabin, 40 s probes:

| Chain | Runaway ASG |
|---|---|
| canceller | +0.29 |
| + 2 Hz | +10.66 |
| + 5 Hz | +13.21 |
| + aux | +16.90 |
| naive + 5 Hz | −15.00, the floor |

The gated test reads canceller +0.00, 5 Hz +13.00 and PEM − naive +27.50
(5 s probes, 0.5 dB bisection).

On the raw (unbanded) cabin at 20 s the same row reads canceller −1.30 and
5 Hz +17.16. The branch's single-seed +0.6 → +18.4 came from that unbanded
path, measured against max|F| with 0.8 s probes. Band-limiting moves the
raw S1 and S3 numbers too: canceller +12.06 and +18.21, 5 Hz +19.79 and
+21.20.

## Reverb in the loop: deferred

The branch also carried `mutap::spectral_reverb`, a per-bin
frequency-domain reverb whose decay is shaped from the canceller's F̂. It
does not land with this work, for three reasons:

- Re-measured under these rules, the shaped decay lost to a flat decay
  everywhere.
- No reverb-with-canceller bisection converged by 40 s.
- The class also needs the FFT ABI tag.

It returns with phase 2's reverb-integration item. HANDOFF records what was
measured and the code's location.

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

## Provenance

The gated claims run in about 3 minutes on the Intel Mac (2:46 and 2:56 in
two runs):

- the shifter's image rejection and group delay;
- the 2.7 ms regression row;
- S1: canceller alone, and the shift raising runaway.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_KARAOKE_DUMP=ON
cmake --build build
build/tests/mutap_tests --gtest_filter='AfcDecorrelation.*'
```

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
