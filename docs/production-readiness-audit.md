# Adversarial audit of the production-readiness plan

*Audit, rev 1 — 9 October 2026, of
[`production-readiness-plan.md`](production-readiness-plan.md) rev 1. Nothing
in the plan was changed by this audit; every amendment below is a
recommendation for rev 2. Six decisions the maintainer took between the plan
and this audit are recorded in §2 and treated as fixed constraints.*

---

## 1. Verdict

**The plan's phase list survives. Its sequencing, two of its pass criteria
and one of its DSP claims do not.**

The seven phases name the right work: the predictor's cost, a procedure in
place of a calibration, the field plumbing the externals lack, measured
rooms, a tiered battery, a release. Nothing in the audit argues for a
different list. What fails, checked against the tree:

- **The first real-room session is scheduled eight weeks too late, behind a
  dependency that does not exist.** The chain already takes a hand-measured
  reference delay and the round-trip tool is in `tools/fixtures/`; the
  external already carries `calibrate`, `@cap` and the verdict thresholds.
  The session the plan itself calls "the phase that decides whether this is
  a product" can run in week 1 with the code as it is, and its findings
  should choose what the quarter funds.
- **P1d ("one convolution, not two") is algebraically wrong for this code,
  worth about 1 % of the block, and it silently moves the statistics the
  guard is calibrated on.** The prewhitened ring holds blocks whitened by
  sixteen different predictors; the saving is bounded by the core's
  filtering pass, which is a tenth of the core, which is a tenth of the
  canceller. Drop it.
- **P3a's pass criterion measures nothing the product does**, now that the
  probe is operator-triggered only. The product's actual cold-start
  behaviour — a show restart without a soundcheck — has no answer in the
  plan, and the feature that would answer it (a stored filter and state
  reloaded on restart) is scheduled as a side effect of a different item.
- **P1's M55 target is infeasible as written and unfalsifiable without the
  hardware the maintainer has declined for the quarter.** The ratchet's own
  baselines say the Kalman core alone is about 2.85 k instructions per
  sample on m55; at block 64 / 1024 taps the core by itself exceeds the
  "2× margin" budget before the predictor, the raw convolution, the shadow
  or the guard are counted.
- **Every guard number was measured at a geometry the product does not
  ship.** The guard rows run at block 64 / 1024 taps; `mutap.afc~` defaults
  to block 256 / 2048 taps. The factory thresholds and the soundcheck
  margins have never been measured at the external's default.

Beyond those, the plan omits the work that the house rules make mandatory
for every behaviour change — fingerprint re-records on nine CI legs, the
book's honesty rule, notebook re-execution, the MuTap-Max half of every
feature — and its thirteen-week table is two full-time tracks for one
person. Amended, the plan is one track: a trimmed P0, the first listen,
the predictor's cost (P1a–c), the restart path (P2d plus stored state),
the two operator procedures, the battery tiers, a second listen, one
release.

## 2. Decisions taken after rev 1 (fixed constraints for rev 2)

| Question | Decision | What it changes |
|---|---|---|
| First product | Live-sound AFC via `mutap.afc~` | AEC-only items (clock drift, the ITU rate sweep, a bulk delay line in `aec_chain`) leave the quarter. |
| Resourcing | One maintainer plus Claude Code sessions, about one quarter | One track, not two; the week table is rewritten (§5). |
| Namespace | `tap::mu` only; fix the README; no alias | P0a's scope is every `mutap::` in README.md Status (about fifteen spellings), not only the quick start. The plan's claim that MuTap-Max "already spells both" is false: `mutap::` has zero occurrences there. |
| Stereo far end | Scoped out | `SUPPORT.md` states single-channel; P2e deleted. |
| Probe signal | Operator-triggered during soundcheck only, never automatic | P3a splits into two procedures with two measurements (§3, issue 3). |
| Hardware | None this quarter | P4e leaves the quarter; P1's budget becomes an instruction count (§3, issue 4). |
| Measured rooms | The maintainer's own sweeps, published MIT | P4a uses `measure_rir.py` → `make_rir_fixtures.py --from-wav`; the harness must not band-limit a measured room a second time (§3, issue 9). |
| Patent review | Literature-level search by Claude, recorded as not a legal clearance | P0d(2) cannot gate P6c; §10 of the plan is folded into the phases and deleted. |

