# From research code to product — the production-readiness plan

*Proposal, rev 2 — 9 October 2026. Rev 1 was written against the audit of
the same date and then audited itself
([`production-readiness-audit.md`](production-readiness-audit.md)); rev 2
carries all ten of that audit's amendments and the eight decisions the
maintainer took between the two. The seven parallel phases of rev 1 are now
one track of ten milestones, M0–M9, each with the pass criterion that lets
it fail, in the house workflow: scratch-measure first, threshold with
margin, rooms from both generator families, numbers quoted with their host.
Nothing here is built.*

---

## 1. What this plan is for

The 9 October audit of the repository found that the code is clean,
allocation-free in the hot path and honest about its limits, and that five
things stand between it and a product: nothing has been validated outside
simulation; the feedback canceller costs ten times its core and was never
counted on an embedded target; the safety layer runs on one simulated
loop's calibration; the field plumbing a live rig needs is absent; and a
list of known failure modes is recorded but open. Behind those, a 420-test
battery that is a research instrument, prose numbers that drift, no
releases, and an IP policy that covers copyright only.

The audit of rev 1 added what the plan itself had wrong: the first real-room
session sat eight weeks behind a dependency that does not exist; one DSP
item was algebraically wrong for this code and worth about 1 %; the
cold-start criterion measured nothing the product does once the probe is
operator-only; the M55 budget failed on the Kalman core alone; every guard
number was measured at block 64 / 1024 taps while the external ships block
256 / 2048 taps; measured rooms would have been band-limited twice; no hot
path has a NaN watchdog; and the schedule was two tracks for one person
with the MuTap-Max half, the book, the notebooks and the fingerprint
re-records missing.

Rev 2 is the single track those corrections leave.

## 2. Decisions taken (fixed for this plan)

| Question | Decision | Consequence |
|---|---|---|
| First product | Live-sound acoustic feedback cancellation via `mutap.afc~` | The AEC line is maintained, not advanced, this quarter. |
| Resourcing | One maintainer plus Claude Code sessions, about one quarter | One track; the week table in §6. |
| Namespace | `tap::mu` only; fix the README; no alias | Every `mutap::` in README.md (about fifteen, mostly in Status) is corrected and every snippet compiles as a test. |
| Stereo far end | Scoped out | `SUPPORT.md` states single-channel. |
| Probe signal | Operator-triggered during soundcheck only; never automatic | Two procedures, soundcheck and restart (M5). |
| Hardware | None this quarter | Instruction counts only; cycle claims deleted; M55 / Hexagon work parked (§7). |
| Measured rooms | The maintainer's own swept-sine measurements, published MIT | Captured at the first listen (M1), fixtures at M8, flagged so the harness does not band-limit them again. |
| Patent review | A literature-level search by Claude, recorded as not a legal clearance | Cannot gate a release; recorded in `THIRD_PARTY_NOTICES.md` (M0). |

## 3. The track

```
M0 hygiene ─ M1 first listen ─ M2 count + geometry ─ M3 predictor cost
   ─ M4 restart path ─ M5 two procedures + guard re-measure ─ M6 battery tiers
   ─ M7 second listen ─ M8 measured rooms ─ M9 release
```

Three rules hold for every milestone that changes behaviour, and are
counted in its cost rather than left implicit:

- **Fingerprints.** Any change that moves output bits re-records
  `tests/fingerprints/<leg>.txt` on every leg from the PR's own CI run
  (working note 6); the PR states which lines were expected to move.
- **The book and the notebooks.** Working note 8: a chapter that quotes a
  number that moved is edited in the same change; a notebook whose numbers
  moved is re-executed through its builder script
  (`tools/notebook/build_*.py`), never edited by hand.
- **The MuTap-Max half.** Every library feature the external exposes lands
  with its attribute or message, its maxref and help text, its row in the
  external's test file, and the submodule re-pin, in the MuTap-Max PR that
  follows the MuTap merge.

## 4. Milestones

### M0 — Hygiene *(week 1)*

- **M0a — README and counts.** Every `mutap::` becomes `tap::mu::`; the
  quick start compiles as `tests/test_readme_snippets.cpp`; HANDOFF.md's
  test count and the intro of every doc that quotes one are corrected.
  *Pass: the snippet test is in the gate.*
