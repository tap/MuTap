# Building `mutap.wake~` — implementation proposal

*Proposal, rev 1 — 4 September 2026. **Nothing committed beyond this document:**
no code written, no repository changed apart from these docs. Background and
corpus survey in [`wake-word-briefing.md`](wake-word-briefing.md).*

Formatted version: <https://claude.ai/code/artifact/341309d6-52dc-4d86-896c-2bcdd9534163>

---

## 1. The proposal

> Add a wake-word spotter to the Tap family by promoting two shared pieces down
> into DspTap, extending MuTap's existing `tools/ml` pipeline to a second task,
> and shipping one Max external. No new framework, no TFLite Micro dependency,
> no new repository until a second consumer justifies one.

The bet behind this plan is that **MuTap has already solved the hard
infrastructure problem**, and solved it for exactly this shape of task.
`nn_suppressor.h` is a hand-written, allocation-free, `noexcept` GRU running on
ERB band energies, with its geometry carried as a value in a versioned weights
header, a Python trainer whose feature module is the declared single source of
truth, and a parity test pinning the C++ against it. A keyword spotter is the
same machine with a different head and a different loss.

What does *not* exist yet: a mel/PCEN front end, an int8 or DS-CNN inference
path, a keyword training corpus, and — the piece that actually decides whether
this works — an evaluation harness measuring false accepts per hour rather than
accuracy.

The plan sequences those four, deliberately putting the meter before the model.

## 2. Where it lands

```
MuTap-Max     mutap.afc~     mutap.aec~     [mutap.wake~]  ← new, M7
                                  ↑ submodule pin
MuTap         fd_kalman.h    nn_suppressor.h    [kws.h]    ← new, M5
              tools/ml  ─────────────────────  [tools/ml/kws]  ← extended, M3
                     │            ↑ submodule pin
                     │ refactored onto
                     ↓
DspTap        fft.h    yin.h    [log_mel.h]   [nn/]
                                 ↑ new, M1     ↑ promoted, M2
```

**The load-bearing move is the promotion.** M2 lifts the dense/GRU arithmetic
out of `nn_suppressor.h` into a shared `tap::dsp::nn` and refactors the
suppressor to consume it — so the spotter inherits kernels a shipping, tested
object already exercises, and the family gains one inference substrate instead
of two. This is the same promotion the family has already run three times:
`fft.h` out of MuTap and AmbiTap's duplicate copies, `sample_traits.h` from
SampleRateTap, `yin.h` out of the TapTools pitchaccum kernel.

Two of the three new pieces (M1, M2) belong unambiguously in DspTap, which is
what makes the repository question for the third low-stakes: M1 and M2 are
correct under every option, so nothing blocks on deciding where the spotter
itself lives.

TapTools is the wrong home and worth ruling out explicitly: its charter is
musical, object-level kernels, and a keyword spotter is neither.

## 3. What already exists, and what it saves

| Existing asset | What it does today | Role for the spotter |
|---|---|---|
| `nn_suppressor.h` | Dense → GRU → dense, float32, allocation-free, noexcept, geometry carried by the weights | The inference kernels, promoted in M2. The spotter is a different head on the same arithmetic. |
| `nn_geometry` / MUNN0002 | Model geometry as a value, validated at load; one inference path serves 16 kHz hop-64 and 48 kHz hop-256 | Copy the pattern verbatim for `kws_geometry` and a `MUKW0001` header. Retraining at a new geometry must not require a code change. |
| `tools/ml/features.py` | Declared single source of truth for band definitions and normalization | The precedent to copy for KWS features — and the discipline that makes the parity test meaningful. |
| `tools/ml/test_parity.py` | Pins C++ inference against the Python trainer's output | The single most valuable test in the project. A spotter that disagrees with its trainer fails silently and looks like a data problem. |
| `tools/ml/README.md` | The licensing map and measured benchmark that justified the hybrid design | The template for M3's dataset card. Same question, same rigour, different corpus. |
| Cortex-M55 QEMU rig + `scripts/icount.py` | On-target test subset in CI, instruction counting via a QEMU plugin | The embedded profile and its performance ratchet come essentially free in M6. |
| DspTap `fft.h` backend pattern | Ooura golden model; CMSIS-Helium and vDSP float32 backends re-presenting the exact contract | The mel front end's FFT, and the pattern for any future accelerated kernel. |
| DspTap `sample_traits.h` | float / Q15 / Q31 format core with documented Q-format ladders | Route to a fixed-point front end for M33-class targets, opt-in per existing convention. |

**What this rules out:** no TFLite Micro, no CMSIS-NN dependency, no ONNX
runtime. The family's demonstrated position is hand-written inference against a
documented numeric contract, and a spotter is small enough — tens of thousands
of parameters — that this stays the cheaper option. Importing a runtime would
also break the M55 and Hexagon story MuTap already has working.

