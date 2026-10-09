# Measurement protocol: real rooms and the simulated loop

This is the measurement protocol for the anti-howl work. It covers a real room (mic → canceller
chain → bus gain → loudspeaker → room → mic) and the simulated loop that mirrors it
(`tests/support/decorrelated_loop.h`, driven by `tools/notebook/karaoke_ramp_dump.cpp`). It was
written on 9 October 2026 from the tools and from the references that already cite it. No
real-room session had run by then. Nothing in this document has been executed in a real room
yet, and it contains no measured result. A session starts at [§8, the run sheet](#8-run-sheet-for-the-first-listen-m1).

The tools it is written from:

| Tool | What it defines here |
|---|---|
| `tools/fixtures/measure_rir.py` | the sweep, the deconvolution, the loopback round trip, the decay figures |
| `tools/fixtures/howl_criterion.py` | the offline howl decision, its inputs and its flags |
| `tools/fixtures/make_rir_fixtures.py` | the import of a measured IR into a test fixture |
| `tools/notebook/karaoke_ramp_dump.cpp` | the ramp on the simulated loop |
| `tools/notebook/karaoke_audible.py`, `reverb_audible.py` | the criterion applied to the simulated loop |
| `include/mutap/afc_chain.h` | the reference-delay formula |

Every tool default quoted below is cited to its tool. Where a figure would be needed but nothing
defines it, the text says so.

## 1. Scope and rules

- **Measure first.** Each section below records something before anything is changed.
- **The howl decision is made offline.** The criterion decides it from the recording
  (`howl_criterion.py`, §7). The product's own detector (`howl_detector`, `howl_guard`) is under
  test, so it never decides a limit. The guard is off during every gain ramp
  (`docs/howl-guard.md`, "Not measured": "the protocol keeps it off there"). With the guard on, a
  ramp would measure the guard rather than the loop.
- **Three repeats per condition.** Report the median and the range over the three
  (`howl_criterion.py aggregate`, §7.4). A single run is not a result.
- **Every number carries its context.** For a real room that is the host, the interface and its
  buffer setting, and the room. For the simulated loop it is the host, compiler, build type and
  seeds. `docs/karaoke-afc.md`, "Hosts" shows why: identical signals gave headline rows up to
  3.23 dB apart on two hosts.
- **For simulated rooms, use both generator families.** A closed-loop claim sweeps the C++
  `std::mt19937` rooms in the tests and the notebook's numpy rooms before it is written down
  (HANDOFF.md, working note 3: "One room is not an evaluation").

## 2. The rig

**Interface and channel numbering.** Use one audio interface at 48 kHz, the default rate of every
tool here. Channel numbers on the command line are 1-based everywhere. This is the numbering of
interface labels, of Max's `adc~`/`dac~` and of sounddevice's channel maps (`measure_rir.py`
docstring). `howl_criterion.py analyze --channel` is the one exception: it is a 0-based column
index (`read_wav` indexes `x[:, channel]`). Recording channel 1 is therefore `--channel 0`, and
channel 9 is `--channel 8`.

**The electrical loopback cable** runs from one interface output to one interface input. It
measures the rig's own round trip (DAC → cable → ADC). That is the part of the feedback path
that is not the room (§5.1). It stays patched for the whole session, so that a room measurement
can carry a loopback channel (§4).

**The Max patch.** `mutap.afc~` (MuTap-Max) is the canceller in the loop. It takes the mic on
signal inlet 0 and the loudspeaker feed u on signal inlet 1, and outputs the cleaned signal e on
outlet 0. The attributes this protocol uses, as shipped:

| Attribute | Shipped default | Role here |
|---|---|---|
| `@kalman` | 0 (NLMS core) | `@kalman 1` selects the frequency-domain Kalman engine; the engine A/B in §8 |
| `@shadow` | 2 | the shadow comparator; the guard needs it > 0 |
| `@guard` | 0 | the safety layer; needs `@kalman 1` and `@shadow` > 0; off during ramps (§1) |
| `@loop_ms` | 10 | the guard's loop period, entered by hand from §5.1 |
| `@ceiling` | −6 dB | the guard's absolute howl ceiling, set from the soundcheck |
| `@cap` | none | the deployment gain cap; the external's description gives "the dry limit − 6 dB" |

The `calibrate` message runs the guard's soundcheck. It needs the guard running. This document
uses no other attribute, and it gives these no semantics beyond the external's own descriptions
(`MuTap-Max/source/projects/mutap.afc_tilde/mutap.afc_tilde.cpp`). `mutap.afc~` has no
reference-delay attribute. The delay ahead of its inlet 1 belongs to the patch (§5.2).

The rig patch is not yet in a tracked repository, and it has not been opened in Max. Its exact
channel map is therefore not fixed here. Only the two channels the criterion names are fixed.

**What is recorded**, per run, into one multichannel WAV:

- **Channel 1 = c**, the canceller output (or the bypass output, with the canceller off). It is
  the criterion's documented input (`howl_criterion.py`, "What to record": "the rig's channel 1").
- **Channel 9 = the chain output.** This is the forward signal after the shift and the reverb and
  before the bus gain ("the chain's later output, before the gain (channel 9)", same docstring).
  `afc_chain::forward_block()` is the same signal in the library.
- **The mic.** It is a cross-check only (§7.2).
- **The dry stems**, as played. They are also kept as mono files (§3).
- **The bus gain.** It goes to a gain log, a CSV with rows `time_s,gain_db`
  (`howl_criterion.py --gain-log`). Rows are interpolated linearly and held at the ends. A stepped
  ramp needs two rows per step.

## 3. Program material

- **Speech.** A spoken phrase.
- **Sung material.** A sung phrase.
- **The held note.** A sustained sung note is the hard tonal case. It is the simulated headline's
  material (`docs/karaoke-afc.md`). The criterion's `--no-program` mode false-triggers on it, so
  a held note is only ever analysed against its stem.
- **The backing track.** It is played through the loudspeaker as an aux feed, and it is a
  program stem of its own.

Every stem is kept as a dry mono file (or a file mixed to mono), as it was played. The criterion
resamples a stem to the recording's rate if needed. Each stem is passed as a `--program`
argument.

**The calibration segment must contain every stem's spectral material** (`howl_criterion.py`,
step 3). The criterion fits a static power transfer per stem and per bin over the calibration
frames. A bin that a stem never excited gets no transfer, so that stem stays unexplained there
for the rest of the run. A looped phrase meets this rule when the segment covers one loop. The
segment is the ramp's constant-gain warm-up (§6). The gain there must be ≥ 15 dB below any limit
(same step). An event whose onset falls inside the calibration window is rejected, and it is
reported separately under `events_in_calibration`.

