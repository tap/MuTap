# A reverb behind the canceller

*Measured October 2026 in simulation: the cabin RIR fixture and five other
rooms from both generator families, band-limited through a loudspeaker
model, and phase 0's four room fixtures at full length for the bare loop.
Host: macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release, on a shared
machine ([Hosts](#hosts)). Nothing here has been heard in a real room. Every
number below comes from `tests/test_reverb_stage.cpp`, its `MUTAP_SLOW`
sweep `tests/test_reverb_stage_sweep.cpp`, or the
`tools/notebook/reverb_audible.py` driver; [Provenance](#provenance) gives
the commands.*

A reverb in the voice bus sits inside the feedback loop: whatever it does
to the bus, it does to the howl. This document puts FAUST's Dattorro plate,
as shipped and with the paper's delay lengths, in `afc_chain`'s reverb slot
behind the PEM + FD-Kalman canceller, and measures what it costs, with the
probe lengths shown to converge first. It is the first half of the reverb
work; the spectral reverb shaped from the canceller's estimate comes back
on the same harness next (HANDOFF item 11).

## Headline

- **Behind the canceller the plate costs almost no runaway gain.** Held
  note, six rooms, the as-shipped plate at decay 0.5 to 0.85 and wet 0.15
  to 0.50: the largest median cost is +1.46 dB at S1 and none at S3 (at
  most +2.69 dB at matched loudness). Long decays at high wet even raise the
  runaway limit (S1, decay 0.85, w 0.50: −4.39 dB of cost). The bare loop,
  with no canceller, charges that same setting +5.79 dB by the magnitude
  bound (+6.25 exact, four fixtures at d = 480).
- **The audible limit is where the plate costs.** In the cabin at S1 the
  canceller alone is audible at +16.57 dB; with the plate at decay 0.5 /
  0.7 and wet 0.30, at +12.89 / +10.46, while the ramp's runaway does not
  move (+20.70 alone, +22.07 / +21.76 with the plate). At S3, wet 0.30,
  some seeds are audible below the dry loop's limit: a real, bounded,
  feedback-dependent effect at the plate's own modes, examined below (‡).
- **With a frequency shift, shifting the dry path only is audible later**
  than shifting the whole bus ahead of the plate: per-seed medians favour
  it in 13 of 16 decay × wet × shift × delay pairs, by up to +5.11 dB
  (5 Hz, decay 0.7, w 0.30, S3). Bisected runaway says the opposite (the
  whole-bus shift holds more in 22 of 24 room medians at S1; at S3 at 2 Hz
  only), so the topology is a listening and room question, not settled
  here.
- **Probes: the held note converges at 10 s, decay 0.85 included**; the
  speech envelope drifts and is reported, not gated.
- **The paper plate**, new in the bare-loop table: at equal decay it costs
  more than the plate as shipped (it rings longer); at equal T30 (paper 0.5
  against as-shipped 0.7, 2.12 s) it costs less by the bound, +0.30 against
  +1.11 dB at w 0.30. Behind the canceller, at decay 0.5, the two do not
  separate.

Every runaway and audible number is the held note's or the speech
envelope's on simulated rooms, double precision, on one host; read the
held-note shift rows as the karaoke document does, as a worst case for the
shift.

## The stage

`include/mutap/reverb_stage.h` puts any mono-in reverb engine in
`afc_chain`'s reverb slot (decorrelator → reverb → safety):

- **`tap::mu::reverb_mix<Sample, Reverb>`**: y = (1 − w)·x + w·r(x), where
  r is the reverb's mono return, output L or (L + R)/2
  (`reverb_return::left` / `mid`), and w the wet level in [0, 1].
- **`tap::mu::shifted_dry_mix<Sample, Shifter, Reverb>`**: y = (1 − w)·s(x)
  + w·r(x). The shifter sits on the dry path inside the stage and the reverb
  is fed the unshifted bus, so the reverb's tail never passes through the
  shifter. It goes in the reverb slot with the decorrelator slot empty.

`Reverb` is any type with `process(const Sample* const* in, Sample* const*
out, int n) noexcept`, one input and N ≥ 1 outputs: the shape of the FAUST
shim's `mutap_faust::faust_block`. The FAUST-generated plates stay in
`third_party/faust/`; the header knows only that signature. The stages do
not own the reverb or the shifter. Both are header-only, hold no FFT (no ABI
tag), report `latency()` 0, allocate their return buffers (channels × block)
at construction, and are `noexcept` and allocation-free afterwards, float32
included. `set_wet()` and `set_return_mode()` change the mix between blocks.
`tests/test_reverb_mix.cpp` checks the plumbing on every target, both
emulated selections included, with a stand-in reverb.

The mix is a crossfade, so it lowers the bus level. For white input the
level change is 10·log10 of the energy of (1 − w)·δ + w·r. For the vendored
plates (L return, damping 0.0005; `ReverbStage.PlateDecayAndMixLevel`):

| decay | w 0.15 | w 0.30 | w 0.50 |
|---|---|---|---|
| 0.3 | −0.963 | −1.721 | −2.135 |
| 0.5 | −0.956 | −1.687 | −2.033 |
| 0.7 | −0.925 | −1.546 | −1.622 |
| 0.85 | −0.833 | −1.134 | −0.547 |

Both plates give the same values to the printed digit. A loop measured at
equal forward gain with and without the mix therefore includes this level
change; the tables below give the cost both raw and at matched loudness
(raw cost minus the level change).

## The two plates

Both are vendored in `third_party/faust/` (its README has the details):
`dattorro` is FAUST's `re.dattorro_rev` as shipped (the source of the
`dattorro~` Max external), and `dattorro_paper` uses the paper's delay
lengths with the tank bug fixed. At equal `decay` the paper plate rings
longer. T30 by Schroeder integration, −5 to −35 dB, L / (L + R)/2, damping
0.0005 (`ReverbStage.PlateDecayAndMixLevel`):

| decay | as shipped | paper lengths |
|---|---|---|
| 0.3 | 1.0144 / 0.8620 s | 1.7252 / 1.5975 s |
| 0.5 | 1.1846 / 1.3082 s | 2.1186 / 2.3626 s |
| 0.7 | 2.1173 / 2.1530 s | 3.7882 / 3.8598 s |
| 0.85 | 4.6220 / 4.5825 s | 8.2457 / 8.1939 s |

The paper plate at decay 0.5 has the as-shipped plate's decay-0.7 T30 (L
2.1186 against 2.1173 s).

## How the loop is measured

The karaoke suites' loop and protocol ([karaoke-afc.md](karaoke-afc.md),
"How it is measured"), with the reverb in the chain's slot:

