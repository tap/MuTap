# Adversarial audit of the `mutap.wake~` proposal

*Audit, rev 1 — 4 September 2026, of [`wake-word-plan.md`](wake-word-plan.md)
rev 1 and its companion [briefing](wake-word-briefing.md). Nothing in the plan
was changed by this audit; every amendment below is a recommendation for rev 2.
Rev 2 of the plan, carrying all fourteen, landed alongside this document; its
milestone numbers shifted (rev 1's M2–M7 are rev 2's M3–M8, and the "M1.5"
proposed below became M2).*

Formatted version: <https://claude.ai/code/artifact/16e51831-e360-4475-af97-5af77cd3926e>

---

## 1. Verdict

**The plan's design direction survives. Its central empirical claim does not
survive as written.**

The direction — hand-written, allocation-free inference against a documented
numeric contract, promoted into DspTap, geometry carried as a value in a
versioned weights header, a Python feature module as the single source of
truth, no imported runtime — is sound, and nothing in the audit argues for a
framework instead. The reuse story holds for the *patterns*.

The claim in §1 that "MuTap has already solved the hard infrastructure problem,
and solved it for exactly this shape of task" is wrong about the *oracles*.
Checked against the tree: the learned suppressor is instantiated only in
double, everywhere (its tests, the parity driver, the shipping Max external);
the instruction-count rig's "suppressor" scenario is the classical
`residual_suppressor`, not the GRU; the Cortex-M55 on-target filter does not
select the learned suppressor's tests at all; and the parity script is a
hand-run, double-only comparison against a numpy reference with random
weights, absent from CI. So the float32 embedded profile, on-target execution,
per-hop cost measurement and CI parity for a learned object are all *new work*,
not inheritance — and M2's and M6's pass criteria, which assume they exist, are
vacuous as written.

Two further infrastructure questions were tolerable for a post-filter and are
load-bearing for a spotter, and the plan does not decide either: what happens
to a 16 kHz-trained model on a 44.1/48/96 kHz Max host, and how a 10 ms hop
(never a power of two) coexists with a geometry validator that demands one.

Amended bet, which the plan should state instead: *MuTap has established the
patterns and the rigs; the learned path's float, on-target, cost and parity
oracles must be built before M2, and host-rate policy decided at M0, before
the spotter can inherit anything.* With those amendments the milestone
structure stands.

## 2. How the audit was run

Sixteen agents, three phases, every finding checked against the working
checkouts of MuTap, DspTap and MuTap-Max rather than against the plan's own
description of them.

| Phase | Agents | What they did |
|---|---|---|
| Attack | 7 | One adversarial reviewer per lens: DSP front end and contracts; codebase fit; model architecture; evaluation and data; licensing and IP; embedded profile and performance; scope and sequencing. Each returned findings with file-and-line evidence and a stated refutation test. |
| Verify | 7 | One skeptic per lens, instructed to *refute* each finding and default to refuted when the evidence did not support the claim. Each verdict re-cites the code. |
| Critique | 2 | A completeness critic looking for what no lens examined; a cross-lens judge merging duplicates into root issues, ranking them, and naming findings the verifiers should not have let through. |

42 findings were raised. 8 were confirmed outright, 34 were partially confirmed
(a real point, but overstated or mis-cited, with the accurate statement
recorded), 0 were refuted. The zero refutation rate is itself a signal that the
skeptics leaned lenient, so the judge's list of doubtful survivors (§5) is
carried here as a correction. The critic added five findings the lenses
structurally missed. The main session then spot-checked the top-ranked root
issue's evidence by hand; every claim in it re-checks (§3, issue 1).

Full agent output, including every finding's evidence and the verifier's
reasoning, is in the session transcript; this document carries the merged
result.

## 3. Root issues, ranked by how much they change what happens before work starts

### 1. The infrastructure M2 says it inherits does not exist for the learned path — *critical*

**What the plan says.** §3: the promotion inherits "kernels a shipping, tested
object already exercises"; the M55 rig and ratchet "come essentially free";
`test_parity.py` "pins C++ inference against the Python trainer's output" and
is one of the three CI gates that "already exist". M2 passes when
`test_nn_suppressor.cpp` and `test_parity.py` pass unchanged and "the M55
instruction count for the suppressor does not regress".