## 4. Room measurement by exponential sine sweep

`measure_rir.py` has three sweep subcommands:

- `sweep OUT.wav` writes the excitation (float32 WAV) and `OUT.json`, a sidecar holding every
  parameter. The deconvolution rebuilds the playback buffer from the sidecar, so the excitation
  and its inverse cannot disagree.
- `deconvolve REC.wav --sweep OUT.json` recovers one IR per recorded channel.
- `measure --device NAME --out-ch ... --in-ch ... [--loopback-channel K]` runs a live
  measurement. It writes the sweep, plays and records it, saves the raw recording and
  deconvolves. `--list-devices` lists the devices.

Defaults (`measure_rir.py`, `sweep_params` and `_add_sweep_args`):

| Parameter | Default |
|---|---|
| Rate | 48 kHz |
| Band | 20 Hz – 20 kHz |
| Sweep length | 8 s |
| Level | −6 dBFS peak |
| Fade-in, fade-out | 50 ms and 10 ms, half-Hann |
| Pre-silence, post-silence | 0.5 s and 3.0 s |
| IR length | 2.0 s, clipped to the post-silence |

The sidecar is saved with the raw recording, because the deconvolution cannot run without it.

**Repeats.** `--repeats N` plays N periods back to back. The tool averages them sample by sample,
and uncorrelated noise drops by 10 log10 N dB (step 3). A short recording is zero-padded, with a
warning.