## 4. Licensing map

Same exercise `tools/ml/README.md` ran for the suppressor, applied to a wake
word. Comparable conclusion: every input can be MIT, CC0, CC BY or
self-generated, provided positives are synthesized rather than borrowed.

| Component | Licence | Role | Shippable? |
|---|---|---|---|
| Speech Commands v2 | CC BY 4.0 | Benchmark; harness bring-up | yes, with attribution |
| MSWC | CC BY 4.0 | Hard negatives, phonetic near-misses | yes, with attribution |
| Common Voice | CC0 | Bulk negatives | yes |
| AMI Meeting Corpus | CC BY 4.0 | Conversational negatives | yes, with attribution |
| MUSAN + OpenSLR SLR28 | CC BY / permissive | Noise and RIR augmentation | yes |
| Piper + piper-sample-generator | MIT | Synthetic positives | code yes — **verify each voice's upstream corpus licence separately** |
| openWakeWord / microWakeWord | permissive | Pipeline design reference | yes — we reimplement, share no weights |
| Hey Snips, Qualcomm KSD | research / NC | Comparison only, if at all | **no** |
| PyTorch | BSD-3 | Trainer | never shipped — inference is dependency-free |
| Trained spotter weights | ours | The deliverable | yes, if every input above holds |

Patent posture is comparable to the suppressor's: the windowed-classifier +
posterior-smoothing structure is published academic work from 2014 onward with
permissively licensed implementations, and the shipped inference would be a few
tens of thousands of parameters implemented from scratch. As before — not a
legal opinion; a commercial embedded product still warrants a freedom-to-operate
review.

## 5. The numeric contracts to pin

House rule: each header documents its geometry, conventions, normalization and
latency *as numbers*, and the tests pin them. Changing any of the following is a
breaking change for every consumer and every trained model.

### `tap::dsp::basic_log_mel<Sample>`

- Frame length, hop, window (sqrt-Hann or Hann — stated, not implied), geometry
  fixed at construction per the `prepare()`-buys-worst-case rule.
- Mel scale formula, and whether bands are Slaney- or HTK-normalized. These
  differ *silently*; a model trained against one and run against the other
  degrades in a way that looks like a data bug.
- Filter normalization: unit-area or unit-peak triangles.
- Log floor and any affine output normalization, as literal constants — the
  suppressor's `k_log_floor` / `k_shift` / `k_scale` is the pattern.
- Latency in samples, stated.
- PCEN's smoother coefficient, gain, bias, power and epsilon, each as a number,
  with the plain-log path available as the default.

### `tap::dsp::nn`

- Weight layout and gate ordering per layer, matching PyTorch's, exactly as
  `nn_suppressor_weights` already documents it.
- Which accumulations run in double even in the float profile, and why — the
  family's existing convention for numerically fragile recursions.
- For any int8 path: per-channel vs per-tensor scales, the single rounding
  point, and saturation behaviour, as `sample_traits.h` already sets out for
  Q15/Q31.

### `tap::mu::kws`

- Geometry as a value, carried by the weights and validated at load.
- Posterior smoothing window, confidence window, combination rule, refractory
  period — all as numbers, all settable, all defaulted.
- The declared operating point: threshold, and the recall / false-accepts-per-hour
  pair it was measured at, on a named evaluation set.
- Honest limits stated in the header, per house rule: the phrase it was trained
  on, the distances and SNRs it was evaluated over, and that it is a
  single-channel detector with no beamforming.

## 6. Milestones

Staged in the HANDOFF.md manner: each stage has a crisp pass criterion and a
committed regression fixture, so a failure is caught at the layer that caused
it. The one ordering choice worth defending is **M4 before M5** — the harness
before the model — the same call HANDOFF.md's M2 made when it declared the
closed-loop simulator a deliverable in its own right.

### M0 — Settle the home and the phrase *(decision)*

Two decisions, both cheap now and expensive later: which repository owns the
spotter (§9), and what the wake phrase actually is. The phrase is a technical
decision, not a branding one — three or four syllables, unusual phonotactics,
distinctive stress, not a substring of common speech. A poorly chosen phrase
costs a permanent penalty on the DET curve that no amount of training recovers.

**Pass:** phrase chosen, with its phonetic-confusability shortlist written down;
repository decision recorded in HANDOFF.md.

### M1 — The mel front end *(DspTap)*

`include/tap/dsp/log_mel.h` — `basic_log_mel<Sample>` with the double golden
model and float32 embedded profile, geometry fixed at construction, `noexcept`
and allocation-free processing, riding the existing `real_fft`. PCEN as a
documented option on the same object.