**What the code says.**

- `nn_suppressor` is instantiated as `<double>` only: in
  `tests/test_nn_suppressor.cpp`, in `tools/ml/nn_infer.cpp`, and in
  `mutap.aec_tilde.cpp` (`aec_chain_nn<double>`). There is no float typed test
  and it is absent from `tests/test_float32.cpp`. The float profile the
  promotion is supposed to preserve is never observed anywhere.
- `bench/icount/icount_main.cpp` includes only `fd_kalman.h` and
  `postfilter.h`. The ratchet's "suppressor" scenario is the classical one. The
  "does not regress" clause in M2 measures code M2 does not touch.
- `tests/bare_metal_main.cpp` applies a positive filter for the M55 leg; no
  `NnSuppressor` pattern is in it. The learned path has never executed
  on-target.
- `tools/ml/test_parity.py` compares C++ double output against `nn.py`'s numpy
  reference using *random* weights at the legacy 16 kHz / 22-band geometry,
  gain path only, run by hand behind an OFF-by-default CMake option. It never
  loads trainer output, never covers the shipping 48 kHz geometry, and the
  only "parity" job in `ci.yml` is the unrelated branchless check.
- The plan's own §5 asks the promoted kernels to document "which accumulations
  run in double"; today the answer is none — every dot product accumulates in
  `Sample`.

**What to change.** Add a milestone between M1 and M2 that builds the oracle
the plan assumes: `nn_suppressor<float>` typed tests with a float/double pin;
the learned suppressor in both on-target filters; an `nn_suppressor` icount
scenario seeded on M55 and Hexagon *before* the refactor; `test_parity.py`
wired into CI and run against exported trainer weights at the shipping 48 kHz
geometry. Rewrite §3's `test_parity.py` and "essentially free" rows and M2's
pass criterion to name these gates. Decide the accumulator-precision contract
before the refactor, not during it.

### 2. Host sample rate and front-end frame geometry are undecided, and the pattern to "copy verbatim" cannot serve them — *critical*

**What the plan says.** §3: copy the `nn_geometry` / MUNN0002 pattern
"verbatim"; §5 lists frame, hop and window without units, fmin/fmax or a
host-rate policy; §6 and §7 speak of "per 10 ms hop"; M7 must work "against a
live microphone".

**What the code says.**

- Every corpus in §4 is 16 kHz. Max runs at 44.1, 48 or 96 kHz. The
  suppressor's precedent is retrain-per-rate: `features.py` and
  `nn_suppressor.h` define bands over 0..sr/2, `make_dataset.py` spectrally
  upsamples LibriSpeech 3× for the 48 kHz model, and `mutap.aec~` only prints
  "detuned bands" and runs anyway when the model rate disagrees with the host.
  HANDOFF already files "per-rate (44.1 kHz) models" as an open follow-up.
- No runtime resampler exists in the pinned tree. DspTap holds the FIR
  substrate but its README says the converters live in SampleRateTap and
  RatioTap, repositories the plan never names. The only resampler in any of
  the three repos is test-support code.
- `nn_suppressor_weights::valid()` requires `hop >= 16` and a power of two. A
  10 ms hop is 160, 441 or 480 samples — none is a power of two — so the
  conflict bites at *every* rate, not only 44.1 kHz. The suppressor's
  `frame == 2·hop == FFT size` convention does not hold for a 25 ms / 10 ms
  mel front end either, and §5 omits FFT size and zero-padding placement.
- §5 also omits streaming frame alignment (`features.py` documents that
  GRU-state parity depends on exactly this), bin-0 / DC policy (the inherited
  band builder forces bin 0 into band 0, the opposite of a mel with
  fmin > 0), pre-emphasis, PCEN initial state and reset semantics, and the
  identity of the "reference mel implementation" M1 is scored against — no
  such reference exists in the family today.