**What the report contains** (`STEM_report.json`; `--plot PNG` draws the IR, the Schroeder
curves and the magnitude responses):

- **Latency.** The peak position, refined by parabolic interpolation. For the loopback channel
  there is also a group-delay estimate (§5.1).
- **Acoustic delay.** For every other channel when a loopback channel exists: the onset (the
  first sample within 20 dB of the peak) minus the round trip, in ms and in metres at 343 m/s.
- **Peak-to-noise.** 20 log10 of the peak over the RMS of the IR's last 10 %.
- **Clipping.** It is flagged if any raw recorded sample reaches |x| ≥ 0.999.
- **Schroeder T20 and T30**, broadband and in octave bands from 125 Hz to 8 kHz (step 8). A
  range is reported only if the envelope peak stands 10 dB clear of the noise below the fit's
  bottom. That means 35 dB for T20 and 45 dB for T30, the ISO 3382-1 rule. Otherwise the value
  is `null` and never extrapolated. A `null` T30 means the measurement lacks decay range: average
  more repeats or raise the level, and measure again. The loopback channel gets no decay figures.

**Outputs.** Per channel the tool writes `STEM_chK_ir.wav` and, when a loopback channel exists,
`STEM_chK_ir_acoustic.wav`. Both are 32-bit integer PCM mono, normalized to a peak of 0.5. The
report's `wav_scale` is the factor applied. The `*_acoustic.wav` IR is advanced by the round
trip rounded to whole samples. The fractional remainder is in the report.

**Import into a fixture.** One command per WAV:

```sh
python3 tools/fixtures/make_rir_fixtures.py --from-wav ROOM_ch2_ir_acoustic.wav NAME \
    --source "who, where, when, rig, position; license"
```

`--source` is mandatory: the tool refuses to run without it. Its text is embedded in the header,
because the fixture becomes a permanent pass/fail baseline. The generated header carries
`SPDX-License-Identifier: MIT`, so import only a measurement whose terms permit that. The import
does three things:

1. It trims the bulk delay to a 32-sample guard before the first sample within 40 dB of the peak.
2. It truncates the IR to 4096 taps.
3. It normalizes the IR to unit energy.

The fixture therefore keeps neither the absolute scale nor the real delay. Archive the full IR
WAVs and the report JSON beside the fixture's provenance.

**A measured fixture must not be band-limited again.** The harness passes every simulated path
through `band_limited()` (`tests/support/rooms.h`). This filter has an 80 Hz highpass and a
16 kHz lowpass, and it stands in for the missing loudspeaker model. A measured sweep already
contains the real loudspeaker and microphone. A per-fixture `measured: true` flag that would skip
`band_limited()` is planned (M8 of the production-readiness plan) and not built. Until it exists,
a measured fixture run through the current harness is filtered twice. Say so beside any number
taken from such a run.

## 5. Latency

### 5.1 Round-trip measurement

Run this once per session, with the electrical loopback cable patched:

```sh
python3 tools/fixtures/measure_rir.py loopback --device NAME --out-ch A --in-ch B --blocksize 64
```

- Every repeat opens a fresh PortAudio stream, so the spread covers stream-start variation.
  `--repeats` defaults to 10, and the result goes to `loopback_bsN.json`.
- The excitation is the `--short` preset: a 1 s sweep with 0.1 s pre-silence and 0.5 s
  post-silence.
- Run it at the block size the rig will use. `--blocksize` defaults to 64.

The tool reports two estimates per repeat (`measure_rir.py`, step 5):

