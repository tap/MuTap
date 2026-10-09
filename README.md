# <picture><source media="(prefers-color-scheme: dark)" srcset=".github/icon-dark.svg"><img src=".github/icon-light.svg" width="40" height="40" alt="" align="top"></picture> MuTap

[![CI](https://github.com/tap/MuTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/MuTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

Portable adaptive filters for audio cleaning: acoustic feedback (howling)
suppression and echo cancellation, from Max/MSP to embedded DSP. Header-only
C++20.

The name is literal: **μ ("mu") is the LMS/NLMS step-size that adapts the
filter taps**.

The first tool is an acoustic feedback canceller built on **FDAF-PEM-AFROW**
(Gil-Cacho, van Waterschoot, Moonen, Jensen 2014; Rombouts, van Waterschoot,
Moonen 2007): a partitioned-block frequency-domain adaptive filter whose
update is decorrelated from the near-end source by prediction-error-method
prewhitening — the closed feedback loop biases any naive adaptive estimate,
and the PEM prewhitening is what removes that bias.

One float-parameterized core, three targets:

- **Desktop / Max/MSP** — float64 available; the golden model and correctness
  oracle. Max externals live in the sibling
  [MuTap-Max](https://github.com/tap/MuTap-Max) package repo.
- **ARM Cortex-M55** — float32; builds and passes the on-target test
  subset under QEMU (MPS3 AN547) in CI
- **Qualcomm Hexagon** — float32 with vector-float HVX (QCS8550-class
  cDSP); cross-builds with HVX enabled and passes the on-target test
  subset under QEMU user-mode emulation in CI

## Status

Milestones M0–M4 of [HANDOFF.md](HANDOFF.md) (the full technical plan,
milestone sequence and paper list) are done — the canceller core is
algorithmically complete. What exists today:

- `tap::mu::basic_real_fft<Sample>` — DspTap's real FFT (`tap::dsp`, re-exported
  by [`include/mutap/fft.h`](include/mutap/fft.h)) for float and double
  (`tap::mu::real_fft`, `tap::mu::real_fft32`): Ooura's packed spectrum layout and
  sign convention, run by DspTap's srdif engine (a split-radix DIF engine
  written from the literature; until tap/DspTap#42 a C++20 port of Ooura's
  `rdft`) and locked down by DspTap's tests.
  On the bare-metal Cortex-M55 the float32 transform runs a vendored CMSIS-DSP
  Helium FFT instead (~42% fewer chain instructions; FFT sizes 32 … 4096, which
  every MuTap constructor checks); Apple's vDSP is available on macOS but off
  in MuTap by default (root `CMakeLists.txt`); double always stays on the
  srdif engine, the golden model. See
  [`docs/optimization.md`](docs/optimization.md).
- `tap::mu::partitioned_fdaf<Sample>` — partitioned-block frequency-domain
  adaptive filter (overlap-save, per-bin NLMS update, optional gradient
  constraint): the identification core that PEM prewhitening will wrap.
  Validated open-loop against known impulse responses: on white noise the
  constrained filter converges below −100 dB misalignment in float64 and
  below −55 dB in float32, and the float32 run tracks the float64 golden
  model to single-precision depth ([`tests/test_fdaf.cpp`](tests/test_fdaf.cpp)).
- The closed-loop simulator and metrics
  ([`tests/support/closed_loop.h`](tests/support/closed_loop.h)): mic =
  near-end + true path × speaker, canceller subtraction, forward path with
  gain, delay and a speaker limit; howling detection and bisected
  maximum-stable-gain (MSG) measurement, plus the phase-exact (Nyquist)
  open-loop MSG. Every closed-loop claim below runs its feedback paths
  through a loudspeaker band (80 Hz highpass, 16 kHz lowpass;
  [`tests/support/rooms.h`](tests/support/rooms.h)) and is a median over
  five seed sets; the emulated targets run single-seed canaries.
  Measured MSG sits a median +0.79 dB above the −20·log₁₀ max|F(ω)| bound
  and +0.26 dB above the phase-exact limit. And the **M2 regression
  baseline** ([`tests/test_closed_loop.cpp`](tests/test_closed_loop.cpp)):
  the naive (un-prewhitened) FDAF adds a median +7.95 dB stable gain on
  white near-end, but its bias on tonal program material is
  *destabilizing* (median ASG −15 dB, the probe's floor — it howls at gains
  the open loop handles). Removing that failure is PEM prewhitening's job.

- **PEM prewhitening** — the actual feedback canceller.
  [`include/mutap/lpc.h`](include/mutap/lpc.h): autocorrelation and
  Levinson–Durbin with the conditioning guards (relative ridge, silence
  floor, early stop), plus the pluggable near-end models — `lpc_predictor`
  (short-term LP) and `speech_predictor` (the cascade: short-term LP +
  long-term pitch tap). [`include/mutap/pem_afc.h`](include/mutap/pem_afc.h):
  `tap::mu::pem_afc<Sample, Predictor>`, the FDAF-PEM-AFROW structure —
  cancellation runs on the raw signals, adaptation on the prewhitened pair.
  Measured in the M2 loop (converged at MSG−6 dB, medians over five seed
  sets): on the tonal material where the naive canceller howls at the
  probe's −15 dB floor *below* the open-loop MSG, PEM is stable above it
  with **ASG +9.39 dB (double) / +7.08 dB (float)**; speech-envelope
  material **+10.55 dB**; voiced (pitch-periodic) material **+3.90 dB**
  where naive howls; white +10.26 dB (naive: +7.95)
  ([`tests/test_pem_afc.cpp`](tests/test_pem_afc.cpp),
  [`tests/test_lpc.cpp`](tests/test_lpc.cpp)).

- **Adaptation control** (in `partitioned_fdaf`, so both cancellers get it):
  the **IPC** double-talk indicator (after Gil-Cacho et al. 2014, computed as
  a chance-corrected per-partition coherence — the estimated fraction of
  error power coherent with the input: measured ~0.6 while unconverged, 0.00
  converged, 0.02 under double-talk; and the paper's headline reproduces:
  raw-pair IPC 0.82 in the tonal closed loop vs 0.08 prewhitened, medians),
  **IPC-scaled stepping** (μ·IPC², a Wiener-flavored variable step) plus an
  **instantaneous transient gate**, which together contain a +20 dB near-end
  burst that blows up the ungated loop (median worst block RMS 60.6 vs
  27,076 in double, 67.5 vs 30,248 in float; the gated loop stays under
  1000 in 5 of 5 seed sets in both precisions, 10 of 10 over sets 0..9 —
  containment is likely, not guaranteed: rooms drawn from another standard
  library's generator left 2 of 5 float trajectories uncontained; each
  layer alone is insufficient), and
  **variable regularization** (per-bin normalizer floored at a fraction of
  the mean bin power), making identification scale-invariant from 10⁻⁵× to
  10³× input scale where a fixed epsilon degrades by ~150 dB
  ([`tests/test_adaptation_control.cpp`](tests/test_adaptation_control.cpp)).
  A side-effect worth knowing: variable regularization alone softens the
  naive canceller's tonal bias (ASG −12 → −3.5 dB) — mitigation, not the
  fix; the M2/M3 baseline tests pin the M1-era config explicitly.

- **The Max/MSP external** — `mutap.afc~` in
  [MuTap-Max](https://github.com/tap/MuTap-Max) (Min-DevKit package, MuTap
  as a submodule): universal macOS `.mxo` + Windows `.mxe64` built in CI.
- **The music/tonal near-end predictor** —
  `tap::mu::warped_lpc_predictor<Sample>`: frequency-warped LP (unit delays
  replaced by first-order allpass sections), warped autocorrelation + the
  same ridge-guarded Levinson–Durbin, applied as an unconditionally stable
  tapped allpass chain. At λ = 0 it reduces *exactly* to the plain
  predictor. The test material is a low chord: three incommensurate
  fundamentals, a dozen partials below 500 Hz — it defeats both the pitch
  tap and moderate-order plain LP. **Pair it with IPC-scaled stepping**
  (`fdaf.ipc_step_scaling = true`): the warped whitener notches the
  partials so hard that without the IPC scale the converged closed-loop
  update runs away on some rooms (howls 15 dB *below* the open-loop MSG,
  ~1 room in 5 — found by evaluating across rooms after a single-room
  first cut overfit). With it, the test suite's five rooms measure
  **+7.19…+9.38 dB** on the chord (per-room medians over five seed sets,
  band-limited paths), with no collapse: on room 9, over ten seed sets,
  warped+IPC never reached the −15 dB probe floor (a collapse, as counted
  here), where the speech cascade at its own defaults did in 1 of 10 (a
  one-off ten-set sweep, 2026-09-25; the gated test checks the five-set
  medians). The
  demo notebook repeats the sweep on rooms from its own (numpy) generator
  family. Speech-envelope material improves to +17.81 dB. Defaults
  (λ = 0.5, order 16) are the sweep's best worst-case; Bark-scale λ = 0.766/order 24 trades a slightly better mean
  for a weaker floor at ~1.5× the cost. Select it with
  `tap::mu::pem_afc<Sample, tap::mu::warped_lpc_predictor<Sample>>`.
- **The Cortex-M55 build** — the first embedded target: bare metal
  (newlib + semihosting) on QEMU's MPS3 AN547 board model, platform rig
  (Armv8-M startup, linker script, one-shot gtest harness) ported from
  SampleRateTap ([`platform/`](platform/),
  [`cmake/arm-cortex-m55-mps3.cmake`](cmake/arm-cortex-m55-mps3.cmake)).
  128 tests run on-target (the baked selection in
  [`tests/bare_metal_main.cpp`](tests/bare_metal_main.cpp), 9 October 2026):
  the float32 typed suites (the embedded profile),
  the LP conditioning suite, the float closed-loop canaries (single-seed
  checks that target arithmetic tracks the host, including the PEM tonal
  and burst-gating scenarios; the acoustic claims are host-side), and the
  float-tracks-double oracle check — with double as soft-float (the M55
  FPU is single-precision only).
- **The Hexagon build** — the third target: hexagon-unknown-linux-musl via
  the Codelinaro clang toolchain with **HVX auto-vectorization on**
  (128-byte vectors, 32 fp32 lanes), statically linked and run under
  qemu-hexagon user-mode emulation in CI
  ([`cmake/hexagon-linux-musl.cmake`](cmake/hexagon-linux-musl.cmake)).
  A hosted Linux target needs no platform rig — stock gtest, ctest and
  exit codes work unchanged. Per-push CI runs the same emulation-sized
  selection as the M55 leg (128 tests, ~8 min of TCG); the full suite
  (439 tests on the host build, 9 October 2026), double-typed adaptive
  suites included, was validated once on the ISA when it was 74 tests
  (double is hardware on the Hexagon scalar core). What
  this leg deliberately does not cover: VTCM placement, L2 streaming
  layout and FastRPC offload need the proprietary Hexagon SDK and real
  hardware (HANDOFF.md pins the layout targets).

- **The PEM-FD-Kalman core (v2)** — `tap::mu::partitioned_fdkf<Sample>`
  ([`include/mutap/fd_kalman.h`](include/mutap/fd_kalman.h)): the NLMS
  update replaced by a diagonalized partitioned-block frequency-domain
  Kalman filter (Enzner & Vary 2006; Bernardi et al.'s PEM variant), as a
  drop-in core for the same FDAF-PEM-AFROW structure —
  `tap::mu::pem_afc<Sample, Predictor, tap::mu::partitioned_fdkf<Sample>>`.
  The per-bin state uncertainty and near-end PSD replace the entire
  adaptation-control stack (no step size, no IPC options), and every claim
  is measured: at 0 dB SNR it beats both ends of the NLMS speed/depth
  tradeoff at once (−15.5 dB by block 300 vs μ=0.5's −5.6 and μ=0.1's
  slow start); in the closed loop with **zero knobs** it measures tonal ASG
  +7.19/+7.19 dB double/float (medians; the tuned NLMS stack +9.39/+7.08),
  **+21.56 dB** on speech-envelope near-end (the probe's ceiling is +25;
  NLMS stack +10.55), and holds **+10.62…+14.69 dB across all the music
  rooms with no IPC pairing** (per-room medians; the speech cascade +11.25
  on room 9). A +20 dB near-end burst is survived ungated
  (excursion ~2 dB; ungated NLMS is wrecked); the opt-in transient floor contains it to gated-NLMS quality
  at a measured ~2–6 dB tonal-ASG cost, which is why it defaults off
  ([`tests/test_fd_kalman.cpp`](tests/test_fd_kalman.cpp)).
- **RIR fixtures** — three physically-modeled rooms (image-source method,
  documented geometry, deterministic; [`tools/fixtures/`](tools/fixtures/))
  committed as permanent closed-loop baselines with real early-reflection
  structure. Measured on them (block 64, 16 partitions, speech-envelope
  material, band-limited, medians over five seed sets): **Kalman
  +18.12…+19.38 dB ASG vs the classic stack's +11.25** on the studio
  ([`tests/test_rir_fixtures.cpp`](tests/test_rir_fixtures.cpp)).
  Measured rooms (academic datasets or your own sweeps) join with one
  command: `make_rir_fixtures.py --from-wav`.
- **Open-loop echo cancellation (AEC)** — the same engines with the loop
  cut open: a clean far-end reference exists, and
  `pem_afc::process_block(x, y, e)` is already the AEC call (the 2014
  Gil-Cacho paper the PEM structure implements is an open-loop double-talk
  framework). The open-loop harness
  ([`tests/support/echo_scenario.h`](tests/support/echo_scenario.h))
  reports both the observable ERLE and the true residual-echo suppression
  the simulation alone can measure. Pinned behavior (studio fixture room,
  0 dB double-talk, medians over seeds;
  [`tests/test_aec.cpp`](tests/test_aec.cpp)): double-talk kicks the naive
  NLMS estimate *past useless* (post-double-talk misalignment **+3.4 dB** —
  subtracting its echo estimate adds energy) while **PEM holds −8.5 dB and
  the Kalman core −13.3 dB with 13.4 dB echo suppression through the
  double-talk**, zero adaptation-control config. The trade the default
  engine weighs: naive NLMS posts the best single-talk ERLE on colored
  far-end (~44 dB, excitation-weighted) vs PEM's ~20 dB with uniform
  −20 dB depth; the gated M4 stack freezes flat through double-talk
  (kick ±0.1 dB) but stays excitation-shallow. On music-material
  double-talk the warped predictor beat the speech cascade's suppression
  in **all 18 room × seed pairs** measured (per-room medians 17.3…19.6 vs
  15.2…16.7 dB, Kalman core).
- **Residual-echo post-filter + comfort noise**
  ([`mutap/postfilter.h`](include/mutap/postfilter.h)) — the Stage 2
  deliverable of the [ITU compliance plan](docs/itu-compliance.md):
  a per-bin Wiener suppressor driven by mic-vs-echo-estimate coherence
  gating a learned leakage estimate, comfort noise matched to the
  near-end floor by two-window minimum statistics, and `tap::mu::aec_chain`
  composing it with a linear canceller (default: the **raw** FD-Kalman
  core — open-loop AEC has an exogenous far end, so PEM's decorrelation
  buys nothing and its predictor refit floors misalignment near −20 dB
  where the raw core reaches −75 dBm0(A) bare). Measured on the ITU
  battery ([`tests/test_postfilter.cpp`](tests/test_postfilter.cpp)):
  single-talk residual **−79.9/−88.9 dBm0(A)** (cabin/studio; the P.1120
  clause wants < −58, our margin target < −64), double-talk near-end
  attenuation **1.05 dB** (clause ≤ 3, target ≤ 1.5), double-talk echo
  loss **≥ 34.1 dB in every band** (clause ≥ 27, target ≥ 33), comfort
  noise matched **−1.2 dB / ≤ 1.7 dB per band** (clause +2/−5, half-mask
  spectrum), noise pumping 3.3 dB, near-end build-up 20.9 ms.
- **The Tier A ITU compliance suite** (Stage 3) — one test per matrix
  row at **both required rates (48 and 16 kHz)** on one pinned chain
  configuration: echo battery (TCL 68/81 dB vs the 46 dB clause,
  spectral masks +13 dB clear, convergence and time-variant-path rows),
  double-talk battery (P.340 **Category 1 full duplex**: echo loss
  ≥ 37 dB in every band during double talk at ≤ 1 dB near-end cost),
  switching/comfort-noise/pumping dynamics, and the **Annex E stability
  sweep: stable at 0 dB far-end ERL — the sweep's floor — at both
  rates**. Every ITU requirement met; the handful of self-imposed
  half-margin targets missed at 16 kHz are documented regression gates,
  never silent passes ([`docs/itu-compliance.md`](docs/itu-compliance.md)
  "Stage 3 delivered" carries the full measured table). The chain grew
  an initial receive guard (switched send loss until convergence
  certifies) because no echo-estimate-referenced suppressor can see
  echo while the canceller is fresh — the convergence-in-noise mask is
  unmeetable without it.
- **The Tier B G.168-adapted battery** — G.168's test structure
  transplanted onto acoustic paths (reported as *adapted*, the rec
  disclaims acoustic scope): all twelve rows pass at both rates —
  convergence masks with 20+ dB early loss, double-talk divergence
  bounded 24 dB inside the limit, leak rate that *improves* over 45 s
  of silence, tone stability cancelled to the numerical floor, comfort
  noise tracking steps within +-1.8 dB — plus the withdrawn G.167's
  figures as an informative row. The one deviation Stage 3b documented —
  slow deep re-convergence after *abrupt* path changes — is now closed
  for changes toward quieter/different paths by the **re-convergence
  rescue** (a one-shot uncertainty lift triggered by the over-explained
  echo ratio, the one signal double talk cannot fake): the swap rows
  measure 46/49 dB combined loss in [1,2] s and −96/−123 dBm0 deep
  steadies, at cold-start speed, with every other battery row
  bit-identical — and for changes toward **louder** paths by the
  **shadow comparator** (the classical dual-path answer as a second
  trigger: a small fast canceller runs alongside, and the rescue fires
  when it out-cancels the main — a performance comparison no double
  talk can fake, measured at zero false fires across the DT/noise
  batteries).
- **The compliance proof notebook** (Stage 4) —
  [`notebooks/itu_compliance.ipynb`](notebooks/itu_compliance.ipynb):
  requirement/measured/margin tables for every row above, convergence
  trajectories drawn against the recommendations' time masks, the
  double-talk timelines against the P.340 windows, and an honest figure
  of the re-convergence deviation. Nothing in it is pasted in: the first
  cell compiles [`tools/notebook/itu_dump.cpp`](tools/notebook/itu_dump.cpp)
  (`-DMUTAP_BUILD_ITU_DUMP=ON`) — a C++ battery reusing the test suite's
  own scenario machinery and meters — and re-measures everything live
  (~6 minutes, deterministic seeds).

- **The learned residual suppressor** — `tap::mu::nn_suppressor<Sample>`
  ([`include/mutap/nn_suppressor.h`](include/mutap/nn_suppressor.h)): a
  52k-parameter GRU predicting per-band gains on the linear canceller's
  output, composable as the chain's post stage
  (`tap::mu::aec_chain_nn`, [`include/mutap/nn_chain.h`](include/mutap/nn_chain.h))
  and exposed as `mutap.aec~ @postfilter 2`. Trained end-to-end on
  clean-licensed material by [`tools/ml/`](tools/ml/) (LibriSpeech
  CC BY 4.0 + synthesized scenarios through the real canceller), with
  geometry-parameterized weights (MUNN files) and Python/C++ parity
  pinned by [`tools/ml/test_parity.py`](tools/ml/test_parity.py).
  Measured (48 kHz speech, medians): single-talk ERLE 53.4 dB vs the
  classical chain's 34.5 at better near-end transparency; the classical
  engine keeps the double-talk lead off-domain and remains the default.
  The full measured comparison against DTLN-aec and WebRTC AEC3 — and
  the licensing survey that motivated the hybrid design — lives in
  [`tools/ml/README.md`](tools/ml/README.md) and the executed notebooks
  [`notebooks/ml_aec_comparison.ipynb`](notebooks/ml_aec_comparison.ipynb) /
  [`notebooks/aec_head_to_head.ipynb`](notebooks/aec_head_to_head.ipynb).
- **The anti-howl safety layer** — `tap::mu::howl_guard`
  ([`include/mutap/howl_guard.h`](include/mutap/howl_guard.h)), per-mic
  arming, duck, re-arm and back-off over the canceller's verdict and a
  `howl_detector` on each residual, attached to `afc_chain` with
  `set_guard()`, with a soundcheck sampler that sets each mic's verdict
  thresholds; measured live in [`docs/howl-guard.md`](docs/howl-guard.md).
- **The reverb stage** — `tap::mu::reverb_mix` and `tap::mu::shifted_dry_mix`
  ([`include/mutap/reverb_stage.h`](include/mutap/reverb_stage.h)): any
  mono-in reverb engine in `afc_chain`'s reverb slot as
  y = (1 − w)·x + w·r, and the topology that frequency-shifts the dry path
  only so a reverb tail never recirculates through the shifter;
  header-only, no FFT. The vendored FAUST Dattorro plates measured behind
  the canceller, with converged probes, in
  [`docs/reverb-afc.md`](docs/reverb-afc.md).
- **The NaN watchdog** ([`include/mutap/watchdog.h`](include/mutap/watchdog.h)):
  one finite check per block on the input and the residual in every hot
  path (`partitioned_fdaf`, `partitioned_fdkf`, `pem_afc`,
  `residual_suppressor`, `aec_chain`, `howl_guard::analyze`), read off
  sums the stage already computes (the error spectrum's DC slot, the
  suppressor's analysis and gain spectra, the detector's block power), so
  nothing runs per sample; a NaN or infinity resets the stage as `reset()`
  does, comes out as zeros and counts on `watchdog_trips()`, which the Max
  externals report. Recovery within one block and the counter at 1 for
  every stage ([`tests/test_watchdog.cpp`](tests/test_watchdog.cpp): the
  block after the trip is bit-identical to a fresh stage's); no arithmetic
  touched on finite input (every fingerprint line held on all nine CI
  legs); its instruction-count cost is recorded in the header from the
  ratchet: at most 40 instructions per block where code generation held
  still, 26 of 30 target × scenario rows inside ±0.1 %, one M33 chain row
  at +0.7 % by a code-generation shift (a recorded finding; the per-sample
  first cut measured 0.2–2.7 % and was replaced).

Next up (see [HANDOFF.md](HANDOFF.md) "What's next"): in-Max listening in
a real room and the default-engine decision, then the M55 performance
work (CMSIS-DSP/Helium mapping, instruction-count ratchets) and the
Hexagon data-layout work on real hardware (VTCM residency, FastRPC
offload).

## ITU-T compliance

The echo-cancellation chain — `tap::mu::aec_chain` configured by
`tap::mu::aec_chain_preset` (and exposed as `mutap.aec~ @postfilter 1` in
[MuTap-Max](https://github.com/tap/MuTap-Max)) — **meets every
requirement of the in-force ITU-T automotive/hands-free recommendations
at both required rates, 48 kHz and 16 kHz**, on one pinned
configuration. Measured margins over the requirements (worst rate,
cabin path):

| Claim | Requirement | Measured (worst rate) | Margin |
|---|---|---|---|
| Terminal coupling loss (P.1110/P.1120 §11.11.1) | ≥ 46 dB | 68.4 dB | +22 dB |
| Single-talk echo level (§11.11.2) | < −58 dBm0(A) | −76.4 | +18 dB |
| Attenuation spectrum vs the WB mask (§11.11.3) | mask | mask +13.3 dB | +13 dB |
| Convergence from cold start (§11.11.4) | ≥ 40 dB by 1.2 s | 45.4 dB | +5.4 dB |
| Convergence in driving noise (§11.11.5) | at ref by 1.5 s | at ref by 0.75 s | 2× |
| Double-talk send attenuation (P.340 Cat. 1) | ≤ 3 dB | 0.9 dB integrated | +2.1 dB |
| Double-talk echo loss, every band (P.340 Cat. 1) | ≥ 27 dB | 37.5 dB | +10 dB |
| Comfort-noise level match (§11.13) | +2/−5 dB | −2.15 dB | inside |
| Closed-loop stability (P.1110 Annex E) | stable | stable at 0 dB far-end ERL | sweep floor |
| Algorithmic delay | ≤ 70 ms budget | 10.7 / 32 ms | ≥ 2× |

Scope, honestly stated: G.168's battery is reported as **G.168-adapted**
(the rec disclaims acoustic scope); G.167 is withdrawn and **run and
reported**, not claimed; compressed real speech is method-equivalent
pending ITU test-vector procurement (synthetic P.501 signals generated
from the recommendations' algorithmic descriptions); and self-imposed
margin *targets* missed at 16 kHz are documented regression gates, never
silent. The clause-by-clause matrix is
[`docs/itu-compliance.md`](docs/itu-compliance.md); every row is
asserted by the test suite on every CI run, and
[`notebooks/itu_compliance.ipynb`](notebooks/itu_compliance.ipynb)
re-measures the whole battery live and renders the trajectories.

The battery is certified at **both** deployment precisions: every row is
a typed test over `<float, double>`, and the whole suite clears every
gate at **float32** — the precision the M55/Hexagon actually run — with
the same margins as the double golden model. float32 is verified against
the ITU thresholds directly, not inferred from double: the one place
they differ is the G.168 §7 tone row, where the float preset enables the
core's narrowband guard (classical tone-disabler discipline) that double
does not need — proof the two precisions are not related by a uniform
noise floor. On the M55/Hexagon emulated selection, where the full ITU
suite is too slow, [`tests/test_float32.cpp`](tests/test_float32.cpp)
runs the headline rows on-target (float-vs-double 0.1–0.7 dB; tone row
−64.8 dBm0(A) at 16 kHz against the −49.3 requirement).

## Quick start

```cmake
add_subdirectory(MuTap)                 # or FetchContent
target_link_libraries(app PRIVATE MuTap::MuTap)
```

```cpp
#include <mutap/mutap.h>
#include <vector>

tap::mu::real_fft32 fft(1024);          // power-of-2 size, fixed at construction
std::vector<float> block(1024);
// ... fill block with audio ...
fft.forward_inplace(block.data());      // noexcept, allocation-free
// bins: DC in [0], Nyquist in [1], then re/im interleaved pairs
```

The feedback canceller itself — PEM prewhitening (the speech cascade) over
the FD-Kalman core, at the geometry the test suite measures (block 64,
1024 taps):

```cpp
#include <mutap/mutap.h>
#include <vector>

using canceller = tap::mu::pem_afc<float, tap::mu::speech_predictor<float>,
                                   tap::mu::partitioned_fdkf<float>>;
canceller::config cfg;
cfg.fdaf.block_size = 64;               // samples per process_block() call
cfg.fdaf.partitions = 16;               // filter length = 16 x 64 = 1024 taps
canceller afc(cfg);                     // allocates here, and only here

std::vector<float> speaker(64), mic(64), clean(64);
// ... every block: speaker = what the loudspeaker played, mic = what it heard ...
afc.process_block(speaker.data(), mic.data(), clean.data()); // noexcept, allocation-free
// clean = mic - the estimated feedback: feed it to the forward path (gain,
// delay, ...) and back to the loudspeaker. afc.watchdog_trips() counts the
// blocks a NaN or infinity reached it (the stage resets and outputs silence).
```

Every `cpp` block above is extracted from this file at configure time and
compiled and run as `tests/test_readme_snippets.cpp` on every CI leg.

Build and test:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

For a reader who wants to *use* the canceller rather than build it, the
book [`book/`](book/) ("Quieting the Loop") explains the algorithm and
every `mutap.afc~` knob and trade-off with no DSP background assumed.

For a visual tour — howling past the MSG, the naive-canceller bias
limit-cycling vs PEM riding 6 dB above the open-loop limit, ASG by program
material, IPC, burst survival — see
[notebooks/afc_demo.ipynb](notebooks/afc_demo.ipynb), which drives the
library through its C ABI (`-DMUTAP_BUILD_CAPI=ON`, `tools/capi/`) via
ctypes (Python needs `numpy` and `matplotlib`; the first cell builds the
shared library if needed). The echo-canceller counterpart is
[notebooks/itu_compliance.ipynb](notebooks/itu_compliance.ipynb) — the
ITU compliance proof, every number re-measured live by a compiled C++
battery.

## Layout

```
include/mutap/       the library (header-only; umbrella header mutap.h)
submodules/dsptap/   DspTap submodule: the shared FFT (the header-only srdif
                     engine under include/tap/dsp/fft/; see
                     THIRD_PARTY_NOTICES.md)
tests/               GoogleTest suite (fetched at configure time)
tools/capi/          C ABI shared library for FFI consumers (notebooks)
tools/notebook/      notebook builders + the ITU measurement dump (C++)
notebooks/           demo + compliance-proof notebooks (build products)
book/                "Quieting the Loop" (mdBook) — the user-facing field guide
platform/            Cortex-M55 bare-metal board support (startup, linker)
cmake/               cross toolchain files (Cortex-M55 MPS3, Hexagon musl)
tests/fixtures/      RIR fixtures (committed rooms; tools/fixtures regenerates)
third_party/faust/   anti-howl PoC reference material, never part of the library:
                     FAUST's Dattorro plate and faust-icc's suppressor (corrected),
                     their FAUST-generated C++ and a shim; host tests only
                     (third_party/faust/README.md, THIRD_PARTY_NOTICES.md)
```

Planned as the milestones land: `examples/`, `bench/`, `docs/`.

## Style

This repo follows the shared Tap house rules — see [STYLE.md](STYLE.md).
`.clang-format` and `.clang-tidy` are the canonical TapHouse configs, enforced
in CI. Run `pre-commit install` once per clone (`pipx install pre-commit`) so
the `.pre-commit-config.yaml` hook formats staged C/C++ with the same pinned
clang-format the CI gate uses — no more format-only CI failures.

The pre-commit hook covers **clang-format only**; clang-tidy (the naming and
mandatory-braces gate) needs a compile database, so it stays a CI gate. Mirror
it locally before pushing with `scripts/tidy.sh` — no args sweeps every project
TU like CI, or pass the files you changed for a fast check
(`scripts/tidy.sh tests/test_foo.cpp`).

## License

MIT (see [LICENSE](LICENSE)). Bundled third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