Typed GoogleTest battery pinning every contract point above, plus float/double
cross-precision agreement. C ABI exposure in `tools/capi` and the `dsptap_py`
bridge, so the notebooks measure the shipping C++ rather than a Python
restatement.

**Pass:** agreement with a reference mel implementation at a stated tolerance on
a fixed test signal, with the tolerance committed. PCEN's gain-tracking pinned
on a level-stepped input. README section added and the primitive count bumped,
per the existing checklist.

### M2 — Promote the inference kernels *(DspTap · MuTap)*

Lift the dense and GRU arithmetic out of `nn_suppressor.h` into `tap::dsp::nn`,
add depthwise-separable convolution and a streaming activation cache, and
refactor `nn_suppressor` to consume the promoted kernels. Pure refactor on
MuTap's side — no behaviour change intended.

This milestone most repays being done early and most punishes being done late:
every subsequent stage builds on kernels a shipping, tested object already
exercises.

**Pass:** `test_nn_suppressor.cpp` and `tools/ml/test_parity.py` pass unchanged,
and the M55 instruction count for the suppressor does not regress. A behaviour
change here shows up as a parity failure, which is exactly what that test is
for.

### M3 — Corpus and dataset builder *(tools/ml)*

A `kws_features.py` declared as source of truth alongside the existing
`features.py`; TTS positive synthesis via Piper; augmentation over MUSAN and
SLR28; hard-negative mining from MSWC by phonetic edit distance; bulk negatives
from Common Voice and AMI. One command rebuilds the dataset from a committed
manifest.

Deliverable alongside the code: a **dataset card** in the shape of the existing
licensing map — per-corpus counts, hours, licences and attribution text.

**Pass:** dataset rebuilds reproducibly from the manifest on a clean checkout,
and the card accounts for every hour of audio with a licence. Recorded hold-out
set collected separately and never seen by training.

### M4 — The evaluation harness, before any model *(tools/ml · tests)*

DET curve tooling: false-rejection rate against false-accepts-per-hour over
hundreds of hours of negatives, with the per-utterance / per-hour asymmetry
built into the meter rather than bolted on. Threshold sweep, refractory
handling, committed report format.

Proven out against a deliberately trivial baseline — a band-energy threshold —
so the meter is known to work before it judges anything that matters.

**Pass:** the harness produces a sane DET curve for the trivial baseline:
near-total recall at an absurd false-accept rate, collapsing to zero recall as
the threshold tightens. If that curve looks wrong, the meter is wrong, and
finding out here is the entire point of the milestone.

### M5 — The spotter *(MuTap)*

`kws.h` — streaming DS-CNN or dilated TDNN over the M1 features and M2 kernels,
with `kws_geometry` carried in a `MUKW0001` weights header, posterior smoothing
and confidence combination as documented constants, and a refractory period.
PyTorch trainer and exporter beside the existing ones; parity test against the
trainer.

Architecture choice deferred to here rather than decided up front: with M4 in
place it becomes a measurement rather than an argument.

**Pass:** parity with the trainer to float tolerance, and a stated operating
point measured on the *recorded* hold-out set — not the synthetic one —
committed as a regression baseline the way the RIR fixtures are. Target to aim
at: ≥ 95 % recall at ≤ 1 false accept per hour. Whatever is actually achieved is
what gets written down.

### M6 — Embedded profile and the ratchet *(DspTap · MuTap)*

Front end and inference through the bare-metal M55 QEMU rig already in CI;
instruction count per 10 ms hop measured with `scripts/icount.py` and committed
as a budget CI ratchets against. Q15 front-end profile via `sample_traits.h` if
the M33 class is a real target; int8 inference path if the budget demands it.

**Pass:** on-target subset green under QEMU, instruction budget committed, and
any fixed-point profile agreeing with the float golden model within a stated,
tested tolerance.

### M7 — `mutap.wake~` *(MuTap-Max)*

One external, matching the sibling naming convention. Signal inlet; bang outlet
on detection; confidence float outlet for metering and threshold-setting by ear.
Attributes for threshold, refractory period, model path. Reference page and help
patcher demonstrating live detection with a visible confidence meter, so a user
can see the margin rather than guess at it.

**Pass:** loads and behaves correctly in Max on both platforms, macOS binary
universal, and validated against a live microphone at conversational distance —
not only against files.

> **Sequencing note.** M1 and M2 are worth doing regardless of whether the wake
> word ships. A mel front end is a primitive several Tap libraries would use,
> and consolidating the inference kernels removes a duplication that will
> otherwise appear the moment any second learned object is added. If the project
> stops after M2, the family is still better off — a useful property for a
> speculative effort to have.