- **The parabolic-peak estimate.** The largest |h| is refined over the three samples around it.
- **The group-delay estimate.** The phase slope of H is fitted over 1–10 kHz, weighted by |H|².

The two should agree within a fraction of a sample. If they do not, the loopback is suspect. The
group-delay figure is the round trip R used below; the parabolic one is used only if the fit
fails. The tool reports the minimum, median and maximum over the repeats. The **min–max spread**
of the group-delay estimate is the **jitter margin** used in §5.2. The tool also records
PortAudio's own latency report. That is a cross-check, not R.

Enter the measured round trip by hand where the rig needs it. That includes `@loop_ms`: the
external's description says the loop period comprises "@block, the host's I/O buffers and
converters, and the acoustic flight from speaker to mic". So `@loop_ms` is R plus the loudspeaker
latency (§5.3) plus the flight time, not R alone.

### 5.2 Setting the reference delay for afc_chain

The formula and its reasoning are in `include/mutap/afc_chain.h`, "THE REFERENCE":

- The canceller input u is what the DAC plays, fed back through a delay line of
  `reference_delay_samples`.
- The chain always lags the reference by one block. The speaker block written now depends on
  this block's cancellation. So `u[n] = speaker[n - block_size - reference_delay_samples]`.
- On a rig, a speaker sample written at stream index j reaches the mic stream at j + R. Set:

```
reference_delay_samples = R - block_size - jitter margin
```

The margin is the loopback repeats' min–max spread. If the result comes out negative, use 0. The
direct path then lands that much earlier than flight plus margin. It never lands before tap 0,
because R ≥ block_size on any rig that writes the speaker block after reading the mic block.

When the delay is right, the filter's taps model the room and not the buffers. The direct path
lands at the acoustic flight time plus the margin. Without the delay it lands R − block_size
samples late, the filter spends taps on dead delay, and the room's tail is cut off the end of the
filter. The header says the loudspeaker DSP latency (§5.3) "belongs to" R too.

**Readback check.** `reference_aligned()` reads the largest tap of the learned filter. On a
correct setting it sits near the flight time plus the margin. On tonal material the readback is
biased (HANDOFF.md item 9). On a held note, the largest tap is the closed-loop bias at the lag
where the reference is in phase with the note. Read it on broadband material: speech or the
backing track.

**In the Max patch.** `mutap.afc~` takes u on its inlet 1, and the patch's own delay ahead of
that inlet plays the role of `reference_delay_samples`. `mutap.afc~` wraps the canceller, not
`afc_chain`. It adds its own `@block` of latency on the cleaned output (its description), and
Max adds signal-vector buffering. The exact offset for that path is not derived in this
document. The patch's delay must be worked out from the same reasoning and checked with the
readback on broadband material.

### 5.3 Loudspeaker DSP latency

A powered loudspeaker's own DSP and amplifier add a fixed delay that the electrical loopback
never sees.

- **Acoustic loopback** (the loop closes through the speaker): this delay is already part of R.
- **Electrical loopback:** it must be measured separately, and added where R is used.

The procedure:

1. Measure the electrical round trip with the cable (§5.1).
2. Measure acoustically through the speaker, with the measurement mic at a taped, known distance
   d on axis. Use `measure_rir.py measure ... --loopback-channel K` with the cable still on input
   K. That run reports each acoustic channel's delay as the onset minus the round trip
   (`measure_rir.py`, step 6).
3. Subtract the flight time d / 343 m/s (the tool's `SPEED_OF_SOUND`; the speed of sound depends
   on the air temperature, so log the temperature with d).

The difference is the loudspeaker's DSP latency. This is a procedure. No speaker's latency has
been measured for this repo.

## 6. The gain ramp

The ramp is the same in a room and in the simulated loop. The simulated version is
`karaoke_ramp_dump.cpp`, whose defaults are the protocol's:

