# Feedback cancellation for in-cabin karaoke

*Measured September 2026 in simulation, on the tests' portable-random
scenario (tap/MuTap#66): the cabin RIR fixture and four other rooms,
band-limited through a loudspeaker model. Host: macOS 15 x86_64, AppleClang,
Release. Nothing here has been heard in a real car. Every number below comes
from a gated test, the `MUTAP_SLOW` sweep, or the `karaoke_audible.py`
driver; [Provenance](#provenance) gives the exact commands.*

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
- Host: macOS 15 x86_64, AppleClang, Release (see [Hosts](#hosts)).

| Chain | S1 audible | S1 runaway (ramp) | S1 runaway (bisected, 40 s) | S3 audible | S3 runaway (ramp) | S3 runaway (bisected, 40 s) |
|---|---|---|---|---|---|---|
| dry (no canceller) | +0.18 | +1.08 | 0 (the reference) | +0.21 | +1.62 | 0 (the reference) |
| **canceller** | **+14.96** | +21.43 | +11.72 | **+11.65** | +21.86 | +17.87 |
| canceller + 2 Hz shift † | +5.50 | +19.65 | +14.79 | +7.25 | +20.32 | +16.99 |
| canceller + 5 Hz shift † | +1.65 | +19.49 | +17.26 | +1.30 | +18.26 | +16.90 |
| canceller + backing track (aux) | +3.59 | +23.01 | +14.53 | +23.79 ‡ | +24.53 | +16.82 |
| canceller + 2 Hz + backing track | +19.56 | +20.44 | — | +22.39 | +23.85 | — |
| canceller + 5 Hz + backing track | +19.18 | +19.95 | — | +22.16 | +22.58 | — |

Per seed:

| Chain | Delay | Audible, per seed | Runaway (ramp), per seed |
|---|---|---|---|
| canceller | S1 | +14.96 +15.55 +11.88 +14.69 +14.97 | +21.43 +21.55 +20.80 +21.53 +18.92 |
| canceller | S3 | +12.87 +11.65 +11.17 +13.09 +2.29 | +22.23 +19.35 +21.99 +21.53 +21.86 |

† **Worst case for the shift.** The held note is a perfectly periodic
synthetic note with a 40 dB floor between its harmonics. Partials that
recirculate through the shifter stand out against that floor, climbing by
the shift on each pass. The criterion counts them as audible, and that
decision is deliberate. Any claim about the shift on singing needs real sung
recordings and the ABX test.

‡ Median over four seeds. At seed 42 the criterion found no qualifying event
at all before the ramp ran away (at +24.53).

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
then probed: at S1, +21.43 against +11.72. Treat the difference as a
protocol effect, not an error (see HANDOFF, open issues).

The canceller alone gives up about 6.5 dB (S1) and about 10 dB (S3) between
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
| canceller, 2.7 ms | −1.66 | −1.19 | −1.11 | −1.11 | −1.11 |
| + 5 Hz, 2.7 ms | +13.54 | +14.01 | +14.09 | +13.56 | +14.09 |
| canceller, S1 | +10.47 | +11.56 | +11.64 | +11.72 | +11.72 |
| + 2 Hz, S1 | +13.54 | +14.64 | +14.72 | +14.79 | +14.79 |
| + 5 Hz, S1 | +16.01 | +17.10 | +17.00 | +17.26 | +17.26 |
| + aux, S1 | +13.28 | +14.38 | +14.45 | +14.53 | +14.53 |
| naive + 5 Hz, S1 | +3.96 | −0.48 | +3.64 | +3.63 | −15.00 |
| canceller, S3 | +16.60 | +18.28 | +18.69 | +18.67 | +17.87 |
| + 2 Hz, S3 | +15.63 | +17.14 | +16.32 | +16.74 | +16.99 |
| + 5 Hz, S3 | +14.40 | +16.52 | +16.76 | +16.83 | +16.90 |
| + aux, S3 | +14.32 | +16.35 | +16.58 | +16.74 | +16.82 |
| naive + 5 Hz, S3 | +5.97 | +8.00 | +8.23 | +8.39 | +8.47 |

What the table shows:

- **The old probe was too short.** The branch's 0.8 s probe over-reads the
  dry open loop by 0.55 dB (2.7 ms), 1.25 dB (S1) and 2.50 dB (S3). Like for
  like, the canceller rows' 0.8 s medians sit −2.50 to +2.35 dB from their
  40 s values: the held note reads low, the speech-envelope material reads
  high.
- **The gated S1 shift row needs 20 s.** Its 10 s median reads +17.00
  against +17.26 at 40 s, 0.26 dB off, so the rule picks 20 s.
- **One row never converges.** The naive core behind the shift at S1 does
  not settle with probe length.
- **The canceller at S3 still moves at 40 s** (+18.67 at 20 s, +17.87 at
  40 s). No S3 row is gated.
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

On the speech-envelope material the cabin reads +20.24 (S1) and +20.77 (S3)
at 40 s. The S3 value is still drifting: the 20 s probe read +21.48.

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
| cabin | +5.80 | +3.34 | **−1.23** | **−2.37** |
| studio | +5.62 | +6.15 | **−0.70** | +0.88 |
| rehearsal | +5.01 | +3.69 | +1.23 | **−0.26** |
| mt5 | +8.88 | +6.50 | +3.78 | +3.25 |
| mt9 | +8.26 | +6.59 | +3.43 | +3.16 |

- At S1 the shift raises the runaway limit in every room.
- At S3 the direction depends on the room. The 5 Hz shift loses in the
  cabin (−1.14 at 40 s) and studio and gains in rehearsal, mt5 and mt9; the
  2 Hz shift loses in the cabin and rehearsal. The S3 direction is recorded,
  not gated. (Before the portable variates the studio read +1.85 at 5 Hz and
  rehearsal +1.23 at 2 Hz: one of the rows that moves by host and scenario.)

**Audible, worst case.** On the bare held note the shift *lowers* the
audible limit, well below the canceller alone: from +14.96 to +5.50 (2 Hz)
and +1.65 (5 Hz) at S1, and from +11.65 to +7.25 and +1.30 at S3. The
criterion flags the recirculating partials first. This is the worst case
described under the headline table.

Some limits come from events only a dechirped pass found (the driver prints
where the plain long pass first fires beside each such row):

| Run | Criterion limit | First dechirp-only event | Plain long pass first fires |
|---|---|---|---|
| 5 Hz, S1, seed 42 | +1.27 | 0.51 s at 657–686 Hz, pass long@−250 | +7.24 |
| 5 Hz, S3, seed 2 | +4.48 | 0.94 s at 5.22–5.32 kHz | +5.06 |
| 5 Hz, S3, seed 82 | +1.30 | 0.62 s at 5.26–5.32 kHz, pass long@−250 | +5.59 |
| 2 Hz, S1, seed 82 | +5.50 | 0.58 s at 5.22–5.30 kHz, pass long@+500 | +8.11 |

Counting the plain long pass alone, the medians read +2.71 (5 Hz, S1),
+3.07 (5 Hz, S3) and +5.81 (2 Hz, S1): still far under the canceller alone.
The old seed-2 −17.46 dechirp-only event is gone. The earliest events now
are plain-pass events: 2 Hz at S1, seeds 2 and 22 (−9.27 and −9.09, a
0.60 s track at 5.55 kHz in seed 2), and 5 Hz at S3, seed 22 (−14.90).

**Recovery with the backing track.** With a white backing track at the
singer's level, the shifted chains are audible only at runaway: +19.56 and
+19.18 at S1, +22.39 and +22.16 at S3 (2 and 5 Hz).

The shift is **not free perceptually**. Partials move by equal Hz, not by
equal ratio, so a sung voice goes slightly inharmonic and detunes against an
unshifted backing track. That, together with the worst-case audible numbers
above, is why the listening test decides.

## The backing track is free excitation

A backing track is uncorrelated with the singer, so it excites the
identification for free. That works only when the canceller's reference is
tapped after the track is mixed into the speaker feed.

- **Runaway, cabin, 40 s.** The track raises the limit at S1, from +11.72
  to +14.53, and lowers it at S3, from +17.87 to +16.82.
- **Other rooms, 20 s.** S1: studio +13.57, rehearsal +12.08, mt5 +15.15,
  mt9 +14.53, above the canceller alone in every room. S3: studio +16.83,
  rehearsal +14.81, mt5 +16.90, mt9 +15.41, below the canceller alone in
  every room (the cabin too: +16.74 against +18.67).
- **Audible, cabin.** S3 is audible only at runaway (+23.79, four seeds; see
  ‡). S1 splits: four seeds are audible early (−0.54 to +4.20: +0.02, −0.54,
  +3.59, +4.20) and one only at runaway (+22.99), for a median of +3.59.
  Nothing explains that yet (HANDOFF, open issues).

## Delay modulation

The delay wobbles ±8 samples at 1.3 Hz, a gentler decorrelator with no pitch
artifact. Held note, 20 s probes, runaway ASG:

| Room | S1 | S3 |
|---|---|---|
| cabin | +13.83 | +17.18 |
| studio | +13.22 | +18.23 |
| rehearsal | +13.22 | +18.15 |
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
| cabin | −15.00 | +32.26 | +8.47 | +9.05 |
| studio | −10.96 | +27.42 | +6.27 | +12.39 |
| rehearsal | −14.47 | +30.41 | +5.49 | +12.04 |
| mt5 | −15.00 | +35.07 | +7.06 | +14.41 |
| mt9 | −15.00 | +35.16 | +6.88 | +14.77 |

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
| canceller | −1.11 |
| + 2 Hz | +10.75 |
| + 5 Hz | +14.09 |
| + aux | +16.99 |
| naive + 5 Hz | −15.00, the floor |

The gated test (5 s probes, 0.5 dB bisection) reads canceller −1.50, 5 Hz
+13.50 and PEM − naive +22.25 on this host; its comments quote the Linux
run of tap/MuTap#66 (−1.50 / +13.50 / +28.50).

On the raw (unbanded) cabin at 20 s the same row reads canceller +0.63 and
5 Hz +16.28. The branch's single-seed +0.6 → +18.4 came from that unbanded
path (on the earlier, library-dependent variates), measured against max|F|
with 0.8 s probes. Band-limiting moves the raw S1 and S3 numbers too:
unbanded, canceller +12.50 and +18.65, 5 Hz +19.71 and +20.59.

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

## Hosts

The numbers here were measured on macOS 15 x86_64 with AppleClang, Release,
at tap/MuTap 864e596. The portable variates (tap/MuTap#66,
`tests/support/portable_random.h`) make the *signals* identical on every
host: the rooms, the near ends and the backing track. The closed loop is
chaotic, though, so platform arithmetic (libm, contraction) still moves
single rows by dB while the medians agree in direction. The gated test's
comments were measured on Linux GCC 13.3 by #66, and this doc on macOS. The
2.7 ms row's naive core is the clearest example: four probe floors and
−14.61 on Linux, a median of −8.75 here; PEM − naive +28.50 on Linux, +22.25
here. Every gated claim passes on both.

## Provenance

The gated claims run in 3:47 on the Intel Mac:

- the shifter's image rejection and group delay;
- the 2.7 ms regression row;
- S1: canceller alone, and the shift raising runaway.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_KARAOKE_DUMP=ON
cmake --build build
build/tests/mutap_tests --gtest_filter='AfcDecorrelation.*'
```

The sweep (every bisected table above) takes 2:49:54 wall-clock on 6
threads of the 12-thread Intel Mac:

```sh
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=6 build/tests/mutap_tests \
    --gtest_filter='AfcDecorrelationSweep.*' --gtest_output=xml:sweep.xml
```

The audible table needs numpy and scipy. All seven configurations at the
five seeds, one invocation, took 25:12 with 6 workers:

```sh
python3 tools/notebook/karaoke_audible.py --dump build/tools/notebook/karaoke_ramp_dump \
    --work runs --jobs 6 --seeds 2,22,42,62,82 --json aud.json
```