- **M0b — `PROTOCOL.md`.** `afc_chain.h`, `decorrelated_loop.h` and
  `docs/reverb-afc.md` cite a `PROTOCOL.md` (§5.1 round-trip measurement,
  §5.3 loudspeaker DSP latency, §7.3 the audible criterion's flags) that is
  not in the repository. It is written from what those references and
  `tools/fixtures/measure_rir.py` / `howl_criterion.py` define, and M1's run
  sheet is its first section. *Pass: every existing reference resolves.*
- **M0c — The NaN watchdog.** One finite check per block on the input and
  residual power in `pem_afc`, `partitioned_fdkf`, `partitioned_fdaf`,
  `residual_suppressor` and `howl_guard::analyze`; on failure the stage
  resets its state (the guard to ARMING) and increments a counter the
  external reports on its right outlet. *Pass: a test feeds one NaN block
  per stage and asserts recovery within one block and the counter at 1;
  fingerprints unchanged on finite input; cost ≤ 0.1 % of the stage on the
  ratchet.*
- **M0d — Versioning.** Tag `v0.1.0` at the current `main`; semver;
  `CHANGELOG.md`; `scripts/repin.sh` for the MuTap-Max submodule dance
  (working note 6: MuTap-Max pins a main-reachable SHA between releases and
  a tag at releases). *Pass: the tag exists.*
- **M0e — The claims table, by hand.** `docs/claims.md`: one row per
  number README.md "Status" quotes — claim, gating test, threshold, host,
  date. Hand-maintained until M6 gives the claims tier `RecordProperty`
  output to generate it from. *Pass: every Status number has a row.*
- **M0f — IP, recorded.** The literature-level patent search (PEM-based
  feedback cancellation, frequency-domain Kalman echo cancellation,
  dual-path comparators, residual-echo suppression with comfort noise)
  written into `THIRD_PARTY_NOTICES.md` as what it is: a search, not a
  clearance. The Hilbert coefficients stay; their entry records the source
  page's terms (a Stack Exchange answer carries CC BY-SA attribution) and
  that the numbers are the output of a published design procedure. No
  redesign: it would move every shift row silently (the shifter is in no
  fingerprint) and the sweeps behind those rows cost hours each.

### M1 — The first listen *(weeks 1–2, on the code as it is)*

The session HANDOFF.md has asked for since Rev 3, run before any DSP
changes so that it orders the rest of the quarter.

- **Rig.** A Max patch with `mutap.afc~` as shipped (NLMS default and
  `@kalman 1`, `@shadow 2`, `@guard 1`), the interface's round trip
  measured with `measure_rir.py` over an electrical loopback and entered by
  hand, `@loop_ms` from it, `@ceiling` and `@cap` from the soundcheck's
  gain structure. Two rooms at least.
- **Sweeps.** The same session captures each room's impulse response with
  the sweep (`measure_rir.py sweep` / `deconvolve`), which M8 turns into
  fixtures. The WAVs enter by commit under MIT.
- **Run sheet** (`PROTOCOL.md` §8): the gain walk with the canceller off
  and on, speech and sung material; the ramp recorded at the chain output
  and run through the audible criterion offline
  (`tools/notebook/karaoke_audible.py`); `calibrate` with and without the
  backing track; a frequency-shift A/B at 2 and 5 Hz; both predictors;
  both engines; every audible artefact named.
- *Pass: in each room the canceller raises both the ramp's runaway limit
  and its audible limit (a direction, per working note 2), the numbers in
  `docs/real-room.md` with the interface, rooms and gain structure; a ranked
  list of what was heard that M3–M5 answer.* A room where the direction
  fails is a finding that reorders the track, not a failure of the plan.

### M2 — Count it, and decide the geometry *(week 3)*

- **M2a — The AFC path on the ratchet.** Icount scenarios `afc_speech`,
  `afc_warped` (shadow 2) and `guard_m1` at both candidate geometries,
  block 64 / 1024 taps and block 256 / 2048 taps, on m55, m33 and hexagon;
  baselines seeded. *Pass: `NO BASELINE` gone; `bench/README.md` carries
  the counts per block with no cycle or MHz prose.* For reference, the
  existing m55 baseline puts the Kalman core alone at about 2.85 k
  instructions per sample at eight partitions.
- **M2b — The product geometry.** Decided from M1 (what the loop needed),
  M2a (what fits) and the latency budget (`afc_chain::latency()` plus the
  interface): block 64 for the 10 ms loop the PoC protocol assumes, or
  block 256 as the external ships, or a third. Written into the external's
  defaults, `PROTOCOL.md`, and `tests/support/guard_loop.h`'s constants,
  which today are block 64 / 1024 taps while the external ships block 256
  / 2048. *Pass: one geometry, its latency budget in milliseconds, and the
  sixteen gated guard rows re-run at it (they move; M5 re-calibrates).*
- **M2c — `afc_preset(block, fs)`.** The AFC path has no preset: the
  external assembles its config by hand and rescales only the shadow
  smoothing and the analysis window. A `pem_afc_preset<Sample>(block_size,
  partitions, sample_rate)` carrying the predictor's lags in hertz
  (`min_lag` / `max_lag` are samples today, so 96 kHz would raise the pitch
  floor to 240 Hz and break `analysis_capacity ≥ 2·max_lag`), the shadow
  smoothing in seconds, and the guard's `loop_period_s`; the external calls
  it. *Pass: bit-identical at 48 kHz / block 256; the AFC loop rows at
  44.1 and 96 kHz inside their 48 kHz margins, or the rate documented as
  unsupported in `SUPPORT.md`.*

### M3 — The predictor's cost *(weeks 4–6)*

Target, stated so the milestone can fail: at the product geometry,
`pem_afc` on the speech cascade at **≤ 0.33× its M2a instruction count**
(the core is a tenth of today's canceller; the predictor is the rest) and
**≤ 10 % of one x86 core per microphone**; at block 256 the external's
default already measures about 10 %, so the x86 target binds only if M2b
chooses block 64.

- **Measurement design, before any change.** A paired sweep: same host,
  same seeds, per-seed ASG difference (new − old), both generator
  families, median and range of the differences, seed count chosen so the
  range is under the threshold; recorded by a `MUTAP_SLOW` row, never
  gated on an absolute. The unpaired 0.5 dB gate of rev 1 was below the
  instrument's noise (karaoke rows differ by up to 3.23 dB across hosts).
  *Threshold: median difference ≥ −0.5 dB with the range stated.*
- **M3a — Refit cadence.** `pem_afc::config::refit_interval_blocks`
  (default 1, bit-identical). Measured at 1, 2, 4, 8, both predictors,
  Kalman core. *Pass: the largest interval inside the threshold becomes
  the preset's value.*
- **M3b — The pitch search.** `speech_predictor::analyze` computes a
  normalised correlation over 369 lags on a 1024-sample window every
  block. Two candidates, measured against brute force on the test
  materials: (i) a coarse search on a decimated residual (DspTap's
  `decimate.h` offers 2 / 3 / 6; 2 or 3 here) and a ±4-lag fine search
  with ρ and the 0.3 voicing threshold evaluated at full rate; (ii) the
  cross-correlation by one 2048-point real FFT with the per-lag energies
  as prefix sums of x² (which is what makes it equivalent to the
  normalised form). A predictor that embeds a `basic_real_fft` moves
  inside `tap::mu::inline TAP_DSP_FFT_ABI` and becomes the seventh embedder
  in `test_fft_engine_contract.cpp` (working note 6a). *Pass: lag within
  ±1 sample and the same voicing decision on ≥ 99 % of frames brute force
  voices, false voicing on ≤ 0.5 % of frames it does not, ASG inside the
  paired threshold, the search ≥ 5× cheaper on the ratchet.* Brute force
  stays behind a flag so the old numbers remain reproducible.
- **Not done: the one-convolution form.** Rev 1's P1d assumed e′ = A(q)·e
  equals the core's prewhitened error; it does not for a predictor refit
  every block (the ring holds sixteen blocks whitened by sixteen
  predictors), the saving is bounded by the core's filtering pass (about
  5 µs of 432), and it changes the error the shadow comparator and the
  noise tracker read. Recorded here so it is not re-proposed.
- **Not done this quarter: vectorisation.** The float32 per-bin loops get
  the `__restrict`-parameter treatment only if M2a shows the autovectoriser
  bailing where it matters; explicit MVE / HVX intrinsics wait for hardware
  (§7).
- **Cost rows carried.** Fingerprints on nine legs; the karaoke `MUTAP_SLOW`
  sweep re-run at the new cadence (its headline rows feed `claims.md`);
  `afc_demo.ipynb` re-executed; the book's chapter 1 cost paragraph.

### M4 — The restart path *(weeks 6–7)*

The product's actual cold start is the show after the soundcheck, a
sample-rate change, or any structural attribute of the external — not a
reset with a probe. Today that opens `unprotected` through the cap or never
opens. The feature is a stored state reloaded on restart.

- **M4a — Snapshot and restore.** `pem_afc::snapshot(bytes)` /
  `restore(bytes)` over a caller-provided buffer (allocation-free, size from
  `snapshot_size()`): F̂ as partition spectra, the Kalman P and ψ_s, the
  predictor's coefficients and state, the shadow's F̂ and P; a version and
  geometry header so a mismatch is refused. `load_impulse_response()` is
  the time-domain half for consumers that hold only taps. The contract
  states what P and ψ_s become on an engine switch (a stated prior) and on
  a geometry change (refused). *Pass: restore then one silent block
  reproduces the pre-snapshot block output bit-exactly; `uncertainty_ratio`
  before and after within 0.1 dB — the guard reads A′ = ΣP / ΣP(reset), so
  a restore that reset P would read 0 dB ≥ `restart_a_db` and re-arm.*
- **M4b — The external.** `mutap.afc~` messages `store <path>` / `recall
  <path>` carrying the snapshot with the guard's thresholds (which already
  survive `reset`) and the soundcheck's `d_db` / `a_db`; the engine
  transplant across `@warp`, `@kalman`, `@gate`, `@shadow`, `@guard`,
  `@ceiling`, `@loop_ms` so those no longer discard the filter; `@block`
  and the filter length still cold-start. *Pass: switching engine or
  predictor mid-run re-converges in < 1 s on the misalignment oracle
  against the measured 1.4 s cold start; the guard stays declared.*
