# MuTap

[![CI](https://github.com/tap/MuTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/MuTap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

Portable adaptive filters for audio cleaning: acoustic feedback (howling)
suppression and echo cancellation, from Max/MSP to embedded DSP. Header-only
C++20 apart from one tiny static target (`MuTap::fft`, the vendored Ooura
`fftsg.c`).

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

- `mutap::basic_real_fft<Sample>` — Ooura split-radix real FFT wrapped for
  float and double (`mutap::real_fft`, `mutap::real_fft32`), with the packed
  spectrum layout and sign convention documented in
  [`include/mutap/fft.h`](include/mutap/fft.h) and locked down by tests.
- `mutap::partitioned_fdaf<Sample>` — partitioned-block frequency-domain
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
  maximum-stable-gain (MSG) measurement. Measured MSG agrees with the
  −20·log₁₀ max|F(ω)| bound within 0.35 dB. And the **M2 regression
  baseline** ([`tests/test_closed_loop.cpp`](tests/test_closed_loop.cpp)):
  the naive (un-prewhitened) FDAF adds +6…+10 dB stable gain on white
  near-end but its bias on tonal program material is *destabilizing*
  (ASG ≤ −12 dB — it howls at gains the open loop handles). Removing that
  failure is PEM prewhitening's job.

- **PEM prewhitening** — the actual feedback canceller.
  [`include/mutap/lpc.h`](include/mutap/lpc.h): autocorrelation and
  Levinson–Durbin with the conditioning guards (relative ridge, silence
  floor, early stop), plus the pluggable near-end models — `lpc_predictor`
  (short-term LP) and `speech_predictor` (the cascade: short-term LP +
  long-term pitch tap). [`include/mutap/pem_afc.h`](include/mutap/pem_afc.h):
  `mutap::pem_afc<Sample, Predictor>`, the FDAF-PEM-AFROW structure —
  cancellation runs on the raw signals, adaptation on the prewhitened pair.
  Measured in the M2 loop (converged at MSG−6 dB, across seeds and both
  precisions): on the tonal material where the naive canceller howls ≥12 dB
  *below* the open-loop MSG, PEM is stable above it with **ASG +4.5…+6.8 dB**;
  speech-envelope material **+3…+12.6 dB**; voiced (pitch-periodic) material
  **+2.7…+4.5 dB** where naive howls; white unchanged (~+7.4 dB)
  ([`tests/test_pem_afc.cpp`](tests/test_pem_afc.cpp),
  [`tests/test_lpc.cpp`](tests/test_lpc.cpp)).

- **Adaptation control** (in `partitioned_fdaf`, so both cancellers get it):
  the **IPC** double-talk indicator (after Gil-Cacho et al. 2014, computed as
  a chance-corrected per-partition coherence — the estimated fraction of
  error power coherent with the input: measured ~0.7 while unconverged, 0.00
  converged, 0.02 under double-talk; and the paper's headline reproduces:
  raw-pair IPC 0.73 in the tonal closed loop vs 0.05 prewhitened),
  **IPC-scaled stepping** (μ·IPC², a Wiener-flavored variable step) plus an
  **instantaneous transient gate**, which together contain a +20 dB near-end
  burst that blows up the ungated loop (worst block RMS ~25 vs ~56000 —
  each layer alone is insufficient), and **variable regularization**
  (per-bin normalizer floored at a fraction of the mean bin power), making
  identification scale-invariant from 10⁻⁵× to 10³× input scale where a
  fixed epsilon degrades by ~150 dB
  ([`tests/test_adaptation_control.cpp`](tests/test_adaptation_control.cpp)).
  A side-effect worth knowing: variable regularization alone softens the
  naive canceller's tonal bias (ASG −12 → −3.5 dB) — mitigation, not the
  fix; the M2/M3 baseline tests pin the M1-era config explicitly.

- **The Max/MSP external** — `mutap.afc~` in
  [MuTap-Max](https://github.com/tap/MuTap-Max) (Min-DevKit package, MuTap
  as a submodule): universal macOS `.mxo` + Windows `.mxe64` built in CI.
- **The music/tonal near-end predictor** —
  `mutap::warped_lpc_predictor<Sample>`: frequency-warped LP (unit delays
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
  first cut overfit). With it, measured ASG on the chord across **eleven
  random rooms from two generator families: +7.2…+11.3 dB, no collapse
  anywhere** — including the room where the speech cascade at its own
  defaults destabilizes (−2.2 dB) — and speech-envelope material improves
  to +13.8 dB. Defaults (λ = 0.5, order 16) are the sweep's best
  worst-case; Bark-scale λ = 0.766/order 24 trades a slightly better mean
  for a weaker floor at ~1.5× the cost. Select it with
  `mutap::pem_afc<Sample, mutap::warped_lpc_predictor<Sample>>`.
- **The Cortex-M55 build** — the first embedded target: bare metal
  (newlib + semihosting) on QEMU's MPS3 AN547 board model, platform rig
  (Armv8-M startup, linker script, one-shot gtest harness) ported from
  SampleRateTap ([`platform/`](platform/),
  [`cmake/arm-cortex-m55-mps3.cmake`](cmake/arm-cortex-m55-mps3.cmake)).
  58 tests run on-target: the float32 typed suites (the embedded profile),
  the LP conditioning suite, the float closed-loop scenarios including the
  PEM tonal headline and burst gating, and the float-tracks-double oracle
  check — with double as soft-float (the M55 FPU is single-precision only).
- **The Hexagon build** — the third target: hexagon-unknown-linux-musl via
  the Codelinaro clang toolchain with **HVX auto-vectorization on**
  (128-byte vectors, 32 fp32 lanes), statically linked and run under
  qemu-hexagon user-mode emulation in CI
  ([`cmake/hexagon-linux-musl.cmake`](cmake/hexagon-linux-musl.cmake)).
  A hosted Linux target needs no platform rig — stock gtest, ctest and
  exit codes work unchanged. Per-push CI runs the same emulation-sized
  selection as the M55 leg (58 tests, ~8 min of TCG); the full 74-test
  suite, double-typed adaptive suites included, has also been validated
  once on the ISA (double is hardware on the Hexagon scalar core). What
  this leg deliberately does not cover: VTCM placement, L2 streaming
  layout and FastRPC offload need the proprietary Hexagon SDK and real
  hardware (HANDOFF.md pins the layout targets).

- **The PEM-FD-Kalman core (v2)** — `mutap::partitioned_fdkf<Sample>`
  ([`include/mutap/fd_kalman.h`](include/mutap/fd_kalman.h)): the NLMS
  update replaced by a diagonalized partitioned-block frequency-domain
  Kalman filter (Enzner & Vary 2006; Bernardi et al.'s PEM variant), as a
  drop-in core for the same FDAF-PEM-AFROW structure —
  `mutap::pem_afc<Sample, Predictor, mutap::partitioned_fdkf<Sample>>`.
  The per-bin state uncertainty and near-end PSD replace the entire
  adaptation-control stack (no step size, no IPC options), and every claim
  is measured: at 0 dB SNR it beats both ends of the NLMS speed/depth
  tradeoff at once (−15.7 dB by block 300 vs μ=0.5's −5.9 and μ=0.1's
  slow start); in the closed loop with **zero knobs** it matches the tuned
  stack on tonal ASG (+4.7/+7.8 dB), **saturates the +25 dB probe ceiling**
  on speech-envelope and white near-end (NLMS stack: +3…+12.6), and holds
  **+8.4…+13.4 dB across all the music rooms with no IPC pairing** —
  including the room where the NLMS speech cascade destabilizes. A +20 dB
  near-end burst is survived ungated (excursion ~2 dB; ungated NLMS is
  wrecked); the opt-in transient floor contains it to gated-NLMS quality
  at a measured ~2–6 dB tonal-ASG cost, which is why it defaults off
  ([`tests/test_fd_kalman.cpp`](tests/test_fd_kalman.cpp)).
- **RIR fixtures** — three physically-modeled rooms (image-source method,
  documented geometry, deterministic; [`tools/fixtures/`](tools/fixtures/))
  committed as permanent closed-loop baselines with real early-reflection
  structure. Measured on them (block 64, 16 partitions, speech-envelope
  material): **Kalman +17.8…+19.1 dB ASG vs the classic stack's
  +9.4…+9.7** ([`tests/test_rir_fixtures.cpp`](tests/test_rir_fixtures.cpp)).
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
  near-end floor by two-window minimum statistics, and `mutap::aec_chain`
  composing it with a linear canceller (default: the **raw** FD-Kalman
  core — open-loop AEC has an exogenous far end, so PEM's decorrelation
  buys nothing and its predictor refit floors misalignment near −20 dB
  where the raw core reaches −75 dBm0(A) bare). Measured on the ITU
  battery ([`tests/test_postfilter.cpp`](tests/test_postfilter.cpp)):
  single-talk residual **−80.5/−89.7 dBm0(A)** (cabin/studio; the P.1120
  clause wants < −58, our margin target < −64), double-talk near-end
  attenuation **1.14 dB** (clause ≤ 3, target ≤ 1.5), double-talk echo
  loss **≥ 34.2 dB in every band** (clause ≥ 27, target ≥ 33), comfort
  noise matched **−1.5 dB / ≤ 1.7 dB per band** (clause +2/−5, half-mask
  spectrum), noise pumping 2.9 dB, near-end build-up 11.6 ms.

Next up (see [HANDOFF.md](HANDOFF.md) "What's next"): in-Max listening in
a real room and the default-engine decision, then the M55 performance
work (CMSIS-DSP/Helium mapping, instruction-count ratchets) and the
Hexagon data-layout work on real hardware (VTCM residency, FastRPC
offload).

## Quick start

```cmake
add_subdirectory(MuTap)                 # or FetchContent
target_link_libraries(app PRIVATE MuTap::MuTap)
```

```cpp
#include <mutap/mutap.h>

mutap::real_fft32 fft(1024);            // power-of-2 size, fixed at construction
std::vector<float> block(1024);
// ... fill block with audio ...
fft.forward_inplace(block.data());      // noexcept, allocation-free
// bins: DC in [0], Nyquist in [1], then re/im interleaved pairs
```

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
shared library if needed).

## Layout

```
include/mutap/       the library (header-only; umbrella header mutap.h)
third_party/ooura/   vendored Ooura FFT (see THIRD_PARTY_NOTICES.md)
tests/               GoogleTest suite (fetched at configure time)
tools/capi/          C ABI shared library for FFI consumers (notebooks)
notebooks/           demo notebook (ctypes over the C ABI)
book/                "Quieting the Loop" (mdBook) — the user-facing field guide
platform/            Cortex-M55 bare-metal board support (startup, linker)
cmake/               cross toolchain files (Cortex-M55 MPS3, Hexagon musl)
tests/fixtures/      RIR fixtures (committed rooms; tools/fixtures regenerates)
```

Planned as the milestones land: `examples/`, `bench/`, `docs/`.

## Style

This repo follows the shared Tap house rules — see [STYLE.md](STYLE.md).
`.clang-format` and `.clang-tidy` are the canonical TapHouse configs, enforced
in CI.

## License

MIT (see [LICENSE](LICENSE)). Bundled third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
