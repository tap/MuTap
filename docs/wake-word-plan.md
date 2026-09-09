# Building `mutap.wake~` — implementation proposal

*Proposal, rev 3 — 8 September 2026; **in progress**: M0 decided, M1 done
(DspTap), M2 done (MuTap) and M3 done (DspTap · MuTap), each with a dated
record of measured numbers under its milestone in §6. Background and
corpus survey in [`wake-word-briefing.md`](wake-word-briefing.md). Rev 2
carried every amendment from the [adversarial audit](wake-word-audit.md) of
rev 1; rev 3 rewrites §6 M4 per the [M4 review](wake-word-m4-review.md) and
the two decisions of 8 September 2026 recorded in HANDOFF.md (hold-out audio
out of git; training features from the shipping front end through
`dsptap_py.LogMel`). M4 is staged M4a → M4b → M4c; the milestone numbering is
unchanged. The audit refers to rev 1's milestone numbers (the mapping is given
in §6); the review's line numbers refer to rev 2's M4 section, which rev 3
replaces.*

Formatted version: <https://claude.ai/code/artifact/341309d6-52dc-4d86-896c-2bcdd9534163>

---

## 1. The proposal

> Add a wake-word spotter to the Tap family by promoting two shared pieces down
> into DspTap, extending MuTap's existing `tools/ml` pipeline to a second task,
> and shipping one Max external. No new framework, no TFLite Micro dependency,
> no new repository until a second consumer justifies one.

The bet behind this plan, stated as the audit corrected it: **MuTap has
established the patterns and the rigs, not the oracles.** `nn_suppressor.h` is
a hand-written, allocation-free, `noexcept` GRU running on ERB band energies,
with its geometry carried as a value in a versioned weights header and a Python
feature module declared the single source of truth. Those patterns transfer
directly. What does *not* transfer — because it was never built for the learned
path — is the verification around it: the suppressor is instantiated only in
double everywhere it runs, has never executed on the Cortex-M55 rig, has no
instruction-count scenario, and its parity script is a hand-run, double-only,
random-weights check outside CI. A keyword spotter is the same machine with a
different head and a different loss, *once those oracles exist*. Building them
is a milestone of its own (M2), and it is worth doing whether or not the
spotter ships, because it is the verification the suppressor should already
have.

What does not exist yet at all: a mel/PCEN front end, a policy for the host
sample rate, a streaming convolutional inference path, a keyword corpus with
honest splits, and — the piece that actually decides whether this works — an
evaluation harness measuring false accepts per hour with a pass criterion that
can fail.

The plan sequences those, deliberately putting the oracles before the refactor
and the meter before the model.

## 2. Where it lands

```
MuTap-Max     mutap.afc~     mutap.aec~     [mutap.wake~]  ← new, M8
                                  ↑ submodule pin (transitive to DspTap)
MuTap         fd_kalman.h    nn_suppressor.h    [kws.h]    ← new, M6
              tools/ml  ─────────────────────  [tools/ml/kws]  ← extended, M4
                     │            ↑ submodule pin
                     │ refactored onto
                     ↓
DspTap        fft.h    yin.h    [log_mel.h]   [nn/]   [decimate.h]
                                 ↑ new, M1     ↑ promoted, M3   ↑ new, M1 (DspTap, per M0)
RatioTap      44.1 ↔ 48 only — composed by mutap.wake~ for 44.1 kHz hosts
```

**The load-bearing move is the promotion.** M3 lifts the dense/GRU arithmetic
out of `nn_suppressor.h` into a shared `tap::dsp::nn` and refactors the
suppressor to consume it — so the spotter inherits kernels a shipping object
exercises, and the family gains one inference substrate instead of two. This is
the same promotion the family has already run three times: `fft.h` out of MuTap
and AmbiTap's duplicate copies, the FIR substrate from SampleRateTap, `yin.h`
out of the TapTools pitchaccum kernel.

The promotion implies a three-repo sequence — DspTap PR, MuTap pin bump,
MuTap-Max pin bump (MuTap-Max reaches DspTap only transitively through MuTap)
— and an ownership rule: the weights format, its loader and dimension
validation stay MuTap-owned; DspTap holds only arithmetic over caller-provided
spans. MUNN0002 images are unaffected.

M1 and M3 belong unambiguously in DspTap, which is what keeps the repository
question for the spotter itself low-stakes. TapTools is the wrong home and
worth ruling out explicitly: its charter is musical, object-level kernels, and a
keyword spotter is neither.

## 3. What already exists, and what it saves

Read from the checkouts, with the audit's corrections applied.

| Existing asset | What it does today | Role for the spotter |
|---|---|---|
| `nn_suppressor.h` | Dense → GRU → dense over ERB band energies; allocation-free, noexcept; geometry carried by the weights. Every dot product accumulates in `Sample`. ~~Instantiated only as `<double>`~~ — *M2 typed its tests over float and double, pinned float vs double at −120 dB, and put the float profile on-target on M33, M55 and Hexagon.* | The inference kernels, promoted in M3 *after* M2 gives them a float oracle. The spotter is a different head on the same arithmetic. |
| `nn_geometry` / MUNN0002 | Model geometry as a value, validated at load; 16 kHz hop-64 and 48 kHz hop-256 served by one inference path. **Validator requires a power-of-two hop.** | Copy the geometry-as-value pattern for `kws_geometry` and a `MUKW0001` header; do *not* copy the hop constraint. Retraining at a new geometry must not require a code change. |
| `tools/ml/features.py` | Declared single source of truth for band definitions and normalization; documents that streaming-state parity depends on frame alignment. | The precedent for `kws_features.py` — with the ownership boundary of §5 so the two sources of truth cannot disagree. |
| `tools/ml/test_parity.py` | C++ inference against `nn.py`'s numpy reference, gain path only. ~~Double only, random weights, legacy 16 kHz geometry, hand-run, not a CI job~~ — *since M2 a CI job (`nn-parity`): both profiles, random weights at 16 k and 48 k plus the exported v2 weights, pinned at 1e-6.* | The shape of the test the spotter needs. M2 turns it into one: CI-run, both profiles, exported trainer weights, shipping geometry. |
| `tools/ml/README.md` | The licensing map and measured benchmark that justified the hybrid design; files "int8 + CMSIS-NN for the M55 path" as next step. | The template for M4's dataset card, and a roadmap this plan must reconcile with (§5, `tap::dsp::nn`). |
| Cortex-M55 QEMU rig + `scripts/icount.py` | On-target positive-filter test subset in CI; whole-binary instruction count per scenario with a ±3 % drift gate against `bench/baselines.json` (`fdkf`, `chain` at 16 k and 48 k, on m55 and hexagon). ~~No learned-path scenario; `NnSuppressor` not in the on-target filter~~ — *M2 added the `nn_suppressor` scenario at both geometries, the M33 leg, and the float suppressor suite to every on-target filter.* | The rig the embedded profile runs on, once M2 adds the learned scenarios. The ratchet is a drift gate; the budget is a separate absolute assertion (§7). |
| DspTap `fft.h` backend pattern | Ooura golden model; CMSIS-Helium and vDSP float32 backends re-presenting the exact contract, certified by parity tests. | The mel front end's FFT, and the rule for any accelerated NN backend: optional, opt-in, parity-pinned against the scalar golden path. |
| RatioTap | Synchronous 44.1 ↔ 48 kHz, one rational pair by charter ("no other ratios"), compile-time direction type, Kaiser prototype over the DspTap substrate, instruction-count ratchet on M33/M55/Hexagon. SampleRateTap beside it is near-unity async only. | Composed by `mutap.wake~` for 44.1 kHz hosts (44.1 → 48, then 3:1 to 16 kHz), and the *design template* for the decimator: fixed ratios as types, speed-first profiles, ratchet-gated. |
| DspTap `sample_traits.h`, `kaiser.h`, `fir_kernels.h` | The FIR substrate: float / Q15 / Q31 format core with documented Q-format ladders, Kaiser prototype design, dot kernels. No fixed-point FFT; the rate converters themselves live in SampleRateTap and RatioTap. | The *convention* for any later Q15 front end — not a fixed-point front end in itself — and the substrate the integer-ratio decimator of M1 is built on, exactly as RatioTap builds on it. |

**What this rules out:** TFLite Micro and ONNX runtime as dependencies. The
family's demonstrated position is hand-written inference against a documented
numeric contract, and a spotter is small enough — tens of thousands of
parameters — that this stays the cheaper option. Accelerated backends (CMSIS-NN,
Helium intrinsics) are *not* ruled out: they follow the `fft.h` rule — a scalar
golden kernel, an optional backend behind the same contract, parity tests
between them — which keeps the suppressor's filed int8 + CMSIS-NN follow-up
reachable rather than foreclosed.

## 4. Licensing map

Same exercise `tools/ml/README.md` ran for the suppressor, applied to a wake
word, corrected by the audit. Conclusion, narrowed: every input can be MIT,
Apache 2.0, CC0 or CC BY, **provided the TTS voices are lineage-verified** —
synthetic positives are the least clean input, not the cleanest.