- **Loop.** `tests/support/decorrelated_loop.h`: mic → canceller → forward
  delay → stage → gain → speaker → room. The stage is
  `decorrelated_loop`'s new forward stage, run on each block after the
  forward delay and before the gain: the chain's reverb slot. Every loop
  built with a stage resets it, so each bisection probe starts the plate
  from silence, as it starts the loop's own history from zero.
  `tests/support/reverb_rig.h` builds the stage: `reverb_mix` over a
  vendored plate, the library's `frequency_shifter` then `reverb_mix` (the
  whole-bus shift), `shifted_dry_mix` (the dry-only shift), or the shifter
  alone. Every shift row in this document uses the library's shifter (its
  recursive oscillator); the karaoke suites' shift rows use the loop's
  exact-ramp oscillator, with the same Hilbert pair.
- **Rooms.** Six band-limited 1024-tap rooms: the image-source fixtures
  cabin, studio, rehearsal and hall, and the `random_decaying_rir` rooms mt5
  and mt9 (both generator families). S1 is a 480-sample forward delay
  (10 ms), S3 960 (20 ms).
- **Materials.** The held 300 Hz note (`voiced_near_end`, 160-sample
  period) and the speech-envelope material (`ar_near_end`).
- **Bisection.** `tests/support/karaoke_asg.h`: PEM + FD-Kalman (speech
  cascade, default configuration) converges 1500 blocks at `exact_msg_db`
  − 6 dB **with the reverb in the loop**; then the forward gain is bisected
  to 0.1 dB over [exact − 20, exact + 30] with the 40 dB runaway rule on a
  second seed of the material, a fresh copy of the converged canceller and
  a silent plate per probe. Five seed sets (2, 22, 42, 62, 82).
- **Like for like.**
  - ASG = the chain's limit − the dry open loop's limit (no canceller, no
    reverb), bisected on the same probe material and length.
  - Cost = the canceller-alone limit − the canceller-plus-reverb limit, per
    seed; positive means the reverb costs stable gain.
  - Matched = cost − the mix's level change (the table in
    [The stage](#the-stage)): the cost had the mix been made as loud as the
    dry bus. The mix is quieter, so matched is the larger.
- **Medians.** Per room, the median of the five seed sets; across rooms,
  the median of the six per-room medians, with the per-room range and the
  per-seed range over all 30 runs beside it. The loop is chaotic, so single
  seeds move by dB; read directions, not single magnitudes.

## Probe convergence first

Phase 0 needed 384 s probes before a dry loop with the plate in it
converged (4.6 s tail), and on the karaoke branch the spectral reverb's
in-loop bisection was still moving at 40 s. So before any in-loop number,
`ReverbStageSweep.ProbeConvergence` bisects the cabin and mt5 at S1, wet
0.30, with probes of 10, 20, 40, 80 and 160 s. **Converged** means the
shortest probe from which every longer probe's five-seed median ASG lies
within 0.25 dB of the row's longest probe. Median ASG, dB:

| Room, material | Row (wet 0.30, damping 0.0005, L) | 10 s | 20 s | 40 s | 80 s | 160 s | Converged |
|---|---|---|---|---|---|---|---|
| cabin, held | canceller alone | +11.66 | +11.74 | +11.74 | +11.74 | | 10 s |
| cabin, held | as shipped, decay 0.5 | +10.98 | +11.05 | +11.05 | +11.05 | +11.05 | 10 s |
| cabin, held | as shipped, decay 0.7 | +10.98 | +11.05 | +11.05 | +11.05 | +11.05 | 10 s |
| cabin, held | as shipped, decay 0.85 | +13.71 | +13.79 | +13.79 | +13.79 | +13.79 | 10 s |
| cabin, held | paper, decay 0.5 | +11.17 | +11.25 | +11.25 | +11.25 | +11.25 | 10 s |
| cabin, held | decay 0.7 + 5 Hz, whole bus | +18.59 | +18.67 | +18.67 | +18.67 | | 10 s |
| cabin, held | decay 0.7 + 5 Hz, dry only | +17.03 | +17.11 | +17.11 | +17.11 | | 10 s |
| mt5, held | canceller alone | +11.05 | +11.05 | +11.05 | +11.05 | | 10 s |
| mt5, held | as shipped, decay 0.5 | +11.56 | +11.64 | +11.64 | +11.64 | +11.64 | 10 s |
| mt5, held | as shipped, decay 0.7 | +12.64 | +12.71 | +12.71 | +12.71 | +12.71 | 10 s |
| mt5, held | as shipped, decay 0.85 | +12.44 | +12.52 | +12.52 | +12.52 | +12.52 | 10 s |
| mt5, held | paper, decay 0.5 | +11.35 | +11.35 | +11.35 | +11.35 | +11.35 | 10 s |
| mt5, held | decay 0.7 + 5 Hz, whole bus | +18.50 | +18.57 | +18.57 | +18.57 | | 10 s |
| mt5, held | decay 0.7 + 5 Hz, dry only | +17.79 | +17.79 | +17.79 | +17.79 | | 10 s |
| cabin, speech | canceller alone | +21.02 | +20.82 | +20.23 | +19.75 | | **not converged** |
| cabin, speech | as shipped, decay 0.5 | +21.70 | +21.80 | +21.31 | +21.31 | +20.82 | **not converged** |
| cabin, speech | as shipped, decay 0.7 | +21.41 | +21.60 | +21.21 | +20.72 | +20.82 | 80 s |
| cabin, speech | as shipped, decay 0.85 | +20.53 | +20.53 | +20.33 | +20.04 | +19.94 | 80 s |
| mt5, speech | canceller alone | +23.26 | +23.16 | +22.87 | +22.68 | | 40 s |
| mt5, speech | as shipped, decay 0.5 | +24.53 | +24.73 | +24.14 | +24.24 | +24.14 | 40 s |
| mt5, speech | as shipped, decay 0.7 | +24.73 | +24.73 | +23.95 | +24.14 | +23.85 | **not converged** |
| mt5, speech | as shipped, decay 0.85 | +24.04 | +23.65 | +23.65 | +23.55 | +23.36 | 80 s |

The canceller-alone rows and the shift rows ran to 80 s, the rest to 160 s.

- **The held note converges at 10 s, decay 0.85 included.** Every held-note
  row reads the same at 20, 40, 80 and 160 s, seed by seed. With the
  canceller in the loop, the held note either runs away within the first
  20 s of a probe or not at all. Phase 0's 384 s was the dry loop with the
  plate, no canceller; it does not carry over.
- **The speech envelope drifts down with probe length** and three of its
  eight rows had not converged by their longest probe: the canceller alone
  in the cabin moves 1.27 dB from 10 to 80 s. The karaoke suites saw the
  same drift on this material. No speech-envelope row is gated; the grid
  runs it on 80 s probes and reports it.
- The plate's cost converges faster than either limit: on speech it reads
  between −0.10 and −1.86 dB at every probe, the plate raising the limit.

The grid and the shift rows therefore run the held note on 20 s probes,
twice the converged length, and the gated rows on 10 s.

## What the plate costs behind the canceller (runaway)

`ReverbStageSweep.Grid`, six rooms, five seed sets each. Each cell is the
median over the six rooms of each room's five-seed median, with [the
range of the room medians] and {the per-seed range over all 30 runs}.
Cost is positive when the plate lowers the stable gain; matched is cost
minus the mix's level change (from [The stage](#the-stage)). Damping
0.0005 and the L return unless the row says otherwise.

