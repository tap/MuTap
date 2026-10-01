# The safety layer: a per-mic howl guard

*Measured September 2026 in simulation: six rooms from both generator
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
  A′ < −23.842 dB) has held for 1.5 s with the detector quiet. After 10 s
  without a declaration it raises `unprotected`, and, when the host has set
  a cap (the dry limit − 6 dB), it opens to the cap instead.
- **OPEN** at full gain, less 3 dB per strike.
- **DUCKED** 20 dB down on a detector trip (the level catch always; the
  growth path only while the verdict is not ok) or on the verdict lost
  (ok → not-ok held 0.3 s). It releases when the verdict has held ok for
  1.5 s with the detector quiet, or on a timer (5 s × 2^strikes) when the
  verdict cannot recover, and the verdict trigger then stays disarmed until
  the verdict has read ok once.
- **RELEASING** ramps back over 200 ms; **LATCHED** is the end state after
  three strikes.

Two mics are attributed by the detectors' band level at the shared howl
frequency, with a duck-all fallback. A bus stage in `afc_chain`'s safety
slot repeats the duck after the reverb. The header lists every transition.

## Headline

| Claim (the 40 dB rule is the howl oracle) | Gated rows (cabin, mt5) | `MUTAP_SLOW` sweep (six rooms, five seed sets) |
|---|---|---|
| Guarded cold starts that reached the 40 dB rule | 0 of 108 | 0 of 620 |
| … at the canceller's limit + 6, unguarded twins | 575 blocks, 3 of 6 runs | — |
| Declaration with the backing track, median | 1.77 to 2.07 s | 1.76 to 2.11 s |
| Declaration without it | 0 runs: all leave ARMING through the cap | 0 runs; without a cap, ARMING for all 20 s |
| Walk at exact − 6: release − misalignment-oracle reconvergence, median [min] | 1.59 s [1.38], 0 early of 3 | 1.60 s [1.38], 0 early of 6 |
| Walk at the limit − 6: ducks / howl blocks | 0 of 12 / 0 | 0 of 30 / 0 |
| Walk at the limit + 6 (rehearsal → hall): howl blocks guarded / unguarded | 0 / 1203 | — |
| F → 2F: howl blocks; LOST-ducks after the timer re-arm | 0; 16 in 6 of 8 runs | 0; 47 in 20 of 30 runs |
| Two mics, one forced: first TRIP on the wrong mic alone; howl blocks | 0 of 8; 1 | 1 of 20; 3 |
| Stable material at the limit − 6: ducks off a loop-born burst | 0 in 24 runs | 0 in 180 runs |
| Dattorro in the loop: voice 0.5 s after a trip, with / without the bus stage | −66.53 / −47.13 dB | — |
| Cost per block, one mic, float / double | 0.60 / 0.88 % of a canceller | — |

*macOS 15.7 x86_64, AppleClang 17, Release, double; S1 (10 ms) unless a
section says otherwise.*

- **No guarded single-mic run reached the 40 dB rule**, including the stress
  rows where the unguarded loop howled. With two mics and one forced into a
  howl, a few blocks reached it (1 gated, 3 in the sweep, all mt5+mt9).
- **It is not free of audible cost where the verdict is weak.** Without a
  backing track no run declares from ARMING, so those runs sit at the cap.
  A louder coupling (F → 2F) makes the guard cycle between ducked and open.
- **At the operating point the verdict does not see a walk.** No walk
  ducked and none howled: the canceller re-identifies on its own there.

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
| voiced, S1, no cap | 10 | 0 | — | 0 (in ARMING for all 20 s) | 0 | 19.16 [19.45] | — |
| music, S1, no cap | 10 | 0 | — | 0 (in ARMING for all 20 s) | 0 | 16.34 [16.80] | — |

*macOS 15.7 x86_64, AppleClang 17, Release; 1979.5 s on 3 threads.*

**0 howl blocks in 620 guarded cold starts.** Every run with the backing
track declared, every run without it left ARMING only through the cap, and
without a cap the held note and the music stayed in ARMING for the whole
20 s.

## Walks to another room

A walk (S2a) swaps every mic's room at 10 s; the run lasts 25 s. Gated:
the six walks of the release experiment, voiced + aux, seeds 1 and 21, at
two operating points.

| Operating point | Walks | Ducked | By LOST | Duck after the change, median (s) | Releases | Release − reconvergence, median [min] (s) | Early | Howl blocks after the walk | D before the walk, median (dB) | Longest not-ok run after it, median [max] (s) |
|---|---|---|---|---|---|---|---|---|---|---|
| exact_msg_db − 6 | 12 | 3 | 3 | 0.36 | 3 | 1.59 [1.38] | 0 | 0 | −7.91 | 0.25 [0.47] |
| the limit − 6 | 12 | 0 | — | — | 0 | — | — | 0 | −8.46 | 0.21 [0.22] |