| Component | Licence | Role | Shippable? |
|---|---|---|---|
| Speech Commands v2 | CC BY 4.0 | Benchmark; ingested at M4a; harness bring-up (M5 runs on this first) | yes, with attribution |
| MSWC | CC BY 4.0; no-reidentification term | Hard negatives, phonetic near-misses. **Force-aligned out of Common Voice** — splits must be speaker-disjoint across both. | yes, with attribution |
| Common Voice | CC0 licence; Mozilla Data Collective Data Consumer terms: account, no re-identification, no re-hosting outside the platform | Bulk negatives, by Data Collective dataset id | audio never redistributed; derived weights yes |
| AMI Meeting Corpus | CC BY 4.0 | Conversational negatives | yes, with attribution |
| MUSAN | CC BY 4.0 (per-directory `LICENSE` files) | Noise and speech partitions for augmentation; music per the M4 split axis — a train music share and the frozen, artist-disjoint eval music share, de-duplicated against FMA — because a Max user's room has music in it | yes, with per-file attribution |
| Free Music Archive (CC BY / CC0 subset only) | per track | Bulk music negatives beyond MUSAN's ~42 h, filtered to permissive tracks by the manifest | yes, with attribution per track |
| OpenSLR SLR28 | Apache 2.0 (simulated RIRs); real-RIR subset carries RWCP / REVERB / AIR third-party terms | Reverberation augmentation | simulated subset yes; **real subset only after its upstream terms are checked** |
| `piper-tts` ≥ 1.3 + `piper-sample-generator` | `piper-tts` GPL-3.0-or-later (espeak-ng embedded) — a build-time tool invoked as a subprocess, never imported by distributed code, never redistributed; its output is not a covered work. Sample generator MIT. **Voice models carry their training corpus's terms.** Verified at M0 from the per-voice model cards: most English Piper voices — `libritts_r`, `vctk`, `arctic`, `l2arctic`, `joe`, `kusal`, and the whole en_GB set except `cori` — are *fine-tuned from* the Lessac voice and inherit its research-only lineage; `hfc_*` and `semaine` are on NC datasets; `bryce`, `danny`, `kathleen`, `amy` have unverifiable base voices | Synthetic positives, TTS-derived | code yes, with the sample generator's own LibriTTS-R `.pt` generator **excluded: Lessac lineage** (the `libritts_r` checkpoint; loads with `weights_only=False`); voices: **`en_US-libritts-high`** (from scratch, LibriTTS train-clean-360, CC BY 4.0, 904 speakers), **`en_US-kristin-medium`** (from scratch, LibriVox, public domain), **`en_US-john-medium`** (fine-tuned from Kristin), **`en_GB-cori-high`** (from scratch, LibriVox, public domain) |
| openWakeWord / microWakeWord | Apache 2.0 (code); models CC BY-NC-SA 4.0; pre-computed feature sets CC BY-NC with WHAM / CHiME-6 upstream | Design reference only | design yes — **no code, models or feature sets imported**; every feature shard regenerated from manifest audio |
| Hey Snips, Qualcomm KSD | research / NC | Comparison only, if at all | **no** |
| Recorded hold-out set | ours; consent scope and permitted uses recorded per talker under the committed template (evaluation, indefinite retention, M7 replay, aggregate publication, pseudonymity, withdrawal from the tip — rows stay in history); optional per-talker CC BY 4.0 opt-in attributed to the collection | The evaluation set | audio never in git — FLAC in the off-git store, only `holdout.json` committed; an opted-in subset may be released later under CC BY 4.0 |
| PyTorch | BSD-3 | Trainer | never shipped — inference is dependency-free |
| Trained spotter weights | ours, derived from CC BY inputs | The deliverable | yes, **with attribution delivered**: a provenance block in the MUKW payload and a notices file in the Max package |

Patent posture: the shipped structure is a single-stage, single-channel
windowed classifier with posterior smoothing, reimplemented from the published
literature (Chen, Parada & Heigold 2014 onward) with no imported code and hence
no Apache patent grant to rely on. The 2014-onward publishers are also the
field's patent holders, so "comparable to the suppressor's" is not claimed. Not
a legal opinion; a commercial embedded product still warrants a
freedom-to-operate review.

## 5. The numeric contracts to pin