**What to change.** Make host-rate policy an M0 decision with its cost stated:
per-host-rate models on upsampled corpora (the suppressor's route, with the
band-limited-above-8 kHz risk named), or a real-time decimator as a new DspTap
primitive built on `kaiser.h` and `fir_kernels.h`. Specify in §5 that
`kws_geometry` decouples hop from FFT size (zero-padded power-of-two FFT), and
add the missing contract lines. Name the M1 reference — a committed numpy mel
in `tools/ml`, written before M1 — and make the float/double tolerance a
measured number on a signal that excites every band.

### 3. The evaluation the plan calls its decisive instrument cannot yet produce a trustworthy number — *critical*

**What the plan says.** M4 is "the meter before the model"; it passes when the
trivial baseline's DET curve "looks" right. M3 draws bulk negatives from
Common Voice and hard negatives from MSWC, and delivers a recorded hold-out
set "collected separately". §8 says corpus assembly happens locally and enters
the repo as "manifests and derived features, not audio".

**What the audit found.**

- No train/dev/eval split is specified for negatives. MSWC is force-aligned
  *out of* Common Voice, so a corpus-level split leaks: false accepts per hour
  would be measured on material the model trained on.
- M4's pass criterion cannot fail — any threshold sweep produces a curve that
  collapses as the threshold tightens. The scoring rules that actually break
  a KWS meter (hit window around a positive, one-hit-per-utterance, false
  accept merging under refractory, the hours denominator) are undefined
  anywhere in the plan, and a band-energy baseline cannot expose accounting
  errors in them.
- The recorded hold-out set has no size, talker count, distance/SNR matrix,
  owner, or consent/licence row. It is the basis for M5's only measured
  number and its generalization claim is undefined. The RIR fixture precedent
  the plan cites requires provenance per committed fixture.
- "Derived features" for 300 h of audio is roughly 17 GB of float32; the repo
  cannot hold it and no feature store is named. As specified, the DET notebook
  is re-executable on one machine only. The two existing ML notebooks already
  show this failure mode.
- The FA/hour side of the operating point has no named held-out negative
  corpus, and the 1 FA/hour target is looser than the briefing's product
  figure without saying it is a first-release target.
- M4 as written depends on M3's corpus and M0's phrase even though §4's table
  says to bring the harness up on Speech Commands first.

**What to change.** Before M3: utterance- and speaker-disjoint splits with the
MSWC↔Common Voice overlap handled; a manifest schema (release ids, archive
checksums, decoder versions, front-end contract version); a named feature
store outside git; a hold-out specification (N talkers, condition matrix,
owner, consent row). Replace M4's pass with a planted-event oracle test with
exact expected recall and FA/hour that *must fail* on an accounting error, and
name harness↔`kws.h` decision-stage parity as a second parity surface. Let M4
run on Speech Commands so it genuinely precedes M0 and M3.

### 4. The licensing map inverts the risk — *major*

**What the plan says.** §4: every input can be MIT, CC0, CC BY or
"self-generated", with TTS positives the clean case; MUSAN + SLR28 are
"CC BY / permissive"; openWakeWord / microWakeWord are "permissive".

**What the audit found.** (Network access in this environment was limited;
these are flagged for verification at the point of use, as the plan's own
provenance section requires.)

- Both voices that `piper-sample-generator` documents descend from the
  Blizzard 2013 Lessac corpus, whose licence is research-only and names
  speech-recognition products as excluded. TTS positives are the *least*
  clean input, not a self-generated one. Lessac-free Piper voices exist.
- "Yes, with attribution" has no delivery mechanism: nothing in M5 or M7
  places attribution in the shipped weights header (stamped MIT) or the Max
  package. The suppressor precedent credits LibriSpeech in two READMEs and
  nowhere a user of the external would see.
- SLR28 is Apache 2.0, not CC BY, and its real-RIR subset carries third-party
  RWCP/REVERB/AIR provenance the plan does not check.
- openWakeWord and microWakeWord code is Apache 2.0; their models are
  CC BY-NC-SA and their pre-computed feature sets CC BY-NC with WHAM/CHiME6
  upstream. The row should say "design reference only; no code, models or
  features imported".
- Common Voice and MSWC carry a downloader promise not to attempt speaker
  re-identification. It does not restrict the plan's use, but belongs in the
  dataset card.
