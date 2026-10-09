# From research code to product — the production-readiness plan

*Proposal, rev 1 — 9 October 2026. Written against the audit of the same
date (its findings are restated in §1 so this document stands alone). Nothing
here is built; every milestone names the pass criterion that lets it fail, in
the house workflow: scratch-measure first, threshold with margin, rooms from
both generator families, numbers quoted with their host.*

---

## 1. What the audit found

The code is clean, allocation-free in the hot path and honest about its
limits. What stands between it and a product is not code quality. Five
things do, in this order:

1. **Nothing has been validated outside simulation.** Every acoustic number
   comes from image-source rooms, random decaying FIRs and synthetic
   signals; the loudspeaker is a hard clip and a band-limit; there is no
   clock drift, no transducer nonlinearity, no measured impulse response,
   and the P.501 real-speech attachments were never obtained. HANDOFF.md
   "What's next" item 1 has said so since Rev 3.
2. **The feedback canceller costs ten times its core and was never counted
   on an embedded target.** Measured 9 October 2026 (Linux x86-64, GCC 13.3,
   Release, double, a build running alongside so pessimistic by perhaps
   20 %): at block 64 / 1024 taps / 48 kHz the raw `partitioned_fdkf` is
   46 µs per 1333 µs block (3 % of a core), `pem_afc` on the speech cascade
   with the 2-partition shadow is 432 µs (32 %), float32 447 µs (no gain:
   the code is scalar), the warped predictor 95 µs (7 %); at block 256 /
   2048 taps the certified `aec_chain` is 167 µs (3 %). The cost is
   `speech_predictor::analyze`: a brute-force pitch search over 369 lags on
   a 1024-sample window, every block. `bench/baselines.json` counts only
   the AEC scenarios and the learned suppressor on m55 / m33 / hexagon;
   no `pem_afc`, `speech_predictor` or `howl_guard` workload exists.
3. **The safety layer runs on one simulated loop's calibration.**
   `guard_policy::d_db = -1.235` and `a_db = -23.842` are, by their own
   comments, a calibration of that loop; the shadow ratio's usable window is
   about 1 dB wide; `howl_detector.h` says no threshold separates stable-loop
   residuals from howls; the cold start without a backing track never
   declares and leaves ARMING only through the cap, flagged `unprotected`.
4. **Field plumbing is absent.** No delay estimation (`afc_chain` takes a
   hand-measured `reference_delay_samples`; `aec_chain` assumes an aligned
   far end), no clock-drift handling, no sample-rate-aware core (time
   constants per block, the preset's power-law rescaling calibrated at two
   geometries), and every structural attribute of the externals rebuilds
   the engine and discards the learned filter.
5. **Known failure modes are recorded but open**, and the default engine is
   still NLMS "on seniority" while the Kalman core wins every simulated
   scenario.

