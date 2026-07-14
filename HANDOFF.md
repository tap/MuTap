# MuTap — Project Handoff

> Context brief for a Claude Code session. Captures decisions and technical direction from prior planning so the next session can start building without re-deriving anything. Can be adapted into a `CLAUDE.md` for persistent project context.
>
> **Rev 2** — refined against the actual `tap` ecosystem on disk (AmbiTap, AmbiTap-Max, SampleRateTap). Two former open decisions are now settled by sibling-repo convention, the single-repo assumption is corrected to the ecosystem's core/-Max split, and the build milestones are restructured into individually testable stages.

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

## Milestones (restructured: each stage independently testable)

The original "milestone 2 = the whole algorithm" was too big a bite. FDAF-PEM-AFROW decomposes into layers where each layer has a crisp pass/fail experiment; build them in this order and land regression fixtures with each.

**M0 — Scaffold `tap/MuTap`.** Sibling-repo layout: `include/mutap/`, `tests/`, `bench/`, `examples/`, `notebooks/`, `tools/capi/`, `cmake/`, `docs/`; MIT LICENSE; `STYLE.md` + `.clang-format`/`.clang-tidy` copied from a sibling; CI (`ci.yml`: build matrix, tests, format/tidy gates); README using the framing above. Vendor Ooura, port the real-FFT wrapper.

**M1 — Partitioned-block FDAF, open loop.** Overlap-save partitioned frequency-domain adaptive filter with NLMS update, `Sample`-templated, RT-contract-clean. *No PEM, no closed loop yet* — run it as a plain AEC: known synthetic/measured RIR as the true path, white and colored noise as input. **Pass:** misalignment (normalized coefficient error) converges to the level and at the rate the FDAF literature predicts; float32 vs float64 diffs are at rounding level.

**M2 — Closed-loop simulator + metrics.** The harness *is* a deliverable: true path `F` (RIR), forward path `G` (gain K, delay d, optional processing), near-end source injection, block-by-block loop. Metrics: howling onset detection, **maximum stable gain / added stable gain (ASG)**, ERLE, misalignment. **Pass:** reproduces the textbook failure — the naive M1 filter's estimate biases and the loop howls at modest K. That failure is kept as a permanent regression baseline; PEM's whole job is to beat it.

**M3 — PEM prewhitening.** Pluggable near-end predictor interface; first plug-in is the **speech cascade** (short-term LP + long-term/pitch predictor, per the papers). Autocorrelation → Levinson-Durbin with regularization and order limits; prefilter both `u` and `e`; frame-based per AFROW. Dedicated unit tests for the conditioning watch-list. **Pass:** on speech-like near-end in the M2 loop, bias is removed — misalignment keeps improving where M1 plateaued/diverged, and measured ASG gains match the ballpark of the FDAF-PEM-AFROW paper.

**M4 — Adaptation control + robustness.** IPC computation, IPC-gated freeze/slow adaptation, variable regularization. Double-talk and transient fixtures. **Pass:** survives simulated double-talk and near-end bursts without divergence; IPC behaves as the 2014 paper describes.

**M5 — Scaffold `tap/MuTap-Max` + first external.** Min-DevKit package mirroring AmbiTap-Max (MuTap as submodule); one external wrapping the M4 processor — working name **`mutap.defeed~`** (sibling convention: `<repo>.<verb>~`, e.g. `ambitap.encode~`; final name open). Attributes: forward-path delay report, adaptation freeze, filter length, predictor selection; help patcher demonstrating added-stable-gain in a live mic→speaker patch.

**M6+ (later):**
- **Music/tonal near-end predictor** (frequency-warped all-pole or pole-zero) as the second plug-in.
- **Cortex-M55 build** via SampleRateTap's QEMU AN547 rig; CMSIS-DSP mapping; instruction-count ratchet.
- **Hexagon build** — same float32 core; effort goes to VTCM/L2 data layout and vector mapping, not numeric conversion.
- **PEM-FD-Kalman** upgrade (v2 algorithm).

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

Resolved since rev 1: ~~license~~ (MIT, by convention), ~~core language~~ (header-only C++20, by convention), ~~milestone ordering~~ (core + closed-loop harness first, Max wrapper at M5 — and the wrapper lives in `tap/MuTap-Max`).

Still open:

- **First program-material target for tuning/demos** — speech vs music. The *implementation* order is fixed (speech predictor first, to validate against the literature; music predictor second), but which material the first real-room demo and default tuning target depends on the actual use case (live PA/instruments would pull the M6 music predictor forward, right after M4).
- **Max external naming** — `mutap.defeed~` is a placeholder; alternatives: `mutap.afc~`, `mutap.howl~`, `mutap.clean~`. Sibling convention is `<repo-lowercase>.<name>~`.
- **RIR fixtures** — which measured room impulse responses to standardize on for regression tests (e.g. a MYRiAD/openAIR room vs. self-measured), since the fixtures become permanent pass/fail baselines.