**Held note, S1 (10 ms), 20 s probes.** The canceller alone: ASG +11.74
[+11.05, +12.42] {+7.73, +13.98}.

| Plate, decay, wet | ASG | Cost [rooms] {seeds} | Mix level | Matched |
|---|---|---|---|---|
| as shipped 0.5, w 0.15 | +10.98 | +0.49 [−0.39, +2.05] {−4.00, +5.76} | −0.96 | +1.44 |
| as shipped 0.5, w 0.30 | +11.46 | +0.88 [−0.78, +2.44] {−4.30, +4.20} | −1.69 | +2.57 |
| as shipped 0.5, w 0.50 | +12.62 | −1.07 [−1.66, −0.39] {−4.30, +2.15} | −2.03 | +0.96 |
| as shipped 0.7, w 0.15 | +10.68 | +1.46 [−0.20, +1.76] {−3.91, +3.32} | −0.93 | +2.39 |
| as shipped 0.7, w 0.30 | +11.45 | +0.39 [−0.29, +0.68] {−4.00, +3.81} | −1.55 | +1.94 |
| as shipped 0.7, w 0.50 | +14.49 | −2.15 [−3.91, −1.86] {−4.98, +0.00} | −1.62 | −0.53 |
| as shipped 0.85, w 0.15 | +12.03 | −0.20 [−1.17, +2.34] {−4.30, +3.42} | −0.83 | +0.64 |
| as shipped 0.85, w 0.30 | +13.32 | −1.46 [−2.05, −0.49] {−6.35, +3.03} | −1.13 | −0.33 |
| as shipped 0.85, w 0.50 | +16.33 | −4.39 [−6.15, −3.52] {−8.50, −1.27} | −0.55 | −3.85 |
| paper 0.5, w 0.15 | +11.35 | +0.59 [+0.10, +1.46] {−4.30, +3.61} | −0.96 | +1.54 |
| paper 0.5, w 0.30 | +11.35 | +0.29 [−0.29, +1.46] {−4.79, +3.71} | −1.69 | +1.98 |
| paper 0.5, w 0.50 | +13.79 | −1.56 [−2.73, −1.07] {−6.35, +0.29} | −2.03 | +0.47 |
| as shipped 0.5, w 0.30, damping 0.5 | +11.84 | +0.39 [−0.78, +0.59] {−3.52, +3.91} | −1.71 | +2.10 |
| as shipped 0.5, w 0.30, (L + R)/2 | +11.54 | +0.29 [−0.78, +1.07] {−4.30, +5.08} | −1.70 | +2.00 |
| as shipped 0.7, w 0.30, damping 0.5 | +11.54 | +0.29 [−1.07, +0.49] {−4.20, +3.42} | −1.67 | +1.96 |
| as shipped 0.7, w 0.30, (L + R)/2 | +11.46 | +1.07 [−0.29, +1.56] {−5.86, +3.61} | −1.62 | +2.69 |
| as shipped 0.85, w 0.30, damping 0.5 | +13.20 | −1.37 [−2.15, +0.49] {−6.05, +2.73} | −1.58 | +0.21 |
| as shipped 0.85, w 0.30, (L + R)/2 | +12.81 | −1.17 [−3.12, −0.49] {−5.66, +1.27} | −1.32 | +0.15 |