## 7. CI and the ratchet

Four gates, three of which already exist and need only extending:

- **Contract tests** — typed GoogleTest batteries in DspTap, Catch2 in MuTap.
- **Python↔C++ parity** — trainer and shipping inference agree to float
  tolerance on committed fixture input. Catches the entire class of bug where a
  feature definition drifts between the two sides and the model quietly
  degrades.
- **On-target subset under QEMU** — the M55 rig MuTap already runs, extended to
  the front end and spotter.
- **Instruction-count ratchet** — a committed budget per 10 ms hop enforced by
  `scripts/icount.py`, so an innocuous-looking change that doubles the always-on
  cost is caught in review rather than on hardware.

Deliberately *not* in CI: the DET evaluation. It needs hundreds of hours of
audio and a trained model, so it belongs in the notebook verification layer —
executed, committed, re-executed when behaviour changes, exactly as
`notebooks/pitchshift.ipynb` is. The standing promise applies: every performance
claim measured, not remembered, traceable to the cell that produced it.

## 8. Risks, honestly

**The synthetic-positive gap.** The most likely failure is a model that scores
beautifully on TTS positives and disappoints on a real talker across a room.
Mitigation is structural rather than clever: the recorded hold-out set in M3,
and M5's pass criterion measured on it. If the gap is large, the answer is more
augmentation realism and more real recordings — both slow, neither surprising.

**The negative corpus is a storage and time problem.** Hundreds of hours of
negatives is hundreds of gigabytes decoded, and the remote development
containers already have limited disk and a network policy that blocks some
dataset hosts — the RIR-fixture note in HANDOFF.md hit exactly this. Assume
corpus assembly happens locally and enters the repo as manifests and derived
features, not audio.

**Charter drift.** MuTap's stated charter is adaptive filters for audio
cleaning, and its name is literally the LMS step size. A keyword spotter is
neither an adaptive filter nor audio cleaning. The counter-argument: MuTap is
already the family's speech library, already contains a learned non-adaptive
component, and already has the embedded rigs — and a second repository would
either duplicate that or pin MuTap anyway. Worth deciding on purpose rather than
by drift; see §9.

**Scope creep toward speaker verification.** Tier 3 of the cascade usually
carries speaker ID, and it is tempting. It is a separate project with its own
corpora, enrolment UX and privacy posture. Recommend explicitly out of scope.

**The plan's own weakest estimate.** M5's architecture and training loop is the
only milestone with genuine unknowns in it — everything else is either a known
refactor or a known harness. If a schedule is needed, treat M1–M4 as reasonably
estimable and M5 as the one to time-box with a decision point rather than
estimate.

## 9. Open decisions

**Which repository owns the spotter.** MuTap with a widened charter, or a new
sibling library pinning DspTap. M1 and M2 are correct under both, so this does
not block until M5.
*Recommend MuTap, charter restated as portable speech DSP for embedded targets.
Revisit only if a second consumer for keyword spotting appears in the family —
at which point the promotion pattern makes moving it cheap.*

**The wake phrase.** Needed at M0 and genuinely load-bearing: syllable count and
phonetic distinctiveness set a ceiling on the achievable DET curve.
*Yours to choose. Technical constraints: three to four syllables, unusual
phonotactics, not a substring of common English.*

**Model architecture.** DS-CNN, dilated TDNN, or a small GRU reusing the
promoted kernels directly.
*Defer to M5 and decide by measurement. The GRU path is cheapest to reach
because M2 delivers it; DS-CNN is the stronger default on microcontroller-class
targets. With M4 in place this is a measurement, not an argument.*

**Fixed-point front end.** Whether M6 includes a Q15 mel profile — real work,
and only pays off on M33-class parts without usable float.
*Defer until a target is named. The M55 has float; the existing `sample_traits`
convention makes this opt-in per primitive precisely so it can wait.*

**Whether to ship weights at all.** An alternative shape: ship `mutap.wake~` as
a runtime that loads a user-supplied model, with a documented training pipeline
and no bundled phrase. Sidesteps corpus assembly entirely and suits a Max
audience who may want their own phrase.
*Worth considering seriously as a first release, with a bundled model following
once the recorded evaluation set exists. It reorders the plan rather than
shortening it — M4 still has to happen before anyone can tell whether a trained
model is any good.*

---

### Provenance

- Proposal only — no code written, no repository changed apart from these docs.
- Milestone pass criteria state **targets, not measurements**. No number in this
  document is a measured result.
- Existing-asset descriptions were read from the working checkouts of DspTap,
  MuTap, TapTools and TapTools-Max on 4 September 2026.
- Corpus licences summarized from the companion briefing and require
  verification at the point of use.