House rule (DspTap's wording): geometry fixed at construction, every buffer
allocated there; processing `noexcept` and allocation-free; each header
documents its packing, conventions, normalization and latency *as numbers*, and
the tests pin them. Changing a contract point is a breaking change for every
consumer and every trained model.

**Ownership rule, so two sources of truth cannot disagree.** `log_mel.h` owns
the *formula-level* contract: mel scale, filter normalization, window, FFT size
and zero-padding placement, frame alignment, bin-0 policy, pre-emphasis. Every
parameter a trainer might tune — band count, fmin/fmax, log floor, affine
normalization, all PCEN parameters — is *runtime geometry* carried by the MUKW
weights and validated at load. `kws_features.py` is the source of truth for
the *values*; `log_mel.h` for the *formulas*. Retraining never touches DspTap.

### `tap::dsp::basic_log_mel<Sample>`

- Internal sample rate, frame length, hop and FFT size **in samples at that
  rate** (the reference geometry: 16 kHz, 400 / 160 / 512), with the plain
  statement that frame ≠ 2·hop and the FFT is zero-padded to a power of two
  at the *end* of the frame.
- Streaming alignment: frame *t* ends at the newest sample; no centring, no
  prepended zero frame. Latency in samples = frame length, stated.
- Window (Hann or sqrt-Hann — stated), pre-emphasis coefficient (or 0, stated).
- Mel scale formula; Slaney or HTK normalization; unit-area or unit-peak
  triangles. These differ *silently*.
- Bin-0 policy: DC excluded (fmin > 0), stated as a number.
- Log floor **relative to the sample format**, and the affine normalization —
  the suppressor's `k_log_floor` / `k_shift` / `k_scale` is the pattern — as
  runtime parameters with defaults.
- PCEN: smoother coefficient, gain, bias, power and epsilon as runtime
  parameters; initial smoother state and `reset()` semantics as a contract
  point; plain-log as the default path.
- Float/double cross-precision tolerance as a **measured** number, on a test
  signal that excites every band.

### Host rate

- The spotter runs at one internal rate, 16 kHz. `kws.h` knows nothing about
  any other rate and **refuses** one rather than warning and proceeding; rate
  conversion is never at the MuTap level.
- Conversion lives in the Max layer as an option on `mutap.wake~`
  (`@resample`, on by default): when the host runs at 32 / 48 / 96 kHz the
  external decimates by 2 / 3 / 6 in front of the spotter; at 44.1 kHz it
  composes RatioTap's 44.1 → 48 with the 3:1 stage; at 16 kHz it passes
  through. Any other rate is refused. Where the interface supports 16 kHz,
  running the patch there costs nothing and the option is a no-op.
- None of the family's converters covers this today — `poly~` resamples by
  powers of two only, RatioTap is 44.1 ↔ 48 by charter, SampleRateTap is
  near-unity async — so the integer-ratio decimator is new work (M1), built
  on the same DspTap substrate RatioTap uses.

### `tap::dsp::decimate` *(DspTap, decided at M0)*

- Ratios 2, 3, 6 as compile-time types, RatioTap's pattern; Kaiser-designed
  polyphase FIR from `kaiser.h` over `fir_kernels.h`; stopband attenuation,
  passband edge and group delay as numbers per profile; latency in samples at
  the host rate; float golden model pinned against a committed scipy
  reference, as RatioTap's is.

### `tap::dsp::nn`

- Weight layout and gate ordering per layer, matching PyTorch's, exactly as
  `nn_suppressor_weights` already documents it.
- Accumulator precision per kernel, stated: today every dense and GRU dot
  product accumulates in `Sample`, and M3 preserves that. Any later change is
  a documented contract change with a parity delta. **No double anywhere in
  the hot path**, in any header on the spotter's route: the RP2350's Cortex-M33
  has a single-precision FPU only, so double is soft-float there.
- Streaming convolution: the activation cache's size and the causal delay per
  layer as numbers; equivalence to the non-streaming forward pass pinned by a
  torch-versus-streaming fixture.
- The `fft.h` backend rule: scalar golden kernel; any accelerated backend is
  opt-in and must re-present the exact contract, parity-tested. For an int8
  backend: per-channel vs per-tensor scales, the single rounding point,
  saturation — in the manner `sample_traits.h` sets out for Q15/Q31.

### `tap::mu::kws`

- Geometry as a value, carried by the weights and validated at load; the
  validator accepts any hop ≥ 1, not only powers of two.
- Label form the model was trained with (clip-level with tracked endpoint, or
  frame-aligned) and the head shape (single-class or per-word) — decided in M6
  from the label data M4 stores (keyword end sample and tolerance *T* in
  hops), recorded in the header together with *T*.
- Posterior smoothing window, confidence window, combination rule, refractory
  period — all as numbers, all settable, all defaulted, **all carried in the
  MUKW payload** alongside the threshold, so a retrain updates one place and
  the Max `@threshold` default reads from the model.
- The **front-end contract version** — the `log_mel` formula-contract version
  the model was trained against — carried in the MUKW payload and checked at
  load; a mismatch is refused with a message naming both versions. Under the
  runtime-first release users train against a pipeline that will move, and
  this is what keeps an old model from silently degrading on a new external.
- The **provenance block**: corpus-level source ids, the lock's `eval_set_id`
  per eval share and the hold-out set id, and the dataset card's hash and URI
  — never per-track text, which lives in `ATTRIBUTION.csv` and the M8 notices
  — about 2 KiB, with its own byte budget outside the §7 weights ceiling.
- Detection latency: hops from phrase end to the bang, as a number.
- Streaming-state policy (reset per window or carried) if a recurrent layer is
  present.
- The declared operating point: threshold, and the recall / false-accepts-per-
  hour pair it was measured at, on a **named** evaluation set — measured on the
  C++ engine through the C ABI, never on the Python model.
- Honest limits stated in the header: the phrase, the internal rate and host
  rates supported, the distances and SNRs evaluated over, the microphone path
  the operating point was measured through, single-channel, no beamforming, no
  pre-roll, no VAD gating, no low-power tier.
- A privacy statement, in the header and repeated in the help patcher and the
  Pico README: audio is processed in place and never stored or transmitted; the
  only outputs are the bang and the confidence value.

## 6. Milestones

Staged in the HANDOFF.md manner: each stage has a pass criterion that *can
fail* and a committed regression fixture. Rev 1's M2–M7 are rev 2's M3–M8; M2
is new.

### M0 — Decisions *(nothing built)*

Five decisions, each cheap now and expensive later.

1. **Repository.** MuTap with a widened charter (§9) or a new sibling. Affects
   M4/M5 file placement, so it blocks at M4, not M6.
2. **Host-rate policy.** Either (a) the spotter runs at a fixed internal
   16 kHz, `kws.h` refuses other rates, and `mutap.wake~` offers conversion
   as an option in the Max layer — a new integer-ratio decimator for 32 / 48 /
   96 kHz, composed with RatioTap for 44.1 kHz; or (b) a model per host rate
   on spectrally upsampled corpora, the suppressor's route, accepting that
   bands above 8 kHz are never excited in training and that 44.1 kHz needs a
   third model. Under (a), a sub-decision: the decimator's home — a DspTap
   primitive, or a sibling of RatioTap on the same substrate (RatioTap itself
   refuses other ratios by charter). Recommendation in §9.
3. **Release shape.** Runtime-first (a user-supplied model, no bundled phrase)
   or bundled weights. Decides whether the phrase blocks anything (§9).
4. **The wake phrase**, if bundled: three or four syllables, unusual
   phonotactics, distinctive stress, not a substring of common English. A
   poorly chosen phrase costs a permanent penalty on the DET curve that no
   training recovers.
5. **TTS voice lineage.** Each Piper voice's training corpus and its terms,
   written down; Lessac-derived voices excluded.

**Pass:** all five recorded in HANDOFF.md, with the phonetic-confusability
shortlist if a phrase is chosen, and the compute budget of M4 named (where
training runs, and a per-run time target).

**Decided, 4 September 2026** — recorded in HANDOFF.md; the arguments stay in
§9.

1. **Repository: MuTap**, charter restated as portable speech DSP for embedded
   targets alongside its adaptive-filter core.
2. **Host rate: fixed internal 16 kHz**; `kws.h` refuses other rates;
   `@resample` on `mutap.wake~` backed by a **DspTap `decimate.h`** (ratios 2,
   3, 6 as types, RatioTap's design path), composed with RatioTap for 44.1 kHz.
3. **Release shape: runtime-first**, no bundled phrase; the training guide is
   the primary document.
4. **Wake phrase: none shipped.** Development phrase for M4–M7 is a name-shaped
   Speech Commands word (`marvin`, with `sheila` as the near-miss check) for
   M5 bring-up, then a synthesized four-syllable phrase once Piper is in place
   at M4 — chosen then, recorded in the manifest, never shipped.
5. **TTS voices, lineage-verified from the Piper model cards:**
   `en_US-libritts-high` (trained from scratch on LibriTTS train-clean-360,
   CC BY 4.0, 904 speakers — the primary voice, because speaker diversity is
   the property that matters), `en_US-kristin-medium` and `en_GB-cori-high`
   (trained from scratch on LibriVox, public domain), `en_US-john-medium`
   (fine-tuned from Kristin). Excluded: every voice fine-tuned from Lessac
   (`libritts_r`, `vctk`, `arctic`, `l2arctic`, `joe`, `kusal`, `alan`,
   `alba`, `aru`, `northern_english_male`, `semaine`), the NC-dataset voices
   (`hfc_*`, `semaine`), the voices with unverifiable base models (`bryce`,
   `danny`, `kathleen`, `amy`), and the sample generator's bundled LibriTTS-R
   `.pt` generator (Lessac lineage). Attribution text for LibriTTS goes in
   the dataset card.

**Compute:** an Apple Silicon Mac for corpus assembly (M4, in a pinned
Python ≥ 3.10 environment; its system Python is 3.9.6) and training (M6,
where the trainer gains `--device mps`); per-run target under an hour on the
50 h selection subset — a named manifest subset of training negatives (M4).
The remote containers are for the C++, the harness and the toy CI fixture,
never the corpus.

### M1 — The mel front end, and the decimator *(DspTap)*

`include/tap/dsp/log_mel.h` — `basic_log_mel<Sample>` per the §5 contract, with
the double golden model and float32 embedded profile, riding the existing
`real_fft` with the FFT size decoupled from the hop. PCEN as a documented option
on the same object. Under route (a), `decimate.h` beside it — ratios 2, 3
and 6 as types, RatioTap's design path (Kaiser prototype, committed scipy
reference vectors, C ABI), in whichever home M0 chose.

**Before the header:** a numpy restatement of the contract, written first and
committed, is the reference M1 is scored against — the family has no
reference mel today, and M1's pass needs one that exists before the C++ does.
*As built:* it lives in DspTap as `tools/reference/make_frontend_reference.py`
(numpy only, generating `tests/reference/frontend_vectors.h`), not in MuTap,
so there is exactly one numpy copy of the formulas in the family. It is the
oracle: M4's `kws_features.py` computes training features through the
shipping front end (`dsptap_py.LogMel`, decided 8 September 2026) and imports
the reference through the submodule for its parity self-check — at the
reference geometry now, at the manifest geometry once the reference is
parameterized (a small DspTap follow-up, not an M4 prerequisite) — and for the
band-support assertion through its already-parameterized `mel_weights()`. The
reference's own docstring, which says MuTap's KWS feature module imports it,
describes that self-check import.

Typed GoogleTest battery pinning every §5 contract point; float/double
agreement measured, not assumed; C ABI exposure in `tools/capi` and the
`dsptap_py` bridge; README section and primitive count per the DspTap
checklist; `.clang-tidy` clean under the clang front end.

**Pass:** agreement with the committed numpy reference at a committed tolerance
on a fixed multi-band test signal; PCEN gain-tracking pinned on a level-stepped
input; PCEN reset semantics pinned; streaming output identical to whole-signal
output frame for frame (the alignment contract); decimator passband ripple,
stopband attenuation and latency pinned per ratio against the committed
reference, if built.

**Done, 4 September 2026** (DspTap branch `claude/mutap-wake-word-plan-2i63pe`).
Measured, not estimated: C++ vs numpy on the reference signal — double
1.5e-14 (log) / 3.4e-14 (PCEN), float 6.7e-7 / 5.3e-6, pinned at 1e-13 and
1.2e-5; float vs double 6.7e-7 / 5.3e-6, pinned at 2×; decimator float vs numpy
under 3e-5 for all six ratio × profile cases, Q15 vs float 1.6e-4 at half
scale, pinned at 3.2e-4. Tap counts searched and pinned: economy 81 / 121 / 239,
transparent 259 / 389 / 773. Contract version 1. Both primitives are in the C
ABI and the `dsptap_py` bridge (`LogMel`, `Decimator`); the bridge reproduces
the numpy reference to 1.5e-14. 139 DspTap tests green, clang-format and
clang-tidy clean. One correction to §5 discovered in testing: PCEN's steady
state keeps a deliberate E^(1−α) level dependence, so "gain tracking" is
pinned to the closed form (E/(ε+E)^α + δ)^r − δ^r rather than to unity.

### M2 — Oracles for the learned path *(MuTap)* — new in rev 2

Build the verification the suppressor should already have, before anything
refactors it:

- `nn_suppressor<float>` typed tests with a float/double cross-precision pin,
  and its entry in `test_float32.cpp`.
- A Cortex-M33 leg ported from RatioTap (`cmake/arm-cortex-m33-mps2.cmake`,
  `platform/mps2_an505`, `armv8m_startup.c`, the CI job and ratchet step),
  running the float-profile on-target subset under QEMU's mps2-an505 — the
  Raspberry Pi Pico 2 class of core, single-precision FPU, no FP64, no MVE —
  with its own `bench/baselines.json` entries. RatioTap has run this rig since
  its v0.1, so it is a port, not a design.
- `NnSuppressor` patterns added to every on-target positive filter (M33, M55,
  Hexagon).
- An `nn_suppressor` scenario in `bench/icount` at both shipping geometries,
  baselines seeded on m33, m55 and hexagon.
- `test_parity.py` promoted to a CI job: both profiles, **exported trainer
  weights** (`pretrained/suppressor_v2_48k.munn`) at the shipping 48 kHz
  geometry as well as random weights, run under `MUTAP_BUILD_ML_TOOLS=ON` in
  `ci.yml`.

**Pass:** every item above green in CI on the *unmodified* suppressor, and the
accumulator-precision contract of §5 written down as the observed behaviour.
This milestone is worth doing even if the project stops here.

**Done, 5 September 2026** (MuTap branch `claude/mutap-wake-word-plan-2i63pe`).
Measured, not estimated. `nn_suppressor` is now a typed suite over float and
double (unit-gain transparency pinned at −140 dB double / −120 dB float, the
comfort floor, echo-explained tracking at 1e-6 / 1e-4, the shipping 48 kHz
geometry, the chain composition); float vs double on the suppressor −129.4 dB,
pinned at −120; the learned chain in `test_float32.cpp` −91.4 dB, pinned at −85.
The parity driver runs both profiles (`nn_infer … --float`) and the CI job runs
six cases — random 16 k, random 48 k and the exported `suppressor_v2_48k.munn`,
each in double and float: double 1.6e-8 / 2.0e-8 / 2.9e-8, float 2.5e-7 /
2.0e-7 / 2.9e-7, both pinned at 1e-6. The accumulator contract is written into
the header docstring as observed: every dot product accumulates in `Sample`,
nothing in the float profile touches double. The M33 leg is ported from RatioTap
(`cmake/arm-cortex-m33-mps2.cmake`, `platform/mps2_an505.ld`, the shared
`armv8m_startup.c`, a `cortex-m33-qemu` CI job with the Ooura float32 FFT
pinned since there is no MVE) and the on-target filter on every leg carries the
float `nn_suppressor` suite, the cross-precision pin and the chain test. The
M33 leg also produced the first concrete cost figure for "double on the
RP2350" in this plan: the tonal PEM headline took 1085 s under qemu
mps2-an505 against 84 s on mps3-an547 and the selection timed out, so M2
first shipped with the four long float PEM scenarios excluded there. An
experiment then separated the two suspects — with the test harness's
deliberate double room simulation switched to float the scenario still took
772 s, so the harness was a third of the cost and the library two thirds: the
speech predictor's pitch search accumulated its normalized correlation in
double for every lag from 32 to 400 over a 1024-sample window, about a
million software double operations per 64-sample block on that core. The
follow-up landed with M3: the search accumulates in `Sample` (`lpc.h`), the
double profile is bit-for-bit unchanged, the float rows' measured numbers
are unchanged to the bisection's 0.5 dB quantum (kalman-loop tonal ASG
+7.81 dB before and after; PEM tonal +10.84 → +11.13; ERLE and misalignment
identical), and the full float selection now runs on the M33 in 174 s (the
tonal headline 31 s), so the exclusion is gone and the M33 leg runs exactly
the M55's selection. That is the reason §5's rule that nothing on the
wake-word path touches double is a budget rule, not a style rule. Layer 4
of `bench/icount` is the suppressor at both trained geometries with xorshift
weights; baselines seeded locally on m55 and m33 (the local ratchet reproduces
every committed m55 baseline to 0.00 %, so local seeding is trustworthy; CI
then reproduced both the m55 and the m33 sets at +0.00 %) and on hexagon from
the first CI run's `NO BASELINE` report, per the seeding procedure in
`bench/README.md`. Per-hop cost of the *existing* GRU
suppressor, whole-binary count divided by hops processed (setup included, so an
upper bound): m55 ≈ 383 k instructions/hop at 48 kHz (hop 256) and ≈ 271 k at
16 kHz (hop 64); m33 ≈ 711 k and ≈ 491 k. Against the wake-word ceiling of
150 k/hop in §7 that is the number M6's spotter has to beat by 2–5×, which is
why the spotter is a depthwise-separable head and not this GRU.