**Held note, S3 (20 ms), 20 s probes.** The canceller alone: ASG +18.40
[+17.25, +18.59] {+16.46, +19.28}.

| Plate, decay, wet | ASG | Cost [rooms] {seeds} | Mix level | Matched |
|---|---|---|---|---|
| as shipped 0.5, w 0.15 | +18.79 | −0.59 [−1.76, +1.17] {−3.22, +3.71} | −0.96 | +0.37 |
| as shipped 0.5, w 0.30 | +18.87 | −1.17 [−2.15, +0.49] {−4.20, +1.56} | −1.69 | +0.51 |
| as shipped 0.5, w 0.50 | +19.84 | −1.76 [−3.22, −1.17] {−4.39, +0.29} | −2.03 | +0.28 |
| as shipped 0.7, w 0.15 | +18.79 | −0.59 [−0.98, +0.20] {−3.71, +2.73} | −0.93 | +0.34 |
| as shipped 0.7, w 0.30 | +19.36 | −0.39 [−2.15, +0.10] {−4.10, +1.37} | −1.55 | +1.16 |
| as shipped 0.7, w 0.50 | +20.92 | −1.66 [−4.10, −0.68] {−4.49, +0.29} | −1.62 | −0.04 |
| as shipped 0.85, w 0.15 | +18.98 | −0.59 [−1.76, +0.68] {−2.64, +3.03} | −0.83 | +0.25 |
| as shipped 0.85, w 0.30 | +19.36 | −0.98 [−2.05, −0.39] {−3.61, +1.66} | −1.13 | +0.16 |
| as shipped 0.85, w 0.50 | +19.38 | −0.39 [−3.03, +0.98] {−5.27, +1.66} | −0.55 | +0.16 |
| paper 0.5, w 0.15 | +18.79 | −0.68 [−1.07, −0.10] {−2.83, +1.66} | −0.96 | +0.27 |
| paper 0.5, w 0.30 | +18.87 | −0.39 [−1.46, +0.68] {−3.32, +3.12} | −1.69 | +1.30 |
| paper 0.5, w 0.50 | +19.59 | −1.56 [−2.44, −0.29] {−4.98, +1.46} | −2.03 | +0.47 |
| as shipped 0.5, w 0.30, damping 0.5 | +18.30 | −0.10 [−1.37, +1.17] {−3.03, +2.25} | −1.71 | +1.62 |
| as shipped 0.5, w 0.30, (L + R)/2 | +19.06 | −0.49 [−1.17, +0.00] {−3.61, +2.54} | −1.70 | +1.22 |
| as shipped 0.7, w 0.30, damping 0.5 | +19.55 | −1.27 [−2.83, +0.59] {−3.81, +2.73} | −1.67 | +0.40 |
| as shipped 0.7, w 0.30, (L + R)/2 | +18.87 | −0.20 [−1.95, +0.29] {−4.49, +3.22} | −1.62 | +1.42 |
| as shipped 0.85, w 0.30, damping 0.5 | +19.65 | −1.46 [−1.95, −0.29] {−4.20, +1.27} | −1.58 | +0.11 |
| as shipped 0.85, w 0.30, (L + R)/2 | +19.28 | −0.98 [−3.22, −0.29] {−4.20, +2.44} | −1.32 | +0.34 |

**Speech envelope, S1, 80 s probes** (not converged in every room; read as
directions). The canceller alone: ASG +19.75 [+18.48, +22.68] {+17.70,
+22.87}.

| Plate, decay, wet | ASG | Cost [rooms] {seeds} | Mix level | Matched |
|---|---|---|---|---|
| as shipped 0.5, w 0.30 | +21.31 | −1.17 [−1.86, −0.68] {−2.44, +0.20} | −1.69 | +0.51 |
| as shipped 0.7, w 0.30 | +21.21 | −0.88 [−1.66, −0.78] {−1.86, +0.20} | −1.55 | +0.67 |
| as shipped 0.85, w 0.30 | +20.53 | −0.29 [−0.78, +0.10] {−1.95, +0.78} | −1.13 | +0.84 |

What the grid says:

- **Behind the canceller the plate costs little runaway gain, at any decay
  measured.** The largest raw cost, median over the rooms, is +1.46 dB
  (S1, held, decay 0.7, w 0.15), and +2.69 dB at matched loudness (S1,
  held, decay 0.7, w 0.30, (L + R)/2). At S3 no raw cost is positive.
  Where the bare loop (no canceller, the four fixtures at 4096 taps)
  charges decay 0.85 at w 0.30 +3.71 dB by the bound, the canceller-held
  loop reads −1.46 (S1).
- **Long decays and high wet raise the runaway limit on the held note.**
  At S1, decay 0.85, w 0.50 the chain holds 4.39 dB more than the
  canceller alone (−3.85 at matched loudness), in every room ({−8.50,
  −1.27} per seed). Runaway is not the whole story: the audible rows below
  fall as wet and decay rise.
- **Damping, the return and the plate do not separate.** At w 0.30,
  damping 0.5 against 0.0005 moves the cost by −0.49 / −0.10 / +0.09 dB at
  S1 (decay 0.5 / 0.7 / 0.85) and +1.07 / −0.88 / −0.48 at S3; the
  (L + R)/2 return against L by −0.59 / +0.68 / +0.29 at S1. The paper
  plate at decay 0.5 sits within 0.78 dB of the as-shipped plate at every
  wet and delay. Every one of these differences is smaller than the
  per-room ranges of the two rows it compares.