1. **Warm-up.** 30 s at a constant start gain, 20 dB under the open-loop limit (`--warmup 30`,
   `--start-below 20`). In the simulated loop the open-loop limit is the dry loop's phase-exact
   MSG, `exact_msg_db`. In a room it is the dry loop's measured limit, which comes from the
   canceller-off ramp (§8 step 4). That run's own start gain can only be a rough walk-up. If its
   limit comes out less than 15 dB above its start gain, the calibration rule in §3 is broken:
   run it again from 20 dB under the limit it found.
2. **Ramp.** +1 dB per 2 s, which is 0.5 dB/s (`--rate 0.5`). The simulated loop updates the gain
   every block. The ramp continues until the loop runs away or an audible howl.
3. **The gain log.** Rows `time_s,gain_db` of absolute bus gain. The dump writes one row per
   block (`PREFIX.gain.csv`). The criterion takes it with `--gain-log`, and by default its
   calibration window is the log's first constant segment, the warm-up (step 3).

**Two limits, two rules.**

- **The ramp's own runaway rule.** In the simulated loop this is the harness's rule: a 64-sample
  block at 40 dB over the unit-RMS near end. The dump records that gain and time, then runs 2 s
  more (`--tail`) or stops at 40 dB over the MSG (`--max-over`). In a room there is no unit-RMS
  near end, and no tool defines a room runaway rule. Until one exists, record the gain at which
  the ramp was stopped, and label it as such.
- **The audible limit.** The criterion's limit (§7): the bus gain at the onset of the first
  qualifying event.

The two differ by protocol, not by error. With a shifter in the loop, recirculating partials
become audible below runaway (`howl_criterion.py`, "Known limits"). A slowly ramped, continuously
adapting canceller also holds more than one that was converged and then probed (HANDOFF.md item
9, "Ramp vs bisected runaway"). Report both, each labelled.

**The canceller adapts live from a cold start.** Reset it before each run. It converges during
the warm-up, and there is no separate convergence phase. The simulated loop does the same: the
live PEM + FD-Kalman canceller with the speech cascade at its default configuration.

**Three repeats per condition** (§1). In the simulated loop the repeats are seeds.

## 7. The offline howl criterion

### 7.1 The criterion

A howl is "a narrowband peak ≥ 20 dB over the program's spectral envelope, sustained ≥ 500 ms"
(`howl_criterion.py`). The program's envelope is explained power: each stem's fitted transfer
applied to the aligned stem. The steps:

- **Alignment.** GCC-PHAT between the calibration segment and each stem, over a ±2 s lag window.
  The lags are joint by default, anchored on the clearest stem. A peak-to-rival ratio below 2.0 is
  written as "alignment ambiguous".
- **Calibration.** A per-bin, non-negative least-squares fit of each stem's power transfer over
  the warm-up, smoothed over ±2 bins. The noise floor is the 10th percentile of the recording's
  power over the same frames.
- **Two analysis passes.**
  - The long pass: N = 8192, hop 1024 at 48 kHz (5.86 Hz bins, 21.3 ms). It is the protocol's
    resolution, and it separates a howl from the program's harmonics.
  - The short pass: N = 2048, hop 256, for drifting howls. It is on by default unless `--dechirp`
    is given.
  - N and the hop scale with the sample rate.
- **Dechirping.** `--dechirp` repeats the long pass at ±250, ±500 and ±800 Hz/s. A component
  sweeping at that rate then reads as a stationary tone. It is recommended for any run with a
  frequency shifter or another time-variant element in the loop. It costs about 4.6× the
  analysis.
- **The rule.** A bin is flagged when M/E ≥ 20 dB (`--excess-db`) and it is a narrowband peak:
  ≥ 10 dB over the median within ±1/3 octave (`--peak-db`). Flagged peaks are linked across
  frames (±3 bins per hop, one hop of dropout). A track lasting ≥ 500 ms (`--min-duration`)
  qualifies. The run's limit is the bus gain at the onset of the first qualifying event.