- **M4c — The row that does not exist.** `tests/support/guard_runs.h`
  gains a "restart from stored state" row: soundcheck, store, reset,
  recall, first voiced input. It is M5's instrument.

### M5 — Two operator procedures, and the guard at the product geometry *(weeks 8–9)*

- **M5a — Soundcheck.** Operator-triggered: a shaped-noise probe at a
  stated level relative to `@cap`, during which (1) the canceller
  converges, (2) the reference delay is estimated — `afc_chain::
  reference_aligned()` already reads the largest tap; a GCC-PHAT estimator
  is built only if that readback measurably fails on the probe — and
  written back (the ring's maximum preallocated as a contract number),
  (3) the soundcheck sampler collects D and A′, (4) the snapshot is
  stored. A delay estimator runs on the probe only: on tonal material the
  closed-loop bias puts the largest correlation at the lag where the
  reference is in phase with the note (HANDOFF item 9), so an estimator
  that runs during the show would move on held notes. *Pass: declares
  within X s of the probe in every simulated room at the product
  geometry, 0 howl blocks; the estimated delay within ±8 samples of the
  inserted electrical delay on the AFC loops (`guard_loop.h` gains an
  inserted-delay parameter and a per-block estimate trace).*
- **M5b — Restart.** Stored thresholds plus M4's snapshot. *Pass: declares
  within Y s of the first voiced input at the arming gain, or opens capped
  and reports `unprotected`, and the help patcher says which; 0 howl
  blocks on M4c's row across the rooms.* X and Y are measured first and
  gated with margin, per working note 1.
- **M5c — Calibrate-first.** A guard built without a stored calibration
  and without a cap stays ARMING and says so; the factory `d_db` / `a_db`
  are the fallback the help patcher explains, not the defaults a mic opens
  on. *Pass: the sixteen gated guard rows and the two margin rows
  (`cal_d_margin_db` 4, `cal_a_margin_db` 3) re-measured at M2b's
  geometry on both material sets; the externals' defaults follow.*
- **M5d — The open modes**, each closed or kept by a measurement: F → 2F
  cycling (a LOST inside probation counts as a strike; the walk and
  cold-start rows must not move); A′ regrowing in silence (age P only while
  input power exceeds a floor — a `fd_kalman.h` change under the
  ITU-certified chain, so opt-in, default off, ITU rows bit-identical);
  the Kalman tonal room-6 reading (five seed sets on that room: defect or
  noise, recorded); held-note blindness (not fixable by a comparator;
  promoted to a documented limit in `howl_guard.h` and the book);
  gated-NLMS burst containment (resolved by M9a's default).
- **Carried.** The help patcher walks both procedures; the book's chapter
  1 gains "Soundcheck and restart"; `afc_demo.ipynb` section on the guard
  re-executed.

### M6 — The battery as a gate *(weeks 9–10)*

The 9 October run: 420 tests, 6,805 CPU-seconds; 331 under 10 s cost
364 s, 65 of 10–60 s cost 1,910 s, 24 over 60 s cost 4,531 s. CI runs
ctest serially on every leg and the sanitizer job runs the whole battery.

- **M6a — One selection list.** The emulated selection lives in two places
  kept in sync by hand (working note 4); it becomes one list in
  `tests/CMakeLists.txt` that generates `bare_metal_main.cpp`'s filter and
  the hosted tiers' label filters. *Pass: one edit reaches every leg.* This
  precedes the tiers because `gtest_discover_tests` labels are per
  executable.
- **M6b — Three tiers.** `gate`: a per-test cap of 20 s, `ctest -j`, every
  push, under 10 minutes on four cores (the rows that move out are listed
  by name in the PR; the ITU typed rows over the cap move to `claims` with
  a short-probe contract twin in the gate where one exists). `claims`:
  nightly on the three hosts, each threshold with its host tolerance beside
  it. `sweeps`: `MUTAP_SLOW`, weekly. The sanitizer leg runs `gate` plus a
  named subset. *Pass: `ctest -L gate -j4` under 600 s here; a push cannot
  be blocked by a row whose threshold was measured on one host.*
- **M6c — Chaos out of the gate.** Every bisection-based ASG row left in
  the gate becomes a direction or floor assertion with several dB of
  margin on a shortened probe; the bisection moves to `claims`. The
  "repeat until-fail 20" CI step is retired once no gate row needs it.
- **M6d — The claims table, generated.** `claims` rows record
  `RecordProperty`; `docs/claims.md` is rendered from the nightly run; a
  CI check diffs README numbers against it. *Pass: M0e's hand table is
  replaced.*

### M7 — The second listen *(week 11)*

The rig of M1 with M3–M5 landed: the soundcheck and restart procedures by
the run sheet, the predictor at its new cost, the Kalman engine, both
rooms. *Pass: the M1 direction holds; the two procedures behave as their
help text says; every M1 artefact is either fixed or in `SUPPORT.md` as a
limit; `docs/real-room.md` session 2.*

### M8 — Measured rooms in the harness *(weeks 11–12)*

- M1's sweeps through `make_rir_fixtures.py --from-wav`, each fixture
  flagged `measured: true`, which `tests/support/rooms.h` honours by not
  applying `band_limited()` (the sweep already contains the loudspeaker and
  the microphone). *Pass: the flag exists and a test asserts a measured
  fixture passes through unfiltered.*
- The AFC claims re-run on them with **filter length as the variable**:
  1024 taps is 21 ms and a real RT60 is hundreds of milliseconds, so every
  row will move; the table says by how much at 1024, 2048 and 4096 taps.
  *Pass: the table is in `claims.md`; any row that collapses becomes a
  finding with an issue.*
- The simulated rooms stay as the regression baselines; the measured ones
  are the product's.

### M9 — Release *(week 13)*

- **M9a — The default engine.** The Kalman core becomes `mutap.afc~`'s
  default (`afc_chain` already defaults to it; `test_pem_afc`'s NLMS
  canaries and the external's `@kalman` default are what change); NLMS
  stays as `@kalman 0`, documented as legacy; the book's chapter 1 follows.
- **M9b — `SUPPORT.md`.** Single-channel only; the calibrated geometries
  and rates (M2c's table); no clock-drift compensation (mic and speaker
  share one interface clock in this product; two-device echo cancellation
  is a known gap); the hardware the counts were taken under (QEMU
  instruction counts, no cycles); the open modes M5d kept.
- **M9c — `v0.2.0`.** Tag, changelog, the claims table snapshot, the
  MuTap-Max package re-pinned at the tag and built in its CI, every
  notebook re-executed whose numbers moved.

## 5. Exit criteria for the quarter

| # | Criterion | Instrument |
|---|---|---|
| 1 | Two real-room sessions recorded, the second with the canceller raising both the runaway and the audible limit in each room | `docs/real-room.md`, the ramp and `howl_criterion.py` |
| 2 | The AFC path counted on three targets, at the product geometry, at ≤ 0.33× its first count | `bench/baselines.json`, the ratchet |
| 3 | Soundcheck and restart procedures with measured declaration times and 0 howl blocks at the product geometry | `guard_runs.h` rows, the help patcher |
| 4 | The guard's sixteen gated rows and both margins measured at the shipping geometry | `test_howl_guard_host.cpp` |
| 5 | `ctest -L gate` under 10 minutes on every push; claims nightly; the claims table generated | CI |
| 6 | A NaN block recovers within one block in every stage | the watchdog test |
| 7 | Measured rooms in the harness, unfiltered, with the filter-length table | `claims.md` |
| 8 | `v0.2.0` tagged with `SUPPORT.md`, the Kalman default, and MuTap-Max pinned to it | the tag |

## 6. Order of work

| week | milestone | the rows that ride with it |
|---|---|---|
| 1 | M0a–f | the README snippet test; `PROTOCOL.md` |
| 1–2 | M1 first listen | sweeps captured; `docs/real-room.md` session 1 |
| 3 | M2a count, M2b geometry, M2c preset | guard constants moved; fingerprints if the preset moves bits |
| 4–6 | M3a cadence, M3b pitch search | paired sweep; fingerprints; karaoke sweep re-run; notebook; book |
| 6–7 | M4a snapshot, M4b external, M4c row | maxref, help, the external's test |
| 8–9 | M5a–d | guard rows re-measured; help patcher; book chapter |
| 9–10 | M6a–d | CI workflows; the rows that move listed |
| 11 | M7 second listen | `docs/real-room.md` session 2 |
| 11–12 | M8 measured rooms | fixtures; the filter-length table |
| 13 | M9 release | tag; `SUPPORT.md`; MuTap-Max package; notebooks |

Every row ends with its numbers in `claims.md` and the HANDOFF working
notes updated; a milestone that misses its pass criterion records the miss
as a finding before the next starts.

## 7. Parked (not this quarter, recorded so their absence is explicit)

- **Hardware.** An MPS3 AN547 or other Cortex-M55 board and a Hexagon
  device with the SDK: cycles per block, the VTCM / L2 layout, explicit
  MVE / HVX intrinsics. Until then every embedded number is an instruction
  count and says so.
- **Clock drift and a bulk delay line for the AEC** (two-device echo
  cancellation). Needs a fractional resampler the tree does not have (no
  SampleRateTap submodule; DspTap carries the FIR substrate only), with
  its own ABI, latency and ratchet rows.
- **The ITU battery at 44.1 and 96 kHz**, the P.501 real-speech
  attachments (the `itu.int` allowlisting is still the blocker; an owner
  and a date go into HANDOFF when the AEC line resumes).
- **Stereo far end.** Scoped out.
- **The Hilbert redesign.** Only with its re-measurement cost stated.
- **Headers to docs.** Moving the measurement tables out of `postfilter.h`
  and `howl_guard.h` keeps the failure modes and contract numbers in the
  header (house style) and is release hygiene for a later quarter; the
  suppressor's two estimator forms collapse when a vectorised float path
  exists to prefer one.
- **A formal freedom-to-operate review.** The literature search is
  recorded as not one.

## 8. Risks, honestly

- **M1 may contradict the simulation.** That is its purpose; a direction
  that fails in a real room reorders M3–M5 and is recorded before anything
  else is built.
- **M2b may choose block 64**, where the canceller costs 32 % of an x86
  core today; M3's x86 target then binds, and if M3a–b do not reach it the
  external ships block 256 with the 10 ms loop documented as unsupported.
- **M3b's search may change the voicing decision** on material brute force
  voices marginally; the paired sweep's range is the number that says
  whether that matters, and brute force stays one flag away.
- **M4's snapshot is a file format** the external writes; its version and
  geometry header are a contract from the first release.
- **M5's procedures change the product's UX**: a guard that will not open
  without a soundcheck or a cap is safer and less convenient, and users who
  are not shown why will turn it off. The help patcher and the book carry
  the procedure in the same change.
- **M6b moves rows out of the gate** that today block a push; a regression
  in a `claims` row surfaces the next morning, not at the push. The
  short-probe twins in the gate are the mitigation and are listed by name.
- **Thresholds move** under M2b, M3 and M8; every move is a `claims.md`
  row with a before and after, never a silent re-record.