- The patent paragraph borrows the suppressor's "comparable" prior-art
  phrasing in a field where the 2014-onward publishers are the patent holders.
  Neither patent the audit surfaced reads on a single-stage reimplementation;
  the fix is narrower wording, not a change of design.

**What to change.** Make voice-lineage verification an M0 pass criterion and
choose a Lessac-free voice; reword "self-generated" to "TTS-derived,
lineage-verified per voice"; relabel the SLR28 and openWakeWord rows; add a
hold-out consent/licence row; require every feature shard to be regenerated
from manifest audio; add a provenance/attribution block to the MUKW exporter
output and a notices file to the Max package as M5/M7 pass items.

### 5. M3 pre-commits the M5 architecture the plan says it defers — *major*

The dataset builder never decides the label form. A clip-level label is
implicit, which trains a clip classifier; but without a tracked keyword
endpoint (or frame/word alignment) the M5 "measure, don't argue" comparison
cannot include frame-wise or end-anchored losses on equal footing, and the
briefing itself (§4, end-to-end variants) says alignment is the hard part.
The `kws` contract also lacks a detection-latency constant (hops from phrase
end to the bang) and, if a GRU stays on the shortlist, a hidden-state
reset-or-carried policy for streaming training. Single-class versus per-word
head follows from the same decision.

**What to change.** Add label form and endpoint tolerance under RIR/tempo
augmentation to M3's pass; add detection latency and streaming-state policy to
§5's `kws` contract.

### 6. No cost ceiling exists anywhere, and the ratchet is not a budget — *major*

`scripts/icount.py` is a ±3 % whole-binary drift gate seeded from the first
measurement. The plan calls it "a committed budget per 10 ms hop", which it is
not, and states no ceiling on MACs, instructions, weight or activation RAM, or
detection latency. M5's architecture choice therefore has one axis (DET) when
it needs two. M6's int8 option is coupled to the architecture it is supposed
to be independent of: the briefing says recurrent state is awkward to
quantize, so int8-as-requirement constrains the shortlist.

**What to change.** State numeric ceilings up front; derive a per-hop figure
from the corpus in M6 and describe `icount.py` honestly as a drift gate plus a
separate absolute assertion; either decide float32-only for M55 now or say
that the int8 option constrains M5.

### 7. M2 bundles consumer-less kernels and skips DspTap's checklist — *major*

"Kernels a shipping, tested object already exercises" is true of dense and GRU
and false of the depthwise-separable convolution and streaming activation
cache M2 also adds: nothing runs them and no numpy reference covers them until
M5. M2's pass criterion also omits the DspTap-side typed battery, README
section and tidy items that M1 lists for itself, and does not say that the
weights format and dimension validation stay MuTap-owned or that the
promotion implies DspTap PR → MuTap pin bump → MuTap-Max pin bump.

**What to change.** Restrict M2 to dense + GRU with the full DspTap checklist;
move DS-conv and the cache to M5, or give M2 a torch-versus-streaming kernel
fixture; record the ownership and pin sequence.

### 8. The feature contract gets two owners in two repos — *major* (critic)

M1 freezes `log_mel` — including every PCEN constant — as a DspTap contract
before any trainer exists, while M3 declares `kws_features.py` the source of
truth. Under DspTap's rule every feature iteration during M3/M5 becomes a
cross-repo breaking change through a submodule pin, and feature parameters are
exactly what a KWS pipeline iterates on.

**What to change.** State the ownership rule: `log_mel.h` owns the
formula-level contract (mel scale, normalization, window, FFT/padding, bin-0
policy); everything a trainer might tune (band count, fmin/fmax, log floor,
affine normalization, all PCEN parameters) is runtime geometry carried by the
MUKW weights. Retraining then never touches DspTap.

### 9. "No CMSIS-NN" contradicts the family's filed roadmap — *major* (critic)

§3 rules out CMSIS-NN as "the family's demonstrated position", but HANDOFF and
`tools/ml/README.md` both file "int8 + CMSIS-NN for the M55 path" as the
suppressor's next step, and the `fft.h` precedent the same table cites *is* an
optional CMSIS backend behind a fixed contract. Because M2 makes `tap::dsp::nn`
the single substrate, the plan silently decides the suppressor's roadmap too.