*macOS 15.7 x86_64, AppleClang 17, Release. The release instant is the ramp
start (RELEASING entry); "reconvergence" is the misalignment oracle's.*

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

| Walk | At exact − 6: ducked (by LOST) | Duck after the change, median (s) | Release − reconvergence, median [min] (s) | Early | At the limit − 6: ducked | Howl blocks (both points) |
|---|---|---|---|---|---|---|
| studio → rehearsal | 5 of 5 (5) | 0.33 | 1.60 [1.44] | 0 | 0 of 5 | 0 |
| rehearsal → hall | 0 of 5 | — | — | — | 0 of 5 | 0 |
| hall → cabin | 0 of 5 | — | — | — | 0 of 5 | 0 |
| cabin → studio | 0 of 5 | — | — | — | 0 of 5 | 0 |
| mt5 → mt105 | 1 of 5 (1) | 0.36 | 1.38 [1.38] | 0 | 0 of 5 | 0 |
| mt9 → mt109 | 0 of 5 | — | — | — | 0 of 5 | 0 |
| pooled | 6 of 30 (6) | 0.34 | 1.60 [1.38] | 0 | 0 of 30 | 0 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

Every release came after the misalignment oracle had reconverged (6 of 6,
the earliest 1.38 s after it). The verdict sees one walk in five at the
experiment's point, and none at the canceller's limit − 6.

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
voiced + aux at the limit − 6, 30 s.

| Room | Runs ducked | Duck after the change, median (s) | Runs re-armed | Re-arm after the duck, median (s) | LOST-ducks after the re-arm (runs) | Howl blocks | Time ducked or releasing after the change, median |
|---|---|---|---|---|---|---|---|
| cabin | 2 of 2 | 4.65 | 0 | — | 0 (0) | 0 | 0.56 |
| mt5 | 2 of 2 | 0.39 | 2 | 10.01 | 4 (2) | 0 | 0.96 |
| studio | 2 of 2 | 0.37 | 2 | 5.00 | 6 (2) | 0 | 0.94 |
| hall | 2 of 2 | 0.38 | 2 | 5.00 | 6 (2) | 0 | 0.93 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- Every run ducked (LOST), the timer re-arm came at rearm_timeout_s (5.00 s,
  10.01 s after a strike), and nothing howled.
- **The edge rule does not stop the duck / open cycle.** The rule disarms
  LOST after a timer re-arm until the verdict reads ok once, and the verdict
  does read ok again once the re-armed gain is up: 16 LOST-ducks came after
  a re-arm, in 6 of 8 runs. In mt5, studio and hall the guard spends a median
  0.93 to 0.96 of the time after the change ducked or releasing. Cabin never
  re-armed and still ducked 7 times: there the verdict held ok for 1.5 s
  while ducked (the walk path) and was lost again at full gain.
- This is safe (0 howl blocks) but audible. Two candidate policy changes,
  neither measured: count a LOST within probation as a strike (the back-off
  then lowers the level and doubles the hold), or re-arm only into a release
  that needs ok held.

The sweep's six rooms over five seed sets:

| Room | Runs ducked | Duck after the change, median (s) | Runs re-armed | Re-arm after the duck, median (s) | LOST-ducks after the re-arm (runs) | TRIP-ducks | Howl blocks | Time ducked or releasing, median |
|---|---|---|---|---|---|---|---|---|
| cabin | 5 of 5 | 2.53 | 1 | 16.73 | 1 (1) | 0 | 0 | 0.55 |
| mt5 | 5 of 5 | 0.38 | 5 | 5.00 | 8 (4) | 4 | 0 | 0.96 |
| studio | 5 of 5 | 0.37 | 5 | 5.00 | 14 (5) | 0 | 0 | 0.94 |
| rehearsal | 5 of 5 | 0.37 | 5 | 5.00 | 0 (0) | 0 | 0 | 0.26 |
| hall | 5 of 5 | 0.38 | 5 | 5.00 | 15 (5) | 0 | 0 | 0.93 |
| mt9 | 5 of 5 | 0.37 | 5 | 5.00 | 9 (5) | 3 | 0 | 0.95 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

0 howl blocks in 30 runs; 47 LOST-ducks after a re-arm, in 20 of 30 runs.
Rehearsal is the one room where the re-armed loop stayed open (0.26 of the
time ducked).

## A gap between songs

A gap between songs: the singer and the backing track at digital zero
from 8 s to 28 s, voiced + aux at the limit − 6, cabin and mt5, seeds 1, 21,
41, 34 s.

| Room | Peak A′ in the gap, median [max] (dB) | Ducks in the gap | Restarts (ARMING) | OPEN when the singer returns | Howl blocks after | Loudest residual in the 1 s after the gap, median (dB re the near end) |
|---|---|---|---|---|---|---|
| cabin | −15.21 [−14.96] | 3 | 0 | 3 of 3 | 0 | +3.91 |
| mt5 | −15.30 [−15.11] | 3 | 0 | 3 of 3 | 0 | +3.91 |