## 3. How the audit was run

Two passes against the working checkouts of MuTap and MuTap-Max, not against
the plan's description of them.

| Pass | What it did |
|---|---|
| Main session | Re-read the plan against the headers, harnesses, CI workflow and the test-cost table of the 9 October run; checked each pass criterion for an instrument that can measure it; checked each DSP claim against the code it describes. |
| Independent reviewer | One adversarial reviewer with no stake in the plan, briefed with the eight decisions above and ten things to probe, returning findings with file-and-line evidence. 23 findings; every numeric claim the main session relied on was re-verified by hand (the ratchet's block count, the m55 baselines, the harness config fields, the fingerprint coverage, the MuTap-Max spelling). |

The two passes overlapped on seven findings and each found things the other
missed; the merged list follows. Severity is by how much the finding changes
what happens before work starts.

## 4. Root issues, ranked

### 1. The first listen is scheduled behind a false dependency — *critical*

**What the plan says.** §2: real-room sessions "need P2's delay estimator on
the rig, so they start once P2a lands"; §12 puts the first session at weeks
9–10.

**What is true.** `afc_chain::config::reference_delay_samples`
(`include/mutap/afc_chain.h:144`) takes the measured round trip today, the
class comment (lines 90–113) says how to set it, and
`tools/fixtures/measure_rir.py` measures "round-trip latency with an
exponential sine sweep" over an electrical loopback. The external carries
`calibrate`, `@cap`, `@d_db`, `@a_db`, `@cal_s` and the margins
(`MuTap-Max/.../mutap.afc_tilde.cpp:445–576`). HANDOFF.md has listed
in-Max listening as item 1 since Rev 3. §11 of the plan concedes that a
real-room reading "reorders P3 and P4b" — which is the argument for doing it
first, not ninth.

**Amendment.** The first session is week 1–2, on the external as it is,
with the hand-measured delay and the existing soundcheck message. Its run
sheet is the plan's P4c list. What it finds decides the weight of P1, P2d
and P3 for the rest of the quarter.

### 2. P1d is wrong for this code, worth about 1 %, and moves the guard's statistics — *critical*

**What the plan says.** e′ = A(q)·e can replace the core's own filtering of
the prewhitened pair because "F̂ is held fixed within the block".

**What is true.** (a) The identity needs A(q) time-invariant over the
filter's memory. `pem_afc::process_block` refits the predictor every block
(`pem_afc.h:301`) and whitens only the new block with the new coefficients
(`:304–305`); the core pushes that spectrum into a ring sixteen blocks deep
(`fd_kalman.h:334–339`). The core's e′ is y′ₜ − Σₚ F̂ₚ U′ₜ₋ₚ with each U′ₜ₋ₚ
whitened by a different Aₜ₋ₚ, and the speech predictor's pitch ring is
stateful across blocks (`lpc.h:343–347`). Whether F̂ moves inside the block
is irrelevant. (b) The saving is the core's filtering pass: sixteen
`packed_mac` calls and one 128-point inverse (`fd_kalman.h:373–378`). The
update that stays costs a forward FFT, two sixteen-partition per-bin passes
and, with the default gradient constraint, thirty-two more FFTs
(`:433–499`). The core is 46 µs of the canceller's 432; P1d is worth
roughly 5 µs. §11's "floor roughly 15 % if P1d loses" is therefore wrong by
construction: P1b and P1c alone take the predictor from about 370 µs to
tens, which reaches the 10 % target arithmetically. (c) The shadow
comparator reads `m_e_pw` (`pem_afc.h:330–343`) and the Kalman noise
tracker reads FFT[0 | e′] (`fd_kalman.h:503–513`). Changing e′ moves the
distribution of D, whose usable window is about 1 dB wide
(`pem_afc.h:218–223`), and of A′. That, not the pitch search, is the real
reason the guard must be re-calibrated after any change to the prewhitened
pair.

**Amendment.** Drop P1d. If kept as an experiment, its pass criterion is a
paired same-host same-seed ASG comparison (issue 6) plus a before/after
distribution of D and A′ on the guard rows.

### 3. P3a measures nothing the product does; the restart case has no answer — *critical*

**What the plan says.** P3a: a probe during ARMING; pass = "cold start
declares in < 5 s in every simulated room without a backing track".

**What is true.** The probe is operator-triggered only. At every other cold
start the measured behaviour stands: "without it none declares in 20 s …
every run leaves ARMING through the cap at 10.00 s; with no cap set, all 18
such runs stay in ARMING" (`howl_guard.h:139–146`;
`tests/test_howl_guard_host.cpp`). A restart with no soundcheck — the show
after the soundcheck, a crash, a sample-rate change, every structural
attribute of the external — opens `unprotected` through the cap or never
opens. `set_thresholds()` restores a stored soundcheck
(`howl_guard.h:470–480`) but the declaration needs A′ < a_db, i.e. a
converged Kalman P; only a stored filter and P reloaded on restart gives
that without excitation. The plan has that feature as P2d's
`load_impulse_response()`, scheduled as a convenience for engine switches,
not as the restart path. Every row in `tests/support/guard_runs.h` starts
"from reset"; no row measures a restart from stored state.