- **Per-seed spread dominates:** single seeds run from −8.50 to +5.76 dB
  of cost on the held note. The gated row
  (`ReverbStage.CancellerHoldsBehindThePlates`, cabin, S1) bounds the
  median cost at +3.5 dB.

## Shift and reverb

With a frequency shift in the decorrelator slot and the plate behind it,
the plate's tail goes round the loop through the shifter and climbs by the
shift on every pass. `shifted_dry_mix` is the alternative: the shift on the
dry path only, the plate fed the unshifted bus. Both are measured here
against the shift alone, all on the library's shifter.

### Runaway (bisected), six rooms

`ReverbStageSweep.Shift`: held note, 20 s probes, the as-shipped plate at
wet 0.30. ASG is the median of the six rooms' five-seed medians, [the range
of the room medians]; whole-bus − dry-only is the per-seed difference of
the two chain limits, its median per room, then the median of those, with
the count of rooms where it is negative:

| S1 | 2 Hz | 5 Hz |
|---|---|---|
| canceller alone | +11.74 [+11.05, +12.42] | (same) |
| shift alone | +17.50 [+15.18, +19.06] | +17.23 [+15.37, +20.53] |
| plate alone, decay 0.5 | +11.46 [+9.98, +11.86] | (same) |
| whole bus + plate 0.5 | +17.52 [+16.82, +19.75] | +19.36 [+16.54, +19.65] |
| dry only + plate 0.5 | +14.10 [+13.01, +15.25] | +16.91 [+15.84, +18.28] |
| whole bus − dry only, decay 0.5 | +4.49 (0 of 6 rooms < 0) | +1.46 (0 of 6) |
| plate alone, decay 0.7 | +11.45 [+10.66, +12.71] | (same) |
| whole bus + plate 0.7 | +17.99 [+16.74, +19.45] | +18.57 [+14.88, +18.77] |
| dry only + plate 0.7 | +15.45 [+13.91, +16.23] | +17.11 [+16.15, +18.77] |
| whole bus − dry only, decay 0.7 | +3.42 (0 of 6) | +0.68 (1 of 6; −0.98) |

| S3 | 2 Hz | 5 Hz |
|---|---|---|
| canceller alone | +18.40 [+17.25, +18.59] | (same) |
| shift alone | +19.38 [+16.74, +21.60] | +18.89 [+17.52, +21.31] |
| plate alone, decay 0.5 | +18.87 [+18.30, +20.25] | (same) |
| whole bus + plate 0.5 | +20.45 [+18.89, +22.58] | +20.33 [+19.00, +21.80] |
| dry only + plate 0.5 | +18.18 [+17.23, +19.08] | +20.16 [+18.98, +21.21] |
| whole bus − dry only, decay 0.5 | +1.95 (0 of 6) | +0.20 (3 of 6) |
| plate alone, decay 0.7 | +19.36 [+17.73, +19.75] | (same) |
| whole bus + plate 0.7 | +20.35 [+19.20, +22.38] | +20.25 [+19.00, +21.70] |
| dry only + plate 0.7 | +19.67 [+18.40, +20.04] | +20.45 [+18.32, +22.29] |
| whole bus − dry only, decay 0.7 | +1.17 (1 of 6) | +0.68 (3 of 6) |

- **By runaway the whole-bus shift holds more than the dry-only shift at
  S1**: its room medians are positive in 22 of 24, zero in one and
  negative in one (−0.98, 5 Hz, decay 0.7). At S3 the same holds at 2 Hz
  (11 of 12 positive); at 5 Hz the two do not separate (3 of 6 rooms each
  way at both decays). The dry-only shift leaves the wet path
  (gain 0.30) unshifted and gives back part of the shift's runaway gain:
  at 2 Hz it reads +14.10 / +15.45 against the shift alone's +17.50.
- **By the audible limit it is the other way round** (next section): on
  the cabin ramp the dry-only shift is audible later in 13 of 16 pairs.
  The two measurements differ in protocol as well as criterion (a converged
  canceller bisected against a canceller adapting from a cold start under
  a ramp; karaoke-afc.md, "Ramp vs bisected runaway"), and the ramp's own
  runaway gain also favours the dry-only shift (16 of 16). Which one a room
  shows is phase 1's to measure.
- `ReverbStage.WholeBusShiftHoldsMoreRunawayThanDryOnly` gates the
  direction in the cabin at 2 Hz, decay 0.5 (per-seed median +3.09 dB).

### Audible limits: the product-chain candidates

The audible limit on the canceller output c, by the offline howl criterion
under the protocol's ramp (30 s warm-up 20 dB under the open-loop limit,
then +1 dB per 2 s; the live canceller from a cold start), with
PROTOCOL.md §7.3's flags: `--rt60 0.0328` (the band-limited cabin) on every
row, `--chain-rt60` the plate's T30 (1.1846 s at decay 0.5, 2.1167 s at 0.7,
from `measure_rir.py`'s `schroeder()`) on every row with the plate, and
`--dechirp` on every row with a shift. The reverb-only rows are analysed on
the chain output (after the plate, before the gain), with c as the
cross-check. The cabin, held note, the as-shipped plate, the library's
shifter, five seeds; dB over the dry loop's `exact_msg_db` (S1 −6.8967,
S3 −6.8603); medians (`tools/notebook/reverb_audible.py`):