*macOS 15.7 x86_64, AppleClang 17, Release.*

- **A′ does not return to ~0 dB in silence.** It regrows toward the path
  level and peaks near −15 dB, far below `restart_a_db` (−1 dB). The
  restart rule therefore fires only on a canceller reset, not on a gap.
- In the gap A′ crosses −23.842 dB, so the verdict is lost and the guard
  ducks. Five seconds later the timer re-arms it, and it is OPEN, at full
  gain, when the singer returns. Nothing howled at re-entry here.

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
  the other; it never ducked the wrong mic alone. The duck-all fallback never
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
| 1 | 2246 | 0.60 % | 3180 | 0.88 % |
| 2 | 3962 | 1.02 % | 6311 | 1.59 % |

*macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release, a shared machine.*

The guard is the detectors plus a few dozen comparisons and a gain ramp per
mic. It allocates only in its constructor.

## What did not separate, and what was not measured

- **F → 2F cycles duck / open.** The edge rule works as specified, but the
  verdict itself reads ok at the ducked gain, so LOST re-arms and fires
  again at full gain ([A louder coupling](#a-louder-coupling-f--2f)). Safe,
  audible, and a policy decision for the next revision.
- **The verdict's thresholds are one loop's calibration.** At the
  canceller's limit − 6 D sits far below −1.235 dB, and a walk's excursion
  is shorter than the trip hold, so no walk ducked
  ([Walks](#walks-to-another-room)). No walk howled either: the canceller
  re-identified at that gain on its own. Whether a deployment needs a
  per-room D threshold (the design note's soundcheck readout) is open.
- **Nothing declares without a backing track.** Held notes, music and
  speech alone never declared in 20 s from ARMING; every such run relies on
  the arming timeout and the host's cap. Without a cap they stay ducked
  30 dB with `unprotected` raised. A soundcheck excitation, or ARMING at a
  shallower duck for those materials, was not measured.
- **Silence does not restart the guard.** A′ peaks near −15 dB in a 20 s
  gap, so `restart_a_db` fires only on a canceller reset; the gap's LOST
  duck and timer re-arm leave the guard OPEN at full gain when the singer
  returns. No re-entry howled here.
- **The frozen-estimate margin is not a howl oracle at these gains**, so
  the howl claims rest on the 40 dB rule alone.
- **The latch never engaged in a live row.** The most strikes were 5 over
  three runs at the limit + 6; the latch, `clear()` and the end state are
  covered by the state-machine suite (`tests/test_howl_guard.cpp`) only.
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
counts with margin, never one trajectory. No second host has run these
tests. Quote the host with the number.

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

Wall times in one full `ctest --test-dir build` run (each row on 4
threads; 364 of 364 tests passed, 18 of them skipped: the `MUTAP_SLOW`
sweeps and `PortableRandom.MatchesLibstdcxxBitForBit`):

| Test | Seconds |
|---|---|
| `HowlGuardHost.ColdStartWithBackingTrack` | 55.01 |
| `HowlGuardHost.ColdStartWithoutBackingTrack` | 54.29 |
| `HowlGuardHost.ColdStartAboveTheDryLimit` | 84.02 |
| `HowlGuardHost.ColdStartAboveTheCancellerLimit` | 53.84 |
| `HowlGuardHost.ColdStartShiftedAndAtS3` | 72.96 |
| `HowlGuardHost.WalkReleasesAfterTheMisalignmentOracle` | 68.75 |
| `HowlGuardHost.LateHowlAfterAWalkIsCaught` | 41.62 |
| `HowlGuardHost.LouderCouplingRearms` | 20.99 |
| `HowlGuardHost.SongGapDoesNotRestart` | 26.74 |
| `HowlGuardHost.TwoMicsAttributeASingleMicHowl` | 28.88 |
| `HowlGuardHost.AudibleCostOnStableMaterial` | 62.33 |
| `HowlGuardHost.BusStageCutsTheReverbRing` | 9.07 |
| `HowlGuardHost.CostPerBlock` | 18.21 |
| total | 596.71 |

The cost table above is from an earlier run of the same gated rows on the
same machine (`CostPerBlock` re-measures on every run and gates the ratio
with margin).

The sweep is behind `MUTAP_SLOW=1`; it prints and asserts nothing. It was
run on 3 threads (`MUTAP_SLOW_THREADS=3`) beside other work:

```sh
MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='HowlGuardSweep.*'
```

| Test | Wall time (3 threads) |
|---|---|
| `HowlGuardSweep.OperatingPoints` | 1583.2 s |
| `HowlGuardSweep.ColdStart` | 1979.5 s |
| `HowlGuardSweep.WalksAndLouderCoupling` | 426.3 s |
| `HowlGuardSweep.TwoMicsAndAudibleCost` | 667.5 s |
| total | 4656.5 s |

The operating points are deterministic: `HowlGuardSweep.OperatingPoints`
reprints the gated rows' `k_ops` table to the last digit on this host.