**Amendment.** Two procedures, each with a measurement and a row that does
not yet exist: (1) *soundcheck*: operator probe at a stated level relative
to the cap, estimator and calibration during it; pass = declares within
X s of the probe in every simulated room, 0 howl blocks. (2) *restart*:
stored thresholds plus stored F̂ and P (the restart path, issue 5); pass =
declares within Y s of the first voiced input at the arming gain, or opens
capped and reports `unprotected`, and the help patcher says which happens.
The restart path is a product feature, scheduled as one.

### 4. P1's M55 budget fails on the core alone and has no instrument — *critical*

**What the plan says.** "An m55 instruction count that fits the 48 kHz block
budget with 2× margin", budget 533 k cycles at 400 MHz and 1.0 IPC, "the
cycle translation is a measured number from P4e".

**What is true.** `bench/baselines.json` m55: `fdkf_48k` 139,981,007
instructions over 192 blocks of 256 (`bench/icount/icount_main.cpp:71`) ≈
729 k per block ≈ 2.85 k per sample for the Kalman core alone at eight
partitions; `chain_48k` 417,313,588 ≈ 2.17 M per 5.33 ms block ≈ 2 % over
a 400 MHz × 1.0 IPC budget already. At block 64 / 1024 taps the core has
sixteen partitions, so its per-bin work per sample roughly doubles: on the
order of 350–450 k instructions per 64-sample block against the 533 k
budget, before the predictor, `pem_afc`'s own raw convolution
(`pem_afc.h:277–286`), the shadow and the guard. "Fits with 2× margin"
(≤ 267 k) fails on the core by itself. P4e has left the quarter, so the
cycle translation never arrives. (Instructions are not cycles, and Helium
can retire more than one lane per instruction; that is exactly why the
number cannot be asserted without a board.)

**Amendment.** P1a first. Then the target is stated as instructions per
block from the ratchet, at the geometry the count says fits, with the
MHz/IPC prose deleted until a board exists. The plan's §11 escape hatch
("the geometry it does fit, documented") becomes the primary criterion.

### 5. P2d's "guard stays declared across the switch" needs the transplant to carry P — *major*

