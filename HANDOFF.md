# MuTap — Project Handoff

> Context brief for a Claude Code session. Captures decisions and technical direction so the next session can start working without re-deriving anything. Can be adapted into a `CLAUDE.md` for persistent project context.
>
> **Rev 5** — planning revision, no code. Rev 4's effort is fully merged
> (both repos); the next effort is chosen and staged: **ITU compliance
> for the AEC, with margin** — prove, via a requirements matrix, a
> calibrated ITU-signal layer, a new residual-echo post-filter, a
> compliance test suite and a proof notebook, that the AEC chain passes
> the applicable clauses of G.167 / P.340 / a G.168-adapted battery /
> P.1100-P.1120 (automotive) with asserted headroom. Scope decisions
> settled with Tim; see "The next effort (Rev 5)". BLOCKER for Stage 0:
> `itu.int` must be allowlisted in the dev environment's network policy
> (Tim's action) so the rec texts can be read.
>
> **Rev 4** — planning revision, no code. The next effort is chosen and
> staged: **AEC objects + the echo-cancellation book chapter** (former
> "What's next" items 5+6 — chosen because items 1–2 need Tim in a real
> room and 4 needs the Hexagon SDK). Two open decisions are settled: the
> feedback external is renamed **`mutap.afc~`** (was `mutap.defeed~`, a
> placeholder) and the new echo canceller is **`mutap.aec~`** — an
> AFC/AEC acronym pair. See "The next effort (Rev 4)" for the
> three-stage plan.
>
> **Rev 3** — the plan below has been EXECUTED: M0 through M6 and the v2
> Kalman upgrade are built, measured, tested and merged. This revision adds
> the "State of the repo" and "Working notes" sections (start there), marks
> the milestone list as history, and re-ranks what's next. The algorithm
> description, portability architecture, Hexagon specifics and paper list
> remain the reference they always were.
>
> **Rev 2** — refined against the actual `tap` ecosystem on disk (AmbiTap, AmbiTap-Max, SampleRateTap). Two former open decisions settled by sibling-repo convention, the single-repo assumption corrected to the ecosystem's core/-Max split, milestones restructured into individually testable stages.

---

## State of the repo (Rev 3)

Everything below exists, is regression-tested, and is green in CI
(Linux GCC/Clang, macOS, Windows, ASan+UBSan, clang-format/tidy,
Cortex-M55 under QEMU, Hexagon under QEMU). The README's Status section
carries the measured numbers; this is the map:

- `include/mutap/` — `fft.h` (Ooura wrapper), `fdaf.h` (partitioned-block
  NLMS core + the M4 control stack: IPC, IPC-scaled stepping, transient
  gate, variable regularization), `fd_kalman.h` (**the v2 Kalman core** —
  no step size, no IPC; per-bin state uncertainty + near-end PSD replace
  the control stack; opt-in `transient_floor_ratio` trades tonal ASG for
  burst hardening), `lpc.h` (ridge-guarded Levinson + the two pluggable
  near-end models: `speech_predictor`, `warped_lpc_predictor`),
  `pem_afc.h` (the FDAF-PEM-AFROW wrapper, templated over BOTH the
  predictor and the adaptive core).
- `tests/` — 97 tests; `tests/support/closed_loop.h` is the closed-loop
  simulator + MSG/ASG bisection metrics (a deliverable in its own right);
  `tests/support/echo_scenario.h` is its open-loop AEC counterpart (echo
  path + double-talk injection, observable ERLE + true residual-echo
  suppression), exercised by `test_aec.cpp`.
- `tools/capi/` + `notebooks/afc_demo.ipynb` — the C ABI and the executed
  demo notebook (7 sections, every figure measured). The notebook is a
  build product of `tools/notebook/build_afc_demo.py` — edit the script,
  never the .ipynb.
- `platform/` + `cmake/arm-cortex-m55-mps3.cmake` — the bare-metal M55
  rig; `cmake/hexagon-linux-musl.cmake` — the Hexagon cross build.
- `book/` — "Quieting the Loop" (mdBook, AmbiTap-book conventions): one
  chapter so far, the user-facing guide to the canceller and every knob.
- Sibling repo `tap/MuTap-Max` — two externals: `mutap.afc~` (the
  feedback canceller, renamed from the `mutap.defeed~` placeholder in
  Rev 4's Stage 2) and `mutap.aec~` (the open-loop echo canceller), both
  with attributes `block`, `mu`, `adapt`, `gate`, `warp`, `kalman`;
  engine is a `std::variant` over {speech, warped} × {NLMS, Kalman},
  xtc~-pattern lock-free rebuild handoff.

## Working notes for the next session (hard-won; read before touching)

1. **Every test threshold is a measured number.** The workflow that built
   this repo: run the experiment in a scratch harness first, write the
   measured value into a comment next to the assertion, set the threshold
   with margin. Keep doing this — reviewers (and CI archaeology) depend
   on those comments.
2. **Closed-loop trajectories are chaotic; assert directions, not
   magnitudes.** Platform libm/FMA differences (AppleClang arm64
   especially) get amplified arbitrarily by the loop. Three incidents to
   learn from: the λ=0 warped/plain identity is algebraic but not bitwise
   (FMA contraction → assert 1e-9, not 4 ulp); a naive-loop blowup peak
   measured 18k on Linux and 34k on macOS (assert survival/recovery and
   floored containment, never the chaotic peak); single-seed ASG numbers
   move by dB between platforms (assert medians across seeds, bisected
   stability directions, floors with several dB of margin).
3. **One room is not an evaluation.** The warped predictor's first cut
   was validated on one simulated room and shipped claims that a
   five-room sweep demolished (one room in five DESTABILIZED at the old
   defaults). The fix was structural (IPC pairing / the Kalman core), not
   tuning. Any new closed-loop claim: sweep rooms from BOTH generator
   families (the C++ `std::mt19937` rooms in the tests and the notebook's
   numpy rooms) before writing the number down.
4. **The emulated-target test selection lives in TWO places that must be
   kept in sync by hand**: the baked gtest filter in
   `tests/bare_metal_main.cpp` (Cortex-M55, no argv on target) and the
   `TEST_FILTER` in `tests/CMakeLists.txt` (Hexagon and any future hosted
   cross target). Both mirror the same policy: float typed suites,
   LP/conditioning, validation/contract tests, float closed-loop
   scenarios; the double-typed adaptive suites stay host-only (the full
   suite ran once on the Hexagon ISA — 44 min of TCG — and passed; the
   per-push selection takes ~8).
5. **Hexagon CI quirks**: the toolchain downloads from Codelinaro's
   artifactory (the quic/toolchain_for_hexagon GitHub releases carry NO
   assets — the release notes just link out); the archive's inner
   directory name varies, so the workflow locates `clang++` and derives
   the root; `-static` does NOT imply `--eh-frame-hdr` and without it the
   first `throw` aborts (that cost one CI round to find).
6. **The submodule dance**: MuTap merges are rebase-merges, so every
   merge orphans the branch SHAs. After any MuTap PR merges, re-pin
   `MuTap-Max/submodules/MuTap` to the new main tip before (or as part
   of) the next MuTap-Max merge — a dangling gitlink breaks recursive
   clones once branches are cleaned up.
7. **min-api pin gotcha** (MuTap-Max): the pinned min-api has no scalar
   `send()` path on queue-backed outlets — send a pre-allocated `atoms`
   lvalue (see `m_ipc_atoms` in the external).
8. **The book's honesty rule** (inherited from the AmbiTap book): no
   number in `book/` that the test suite or notebook doesn't measure, no
   patch or attribute described that doesn't exist. If a chapter needs a
   feature, build the feature in the same change.

## What's next (ranked)

1. **In-Max listening.** Everything is code-complete and
   simulation-verified; nothing has been heard in a real room through a
   real mic→speaker loop. The help patchers are the checklist: for
   `mutap.afc~`, added stable gain by ear, IPC metering, engine/gate/
   warp A/B; for `mutap.aec~`, the in-patch simulated-room demo plus a
   real call. Findings feed 2.
2. **Decide the default engine.** The Kalman core beats the tuned NLMS
   stack in every simulated scenario; after real-room listening, decide
   whether `@kalman` becomes the external's default (and whether the M4
   knobs get de-emphasized in docs).
3. **M55 performance work** — CMSIS-DSP/Helium mapping of the FDAF hot
   path and SampleRateTap-style instruction-count ratchets in CI.
4. **Hexagon performance work** — needs the proprietary SDK + hardware:
   VTCM residency, L2 streaming layout, HVX mapping (the specifics
   section below still holds).
5. **AEC objects** — *DONE (Rev 4's "next effort", all three stages):
   `mutap.aec~`, the echo scenario harness + `test_aec.cpp`, notebook
   section 8, and the echo chapter. See the section below.*
6. **Book chapters** — ~~echo cancellation~~ (done, Stage 3), and the
   embedded-targets story (still open).
7. **RIR fixtures** — infrastructure DONE (`tools/fixtures/
   make_rir_fixtures.py`, `tests/fixtures/rir_*.h`, `test_rir_fixtures.cpp`):
   three physically-modeled image-source rooms are committed baselines.
   Still open (below): adding measured rooms — one command per WAV.

## The next effort (Rev 4): AEC objects + echo chapter

Items 5+6 above, planned in detail. The premise that makes this cheap:
`pem_afc::process_block(u, y, e)` already IS the AEC signature (reference
in, mic in, cleaned signal out), and the paper the core implements
(Gil-Cacho et al. 2014) is titled a framework for *double-talk-robust
acoustic echo cancellation* — PEM prewhitening is the AEC double-talk
story, replacing a classical double-talk detector. No new DSP; the work
is validation, an external, and docs. Three PRs, in dependency order:

**Stage 1 — MuTap: open-loop AEC validation layer.** *DONE — measured
numbers in the README Status section and the test_aec.cpp header; notebook
section 8 is the AEC demo. One finding worth carrying forward: in pure
single-talk on colored far-end, the naive NLMS posts the best observable
ERLE (~44 dB, excitation-weighted) while PEM measures ~20 dB with a far
deeper uniform estimate — the double-talk segment then destroys the naive
filter (misalignment goes positive) where PEM/Kalman hold. That trade is
the AEC default-engine story for the chapter.*
- An echo scenario harness in `tests/support/` — much simpler than the
  closed-loop simulator (no loop closure): far-end source → true echo
  path F (reuse the committed RIR fixtures) → mic, plus near-end
  double-talk injection.
- ERLE as a shared metric (today it lives ad hoc inside
  `test_fdaf.cpp`), alongside misalignment.
- `test_aec.cpp`: single-talk convergence on white and colored far-end;
  double-talk scenarios where the naive core biases and PEM keeps
  converging (the open-loop analogue of the M2 baseline); both engines
  × both predictors. Per the working notes: thresholds measured in a
  scratch harness first, medians across seeds, rooms from BOTH
  generator families.
- A notebook section (via `tools/notebook/build_afc_demo.py`, never the
  .ipynb) demonstrating AEC, so the chapter has measured figures.

**Stage 2 — MuTap-Max: `mutap.aec~` + the rename.** *DONE (on the
MuTap-Max branch): the rename, the new external, the in-patch help
patcher, and the overdue submodule re-pin. Re-pin AGAIN after this
repo's Stage 1/3 changes merge (working note 6).*
- The coordinated rename `mutap.defeed~` → `mutap.afc~`: project
  folder, class, maxref, help patcher, README references. (Book
  chapter 1's references update in Stage 3, after the rename exists —
  honesty rule.)
- New `mutap.aec~` cloned from the afc~ pattern: same engine
  `std::variant` matrix, same lock-free rebuild handoff, same
  attributes (`block`, `mu`, `adapt`, `gate`, `warp`, `kalman`), same
  pre-allocated-atoms IPC outlet. What changes is semantics and docs:
  inlet 2 is the far-end signal being sent to the speaker, and the
  help patcher demonstrates echo removal + double-talk survival fully
  in-patch (a delay/filter stands in for the room — unlike afc~, this
  one is demonstrable without a physical mic→speaker loop).
- Fold in the overdue submodule re-pin (working note 6): the pin is
  stale at `813e503` (pre-RIR-fixtures); re-pin again after Stage 1
  merges.

**Stage 3 — MuTap: book chapter "Echo cancellation".** *DONE:
`book/src/echo-cancellation.md` (every number from test_aec.cpp /
notebook section 8), plus the MuTap-side rename sweep (chapter 1,
introduction, README, notebook). The whole effort is complete; what
remains of the AEC line is real-call listening, which folds into
"What's next" item 1.*

## The next effort (Rev 5): ITU compliance for the AEC, with margin

Goal (Tim's directive): the AEC passes the applicable ITU specifications,
preferably with LARGE margin, proven by tests/harness and notebooks. What
this repo can prove is **algorithmic compliance with margin**: the AEC
chain, driven by the standardized test signals through simulated and
physically-modeled echo paths, meets every testable clause — each margin
regression-tested, plus a notebook showing requirement vs measured vs
margin per clause. Device-level certification (real transducers, HATS
rigs, TCLw of a physical terminal) inherently needs a lab; this effort
builds the evidence package that makes the lab visit boring.

Scope decisions settled with Tim (2026-07-16):
- **Normative anchors**: P.340 (hands-free terminal: TCLw, duplex
  categories) + G.167 (the AEC-specific rec) + a G.168-adapted battery
  (its tests are for line echo; we adapt them to acoustic paths as a
  supplementary suite) + **the automotive series P.1100/P.1110/P.1120**.
  The automotive recs cover the whole terminal chain — the matrix must
  mark which clauses concern the AEC vs out-of-scope terminal items
  (frequency response, noise reduction), and they bring two new
  simulation axes: car-cabin echo paths and driving-noise conditions.
- **Post-filter approved**: linear cancellation measures ~20 dB; the
  single-talk attenuation targets are 40+ dB. Stage 2 builds the
  residual-echo suppressor + matched comfort noise as a new pluggable
  MuTap stage, plus an integrated AEC-chain type (the specs measure the
  chain, not the filter).
- **Spec access**: itu.int gets allowlisted in the dev environment's
  network policy (BLOCKER, Tim's action — environment settings on
  claude.ai/code). The rec PDFs are free downloads; nothing gets
  committed to the repo but our own distilled matrix. P.501 signals are
  GENERATED from their algorithmic descriptions, never redistributed.
- **Margin policy (proposed, confirm in Stage 0)**: every requirement
  asserted with >= 6 dB (level clauses) or >= 2x (time clauses) headroom,
  so margin EROSION fails CI, not just outright failure.

**Stage 0 — Requirements matrix.** *DONE — `docs/itu-compliance.md`.
Editions read: P.1110/P.1100 (03/2017), P.1120 (10/2025), P.340
(05/2000), P.501 (04/2025), G.168 (04/2015), G.167 (03/1993, withdrawn),
G.131 (11/2003). Findings that shaped the matrix: (1) the AUTOMOTIVE
series is the operative modern AEC battery (concrete masks: TCL >= 46 dB,
residual < -58 dBm0(A), ERL >= 40 dB by 1.2 s, spectral masks, switching
dynamics, comfort-noise tolerances); (2) P.1120 (2025) normatively cites
P.340 (2000) — the edition in hand — and restates all category tables,
so the P.340-2019 procurement gap is moot for the operative rows;
(3) P.340/G.167 explicitly permit simulated echo paths and P.1110
clause 8 defines digital insertion interfaces for exactly this kind of
testing; (4) targets chosen: P.340 CATEGORY 1 (full duplex) on all three
double-talk tables, WB+SWB/FB primary bandwidths at 48 kHz; (5) the
P.1110 Annex E stability sweep (far-end ERL 50 -> 0 dB, no howling) is a
closed-loop test — AFC home turf; (6) real-speech gap: three core tests
use ITU attachment WAVs (P.501 7.3.2/7.3.3/7.3.5) — Stage 1 attempts
download for local git-ignored test use, else documented synthetic
stand-ins marked method-equivalent; (7) P.502 still unobtained —
analysis methods reconstructed from the P.340/P.111x test descriptions.
Rec PDFs live in the session scratchpad only (never committed).*

**Stage 1 — Calibrated ITU signal layer** (`tests/support/`). *DONE —
`itu_levels.h` (dBov/dBm0/dBPa conventions, prewarped-bilinear
A-weighting, 35/5 ms integrators, sliding peak, P.56-style active-level
meter) + `itu_signals.h` (CSS single/double-talk with the P.501
Table 7-1/7-2 voiced segments transcribed from the PDF, 8192-pt PN
adaptive-systems variant, shaping + band-limit filters, AM-FM
orthogonal pair + comb analysis, activation sequence, Hoth + synthetic
driving noise, moving-reflector time-variant path) +
`tests/fixtures/rir_cabin.h` (image-source cabin, RT60 66.6 ms) +
`test_itu_signals.cpp` (12 tests, measured-first). KEY DECISION made in
stage: the suite runs natively at 44.1 kHz (CSS is sample-exact only
there; matrix updated). Measured instrument floor worth knowing: the
AM-FM comb separation is 94.3 dB at the spec's exact band edges — and
collapses to 19 dB if any guard band is added (recorded in the test).
Existing fixtures verified bit-reproducible with pyroomacoustics 0.10.1
before generating the cabin (--only flag added to the generator). Still
open from this stage: the ITU real-speech attachment WAVs
(tests/data/itu/, git-ignored) for the three signal-exact rows.
AMENDED (Tim's directive): 48 kHz and 16 kHz are REQUIRED operating
rates (44.1 kHz stays the P.501-native reference) — the NOTE 2
resampler was built (ripple <= 0.014 dB, alias rejection 101 dB), the
formula generators take fs directly, and make_css_at() emits
sample-exact periods at every required rate; matrix policy updated.*

**Stage 2 — Residual-echo post-filter + comfort noise.** The big DSP
item. New header (working name `mutap/postfilter.h`): coherence-based
residual-echo estimate, spectral suppression, comfort noise matched to
the near-end noise floor; plus the integrated chain type. House
workflow: scratch-measure first, thresholds with margin, rooms from both
generator families. RT contract as everywhere.
*DONE — `mutap/postfilter.h` (`residual_suppressor` + `aec_chain`) +
`tests/test_postfilter.cpp` (12 tests, measured-first; every Stage 2
deliverable inside its margin target — the measured table and the four
design decisions the numbers forced live in the matrix's "Stage 2
delivered" section). The headline discovery: the AEC chain must NOT
use PEM — open-loop AEC has an exogenous far end and the predictor
refit floors misalignment near -20 dB where the raw FD-Kalman core
(transition 0.9998, initial uncertainty 10 — the measured AEC sweet
spot) reaches -75 dBm0(A) bare with double-talk immunity from its own
noise-PSD tracker. The suppressor correlates the MIC, not E, against
the echo estimate (orthogonality principle: coh(E,Yhat) saturates at
0.34 while adapting), and takes suppression DEPTH from a
coherence-gated leakage estimate so double-talk transparency is the
shape of the rule, not a detector. Two instrument fixes en route:
A-weighting poles above Nyquist now prewarp-clamped (16 kHz tests went
NaN), and pem_afc grew echo_estimate_block(). mutap.h includes the new
header; emulated-target selections unchanged (double-only ITU suites
stay host-side).*

**Stage 3 — Compliance suite** (`tests/test_itu_*.cpp`). One gtest per
matrix row asserting requirement + margin policy. Swept across fixture
rooms + synthetic rooms + car cabins where the clause demands.
*TIER A DONE — `tests/support/itu_chain.h` (the pinned compliance
chain) + `test_itu_echo.cpp` / `test_itu_doubletalk.cpp` /
`test_itu_dynamics.cpp`: every Tier A row at BOTH required rates, one
measured table in the matrix's "Stage 3 delivered" section. Every ITU
REQUIREMENT met at both rates; our own half-margin targets miss on a
handful of 16 kHz rows (early convergence, hangover, minimum-statistics
bias — 16 ms blocks mean fewer adaptation steps per unit time), each a
documented regression gate. Chain elements the measurements forced:
the initial receive guard (switched < 14 dB send loss until convergence
certifies — the convergence-in-noise mask is unmeetable without it),
release snap + floor-initialized gains, the coherence-gated low-band
suppression cap with sustained certification (P.340 transfer bound),
and one REJECTED design recorded in postfilter.h (a noise-floor gain
bound that self-references the residual's pause minima in near-
silence). Geometry discovery worth keeping: the partitioned Kalman's
convergence collapses at block 128 / 16 kHz (8.9 vs 22.5 dB ERL by
600 ms at block 256) — open investigation item at the time, worked
around by pinning block 256 at both rates; since diagnosed and closed
(see the BLOCK-128 NOTCH note below). STILL OPEN (Stage 3b): the Tier B
G.168-adapted battery and the G.167 historical row.*
*STAGE 3b DONE — `tests/test_g168.cpp`: all twelve adapted rows at both
required rates + the G.167 historical row; measured table and the one
documented deviation in the matrix's "Stage 3b delivered" section.
CORE FOLLOW-UP FILED: fd_kalman uncertainty re-inflation on sustained
innovation excess — after an abrupt path change the converged filter's
small state uncertainty makes deep re-convergence take ~7 s (48 kHz) /
>10 s (16 kHz) where initial convergence (P(0) = 10) reaches Figure 9
in 1.4 s; would also lift the TimeVariantPath and 16 kHz hangover
margins.*
*FOLLOW-UP DELIVERED (Stage 6) — as the RE-CONVERGENCE RESCUE:
aec_chain watches the suppressor's echo-explained ratio and, on
sustained OVER-explanation (the one mismatch signal double talk cannot
fake — a near end only ADDS mic power), calls the core's new
reinflate_uncertainty(): one bounded lift of P back to P(0), weights
kept, ~2 s cooldown. Swap rows now measure 46/49 dB combined loss in
[1,2] s and -96/-123 dBm0 deep steadies (the 16 kHz ">10 s" case
closes); every other battery row bit-identical. The in-core detector
family (momentum of the normalized update direction, three gate
variants) was measured and REJECTED — sinusoid-pair near ends (P.501
AM-FM combs sharing analysis bins) defeat per-bin correlation
statistics; failures recorded in fd_kalman.h and the rescue's config
comment. NEW CORE FOLLOW-UP FILED: a dual-path/shadow comparator
(classical G.168-device architecture — run a cheap fast filter in
parallel and compare actual cancellation) is the only known-robust way
to close the remaining LOUDER-direction deep phase, which
over-explanation cannot see; it would also lift the TimeVariantPath
and 16 kHz hangover margins the original filing named.*
*SHADOW COMPARATOR DELIVERED — as the rescue's second trigger in
aec_chain: a 2-partition fast shadow (transition 0.999, ~25 % extra
cost, preset-enabled, shadow_partitions 0 disables) fires the same
one-shot lift when it out-cancels the main by 3 dB sustained 0.3 s of
receive-active blocks. Zero false fires measured across the DT/noise
batteries (performance comparison is DT-immune by construction — the
statistic the correlation family could never get). Louder-direction
swaps now recover at cold-start speed at both rates (fires 0.8/1.8 s;
deep steady -47 -> -79/-84 dBm0); swings 16 kHz and three-phase 48 kHz
read windows lock the wins (-57.5 / -65.4), the mirror windows catch
partial recovery. TVP and 16 kHz hangover margins unchanged (slow
drift and post-DT drag do not make the shadow win — those margins
remain future work, likely needing the noise-tracker semantics
revisit).*
*BLOCK-128 NOTCH CLOSED — the Stage 3 anomaly diagnosed (s8 scratch
series) and fixed. It was never "16 kHz" or "block 128": the notch
follows an ~8 ms hop (it reproduces at 32 kHz / block 256, vanishes at
32 kHz / block 128, and needs the single-talk CSS — white noise, PN
alone, voiced alone and the double-talk CSS's other pitch are all
clean). Mechanism, in two coupled parts: (1) at a 16 ms analysis
window the CSS voiced segment's 329 Hz comb is resolved, the
excitation is rank-deficient, and the diagonal Kalman splits the
minimum-norm solution uniformly across partitions — misalignment lands
ABOVE 0 dB, worse than an empty filter for the broadband PN that
follows; (2) the P decrement 1 - c g|U|^2 is innovation-independent,
so the repeating regressor burns uncertainty it never earned, and
within ~10 blocks the noise tracker absorbs the unmodeled echo: the
same self-lock the re-convergence rescue treats, but at cold start
(the chain's shadow trigger does fire at ~0.6 s and helps the tail,
+10 dB by 3 s, but each voiced segment re-poisons under the 2 s
cooldown). Fix, two core knobs, both default OFF (certified
geometries bit-identical, verified on the full dump): an
excitation-novelty covariance discount (P decrement scaled by
1 - coherence of successive input spectra, floor 0.1) and a decaying
per-partition P(0) prior (r = 0.5; encodes "echo paths decay", the
prior the min-norm split follows on rank-deficient input; measured
trade — 32 ms of dead bulk delay converges slower, 21.1 -> 16.8 dB by
600 ms, so keep it off with uncompensated delay). aec_chain_preset
enables both only inside the prone 6..12 ms hop band. Measured at
16 kHz / block 128: 8.9 -> 15.5 dB ERL by 600 ms, 27.9 -> 46.3 by
3 s (chain with shadow on top: 17.4 / 51.3); AM-FM DT, convergence in
noise and the 30 s tone all same-or-better; regression-gated in
test_postfilter.cpp (Block128NotchClosed, PresetNoveltyPolicy).*

**Stage 4 — Proof notebook.** `tools/notebook/build_itu_compliance.py`
-> `notebooks/itu_compliance.ipynb`: one section per requirement group,
requirement/measured/margin table per section, convergence curves vs the
specs' time masks, double-talk timelines vs the P.340 windows.
*DONE — the notebook's first cell compiles `tools/notebook/itu_dump.cpp`
(option `MUTAP_BUILD_ITU_DUMP`) and re-measures the whole battery live
(~6 min): the dump includes the test suite's own itu_chain.h machinery,
so every notebook number is the number the assertions gate. Nine
sections: steady state, Tier A convergence vs masks, G.168 Figures
9/11 + the re-convergence deviation (honest figure), double talk vs
P.340, comfort noise/pumping, path dynamics, stability/delay, G.167
run-and-reported. CSS generation (NOTE 2 resampler from 44.1 kHz
native) dominates the dump's runtime — memoized in the dump only,
byte-identical output verified; the signal layer is untouched.*

**Stage 5 — Externals + docs.** Post-filter attribute on `mutap.aec~`,
C ABI extension for the notebook, maxref/help updates, book-chapter
section, README compliance claims with margins. (Submodule dance,
working note 6, as always.)
*MAX HALF DONE — `mutap.aec~ @postfilter` engages the certified chain
(raw Kalman + suppressor + comfort noise + receive guard) with the
compliance preset's time-constant rescaling replicated for arbitrary
(block, sample rate); maxref/help/README updated. CORE FOLLOW-UP FILED:
the pinned compliance preset (tests/support/itu_chain.h chain_config)
belongs in the library proper — e.g. a `mutap::aec_chain_preset(block,
fs)` factory — so the Max external and any future consumer stop
duplicating the scaling rule; the external's copy (mutap.aec_tilde.cpp
make_chain_config) must be kept in lockstep until then. Note: Stage 4's
notebook ended up NOT needing the C ABI (its dump harness includes the
test machinery directly), so the "C ABI extension" item is now about
FFI consumers generally — scope it when the MuTap half lands.*
*CORE HALF DONE — `mutap::aec_chain_preset<Sample>(block, partitions,
fs)` lives in postfilter.h (the follow-up above, resolved: the preset
generalizes the per-rate floor-bias calibration by interpolating
between the two measured points, clamped to the measured neighborhood);
tests/support/itu_chain.h chain_config() is now a one-line call into it,
so any preset change lands as a failing gate, never silent drift — the
full suite re-verified green after the refactor. C ABI grew the
`mutap_aec_*` family (create-from-preset / process / echo_explained /
converged / clone; probed via a C driver). README gained the
compliance-claims table with margins; the book's echo chapter gained
"The last 30 dB: @postfilter" plus the two new knobs. REMAINING in
MuTap-Max, after this merges: re-pin the submodule and switch the
external's make_chain_config to call the library preset.*

---

## What this is

**MuTap** is a collection of portable, reusable audio "cleaning" tools built around adaptive filtering. The first tool targets **acoustic feedback (howling) suppression** using a PEM-AFROW-based adaptive feedback canceller. The defining constraint is **portability**: the same DSP core must run on Max/MSP (desktop float), ARM Cortex-M55 (Helium), and Qualcomm Hexagon (HVX, floating-point). Because the Hexagon target has float HVX, the plan is a single float32 core across all three — no fixed-point port.

The name is load-bearing, not decorative: **μ ("mu") is the LMS/NLMS step-size that adapts the filter taps** — so "MuTap" literally describes what the library does, on top of the handle + FIR-tap + Zen-koan readings.

### Two repos, not one

Both already exist (empty) under the `tap` org, mirroring the AmbiTap split:

- **`tap/MuTap`** — the portable core: header-only C++20 library (`include/mutap/`, namespace `mutap`), plus tests, benchmarks, notebooks, and the offline/closed-loop validation harness. This repo is the golden model and the only place DSP lives.
- **`tap/MuTap-Max`** — the Max/MSP package: a Cycling '74 Min-DevKit repo with MuTap as a git submodule (`submodules/MuTap`), one external per folder under `source/projects/`, thin wrappers over `mutap::dsp` processors. Follows AmbiTap-Max exactly (`min-api` in `source/`, `package-info.json`, `help/`, `patchers/`).

The original plan's "platform wrappers live in subdirectory (b) of one repo" is superseded by this split.

### Settled by ecosystem convention (formerly open decisions)

- **License: MIT.** Every sibling is MIT ("Copyright (c) … Timothy Place" / "… contributors").
- **Core language: C++20, header-only.** Matches AmbiTap and SampleRateTap (CMake ≥ 3.24, GCC 11+/Clang 14+/MSVC 19.30+), consumable via `add_subdirectory`/FetchContent as `MuTap::MuTap`. Small vendored `.c`/`.cpp` files as tiny static targets are acceptable (AmbiTap does this for Ooura: `AmbiTap::fft`).
- **House style:** copy `.clang-format` and `.clang-tidy` verbatim from a sibling and adapt `STYLE.md` ("Tap House Rules") — `snake_case` types/functions/variables, `PascalCase` template parameters only, `m_` members. CI enforces both.
- **Real-time contract** (SampleRateTap precedent): the audio-path entry points are `noexcept`, lock-free, and allocation-free; all allocation and any filter design happen at construction. Control-rate/parameter work that needs heavy recompute goes through AmbiTap's patterns (`rt_published` wait-free publication, `async_rebuilder` worker) rather than locks.

---

## Repository metadata (ready to paste)

**`tap/MuTap` description:**
```
Portable adaptive filters for audio cleaning: acoustic feedback (howling) suppression and echo cancellation — header-only C++20 core, from desktop to embedded DSP.
```

**`tap/MuTap` topics:**
```
audio  dsp  adaptive-filter  feedback-cancellation  howling-suppression
echo-cancellation  acoustic-echo-cancellation  cortex-m  hexagon  cpp20
```

**`tap/MuTap-Max` description:**
```
Max/MSP externals for adaptive audio cleaning (feedback/howling suppression), thin wrappers over the MuTap library. Min-DevKit package.
```

**`tap/MuTap-Max` topics:**
```
maxmsp  max-external  audio  dsp  feedback-cancellation  howling-suppression  min-devkit
```

---

## Scope: two distinct problem families

These share math but are **different problems** and should be built as separate stages, not one filter:

- **AEC (acoustic echo cancellation)** — a *clean reference* exists (the far-end signal sent to the speaker). Open-loop, well-conditioned, unbiased estimate. Not the current focus.
- **AFC (acoustic feedback cancellation / howling suppression)** — closed loop, *no clean reference*; the loudspeaker plays a processed version of the mic signal itself. The loudspeaker signal is correlated with the near-end source, which **biases** a naive adaptive estimate. **This is what MuTap tackles first.**

---

## Target algorithm: FDAF-PEM-AFROW

**PEM-AFROW** = Prediction-Error-Method Adaptive Filtering using ROW operations (Rombouts, van Waterschoot, Moonen, *JAES* 55(11):955–966, 2007). Designed for **long room acoustic paths**. Uses batch/frame-based linear prediction and introduces **no model approximations** — the prewhitening is expressed purely as row operations on the loudspeaker signal data matrix (hence the name). This is the distinction from Spriet's earlier recursive PEM-AF, which only holds for short feedback paths.

### Core idea — solving the closed-loop bias

Model the near-end source `v(n)` as the output of a shaping filter `1/A(q)`. Estimate the inverse `A_hat(q)` by linear prediction of the feedback-compensated signal `e(n)` (the best available proxy for `v`). **Prefilter both the adaptive filter's input (loudspeaker signal) and its desired signal by `A_hat(q)` before the adaptive update.** Whitening the near-end out of the regressor makes the feedback-path estimate (approximately) unbiased — the FDAF formulation relates this rigorously to the BLUE of the feedback path.

### Signal flow (closed loop)

```
  y(n) = v(n) + F(q)·u(n)          mic = near-end source + true feedback path * loudspeaker
  e(n) = y(n) - F_hat(q)·u(n)      feedback-compensated / error signal
  u(n) = G(q)·e(n)                 forward path: gain K, delay d, + any processing

  PEM decorrelation:
    A_hat(q)  <- linear prediction on e(n)      (near-end inverse model / whitening filter)
    u'(n) = A_hat(q)·u(n),  e'(n) = A_hat(q)·e(n)
    adaptive update (NLMS/RLS) runs on the prewhitened pair (u', e')  ->  unbiased F_hat
```

### Which variant to build

**Build FDAF-PEM-AFROW** (frequency-domain, partitioned-block), not the time-domain original. Rationale:
- ~2 orders of magnitude cheaper than the time-domain versions, and outperforms them.
- Partitioned-block frequency domain is exactly the portable common denominator across Max / M55 / Hexagon.
- Low delay (matters for live Max use).
- Provides a built-in double-talk indicator — the **IPC (instantaneous pseudo-correlation)** measure — for free.

**Upgrade path (SOTA):** PEM-based frequency-domain Kalman filter (Bernardi, van Waterschoot, Wouters, Moonen) — adds fast convergence + good tracking via a frequency-domain Kalman filter, low complexity, low delay via partitioned blocks. Treat as v2 once FDAF-PEM-AFROW works.

### The key design decision: the near-end model

This is the main tuning knob and it depends on program material:
- **Speech:** cascade of a short-term predictor (removes formant coloring) and a long-term / pitch predictor (removes voiced periodicity). This is the original derivation.
- **Music / tonal (PA, instruments):** a plain all-pole model is a poor fit — sinusoids-in-noise want a **pole-zero** representation, so all-pole needs a very high (expensive) order. Prefer a **frequency-warped all-pole** model or a moderate-order **pole-zero** model.

Make the near-end predictor a **pluggable component** from the start. **Default sequencing: implement the speech cascade first** — it is the model the papers derive and report results for, so it's the only way to validate the implementation against published convergence/bias behavior. The warped/pole-zero music predictor is the second plug-in, not a fork of the core. (If the primary live use case is music/PA, that reprioritizes the *tuning and demo* work, not this implementation order.)

---

## Hard parts / watch-list

1. **LP / whitening is the most numerically delicate block.** The autocorrelation → Levinson-Durbin → reflection-coefficient → prewhitening chain inverts a spectrum and can be ill-conditioned, so it's the part most worth validating against the golden model. In float across all three targets this is a **conditioning/stability** concern, not a quantization one — guard it with regularization and sane order limits rather than the Q-format headroom analysis a fixed-point target would have demanded. Give this block its own unit tests: near-singular autocorrelation fixtures, silence, DC, pure tones, order sweeps.
2. **Adaptation control.** Freeze / slow adaptation when the loop isn't informative (strong near-end, transients, silence). Use IPC as the trigger. Gets a demo to survive a real room.
3. **Regularization.** The prewhitened system can be ill-conditioned; use a variable-regularization scheme, not a fixed epsilon.
4. **Forward-path delay ↔ maximum stable gain.** Reason about added stable gain explicitly rather than discovering it empirically — and *measure* it in the closed-loop simulator (see milestones): sweep forward gain K to the howling onset with and without the canceller; the difference is the deliverable metric (ASG).
5. **The closed loop is the test, not an afterthought.** An offline WAV-in/WAV-out harness cannot exercise feedback: the system under test *creates* its own input. The validation harness must simulate the full loop (true path F from a real/synthetic RIR, forward path G with gain and delay, near-end injection) sample-block by sample-block.

---

## Portability architecture (the crux)

| Target | Numeric | Notes |
|---|---|---|
| **Max/MSP** | float64 / float32 | Desktop, unconstrained. **This is the golden model / correctness oracle.** Ships via `tap/MuTap-Max` (Min-DevKit, like AmbiTap-Max). |
| **ARM Cortex-M55** | float32 | Armv8.1-M + Helium (MVE-F float incl. f16). CMSIS-DSP provides Helium-vectorized `arm_lms_norm`, biquad cascades, FIR/IIR, and `arm_rfft_fast_f32`. Staying in float32 also sidesteps the documented `arm_rfft_q31` hard-fault issues under MVE. **Reuse SampleRateTap's embedded rig:** its `platform/` already carries Armv8-M startup + linker scripts for MPS2 AN505 (Cortex-M33) and **MPS3 AN547 (Cortex-M55)**, run under QEMU in CI with instruction-count ratchets (see SampleRateTap `docs/PERFORMANCE.md`, `docs/HARDWARE_TESTING.md`). Copy that pattern instead of inventing one. |
| **Qualcomm Hexagon** | float32 (HVX float) | **Target SoC: QCS8550 / SM8550 (Snapdragon 8 Gen 2), Hexagon V73-class cDSP.** HVX with vector float (fp32/fp16), 128-byte vectors, ~8 MB TCM scratchpad. Runs the same float32 core as Max/M55; C/C++ + intrinsics under LLVM via the Hexagon SDK, offloaded from the CPU over FastRPC. The real work here is **data layout, not numerics** (see below). |

**Design principle:** **one float32 core, three targets.** Since Hexagon has float HVX, there is no fixed-point port to maintain — validate once in float on Max (oracle, with float64 available there too), then run essentially the same numerics on M55 and Hexagon, so golden-model diffs stay tight everywhere. Still parameterize the core over sample type (cheap insurance, keeps float64 on desktop; AmbiTap/SampleRateTap already template on `Sample`), but the per-target effort now shifts from sample-type surgery to **data layout and scheduling**: block sizes, memory placement (VTCM/L2 on Hexagon), and vector mapping. A **partitioned-block frequency-domain** structure is the unifying choice: minimizes MIPS (M55), maps onto HVX's wide vectors and block memory model, and is trivially fast on desktop.

### Reuse from the ecosystem (don't rebuild)

- **FFT + partitioned convolution:** AmbiTap already has an Ooura real-FFT wrapper (`include/ambitap/math/binaural/ooura_fft.h`) and a **partitioned overlap-save convolver** (`convolution.h`, `convolver_bank.h`) — the exact structural skeleton of an FDAF. Don't take a cross-repo dependency for it; **vendor the same Ooura core and port the wrapper/convolver patterns into `mutap::`** (they'll diverge: FDAF needs the frequency-domain state exposed for the adaptive update, not encapsulated behind a convolver API).
- **RT plumbing:** `rt_published` / `async_rebuilder` patterns from AmbiTap `dsp/util/` for any control-thread ↔ audio-thread handoff.
- **Embedded CI:** SampleRateTap's QEMU MPS3 AN547 flow for the M55 milestone.
- **Validation style:** notebooks drive the library through a **C ABI + ctypes** (SampleRateTap `tools/capi/` pattern, `-DMUTAP_BUILD_CAPI=ON`), with deterministic regression fixtures and a `bench/` directory.

### Hexagon target specifics (QCS8550 / SM8550)

Concrete numbers that pin down the layout decisions (the vector-width question is settled — this generation is **128-byte only**; the legacy 64-byte HVX mode was dropped at V66, so don't design around it):

- **128-byte HVX vectors = 32 fp32 lanes.** Make FFT partition lengths and processing block sizes multiples of 32; 128-byte-align all vector buffers. Lay out the FDAF partitions and the complex frequency-domain data so radix stages fill full 32-lane vectors (split real/imag layout usually maps more cleanly than interleaved).
- **~8 MB TCM/VTCM scratchpad.** Big enough to keep the *entire* partitioned feedback-path filter + FDAF state (partitions, regressor history, twiddles, FFT scratch) resident. That residency is the performance win — size the partition count/length to fit VTCM and DMA audio blocks in/out. Confirm the exact VTCM budget exposed to a user cDSP session via the SDK.
- **No L1 for HVX; L2 is the vector unit's first cache.** Favor streaming, contiguous access; the vector FIFO hides L2 latency. Avoid scatter/gather in the hot path unless necessary.
- **~4 vector contexts/threads.** Natural axes for parallelism if needed: across filter partitions, or across channels for multichannel AFC.
- **Platform longevity:** QCS8550 is on Qualcomm's Product Longevity Program (support expected through ~2033) and is user-programmable on the cDSP — a good fit for a long-lived embedded audio product. Dev hardware: Lantronix Open-Q / Qualcomm RB-class boards, eInfochips Aikri SoM.

---

## Milestones (Rev 3: ALL DONE — kept as the record of how it was staged)

Every stage below landed with its pass criterion met and regression
fixtures committed; the README's Status section carries the measured
results. The staging itself proved out — each layer's crisp pass/fail
experiment caught real issues (M2's naive-bias baseline, the M4 burst
fixtures, the room sweeps) that a build-it-all-at-once plan would have
merged blind.

**M0 — Scaffold `tap/MuTap`.** Sibling-repo layout: `include/mutap/`, `tests/`, `bench/`, `examples/`, `notebooks/`, `tools/capi/`, `cmake/`, `docs/`; MIT LICENSE; `STYLE.md` + `.clang-format`/`.clang-tidy` copied from a sibling; CI (`ci.yml`: build matrix, tests, format/tidy gates); README using the framing above. Vendor Ooura, port the real-FFT wrapper.

**M1 — Partitioned-block FDAF, open loop.** Overlap-save partitioned frequency-domain adaptive filter with NLMS update, `Sample`-templated, RT-contract-clean. *No PEM, no closed loop yet* — run it as a plain AEC: known synthetic/measured RIR as the true path, white and colored noise as input. **Pass:** misalignment (normalized coefficient error) converges to the level and at the rate the FDAF literature predicts; float32 vs float64 diffs are at rounding level.

**M2 — Closed-loop simulator + metrics.** The harness *is* a deliverable: true path `F` (RIR), forward path `G` (gain K, delay d, optional processing), near-end source injection, block-by-block loop. Metrics: howling onset detection, **maximum stable gain / added stable gain (ASG)**, ERLE, misalignment. **Pass:** reproduces the textbook failure — the naive M1 filter's estimate biases and the loop howls at modest K. That failure is kept as a permanent regression baseline; PEM's whole job is to beat it.

**M3 — PEM prewhitening.** Pluggable near-end predictor interface; first plug-in is the **speech cascade** (short-term LP + long-term/pitch predictor, per the papers). Autocorrelation → Levinson-Durbin with regularization and order limits; prefilter both `u` and `e`; frame-based per AFROW. Dedicated unit tests for the conditioning watch-list. **Pass:** on speech-like near-end in the M2 loop, bias is removed — misalignment keeps improving where M1 plateaued/diverged, and measured ASG gains match the ballpark of the FDAF-PEM-AFROW paper.

**M4 — Adaptation control + robustness.** IPC computation, IPC-gated freeze/slow adaptation, variable regularization. Double-talk and transient fixtures. **Pass:** survives simulated double-talk and near-end bursts without divergence; IPC behaves as the 2014 paper describes.

**M5 — Scaffold `tap/MuTap-Max` + first external.** Min-DevKit package mirroring AmbiTap-Max (MuTap as submodule); one external wrapping the M4 processor — working name **`mutap.defeed~`** (sibling convention: `<repo>.<verb>~`, e.g. `ambitap.encode~`; final name open). Attributes: forward-path delay report, adaptation freeze, filter length, predictor selection; help patcher demonstrating added-stable-gain in a live mic→speaker patch.

**M6+ (later):**
- **Music/tonal near-end predictor** — *done*: `warped_lpc_predictor` (frequency-warped all-pole), room-robust when paired with IPC-scaled stepping (see the class comment in `lpc.h` for the measured room sweep and why the pairing is required).
- **Cortex-M55 build** — *done* (bare-metal QEMU AN547 rig ported from SampleRateTap, on-target subset in CI). Still open from this line: CMSIS-DSP mapping and the instruction-count ratchet.
- **Hexagon build** — *toolchain leg done*: hexagon-unknown-linux-musl (Codelinaro clang, HVX auto-vectorization, static musl link) runs the emulation-sized suite under qemu-hexagon in CI, and the full suite passed once on the ISA. Still open: the actual performance work — VTCM residency, L2 streaming layout, HVX vector mapping of the FDAF hot path — which needs the Hexagon SDK and hardware.
- **PEM-FD-Kalman** upgrade (v2 algorithm) — *done*: `partitioned_fdkf` (fd_kalman.h), a drop-in core for `pem_afc` (which is now templated over the adaptive core). Measured: dissolves the NLMS speed/depth tradeoff, saturates the +25 dB ASG probe on broadband near-end, room-robust on music with zero adaptation-control config, survives bursts ungated. The transient floor is opt-in (burst hardening at a measured tonal-ASG cost). Not yet the default core in the Max external — that switch (and any deprecation of the M4 knobs) deserves its own decision after in-Max listening.

---

## Reference implementations to study

- **SpeexDSP** (`speex_echo`) — MDF-style AEC, portable C, has a fixed-point build path. Best embedded-friendly starting point for structure.
- **WebRTC AEC3** — modern, robust, but float-only and entangled with the WebRTC tree. The **aec3-rs** Rust port exposes the modules standalone and is easier to read than the Chromium source.
- **ST FP-AUD-AEC1** (STM32Cube function pack) — full AEC reference on Cortex-M; useful embedded structural template.
- **In-house:** AmbiTap's `convolution.h` / `convolver_bank.h` (partitioned overlap-save) and `ooura_fft.h`; SampleRateTap's `platform/` + perf-ratchet CI.

*(Note: the external ones are AEC references; PEM-AFROW / AFC has less turnkey open source — expect to implement the feedback canceller from the papers below.)*

---

## Key papers

- **T. van Waterschoot, M. Moonen**, "Fifty Years of Acoustic Feedback Control: State of the Art and Future Challenges," *Proc. IEEE* 99(2):288–327, 2011. — The canonical survey; read first.
- **G. Rombouts, T. van Waterschoot, M. Moonen**, "Robust and Efficient Implementation of the PEM-AFROW Algorithm for Acoustic Feedback Cancellation," *J. Audio Eng. Soc.* 55(11):955–966, 2007. — The core algorithm.
- **J. M. Gil-Cacho, T. van Waterschoot, M. Moonen, S. H. Jensen**, "A Frequency-Domain Adaptive Filter (FDAF) Prediction Error Method (PEM) Framework for Double-Talk-Robust Acoustic Echo Cancellation," *IEEE/ACM TASLP* 22(12), 2014. — FDAF-PEM-AFROW, IPC measure, BLUE connection. The variant to build.
- **G. Bernardi, T. van Waterschoot, J. Wouters, M. Moonen**, "A PEM-based frequency-domain Kalman filter for adaptive feedback cancellation." — The SOTA upgrade path.
- **A. Spriet, I. Proudler, M. Moonen, J. Wouters**, "Adaptive feedback cancellation in hearing aids with linear prediction of the desired signal," *IEEE Trans. Signal Process.* 53(10):3749–3763, 2005. — The PEM-AF predecessor.
- **M. R. Schroeder**, "Improvement of acoustic-feedback stability by frequency shifting," *JASA* 36(9), 1964; and **E. Berdahl, D. Harris**, "Frequency shifting for acoustic howling suppression," *DAFx*, 2010. — Cheap decorrelation, if a shift is added to the forward path.

---

## Open decisions (for Tim)

Resolved since rev 1: ~~license~~ (MIT), ~~core language~~ (header-only C++20), ~~milestone ordering~~ (executed as staged). Resolved since rev 2: ~~first program-material target~~ (moot — both predictors and both engines exist; what remains is the real-room tuning pass, item 1 of "What's next").

Resolved since rev 3: ~~Max external naming~~ — settled (Rev 4): **`mutap.afc~`** (rename from the `mutap.defeed~` placeholder) and **`mutap.aec~`** for the new echo canceller, an acronym pair matching the literature. The rename executes in Stage 2 of "The next effort" above.

Still open:
- **Default engine in the external** — `@kalman` off (classic NLMS) is the shipping default purely on seniority; the measured case for flipping it is in `tests/test_fd_kalman.cpp` and book chapter 1. Decide after real-room listening.
- **RIR fixtures, the measured half** — the fixture pipeline is built and three physically-modeled rooms (image-source, documented geometry) are committed baselines with regression tests. What remains yours: which MEASURED rooms join them — an academic dataset room (MYRiAD is the PEM-AFROW group's own database; openAIR is the other usual source; check each room's license allows redistribution in an MIT repo) and/or your own swept-sine measurements. Either way it is one command per room: `python3 tools/fixtures/make_rir_fixtures.py --from-wav room.wav myroom --source "<provenance + license>"`, then a test with a freshly measured threshold. (The dataset hosts are unreachable from the remote dev container's network policy, so the WAVs have to enter via a commit.)