One contract defect found by the oracles, fixed on both sides: the ERB band
edges are built from `erb_inv(erb_rate(fs/2))`, and at 48 kHz that round trip
lands 2.2e-11 *below* fs/2 in libm (above it at 16 kHz; above at both in numpy),
so the strict `f < hi` band test dropped the Nyquist bin from the last band —
the C++ suppressor notched bin N/2 at 48 kHz while the numpy reference did
not. Before the fix the 48 kHz parity cases disagreed by 3e-2 (random) and
8e-3 (v2 model) in both profiles, and the shipping-geometry test read −27.9 dB
on unit gains. The top edge is now fs/2 exactly and the Nyquist bin belongs
to the last band, in `nn_suppressor.h` and `features.py` alike; this is the
kind of finding the audit predicted an oracle-less learned path would carry
unseen, and the reason §5's ownership rule exists.

### M3 — Promote the inference kernels *(DspTap · MuTap)*

Lift the dense and GRU arithmetic — only those — out of `nn_suppressor.h` into
`tap::dsp::nn`, and refactor `nn_suppressor` to consume it. Pure refactor on
MuTap's side; the float profile is now observed, so "no behaviour change" is
testable in both profiles. Full DspTap checklist for the new header.
Depthwise-separable convolution and the streaming activation cache arrive in
M6 with their consumer and their torch-versus-streaming fixture, not here.

**Pass:** the M2 battery passes unchanged in both profiles; the CI parity job
passes at float tolerance on exported weights; the `nn_suppressor` icount
scenario is within the drift gate on both targets; DspTap's typed battery for
`tap::dsp::nn` pins layout, gate order and accumulator precision; MuTap and
MuTap-Max pins bumped.

