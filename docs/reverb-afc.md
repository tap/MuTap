# A reverb behind the canceller

*Measured October 2026 in simulation: the cabin RIR fixture and five other
rooms from both generator families, band-limited through a loudspeaker
model, and phase 0's four room fixtures at full length for the bare loop.
Host: macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release, on a shared
machine ([Hosts](#hosts)). Nothing here has been heard in a real room. Every
number below comes from `tests/test_reverb_stage.cpp`,
`tests/test_spectral_reverb_host.cpp`, their `MUTAP_SLOW` sweeps
`tests/test_reverb_stage_sweep.cpp` and `tests/test_spectral_reverb_sweep.cpp`,
or the `tools/notebook/reverb_audible.py` driver; [Provenance](#provenance)
gives the commands.*

A reverb in the voice bus sits inside the feedback loop: whatever it does
to the bus, it does to the howl. This document puts FAUST's Dattorro plate,
as shipped and with the paper's delay lengths, in `afc_chain`'s reverb slot
behind the PEM + FD-Kalman canceller, and measures what it costs, with the
probe lengths shown to converge first. The second half puts the spectral
reverb shaped from the canceller's estimate (`tap::mu::spectral_reverb`)
on the same harness beside the plate ([Spectral reverb](#spectral-reverb);
HANDOFF item 11): it loses.

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
- **The spectral reverb loses to the plate everywhere measured**
  ([Spectral reverb](#spectral-reverb)): behind the canceller it costs
  +2.83 to +30.57 dB of runaway gain (six rooms, S1 and S3, wet 0.15 and
  0.30, flat and shaped; 40 s probes, not converged) where the plate costs
  −1.37 to +0.88, and without a canceller the flat reverb sits 17 to 32 dB
  below the dry room's limit. Shaping from F̂ helps only at shape_max 1. It
  stays in the repository as a measured negative result.
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
  from silence, as it starts the loop's own history from zero. A stage's
  own bulk latency is budgeted inside the forward delay, so the loop's total
  delay stays S1's or S3's whatever the stage (the plates report none; the
  spectral reverb's one block comes out of it, keeping it like for like).
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

## Spectral reverb

Phase 2 item 7, part 2 (HANDOFF item 11): `tap::mu::spectral_reverb`, the
per-bin reverb from the karaoke branch (commit `8abe958`), back under the
ABI rules and measured on the harness above, beside the as-shipped plate.

**Verdict: it loses to the plate, everywhere measured, and it stays in the
repository only as a measured negative result** (the header says so). Behind
the canceller it costs +2.83 to +30.57 dB of runaway gain (median over six
rooms, held note, 40 s probes) where the plate at the same wet costs −1.37
to +0.88. Paired by seed, no room's median favours the spectral reverb
over the plate for 22 of the 24 spectral rows compared, and 1 of 6 rooms
does for the other two. Shaping from F̂ helps a flat spectral reverb at
shape_max 1 (and at S3, w 0.30, at every ceiling) but never closes the gap
to the plate, and shape_max 4 loses up to 12.79 dB more than flat at S1.
Without a canceller the flat reverb's limit is 17 to 32 dB below the dry
room's (room medians, 160 s probes), which confirms HANDOFF item 11's −18
to −25. On the audible ramp every spectral row is audible earlier than the
plate at the same wet. The alternative, removing the class again, is
recommended against (HANDOFF item 11): the gated rows that hold the
negative result need it.

### The spectral stage

`include/mutap/spectral_reverb.h`: one complex one-pole per bin on
weighted-overlap-add spectra (Hann window of 2 × block, hop one block, no
synthesis window), S_k ← a_k S_k + X_k, Y_k = (1 − w) X_k + w_k S_k. The
whole output, dry path included, is the input one block late (`latency()`,
64 samples, 1.33 ms at 48 kHz). In the loop that block comes out of the
forward delay (`decorrelated_loop`'s forward stage now subtracts a stage's
`latency()`; 0 for the plates, whose rows are unchanged), so every
spectral row has the plates' total loop delay, 480 samples at S1 and 960 at
S3, as a chain's output delay line would absorb it.

- **Flat** (`spectral_shaping::flat`): every bin at the configured rt60 and
  wet; `reshape*()` is ignored.
- **Shaped** (`spectral_shaping::from_path`): per bin the allowance
  (median|P| / |P_k|), clamped to [0.05, `shape_max`], scales the wet gain
  and the decay time up where the path P is weak and down where it is
  strong; the wet gains are renormalised to the flat shape's Σ w_k², the
  decay times are not. P is the canceller's F̂ (`copy_impulse_response()`,
  `reshape_from_impulse_response()`), or for several microphones the
  coherent bus sum |Σ F̂_m| (`reshape_from_impulse_responses()`).
- **The rules:** defined inside `tap::mu::inline TAP_DSP_FFT_ABI` (it holds
  a `basic_real_fft` by value) and pinned as the sixth embedder in
  `tests/test_fft_engine_contract.cpp`; the transform size 2 × block goes
  through `fft_detail::checked_fft_size` before any buffer is sized (block
  16 to 2048 on the Cortex-M55's CMSIS-DSP; an unchecked size there is a
  release-mode HardFault). An `afc_stage` (`process_block(in, out, n)`
  `noexcept`, n a multiple of the block), allocation only at construction,
  float and double; `tests/test_spectral_reverb.cpp` runs its plumbing on
  every target, both emulated selections included.

What it is beside the plate (`SpectralReverbHost.DecayAndLevelBesideThePlate`):
its all-wet impulse response's T30 equals its rt60 (1.0000 s at rt60 1 s,
1.1846 s at 1.1846 s, the plate's decay-0.5 T30), and it is much louder
than the plate's mix at the same wet. For white input the flat stage reads
+3.40 / +7.60 dB at wet 0.15 / 0.30 (rt60 1 s) and +3.81 / +8.22 dB (rt60
1.1846 s), against the plate's −0.956 / −1.687 dB: each bin's one-pole sums
about rt60 × 750 hops of its input. Shaped, the level rises with
`shape_max` (the decay times are not renormalised): the grid's medians run
from +3.34 (shape_max 1, w 0.15) to +13.06 dB (shape_max 4, w 0.30).

### How the spectral rows are measured

The plates' protocol ([How the loop is measured](#how-the-loop-is-measured))
with one step the shaping needs (`tests/support/spectral_rig.h`): the
canceller converges with the reverb in the loop, flat (nothing to shape
from yet); the reverb is then shaped once from the converged F̂ and the
shape is held through every probe of the bisection, each probe starting the
tail from silence. That is the branch's protocol ("converge, then shape from
what the canceller has identified"). Shaped rows run without a canceller
(the open loop) are shaped from the F̂ a canceller converged to in the same
loop, then bisected with no canceller. The audible rows refresh the shape
from the live canceller every 256 blocks instead, since that canceller
starts cold.

### Spectral probe convergence

`SpectralReverbSweep.ProbeConvergence`: the cabin and mt5 at S1, wet 0.30,
rt60 1 s, flat and shaped (shape_max 1, 2, 4), held note to 160 s and
speech envelope to 80 s (the budget), each chain row with the open loop of
the same reverb beside it. Converged as for the plates. Median ASG, dB re
the dry open loop:

| Room, material | Row (wet 0.30, rt60 1 s) | 10 s | 20 s | 40 s | 80 s | 160 s | Converged |
|---|---|---|---|---|---|---|---|
| cabin, held | canceller alone | +11.66 | +11.74 | +11.74 | +11.74 | | 10 s |
| cabin, held | flat | −3.87 | −4.67 | −5.64 | −6.62 | −6.82 | 80 s |
| cabin, held | shape_max 1 | +3.36 | +2.27 | +1.48 | −0.37 | −0.27 | 80 s |
| cabin, held | shape_max 2 | +5.41 | +3.63 | +3.63 | +2.27 | +2.36 | 80 s |
| cabin, held | shape_max 4 | −2.01 | −3.30 | −5.74 | −17.46 | −19.02 | **not converged** |
| cabin, held | flat, no canceller | −29.14 | −29.75 | −30.14 | −30.33 | −30.43 | 80 s |
| cabin, held | shape_max 1, no canceller | −18.30 | −19.00 | −19.39 | −19.59 | −19.69 | 80 s |
| cabin, held | shape_max 2, no canceller | −18.50 | −19.30 | −19.79 | −20.08 | −20.18 | 80 s |
| cabin, held | shape_max 4, no canceller | −22.70 | −25.06 | −26.52 | −27.50 | −27.99 | **not converged** |
| mt5, held | canceller alone | +11.05 | +11.05 | +11.05 | +11.05 | | 10 s |
| mt5, held | flat | +1.21 | +1.00 | +0.41 | −0.27 | −0.57 | **not converged** |
| mt5, held | shape_max 1 | +10.57 | +8.03 | +9.49 | +8.03 | +8.42 | **not converged** |
| mt5, held | shape_max 2 | +8.52 | +5.00 | +5.98 | +6.86 | +6.46 | **not converged** |
| mt5, held | shape_max 4 | +5.98 | +2.75 | −13.85 | +2.07 | −20.00 | **not converged** |
| mt5, held | flat, no canceller | −21.91 | −22.23 | −22.52 | −22.81 | −22.91 | 80 s |
| mt5, held | shape_max 1, no canceller | −13.71 | −14.22 | −14.51 | −14.61 | −14.71 | 40 s |
| mt5, held | shape_max 2, no canceller | −16.35 | −17.73 | −18.61 | −19.10 | −19.39 | **not converged** |
| mt5, held | shape_max 4, no canceller | −19.86 | −22.03 | −23.40 | −24.18 | −24.57 | **not converged** |
| cabin, speech | canceller alone | +21.02 | +20.82 | +20.23 | +19.75 | | **not converged** |
| cabin, speech | flat | +3.26 | +3.05 | +2.95 | +2.66 | | **not converged** |
| cabin, speech | shape_max 1 | +15.47 | +15.55 | +15.45 | +15.45 | | 10 s |
| cabin, speech | shape_max 2 | +13.81 | +13.89 | +12.81 | +12.81 | | 40 s |
| cabin, speech | shape_max 4 | +12.42 | +10.76 | +11.25 | +10.86 | | **not converged** |
| mt5, speech | canceller alone | +23.26 | +23.16 | +22.87 | +22.68 | | 40 s |
| mt5, speech | flat | +10.18 | +11.25 | +9.30 | +8.81 | | **not converged** |
| mt5, speech | shape_max 1 | +14.28 | +14.38 | +13.59 | +13.40 | | 40 s |
| mt5, speech | shape_max 2 | +11.25 | +10.96 | +10.66 | +10.37 | | **not converged** |
| mt5, speech | shape_max 4 | +10.86 | +10.47 | +10.47 | +10.47 | | 20 s |

- **Unlike the plates, the spectral rows do not converge at 10 s.** On the
  held note the cabin's flat, shape_max 1 and 2 rows converged at 80 s;
  shape_max 4 and every mt5 chain row had not converged by 160 s. The
  medians mostly fall with probe length (more cost): the cabin's shape_max
  4 row goes from −2.01 at 10 s to −19.02 at 160 s, two of its five seeds
  at the bracket floor (−20.00). This is HANDOFF item 11's "not converged by
  40 s", now measured to 160 s.
- **The open loop converges by 80 s** for flat and shape_max 1 (both rooms)
  and shape_max 2 in the cabin; shape_max 4 drifts down to 160 s.
- **On speech** four of the eight spectral chain rows converged by 80 s
  (cabin shape_max 1 at 10 s, shape_max 2 at 40 s; mt5 shape_max 1 at 40 s,
  shape_max 4 at 20 s); the canceller alone in the cabin had not, as in the
  plates' table.

The grid below therefore runs the held note on 40 s probes (the budget: a
160 s row costs about four times a 40 s one), which is **not converged** and
is reported, not gated. The 40 s median ASG read at or above the 160 s one
in 7 of the 8 held rows above (mt5 shape_max 2: +5.98 against +6.46), so
the grid mostly understates the spectral reverb's cost. The open-loop rows
run on 160 s probes. The gated rows that use converged probes (80 s, the
cabin's flat and shape_max 1 chain rows, the cabin's and mt5's open loops)
run behind `MUTAP_SLOW`: at 697 s and 189 s on 3 threads they do not fit
the default run (the sanitizer legs). The default run carries a short proxy
for each, 20 s probes and two seed sets, and asserts the same directions.
Short probes understate both the spectral reverb's cost and the open
loop's danger (the table: every such median falls with probe length), so a
pass on 20 s is conservative.

### Behind the canceller: the spectral reverb against the plate (runaway)

`SpectralReverbSweep.Grid`, six rooms, five seed sets, held note, 40 s
probes. Each cell is the median over the six rooms of each room's five-seed
median, [the range of the room medians] and {the per-seed range over all
30 runs}. Cost = the canceller-alone limit − the limit with the reverb, per
seed; level = the stage's level change for white input (median over runs);
matched = cost − level. The plate rows are the as-shipped plate at decay
0.5 (T30 1.1846 s; rt60 1.1846 s is matched to it). Their cost medians
reproduce the plates' 20 s grid
([above](#what-the-plate-costs-behind-the-canceller-runaway)) to the
printed digit at S1 and at S3, w 0.15 (ASG medians within 0.08 dB); at S3,
w 0.30 the cost reads −1.37 against −1.17 there, the S3 canceller-alone
row moving between 20 and 40 s probes (+18.38 here against +18.40).

**S1 (10 ms).** The canceller alone: ASG +11.74 [+11.05, +12.42] {+7.73,
+13.98}.

| Reverb, wet | ASG | Cost [rooms] {seeds} | Level | Matched |
|---|---|---|---|---|
| plate 0.5, w 0.15 | +11.05 | +0.49 [−0.39, +2.05] {−4.00, +5.76} | −0.96 | +1.44 |
| spectral flat, rt60 1.18, w 0.15 | +7.34 | +5.66 [−1.76, +10.35] {−3.71, +13.48} | +3.81 | +1.85 |
| shape_max 1, rt60 1.18, w 0.15 | +8.61 | +2.83 [−2.15, +6.35] {−5.27, +10.55} | +3.34 | −0.51 |
| shape_max 2, rt60 1.18, w 0.15 | +6.46 | +7.03 [+0.98, +11.62] {−2.93, +15.43} | +4.91 | +2.12 |
| shape_max 4, rt60 1.18, w 0.15 | +0.21 | +14.26 [+3.61, +21.39] {−2.44, +31.74} | +7.25 | +7.01 |
| plate 0.5, w 0.30 | +11.54 | +0.88 [−0.78, +2.44] {−4.30, +4.20} | −1.69 | +2.57 |
| spectral flat, rt60 1, w 0.30 | +0.31 | +12.70 [+6.05, +17.38] {+4.39, +19.14} | +7.60 | +5.09 |
| shape_max 1, rt60 1, w 0.30 | +1.48 | +10.35 [+1.56, +12.01] {−2.54, +13.96} | +6.92 | +3.43 |
| shape_max 2, rt60 1, w 0.30 | +2.85 | +12.89 [+6.45, +14.45] {+2.73, +31.74} | +9.15 | +3.74 |
| shape_max 4, rt60 1, w 0.30 | −13.65 | +27.34 [+17.48, +32.42] {+5.37, +33.01} | +11.96 | +15.38 |
| spectral flat, rt60 1.18, w 0.30 | −0.76 | +13.09 [+9.57, +17.68] {+5.66, +21.97} | +8.22 | +4.87 |
| shape_max 1, rt60 1.18, w 0.30 | +4.22 | +10.74 [+3.91, +12.89] {+1.07, +18.46} | +7.61 | +3.13 |
| shape_max 2, rt60 1.18, w 0.30 | +0.02 | +13.48 [+5.08, +15.72] {+1.56, +30.66} | +9.94 | +3.53 |
| shape_max 4, rt60 1.18, w 0.30 | −18.34 | +30.57 [+13.38, +32.42] {+3.42, +33.98} | +12.22 | +18.34 |

**S3 (20 ms).** The canceller alone: ASG +18.38 [+16.93, +18.59] {+16.52,
+19.36}.

| Reverb, wet | ASG | Cost [rooms] {seeds} | Level | Matched |
|---|---|---|---|---|
| plate 0.5, w 0.15 | +18.87 | −0.59 [−1.76, +1.17] {−3.22, +3.71} | −0.96 | +0.37 |
| spectral flat, rt60 1.18, w 0.15 | +8.61 | +10.16 [+7.32, +11.91] {+5.86, +12.99} | +3.81 | +6.34 |
| shape_max 1, rt60 1.18, w 0.15 | +9.10 | +10.94 [+3.42, +12.79] {+1.46, +13.18} | +3.59 | +7.35 |
| shape_max 2, rt60 1.18, w 0.15 | +8.22 | +11.82 [+6.35, +14.84] {+2.44, +18.75} | +5.22 | +6.60 |
| shape_max 4, rt60 1.18, w 0.15 | +9.49 | +15.04 [+6.84, +17.77] {+1.46, +20.90} | +8.09 | +6.95 |
| plate 0.5, w 0.30 | +18.87 | −1.37 [−2.15, +0.49] {−4.20, +1.56} | −1.69 | +0.32 |
| spectral flat, rt60 1, w 0.30 | +0.61 | +18.07 [+13.48, +18.36] {+12.89, +20.02} | +7.60 | +10.46 |
| shape_max 1, rt60 1, w 0.30 | +3.14 | +16.99 [+10.35, +17.87] {+7.62, +20.61} | +6.94 | +10.05 |
| shape_max 2, rt60 1, w 0.30 | +1.68 | +17.77 [+12.01, +20.61] {+8.89, +23.44} | +9.04 | +8.74 |
| shape_max 4, rt60 1, w 0.30 | +1.78 | +18.07 [+12.50, +24.12] {+9.38, +27.15} | +12.19 | +5.87 |
| spectral flat, rt60 1.18, w 0.30 | −0.57 | +18.75 [+15.14, +19.92] {+14.65, +21.39} | +8.22 | +10.53 |
| shape_max 1, rt60 1.18, w 0.30 | +3.44 | +15.14 [+10.16, +17.77] {+7.23, +20.41} | +7.49 | +7.65 |
| shape_max 2, rt60 1.18, w 0.30 | +1.78 | +16.70 [+11.91, +18.65] {+7.81, +24.80} | +9.85 | +6.85 |
| shape_max 4, rt60 1.18, w 0.30 | +3.63 | +15.23 [+11.33, +17.09] {+3.91, +30.08} | +13.06 | +2.17 |

The same runs paired by seed: the spectral chain's limit minus the plate's
at the same wet, and shaped minus flat at the same rt60 and wet; the
median over the six rooms of each room's per-seed median, [range], (rooms
where it is > 0):

| | S1, w 0.15 (rt60 1.18) | S1, w 0.30 (rt60 1) | S1, w 0.30 (rt60 1.18) | S3, w 0.15 (rt60 1.18) | S3, w 0.30 (rt60 1) | S3, w 0.30 (rt60 1.18) |
|---|---|---|---|---|---|---|
| flat − plate | −3.52 (1 of 6) | −11.52 (0) | −11.72 (0) | −10.25 (0) | −17.77 (0) | −19.43 (0) |
| shape_max 1 − plate | −2.73 (1 of 6) | −8.50 (0) | −7.03 (0) | −8.20 (0) | −16.21 (0) | −15.43 (0) |
| shape_max 2 − plate | −4.59 (0) | −7.81 (0) | −10.45 (0) | −9.08 (0) | −16.89 (0) | −17.19 (0) |
| shape_max 4 − plate | −10.64 (0) | −24.51 (0) | −29.30 (0) | −8.98 (0) | −17.58 (0) | −15.72 (0) |
| shape_max 1 − flat | +1.17 (3 of 6) | +1.86 (4 of 6) | +4.30 (5 of 6) | +1.46 (3 of 6) | +1.95 (6 of 6) | +3.71 (6 of 6) |
| shape_max 2 − flat | −1.17 (2 of 6) | −0.49 (2 of 6) | −0.78 (2 of 6) | +0.59 (3 of 6) | +1.37 (4 of 6) | +2.64 (6 of 6) |
| shape_max 4 − flat | −7.91 (0) | −12.79 (0) | −12.60 (0) | +0.39 (3 of 6) | +0.98 (3 of 6) | +3.71 (6 of 6) |

- **The plate holds more runaway gain than every spectral row:** the
  median per-seed difference is −2.73 to −29.30 dB, and in 22 of the 24
  comparisons no room's median favours the spectral reverb (1 of 6 rooms
  for flat and shape_max 1 at S1, w 0.15). Its cost, +2.83 to +30.57 dB,
  starts above the plate's largest (+0.88 on this grid; +1.46 on the
  plates' own).
- **Loudness explains part of it, not all.** The spectral stage at equal
  wet is 4.30 to 14.75 dB louder than the plate's mix. At matched loudness the
  spectral rows still cost +2.12 to +18.34 at S1 except shape_max 1 at
  w 0.15 (−0.51; the plate +1.44) and flat at w 0.15 (+1.85; plate +1.44),
  and +2.17 to +10.53 at S3 against the plate's +0.32 / +0.37. Matched is
  the cost had the stage been turned down to the dry bus's loudness; the
  measurement ran it as configured.
- **Shaping helps only at shape_max 1** at S1 (+1.17 to +4.30 over flat, 3
  to 5 of 6 rooms); shape_max 2 does not separate from flat there and
  shape_max 4 loses 7.91 to 12.79 dB in every room. At S3 every ceiling
  reads at or above flat at w 0.30 (+0.98 to +3.71). Where the branch had
  "shaped ≥ flat" at shape_max 4 (0.8 s probes), the converged and 40 s
  probes say the opposite at S1.
- **Speech envelope** (S1, wet 0.30, rt60 1 s; ProbeConvergence, cabin and
  mt5 only, 80 s; the six-room speech grid was not run): the spectral rows
  cost +4.30 to +17.09 (cabin) and +9.28 to +14.06 (mt5) against the
  canceller alone, where the plate at decay 0.5 raised the limit (cabin
  +21.31 against the canceller's +19.75, mt5 +24.24 against +22.68; the
  plates' convergence table).

### Without a canceller (open loop)

The open loop with the reverb in it against the dry room (no canceller),
160 s probes, six rooms (the grid; rt60 1.1846 s), median of room medians
[range]:

| | S1, w 0.15 | S1, w 0.30 | S3, w 0.15 | S3, w 0.30 |
|---|---|---|---|---|
| flat | −19.49 [−26.04, −17.15] | −25.45 [−31.80, −23.40] | −19.10 [−24.57, −17.83] | −25.16 [−30.33, −23.98] |
| shape_max 1 | −18.32 [−19.20, −14.71] | −18.52 [−22.13, −15.49] | −16.56 [−19.98, −12.56] | −17.44 [−23.30, −16.46] |
| shape_max 2 | −18.42 [−25.94, −15.68] | −20.96 [−25.84, −15.78] | −18.81 [−21.05, −13.34] | −19.79 [−24.77, −17.54] |
| shape_max 4 | −22.42 [−29.84, −18.22] | −23.69 [−28.87, −17.54] | −17.64 [−19.98, −13.73] | −17.44 [−25.74, −13.83] |

The plate costs a bare loop at most +5.79 dB by the bound (decay 0.85, w
0.50; [The bare loop](#the-bare-loop-what-the-plate-costs-without-a-canceller)).
HANDOFF item 11's −18.05 to −24.61 for the flat reverb holds with converged
probes: −17.15 to −31.80 over the six rooms. Shaping does not make it safe:
every shaped row is 12.56 to 29.84 dB below the dry room's limit. Gated
(cabin / mt5, S1, w 0.30, rt60 1 s): on converged 80 s probes behind
`MUTAP_SLOW` (`SpectralReverbHost.UnsafeWithoutTheCanceller`) flat
−30.51 / −22.77, shape_max 1 −19.61 / −14.69 (five-set medians); in the
default run on 20 s probes (`UnsafeWithoutTheCancellerShortProbe`, two
sets) −29.80 / −22.42 and −19.79 / −13.98 (means). Behind the canceller,
`CostsMoreThanThePlateShortProbe` (default, cabin, 20 s, two sets) reads the
plate's limit above the spectral reverb's by +14.66 (flat) and +9.78
(shape_max 1) dB, and `CostsMoreThanThePlateBehindTheCanceller`
(`MUTAP_SLOW`, 80 s) costs the spectral reverb +17.19 and +8.44 dB
against the canceller alone.

### The hypothesis: does shaping put the reverb where the estimate is worst?

HANDOFF item 11's hypothesis was that the allowance up to shape_max builds
high-Q resonances where the canceller's estimate is poorest. Measured at
the moment of shaping in every shaped grid run (`spectral_rig.h`'s
`per_bin`; 30 runs a row): per bin of the reverb's 65-bin grid, the
misalignment m_k = |F_k − F̂_k| (F the true band-limited path, both sampled
exactly on the grid), the bin's decay time T_k, its tail energy E_k =
w_k²/(1 − a_k²) and peak gain g_k = (1 − w) + w_k/(1 − a_k); per run,
Spearman's ρ over the bins, the share of Σ E_k in the quarter of the bins
with the largest m_k (a flat reverb's share is 16/65 = 0.246), and a
residual-loop indicator, 20 log10 of max_k m_k g_k over the flat reverb's.
Medians over the 30 runs [min, max]:

| Row | ρ(T, m) | ρ(T, m / \|F\|) | E share, worst quarter | Peak m·g re flat, dB |
|---|---|---|---|---|
| S1, shape_max 1, w 0.15 | −0.535 [−0.865, −0.200] | +0.161 [−0.235, +0.644] | 0.134 [0.025, 0.217] | −2.456 [−6.914, +0.247] |
| S1, shape_max 2, w 0.15 | −0.589 [−0.876, −0.258] | +0.217 [−0.201, +0.689] | 0.082 [0.007, 0.238] | +2.375 [−4.481, +7.970] |
| S1, shape_max 4, w 0.15 | −0.601 [−0.881, −0.263] | +0.239 [−0.199, +0.680] | 0.036 [0.003, 0.312] | +8.251 [−3.879, +16.717] |
| S1, shape_max 1, w 0.30 | −0.336 [−0.784, −0.033] | +0.495 [−0.204, +0.625] | 0.175 [0.063, 0.230] | −1.015 [−6.522, +1.722] |
| S1, shape_max 2, w 0.30 | −0.437 [−0.843, −0.108] | +0.516 [−0.161, +0.657] | 0.091 [0.013, 0.217] | +3.706 [−4.829, +9.853] |
| S1, shape_max 4, w 0.30 | −0.458 [−0.840, −0.131] | +0.526 [−0.143, +0.640] | 0.050 [0.003, 0.189] | +8.408 [−5.239, +17.698] |
| S3, shape_max 1, w 0.15 | −0.625 [−0.877, −0.243] | +0.168 [−0.240, +0.537] | 0.110 [0.029, 0.230] | −3.250 [−6.873, +1.261] |
| S3, shape_max 2, w 0.15 | −0.683 [−0.942, −0.277] | +0.205 [−0.208, +0.588] | 0.058 [0.007, 0.177] | +0.438 [−5.248, +7.515] |
| S3, shape_max 4, w 0.15 | −0.688 [−0.945, −0.285] | +0.187 [−0.205, +0.585] | 0.022 [0.002, 0.211] | +5.487 [−2.705, +14.620] |
| S3, shape_max 1, w 0.30 | −0.336 [−0.687, −0.109] | +0.404 [−0.125, +0.596] | 0.177 [0.093, 0.230] | −0.848 [−5.504, +1.783] |
| S3, shape_max 2, w 0.30 | −0.381 [−0.755, −0.135] | +0.429 [−0.211, +0.655] | 0.110 [0.030, 0.314] | +4.981 [−2.803, +9.010] |
| S3, shape_max 4, w 0.30 | −0.402 [−0.754, −0.135] | +0.440 [−0.203, +0.654] | 0.059 [0.010, 0.363] | +9.717 [−3.728, +17.130] |

(rt60 1.1846 s; the rt60 1 s rows at w 0.30 read within 0.11 of these in
ρ and within 0.017 in share, and the sweep prints them.) ρ(E, ·) and ρ(g, ·) equal ρ(T, ·):
all three rise with the allowance.

- **In absolute terms, no: shaping puts the long decays where the
  misalignment is smallest.** ρ(T, m) is negative in every row (median
  −0.336 to −0.688), and the shaped reverb puts less of its tail energy in
  the worst quarter of the bins than a flat one does (0.022 to 0.178 over
  all 18 rows, against 0.246).
- **In relative terms, yes, weakly:** ρ(T, m / |F|) is positive in every
  row (median +0.161 to +0.526). The bins where |F̂| is weak are where the
  estimate is worst relative to the path.
- **What tracks the loss is the ceiling.** The residual-loop indicator
  rises with shape_max: shape_max 1 lowers the worst bin's m·g against flat
  (−0.848 to −3.250 dB), shape_max 4 raises it by +5.487 to +9.717 dB,
  which matches shape_max 1 being the only ceiling that beats flat at S1 and
  shape_max 4 losing there. Across runs, the indicator and the energy share
  barely predict a run's own shaped − flat limit: Spearman's ρ over the 30
  runs of a row is −0.239 to +0.699 (indicator) and −0.351 to +0.434
  (share), with no consistent sign. In the measured form: a large ceiling
  raises the largest residual m_k·g_k on the grid (+5.487 to +9.717 dB at
  shape_max 4) although its long decays sit in bins of below-median
  misalignment, so the hypothesis as worded (energy concentrated where the
  estimate is worst) is not what the runs show; and none of the per-bin
  statistics predicts a single run's outcome well.

### Spectral audible limits

The plates' audible protocol ([Audible limits](#audible-limits-the-product-chain-candidates)),
`reverb_audible.py --plate spectral`: the cabin, held note, the ramp from
20 dB under the dry loop's `exact_msg_db` after a 30 s warm-up, the live
canceller from a cold start, the shape refreshed from its F̂ every 256
blocks (flat rows: never). PROTOCOL.md §7.3's reverb-only flags: the
chain output analysed (after the reverb, before the gain), c as the
cross-check, `--rt60 0.0328` and `--chain-rt60 1.1846` (the spectral
reverb's own T30 at rt60 1.1846 s, flat and shaped rows alike). rt60
1.1846 s, five seeds, medians, dB over `exact_msg_db`; the first four rows
are the plates' table's (same protocol, this host):

| Chain | S1 audible (c) | S1 runaway (ramp) | S3 audible (c) | S3 runaway (ramp) |
|---|---|---|---|---|
| dry (no canceller) | +0.18 | +1.08 | +0.21 | +1.62 |
| canceller | +16.57 | +20.70 | +12.37 | +21.37 |
| + plate 0.5, w 0.15 | +16.53 | +21.46 | +10.38 | +23.01 |
| + plate 0.5, w 0.30 | +12.89 | +22.07 | −4.77 ‡ | +23.31 |
| + spectral flat, w 0.15 | −7.42 (−6.74) | +4.73 | −17.78 (−2.05) | +11.66 |
| + spectral flat, w 0.30 | −6.30 (−6.42) | −2.82 | −12.77 (−9.24) | +3.56 |
| + shape_max 1, w 0.15 | +11.90 (+11.90) | +18.31 | −13.14 (+4.98) | +17.98 |
| + shape_max 1, w 0.30 | −4.32 (−4.33) | +7.83 | −17.06 (−1.14) | +9.41 |
| + shape_max 2, w 0.15 | +4.52 (−1.39) | +13.52 | −18.66 (+6.70) | +17.07 |
| + shape_max 2, w 0.30 | −16.47 (−10.79) | +2.27 | −19.38 (−6.32) | +8.40 |
| + shape_max 4, w 0.15 | −9.40 (−7.99) | +1.16 | −17.23 (+8.34) | +14.11 |
| + shape_max 4, w 0.30 | −18.18 (−17.49) § | −6.94 | −18.83 (−6.15) | +7.84 |

§ Four seeds: seed 62 ran away at the ramp's start gain (runaway −20.00,
the floor) and the criterion found no audible event. Over the 80 runs the
criterion raised 23 warnings on the chain output (flat 4 and 5 at S1, w
0.15 / 0.30, and 2 and 5 at S3; shape_max 2 at S1, w 0.30, 2; shape_max 4
at S1, 2 and 3) and 2 on c; the driver's JSON keeps them.

- **Every spectral row is audible earlier than the plate at the same wet.**
  The best, shape_max 1 at S1, w 0.15, is audible at +11.90 against the
  plate's +16.53. 14 of the 16 rows are audible below the dry loop's own
  audible limit (+0.18 / +0.21) on the chain output, and 12 of 16 on c.
- **The ramp's runaway falls too**, unlike the plate's: −6.94 to +18.31
  against the plate's +21.46 to +23.31 and the canceller's +20.70 /
  +21.37. On the ramp shape_max 1 and 2 run away later than flat at S1
  (+18.31 / +7.83 and +13.52 / +2.27 against +4.73 / −2.82; shape_max 4
  earlier, +1.16 / −6.94), and every shaped row later than flat at S3.
- **At S3 the chain output is flagged near the ramp's start** (−12.77 to
  −19.38; the ramp starts at −20 after the warm-up) where c reads −9.24 to
  +8.34. What the criterion flags in the chain output there was not
  examined. On c as on the chain output, every spectral row sits below the
  plate's row at the same wet (at S3, w 0.30, the plate's c cross-check
  reads −0.18, its chain output −4.77 ‡).
- 80 runs on 3 jobs, 1380.5 s wall. Every WAV was deleted after its
  analysis.

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
- **The spectral reverb's S3 audible rows are flagged near the ramp's
  start on the chain output** (−12.77 to −19.38, where c reads −9.24 to
  +8.34; [Spectral audible limits](#spectral-audible-limits)): the same
  family as the plate's S3 lines (‡ in the audible table), not examined
  here.
- **The spectral reverb's in-loop rows did not converge at 10 s** and most
  had not by 160 s ([Spectral reverb](#spectral-reverb)); its grid ran on
  40 s probes and is reported, not gated. Not run for it: the six-room
  speech grid, S3 on speech, converged grid probes, float32 in the loop,
  two microphones (the bus-sum shaping has plumbing tests only), and
  audible rows beyond the cabin held note at rt60 1.1846 s.
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
work's; three for the spectral reverb's sweeps and audible runs). The
analytic tables (T30, mix level, bare loop) are deterministic;
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

The spectral reverb ([Spectral reverb](#spectral-reverb)): its plumbing on
every target, the gated host rows (the default run's, then the two
converged rows behind `MUTAP_SLOW`), then the sweep (`SPECTRAL_SWEEP_*`
variables select subsets; the file comment lists them):

```sh
build/tests/mutap_tests --gtest_filter='spectral_reverb_test*:SpectralReverbConfig*:SpectralReverbRt*'
MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='SpectralReverbHost.*'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='SpectralReverbHost.*'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 SPECTRAL_SWEEP_MATS=held build/tests/mutap_tests --gtest_filter='SpectralReverbSweep.ProbeConvergence'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 SPECTRAL_SWEEP_MATS=speech SPECTRAL_SWEEP_PROBES=10,20,40,80 build/tests/mutap_tests --gtest_filter='SpectralReverbSweep.ProbeConvergence'
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 SPECTRAL_SWEEP_MATS=held build/tests/mutap_tests --gtest_filter='SpectralReverbSweep.Grid'
```

| Test | Wall time | Threads | Jobs |
|---|---|---|---|
| `SpectralReverbHost.DecayAndLevelBesideThePlate` (default) | 0.25 s | 1 | (analytic) |
| `SpectralReverbHost.UnsafeWithoutTheCancellerShortProbe` (default) | 27.03 s | 3 | 12 open-loop bisections (4 after a convergence) |
| `SpectralReverbHost.CostsMoreThanThePlateShortProbe` (default) | 77.25 s | 3 | 6 chain bisections |
| `SpectralReverbHost.UnsafeWithoutTheCanceller` (`MUTAP_SLOW`) | 189.41 s | 3 | 30 open-loop bisections (10 after a convergence) |
| `SpectralReverbHost.CostsMoreThanThePlateBehindTheCanceller` (`MUTAP_SLOW`) | 696.95 s | 3 | 15 chain + 5 open-loop bisections |
| `ProbeConvergence`, held | 11349.4 s (cpu-sum 34003.1 s) | 3 | 290 |
| `ProbeConvergence`, speech | 6849.3 s (cpu-sum 20503.6 s) | 3 | 240 |
| `Grid`, held | 35418.6 s (cpu-sum 106197.4 s) | 3 | 1500 |

A spectral job bisects the chain or the open loop with the reverb, after
converging the canceller where the shape needs it. The audible rows:

```sh
python3 tools/notebook/reverb_audible.py --dump build/tools/notebook/karaoke_ramp_dump --work runs --jobs 3 \
    --plate spectral --decays 1.1846 --wets 0.15,0.3 --shape-maxes 0,1,2,4 \
    --configs spec0_1.1846_0.15,spec0_1.1846_0.3,spec1_1.1846_0.15,spec1_1.1846_0.3,spec2_1.1846_0.15,spec2_1.1846_0.3,spec4_1.1846_0.15,spec4_1.1846_0.3 \
    --json audible_spectral.json
```

80 runs on 3 jobs: 1380.5 s wall (per-run sum 4071.5 s). Each run's WAVs
are deleted after its analysis; the scratch directory held 139 MB of JSON
and gain logs at the end.

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