| Chain | S1 audible | S1 runaway (ramp) | S3 audible | S3 runaway (ramp) |
|---|---|---|---|---|
| dry (no canceller) | +0.18 | +1.08 | +0.21 | +1.62 |
| canceller | +16.57 | +20.70 | +12.37 | +21.37 |
| + 2 Hz † | +4.03 | +20.13 | +6.50 | +18.73 |
| + 5 Hz † | +4.43 | +19.39 | +2.18 | +19.49 |
| + plate 0.5, w 0.15 | +16.53 | +21.46 | +10.38 | +23.01 |
| + plate 0.5, w 0.30 | +12.89 | +22.07 | −4.77 ‡ | +23.31 |
| + plate 0.7, w 0.15 | +13.53 | +20.89 | +11.44 | +23.18 |
| + plate 0.7, w 0.30 | +10.46 | +21.76 | +3.62 ‡ | +24.19 |
| + 2 Hz whole bus + plate 0.5, w 0.15 † | +5.92 | +20.71 | +6.77 | +20.68 |
| + 2 Hz dry only + plate 0.5, w 0.15 † | +8.55 | +21.31 | +7.90 | +21.63 |
| + 5 Hz whole bus + plate 0.5, w 0.15 † | +6.66 | +20.93 | +2.95 | +21.01 |
| + 5 Hz dry only + plate 0.5, w 0.15 † | +6.48 | +21.28 | +3.53 | +22.24 |
| + 2 Hz whole bus + plate 0.5, w 0.30 † | +7.53 | +21.70 | +7.74 | +22.62 |
| + 2 Hz dry only + plate 0.5, w 0.30 † | +8.51 | +22.24 | +8.98 | +22.62 |
| + 5 Hz whole bus + plate 0.5, w 0.30 † | +6.43 | +22.12 | +5.71 | +22.91 |
| + 5 Hz dry only + plate 0.5, w 0.30 † | +8.06 | +22.66 | +7.10 | +23.55 |
| + 2 Hz whole bus + plate 0.7, w 0.15 † | +6.62 | +20.85 | +7.50 | +18.99 |
| + 2 Hz dry only + plate 0.7, w 0.15 † | +8.71 | +20.85 | +8.68 | +22.12 |
| + 5 Hz whole bus + plate 0.7, w 0.15 † | +6.93 | +20.45 | +3.12 | +20.26 |
| + 5 Hz dry only + plate 0.7, w 0.15 † | +7.13 | +21.75 | +4.45 | +21.12 |
| + 2 Hz whole bus + plate 0.7, w 0.30 † | +8.87 | +20.47 | +6.74 | +21.31 |
| + 2 Hz dry only + plate 0.7, w 0.30 † | +7.69 | +21.47 | +6.19 | +22.67 |
| + 5 Hz whole bus + plate 0.7, w 0.30 † | +5.73 | +21.67 | +4.63 | +22.15 |
| + 5 Hz dry only + plate 0.7, w 0.30 † | +10.30 | +22.62 | +9.46 | +24.42 |

† **Worst case for the shift**, as in the karaoke document: the held note is
a perfectly periodic synthetic note with a 40 dB floor between its
harmonics, so partials recirculating through the shifter stand out.

‡ The reverb-only rows at S3, wet 0.30, are audible below the dry loop's
limit in some seeds: per seed −7.89 −0.17 +1.89 −4.77 −13.55 at decay 0.5
(c cross-check −2.48 −0.18 +1.90 +6.39 −13.55) and +4.56 +6.94 −7.15 +3.62
−2.56 at 0.7. Their runaway limits are the highest in the table. Examined
(same host, same seeds, cabin, held note; the controls re-ran the ramp dump
and `reverb_audible.py` with the §7.3 flags): what the criterion flags is a
sustained narrowband line at one of the plate's own strongest low modes —
93.8 Hz or 832 Hz in 7 of the 10 rows, both within 3 dB of the plate's
impulse-response peak below 2 kHz (164.7 Hz) — 26–31 dB over the envelope
for 0.5–0.8 s, first crossing at a forward gain 0.1–20.4 dB below the dry
limit. It needs the loop: with the feedback path open (gain held at −300 dB
for 120 s) the plate on the held note or on speech produces zero flagged
tracks in 20 of 20 runs; the canceller alone at S3 on the same seeds reads
+10.75 / +12.37 (sane); the same plate at S1 reads +10.14 on both the chain
output and c. It does not run away: the bisected cost at these settings is
≤ −0.10 dB and the ramp's runaway is the highest in the table, so the lines
stay bounded 20–30 dB over the envelope rather than growing to the 40 dB
rule. In 3 of 10 rows the chain output and c disagree by more than 5 dB on
different lines (the cross-check working as intended; no alignment warning).
So the bisected cost and the audible limit answer different questions in
this cell: by the broadband measure the plate is free at S3; by the
criterion it sets an earlier, seed-dependent limit at its own modes. The
mechanism that lets a loop 10–20 dB under its limit sustain a 30 dB line at
a plate mode was not identified (the canceller is in the loop; the effect
is absent without the plate and absent at S1). Read the per-seed range,
not the median, for this cell; whether it is audible on real singing is a
phase 1 question. Full table: the investigation's record.

What the audible rows say:

- **The dry-only shift does better than the whole-bus shift.** Per seed,
  dry-only minus whole-bus, the median is positive in 13 of the 16
  shift + plate pairs for the audible limit and in 16 of 16 for the ramp's
  runaway; the three exceptions are 2 Hz at decay 0.7, wet 0.30 (S1 −1.97,
  S3 −0.41) and 5 Hz at decay 0.5, wet 0.15, S1 (−0.18). The largest gains
  are at 5 Hz, decay 0.7, wet 0.30: +4.34 (S1) and +5.11 (S3).
- **The plate alone costs the canceller audible gain as wet and decay
  rise:** at S1 +16.53 / +12.89 at decay 0.5 (w 0.15 / 0.30) and +13.53 /
  +10.46 at 0.7, against the canceller's +16.57. Its runaway limit does not
  fall (+20.89 to +22.07 against +20.70).