- **The reverberation hold.** With `--rt60`, the program's energy at a bin is held to decay at
  the room's rate, so a program stop is not unexplained power. `--chain-rt60` adds a reverb
  inside the chain; the hold uses the larger of the two per band. Without `--rt60` the tool
  warns: program stops then false-trigger in reverberant rooms.

These thresholds are the protocol's parameters. They are exposed as options with these defaults,
and every one is written into the run's JSON. Keep each run's `--plot` so that a person can check
the criterion was not fooled by the program.

### 7.2 What to record, and which signal to analyse

From `howl_criterion.py`, "What to record". The loop the ramp probes closes through whatever the
chain leaves of the room path. With a canceller holding F̂, its output is c = v / (1 − G(F − F̂)H).
The stems therefore arrive at c at fixed levels while that residual loop is stable, and a howl
shows up as its instability.

| Signal | Rig channel | Use |
|---|---|---|
| c, the canceller (or bypass) output | 1 | the documented input; aligned in every configuration tested |
| the chain output, before the gain | 9 | valid while the chain is time-invariant (an in-chain reverb); not after a frequency shifter |
| the mic of a chain-on run | — | cross-check only |

Behind a frequency shifter, the chain output holds no coherent copy of the unshifted stems.
GCC-PHAT then finds a confident false peak, and the tool warns ("the edge of the window").

The mic hears v(1 + GF …). That grows with the gain long before anything is unstable, and a
static calibration cannot explain the growth. With the canceller bypassed, c is the mic.

### 7.3 Flags per condition

This table is `howl_criterion.py`'s "Flags per condition" merged with `reverb_audible.py`'s
analysis table:

| Condition | Analyse | Flags | Cross-check |
|---|---|---|---|
| dry, or canceller only | c | `--rt60 ROOM` | — |
| + frequency shift | c | `--rt60 ROOM --dechirp` | — |
| + reverb only | the chain output | `--rt60 ROOM --chain-rt60 T` | c |
| + shift + reverb (the product chain) | c | `--rt60 ROOM --chain-rt60 T --dechirp` | the chain output, only where it reports no alignment warning |

- **ROOM, in a real room**, is the room's T30 from the sweep report (§4). `--rt60` accepts the
  report JSON directly. It then takes each octave's T30 from 125 Hz to 8 kHz, falling back to the
  broadband T30 and then T20 (`--rt60-channel` picks the channel).
- **ROOM, in the simulated loop**, is the band-limited fixture's T30 at 4096 taps, from
  `measure_rir.py`'s `schroeder()`. `karaoke_audible.py`'s `cabin_t30()` computes it. The
  1024-tap path in the loop has too little decay range for a T30. For the cabin it is 0.0328 s
  (`docs/karaoke-afc.md`, "How it is measured").
- **T** is the reverb's own T30 for the settings in use. In the simulated loop it comes from the
  dump's `--reverb-ir-only` impulse response through `schroeder()` (`reverb_audible.py`).

Analysing c with the reverb's T30 in the hold is what counts a reverb tail recirculating through
the shifter, because that tail is part of c's loop. No `--dechirp` is used without a shift.

The simulated analysis channels are those of the dump's output: c is the dump's channel 0, and
the chain output is its channel 3 (`karaoke_ramp_dump.cpp` header). On the rig they are columns 0
and 8 (§2).

A typical rig invocation:

```sh
python3 tools/fixtures/howl_criterion.py analyze RUN.wav --channel 0 \
    --program voice.wav [--program track.wav] --gain-log RUN.gain.csv \
    --rt60 ROOM_report.json [--chain-rt60 T] [--dechirp] --plot RUN.png
```

Drop `--program track.wav` for runs without the backing track.

### 7.4 Aggregate

```sh
python3 tools/fixtures/howl_criterion.py aggregate RUN1.howl.json RUN2.howl.json RUN3.howl.json
```