**What to change.** Adopt the `fft.h` rule instead: a scalar golden kernel with
an optional, opt-in accelerated backend that must re-present the exact
contract. Design the M2 kernel API with that seam, and reconcile HANDOFF and
the README with whichever decision is taken.

### 10. The DET will be measured on the Python model, not the shipping C++ — *major* (critic)

§7's "standing promise" is that every performance claim is measured on the
shipping code, and §5 requires `kws.h` to declare the operating point "it was
measured at". But M5 lists no C ABI for `kws.h`, the M4 harness is Python in
`tools/ml`, and DspTap's `dsptap_py` bridge is not what MuTap's `tools/ml`
loads. The family's own v1 notebook shows the failure: the hybrid it measured
ran a numpy net over numpy features.

**What to change.** Add a `mutap_kws_*` C ABI family and `mutap_ffi` binding to
M5; state that the final DET runs the C++ engine; make the C++-measured
operating point the one the header declares; run the M4 oracle test against
both paths.

### 11. No compute budget or hardware is named — *major* (critic)

The reference trainer is CPU-only by construction (no device option) and took
about 2.5 h for roughly 1 h of audio. The KWS corpus is two orders of magnitude
larger, plus tens of thousands of TTS syntheses through a PyTorch pipeline of
its own, and M5 wants several full trainings. §8 names disk and network as the
corpus problem and never mentions compute.

**What to change.** Name where training runs, a per-run time target and a
device path in the sibling trainer; size a development negative set for
architecture selection and reserve the full corpus for the final DET; give
TTS synthesis its own runtime estimate and toolchain list.

### 12. Lesser items — *minor*

- **Release shape.** §9 calls "ship a runtime, not weights" a reorder. It is a
  shortening: it removes the phrase as a blocking decision, the shipped
  dataset card, the recorded hold-out set and the measured operating point,
  while keeping M3's pipeline as the product. Say so, and reconcile with
  HANDOFF's "before anything starts".
- **Operating point plumbing.** Nothing links the DET-measured threshold to
  the constants in `kws.h`, the MUKW header and the Max `@threshold` default.
  Put them in the MUKW payload and have the exporter emit them from the
  notebook report.
- **"Ship" is undefined.** Neither repo has a tag, release, artifact upload or
  signing step; users clone and symlink. State what ship means today, or add
  a release job. Add the customary docs stage (book chapter, README status
  rows, notices file) that every prior effort under HANDOFF carried.
- **Charter.** "Portable speech DSP for embedded targets" does not cover the
  AFC/music side; the repo decision affects M3/M4 file placement, not only
  M5; HANDOFF and the plan disagree on when it blocks.