**Done, 5 September 2026** (DspTap and MuTap branches
`claude/mutap-wake-word-plan-2i63pe`; tap/DspTap#15). DspTap's seventh
primitive, `include/tap/dsp/nn.h`: `basic_dense<Sample>` (row-major
`[out x in]`, linear / tanh / sigmoid) and `basic_gru<Sample>` (PyTorch
`nn.GRU` convention, gates r, z, n), contract version 1, weights stored as
float32 and converted to `Sample` at the point of use, bias-first ascending
accumulation in `Sample`, moved-in owned weights, noexcept processing.
Measured: the GRU against an independent long-double restatement summing in
the opposite order 2.2e-16 double / 1.0e-7 float (pinned 1e-14 / 1e-6);
float vs double at the suppressor geometry 6.3e-7 state / 2.5e-7 gains
(pinned 3e-6 / 1e-6); 156 DspTap tests, warnings as errors, clang-tidy
clean. MuTap's `nn_suppressor` consumes the kernels (its weight arrays are
moved into them at construction, one copy in memory) and the promotion is
**bit-identical** in both profiles on the shipping v2 model against the
pre-refactor binary; the six parity cases pass at the M2 depths; the M2
battery passes unchanged; the `nn_suppressor` ratchet moved −0.12 % / −0.10 %
on m55 and −0.19 % / −0.11 % on m33 (16 k / 48 k), inside the gate, with
every other scenario unchanged to the instruction. Pins: MuTap's DspTap pin
points at the M3 tree (repointed at the identical tree on `main` once
tap/DspTap#15 merges); MuTap-Max's MuTap pin follows once MuTap's PR merges.

### M4 — Corpus, splits and dataset builder *(tools/ml/kws)*

Staged: **M4a** the builder and its contracts on a redistributable bring-up
corpus (unblocks M5); **M4b** the full corpus, phrase, splits, lock and card;
**M4c** the recorded hold-out, host path only. Training features come from the
**shipping front end** (decided 8 September 2026, recorded in HANDOFF.md):
`kws_features.py` holds the manifest's `log_mel_geometry` values as a frozen
dataclass (the `features.py` `Geometry` pattern) and drives `dsptap_py.LogMel`
through the DspTap C ABI, which takes every geometry field (1.5e-14 against the
numpy reference at M1). `make_frontend_reference.py` stays the oracle M1 built.
The module's self-check runs the bridge against it on M1's reference signal at
the reference geometry, pinned at a measured tolerance; once the small DspTap
follow-up that parameterizes the reference lands, the self-check also runs at
the manifest geometry. That follow-up is not an M4a prerequisite, so geometry
is tunable from the first build. Features are computed in double and stored
float32. Because `log_mel.h` silently zero-fills a band with no FFT bin inside
it, `kws_features.py` asserts band support at the chosen geometry through the
reference's parameterized `mel_weights()` — every band has a bin of nonzero
weight — before a frame is extracted. One builder, `kws_build.py`, rebuilds
everything from the manifest in stages; it never downloads.

**Phrase:** `marvin` / `sheila` from Speech Commands stay the M5 bring-up
positives, served from M4a's manifest. The development phrase is M4b's first
act, before the synthesis, mining and one-shot consented session that depend
on it: candidates (four syllables, unusual phonotactics, not a substring of
common English) are scored by a recorded confusability table — MSWC English
keywords within phone-level edit distance ≤ 1 and ≤ 2 of each word of the
phrase, from the mining tool below, with syllable count and stress — and the
chosen phrase and its table go in the manifest and the card. Never shipped
(M0).

**Sources and mining:** every input is a `sources[]` entry (Manifest, below)
with an `origin` — a URL plus the human obtaining step for a store-only
source, or a repository-relative path under `tools/ml/kws/fixtures/` for a
committed one — and a `redistributable` flag: an origin inside the repository
requires `redistributable: true`, and the builder refuses the manifest
otherwise, which is how the toy fixture is kept to Speech Commands, MUSAN and
SLR28 material; records (manifest rows, `holdout.json`) are not audio and are
committed regardless. `fetch` never downloads: for a store-only source it
verifies the archive a human placed under `<store>/archives/<source-id>/`, for
a repository-relative origin it copies the file there — both against the
manifest's sha256 and size — and every later stage refuses an unverified
archive. Common Voice is CC0 but account-gated on Mozilla Data Collective under
Data Consumer terms (no re-identification, no re-hosting outside the platform),
so the manifest records Data Collective dataset ids, the one-time account step
is a documented human action, and no Common Voice audio is ever committed or
redistributed. MSWC's own splits, speaker-disjoint only per keyword, are
discarded; AMI is taken from the single-distant-microphone array channel, the
headset mix optionally a second condition labelled per clip; MUSAN's noise and
speech partitions augment and its music follows the split axis below,
attributed per file from its per-directory `LICENSE` files; FMA is filtered to
CC BY / CC0 tracks by its per-track licence metadata (the release's
`tracks.csv`, a pinned member of FMA's `sources[]` entry, so a renamed or
altered file fails at `fetch`), licence recorded per track; SLR28 contributes
its simulated subset only (Apache 2.0), the real-RIR subset staying out of
`sources[]` until its RWCP / REVERB / AIR terms are checked (§4). Mining:
grapheme-to-phoneme by espeak-ng — Piper's phonemizer, in the tooling table —
with CMUdict where the word exists; distance is phone-level Levenshtein
matched per word of the phrase against MSWC's keywords; distance-0 hits are
excluded, a transcribed negative containing the whole phrase leaves every
negative pool, and the mined list and phonemizer version are stored in the
manifest so a lexicon update cannot move the negative set silently.

**Positives and augmentation:** positives are Piper synthesis over the four
lineage-verified voices (M0). `piper-tts` ≥ 1.3 is GPL-3.0-or-later (espeak-ng
embedded), runs ONNX Runtime on CPU, and is a build-time tool invoked as a
subprocess in a `--jobs` process pool — never imported by distributed code,
never redistributed, its output not a covered work; the sample generator's
bundled `.pt` generator is excluded (Lessac lineage). Every clip records voice
sha256, speaker id, length / noise / noise-w scales and text variant; speaker
ids are driven explicitly — *N* positives per libritts speaker id and a floor
share for the three single-speaker voices, both manifest fields. For every
voice / speaker id used for positives the builder synthesizes at least as many
**TTS negatives** — CC0 sentences, the phrase's words alone, near-miss phrases,
reversed word order — with the same draws, augmentation and split assignment,
so synthetic-versus-real cannot separate the classes. One augmentation
distribution applies to both classes with a stated dry share, its ranges
manifest fields: SNR against the same-split MUSAN noise share, MUSAN speech
and the train music share, gain, speed factor (the endpoint scaled with it),
RIR subset (within SLR28's simulated set) and RT60 range with the direct-path
index recorded, microphone-path model; *K* draws per positive, one clean plus
*M* noisy per negative. Augmentation is frozen into the store under the
manifest seed, each clip naming its resolved draw, not a bare seed; every draw
is a function of the manifest seed and the clip id alone — resolved in clip-id
order before dispatch, or by keyed hash, never from a process-shared RNG — so
the lock does not depend on `--jobs`, chunking or stage order; the trainer
applies feature-domain masking only. Front-end state: positives are featurized
embedded in ≥ 2 s of same-split negative material with one `reset()` per
mixture — PCEN primes its smoother from the first frame after `reset()`, time
constant −1/ln(1 − 0.025) ≈ 39.5 frames, a transient a running deployment
never sees — the context length a manifest field; the store holds the
plain-log path by default, and a PCEN tune is a rebuild. One resampler for
every source and for the 22,050 Hz TTS output (441:320 to 16 kHz):
`scipy.signal.resample_poly`, window parameters and scipy version in the lock;
the shipping `decimate.h` is never in the training path and the card says so.
The builder emits a class-balance table over source, real / synthetic, dry /
augmented, voice, speaker id and condition.

**Label data:** for every positive the builder stores the keyword end sample
*e* in the dry source — the end of the last 10 ms window above peak − *X* dB,
*X* measured on the four voices at build and recorded — and propagates it
analytically, never re-trimming after augmentation (an energy re-trim after
RIR convolution lands late by about (*X*/60)·RT60 — the whole 20-hop §7
ceiling already at *X* = 40 dB and RT60 = 0.3 s): *e*′ = round(*e*/*f*) under
speed factor *f*, *e*″ = *e*′ + *d* with *d* the RIR's first sample within
40 dB of its peak (the `make_rir_fixtures.py` onset rule), *e*‴ = *e*″ + the
stream offset. The endpoint is stored as a 16 kHz sample index; under
`log_mel.h`'s alignment (frame *t* is complete when sample (*t*+1)·160 − 1
arrives) an endpoint at sample *n* is first covered by frame ⌊*n*/160⌋, which
is its hop index *h*. Tolerance *T* in hops — the trim rule's spread across
the length-scale draws, measured at build — is a manifest field, and M5's hit
window is frames [*h* − *T*, *h* + 20 + *T*], so the §7 latency ceiling is a
scoring rule. Speech Commands clips get endpoints by the same trim rule;
hold-out utterances are annotated on the close-microphone take. Loss form and
head shape are M6's, decided from this label data; *T* goes into the MUKW
header beside the label form M6 records there.

**Splits:** train is what the trainer sees, the 50 h selection subset included;
dev is where architecture, threshold and decision-stage constants are chosen;
eval negatives and the hold-out are read once per release candidate with the
dev-chosen threshold. Assignment is by rule — a keyed hash with a committed
salt — per source: Common Voice `client_id`, MSWC clips joined to it by
filename into the pinned release (the `SPEAKER` column cross-checked, agreement
rate and unjoinable count recorded, unjoinable clips excluded from dev and
eval); AMI participant set, since participants recur across meetings, a meeting
whose participants span splits excluded; Piper voice + speaker id,
single-speaker voices placed whole, `kristin` and `john` together; Speech
Commands' official hash split; MUSAN noise files partitioned train / dev / eval
by file id — the eval noise share exists for the hold-out's SNR mixing and
never augments training or dev — and SLR28 simulated RIR files train / dev by
file id (the hold-out's rooms are real). **Music is its own split axis** keyed
by (source, artist, track): a train music share — additive background under
both classes at the stated SNR range, and music-only negative hours — and a
frozen eval music share, artist-disjoint, MUSAN's music de-duplicated against
FMA, from which it is partly drawn. The eval share exists because a Max user's
room has music playing in it and a false-accept rate measured on speech alone
says nothing about that. The eval negative set is named, never trained on, and
the FA/hour denominator for every number in this plan: **≥ 20 h speech and
≥ 20 h music**, fixed and identical for every run including selection, hours
counted from decoded durations by script. Beside every figure: with zero
events in *H* hours the one-sided 95 % bound is ln 20 / *H* ≈ 3.0 / *H* FA/h
(0.15 FA/h at 20 h), and at 1 FA/h the normal approximation to the 95 %
Poisson interval on the count is ±1.96 √*H*, i.e. ±1.96 / √*H* relative
(±44 % at 20 h, ±28 % at 50 h); M5's report prints the exact interval. TTS
negatives whose voice / speaker id falls in eval form a third share,
`eval-tts` — the synthetic-versus-real check on the eval side, M5's third
column — hours counted by the same script and recorded in the lock with its
own `eval_set_id` like the other two shares, outside the speech and music
floors, which count real material only; no floor is set for it. TTS positives
whose voice / speaker id falls in eval are the positive half of `eval-tts`,
counted in the lock under the same `eval_set_id`; M5 prints their recall
beside the hold-out's real recall as a descriptive figure — the §8
synthetic-positive gap made visible — never a pass, since M6's operating point
is measured on the hold-out alone. The splits are client-id-disjoint and
clip-id-disjoint (an approximation of speaker-disjoint, recorded in the card),
verified by `verify_splits.py` with five named rules: R1 client-id
disjointness across Common Voice and MSWC; R2 no decoded-PCM sha256 of any
dev, eval or hold-out file in a training or augmentation pool of any source,
and no training or dev noise file in any hold-out mixture (that half planted
as a lock row); R3 TTS voice + speaker-id disjointness; R4 AMI participant
disjointness; R5 music artist disjointness after de-duplication. Each rule has
a planted-violation fixture it must reject by name.

**Manifest:** `manifest.json` is a versioned document in three parts, two
committed and one emitted. `sources[]`: release or Data Collective id, origin,
sha256, size, licence id, attribution text, terms accepted and the date they
were verified, `redistributable`, and role (train / aug / dev / eval-speech /
eval-music / eval-noise / eval-tts / holdout). `recipe`: the geometry values
and stored path, `log_mel_contract_version` (from
`dsptap_log_mel_contract_version()`, today 1) kept separate from
`kws_features_version` (the module's schema), phrase and confusability table,
label rule and *T*, context length, the TTS block, mining method with
phonemizer version and mined list, augmentation policy with *K*, *M* and the
dry share, the split rule with its key per source and the committed salt,
exclusion rules, the resampler, and the 50 h selection subset by name. The
build-emitted `lock.json` beside the store: per clip decoded-PCM sha256,
length, split, label, endpoint and resolved draw; per hold-out mixed take the
noise file sha256 and level; per shard sha256 and frame count; per split ×
class hours and counts, and the class-balance table; per eval share (speech,
music, `eval-tts`) its `eval_set_id`, the sha256 of the sorted list of its
clip ids (source id + clip name) — a pure function of `sources[]` and
`recipe`, so it cannot change without a manifest hash change, and the
lock-reproduction pass catches any drift; the hold-out set id (Hold-out,
below); the toolchain — Python, numpy, scipy, soundfile, `piper-tts`,
espeak-ng and ONNX Runtime versions, the DspTap submodule commit and the
contract version — and the self-check's record, the geometries it ran at
(reference only, or reference and manifest) with the measured tolerance at
each. Lock fields are of two kinds. Identity fields — clip ids, split, label,
endpoint sample, resolved draw, frame count per shard, per split × class
counts and hours (from sample counts), the class-balance table, the
`eval_set_id`s, the hold-out set id, and the decoded-PCM sha256 of any source
stored as 16 kHz PCM — are reproduced exactly by every rebuild. Derived fields
— the feature values, the per-shard sha256, the decoded-PCM sha256 of any
lossy or resampled source, and the toolchain block — are reproduced exactly
only by a rebuild on the M0 Mac in the pinned environment; elsewhere the
features must agree within the tolerance written beside the assertion and the
rest are recorded for the report, never compared. The M6 exporter reads
`eval_set_id` and the hold-out set id from the lock into the MUKW provenance
block. Every shard embeds, beside its feature arrays,
`log_mel_contract_version`, the DspTap commit, the full geometry,
`kws_features_version` and the manifest hash — `make_dataset.py` writes
`geometry` into every `.npz`; that set extended — and the trainer refuses a
mismatch as `train_suppressor.py` refuses mixed geometries. No demographic
column (age, gender, accent) is copied from any source: the split key is the
only per-person field held.

**Feature store:** `--store` or `MUTAP_KWS_STORE`, no personal default. Tiers:
`<store>/archives/<source-id>/` (verified inputs, sha256-keyed); `<store>/pcm/`
(decoded 16 kHz clips keyed by archive digest + decoder + resampler ids —
mandatory for the eval split and, decoded from `<store>/holdout/`, for the
hold-out, since M5–M7 consume samples: the 40 h eval floor as int16 is
40 × 3600 × 16000 × 2 B = 4.6 GB); `<store>/features/<manifest-hash>/<split>/`
with `lock.json` beside them; `<store>/holdout/` (M4c). The manifest hash
covers `sources[]` and `recipe`, so any change yields a new directory and
stale shards are refused by hash. Budget, as arithmetic on stated inputs:
archives of the order of 180 GB (`fetch` records the exact figure); decoded
pcm at int16 for the 300 h corpus, 300 × 3600 × 16000 × 2 B = 34.56 GB, the
4.6 GB eval tier included; features 300 h × 3600 × 100 frames/s × 40 bands ×
4 B = 17.28 GB per stored variant (57.6 MB/h), and the frozen augmentation
stores 1 + *M* variants per negative and *K* per positive, so the feature
tier is 17.28 GB × (1 + *M*) plus the positives' draws — about 250 GB in all
at *M* = 1 against the Mac's ~377 GiB free; 50 h of features is 2.88 GB per
variant. `features/` and training-split `pcm/` are regenerable and may be
deleted, eval `pcm/` is regenerated before any DET, `archives/` and `holdout/`
never go. Ingestion: checksum before extraction, filtered tar members, voices
only as pinned `.onnx` + `.json`, no `torch.load` of anything.

**Dataset card:** `kws_dataset_card.py` renders `tools/ml/kws/DATASET.md` and
`ATTRIBUTION.csv` from the manifest and lock — nothing in the card is typed by
hand, so "every hour accounted for with a licence" is a script output:
per-source counts, hours and licences with their verification dates; MUSAN's
per-file and FMA's per-track attribution merged; the Data Collective and
no-reidentification terms; the partition table and the client-id
approximation; the voice lineage and build-time tooling tables (name, pinned
version, licence, role — `piper-tts` and espeak-ng GPL subprocess tools, never
redistributed, the same entry M4a adds to `THIRD_PARTY_NOTICES.md` so its
"nothing GPL-encumbered" sentence stays true and says why); the phrase,
class-balance and resampler statements, and the self-check statement (a
manifest geometry the reference cannot yet run at is covered by the bridge and
DspTap's typed `log_mel` battery alone, and the card says so); the toy set's
provenance; the hold-out's repository privacy statement, distinct from §5's
runtime one — what a `holdout.json` row holds (FLAC sha256, pseudonym, path,
distance, SNR, phrase, endpoint, form version; no audio, form or name), that a
hash reveals nothing without the file, and the withdrawal procedure with its
history limit; the hand-run full-rebuild record. Its test plants an unlicensed
clip and a CC BY-NC FMA track and requires both to fail. The M6 exporter's
provenance block and the M8 notices derive from `ATTRIBUTION.csv`; the MUKW
block itself is corpus-level ids plus the card's hash and URI (~2 KiB),
budgeted outside the §7 weights ceiling.

**Hold-out:** the talkers' audio **stays out of git** (decided 8 September
2026, recorded in HANDOFF.md). It lives as FLAC under `<store>/holdout/` or as
a private release asset; the repository commits only the record,
`holdout.json` — per utterance: sha256 of the FLAC file (the lock keeps the
decoded-PCM sha256 R2 uses), talker pseudonym, microphone path, distance, SNR,
phrase, endpoint sample; per talker: pseudonym, consent-form version,
permitted uses. The `holdout` row in `sources[]` carries as its sha256 the
hash of the sorted per-utterance FLAC hashes — the **hold-out set id** — so a
withdrawal changes it, and with it the manifest hash; every M6 and M7 figure
names the hold-out set id it was measured against and is re-measured, never
edited, after a withdrawal. The builder verifies the FLAC files under
`<store>/holdout/` against those rows as `fetch` verifies archives, and the M5
harness refuses a hold-out whose file hashes do not match — the RIR
precedent's provenance-or-refuse rule without its C-header encoding; no
licence is asserted over the audio, since none is redistributed by default.
Consent scope, in a committed template: evaluation use by the project,
indefinite retention, loudspeaker replay at M7, publication of aggregate
numbers, pseudonymous handling and a working withdrawal path (files deleted,
rows dropped, `holdout.json` re-versioned — the withdrawn rows, hashes and
conditions only, stay readable in the repository's history and forks, and the
template says so) — never public-repo permanence of the audio — plus one
optional per-talker opt-in, "may be published under CC BY 4.0, attributed to
the collection, not to me", so an opted-in subset can be released later
without re-consenting anyone. Signed forms live off-repo, referenced by
pseudonym and form version. Session protocol, host path only: two-channel
simultaneous capture through the host audio interface — a close microphone
(the dry take; endpoints annotated on it; the replay source at M7) and a room
microphone at each stated distance (the evaluation take); SNR realised by
post-hoc mixing of eval-noise-share files into the room take at metered levels
— each mixed take a builder output whose resolved draw (noise file sha256,
level) goes in the lock, not in `holdout.json` — stated in the header's
honest-limits block; talkers speak the M4b phrase and, optionally, non-target
read speech. The Pico-microphone path is produced at M7 by loudspeaker replay
of the dry take through the board's own microphone, which the consent covers.
Size is a store matter (200 utterances × 2 s × 16 kHz × 2 channels × 2 B =
25.6 MB as int16). The pilot is n = 1 (the owner); the **≥ 10 talkers / ≥ 200
utterances on the host path** target (the Pico path inherits the count by
replay at M7) and recall with its Wilson interval are M6's pass precondition —
190 of 200 hits reads 95.0 %, interval [91.0 %, 97.3 %], so per-condition
counts are descriptive, never a pass; the pilot — one talker, however many
utterances — proves the mechanics, not a number.

**Compute and environment:** the M0 Mac; a pinned Python ≥ 3.10 environment
(the system 3.9.6 cannot host current numpy, torch or onnxruntime) with every
package version in the lock; `--jobs` runs the stages in a process pool;
synthesis rate and per-stage wall times are measured at build and written into
M4b's Done record, not estimated here. The 50 h selection subset is a named
manifest subset of *training* negatives for selection runs; the final model
trains on the full training split. `--device mps` belongs to M6's trainer. The
remote containers run the C++, the harness and the toy CI job, never the
corpus.

**M4a — Builder, contracts and bring-up corpus** (unblocks M5; no prerequisite
beyond the current DspTap pin). **Deliverables:** `kws_features.py` (dataclass,
bridge, band-support assertion, self-check); the `manifest.json` schema and
`lock.json` format; `kws_build.py` with its seven subcommands
`fetch` · `decode` · `synth` · `augment` · `extract` · `shard` · `all`,
`--store` and `--jobs`, refusing unverified archives — `synth` exercised by
hand on the Mac over one pinned voice, since the toy fixture carries no voice,
its emitted lock rows carrying every per-clip TTS field and resolved draw, the
resampler record included, and that lock excerpt kept as the hand-run record
the card cites (audio in the store, never in git); `verify_splits.py` with
R1–R5 and their planted fixtures; `kws_dataset_card.py` with its planted-clip
test; Speech Commands v2 ingested (official split, trim-rule endpoints, licence
row); the toy fixture under `tools/ml/kws/fixtures/toy/` — a handful of Speech
Commands clips, a few seconds of MUSAN noise and music, one SLR28 simulated
RIR and an excerpt of the MSWC English keyword list as text (what the training
guide's toy mining run matches against), all CC BY 4.0 / Apache 2.0, ≤ 1 MB,
never Common Voice — with its committed expected lock and feature vectors; the
`THIRD_PARTY_NOTICES.md` entry for `piper-tts` / espeak-ng as build-time
subprocess tools never redistributed; the `kws-dataset` CI job on the
`nn-parity` pattern, with the DspTap C ABI built and the builder's Python
imports (scipy, soundfile) installed beside numpy. **Pass:** the toy manifest
rebuilds into an empty store in CI and reproduces the committed lock's
identity fields exactly and its features within the tolerance written beside
the assertion, and the toy lock is byte-identical between `--jobs 1` and
`--jobs 4` on the same host; a corrupted archive checksum is refused; each
planted leak fixture is rejected by rule name; the planted unlicensed clip and
NC track fail the card test; the self-check holds at the reference geometry; a
toy source whose origin is in the repository but whose `redistributable` is
false is refused. M5's bring-up starts on this output.

**M4b — The full corpus** (parallel with M5's bring-up). **Deliverables:** the
phrase and its table, first; Piper positives and TTS negatives; MSWC mining
with the mined list stored; Common Voice, AMI, MUSAN, FMA and SLR28's
simulated subset with roles and licence rows and the account step documented;
splits by the stated key per source; the eval split with its `eval_set_id` per
share; the 50 h selection subset; the frozen-augmentation store on the M0 Mac;
the generated `DATASET.md` and `ATTRIBUTION.csv`; measured stage wall times
and synthesis rate in the Done record. **Pass:** the full lock reproduces
exactly on the M0 Mac from verified archives into an empty store (elsewhere:
identity fields exactly, features within the measured tolerance); eval
speech and music hours ≥ 20 h each, `eval-tts` hours recorded, and the
class-balance table free of any class-pure attribute, all by script;
`verify_splits.py` green on all five rules over all sources; every clip maps to
a licensed, role-tagged source and the card is generated, not typed; label data
and *T* present for every positive; positive and TTS-negative counts per
voice / speaker id equal the manifest's *N* and floor share, with TTS
negatives ≥ positives for every voice / speaker id, checked by script from the
lock's per-clip voice sha256 and speaker id; phrase recorded and the mined set
non-empty and listed.

**M4c — The hold-out, host path** (before or alongside M6's training; nothing
in M5 waits on it). **Deliverables, in this order:** the consent template with
the scope above and the CC BY 4.0 opt-in, pseudonymous ids, off-repo forms, the
privacy statement in `DATASET.md`; `holdout.json`'s format and the
`<store>/holdout/` location; the session protocol; the recording and validation
scripts (rate, duration, clipping, endpoint present, a talker row keyed to
every file); planted record fixtures under `tools/ml/kws/fixtures/toy/holdout/`
— `holdout.json` variants with a missing talker row, an unknown consent-form
version, permitted uses lacking evaluation or M7 replay, and a row whose
sha256 does not match the toy clip it names, plus a lock fragment whose
hold-out mixed-take draw names a train noise file (R2's hold-out half) — no
talker audio, the stand-in file a toy Speech Commands clip already in the
fixture; the n = 1 pilot in the store with its rows committed and the
`holdout` row in `sources[]`. **Pass, in CI on the planted records:** the
builder refuses each variant by name and `verify_splits.py` rejects the
planted draw as R2. **By hand on the M0 Mac, recorded in the Done record:** the
pilot's FLAC hashes verify against `holdout.json` and the `holdout` row's
sha256 equals the hash of the committed rows' file hashes; its mixed takes
draw only from eval-noise files (R2 over the lock's draws); the M5 harness runs
it end to end — through M5's band-energy sanity baseline, or M6's engine
through the C ABI once it exists — forming every utterance's hit window
[*h* − *T*, *h* + 20 + *T*] from its `holdout.json` endpoint, consuming every
mixed take and reporting per condition (a mechanics check, never a recall
figure), and refuses a copy with one altered file; every committed row is
pseudonymous, under a template that states the withdrawn row's history
permanence, with any CC BY opt-in recorded per talker; and no audio, form or
name is tracked by git.

### M5 — The evaluation harness, before any model *(tools/ml · tests)*

DET curve tooling: false-rejection rate against false-accepts-per-hour, with the
scoring semantics **defined as numbers**: the hit window
[*h* − *T*, *h* + 20 + *T*] frames around each positive, *h* = ⌊*e*/160⌋
the hop index of its manifest endpoint sample *e*, *T* the manifest's
tolerance in hops and 20 the §7 latency ceiling; one hit per utterance;
false-accept merging under the refractory period; the hours denominator from
the eval negative set's decoded durations. Threshold sweep and a committed
report format that reports false accepts per hour **separately on speech, on
music and on TTS speech**, with `eval-tts` recall printed beside the hold-out's
recall as a descriptive gap figure, every FA/h figure printed with its hours
*H* and exact Poisson 95 % interval and, at zero events, the one-sided 95 %
upper bound ln 20 / *H* ≈ 3.0 / *H*.

Starts on **M4a's Speech Commands manifest** (`marvin` / `sheila` as
name-shaped positives, endpoints by M4's trim rule, the official hash split),
so it waits on neither gated corpora nor M4's phrase, and is pointed at M4b's
splits and `eval_set_id` when they exist. Refuses a hold-out whose file hashes
do not match the committed `holdout.json` rows. Runs the engine through the
C ABI so it measures shipping code; the Python model is for training-time
validation only.

**Pass — a test that can fail:** a planted-event oracle. Synthetic streams with
known positive endpoints and known false-accept placements, scored by the
harness against hand-computed recall and FA/hour, exact to the utterance; a
deliberately mis-accounted variant must be rejected. The trivial band-energy
baseline is run as a sanity curve, not as the pass.

### M6 — The spotter *(MuTap)*

`kws.h` per the §5 contract, over the M1 features and M3 kernels, with the
DS-conv / activation-cache kernels landing in `tap::dsp::nn` now, alongside
their consumer and a torch-versus-streaming fixture. `kws_geometry` in a
`MUKW0001` payload that also carries the decision-stage constants, threshold,
declared operating point and front-end contract version. PyTorch trainer and
exporter beside the existing ones, the exporter emitting both the `.mukw`
file and a C header of the same image for flash-resident targets, as
MuTap-Max's default-weights header already does for the suppressor; `kws_infer.cpp` parity driver and CMake target; `mutap_kws_*` C ABI
(create-from-weights, push block, posterior and confidence readout, detection
events with sample timestamps) and its `mutap_ffi` binding; `tools/ml/README.md`
re-scoped to two tasks.

Architecture — DS-CNN, dilated TDNN, or GRU — decided here by measurement on
**two axes**: the M5 DET on the manifest's dev split — never on the 50 h
selection subset, which is training data — and cost against the §7 ceilings.

**Pass:** parity with the trainer to float tolerance in both profiles, CI-run;
the streaming fixture passes; harness ↔ `kws.h` decision-stage parity pinned
on M5's planted streams; the trainer's validation set is the manifest's dev
split, never a random permutation; the exporter reads
`log_mel_contract_version` and *T* from the manifest and `eval_set_id` and
the hold-out set id from its lock into the MUKW payload; a stated operating
point measured **through the C ABI on the recorded hold-out set** —
precondition **≥ 10 talkers and ≥ 200 utterances on the host path**, recall
reported with its Wilson interval and per-condition counts descriptive —
committed as a regression baseline recorded against that hold-out set id;
attribution block present in the exporter output, derived from
`ATTRIBUTION.csv`. Target to aim at: ≥ 95 % recall at ≤ 1 false accept per
hour on the named eval negative set (≥ 20 h speech, ≥ 20 h music), on its
speech and its music share alike — a first-release target, looser than the
briefing's product figure. Whatever is achieved is what gets written down.

### M7 — Embedded profile and the budget *(DspTap · MuTap)*

Front end and spotter through the M33, M55 and Hexagon rigs; `kws` scenarios
added to `bench/icount` at the shipping geometry, baselines seeded on all
three; the per-hop figure derived by dividing the scenario's count by its hop
count. The §7 ceilings asserted as absolute checks beside the drift gate.
Float32 is the profile on every target, the RP2350 included; an int8 backend
or a Q15 front end only if the measured M33 count misses the ceiling *and* the
M6 architecture admits it (a Q15 front end is a new Q-format design, since
DspTap has no fixed-point FFT).

**On hardware — the named M33 target is the Raspberry Pi Pico 2 W.**
`examples/pico2w/` in MuTap: a Pico SDK application reading an **I²S MEMS
microphone through PIO** at 16 kHz, running the front end and spotter on one
of the two cores, pulsing a GPIO on detection and streaming the confidence over
UART. I²S rather than PDM on purpose: a PDM microphone needs its own
demodulation filter from a megahertz bitstream down to 16 kHz, and on an M33
that filter can cost more than the front end and spotter together. PDM support
is a follow-up with its own line in the §7 ceilings, not a free alternative.
The per-hop cycle count from the core's cycle counter is recorded beside the
QEMU instruction count. The board's radio is unused by the plan; a
detection-over-Wi-Fi demo is a natural follow-up, not a deliverable — and one
that must share the RP2350's three PIO blocks between the I²S microphone and
the wireless chip's PIO-driven SPI.

**Bench protocol, so the hardware pass can fail.** The hold-out's
**Pico-microphone path is produced here**: the M4c close-microphone dry takes
and one hour of the eval negatives, speech and music, played through a
loudspeaker at a stated distance and level into the board's own microphone
(the consent template covers replay), with the board's bang line logged; hits
and false accepts counted by the M5 harness's own scoring rules; recall and
FA/hour committed beside the host figures against the same hold-out set id,
noting that the 1 h bench bounds only ≤ 3 FA/h at zero events.

**Built in CI.** A `pico2w` job beside the QEMU legs: the `arm-none-eabi-gcc`
the M33/M55 legs already install, a Pico SDK checkout pinned by tag and
commit, `PICO_BOARD=pico2_w` and `PICO_PLATFORM=rp2350-arm-s`; it builds the
example, uploads the `.uf2` as a workflow artifact, and asserts the flash and
RAM footprint from the linker map against the §7 ceilings, so a change that
no longer fits the part fails in review. QEMU has no RP2350 board model, so
CI proves the build and the footprint; detection on the board is the bench
step of the pass criterion, and its cycle count is committed by hand. The UF2
is the family's first CI-produced binary deliverable, which is a precedent the
M8 release question can reuse.

**Pass:** on-target subsets green on all three rigs; both scenarios within the
drift gate and under the absolute ceilings on every target; the `pico2w` job
green with its UF2 uploaded and its footprint under the ceilings; the Pico 2 W
example's recall and FA/hour under the bench protocol committed, with the
hardware cycle count beside the QEMU figure; any accelerated or
fixed-point backend agreeing with the scalar golden path within a stated,
tested tolerance.

### M8 — `mutap.wake~` *(MuTap-Max)*

One external on the `mutap.aec~` pattern: signal inlet; bang outlet on
detection; confidence float outlet; attributes for threshold (defaulting from
the loaded model), refractory period, model path and `@resample`; with
`@resample` on, decimates 32 / 48 / 96 kHz hosts to the model's rate and
composes RatioTap for 44.1 kHz, reporting the added latency; with it off, or
at any other rate, refuses rather than warns; a no-model state that meters and
never fires, for the runtime-first shape; model loading off the audio thread —
a new model may change geometry and so re-prepare the front end, which
allocates, so the object is built on the main thread and swapped in by
pointer, as `mutap.aec~` does for its weights. Reference page and help patcher with
a visible confidence meter, demonstrating both a 16 kHz patch and a 48 kHz
patch with `@resample`. Package-level notices file
carrying the dataset card's attribution text.

"Ship" means what it means for the family today: source build and a Packages
symlink. A downloadable, signed package is a separate deliverable (tagged
build, artifact upload, macOS notarization) taken on only if wanted.

The customary docs stage: a book chapter for the spotter with numbers only from
the DET notebook and the tests; a MuTap README status row; a MuTap-Max README
roadmap row.

**Pass:** loads and behaves correctly in Max on both platforms, macOS binary
universal; validated against a live microphone at conversational distance both natively
at 16 kHz and at 48 kHz through `@resample`, not only against files; the help patcher's displayed threshold equals
the model's declared operating point.

### The documentation, across the milestones

Every effort in this family ends under HANDOFF's honesty rule: no number in
the book that a test or notebook does not measure, no attribute described
that does not exist. The spotter's documentation is spread over the
milestones rather than left to the end, and one surface is new to the family:
a **user guide to training a phrase**, which under the runtime-first release is
the product's primary document.

| Surface | Where | Milestone | Content |
|---|---|---|---|
| Header docstrings | `log_mel.h`, `decimate.h`, `nn/`, `kws.h` | M1, M3, M6 | Every §5 contract point as a number; the honest-limits block |
| DspTap README | `README.md` | M1, M3 | A section per primitive, count bumped, per the checklist |
| Dataset card | `tools/ml/kws/DATASET.md` + `ATTRIBUTION.csv`, generated by `kws_dataset_card.py` | M4 | Counts and hours per split × class, licences, attribution, Data Collective and no-reidentification terms, partition table, voice lineage table, build-time tooling table, phrase and confusability table, repository privacy statement, hand-run rebuild record |
| Pipeline reference | `tools/ml/README.md`, re-scoped | M6 | Both tasks; the spotter's benchmark beside the suppressor's |
| **Training guide** | `book/src/train-your-own-phrase.md` + `tools/ml/kws/README.md` | draft M6, final M8 | Choosing a phrase (syllables, confusables); verifying a voice's lineage; synthesizing positives; which negatives and how many; running the splits; training on a named device; reading a DET curve and choosing a threshold; exporting MUKW; loading it in `mutap.wake~`; recording a small hold-out of your own voice. **Its commands are a script, and CI runs that script on a toy corpus**, so the guide cannot drift from the pipeline. The toy run installs torch and one lineage-cleared Piper voice in its job, synthesizes a handful of clips, uses the committed M4a toy fixture (`tools/ml/kws/fixtures/toy/`, redistributable sources only) rather than downloading any corpus, in the `kws-dataset` CI job's manner, exercises the MSWC mining on it, and has a stated time budget. |
| Executed DET notebook | `notebooks/`, script-built | M5 onward | The performance record, through the C ABI |
| Book chapter | `book/src/wake-word.md` | M8 | The spotter's design and measured numbers |
| Max reference and help | `docs/mutap.wake~.maxref.xml`, `help/mutap.wake~.maxhelp` | M8 | Attributes, both host-rate patches, a tab pointing at the training guide |
| README status rows | MuTap, MuTap-Max | M8 | Status, roadmap, charter sentence |
| Package notices | MuTap-Max `NOTICES.md` | M8 | The dataset card's attribution text, where a user of the external sees it |
| Pico 2 W example README | `examples/pico2w/README.md` | M7 | Wiring, flashing the CI-built UF2, building locally against the Pico SDK, the measured cycle count |
| HANDOFF | `HANDOFF.md` | M0, M4, M8 | The five M0 decisions; the two M4 decisions of 8 September 2026 (hold-out audio out of git, only `holdout.json` committed; training features from the shipping front end through `dsptap_py.LogMel`); the dated done record per milestone; end state for the next session |

> **Sequencing note.** M1, M2 and M3 are worth doing regardless of whether the
> wake word ships. A mel front end is a primitive several Tap libraries would
> use; the learned suppressor gains the float, on-target and CI-parity
> verification it lacks today; and consolidating the inference kernels removes a
> duplication that will otherwise appear the moment any second learned object
> is added. If the project stops after M3, the family is still better off.

## 7. CI, the ratchet and the ceilings

Four gates. Two exist and need extending; two are built in M2 and M5.

- **Contract tests** — typed GoogleTest batteries in DspTap *and* MuTap
  (MuTap-Max reaches Catch2 only through min-api).
- **Python↔C++ parity** — built as a CI job in M2 for the suppressor, extended
  to the spotter in M6: both profiles, exported weights, shipping geometry.
- **On-target subsets** — the M33, M55 and Hexagon rigs, with the learned path
  added in M2 and the front end and spotter in M7.
- **Pico 2 W build** — the `pico2w` job of M7: build, UF2 artifact, and a
  footprint assertion from the linker map against the weight and state
  ceilings below.
- **Cost** — `scripts/icount.py` is a ±3 % *drift gate* against seeded
  baselines; it catches an innocuous change that doubles the always-on cost.
  It is not a budget. The budget is a separate absolute assertion on the same
  counts, with these **targets set now and ratified against the first M7
  measurement** (they are not measurements):

  | Ceiling | Target |
  |---|---|
  | Instructions per 10 ms hop, front end + spotter, scalar float, per target | ≤ 150 k on M55 and on M33 (10 % of one RP2350 core at 150 MHz) |
  | Weights | ≤ 64 KB |
  | Activation and streaming state | ≤ 32 KB |
  | Detection latency after phrase end | ≤ 20 hops (200 ms) |

**Targets.** The plan inherits MuTap's target set and adds the M33 leg the
family already runs elsewhere:

| Tier | Target | Profile | Verified by |
|---|---|---|---|
| Host, the shipping consumer | macOS universal (arm64 + x86_64), Windows; Linux in MuTap's CI matrix | double golden, float32 | Full batteries, sanitizers, MuTap-Max universal-binary check |
| Embedded, emulated in CI | Cortex-M55 (QEMU mps3-an547): FP32 + FP64 + Helium | float32 | On-target subset, icount ratchet, absolute ceilings |
| Embedded, emulated in CI | Qualcomm Hexagon (qemu-hexagon, HVX auto-vectorized) | float32 | On-target full suite, icount ratchet |
| Embedded, emulated in CI *(new, M2)* | Cortex-M33 (QEMU mps2-an505): FP32 only, no FP64, no MVE — the RP2350 class | float32 | Float-profile subset, icount ratchet, absolute ceilings |
| Embedded, on hardware *(M7)* | **Raspberry Pi Pico 2 W** (RP2350: 2 × Cortex-M33 at 150 MHz, 520 KB SRAM, 4 MB flash) | float32 | CI: `pico2w` job builds `examples/pico2w/` against a pinned Pico SDK, uploads the UF2, asserts flash and RAM footprint. Bench: detection, hardware cycle count beside the QEMU figure |

Deliberately *not* in CI: the DET evaluation. It needs hundreds of hours of
audio and a trained model, so it belongs in the notebook verification layer —
executed and committed, built by script in MuTap's convention, re-executed when
behaviour changes. It runs the C++ engine through the C ABI, and the standing
promise applies: every performance claim measured, not remembered, traceable to
the cell that produced it.

## 8. Risks, honestly

**The synthetic-positive gap.** The most likely failure is a model that scores
beautifully on TTS positives and disappoints on a real talker across a room.
Mitigation is structural: the specified, consented, recorded hold-out set in
M4, and M6's pass criterion measured on it. If the gap is large, the answer is
more augmentation realism and more real recordings — both slow, neither
surprising.

**The corpus is a disk, compute and time problem.** Hundreds of hours of
negatives is hundreds of gigabytes decoded and tens of gigabytes of features;
the reference trainer is CPU-only and took hours per hour of audio; TTS
synthesis is its own toolchain — `piper-tts` running the `.onnx` voices through
ONNX Runtime on CPU, GPL-3 and subprocess-only — with its rate measured at M4b.
Corpus assembly and training happen on named hardware (M0), the feature store
lives outside git (M4), and the repo carries manifests, the builder,
`holdout.json` and the executed notebook.

**Corpus and toolchain access drift.** Two facts moved under the plan between
rev 2 and the M4 review: the maintained Piper became GPL-3 and Common Voice
became account-gated on Mozilla Data Collective with a no-re-hosting clause.
Mitigation: release and Data Collective ids and archive checksums pinned in the
manifest, verified archives retained in the store, every licence row dated at
verification in the generated card, the toy CI fixture drawn only from
redistributable sources, the hold-out audio kept out of git so no term change
can strand a commit, and the builder never downloading — a source that changes
terms is noticed at `fetch`, not in a trained model.

**Charter drift.** MuTap's charter is adaptive filters for audio cleaning, and
its name is literally the LMS step size. A keyword spotter is neither. The
counter-argument: MuTap is already the family's speech library, already contains
a learned non-adaptive component, and already has the embedded rigs — a second
repository would either duplicate that or pin MuTap anyway. Worth deciding on
purpose rather than by drift; see §9. A widened charter also implies README and
description edits, listed in M8.

**Scope creep toward speaker verification.** Tier 3 of the cascade usually
carries speaker ID, and it is tempting. Separate project, separate corpora,
enrolment UX and privacy posture. Explicitly out of scope, as are pre-roll
buffering, VAD gating and multi-keyword models — named in the header's limits.

**The plan's own weakest estimate.** M6's architecture and training loop is the
only milestone with genuine unknowns. M1–M5 are known refactors and known
harnesses. If a schedule is needed, treat M6 as the one to time-box with a
decision point rather than estimate. M4's corpus acquisition and its consented
recording session share that property — one-time account steps, external
terms and other people's time — so they are time-boxed the same way, with
M4a's builder and toy fixture the part that never waits.

## 9. Decisions, with the arguments

All of these were decided on 4 September 2026; the record is in M0 and in
HANDOFF.md. The arguments are kept here.

**Which repository owns the spotter.** MuTap with a widened charter, or a new
sibling library pinning DspTap. M1–M3 are correct under both; blocks at M4.
*Recommend MuTap, charter restated as portable speech DSP for embedded targets —
alongside, not replacing, its adaptive-filter core. Revisit only if a second
consumer for keyword spotting appears in the family.*

**Host-rate policy.** Route (a), fixed internal 16 kHz with conversion as an
option in the Max layer, or route (b), a model per host rate.
*Recommend (a). One model, one corpus geometry, one FFT size, and the front end
never sees bands the training data cannot excite. Where the interface offers
16 kHz the patch can simply run there; otherwise `@resample` on `mutap.wake~`
does the work, never `kws.h`. The conversion has to be built: `poly~` is
powers of two only, RatioTap is 44.1 ↔ 48 by charter, SampleRateTap is
near-unity async. The missing piece is small — an integer-ratio decimator for
2, 3 and 6 — and 44.1 kHz falls out by composing RatioTap's 44.1 → 48 in front
of the 3:1 stage, so no rational 441:160 design is needed. For its home,
recommend a DspTap primitive built on the substrate RatioTap already uses,
with RatioTap's design path as the template; widening RatioTap would contradict
its stated identity, and a third converter repository is more than three
ratios warrant. The decimator is also the piece an embedded target with a
fixed ADC clock would need, so it is not Max-only work.*

**Release shape.** Runtime-first, or bundled weights.
*Recommend runtime-first for the first release. This is a shortening, not a
reorder: it removes the phrase as a blocking decision, the *shipped* dataset
card, the *shipped* hold-out set and the *shipped* operating point from the
critical path — M4c, M6 and M7 still require a recorded hold-out for the
development phrase (§8, the synthetic-positive gap) — and adds a no-model state
to the external and threshold semantics without a measured FA/hour. M4's
builder and M5's harness remain the product a user trains with, and the
training guide in §6 becomes its primary document. A bundled phrase follows
once a recorded evaluation set exists.*

**The wake phrase.** Load-bearing only under bundled weights.
*Yours to choose, when needed. Three to four syllables, unusual phonotactics,
not a substring of common English.*

**Model architecture.** DS-CNN, dilated TDNN, or a small GRU on the promoted
kernels.
*Defer to M6 and decide by measurement on both axes. The GRU is cheapest to
reach; DS-CNN is the stronger default on microcontroller-class targets and the
easier one to quantize if int8 is ever needed.*

**Compute.** Where training runs.
*Yours to name at M0. The plan assumes a GPU-equipped local machine or rented
hours; the remote containers are for the C++ and the harness, not the corpus.*

---

### Provenance

- Proposal only — no code written, no repository changed apart from these docs.
- Milestone pass criteria and the §7 ceilings state **targets, not
  measurements**. No number in this document is a measured result.
- Existing-asset descriptions were read from the working checkouts of DspTap,
  MuTap and MuTap-Max on 4 September 2026 and re-verified by the audit; TapTools
  and TapTools-Max were not available to the audit and are cited only for the
  promotion precedent.
- Corpus licences summarized from the companion briefing and the audit's
  licensing lens, with limited network access, and require verification at the
  point of use.
- Corpus licences and terms for Common Voice, MSWC, AMI, MUSAN, FMA, SLR28,
  Speech Commands and Piper were re-verified on the live pages by the M4 review
  between 5 and 8 September 2026; they remain a starting point for diligence at
  the point of use, not a legal opinion.
- Rev 2 superseded rev 1; the audit's issue numbers refer to rev 1's sections
  and milestones. Rev 3 supersedes rev 2; the M4 review's line numbers refer to
  rev 2's M4 section.