A′ = ΣP / ΣP(reset) (`fd_kalman.h:290, 311`; `pem_afc.h:164–168`).
Loading F̂ with P at its reset value reads A′ = 1 (0 dB) ≥
`restart_a_db = −1` and sends the guard to ARMING (`howl_guard.h:47–49,
236, 961–966`); with P reset the Kalman also churns the loaded filter
(`fd_kalman.h:76–78`). NLMS → Kalman has no P to carry at all.
**Amendment.** The transplant contract names what P and ψ_s become
(stored alongside F̂ for the restart path; a stated prior for an engine
switch); pass includes "A′ before/after within X dB". This, plus the product
geometry (issue 8), is P3's real dependency once P1d is dropped.

### 6. "≤ 0.5 dB median ASG loss" is below the instrument's noise — *major*

Working note 2: single-seed ASG moves by dB between platforms; the karaoke
headline rows differ by up to 3.23 dB across hosts (HANDOFF item 9); the
gated medians are five seeds of a chaotic loop. A 0.5 dB gate on an
unpaired comparison will pass and fail on noise. **Amendment.** A paired
design: same host, same seeds, the per-seed difference (new − old), both
generator families, median and range of the differences, seed count chosen
so the spread is under the threshold; recorded by a `MUTAP_SLOW` sweep, not
a gated row.

### 7. P2a's pass criteria cannot be measured with the harnesses that exist — *major*

- `echo_sim::config` is `echo_path` and `block_size` only
  (`tests/support/echo_scenario.h:53–56`): no bulk delay, so the AEC half
  has no scenario (moot for the quarter, decision 1).
  `closed_loop_sim::config` has no electrical delay either
  (`closed_loop.h:35–40`); only the AFC loops carry one
  (`two_mic_loop.h`, `guard_loop.h:165`, `test_afc_chain.cpp`).
- "0 false moves over the stable-material battery" needs a per-block
  estimate trace; `guard_runs.h` traces state, gain, verdict and
  misalignment only.
- `reference_delay_samples` sizes the ring at construction
  (`afc_chain.h:164`); a live setter needs a preallocated maximum stated as
  a contract number.
- "Shift the partition ring rather than reset" is wrong: the ring holds
  block spectra; what must move is F̂ (copy, shift Δ taps, load), so P2a
  depends on P2d's `load_impulse_response()`, scheduled two weeks after it.
- The closed-loop bias defeats a cross-correlation estimator on tonal
  material exactly as it defeats the largest-tap readback: "on the held
  note the largest tap is the closed-loop bias at the lag where the
  reference is in phase with the note" (`afc_chain.h:353–357`; HANDOFF
  item 9). GCC-PHAT between the speaker feed and the mic sees the same
  thing. The estimator must run on broadband material — which the
  soundcheck probe is — and freeze otherwise.
- `afc_chain::reference_aligned()` (`afc_chain.h:361`) is already an
  estimator; compare it against GCC-PHAT before building a second.

**Amendment.** P2a becomes "delay estimation during the soundcheck probe",
measured on the AFC loops with an inserted electrical delay and a
per-block estimate trace added to `guard_runs.h`; it lands after P2d.

### 8. Every guard number is at a geometry the product does not ship — *major*

`tests/support/guard_loop.h:64–70`: block 64, 1024 taps, 48 kHz.
`mutap.afc_tilde.cpp:163–164`: the external defaults to a 2048-tap filter
at block 256. The factory `d_db` / `a_db`, the 4 / 3 dB soundcheck margins,
the cold-start timings and the attribution numbers were never measured at
the external's default. The plan's cost headline (32 % of a core) is also
the block-64 number; at the external's default the same canceller measures
519 µs per 5.33 ms block, about 10 %, which is already the P1 target.
**Amendment.** Decide the product geometry first (block 64 for the 10 ms
loop the PoC protocol assumes, or block 256 as shipped), state its latency
budget, and re-measure the sixteen gated guard rows there. P3b's pass
becomes "the external's default geometry has calibration margins measured
on both material sets". P3b's other content — "stays ARMING without a cap"
— is measured today (`howl_guard.h:143–146`) and the external already
carries the whole calibrate surface; what is missing is the measurement at
the shipping geometry, not the feature.