Behind those: a 420-test battery that is a research instrument (about 6,800
CPU-seconds by default, twelve rows of 3 to 6 minutes each, thresholds from
one macOS host, a "repeat 20 times" CI step to characterise float rows that
wandered); numbers copied into prose that drift (HANDOFF.md counts 97 tests,
the README quick start calls `mutap::real_fft32` and no `mutap` namespace
exists); no tags, no releases, version 0.1.0; two 1,200-line headers; a
suppressor estimator kept in two bit-identical forms by a CI job; the
Hilbert coefficients' licence "unstated"; and no freedom-to-operate review
(the repo's IP policy is a copyright policy).

## 2. The shape of the plan

Seven phases. P0 is a week of hygiene that everything else quotes. P1 (cost)
and P2 (plumbing) are independent and run in parallel. P3 (the safety layer)
follows P1, because the cost work changes the predictor and the statistics
the guard reads must be re-calibrated after it, not before. P4 (reality) is
the phase that decides whether this is a product; its real-room sessions
need P2's delay estimator on the rig, so they start once P2a lands, while
its fixtures and harness extensions start at once. P5 (the battery as a
gate) runs alongside everything and finishes last; P6 (structure and
release) closes.

```
P0 hygiene ──┬── P1 cost ────────── P3 safety layer ──┐
             ├── P2 plumbing ──┬──────────────────────┼── P4 reality ── P6 release
             │                 └── (P2a unblocks P4c) │
             └── P5 battery as a gate ────────────────┘
```

"Done" for the whole plan: §8's exit criteria, every one a number.

## 3. P0 — Hygiene and truth *(one week, no DSP)*

- **P0a — Fix what is wrong today.** README quick start: `tap::mu::`, not
  `mutap::` (or add `namespace mutap = tap::mu;` in `mutap.h` and keep
  both — a decision, §7). HANDOFF.md's test count and the intro of every
  doc that quotes one. *Pass: the README snippet compiles as a test.*
- **P0b — A claims register.** `docs/claims.md`, generated, one row per
  number the README or a header quotes: the claim, the test that gates it,
  the threshold, the host and date it was measured. Source: gtest
  `RecordProperty` output from the claims tier (P5a). *Pass: every number
  in README.md "Status" has a row; CI fails on a README number without
  one.* This is the mechanism that stops prose drifting again.
- **P0c — Versioning.** Tag `v0.1.0` at the current `main`; semver from
  here; a `CHANGELOG.md`; `MUTAP_VERSION_*` bumped in the release PR; the
  consumer re-pin (working note 6) as a script, `scripts/repin.sh`, run by
  the release PR. *Pass: a tag exists and MuTap-Max pins it.*
- **P0d — IP.** Two actions. (1) Replace the Hilbert coefficients with
  MuTap's own design: the procedure is published (Niemitalo's 2019 Stack
  Exchange answer; the classic elliptic-based design of Valenzuela & Constantinides
  is older still), so re-run it with our own optimizer, pin the image
  rejection ≥ 44 dB over the same band, and the "status unstated" entry in
  `THIRD_PARTY_NOTICES.md` goes away. (2) A freedom-to-operate review
  (Tim's action, outside the repo): PEM-based feedback cancellation
  (Spriet / KU Leuven hearing-aid family), frequency-domain Kalman AEC
  (Enzner), dual-path / shadow comparators, residual-echo suppression with
  comfort noise. A quick literature search on 9 October 2026 found no
  PEM-AFROW patent; that is not a clearance. *Pass: a dated note in
  `THIRD_PARTY_NOTICES.md` recording the outcome.*

## 4. P1 — The feedback canceller's cost *(the predictor)*

Target, stated first so the phase can fail: `pem_afc` on the speech cascade
at block 64 / 1024 taps / 48 kHz at **≤ 10 % of one x86 core** (from 32 %)
with **≤ 0.5 dB** median loss of added stable gain on the five rooms, and an
**m55 instruction count that fits the 48 kHz block budget with 2× margin**
(or the geometry it does fit, documented). The M55 budget: at a nominal
400 MHz, one 64-sample block is 533 k cycles; the ratchet counts
instructions, so the cycle translation is a measured number from P4e, and
until then the gate is instructions per block against that figure at an
assumed 1.0 IPC, stated as such.

- **P1a — Count it first.** Icount scenarios `afc_speech_48k`,
  `afc_warped_48k` (block 64, 1024 taps, shadow 2), `guard_m1_48k` on
  m55, m33 and hexagon; seed the baselines. *Pass: `NO BASELINE` gone; the
  numbers in `bench/README.md` with the per-block budget beside them.*
  This is a day's work and it is the number every later step is measured
  against.
- **P1b — Refit cadence.** `pem_afc::config::refit_interval_blocks`
  (default 1: bit-identical to today). Measure ASG at 1, 2, 4, 8 on the
  five band-limited rooms, both predictors, both cores, five seed sets.
  *Pass: the largest interval with ≤ 0.5 dB median ASG loss becomes the
  preset default; the table lands in the claims register.*
- **P1c — The pitch search.** Two candidates, both measured against the
  brute-force lag and voicing decision on the test materials: (i) coarse
  search on a 4× decimated residual, then ±4 lags fine at full rate;
  (ii) autocorrelation by one 2048-point real FFT of the residual (the
  FFT is already there), then the normalised peak search over the lag
  range. *Pass: lag within ±1 sample and the same voicing decision on
  ≥ 99 % of frames where brute force voices; ASG within 0.5 dB; the search
  ≥ 5× cheaper.* The double profile keeps brute force behind a config
  flag so the old numbers remain reproducible.
- **P1d — One convolution, not two.** `pem_afc` runs the partitioned
  convolution twice per block: raw (for e) and prewhitened (inside the
  core, for e'). Since A(q) is applied to both u and y, e' = A(q)·e holds
  when F̂ is held fixed within the block, which it is. Add a core entry
  point `adapt_block(u_pw, e_pw)` that takes the prewhitened error
  directly and skips the core's own filtering. *Pass: ASG within 0.5 dB
  of the two-convolution form on every gated row, or the change is
  dropped and the reason recorded.* This is the AFROW paper's own
  row-operation form; the measurement decides.
- **P1e — float32 vectorisation.** After P1b–d, the per-bin loops of the
  Kalman predict/correct pass, `packed_mac`, the LP whitening filter and
  the resonator bank get the `__restrict`-parameter treatment that moved
  the howl detector from 10.9 to 2.0 µs on GCC (tap/MuTap#87), and the
  M55 ratchet says what Helium does with them. Explicit MVE / HVX
  intrinsics only where the autovectoriser measurably bails, behind the
  same contract, with the float-tracks-double battery as the oracle.
  *Pass: float32 `afc_speech_48k` count ≤ 0.6× its P1a seed on m55.*
- **P1f — The cost claim in the README.** One table, three hosts, the
  claims register's rows.

## 5. P2 — Field plumbing

- **P2a — Delay estimation.** `tap::mu::delay_estimator<Sample>`:
  GCC-PHAT between reference and mic in the frequency domain (the FFT is
  there), a block-rate estimate with a confidence, a hysteresis so the
  estimate moves only on sustained evidence. Consumers: `afc_chain`
  sets `reference_delay_samples` from it (today a hand-measured number
  per session; PROTOCOL §5.1), and `aec_chain` gains a bulk delay line
  before the canceller that it sets from it. On a change, shift the
  partition ring rather than reset (the filter is the same path, moved).
  *Pass: on the four fixture rooms with 0 … 2000 samples of inserted
  electrical delay, converged misalignment within 1 dB of the
  hand-aligned chain, the estimate within ±8 samples in < 1 s on speech,
  0 false moves over the stable-material battery.*
- **P2b — Clock drift.** A drift estimate from the slope of P2a's delay
  over time, driving a fractional-delay resampler on the reference
  (SampleRateTap's `async` engine is the family's resampler; DspTap's FIR
  substrate is already a dependency). *Pass: at ±100 ppm simulated drift
  between capture and playback, ERLE / ASG within 3 dB of the zero-drift
  row over 10 minutes; today's rows bit-identical at 0 ppm.*
- **P2c — Sample-rate-aware configuration.** A `physical` layer: time
  constants in seconds and frequencies in hertz, derived once into the
  per-block constants the cores keep. `aec_chain_preset`'s power-law
  rescaling becomes that derivation, and the two-point floor-bias
  interpolation is re-measured at 44.1 and 96 kHz so the calibrated range
  is stated from measurement, not clamped. *Pass: the ITU battery's
  headline rows at 44.1 and 96 kHz inside the same margins as 48 kHz, or
  the rate documented as unsupported.*
- **P2d — Reconfigure without a cold start.** `load_impulse_response()`
  on both cores (the inverse of `copy_impulse_response()`); the externals
  transplant the estimate across a rebuild for `@warp`, `@kalman`,
  `@gate`, `@shadow`, `@guard`, `@ceiling`, `@loop_ms`; only `@block` and
  the filter length still cold-start. Live setters for the Kalman
  `transition` and the predictor order where a rebuild is not needed.
  *Pass: switching engine or predictor mid-run re-converges in < 1 s
  (misalignment oracle) where a cold start takes the measured 1.4 s or
  more; the guard stays declared across the switch.*
- **P2e — Stereo far end.** Not built: a decision for Tim (§7). Recorded so
  its absence is explicit.

## 6. P3 — The safety layer: from calibration to procedure *(after P1)*

- **P3a — The soundcheck as code.** ARMING holds the output 30 dB down,
  so the canceller sees no excitation and the verdict cannot declare
  without a backing track. Add a probe: during ARMING the chain may inject
  a shaped-noise burst (level, duration and spectrum configurable; off by
  default) through the speaker path, so the canceller converges and the
  soundcheck sampler sees real statistics. `calibrate` becomes the
  procedure: probe → sample → thresholds → open. *Pass: cold start
  declares in < 5 s in every simulated room without a backing track,
  0 howl blocks, on both hosts.*
- **P3b — Calibrate-first defaults.** The factory `d_db` / `a_db` stop
  being the defaults a mic opens on: a guard built without a calibration
  and without a cap stays ARMING and says so. A product ships the
  procedure, not the constants. *Pass: the Max help patcher walks the
  procedure; the gated rows run on calibrated thresholds.*
- **P3c — The open failure modes**, each with a measurement that closes or
  keeps it: F → 2F cycling (LOST inside probation counts as a strike;
  measure the walk and cold-start rows unchanged); A′ regrowing in
  silence (age P only while input power exceeds a floor, so the
  `restart_a_db` trigger means what it says; measure the 20 s gap row);
  held-note blindness (not fixable by a comparator — the detector is the
  defence; promote the finding to a documented limit in `howl_guard.h` and
  the book); the Kalman tonal room-6 reading (five seed sets on that
  room: defect or noise, decided and recorded); gated-NLMS burst
  containment (resolved by P3d).
- **P3d — Default engine.** The Kalman core becomes the default in
  `mutap.afc~` and `mutap.aec~` (it already is in `afc_chain`); the NLMS
  stack stays as `@kalman 0`, documented as legacy. Every gated AFC row is
  re-measured on the new default, and the book's chapter 1 follows the
  honesty rule. *Pass: the claims register's AFC rows carry the Kalman
  default; the README's "decide after real-room listening" sentence is
  replaced by the decision and its date.*

## 7. P4 — Reality

- **P4a — Measured rooms.** Academic rooms whose licence allows an MIT
  repo (MYRiAD is the PEM-AFROW group's own database; openAIR is the
  other), one command each through `make_rir_fixtures.py --from-wav`; plus
  Tim's own swept-sine measurements of two or three rooms (the WAVs enter
  by commit; the container's network policy cannot fetch them). Re-run
  every AFC and AEC claim on them. *Pass: each gated row's median moves by
  ≤ 3 dB on the measured rooms, or the row becomes a finding with its own
  issue.*
- **P4b — A harness that models what the field has.** Options on
  `closed_loop` and `echo_scenario`, each off by default so today's rows
  stay bit-identical: a loudspeaker model (soft clip plus a memoryless
  cubic at a configurable level), microphone self-noise at a configurable
  SNR, clock drift (P2b's generator), a walking talker (crossfaded
  measured RIR pairs over seconds, not a swap), and consented real
  recordings (LibriSpeech CC BY 4.0 for speech; singing only under the
  consent rules the wake-word plan set for its hold-out). A sensitivity
  table per option. *Pass: the table exists; any option that moves a
  gated row by > 3 dB gets a row of its own.*
- **P4c — The real loop.** The thing HANDOFF.md has asked for since Rev 3.
  A Max patch on Tim's rig with the measured round trip (PROTOCOL §5),
  P2a's estimator checked against it, and a run sheet: the gain walk with
  and without the canceller, added stable gain by ear and by the
  bisection, the guard through the walk, the soundcheck procedure, a
  frequency-shift A/B, both predictors, speech and sung material. The
  session is recorded as numbers in `docs/real-room.md` (no audio in the
  repo). The AEC half: a real two-device call with the delay estimator on,
  ERLE and double-talk by ear. *Pass: ≥ +6 dB added stable gain by ear in
  two rooms; 0 howl events with the guard through the protocol's walk;
  every audible artefact named and either fixed or documented as a limit.*
  Two sessions minimum; the second after P3.
- **P4d — ITU real-speech vectors.** The P.501 attachment WAVs (Tim's
  procurement; git-ignored under `tests/data/itu/`) and the three
  signal-exact rows re-run on them. *Pass: the three rows inside their
  margins, or the "method-equivalent" caveat replaced by the measured
  gap.*
- **P4e — Hardware.** One Cortex-M55 board (an MPS3 AN547 is the obvious
  one; the QEMU model is already its twin) and one Hexagon device with the
  SDK, for cycles rather than instructions and for the VTCM / L2 layout
  work HANDOFF.md's Hexagon section still owes. *Pass: cycles per block
  for `afc_speech_48k` and `chain_48k` on both, beside the instruction
  counts, with the IPC the ratchet's budget now assumes replaced by the
  measured one.*

## 8. P5 — The battery as a product gate *(alongside everything)*

- **P5a — Three tiers.** `gate`: deterministic, platform-stable, under 10
  minutes on 4 cores, every push (contracts, validation, cross-precision,
  fingerprints, the icount ratchet, direction and survival assertions per
  working note 2). `claims`: the bisections and medians, nightly on the
  three hosts, each threshold with a per-host tolerance recorded beside
  it. `sweeps`: `MUTAP_SLOW`, weekly or on demand. ctest labels, three CI
  workflows. *Pass: `ctest -L gate` under 10 minutes here; a push cannot
  be blocked by a row whose threshold was measured on one host.*
- **P5b — Chaos out of the gate.** Every bisection-based ASG row in the
  gate becomes a direction or floor assertion with several dB of margin;
  the bisection moves to `claims`. The "repeat until-fail 20" step is
  retired once no gate row needs it.
- **P5c — The register as the source.** README, HANDOFF, headers and the
  book quote `docs/claims.md`; a CI check diffs the prose numbers against
  it. The measurement essays leave the headers for
  `docs/measurements/<component>.md` (P6a), each linked from the header's
  contract comment.
- **P5d — Emulated selection parity.** The two hand-synchronised test
  selections (working note 4) become one list in `tests/CMakeLists.txt`
  that generates `bare_metal_main.cpp`'s filter. *Pass: a selection edit in
  one place reaches both legs.*

## 9. P6 — Structure and release

- **P6a — Headers hold contracts, docs hold measurements.** `postfilter.h`
  and `howl_guard.h` keep the numeric contract and a one-paragraph link to
  their measurement doc; the essays move. Target: no header over 600
  lines; no behaviour change (fingerprints identical).
- **P6b — One estimator.** Once P1e's vectorised float path exists, the
  suppressor's branchy / branch-free pair collapses to the form the ratchet
  prefers per target, selected at compile time, with the parity job
  checking the two targets' fingerprints rather than two source forms.
- **P6c — Release engineering.** Tagged releases with the claims register
  snapshot, ABI notes (the engine tag), the consumer re-pin script, and a
  `SUPPORT.md` stating which geometries and rates are calibrated (P2c's
  table) and which are not.

## 10. Decisions for Tim

1. **Namespace.** Keep `tap::mu` only and fix the README, or add the
   `mutap` alias for the older spelling. (Recommended: alias; it costs one
   line and MuTap-Max already spells both.)
2. **Stereo far end for the AEC.** Build it (a second reference ring and
   a decorrelation step, a milestone of its own) or state single-channel
   as the product's scope. (Recommended: scope it out for now; revisit if
   a product asks.)
3. **The probe signal.** Whether a deployment may emit a soundcheck burst
   through the PA at all; if not, P3a's procedure is "backing track at a
   known level" and the cold-start pass criterion changes.
4. **Hardware budget.** Which M55 board and which Hexagon device (P4e),
   and whether the Pico 2 W named in the wake-word plan is the M33 target
   for this line too.
5. **Rooms.** Which measured rooms join (P4a), and whether Tim's own
   measurements are published under MIT or kept as private fixtures.
6. **Freedom to operate.** Who runs the patent review (P0d) and whether a
   result blocks P6c.

## 11. Risks, honestly

- **P1 may not reach 10 %.** The predictor refit and the pitch search are
  the known cost; if the single-convolution form (P1d) loses ASG, the
  floor is roughly 15 % on x86 and the M55 budget may fit only at block
  128 or 1024 taps with a lower order. The phase's pass criterion allows
  "the geometry it does fit, documented".
- **P4c may contradict the simulation.** That is its purpose. A real room
  reading several dB below the simulated ASG is a finding that reorders
  P3 and P4b, not a failure of the plan.
- **The calibration procedure (P3a/b) changes the product's UX.** A guard
  that refuses to open without a soundcheck is safer and less convenient;
  the help patcher and the book must carry the procedure or users will
  turn the guard off.
- **P2b's resampler is a new real-time dependency** on another family
  repo's engine; its latency and cost join the ratchet.
- **Thresholds will move** under P1 and P4a; every move is a claims
  register row with a before and after, never a silent re-record.

## 12. Order of work, as a first quarter

| weeks | track A | track B |
|---|---|---|
| 1 | P0a–d | P5a tiers (labels, workflows) |
| 2–4 | P1a count, P1b cadence, P1c pitch search | P2a delay estimator |
| 5–6 | P1d one convolution, P1e vectorise | P2c physical config, P2d transplant |
| 7–8 | P3a probe, P3b calibrate-first, P3c modes | P4a measured rooms, P4b harness options |
| 9–10 | P3d default engine, re-measure | P4c first real-room session |
| 11–12 | P5b–d, P6a–b | P4c second session, P4d vectors, P2b drift |
| 13 | P6c first tagged release | P4e hardware (as it arrives) |

Every row ends with its numbers in the claims register and the HANDOFF
working notes updated; a phase that misses its pass criterion records the
miss as a finding before the next phase starts.
