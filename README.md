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
- **ARM Cortex-M55** — float32 with Helium (MVE-F) *(planned)*
- **Qualcomm Hexagon** — float32 with vector-float HVX (QCS8550-class cDSP)
  *(planned)*

## Status

Early scaffold (milestone M0 of [HANDOFF.md](HANDOFF.md), which carries the
full technical plan, milestone sequence and paper list). What exists today:

- `mutap::basic_real_fft<Sample>` — Ooura split-radix real FFT wrapped for
  float and double (`mutap::real_fft`, `mutap::real_fft32`), with the packed
  spectrum layout and sign convention documented in
  [`include/mutap/fft.h`](include/mutap/fft.h) and locked down by tests.

Next up (M1): the partitioned-block frequency-domain adaptive filter,
validated open-loop against known impulse responses.

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

## Layout

```
include/mutap/       the library (header-only; umbrella header mutap.h)
third_party/ooura/   vendored Ooura FFT (see THIRD_PARTY_NOTICES.md)
tests/               GoogleTest suite (fetched at configure time)
```

Planned as the milestones land: `examples/`, `bench/`, `notebooks/` (driving
the library through a C ABI, `tools/capi/`), `platform/` (Cortex-M55 board
support), `docs/`.

## Style

This repo follows the shared Tap house rules — see [STYLE.md](STYLE.md).
`.clang-format` and `.clang-tidy` are the canonical TapHouse configs, enforced
in CI.

## License

MIT (see [LICENSE](LICENSE)). Bundled third-party components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