### 9. Measured rooms would be band-limited twice — *major*

`tests/support/rooms.h:66` `band_limited()` applies the loudspeaker band
(80 Hz highpass, 16 kHz lowpass) to every host-suite path because the
synthetic rooms have no speaker in them. A swept-sine measurement already
contains the loudspeaker and the microphone. **Amendment.** The fixture
carries a flag (`measured: true`) that the harness honours by skipping
`band_limited()`; `make_rir_fixtures.py --from-wav` writes it. And expect
every AFC row to move by more than the plan's 3 dB on a measured room by
construction: 1024 taps is 21 ms, a real RT60 is hundreds of ms; filter
length is the variable P4a measures, not a constant.

### 10. No hot path has a NaN watchdog — *major* (missing from the plan)

`isfinite` appears in one hot path only, the NLMS IPC estimator
(`fdaf.h:374`). The Kalman core, `pem_afc`, the suppressor and the guard
have none outside their constructors. A non-finite sample from upstream
(a Max patch's divide by zero, a disconnected signal inlet) enters F̂ and P
and stays there until `reset`; the guard's `analyze()` floors the
statistics at `numeric_limits::min()` but a NaN passes `std::max`. A
product canceller needs one cheap check per block on the input power and
the residual power, a state reset on failure, and a counter the external
reports. **Amendment.** A P0 item with a test: feed one NaN block, assert
recovery within one block and the counter incremented; fingerprints
unchanged on finite input.

### 11. P5a's "gate under 10 minutes" does not add up; P5d must precede it — *major*

The 9 October run: 331 tests under 10 s cost 364 s; 65 tests of 10–60 s
cost 1,910 s; 24 tests over 60 s cost 4,531 s. A 600 s gate on four cores
is about 2,400 CPU-seconds: every row over 60 s moves out and about half of
the 10–60 s band with it — where the ITU typed rows live
(`g168_adapted.ComfortNoiseTracksBackground` 64–66 s,
`itu_dynamics.NoisePumping` 58 s) that the plan wants in the gate as
contracts. CI runs ctest serially on every leg (`ci.yml:94, 159, 258, 388,
468`: no `-j`) and the sanitizer job runs the whole battery (ASan rows of
697 s, HANDOFF item 11), unaddressed. P5b changes the assertion's form, not
its cost: the loop simulation is the cost, so "direction instead of
bisection" saves nothing unless the probe is shortened.
`gtest_discover_tests` labels are per executable
(`tests/CMakeLists.txt`), so three tiers need the generated selection list
P5d describes — P5d first. **Amendment.** Gate = a per-test time cap
(about 20 s) plus `ctest -j`; the rows that move are listed by name; the
sanitizer leg gets its own tier; P5d precedes P5a.

### 12. P0d(1), the Hilbert redesign, is not week-one hygiene — *major*

The eight coefficients feed every shift row: karaoke S1 / S3, the reverb
topology rows, the guard's audible-cost rows, the two-mic shift rows, whose
backing sweeps cost 1 h 16, 4,656 s and 9.8 h (HANDOFF items 9, 12, 13). A
new design, even re-pinned ≥ 44 dB over the same band
(`frequency_shifter.h:61–66`), moves all of them, and the shifter is in no
fingerprint (zero lines in `tests/fingerprints/*.txt`), so the move is
silent. **Amendment.** Keep the coefficients; record the licence question
honestly (a Stack Exchange answer carries CC BY-SA attribution terms; the
numbers themselves are the output of a design procedure); schedule any
redesign inside P4 / P5 with the re-measurement as its stated cost.

### 13. P2c measures the wrong battery and misses the sample-count configs — *major*

`speech_predictor::config` carries `min_lag 32`, `max_lag 400` and
`analysis_capacity 1024` in samples (`lpc.h:336–338`); the external never
rescales them (`mutap.afc_tilde.cpp:914–932` rescales `analysis_window`
only). At 96 kHz the pitch floor becomes 240 Hz, and `analysis_capacity ≥
2·max_lag` breaks once `max_lag` is rescaled. P2c's pass runs the ITU
battery, which exists at 48 and 16 kHz only (`itu_chain.h`) with CSS
native at 44.1; 96 kHz is not an ITU geometry and the AEC is not the first
product. **Amendment.** Pass = the AFC loop rows at 44.1 and 96 kHz with
the predictor's lags in hertz, the shadow smoothing and the guard's
`loop_period_s` derived from seconds, and the preset's two stated
exceptions (`postfilter.h:1128–1136`: transition and floor bias are not
rescaled) carried explicitly. The AFC path has no preset at all today — the
external assembles its config by hand — so a `pem_afc_preset(block, fs)` is
the actual gap.

### 14. Clock drift is moot for the first product — *major*

`mutap.afc~` reads mic and speaker feed from one interface; they share a
clock. P2b's resampler would also add a dependency the tree does not have
(the DspTap pin carries `kaiser.h`, `fir_kernels.h` and `decimate.h`, no
`async` engine; there is no SampleRateTap submodule), with an ABI, a
latency contract and ratchet rows of its own. **Amendment.** P2b leaves the
quarter with the AEC line; recorded in `SUPPORT.md` as a known gap for
two-device echo cancellation.

### 15. §12 is two full-time tracks for one person and omits half the work — *major*

Weeks 2–4 alone hold P1a, P1b, P1c and P2a. Missing entirely: the
MuTap-Max half of every feature (setters, transplant, defaults, help
patchers, maxrefs, the external's own test file and CI); the book — the
honesty rule (working note 8) makes P3b, P3d and P2c each a chapter edit in
the same change; notebook re-execution (`afc_demo.ipynb` already quotes
pre-band-limit numbers per HANDOFF item 8; four notebooks); fingerprint
re-records on nine legs for every behaviour change (the plan mentions them
once, in P6a); re-running the `MUTAP_SLOW` sweeps after P1c; the P.501
procurement blocker (`itu.int` allowlisting, HANDOFF Rev 5) with no owner
or date before week 11. **Amendment.** One track (§5).

### 16. Lesser items — *minor*

- **P1c.** The FFT variant is equivalent only with prefix-sum
  normalisation: `speech_predictor::analyze` computes a *normalised*
  correlation with per-lag energies (`lpc.h:414–432`), which are prefix
  sums of x² and come for free. A predictor embedding a 2048-point
  `basic_real_fft` must live inside `tap::mu::inline TAP_DSP_FFT_ABI` and
  become the seventh embedder in `test_fft_engine_contract.cpp` (working
  note 6a). DspTap's `decimate.h` offers 2 / 3 / 6 to 16 kHz, not 4×. The
  voicing decision and its 0.3 threshold must be evaluated at full rate on
  the fine lags, and the pass must bound *false* voicing on frames brute
  force does not voice, not only misses.
- **P1e.** "≤ 0.6× the P1a seed" is reachable by P1c alone; the
  vectorisation baseline is the post-P1b–c scalar count.
- **P0b.** The claims register cannot be generated in week 1: only the
  `MUTAP_SLOW` sweeps use `RecordProperty`; thresholds live in comments.
  A hand-maintained table until P5's tiers exist; a CI check on prose
  numbers is more machinery than a solo maintainer should own.
- **P3c.** "Age P only while input power exceeds a floor" changes
  `fd_kalman.h` under the ITU-certified chain (`postfilter.h:1138–1141`):
  opt-in, default off, ITU rows bit-identical, or it is a re-certification.
- **P3d.** `afc_chain` already defaults to PEM + FD-Kalman
  (`afc_chain.h:132`) and the karaoke, chain, two-mic, guard and reverb
  rows run on it; only `test_pem_afc`'s NLMS canaries and the external's
  `@kalman` default change. Smaller than the plan says.
- **P4c.** "≥ +6 dB by ear" is not a measurement and the protocol it cites
  (`PROTOCOL.md` §5.1, §7.3, referenced from `afc_chain.h:99`,
  `decorrelated_loop.h:291` and `docs/reverb-afc.md`) is not in the repo.
  The instrument exists: the ramp plus the audible criterion
  (`tools/notebook/karaoke_audible.py`, `tools/fixtures/howl_criterion.py`).
  Pass = on the rig's recorded output the canceller raises both the ramp's
  runaway and its audible limit (a direction), numbers recorded, and the
  measured RIR enters the harness so the same room runs in simulation.
  `PROTOCOL.md` lands in P0.
- **P0c.** MuTap-Max pins a main-reachable SHA (449cf6c, thirteen commits
  behind) by working note 6; "MuTap-Max pins the tag" conflicts with it.
  Pin tags at releases only.
- **P6a / P5c.** Headers carry the measured record on purpose
  (`pem_afc.h:60–63`): move the tables, keep the failure modes and contract
  numbers in the header; "no header over 600 lines" is not a product
  criterion.
- **P2b (if ever built).** "Bit-identical at 0 ppm" only if bypassed at
  ratio 1.0; its latency joins `afc_chain::latency()`.
- **§10 of the plan** contradicts the decisions now taken and contains one
  false statement (the MuTap-Max spelling); fold the decisions into the
  phases and delete the section.

## 5. What survives unchanged

- The seven-phase list and the house workflow it is written in.
- P0a (README namespace, counts), P0c (tags, CHANGELOG, re-pin script),
  P0d(2) as a recorded literature search.
- P1a (count the AFC path on the ratchet first) — the cheapest and most
  informative item in the plan — and P1b / P1c with paired measurement.
- P2d as the restart path's mechanism, with the P / ψ_s contract added.
- P3b's intent (calibrate-first), P3c's list of open modes, P3d.
- P4a with the measured-room flag, P4b's harness options, P4d as a
  procurement item with an owner.
- P5's tiers once P5d precedes P5a; P6c.

## 6. Recommended amendments to the plan, in order

1. **Put the first real-room session in weeks 1–2** on the external as it
   is; order the quarter by what it finds (issue 1).
2. **Decide the product geometry** (block, taps, latency budget) before P1a,
   and re-measure the guard rows there (issue 8).
3. **Drop P1d**; state P1's target as an instruction count at the geometry
   P1a says fits; paired measurement for P1b / P1c (issues 2, 4, 6).
4. **Rewrite P3a as two operator procedures**, soundcheck and restart, and
   promote the stored-state restart path (F̂, P, thresholds) to a feature
   with its own row; P2d's contract names P and ψ_s (issues 3, 5).
5. **Add the NaN watchdog** to P0 (issue 10) and `PROTOCOL.md` to P0a.
6. **Move P2a after P2d and tie it to the probe**; drop P2b, P2e and P4e
   from the quarter; rewrite P2c around the AFC path and a
   `pem_afc_preset` (issues 7, 13, 14).
7. **Flag measured rooms** so the harness does not band-limit them; make
   filter length P4a's variable (issue 9).
8. **P5d before P5a**; gate by a per-test cap with `ctest -j`; a sanitizer
   tier (issue 11).
9. **Keep the Hilbert coefficients**; record the licence question; any
   redesign carries its re-measurement cost (issue 12).
10. **One track** with the MuTap-Max half, the book, the notebooks and the
    fingerprint re-records written in as rows (issue 15); delete §10.

A rev 2 of the plan carrying these amendments should be a single track of
roughly: P0 (trimmed, plus watchdog and PROTOCOL.md) → first listen → P1a,
geometry decision, P1b–c with paired sweeps → P2d and the restart path →
the two procedures and the guard re-measurement at the product geometry →
P5d, P5a → second listen → P4a on the measured rooms → one tagged release.
