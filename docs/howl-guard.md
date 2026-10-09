# The safety layer: a per-mic howl guard

*Measured September–October 2026 in simulation: six rooms from both generator
families, band-limited through a loudspeaker model, every run live from a
cold start. Host: macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release, on
a shared machine ([Hosts](#hosts)). Nothing here has been heard in a real
room. Every number below comes from the gated rows of
`tests/test_howl_guard_host.cpp` or its `MUTAP_SLOW` sweep;
[Provenance](#provenance) gives the commands.*

`tap::mu::howl_guard` (`include/mutap/howl_guard.h`) is the gain policy that
sits between the cancellers and the voice bus of `afc_chain`. Per mic it
reads the canceller's two convergence statistics (D, the shadow comparator
ratio, and A′, the identification progress) and a `howl_detector` on the
canceller's residual, and it sets that mic's gain:

- **ARMING** from reset: 30 dB down until the verdict (D < −1.235 dB and
  A′ < −23.842 dB by default; per mic from the soundcheck once calibrated)
  has held for 1.5 s with the detector quiet. After 10 s without a
  declaration it raises `unprotected`. When the host has set a cap (the
  dry limit − 6 dB), it opens to the cap instead. **The cap is mandatory
  for opening without a declaration**: without one the mic stays in ARMING,
  30 dB down, for as long as the verdict does not declare.
- **OPEN** at full gain, less 3 dB per strike.
- **DUCKED** 20 dB down on a detector trip (the level catch always; the
  growth path only while the verdict is not ok) or on the verdict lost
  (ok → not-ok held 0.3 s). It releases when the verdict has held ok for
  1.5 s with the detector quiet, or on a timer (5 s × 2^strikes) when the
  verdict cannot recover. After a timer re-arm the verdict trigger stays
  disarmed until the verdict has held ok for 1.5 s (the release hold), not
  merely read ok once.
- **RELEASING** ramps back over 200 ms; **LATCHED** is the end state after
  three strikes. A strike is a detector trip within 10 s (probation) of
  entering OPEN, or in RELEASING or ARMING, and (by default,
  `lost_in_probation_strikes`; [below](#a-lost-in-probation-as-a-strike))
  a verdict LOST in OPEN within probation.

Two mics are attributed by the detectors' band level at the shared howl
frequency, with a duck-all fallback. A bus stage in `afc_chain`'s safety
slot repeats the duck after the reverb. A soundcheck sampler
(`calibrate_begin()` / `calibrate_end()`) sets each mic's verdict
thresholds from the medians of D and A′ over the protocol's 30 s
track-through, plus a margin. The header lists every transition.

## Headline

| Claim (the 40 dB rule is the howl oracle) | Gated rows (cabin, mt5) | `MUTAP_SLOW` sweep (six rooms, five seed sets) |
|---|---|---|
| Guarded cold starts that reached the 40 dB rule | 0 of 126 | 0 of 690 |
| … at the canceller's limit + 6, unguarded twins | 575 blocks, 3 of 6 runs | — |
| Declaration with the backing track, median | 1.77 to 2.07 s | 1.76 to 2.11 s |
| Declaration without it | 0 runs: with the cap all reach OPEN_CAPPED at 10.00 s; without it all 18 stay in ARMING | 0 runs: 180 of 180 through the cap; without it 90 of 90 in ARMING for all 20 s |
| Walk at exact − 6: release − misalignment-oracle reconvergence, median [min]; strikes | 1.59 s [1.38], 0 early of 3; 3 (option off: 0) | 1.60 s [1.38], 0 early of 6; 6 (off: 0) |
| Walk at the limit − 6, factory thresholds: ducks / howl blocks | 0 of 12 / 0 | 0 of 30 / 0 |
| Walk at the limit − 6, soundcheck thresholds: ducked; release − reconvergence, median [min]; early; howl blocks | 12 of 12; 1.74 s [1.63]; 0; 0 | 30 of 30; 1.74 s [1.63]; 0; 0 |
| Stable material at the limit − 6, soundcheck thresholds: runs with a duck | 0 of 8 | 0 of 180 |
| Walk at the limit + 6 (rehearsal → hall): howl blocks guarded / unguarded | 0 / 1203 | — |
| F → 2F: howl blocks; LOST-ducks after the timer re-arm | 0; 0 in 8 runs (PR B: 16 in 6 of 8) | 0; 0 in 30 runs (PR B: 47 in 20 of 30; option off: 1 in 1 of 30) |
| F → 2F, cabin (walk-path releases): LOST-ducks a run, median; runs latched | 4 in 30 s; 1 of 2 (option off: 4; 0) | 3 in 30 s, 4 of 5; 4 in 120 s, 5 of 5 latched at the cap, 0 ducks after (off: 4 and 19 a run, none latched) |
| F → 2F, the other rooms: restore level at the end; timer re-arm | −3.00 dB; 10.00 s (option off: 0.00; 5.00) | −3.00 dB at 30 s, 0.00 at 120 s; 10.00 s (off: 0.00, mt5 and mt9 −3.00; 5.00) |
| Two mics, one forced: first TRIP on the wrong mic alone; howl blocks | 0 of 8; 1 | 1 of 20; 3 |
| Stable material at the limit − 6: ducks off a loop-born burst | 0 in 24 runs | 0 in 180 runs |
| Dattorro in the loop: voice 0.5 s after a trip, with / without the bus stage | −66.53 / −47.13 dB | — |
| Cost per block, one mic, float / double | 0.57 / 0.84 % of a canceller | — |

*macOS 15.7 x86_64, AppleClang 17, Release, double; S1 (10 ms) unless a
section says otherwise.*

- **No guarded single-mic run reached the 40 dB rule**, including the stress
  rows where the unguarded loop howled. With two mics and one forced into a
  howl, a few blocks reached it (1 gated, 3 in the sweep, all mt5+mt9).
- **It is not free of audible cost where the verdict is weak.** Without a
  backing track no run declares from ARMING, so those runs sit at the cap,
  and without a cap they stay 30 dB down. Under a louder coupling (F → 2F)
  the timer re-arm does not pump, and where the guard releases on the walk
  path (cabin) each LOST in probation is a strike: the cycle ends at the
  latch after a median 4 ducks, the mic held at the cap. Every other room
  pays one 3 dB strike (decayed after 60 s in OPEN) for the LOST the
  change itself causes inside the cold start's probation.
- **The factory thresholds do not see a walk at the operating point; the
  soundcheck's do.** With the thresholds set from 30 s of the song (median
  + 4 dB for D, + 3 dB for A′), every walk ducked and released after the
  canceller had re-identified, and stable material ducked in none of 180
  runs. With the factory thresholds no walk ducked and none howled: the
  canceller re-identifies on its own there.

## How it is measured

**The loop** (`tests/support/guard_loop.h`) is `afc_chain` itself, with the
guard attached through `set_guard()`, in a simulated room: each mic hears
its near end plus the speaker feed through its own band-limited 1024-tap
path after an electrical delay (S1 = 480 samples, 10 ms; S3 = 960). The
reference is aligned as `afc_chain` documents (the direct path at tap 32).
The forward gain K, and the optional 2 / 5 Hz frequency shift (the harness's
exact-ramp shifter), ride in the decorrelator slot, so the cancellers
identify the room and not the gain. The canceller is PEM + FD-Kalman with the
speech cascade and the 2-partition shadow. Every run starts from reset: cold
cancellers, the guard in ARMING.

**Materials.** "voiced" is the harness's held 300 Hz note, "music" the
A-major chord, "speech" the AR(4) speech envelope; "+aux" adds the white
backing track at −12 dB into the speaker (after the chain, so the reference
carries it).

**Operating points**, stated per row:

- **The canceller's limit − 6 dB** for the protection claims. The limit is
  bisected as the karaoke and two-mic suites do: 1500 blocks of convergence
  at the dry limit − 6 dB, then the forward gain bisected to 0.25 dB on a
  10 s probe of the next seed, with a fresh copy of the converged canceller
  per probe; the median over seed sets 1, 21, 41, 61, 81 is the table in
  `tests/test_howl_guard_host.cpp` ([Operating points](#operating-points)).
- **exact_msg_db + 3 / + 6 dB** (over the dry loop's phase-exact limit) and
  **the canceller's limit + 6 dB** for the stress rows.
- **exact_msg_db − 6 dB**, the release experiment's point, for the walk
  timing.

The host's cap is set as the protocol would set it: the dry limit − 6 dB,
relative to the operating gain.

**Oracles** (`tests/support/guard_runs.h`). None reads the guard's own
statistics:

- **the 40 dB rule**: a residual block at RMS ≥ 100 over the unit-RMS near
  end (the harness's howl criterion). It is the howl oracle every claim
  below uses.
- **the frozen-estimate margin**: the phase-exact limit of the loop the
  canceller's current snapshot would close, at the current gains, minus K.
  It is reported as the time it spent below 0 dB. At these operating points
  it reads "unstable" for most of a run even when nothing howls (median
  18.03 s of 20 on voiced + aux at the limit − 6): the adaptive loop holds
  where the frozen snapshot would not, as the release experiment found at
  +3 / +6 dB. It is not a howl oracle here, and it is undefined with a
  shift or a reverb in the loop.
- **the misalignment oracle** (walks): the filter's misalignment over the
  taps it covers; "reconverged" is the start of a 0.3 s run back under the
  pre-walk median + 3 dB.
- **the burst oracle**: a block within 0.5 s of a block at +20 dB.

The detector's ceiling is the harness's: +30 dB re the unit near end, 10 dB
under the 40 dB rule.

**The policy** is the default throughout, `lost_in_probation_strikes` on
(Tim, 2026-10-09). Where turning it off changes a printed number, the row
says so; [A LOST in probation as a strike](#a-lost-in-probation-as-a-strike)
has every off → on comparison. The cold-start, soundcheck, two-mic,
audible-cost, bus-stage and limit + 6 tables read the same either way.

## Cold start

Every run starts from reset at the operating gain, with the protocol's cap,
and runs 20 s. Gated rows: cabin and mt5, seeds 1, 21, 41 (6 runs a row),
S1 unless the row says otherwise.

| Row | Declared | Time to OPEN, median [max] (s) | Left ARMING through the cap | Time in ARMING, median (s) | Howl blocks (40 dB rule) | Frozen margin < 0, median (s) | Unguarded twins: howl blocks (runs) |
|---|---|---|---|---|---|---|---|
| voiced+aux, limit − 6 | 6 of 6 | 1.78 [1.79] | 0 | 1.78 | 0 | 18.03 | 0 (0 of 6) |
| music+aux, limit − 6 | 6 of 6 | 1.77 [1.78] | 0 | 1.77 | 0 | 15.79 | — |
| speech+aux, limit − 6 | 6 of 6 | 2.07 [2.17] | 0 | 2.07 | 0 | 1.13 | — |
| voiced, limit − 6 | 0 of 6 | — | 6 | 10.00 | 0 | 19.09 | — |
| music, limit − 6 | 0 of 6 | — | 6 | 10.00 | 0 | 16.64 | — |
| speech, limit − 6 | 0 of 6 | — | 6 | 10.00 | 0 | 0.00 | 1 (1 of 6) |
| voiced+aux, exact + 3 | 6 of 6 | 1.77 [1.78] | 0 | 1.77 | 0 | 17.33 | 0 (0 of 6) |
| voiced, exact + 3 | 0 of 6 | — | 6 | 10.00 | 0 | 19.00 | — |
| voiced+aux, exact + 6 | 6 of 6 | 1.78 [1.79] | 0 | 1.78 | 0 | 18.03 | 0 (0 of 6) |
| voiced, exact + 6 | 0 of 6 | — | 6 | 10.00 | 0 | 19.23 | — |
| voiced+aux, limit + 6 | 6 of 6 | 1.77 [1.79] | 0 | 1.77 | 0 | 14.77 | 575 (3 of 6) |
| voiced, limit + 6 | 0 of 6 | — | 6 | 10.00 | 0 | 19.69 | 0 (0 of 6) |
| voiced+aux, 2 Hz shift | 6 of 6 | 1.77 [1.79] | 0 | 1.77 | 0 | — | — |
| voiced, 2 Hz shift | 0 of 6 | — | 6 | 10.00 | 0 | — | — |
| voiced+aux, 5 Hz shift | 6 of 6 | 1.78 [1.78] | 0 | 1.78 | 0 | — | — |
| voiced, 5 Hz shift | 0 of 6 | — | 6 | 10.00 | 0 | — | — |
| voiced+aux, S3 | 6 of 6 | 1.77 [1.78] | 0 | 1.77 | 0 | 18.06 | — |
| voiced, S3 | 0 of 6 | — | 6 | 10.00 | 0 | 19.39 | — |

*macOS 15.7 x86_64, AppleClang 17, Release; "exact" is exact_msg_db, the
dry loop's phase-exact limit.*

- **No guarded cold start reached the 40 dB rule.** That includes the
  canceller's limit + 6 dB, where three of the six unguarded voiced+aux twins
  howled (575 blocks).
- **With the backing track every run declares**, in a median 1.77 to
  2.07 s. ARMING costs that long at −30 dB.
- **Without it no run declares within 20 s**, on any material (held note,
  music, speech). The likely cause, not isolated by a test: at the arming
  gain the loop's own signal reaches the reference 30 dB down, so the
  canceller has little to identify from. The 10 s timeout and the cap are
  what let these runs open at all; without a cap they stay in ARMING with
  `unprotected` raised.
- **The unguarded cold start rarely howls in this loop.** One speech run of
  six at the limit − 6, none at exact + 3 / + 6. ARMING's value shows only at
  the limit + 6. The release experiment's rig, which howled from cold at
  + 3 / + 6 dB, ran a different loop (no reference alignment, a different
  backing track).

The sweep (`HowlGuardSweep.ColdStart`) runs every material: six rooms and
five seed sets at S1, cabin and mt5 at S3, with the shifts, and at exact + 3 /
+ 6 on cabin, mt5, rehearsal and hall.

| Row | Runs | Declared | Time to OPEN, median [max] (s) | Left through the cap | Howl blocks | Frozen margin < 0, median [max] (s) | Unguarded twins: howl blocks (runs) |
|---|---|---|---|---|---|---|---|
| voiced+aux, S1 | 30 | 30 | 1.77 [1.80] | 0 | 0 | 18.02 [18.07] | 0 (0 of 30) |
| voiced, S1 | 30 | 0 | — | 30 | 0 | 19.01 [19.39] | — |
| music, S1 | 30 | 0 | — | 30 | 0 | 16.58 [17.12] | — |
| music+aux, S1 | 30 | 30 | 1.78 [1.81] | 0 | 0 | 15.75 [17.53] | — |
| speech, S1 | 30 | 0 | — | 30 | 0 | 0.00 [0.00] | 1 (1 of 30) |
| speech+aux, S1 | 30 | 30 | 2.11 [2.42] | 0 | 0 | 0.78 [2.31] | — |
| voiced+aux, S3 | 10 | 10 | 1.77 [1.79] | 0 | 0 | 18.06 [18.08] | — |
| voiced, S3 | 10 | 0 | — | 10 | 0 | 19.37 [19.56] | — |
| music, S3 | 10 | 0 | — | 10 | 0 | 18.45 [18.82] | — |
| music+aux, S3 | 10 | 10 | 1.78 [1.79] | 0 | 0 | 17.53 [17.69] | — |
| speech, S3 | 10 | 0 | — | 10 | 0 | 0.00 [0.00] | — |
| speech+aux, S3 | 10 | 10 | 2.10 [2.19] | 0 | 0 | 4.62 [5.46] | — |
| each material, 2 Hz | 10 a row | +aux rows 10 of 10 | 1.76 to 2.08 (medians) | non-aux rows 10 of 10 | 0 | — | — |
| each material, 5 Hz | 10 a row | +aux rows 10 of 10 | 1.77 to 2.07 (medians) | non-aux rows 10 of 10 | 0 | — | — |
| each material, exact + 3 | 20 a row | +aux rows 20 of 20 | 1.77 to 2.11 (medians) | non-aux rows 20 of 20 | 0 | — | voiced+aux 0 of 20, speech 0 of 20 |
| each material, exact + 6 | 20 a row | +aux rows 20 of 20 | 1.78 to 2.11 (medians) | non-aux rows 20 of 20 | 0 | — | voiced+aux 0 of 20, speech 0 of 20 |
| voiced, S1, no cap | 30 | 0 | — | 0 (in ARMING for all 20 s) | 0 | 18.99 [19.45] | — |
| music, S1, no cap | 30 | 0 | — | 0 (in ARMING for all 20 s) | 0 | 16.45 [17.08] | — |
| speech, S1, no cap | 30 | 0 | — | 0 (in ARMING for all 20 s) | 0 | 0.00 [0.00] | — |

*macOS 15.7 x86_64, AppleClang 17, Release; 1890 s on 4 threads. This
PR's run; every row that PR B also ran reads the same to the last digit
(PR B's no-cap rows were cabin and mt5 only, 10 runs each).*

**0 howl blocks in 690 guarded cold starts.** Every run with the backing
track declared, every run without it left ARMING only through the cap, and
without a cap the held note, the music and the speech stayed in ARMING for
the whole 20 s.

### Without a backing track: the cap is mandatory

The verdict never declares on voice alone here, so a track-off run opens
only through the arming timeout, and only if the host has set the cap.
Without a cap the guard keeps the mic in ARMING, 30 dB down, raises
`unprotected` at the timeout and stays there for as long as the verdict
does not declare; nothing else opens a mic that has not declared, and
removing the cap re-arms every undeclared mic (a latched one too, whose
floor was the cap). The protocol therefore measures the cap at the
soundcheck and the track-off rows run under it. Gated rows
(`ColdStartWithoutBackingTrack`, `ColdStartWithoutACapStaysArmed`): cabin
and mt5, seeds 1, 21, 41, 20 s from reset at the limit − 6, the cap at the
dry limit − 6:

| Row | Cap | Runs | Declared | Reached OPEN_CAPPED | Time to OPEN_CAPPED, median [max] (s) | `unprotected` from, median (s) | Time in ARMING, median (s) | Ducks after OPEN_CAPPED (runs) | Howl blocks |
|---|---|---|---|---|---|---|---|---|---|
| held note (voiced) | dry limit − 6 | 6 | 0 | 6 | 10.00 [10.00] | 10.00 | 10.00 | 2 (2) | 0 |
| music | dry limit − 6 | 6 | 0 | 6 | 10.00 [10.00] | 10.00 | 10.00 | 0 (0) | 0 |
| speech | dry limit − 6 | 6 | 0 | 6 | 10.00 [10.00] | 10.00 | 10.00 | 0 (0) | 0 |
| held note (voiced) | none | 6 | 0 | 0 | — | 10.00 | 20.00 (all of it) | 0 | 0 |
| music | none | 6 | 0 | 0 | — | 10.00 | 20.00 (all of it) | 0 | 0 |
| speech | none | 6 | 0 | 0 | — | 10.00 | 20.00 (all of it) | 0 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release. "10.00" is block 7499 from
reset, 9.9987 s: the timeout's 7500th block.*

The held note's two ducks under the cap both fell within 0.5 s of a
loop-born burst (a residual block at +20 dB; the burst oracle): 0 ducks off
a burst. On macOS arm64 (CI jobs 110465782241 and 110465811203) the same
row ducked in 5 of 6 held-note runs, not classified there. The gate is
therefore on howl blocks, the OPEN_CAPPED time and the runs with a duck
off any burst (at most 6 of the 18; 0 on Intel), not on the duck count.

The sweep's track-off rows (six rooms, five seed sets, 30 runs a row, the
table above): under the cap every run reached OPEN_CAPPED at 10.00 s, 0
howl blocks; the held note ducked after OPEN_CAPPED in 11 of 30 runs at S1
(9 of 20 at exact + 3, 4 of 20 at exact + 6), music in 1 of 20 at
exact + 3, speech never. Before a declaration the verdict trigger is
disarmed, so each of these ducks is a detector TRIP. The sweep ran before
the burst classification was added, so these are not classified; the gated
row's ducks were all on bursts. Without a cap: 90 of 90 runs in
ARMING for the whole 20 s, `unprotected` from 10.00 s, 0 howl blocks.

## Walks to another room

A walk (S2a) swaps every mic's room at 10 s; the run lasts 25 s. Gated:
the six walks of the release experiment, voiced + aux, seeds 1 and 21, at
two operating points.

| Operating point | Walks | Ducked | By LOST | Duck after the change, median (s) | Strikes (option off) | Releases | Release − reconvergence, median [min] (s) | Early | Howl blocks after the walk | D before the walk, median (dB) | Longest not-ok run after it, median [max] (s) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| exact_msg_db − 6 | 12 | 3 | 3 | 0.36 | 3 (0) | 3 | 1.59 [1.38] | 0 | 0 | −7.91 | 0.25 [0.47] |
| the limit − 6 | 12 | 0 | — | — | 0 (0) | 0 | — | — | 0 | −8.46 | 0.21 [0.22] |

*macOS 15.7 x86_64, AppleClang 17, Release. The release instant is the ramp
start (RELEASING entry); "reconvergence" is the misalignment oracle's. Every
duck at exact − 6 is a strike: the walk at 10 s comes 8.2 s after the
declaration, inside its probation, so each restores to −3 dB.*

- At the release experiment's operating point the guard reproduces its live
  H1.5 policy: the ducks come 0.36 s after the change and the releases 1.38
  to 1.60 s after the oracle reconverged (studio → rehearsal 1.59 and 1.60,
  mt5 → mt105 1.38). Only 3 of 12 walks ducked; the gated set is two seeds,
  and the sweep below has five.
- **At the limit − 6 no walk ducked, and none howled.** Before the walks D
  sits at a median −8.46 dB, far under the −1.235 dB threshold (calibrated
  in the release experiment's loop); after a walk the verdict is lost for
  at most 0.22 s, under the 0.3 s trip hold. At exact − 6 the longest
  excursions reach 0.47 s, and those are the walks that ducked.

The sweep runs the same six walks over five seed sets:

| Walk | At exact − 6: ducked (by LOST) | Duck after the change, median (s) | Strikes (option off) | Release − reconvergence, median [min] (s) | Early | At the limit − 6: ducked | Howl blocks (both points) |
|---|---|---|---|---|---|---|---|
| studio → rehearsal | 5 of 5 (5) | 0.33 | 5 (0) | 1.60 [1.46] (off: [1.44]) | 0 | 0 of 5 | 0 |
| rehearsal → hall | 0 of 5 | — | 0 (0) | — | — | 0 of 5 | 0 |
| hall → cabin | 0 of 5 | — | 0 (0) | — | — | 0 of 5 | 0 |
| cabin → studio | 0 of 5 | — | 0 (0) | — | — | 0 of 5 | 0 |
| mt5 → mt105 | 1 of 5 (1) | 0.36 | 1 (0) | 1.38 [1.38] | 0 | 0 of 5 | 0 |
| mt9 → mt109 | 0 of 5 | — | 0 (0) | — | — | 0 of 5 | 0 |
| pooled | 6 of 30 (6) | 0.34 | 6 (0) | 1.60 [1.38] | 0 | 0 of 30 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release; the default policy, the
option-off values in brackets where they differ.*

Every release came after the misalignment oracle had reconverged (6 of 6,
the earliest 1.38 s after it). The verdict sees one walk in five at the
experiment's point, and none at the canceller's limit − 6.

### With the soundcheck's thresholds

The factory thresholds (−1.235 / −23.842 dB) are the release experiment's
calibration; in this loop D sits near −8 dB, so at the operating point they
never see a walk. With the soundcheck calibration applied
([below](#the-soundcheck-calibration)), the same six walks at the limit − 6:

| Thresholds | Walks | Ducked after the walk | By LOST | Duck after the walk, median (s) | Ducks between the soundcheck and the walk | Releases | Release − reconvergence, median [min] (s) | Early | Howl blocks |
|---|---|---|---|---|---|---|---|---|---|
| Factory, sweep (re-run here) | 30 | 0 | — | — | — | 0 | — | — | 0 |
| Soundcheck, gated (seeds 1, 21) | 12 | 12 | 11 | 0.31 | 0 | 12 | 1.74 [1.63] | 0 | 0 |
| Soundcheck, sweep (five seed sets) | 30 | 30 | 29 | 0.31 | 0 | 30 | 1.74 [1.63] | 0 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release. Soundcheck over the first 30 s
from reset, the walk at 40 s, 55 s runs; the one duck not by LOST in
each row was a detector TRIP (mt5 → mt105).*

Every walk ducked 0.30–0.31 s after the change (the trip hold), and every
release came after the misalignment oracle had reconverged, the earliest
1.63 s after it. Per walk in the sweep, release − reconvergence median
[min]: studio → rehearsal 1.74 [1.71], rehearsal → hall 3.71 [1.70],
hall → cabin 1.75 [1.72], cabin → studio 1.88 [1.81], mt5 → mt105 1.68
[1.67], mt9 → mt109 1.67 [1.63].

## The soundcheck calibration

`calibrate_begin()` starts a sampler that takes each mic's D and A′ once per
block for `calibrate_s` (30 s, the protocol's track-through).
`calibrate_end(apply)` returns, per mic, the medians, 95th percentiles and
maxima, and suggests `d_db` = D's median + `cal_d_margin_db` and `a_db` =
A′'s median + `cal_a_margin_db`. With `apply` each mic runs on its own
thresholds; `set_thresholds()` restores a stored soundcheck and
`clear_calibration()` returns to the policy's.

The sampler is a fixed histogram per mic and statistic: 0.1 dB bins, D over
[−60, +20) dB and A′ over [−100, +20) dB, 2000 counts (8 KB) per mic,
allocated by the constructor. It costs O(1) per block, is exact to the bin
width for any quantile, reads the same on every platform and needs no
random source. A reservoir would need a random source and an O(n)
selection at the end; a streaming quantile (P²) has no error bound on the
bimodal window a cold start produces. Applied thresholds re-arm the verdict
trigger (it needs an ok block under them first) and survive `reset()`, like
the cap.

**How the margins were measured** (`HowlGuardSweep.CalibrationMargins`).
Each run starts from reset at the canceller's limit − 6 with the backing
track, samples the first 30 s and continues with one of two endings. The
stable runs play 30 s more of the same song: the 180 audible-cost runs (six
rooms × five seed sets × voiced+aux and speech+aux × 0 / 2 / 5 Hz). The
walk runs walk at 40 s (the six walks × five seed sets). The live guard
keeps the factory thresholds. Beside it, 72 shadow guards, one per margin
pair, take the same residual and statistics every block and apply their
own soundcheck at 30 s. Until a shadow's gain first differs from the live
guard's, the loop it would have closed is the live loop, so its first duck
is exact. The live guard never ducked in these runs, so every count below
is exact (0 inexact in 210 runs × 72 shadows).

Stable runs with a duck after the soundcheck, of 180:

| D margin \ A′ margin | + 0 dB | + 1 to + 10 dB, or A′ off |
|---|---|---|
| + 0 dB | 174 | 140 |
| + 0.5 dB | 135 | 36 |
| + 1 dB | 131 | 11 |
| + 1.5 dB | 131 | 7 |
| + 2 dB | 131 | 3 |
| + 3 dB | 131 | **0** |
| **+ 4 dB (default)** | 131 | **0** |
| + 6 dB | 131 | 0 |
| D off | 131 | 0 |

Walks seen (a duck within 5 s of the change and none between the soundcheck
and the walk), of 30; in brackets, the runs that ducked before the walk:

| D margin \ A′ margin | + 0 | + 1 | + 2 | + 3 (default) | + 4 to + 10, or off |
|---|---|---|---|---|---|
| + 0 dB | 0 [30] | 2 [28] | 2 [28] | 2 [28] | 2 [28] |
| + 0.5 dB | 0 [30] | 13 [17] | 13 [17] | 13 [17] | 13 [17] |
| + 1 dB | 0 [30] | 21 [9] | 21 [9] | 21 [9] | 21 [9] |
| + 1.5 dB | 0 [30] | 24 [6] | 24 [6] | 24 [6] | 24 [6] |
| + 2 dB | 0 [30] | 28 [2] | 28 [2] | 28 [2] | 28 [2] |
| + 3 dB | 0 [30] | 30 [0] | 30 [0] | 30 [0] | 30 [0] |
| **+ 4 dB (default)** | 0 [30] | 30 [0] | 30 [0] | **30 [0]** | 30 [0] |
| + 6 dB | 0 [30] | 28 [0] | 18 [0] | 16 [0] | 16 [0] |
| D off | 0 [30] | 23 [0] | 1 [0] | 1 [0] | 1 [0] |

*macOS 15.7 x86_64, AppleClang 17, Release; 2079.35 s on 4 threads.
Detection latency, median: 0.30 s at D + 0.5 to + 3, 0.31 s at D + 4.*

- **A′ at its median is not a threshold.** Half the blocks sit above it, so
  every pair with A′ + 0 ducked in at least 131 of 180 stable runs. From
  A′ + 1 dB up, A′ caused no stable duck (the D-off row).
- **D needs + 3 dB** for 0 ducks over the 180 runs (+ 2 dB: 3 runs ducked;
  + 1.5: 7). The default takes **+ 4 dB**: one step of headroom over the
  smallest clean margin, and 2 dB over the last one that ducked. A′ takes
  **+ 3 dB** (a factor of 2 on the ratio), 2 dB over its smallest clean
  margin. At D + 3 and + 4, walk detection is 30 of 30 for every A′ margin
  of 1 dB or more.
- **D carries the walk.** With D off, A′ + 1 sees 23 of 30 walks and
  A′ + 2 only 1. With D + 6 the count falls to 16–28 of 30.

**What the soundcheck reads**: the sampler's medians at the limit − 6, the
median over five seed sets, in dB:

| Room | voiced+aux D (0 / 2 / 5 Hz) | voiced+aux A′ (0 / 2 / 5 Hz) | speech+aux D (0 / 2 / 5 Hz) | speech+aux A′ (0 / 2 / 5 Hz) |
|---|---|---|---|---|
| cabin | −7.75 / −6.45 / −6.55 | −27.75 / −30.45 / −30.45 | −7.15 / −7.15 / −7.15 | −29.55 / −29.55 / −29.55 |
| mt5 | −7.65 / −6.85 / −6.85 | −27.15 / −29.35 / −29.45 | −8.35 / −8.45 / −8.45 | −29.15 / −29.15 / −29.15 |
| studio | −8.45 / −7.65 / −8.35 | −26.25 / −29.05 / −29.25 | −7.85 / −7.85 / −7.85 | −28.15 / −28.05 / −28.15 |
| rehearsal | −9.45 / −11.35 / −12.05 | −26.05 / −27.95 / −28.15 | −7.25 / −7.25 / −7.25 | −26.15 / −26.15 / −26.15 |
| hall | −8.45 / −6.75 / −6.85 | −26.65 / −29.15 / −29.25 | −8.95 / −8.95 / −8.95 | −28.95 / −28.95 / −28.95 |
| mt9 | −8.85 / −7.05 / −7.05 | −26.55 / −28.65 / −28.95 | −8.85 / −8.85 / −8.85 | −28.35 / −28.35 / −28.35 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

Over the 210 runs D's median lands between −12.15 and −6.25 dB, 5 to 11 dB
under the factory −1.235, and A′'s between −30.55 and −25.85 dB, 2 to 7 dB
under −23.842. The 95th percentiles sit 0.5–3.3 dB over D's median and
0.3–3.5 dB over A′'s (medians per group). A′'s excursions above its 3 dB
margin are shorter than the trip hold here: none ducked. The 30 s window
includes the cold start, about 2 s of it before the declaration, and the
median is what that window reads.

What the soundcheck prints (`SoundcheckCalibrationOnStableMaterial`, one
line per run; two of its eight):

```
  cabin voiced+aux seed 1            soundcheck 22500 blocks: D median   -8.15 dB (p95   -5.55, max   +0.15); A' median  -27.75 dB (p95  -27.15, max   -0.05) -> d_db   -4.15, a_db  -24.75 (applied)
  mt5 speech+aux seed 1              soundcheck 22500 blocks: D median   -8.35 dB (p95   -7.35, max   +0.45); A' median  -29.15 dB (p95  -25.45, max   -0.05) -> d_db   -4.35, a_db  -26.15 (applied)
```

The maxima (+0.15 dB, −0.05 dB) are the cold start's first blocks.

**Applied** (`HowlGuardSweep.CalibrationApplied`): the live guard on its
soundcheck thresholds at the default margins, the same 180 + 30 runs.

| Row | Runs | Ducks after the soundcheck (runs) | Howl blocks | `d_db` applied, median [min, max] | `a_db` applied, median [min, max] |
|---|---|---|---|---|---|
| voiced+aux, 0 Hz | 30 | 0 (0) | 0 | −4.45 [−5.55, −3.55] | −23.65 [−24.85, −22.85] |
| voiced+aux, 2 Hz | 30 | 0 (0) | 0 | −2.95 [−7.45, −2.25] | −26.05 [−27.55, −24.85] |
| voiced+aux, 5 Hz | 30 | 0 (0) | 0 | −3.05 [−8.15, −2.35] | −26.25 [−27.55, −25.05] |
| speech+aux, 0 Hz | 30 | 0 (0) | 0 | −3.85 [−5.05, −3.05] | −25.35 [−26.55, −23.15] |
| speech+aux, 2 Hz | 30 | 0 (0) | 0 | −3.85 [−5.05, −3.05] | −25.35 [−26.55, −23.15] |
| speech+aux, 5 Hz | 30 | 0 (0) | 0 | −3.85 [−5.05, −3.05] | −25.35 [−26.55, −23.15] |
| the six walks, between the soundcheck and the walk | 30 | 0 (0) | 0 | medians −5.45 to −3.65 per walk, range [−5.55, −3.35] | medians −24.75 to −23.05 per walk, range [−24.85, −22.95] |

*macOS 15.7 x86_64, AppleClang 17, Release; 1227 s on 4 threads. The walk
runs after the change are in [Walks](#with-the-soundchecks-thresholds).*

**0 ducks in 180 stable runs and 30 of 30 walks seen**, each released after
the oracle. The gated rows (cabin and mt5, seeds 1 and 21): 0 ducks in 8
stable runs, 12 of 12 walks.

## Above the limits

The release experiment's late howl, seconds after a rehearsal → hall walk at
exact + 6 with the verdict never having declared, is the case the detector
exists for. Gated: seeds 1, 21, 41, 25 s, with unguarded twins.

| Gain | Guarded: howl blocks after the walk | Strikes (3 runs) | Latched | Unguarded: howl blocks | Longest unguarded howl (s) |
|---|---|---|---|---|---|
| exact + 6 | 0 | 0 | 0 | 0 | 0.00 |
| the limit + 6 (exact + 15.61) | 0 | 5 | 0 | 1203 | 0.55 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- **The late howl does not reproduce at exact + 6 in this loop.** This
  loop's canceller holds +6 dB with margin (rehearsal's limit is exact
  + 9.61), so the row runs at the canceller's limit + 6 instead.
- **There the guard holds the loop:** 0 howl blocks against 1203
  unguarded, with five strikes over the three runs and no latch.

The sweep adds hall → cabin and runs five seed sets, at exact + 6:

| Walk at exact + 6 | Guarded: ducks (by TRIP) | Strikes | Latched | Guarded howl blocks | Unguarded howl blocks | Longest unguarded howl (s) |
|---|---|---|---|---|---|---|
| rehearsal → hall | 0 | 0 | 0 | 0 | 0 | 0.00 |
| hall → cabin | 5 (5) | 5 | 0 | 0 | 35 | 0.04 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

In hall → cabin the unguarded loop howls briefly after the walk (35 blocks
over five runs, the longest 0.04 s); the guard ducks on the detector (five
TRIPs in five runs, a median 0.10 s after the walk) and nothing reaches the
40 dB rule.

## A louder coupling (F → 2F)

F → 2F (S2b) doubles every path at 10 s: the louder coupling the verdict
reads as mismatch for good. Gated: cabin, mt5, studio, hall, seeds 1 and 21,
voiced + aux at the limit − 6, 30 s; the default policy.

| Room | Runs ducked | Duck after the change, median (s) | Runs re-armed | Re-arm after the duck, median (s) | LOST-ducks after the change (median a run [max]) | LOST-ducks after the first re-arm | Strikes at the end | Runs latched | Restore level at the end, median [min] (dB) | Time ducked or releasing after the change, median | Howl blocks |
|---|---|---|---|---|---|---|---|---|---|---|---|
| cabin | 2 of 2 | 4.65 | 0 | — | 7 (4 [4]) | 0 | 5 | 1 of 2 | −6.00 [−9.00] | 0.65 | 0 |
| mt5 | 2 of 2 | 0.39 | 2 | 10.01 | 1 (1 [1]) | 0 | 2 | 0 | −3.00 [−3.00] | 0.51 | 0 |
| studio | 2 of 2 | 0.37 | 2 | 10.00 | 2 (1 [1]) | 0 | 2 | 0 | −3.00 [−3.00] | 0.51 | 0 |
| hall | 2 of 2 | 0.38 | 2 | 10.00 | 2 (1 [1]) | 0 | 2 | 0 | −3.00 [−3.00] | 0.51 | 0 |
| pooled | 8 of 8 | — | 6 of 8 | 10.00 [10.00, 10.01] | 12 | **0** | 11 | 1 of 8 | — | — | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release; `HowlGuardHost.LouderCouplingRearms`
(which also runs every row with the option off, gated both ways).*

- **Every run ducks, nothing howls, and the timer re-arm does not pump**
  (0 LOST-ducks after it in 8 runs; PR B: 16 in 6 of 8).
- **The change's own LOST is a strike.** It comes 0.37–0.39 s after the
  change at 10 s, inside the probation that began at the declaration
  (1.76–1.79 s), so the level drops to −3 dB and the re-arm waits
  5 s × 2 = 10.00 s.
- **Cabin releases on the walk path** (its verdict holds ok for 1.5 s while
  ducked) and loses the verdict again each time it opens; each of those
  LOSTs falls inside the new probation and strikes, and the third strike
  latches. Within these 30 s that is 7 LOST-ducks in 2 runs, as without the
  rule; the sweep's 120 s rows below show where it ends.
- The gates are counts with margin (every run ducked, 0 howl blocks, at
  most 4 LOST-ducks after a re-arm over 8 runs, cabin strikes at all, no
  other room below −6 dB), not zeros: the loop is chaotic and the rows also
  run on macOS arm64 and Linux in CI.

The sweep's six rooms over five seed sets (`HowlGuardSweep.LouderCoupling`),
at the gated rows' 30 s and at 120 s:

| Room, length | Runs ducked | Runs re-armed (re-arm after the duck, median s) | LOST-ducks after the change (median a run [max]) | TRIP-ducks | LOST-ducks after the first re-arm | Strikes at the end | Runs latched (reached LATCHED; after the change, median s) | Restore level at the end, median [min] (dB) | Gain at the end, median (dB) | Time ducked or releasing, median | Howl blocks |
|---|---|---|---|---|---|---|---|---|---|---|---|
| cabin, 30 s | 5 of 5 | 0 | 17 (3 [4]) | 0 | 0 | 14 | 4 of 5 (2; 16.25) | −9.00 [−9.00] | −26.00 | 0.59 | 0 |
| mt5, 30 s | 5 of 5 | 5 (10.00) | 3 (1 [1]) | 2 | 0 | 5 | 0 | −3.00 [−3.00] | −3.00 | 0.51 | 0 |
| studio, 30 s | 5 of 5 | 5 (10.00) | 5 (1 [1]) | 0 | 0 | 5 | 0 | −3.00 [−3.00] | −3.00 | 0.51 | 0 |
| rehearsal, 30 s | 5 of 5 | 5 (10.00) | 5 (1 [1]) | 0 | 0 | 5 | 0 | −3.00 [−3.00] | −3.00 | 0.51 | 0 |
| hall, 30 s | 5 of 5 | 5 (10.00) | 5 (1 [1]) | 0 | 0 | 5 | 0 | −3.00 [−3.00] | −3.00 | 0.51 | 0 |
| mt9, 30 s | 5 of 5 | 5 (10.00) | 5 (1 [1]) | 0 | 0 | 5 | 0 | −3.00 [−3.00] | −3.00 | 0.51 | 0 |
| cabin, 120 s | 5 of 5 | 0 | 19 (4 [4]) | 0 | 0 | 15 | 5 of 5 (5; 23.27), 0 ducks after it | −9.00 [−9.00] | −11.89 (the cap) | 0.12 | 0 |
| mt5, 120 s | 5 of 5 | 5 (10.01) | 2 (0 [1]) | 4 | 0 | 1 | 0 | 0.00 [−3.00] | 0.00 | 0.09 | 0 |
| studio, rehearsal, hall, mt9, 120 s | 5 of 5 each | 5 each (10.00) | 5 (1 [1]) each | 0 | 0 | 0 | 0 | 0.00 [0.00] | 0.00 | 0.09 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release; 974.9 s on 4 threads for the
four passes (these two and the same two with the option off). A strike
decays after 60 s in OPEN, so the 120 s rows' strike counts are those left
at the end. The singer and the backing track are generated per run length,
so the 30 s and 120 s rows are different trajectories, not one cut short.*

- **0 howl blocks in 60 runs**, and no LOST-duck after a timer re-arm.
- **Cabin's cycle ends at the latch.** Every 120 s cabin run latched, a
  median 23.27 s after the change, after 4 LOST-ducks, and ducked no more;
  the mic then holds the cap (−11.89 dB re the operating gain, the dry
  limit − 6) with `unprotected` raised. Without the rule the same rows
  take 19 LOST-ducks a run and are still cycling at 120 s.
- **Every other room pays one strike** for the change's LOST: −3.00 dB and
  a 10 s re-arm in the 30 s rows, decayed back to 0.00 dB by 120 s (mt5's
  −3.00 minimum there is a TRIP strike, as without the rule).

### The re-arm hold (PR C), measured with the option off

In PR B a timer re-arm disarmed the verdict trigger
(LOST) until the verdict read ok once. Under F → 2F the verdict does read ok
for a while at the re-armed gain, so LOST re-armed on the first ok block and
fired again at full gain: the guard cycled between ducked and open. Now,
after a timer re-arm, LOST arms only once the verdict has been continuously
ok for `release_hold_s` (1.5 s, the same hold as a release). Before and
after, same rows, same host, both with `lost_in_probation_strikes` off (the
policy PR C shipped):

| Room | Runs ducked | Duck after the change, median (s) | Runs re-armed | Re-arm after the duck, median (s) | LOST-ducks after the first re-arm (runs), before → after | All LOST-ducks after the change, before → after | Howl blocks after the change / after the re-arm | Time ducked or releasing after the change, median, before → after |
|---|---|---|---|---|---|---|---|---|
| cabin | 2 of 2 | 4.65 | 0 | — | 0 (0) → 0 (0) | 7 → 7 | 0 / 0 | 0.56 → 0.56 |
| mt5 | 2 of 2 | 0.39 | 2 | 10.01 | 4 (2) → 0 (0) | 5 → 1 | 0 / 0 | 0.96 → 0.51 |
| studio | 2 of 2 | 0.37 | 2 | 5.00 | 6 (2) → 0 (0) | 8 → 2 | 0 / 0 | 0.94 → 0.26 |
| hall | 2 of 2 | 0.38 | 2 | 5.00 | 6 (2) → 0 (0) | 8 → 2 | 0 / 0 | 0.93 → 0.26 |
| pooled | 8 of 8 | — | 6 of 8 | 5.00 [5.00, 10.01] | **16 (6) → 0 (0)** | 28 → 12 | 0 / 0 | — |

*macOS 15.7 x86_64, AppleClang 17, Release. "Before" is this PR's test
harness run on PR B's guard (55a62fd); the duck and re-arm columns are
identical before and after.*

- **The pump after a re-arm is gone in these rows**: 0 LOST-ducks after the
  first re-arm in 8 runs, against 16 in 6 of 8. The timer re-arm itself is
  unchanged (5.00 s; 10.01 s after a strike), and nothing howled before or
  after it.
- **Cabin still cycled, by another route.** Its verdict holds ok for 1.5 s
  while ducked, so it releases on the walk path (no timer re-arm), and the
  verdict is lost again at full gain: 7 LOST-ducks in 2 runs, before and
  after. The re-arm hold does not touch that path; counting a LOST within
  probation as a strike (now the default) does
  ([below](#a-lost-in-probation-as-a-strike)).
- The gate is a rate with margin (at most 4 LOST-ducks after a re-arm over
  the 8 runs, a quarter of the old 2.0 a run), not a zero: the loop is
  chaotic and the rows also run on macOS arm64 and Linux in CI.

The sweep's six rooms over five seed sets (`HowlGuardSweep.LouderCoupling`,
the option off):

| Room | Runs ducked | Duck after the change, median (s) | Runs re-armed | Re-arm after the duck, median (s) | LOST-ducks after the first re-arm (runs), before → after | All LOST-ducks, before → after | TRIP-ducks | Howl blocks | Time ducked or releasing, median, before → after |
|---|---|---|---|---|---|---|---|---|---|
| cabin | 5 of 5 | 2.53 | 1 | 16.73 | 1 (1) → 1 (1) | 18 → 18 | 0 | 0 | 0.55 → 0.55 |
| mt5 | 5 of 5 | 0.38 | 5 | 5.00 | 8 (4) → 0 (0) | 11 → 3 | 4 | 0 | 0.96 → 0.51 |
| studio | 5 of 5 | 0.37 | 5 | 5.00 | 14 (5) → 0 (0) | 19 → 5 | 0 | 0 | 0.94 → 0.26 |
| rehearsal | 5 of 5 | 0.37 | 5 | 5.00 | 0 (0) → 0 (0) | 5 → 5 | 0 | 0 | 0.26 → 0.26 |
| hall | 5 of 5 | 0.38 | 5 | 5.00 | 15 (5) → 0 (0) | 20 → 5 | 0 | 0 | 0.93 → 0.26 |
| mt9 | 5 of 5 | 0.37 | 5 | 5.00 | 9 (5) → 0 (0) | 14 → 5 | 3 | 0 | 0.95 → 0.77 |
| pooled | 30 of 30 | — | 26 of 30 | 5.00 [5.00, 16.73] | **47 (20) → 1 (1)** | 87 → 41 | 7 | 0 | — |

*macOS 15.7 x86_64, AppleClang 17, Release; 4 threads, 88.5 s before and
84.3 s after.*

0 howl blocks in 30 runs, before and after, and none after a re-arm. The one
LOST-duck left after a re-arm is cabin's (its single re-arm, at 16.73 s,
then the walk-path cycle). The TRIP-ducks (the detector, mt5 and mt9) are
unchanged.

### A LOST in probation as a strike

`guard_policy::lost_in_probation_strikes` makes a LOST in OPEN within
probation a strike, as a detector trip there already is: each nuisance duck
backs the restore level off 3 dB and doubles the re-arm timeout, and the
third latches. **It is on by default.** The bar first set for it was (1)
cabin's LOST-ducks after the change at most 1 a run, (2) no new ducks
elsewhere, (3) the other five rooms within 6 dB of the operating point.
(2) and (3) hold; (1) does not, and cannot for any rule that needs three
strikes to latch. Tim's decision (2026-10-09): on, because the cabin is the
karaoke scenario itself, 4 ducks then a stable capped level beats 19 ducks
in two minutes, and the 3 dB back-off for 60 s elsewhere is acceptable.

The F → 2F rows, the option off → on, in one run of each test:

| Room | LOST-ducks after the change (median a run [max]) | Strikes at the end | Runs latched | Restore level at the end, median [min] (dB) | Re-arm after the duck, median (s) | Time ducked or releasing, median | Howl blocks |
|---|---|---|---|---|---|---|---|
| cabin | 7 (4 [4]) → 7 (4 [4]) | 0 → 5 | 0 → 1 of 2 | 0.00 [0.00] → −6.00 [−9.00] | — (no re-arm) | 0.56 → 0.65 | 0 → 0 |
| mt5 | 1 (1 [1]) → 1 (1 [1]) | 1 → 2 | 0 → 0 | 0.00 [−3.00] → −3.00 [−3.00] | 10.01 → 10.01 | 0.51 → 0.51 | 0 → 0 |
| studio | 2 (1 [1]) → 2 (1 [1]) | 0 → 2 | 0 → 0 | 0.00 [0.00] → −3.00 [−3.00] | 5.00 → 10.00 | 0.26 → 0.51 | 0 → 0 |
| hall | 2 (1 [1]) → 2 (1 [1]) | 0 → 2 | 0 → 0 | 0.00 [0.00] → −3.00 [−3.00] | 5.00 → 10.00 | 0.26 → 0.51 | 0 → 0 |

*macOS 15.7 x86_64, AppleClang 17, Release; `HowlGuardHost.LouderCouplingRearms`,
seeds 1 and 21, 30 s.*

The sweep, six rooms over five seed sets, at the gated rows' 30 s and at
120 s (`HowlGuardSweep.LouderCoupling`):

| Room, length | LOST-ducks after the change (median a run [max]) | TRIP-ducks | Strikes at the end | Runs latched (reached LATCHED; after the change, median s) | Restore level at the end, median [min] (dB) | Gain at the end, median (dB) | Time ducked or releasing, median | Howl blocks |
|---|---|---|---|---|---|---|---|---|
| cabin, 30 s | 18 (4 [4]) → 17 (3 [4]) | 0 → 0 | 0 → 14 | 0 → 4 of 5 (2; 16.25) | 0.00 [0.00] → −9.00 [−9.00] | −5.47 → −26.00 | 0.55 → 0.59 | 0 → 0 |
| mt5, 30 s | 3 (1 [1]) → 3 (1 [1]) | 4 → 2 | 4 → 5 | 0 → 0 | −3.00 [−6.00] → −3.00 [−3.00] | −3.00 → −3.00 | 0.51 → 0.51 | 0 → 0 |
| studio, 30 s | 5 (1 [1]) → 5 (1 [1]) | 0 → 0 | 0 → 5 | 0 → 0 | 0.00 [0.00] → −3.00 [−3.00] | 0.00 → −3.00 | 0.26 → 0.51 | 0 → 0 |
| rehearsal, 30 s | 5 (1 [1]) → 5 (1 [1]) | 0 → 0 | 0 → 5 | 0 → 0 | 0.00 [0.00] → −3.00 [−3.00] | 0.00 → −3.00 | 0.26 → 0.51 | 0 → 0 |
| hall, 30 s | 5 (1 [1]) → 5 (1 [1]) | 0 → 0 | 0 → 5 | 0 → 0 | 0.00 [0.00] → −3.00 [−3.00] | 0.00 → −3.00 | 0.26 → 0.51 | 0 → 0 |
| mt9, 30 s | 5 (1 [1]) → 5 (1 [1]) | 3 → 0 | 3 → 5 | 0 → 0 | −3.00 [−3.00] → −3.00 [−3.00] | −3.00 → −3.00 | 0.77 → 0.51 | 0 → 0 |
| cabin, 120 s | 72 (19 [22]) → 19 (4 [4]) | 0 → 0 | 0 → 15 | 0 → 5 of 5 (5; 23.27), 0 ducks after it | 0.00 [0.00] → −9.00 [−9.00] | 0.00 → −11.89 (the cap) | 0.49 → 0.12 | 0 → 0 |
| mt5, 120 s | 2 (0 [1]) → 2 (0 [1]) | 4 → 4 | 1 → 1 | 0 → 0 | 0.00 [−3.00] → 0.00 [−3.00] | 0.00 → 0.00 | 0.09 → 0.09 | 0 → 0 |
| studio, rehearsal, hall, mt9, 120 s | 5 (1 [1]) → 5 (1 [1]) each | 0 → 0 (mt9 1 → 0) | 0 → 0 | 0 → 0 | 0.00 → 0.00 | 0.00 → 0.00 | 0.05 → 0.09 | 0 → 0 |

*macOS 15.7 x86_64, AppleClang 17, Release; 974.9 s on 4 threads for all
four passes.*

- **Within 30 s the option does not shorten cabin's cycle:** 7 LOST-ducks
  in 2 gated runs either way, and 18 → 17 in the sweep (median 4 → 3 a
  run). Cabin's first duck (a LOST) comes a median 2.53 s after the change,
  while the cold start's probation ends about 1.8 s after it, so that duck
  usually strikes nothing; each later one follows a walk-path release,
  falls inside the new probation and strikes. The third strike, which
  latches, is then usually the fourth duck: by 30 s 4 of 5 sweep runs had
  latched (2 had ramped into LATCHED).
- **Over 120 s it ends the cycle at the latch**: every cabin run latched,
  a median 23.27 s after the change, after 4 LOST-ducks (19 a run without
  it, and still cycling at 120 s), and ducked no more; the mic then sits
  at the cap (−11.89 dB re the operating gain, the dry limit − 6) with
  `unprotected` raised. Time ducked or releasing fell from 0.49 to 0.12.
- **Elsewhere it costs one strike and a longer re-arm, no new duck.** In
  every other room the first LOST after the change at 10 s falls inside the
  cold start's probation (OPEN at 1.76–1.79 s), so it strikes: the level
  ends at −3.00 dB (30 s rows; the strike decays by 120 s), the re-arm
  waits 10.00 s instead of 5.00, and the time ducked or releasing doubles
  (0.26 → 0.51). No room backed off more than 3 dB, LOST-ducks are unchanged
  and TRIP-ducks fell (mt5 4 → 2, mt9 3 → 0).
- **0 howl blocks**, on and off, in every row.

**The other rows, off → on.** The option was measured first as an opt-in:
a scratch build with it defaulted on re-ran every gated row
(`HowlGuardHost.*` but `CostPerBlock`) and the `Walks`, `ColdStart` and
`TwoMicsAndAudibleCost` sweeps, against the opt-in build (gated rows,
`Walks`, `ColdStart`) and, for `TwoMicsAndAudibleCost`, PR C's run of the
same code path. The gated rows were then re-run on the default-on build
and read as the scratch build's. Against each other:

| Rows | Runs | What moved, off → on |
|---|---|---|
| Cold start, gated (every `ColdStart*` row) and sweep | 126 / 690 | nothing: every printed number identical, 0 howl blocks, ducks off a burst 0 |
| Walks at exact − 6, gated / sweep | 12 / 30 | ducks unchanged (3 / 6, all LOST, 0.36 / 0.34 s after the walk), release − reconvergence median unchanged (1.59 / 1.60 s); every one of those ducks became a strike (0 → 3 / 0 → 6: the walk at 10 s is inside the cold start's probation); sweep minimum 1.38 both, studio → rehearsal's own minimum 1.44 → 1.46 s; 0 early, 0 howl |
| Walks at the limit − 6 (gated + sweep), and above the limits (gated + sweep) | 12 + 30, 6 + 10 | nothing (no LOST-duck in probation; the TRIP strikes, 5 and 5, unchanged) |
| Soundcheck walks and stable runs, gated | 12 + 8 | nothing (the walk is at 40 s, long after probation) |
| Stable material (audible cost), gated / sweep | 24 / 180 | nothing: 0 / 9 ducks, 0 off a burst |
| Song gap, gated | 6 | the gap's LOST (8 s in, inside the cold start's probation) is a strike: 0 → 3 a room, the level at the end 0.00 → −3.00 dB; mt5's peak A′ maximum −15.11 → −15.10 dB; unchanged: 3 ducks in the gap per room, 0 restarts, 3 of 3 OPEN when the singer returns, 0 howl |
| Two mics, gated / sweep | 8 / 20 | nothing |

*macOS 15.7 x86_64, AppleClang 17, Release; on 995 s (gated), 287 s
(walks), 619 s (two mics and audible cost), 2051 s (cold start); off 878 s
(gated, with `CostPerBlock`), 265 s (walks), 1813 s (cold start); each on
4 threads.*

So the option adds no duck anywhere; what it adds is a strike for any LOST
in the first 10 s of OPEN, including one that a walk (or a louder coupling)
causes while the cold start's probation still runs. A host that wants the
PR C behaviour sets `lost_in_probation_strikes = false`.

## A gap between songs

A gap between songs: the singer and the backing track at digital zero
from 8 s to 28 s, voiced + aux at the limit − 6, cabin and mt5, seeds 1, 21,
41, 34 s.

| Room | Peak A′ in the gap, median [max] (dB) | Ducks in the gap | Strikes (option off) | Restore level at the end, min (dB) | Restarts (ARMING) | OPEN when the singer returns | Howl blocks after | Loudest residual in the 1 s after the gap, median (dB re the near end) |
|---|---|---|---|---|---|---|---|---|
| cabin | −15.21 [−14.96] | 3 | 3 (0) | −3.00 | 0 | 3 of 3 | 0 | +3.91 |
| mt5 | −15.30 [−15.10] (off: [−15.11]) | 3 | 3 (0) | −3.00 | 0 | 3 of 3 | 0 | +3.91 |

*macOS 15.7 x86_64, AppleClang 17, Release; the default policy.*

- **A′ does not return to ~0 dB in silence.** It regrows toward the path
  level and peaks near −15 dB, far below `restart_a_db` (−1 dB). The
  restart rule therefore fires only on a canceller reset, not on a gap.
- In the gap A′ crosses −23.842 dB, so the verdict is lost and the guard
  ducks. The gap starts 8 s in, inside the cold start's probation, so that
  LOST is a strike (one per run, the only duck in it): the timer re-arm
  comes after 5 s × 2 = 10 s and the mic is OPEN at −3 dB when the singer
  returns (the strike decays after 60 s in OPEN). With
  `lost_in_probation_strikes` off: no strike, the re-arm after 5 s, OPEN at
  full gain. Nothing howled at re-entry either way. (The re-arm instants
  are from a scratch trace of this row's set-up, not the test's printout:
  18.61 s cabin and 18.45 s mt5 at seed 1 on; 13.41 to 13.61 s over the six
  runs off.)

## Two microphones

Two mics (`two_mic_loop.h`'s room pairs and separated singers, −10 dB
leakage), each on its own canceller, at the two-mic loop's own limit − 6 dB.
At 10 s mic 1's path is multiplied by 4 (+12 dB, the mic moved toward the
speaker): a howl that one mic causes. Gated: seeds 1 and 21, 20 s.

| Pair | Runs | First TRIP ducked mic 1 alone | Both | Mic 0 alone | Fallbacks | Cascades (mic 0 ducked by LOST) | Howl blocks after the force |
|---|---|---|---|---|---|---|---|
| cabin | 2 | 1 | 1 | 0 | 0 | 0 | 0 |
| cabin + aux | 2 | 2 | 0 | 0 | 0 | 0 | 0 |
| mt5+mt9 | 2 | 2 | 0 | 0 | 0 | 0 | 1 |
| mt5+mt9 + aux | 2 | 2 | 0 | 0 | 0 | 0 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- Attribution picked the forced mic alone in 7 of 8 runs, and both mics in
  the other; it never ducked the wrong mic alone here. On a second host
  (macOS arm64, AppleClang, CI run 36828198214) the same rows read 7 of 8
  forced mic alone and 1 of 8 wrong mic alone (cabin, no aux), with 3 howl
  blocks, the longest one block: the gate is a count with margin (at most
  2 of 8 wrong, at most 24 howl blocks, no howl run of 0.1 s), not a zero. The duck-all fallback never
  fired, and mic 0's verdict never dropped after mic 1 was ducked.

The sweep's five seed sets:

| Pair | Runs | Mic 1 alone | Both | Mic 0 alone | Fallbacks | Cascades | Howl blocks after the force |
|---|---|---|---|---|---|---|---|
| cabin | 5 | 1 | 3 | 1 | 0 | 0 | 0 |
| cabin + aux | 5 | 5 | 0 | 0 | 0 | 0 | 0 |
| mt5+mt9 | 5 | 3 | 2 | 0 | 0 | 0 | 3 |
| mt5+mt9 + aux | 5 | 5 | 0 | 0 | 0 | 0 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

With the backing track attribution picked the forced mic alone in 10 of 10
runs. Without it, both mics ducked in 5 of 10 and the wrong mic alone in one
(cabin: by the rule, mic 0 read more than 6 dB louder than mic 1 at that
trip's peak band). No fallback fired and mic 0's verdict was never lost
afterwards. 3 blocks over five mt5+mt9 runs reached the 40 dB rule.

## Audible cost

Stable material at the limit − 6, from reset, 30 s, counting after the first
OPEN. Gated: cabin and mt5, seeds 1 and 21.

| Material, shift | Runs (declared) | Ducks | On a burst | Ducked time (s) | Runs with a +20 dB block | Howl blocks | Loudest residual block, median [max] (dB re the near end) |
|---|---|---|---|---|---|---|---|
| voiced+aux, 0 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +4.77 [+8.82] |
| voiced+aux, 2 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +6.96 [+7.54] |
| voiced+aux, 5 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +6.30 [+9.42] |
| speech+aux, 0 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +11.67 [+19.56] |
| speech+aux, 2 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +13.05 [+15.56] |
| speech+aux, 5 Hz | 4 (4) | 0 | 0 | 0.00 | 0 | 0 | +15.82 [+18.12] |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- **0 ducks in 24 runs.** The margin is the ceiling's: the loudest residual
  block came within 10.44 dB of the +30 dB ceiling (speech + aux, +19.56).
  No run produced a loop-born burst (a block at +20 dB), so the burst oracle
  had nothing to separate here.

The sweep's six rooms and five seed sets (30 runs a row):

| Material, shift | Ducks | On a burst | Off any burst | Ducked time, max (s) | Runs with a +20 dB block | Howl blocks | Loudest residual block, median [max] (dB) |
|---|---|---|---|---|---|---|---|
| voiced+aux, 0 Hz | 0 | 0 | 0 | 0.00 | 0 | 0 | +4.20 [+15.35] |
| voiced+aux, 2 Hz | 0 | 0 | 0 | 0.00 | 0 | 0 | +7.65 [+9.97] |
| voiced+aux, 5 Hz | 0 | 0 | 0 | 0.00 | 0 | 0 | +7.75 [+12.26] |
| speech+aux, 0 Hz | 1 | 1 | 0 | 1.71 | 6 | 0 | +13.37 [+28.97] |
| speech+aux, 2 Hz | 4 | 4 | 0 | 1.72 | 8 | 0 | +13.81 [+30.34] |
| speech+aux, 5 Hz | 4 | 4 | 0 | 1.72 | 10 | 0 | +15.82 [+34.23] |

*macOS 15.7 x86_64, AppleClang 17, Release.*

**0 ducks off a burst in 180 runs.** All 9 ducks fell within 0.5 s of a
loop-born burst: speech at the limit − 6 still produces them (24 of 90 speech
runs had a block at +20 dB), and a duck there is the guard doing its job. A
duck costs at most 1.72 s of ducked time here.

## The reverb tail and the bus stage

The vendored Dattorro plate (`mutap_faust::faust_block<dattorro_f64,
double>`, paper defaults; `FaustVendored.DattorroAsShippedT30` measures its
T30 at 1.184593 s) in the reverb slot, wet 0.5 mixed over
the dry bus; cabin, voiced + aux at the limit − 6, seed 1. At 10 s the
forward gain steps up 12 dB, a forced howl. Voice is the speaker feed minus
the backing track, in dB relative to the block of the first TRIP:

| After the TRIP | With the bus stage | Without it | Apart |
|---|---|---|---|
| 0.05 s | −43.82 | −22.86 | 20.96 |
| 0.10 s | −62.26 | −43.39 | 18.87 |
| 0.25 s | −55.84 | −37.31 | 18.53 |
| 0.50 s | −66.53 | −47.13 | 19.40 |
| 1.00 s | −81.39 | −66.25 | 15.14 |
| 1.50 s | −80.15 | −64.65 | 15.50 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- The first TRIP came 0.115 s after the step in both runs. The per-mic duck
  alone leaves the charged plate ringing into the speaker. The bus stage
  repeats the duck after it, which cuts the tail by about the duck depth
  (15.14 to 20.96 dB at these offsets).
- On the dry path a trip then counts twice, per mic and on the bus. That is
  the cost of the stage.

## Cost per block

`HowlGuardHost.CostPerBlock`: analyze() per mic, one update(), apply() per
mic, block 64 at 48 kHz, white noise (no trip in the timed loop); against one
`pem_afc` (1024 taps, the 2-partition shadow) process_block in the same
process. Median of five repetitions.

| Mics | Float32 (ns / block) | Of one canceller | Double (ns / block) | Of one canceller |
|---|---|---|---|---|
| 1 | 2180 | 0.57 % | 3292 | 0.84 % |
| 2 | 4983 | 1.09 % | 7826 | 1.73 % |

*macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release, a shared machine.*

The guard is the detectors plus a few dozen comparisons and a gain ramp per
mic (PR B's run read 2246 / 3962 ns float and 3180 / 6311 ns double: a
shared machine moves these by tens of percent). On the Linux GCC CI runner
(job 110465810942) the guard read 8575 / 12794 ns float and 8706 /
13571 ns double against a 186–190 µs canceller: 4.61 / 6.86 % and 4.59 /
7.15 %. The ratio moves by that factor between hosts, so the test prints
and records it (`RecordProperty`) and gates only a gross regression
(under 25 %). The soundcheck sampler adds
two histogram increments per mic and block while it runs; it was off in the
timed loop. The guard allocates only in its constructor.

## What did not separate, and what was not measured

- **F → 2F in cabin costs 4 ducks before it settles, and settles at the
  cap.** The guard there releases on the walk path (the verdict holds ok
  while ducked) and loses the verdict again at full gain; each such LOST in
  probation is a strike, so the cycle ends at the latch after a median 4
  ducks (120 s sweep rows; within the gated 30 s, 7 LOST-ducks in 2 runs,
  as without the rule) with the mic at the cap and `unprotected` raised.
  Safe (0 howl blocks) and audible. Every other room pays one 3 dB strike
  for 60 s for the LOST the change itself causes inside the cold start's
  probation. A rule that latches sooner on LOSTs (fewer strikes to the
  latch), or one that leaves the cold start's probation out, was not
  measured.
- **The soundcheck calibration is one window of one song.** The margins
  (D + 4, A′ + 3 dB) were measured on the song the soundcheck sampled, in
  this loop (aligned reference, white backing track at −12 dB). A
  soundcheck on one song and a show on another, a soundcheck at a
  different gain than the show, a soundcheck without the backing track
  (the verdict there never declares, and the sampler reads whatever D and
  A′ do in ARMING), and the factory thresholds on the release
  experiment's loop with the soundcheck margins were not measured. The
  30 s window includes the cold start (about 2 s before the declaration).
- **Nothing declares without a backing track.** Held notes, music and
  speech alone never declared in 20 s from ARMING; every such run relies on
  the arming timeout and the host's cap, which is therefore mandatory for
  opening without a declaration: without it the mic stays 30 dB down in
  ARMING with `unprotected` raised (90 of 90 sweep runs for all 20 s).
  Under the cap the held note ducks after OPEN_CAPPED in about a third of
  the runs (11 of 30 at S1) on detector TRIPs; in the gated rows both
  Intel ducks sat on loop-born bursts (arm64: 5 of 6 runs ducked, not
  classified). A soundcheck excitation, or ARMING at a shallower duck for
  those materials, was not measured.
- **Silence does not restart the guard.** A′ peaks near −15 dB in a 20 s
  gap, so `restart_a_db` fires only on a canceller reset; the gap's LOST
  duck and timer re-arm leave the guard OPEN at full gain when the singer
  returns. No re-entry howled here.
- **The frozen-estimate margin is not a howl oracle at these gains**, so
  the howl claims rest on the 40 dB rule alone.
- **The latch engages live only in cabin under F → 2F** (1 of 2 gated runs
  by 30 s, 5 of 5 sweep runs by 120 s, each then held at the cap); with
  `lost_in_probation_strikes` off it never engaged (the most strikes were 5
  over three runs at the limit + 6). Leaving the latch on a fresh ok edge,
  and `clear()`, are covered by the state-machine suite
  (`tests/test_howl_guard.cpp`) only.
- **The release experiment's late howl at + 6 dB does not reproduce here**,
  and neither does its cold-start howling at + 3 / + 6. Both stress claims
  run at the canceller's limit + 6 instead.
- **The growth hint does not pre-arm attribution.** Growth while the
  verdict is ok is counted (`hints()`) and does nothing else.
- **Not measured:** the guard in float in the live loop (the host claims are
  double; the float suite checks the state machine and the chain hook on
  both emulated selections); two mics at S3, with a shift or through a walk;
  the guard during a gain ramp (the protocol keeps it off there); any real
  room or real singing.

## Operating points

`HowlGuardSweep.OperatingPoints` bisects the converged canceller's runaway
limit for every room and material over seed sets 1, 21, 41, 61, 81, and
prints the table the gated rows use (`k_ops` in the test file). Values are dB
over exact_msg_db; the gated rows run at the median − 6 dB.

| Room | Material | S1 median | S1 per seed | S3 median | S3 per seed |
|---|---|---|---|---|---|
| cabin | voiced+aux | +11.89 | +10.66 +11.89 +12.07 +12.25 +9.61 | +13.12 | +13.30 +11.54 +13.12 +12.95 +13.83 |
| cabin | voiced | +9.26 | +11.37 +5.57 +13.12 +9.26 +8.20 | +14.88 | +15.23 +14.88 +14.71 +14.36 +17.34 |
| cabin | music | +10.14 | +10.14 +9.79 +9.43 +11.72 +10.14 | +15.76 | +17.34 +15.94 +14.88 +15.59 +15.76 |
| cabin | music+aux | +9.96 | +10.31 +10.84 +9.96 +9.96 +9.61 | +13.48 | +15.59 +12.95 +14.00 +13.48 +13.12 |
| cabin | speech | +19.63 | +20.16 +19.45 +19.63 +19.63 +19.10 | +20.16 | +20.68 +19.28 +20.33 +19.80 +20.16 |
| cabin | speech+aux | +19.45 | +18.75 +19.80 +20.33 +19.45 +18.57 | +19.98 | +18.75 +19.98 +21.04 +19.98 +19.10 |
| mt5 | voiced+aux | +11.02 | +11.02 +8.73 +11.19 +11.37 +9.43 | +12.42 | +11.37 +12.07 +12.77 +12.60 +12.42 |
| mt5 | voiced | +11.02 | +11.02 +10.31 +13.48 +13.65 +7.50 | +15.76 | +16.82 +14.53 +17.34 +15.76 +15.41 |
| mt5 | music | +11.19 | +11.72 +11.19 +10.66 +11.19 +11.89 | +16.99 | +15.94 +16.82 +16.99 +17.70 +17.17 |
| mt5 | music+aux | +11.89 | +11.19 +12.95 +11.72 +12.42 +11.89 | +16.11 | +16.11 +16.46 +15.06 +13.83 +16.11 |
| mt5 | speech | +22.62 | +22.79 +22.62 +21.39 +22.97 +22.09 | +22.62 | +22.62 +22.27 +22.44 +22.97 +22.62 |
| mt5 | speech+aux | +21.56 | +21.56 +20.68 +22.44 +20.51 +22.44 | +22.62 | +22.44 +22.62 +22.62 +21.74 +22.79 |
| studio | voiced+aux | +9.08 | +8.38 +8.03 +9.08 +9.61 +9.26 | — | — |
| studio | voiced | +10.84 | +10.49 +11.89 +7.85 +11.72 +10.84 | — | — |
| studio | music | +10.84 | +12.95 +10.14 +12.07 +10.84 +10.49 | — | — |
| studio | music+aux | +11.02 | +11.72 +11.19 +10.14 +9.96 +11.02 | — | — |
| studio | speech | +19.28 | +19.45 +19.28 +18.93 +17.87 +19.80 | — | — |
| studio | speech+aux | +18.93 | +18.57 +18.75 +19.45 +19.80 +18.93 | — | — |
| rehearsal | voiced+aux | +9.61 | +9.96 +9.61 +9.61 +11.19 +9.26 | — | — |
| rehearsal | voiced | +10.66 | +10.66 +11.19 +8.38 +14.18 +7.68 | — | — |
| rehearsal | music | +12.07 | +10.14 +11.37 +14.00 +12.07 +12.07 | — | — |
| rehearsal | music+aux | +11.54 | +11.89 +11.54 +11.37 +8.73 +12.25 | — | — |
| rehearsal | speech | +15.06 | +15.06 +14.88 +15.06 +14.88 +15.23 | — | — |
| rehearsal | speech+aux | +13.83 | +13.83 +14.18 +14.00 +13.65 +13.83 | — | — |
| hall | voiced+aux | +9.26 | +9.08 +9.61 +9.26 +10.84 +9.26 | — | — |
| hall | voiced | +7.32 | +6.62 +6.97 +7.32 +7.85 +7.32 | — | — |
| hall | music | +11.89 | +9.61 +12.07 +12.42 +11.89 +9.79 | — | — |
| hall | music+aux | +11.37 | +11.19 +11.37 +12.07 +10.66 +11.37 | — | — |
| hall | speech | +19.63 | +19.45 +19.63 +19.98 +19.10 +19.98 | — | — |
| hall | speech+aux | +19.98 | +19.45 +20.16 +20.68 +19.10 +19.98 | — | — |
| mt9 | voiced+aux | +8.73 | +10.49 +9.43 +8.73 +8.55 +8.38 | — | — |
| mt9 | voiced | +10.31 | +10.84 +10.31 +6.27 +8.55 +10.49 | — | — |
| mt9 | music | +11.19 | +12.42 +8.73 +11.54 +10.84 +11.19 | — | — |
| mt9 | music+aux | +11.02 | +10.49 +11.02 +11.37 +11.37 +10.66 | — | — |
| mt9 | speech | +21.39 | +21.39 +21.21 +21.56 +21.21 +21.91 | — | — |
| mt9 | speech+aux | +20.51 | +20.51 +18.75 +21.74 +18.93 +21.56 | — | — |

The two-mic loop's limit over exact_msg_db(F_0 + F_1), the same protocol:

| Pair | Without the backing track, median | Per seed | With it, median | Per seed |
|---|---|---|---|---|
| cabin | +9.43 | +9.43 +9.43 +8.73 +9.96 +9.61 | +11.72 | +9.79 +12.95 +11.54 +12.07 +11.72 |
| mt5+mt9 | +8.55 | +8.55 +7.68 +7.32 +9.43 +8.91 | +8.73 | +8.55 +8.20 +9.61 +10.49 +8.73 |

*macOS 15.7 x86_64, AppleClang 17, Release; 1583.2 s on 3 threads.*

These are this loop's numbers, not the karaoke suite's: the chain's
reference lags one block and is aligned to the direct path, and the probes
are 10 s.

## Hosts

Every number in this document was measured on **macOS 15.7.9 x86_64
(i9-8950HK), AppleClang 17.0.0, Release**, double precision, on a machine
shared with other jobs. The loop is chaotic: platform arithmetic moves
single runs, which is why the gated rows assert medians, directions and
counts with margin, never one trajectory. CI also runs the gated rows on
macOS arm64 and Linux; only pass / fail is recorded from there (the
two-mic row's arm64 counts are quoted where they differ). Quote the host
with the number.

## Provenance

The state machine, the ramps and the chain hook (float and double, every
expectation a transition time or an exact gain; both emulated selections run
the float half):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMUTAP_WERROR=ON
cmake --build build
build/tests/mutap_tests --gtest_filter='howl_guard_test*:HowlGuardConfigValidation.*:HowlGuardRtContract.*'
```

The gated live rows run by default with the rest of the suite:

```sh
build/tests/mutap_tests --gtest_filter='HowlGuardHost.*'
```

Wall times in one run of the gated rows (`--gtest_filter='HowlGuardHost.*'`,
each row on 4 threads; the probation-strike PR's code, the default policy; the full `ctest --test-dir build`
run is in the PR description):

| Test | Seconds |
|---|---|
| `HowlGuardHost.ColdStartWithBackingTrack` | 50.90 |
| `HowlGuardHost.ColdStartWithoutBackingTrack` | 53.31 |
| `HowlGuardHost.ColdStartWithoutACapStaysArmed` | 44.81 |
| `HowlGuardHost.ColdStartAboveTheDryLimit` | 79.38 |
| `HowlGuardHost.ColdStartAboveTheCancellerLimit` | 48.69 |
| `HowlGuardHost.ColdStartShiftedAndAtS3` | 66.32 |
| `HowlGuardHost.WalkReleasesAfterTheMisalignmentOracle` | 68.81 |
| `HowlGuardHost.LateHowlAfterAWalkIsCaught` | 45.35 |
| `HowlGuardHost.LouderCouplingRearms` | 39.86 |
| `HowlGuardHost.SongGapDoesNotRestart` | 26.38 |
| `HowlGuardHost.TwoMicsAttributeASingleMicHowl` | 27.89 |
| `HowlGuardHost.AudibleCostOnStableMaterial` | 59.96 |
| `HowlGuardHost.SoundcheckCalibrationOnStableMaterial` | 42.27 |
| `HowlGuardHost.SoundcheckCalibrationSeesAWalk` | 75.28 |
| `HowlGuardHost.BusStageCutsTheReverbRing` | 8.88 |
| `HowlGuardHost.CostPerBlock` | 17.83 |
| total | 755.91 |

Every gated table that PR C printed reads the same in this run, to the
last printed digit, except `CostPerBlock` (timing), the strike counts the
default `lost_in_probation_strikes` adds (walks at exact − 6, the song
gap, whose mt5 A′ maximum moves 0.01 dB) and `LouderCouplingRearms`, which
runs every row both ways (its off rows read as PR C's).

The sweep is behind `MUTAP_SLOW=1`; it prints and asserts nothing:

```sh
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=4 build/tests/mutap_tests --gtest_filter='HowlGuardSweep.*'
```

| Test | Wall time | Run |
|---|---|---|
| `HowlGuardSweep.OperatingPoints` | 1583.2 s (3 threads) | PR B, not re-run (the guard is not in it) |
| `HowlGuardSweep.ColdStart` | 1890 s (4 threads) | this PR |
| `HowlGuardSweep.Walks` | 245 s (4 threads) | this PR |
| `HowlGuardSweep.LouderCoupling` | 974.9 s (4 threads; 30 s and 120 s rows, the option off and on) | the probation-strike PR |
| `HowlGuardSweep.TwoMicsAndAudibleCost` | 567 s (4 threads) | this PR |
| `HowlGuardSweep.CalibrationMargins` | 2079.35 s (4 threads) | this PR |
| `HowlGuardSweep.CalibrationApplied` | 1227 s (4 threads) | this PR |

`Walks`, `ColdStart` and `TwoMicsAndAudibleCost` re-ran and match PR B's
tables to the last digit where both exist.

The operating points are deterministic: `HowlGuardSweep.OperatingPoints`
reprints the gated rows' `k_ops` table to the last digit on this host.