The command prints the median, the minimum, the maximum and the range of the runs' limits. With
`--json` it also writes them. If some runs reached no limit, it warns: the median then covers
only the runs that did. Report the median and the range over the three repeats, and the count of
runs that reached a limit.

## 8. Run sheet for the first listen (M1)

Per room, in this order. Log the host, interface, buffer size, room, positions, distances and
temperature before step 1.

1. **Rig setup and loopback** (§5.1). Patch the cable and run `measure_rir.py loopback` at the
   rig's block size. Keep the JSON. Enter `@loop_ms` by hand from R, the loudspeaker latency and
   the flight (§5.1, §5.3). Set the patch's reference delay (§5.2).
2. **The sweep** (§4). Run `measure_rir.py measure` with the loopback channel. Save the raw
   recording, the sidecar, the report and both IR WAVs. Check the peak-to-noise and the clipping
   flag. A `null` T30 means measure again. This report is ROOM for every run in the room.
3. **Gain structure.** Set `@ceiling` and `@cap` from the soundcheck's gain structure. The
   `@ceiling` description puts it "between the programme's peak and the limiter". The `@cap`
   description gives the dry limit − 6 dB, which is known only after step 4's canceller-off ramp.
   Until then the guard stays off.
4. **The gain walk**, with the canceller off and then on, speech and then sung material. The
   guard is off (§1). Three repeats per condition, with the canceller reset before each. Each
   ramp is recorded per §2, with c and the chain output. It is run through the criterion offline
   with the §7.3 flags. `tools/notebook/karaoke_audible.py` applies the same criterion to the
   simulated loop. For a room, call `howl_criterion.py` directly.
5. **`calibrate`**, with and without the backing track. This is the guard's soundcheck: `@guard
   1`, `@kalman 1`, `@shadow` > 0, sampling for `@cal_s` seconds. Log the `calibrate_done d_db
   a_db` it sends. The soundcheck margins were measured in one simulated loop with the backing
   track only. A soundcheck without the track was not measured (HANDOFF.md item 12), so this is
   the first such reading.
6. **Frequency-shift A/B at 2 and 5 Hz**: the ramp with the shift, against the canceller alone.
   Analyse c with `--dechirp` (§7.3). Shift-condition limits measured with this criterion
   understate the shifter's stable gain against a white-noise MSG (`howl_criterion.py`, "Known
   limits"). Say which definition a number uses.
7. **Both predictors and both engines.** Run the speech cascade and the warped predictor
   (`@warp`), and the NLMS and Kalman engines (`@kalman 0` / `1`). Each combination gets the same
   ramp and the same analysis.
8. **The artefact log.** Name every audible artefact in a log, with the time and the bus gain:
   ringing, whine, warble, pumping, dropouts, guard ducks. Keep the log beside the run's WAV and
   the criterion's JSON.

**Pass direction.** In each room, the canceller raises both the ramp's runaway limit and its
audible limit over the canceller-off ramp (medians of three). If a room fails this, that is a
finding. Record it with its data. It is not a failure of the protocol.

**Where results go.** Session 1 is to be written up in `docs/real-room.md`. That file does not
exist yet. Every number in it carries its host, interface, room and the "median of 3" with its
range.

## 9. Hosts and provenance

Every simulated number elsewhere in this repo is quoted with its host: OS, architecture, compiler
and build type (`docs/karaoke-afc.md`, "Hosts"; `docs/reverb-afc.md`, "Hosts"). The closed loop
is chaotic, so platform arithmetic moves single rows even on identical signals. A real-room
number needs the same: the host, the interface and its buffer setting, the room, the positions,
and the tool versions (the commit of this repo and of MuTap-Max).

Nothing in this document is a measured result. The figures in it are tool defaults and
thresholds, each cited to its tool, plus the two simulated figures it cites from
`docs/karaoke-afc.md` as examples.