- **Adding the plate to the shift does not lower the audible limit.** The
  shift + plate rows read +5.73 to +10.30 at S1 against +4.03 / +4.43 for
  the shift alone, but they are judged with the plate's T30 in the
  criterion's hold and the shift-alone rows are not. Re-analysed with the
  same hold (`--control`), the shift alone reads +5.84 / +6.10 at 2 Hz and
  +5.28 / +5.37 at 5 Hz (S1; 1.185 / 2.117 s in the hold), and at S3 +6.50
  and +2.18 (unchanged).
- Seed spread is large: with the canceller in, per-seed audible − runaway
  ranges from −2.53 to −36.79 dB, and a single seed reads up to 21.60 dB
  from its row's median (2 Hz alone, S1, seed 82: −17.57 against +4.03).

## The bare loop: what the plate costs without a canceller

Phase 0's method, reproduced for both plates (`ReverbStageSweep.BareLoopCost`):
the loop K·e^{−jωd}·H(ω)·F(ω) with H = (1 − w) + w·R the mix and F one of
the four room fixtures at its full 4096 taps (studio, rehearsal, hall,
cabin; not band-limited), d = 480 samples (S1). Each cell is the cost in
open-loop stable gain, dry limit minus mix limit, positive = less stable
gain, as **magnitude bound / exact Nyquist crossing**: the bound
(−20·log10 max|HF|) holds under any delay; the exact limit is the first K at
which K·L(ω) = 1 at d = 480. Median over the four rooms (the mean of the
middle two, as phase 0); L return. "Matched" is the w 0.30 bound minus the
mix's level change at w 0.30. Spectra on an rfft grid of 2N, N =
nextpow2(4·(IR length + 4095 + 480)); over all 144 cells (both plates, both
returns) the cost moves at most 0.0309 dB (bound) and 0.0693 dB (exact)
from N to 2N.

As shipped (reproduces phase 0's table to the printed digit):

| decay (T30 L) | damping | w 0.15 | w 0.30 | w 0.50 | matched (w 0.30) |
|---|---|---|---|---|---|
| 0.3 (1.01 s) | 0.0005 | −0.16 / −0.56 | −0.21 / −0.83 | −0.16 / −0.66 | +1.51 |
| 0.5 (1.18 s) | 0.0005 | +0.01 / −0.62 | +0.12 / −0.60 | +0.42 / −0.09 | +1.81 |
| 0.5 | 0.5 | −0.09 / −0.67 | −0.09 / −0.83 | +0.03 / −0.54 | +1.63 |
| 0.7 (2.12 s) | 0.0005 | +0.46 / −0.28 | +1.11 / +0.45 | +2.17 / +1.85 | +2.65 |
| 0.7 | 0.5 | +0.03 / −0.74 | +0.18 / −0.67 | +0.96 / −0.07 | +1.85 |
| 0.85 (4.62 s) | 0.0005 | +1.78 / +0.65 | +3.71 / +2.76 | +5.79 / +6.25 | +4.84 |
| 0.85 | 0.5 | +0.50 / −0.55 | +2.22 / +1.18 | +4.26 / +3.12 | +3.79 |

**Paper lengths (`dattorro_paper`), new:**

| decay (T30 L) | damping | w 0.15 | w 0.30 | w 0.50 | matched (w 0.30) |
|---|---|---|---|---|---|
| 0.3 (1.73 s) | 0.0005 | −0.21 / −0.50 | −0.27 / −0.51 | −0.20 / −0.41 | +1.45 |
| 0.5 (2.12 s) | 0.0005 | +0.08 / −0.31 | +0.30 / +0.06 | +0.74 / +0.43 | +1.99 |
| 0.5 | 0.2 | −0.03 / −0.34 | +0.09 / −0.01 | +0.37 / +0.38 | +1.79 |
| 0.5 | 0.5 | −0.16 / −0.37 | −0.18 / −0.16 | −0.00 / +0.15 | +1.53 |
| 0.7 (3.79 s) | 0.0005 | +0.76 / +0.38 | +1.61 / +1.14 | +2.77 / +2.42 | +3.16 |
| 0.7 | 0.2 | +0.34 / +0.22 | +0.80 / +0.87 | +1.53 / +1.99 | +2.42 |
| 0.7 | 0.5 | −0.02 / −0.04 | +0.14 / +0.37 | +0.98 / +0.97 | +1.81 |
| 0.85 (8.25 s) | 0.0005 | +2.30 / +1.43 | +4.32 / +4.00 | +6.59 / +6.47 | +5.45 |
| 0.85 | 0.2 | +0.91 / +0.88 | +2.78 / +3.11 | +4.92 / +5.35 | +4.19 |
| 0.85 | 0.5 | +0.33 / +0.13 | +2.04 / +1.42 | +3.92 / +4.12 | +3.62 |

What the bare-loop table says, for a listening pass that has to pick a
decay and a wet:

- **At equal decay the paper plate costs more**, because it rings longer:
  at w 0.30, damping 0.0005, the bound reads +0.30 against +0.12 at decay
  0.5, +1.61 against +1.11 at 0.7, +4.32 against +3.71 at 0.85.
- **At equal T30 the paper plate costs less.** At decay 0.5 (T30 2.12 s)
  it reads +0.08 / +0.30 / +0.74 by the bound at w 0.15 / 0.30 / 0.50; the
  as-shipped plate at decay 0.7, the same T30, reads +0.46 / +1.11 / +2.17.
  Why was not measured (phase 0 measured the as-shipped plate's |R| peak
  only); nor does the bare loop say how the two sound.
- **Damping trims the long decays, more on the paper plate.** At w 0.30
  the bound falls from +3.71 to +2.22 dB (as shipped, decay 0.85, damping
  0.0005 → 0.5) and from +4.32 to +2.04 (paper, decay 0.85); at decay 0.5
  it moves the as-shipped plate by 0.21 dB and the paper plate by 0.48.
- **The (L + R)/2 return never costs more by the bound:** at w 0.30 it
  reads 0.00 to 0.39 dB below L in all 24 rows (plate × decay × damping);
  by the exact crossing from 0.73 dB below to 0.14 above. The sweep prints
  its full table.

## What did not converge, separate or get measured

- **The speech envelope did not converge** in three of eight convergence
  rows by the longest probe (cabin canceller to 80 s; cabin decay 0.5 and
  mt5 decay 0.7 to 160 s). Its grid rows (80 s) are directions, not gated.
- **Damping, the return and the plate do not separate** behind the
  canceller (the grid); only the bare loop separates them.
- **The two shift topologies disagree by measurement:** the whole-bus
  shift holds more runaway gain by bisection, the dry-only shift is audible
  later on the ramp. Neither protocol is wrong; which a room shows is a
  phase 1 measurement.
- **The reverb-only rows at S3, wet 0.30, are audible below the dry loop's
  limit in some seeds** (the audible table, ‡): real, feedback-dependent,
  bounded, at the plate's own modes; mechanism not identified.
- **Not run:** the speech envelope at S3; the paper plate in the loop at
  decay 0.7 and 0.85 (the bare loop covers them); the shift rows at wet
  0.15 and 0.50 by bisection (the audible driver covers 0.15); the
  audible rows for the paper plate, for other rooms than the cabin and for
  the speech envelope; float32 in the loop (every in-loop number is
  double; `test_reverb_mix.cpp` runs the stage's float plumbing on every
  target); the backing track; two microphones.
- **The audible table's canceller row is not the karaoke document's.**
  karaoke-afc.md's headline analysed every row with `--dechirp`; here
  PROTOCOL.md §7.3's flags apply (no `--dechirp` without a shift), and the
  canceller alone reads +16.57 at S1 (karaoke-afc.md: +14.96 on this host).
  The loop itself is unchanged: `decorrelated_loop` with no stage gives the
  karaoke measurement's limits bit for bit against origin/main (cabin and
  mt5, held and speech, with and without the shift, seed 2, 5 s probes).