- **Factual errors in asset descriptions.** MuTap's tests are GoogleTest, not
  Catch2. The "`prepare()`-buys-worst-case rule" is not locatable in DspTap,
  MuTap or MuTap-Max (quote DspTap's construction-time wording instead).
  `sample_traits.h` is a FIR format core, not a fixed-point front-end route;
  DspTap has no fixed-point FFT. `tools/ml` is suppressor-shaped and the
  second task needs its own parity driver, CMake target and README re-scope.
  Provenance omits MuTap-Max, which the plan clearly read.

## 4. What survives unchanged

Worth saying, since an audit lists only defects:

- Promoting a mel front end and the dense/GRU kernels into DspTap is right and
  pays off whether or not the spotter ships. Every lens agreed.
- No TFLite Micro, no ONNX runtime. Nothing argued for one.
- Geometry as a value in a versioned weights header, validated at load, with
  the trainer's feature module as the source of truth — the pattern is
  correct; only its ownership boundary (issue 8) needs stating.
- The meter before the model. The ordering is right; the meter's pass
  criterion is what needs fixing.
- Recommending MuTap as the home. The audit found no reason for a new
  repository.
- The honesty of the provenance section. No number in the plan is presented
  as measured, and the audit found none that was.

## 5. Findings the audit discounted

The cross-lens judge flagged seven survivors the verifiers should have
rejected or narrowed. They are recorded here so the correction is visible:

| Finding | Why discounted |
|---|---|
| float/double agreement "unpassable" on log features | The verifier's own measurement showed sqrt-Hann float and double log-band features agree to 0.000 decades; worst case found was ~0.27 feature units on an on-bin sine. Residual is a one-line M1 note: measure the tolerance on a signal that excites every band. |
| Network policy rationale "stale" | One day's proxy reachability from one container is an environment observation, not a plan defect. The disk half stands (issue 3). |
| Always-on duty cycle "never measured" | Misreading: the briefing's "few percent" is utilization of a continuously running spotter, which per-hop instruction counting measures directly. Only the latency omission survives (issue 5). |
| Patent paragraph | Neither cited patent reads on the plan's single-stage structure. A wording preference, kept in issue 4 as such. |
| "`prepare()` rule does not exist in the family" | Only shown absent from the three repos present; TapTools was not available. Downgraded to "not locatable here". |
| Three-repo pin sequence undecided | Documented in DspTap's CLAUDE.md and HANDOFF; residual is one clause in M2. |
| Charter wording inadequate | A judgement call, not a verifiable defect. Only the HANDOFF-versus-plan inconsistency is concrete. |

## 6. Recommended amendments to the plan, in order

For rev 2 of `wake-word-plan.md`:

1. **§1** — restate the bet as patterns-and-rigs inherited, oracles to be
   built (issue 1).
2. **§3** — correct the `test_parity.py`, "essentially free", `sample_traits`,
   Catch2 and `prepare()` rows; replace "no CMSIS-NN" with the `fft.h`
   backend rule (issues 1, 9, 12).
3. **§4** — TTS row reworded and voice lineage made an M0 gate; SLR28 and
   openWakeWord rows relabelled; hold-out consent row and attribution
   delivery added; patent paragraph narrowed (issue 4).
4. **§5** — add FFT size/padding, frame alignment, bin-0, pre-emphasis, PCEN
   initial state, fmin/fmax in Hz, detection latency, streaming-state policy;
   state the DspTap-versus-weights ownership rule (issues 2, 5, 8).
5. **M0** — add host-rate policy and voice lineage to the decisions; note
   what runtime-first actually removes (issues 2, 4, 12).
6. **M1** — name the reference implementation; make the cross-precision
   tolerance a measured number (issue 2).
7. **New M1.5** — build the float32, on-target, icount and CI-parity oracles
   for the learned suppressor before touching it (issue 1).
8. **M2** — dense + GRU only, full DspTap checklist, ownership and pin
   sequence stated (issue 7).
9. **M3** — splits, manifest schema, feature store, hold-out spec, label
   form, compute budget, dataset card rows (issues 3, 5, 11).
10. **M4** — planted-event oracle pass criterion; runs on Speech Commands
    first; decision-stage parity named (issue 3).
11. **M5** — C ABI and `mutap_ffi` binding; cost ceilings as a second axis;
    operating point emitted by the exporter into the MUKW payload (issues 6,
    10, 12).
12. **M6** — per-hop figure derived from the corpus; ratchet described
    honestly; int8 conditional on M5 (issue 6).
13. **M7** — define ship; add the docs stage and notices file (issue 12).
14. **HANDOFF entry** — reconcile on what blocks when (issue 12).

---

### Provenance

- Audit only — no code written; the plan and briefing are unchanged.
- Agent claims about the code were re-verified by a skeptic per lens and, for
  the top-ranked issue, by hand in the main session against
  `tests/test_nn_suppressor.cpp`, `tools/ml/nn_infer.cpp`,
  `mutap.aec_tilde.cpp`, `bench/icount/icount_main.cpp`,
  `tests/bare_metal_main.cpp`, `.github/workflows/ci.yml`,
  `include/mutap/nn_suppressor.h` and `HANDOFF.md`.
- Licence and patent statements (issue 4) were made with limited network
  access and are flagged for verification at the point of use; they are not a
  legal opinion.
- TapTools and TapTools-Max were not available to the audit; claims the plan
  makes about them are marked unverifiable rather than wrong.