## Hosts

Every number here was measured on **macOS 15.7.9 x86_64 (i9-8950HK),
AppleClang 17.0.0, Release**, double precision in the loop, on a machine
shared with another job (at most four of its twelve threads were this
work's). The analytic tables (T30, mix level, bare loop) are deterministic;
the loop is chaotic, which is why the in-loop claims are medians over five
seed sets and six rooms, and the gated rows bound medians with several dB
of margin. No second host has run the sweep or the audible driver. CI runs
the gated rows on its macOS arm64 and Linux legs (pass/fail only). The
audible driver ran on Python 3.10.7, numpy 2.2.6, scipy 1.15.3.

## Provenance

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUTAP_WERROR=ON -DMUTAP_BUILD_KARAOKE_DUMP=ON
cmake --build build
```

The stage's plumbing (every target; both emulated selections run the float
half) and the gated host rows:

```sh
build/tests/mutap_tests --gtest_filter='reverb_mix_test*:ReverbMix*'
build/tests/mutap_tests --gtest_filter='ReverbStage.*'
```

Wall times in the full `ctest` run of this PR (396 of 396 passed, 3233.67 s,
serial; each in-loop row on 4 threads): `CancellerHoldsBehindThePlates`
133.29 s, `WholeBusShiftHoldsMoreRunawayThanDryOnly` 63.73 s,
`PaperPlateBareLoopCost` 1.35 s, `PlateDecayAndMixLevel` 0.34 s, the rest
0.01 s each.

The sweep, behind `MUTAP_SLOW=1` (at most 4 threads by default;
`MUTAP_SLOW_THREADS` overrides; every finished bisection prints a `job`
line):

```sh
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.ProbeConvergence'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.Grid'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=1 REVERB_SWEEP_DELAYS=480 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.Shift'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=4 REVERB_SWEEP_DELAYS=960 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.Shift'
MUTAP_SLOW=1 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.BareLoopCost'
```

| Test | Wall time | Threads | Bisections |
|---|---|---|---|
| `ProbeConvergence` | 22607.7 s (cpu-sum 67810.6 s) | 3 | 610 |
| `Grid` | 24684.3 s (cpu-sum 73998.3 s) | 3 | 1350 |
| `Shift`, S1 | 17611.7 s (cpu-sum 17611.6 s) | 1 | 420 |
| `Shift`, S3 | 4813.9 s (cpu-sum 19196.2 s) | 4 | 420 |
| `BareLoopCost` | 53.5 s | 1 | (analytic) |

The audible table needs numpy and scipy; each run is about 2 minutes of
dump and one to three analyses. One invocation with `--control` reproduces
all of it (the runs are deterministic):

```sh
python3 tools/notebook/reverb_audible.py --dump build/tools/notebook/karaoke_ramp_dump --work runs --jobs 1 \
    --control --json audible.json
python3 tools/notebook/reverb_audible.py --merge audible.json --work runs   # reprint
```

How this table was actually produced: a first invocation without
`--control` ran 230 of its 240 runs on one job in about 5 h 10 min and
then died on a write: it kept every run's WAVs (about 180 MB a run) and
the scratch disk filled. The driver now deletes each run's WAVs after the
analysis (`--keep-wav` keeps them) and writes each run's record as it
finishes. `--recover` rebuilt the 230 records from the criterion's JSON
and the gain logs (the runaway gain by the dump's own rule; before the WAVs
were deleted, a scratch check found it equal to the WAVs' first 40 dB block
in 230 of 230 runs); a resumed invocation ran the last 10 runs (786.2 s);
the 20 shift-only runs were re-run with `--control` (3104.9 s), and their
limits repeat the first run's to the printed digit.
