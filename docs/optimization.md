# Embedded performance: optimization notes

Measured, target-specific notes for the DSP hot path. Every claim here is a
number from the deterministic instruction-count ratchet (`bench/README.md`,
`scripts/icount.py`) under QEMU, not an estimate. The deployment precision is
**float32** on both embedded targets (the M55 has no double-precision FPU;
double runs soft-float and is the desktop golden model only).

## FFT backend: CMSIS-DSP Helium on Arm (opt-in)

### Why

The real FFT is the single hottest kernel in the chain. Profiling the M55
(`-mcpu=cortex-m55`, GCC 13, Helium/MVE) showed GCC does autovectorize the
vendored Ooura float FFT (`third_party/ooura/fftsg_float.c`) — but not nearly
as well as Arm's hand-tuned CMSIS-DSP kernels. Measured, per forward transform,
instructions under QEMU:

| N (use)                  | Ooura (GCC autovec) | CMSIS `arm_rfft_fast_f32` | advantage    |
|--------------------------|--------------------:|--------------------------:|--------------|
| 2048 (suppressor analysis) | 83,950            | 27,632                    | 3.04x (−67%) |
| 512  (canceller)         | 17,729              | 6,261                     | 2.83x (−65%) |

Carried through the whole certified chain, that FFT win lands as a **uniform
~42% reduction in total M55 instructions** (icount workloads, CMSIS vs the
prior Ooura baselines; the CMSIS column is what `bench/baselines.json` now
records for m55):

| scenario        | Ooura (prior) | CMSIS        | delta   |
|-----------------|--------------:|-------------:|---------|
| chain_48k       | 729,807,065   | 424,704,686  | −41.8%  |
| chain_16k       | 629,556,308   | 367,921,588  | −41.6%  |
| suppressor_48k  | 399,774,768   | 231,421,941  | −42.1%  |
| suppressor_16k  | 403,403,614   | 235,023,308  | −41.7%  |
| fdkf_48k        | 243,300,702   | 139,974,811  | −42.5%  |
| fdkf_16k        | 140,681,366   |  80,854,232  | −42.5%  |
| shadow_48k      |  89,371,476   |  51,279,103  | −42.6%  |

### What it costs: bit-identity

CMSIS uses different butterflies and twiddle factors than Ooura, so its output
is **not** bit-identical — it agrees only to single-precision rounding
(measured max relative error 1.3e-7 at N=512, 2.0e-7 at N=2048, i.e. float32
epsilon). Two things make that acceptable as the M55 default:

- **The ITU certification is unaffected.** It is measured on the
  double-precision path, which is *always* Ooura (`basic_real_fft<double>`
  never routes through CMSIS). The double golden model and every bit-identity
  claim in `docs/itu-compliance.md` stand unchanged.
- **The float32 embedded path stays within its asserted gates.** The full
  float32 ITU battery (`tests/test_float32.cpp` and the emulated suites) passes
  on CMSIS — it was always a tolerance oracle, never a bit-exact one, precisely
  because float32 is a rounding-level approximation of the double reference.

So the backend is **default ON for the bare-metal M55 embedded profile** — the
deployment target — and OFF everywhere else. The Ooura float32 path remains one
flag away (`-DMUTAP_FFT_CMSIS=OFF`) and is kept alive by a dedicated CI leg.

### How it is wired

`include/mutap/fft.h` routes `basic_real_fft<float>` through CMSIS when
`MUTAP_FFT_CMSIS` is defined; the CMake option defaults ON for the bare-metal
M55 profile (`CMAKE_SYSTEM_NAME=Generic` + arm) and OFF everywhere else, so
desktop, the Max/C-ABI host builds (including Apple Silicon arm64), and Hexagon
are untouched. `double` always keeps Ooura regardless.
The wrapper re-presents CMSIS in **Ooura's exact numeric contract** so nothing
downstream changes and every intermediate spectrum matches the Ooura build to
float epsilon:

- **Sign convention.** CMSIS uses the engineering convention exp(−i2π/N);
  Ooura (and our documented packed layout) uses exp(+i2π/N). The wrapper
  conjugates the imaginary bins on every transform. Verified by
  `real_fft_test/0.SignConventionIsPlusI` running on the CMSIS backend.
- **Inverse scaling.** CMSIS's inverse RFFT is 1/N-normalized; Ooura's is
  unnormalized (the caller applies 2/N). The wrapper scales the CMSIS inverse
  by N/2, so the existing `inverse()` 2/N normalization still closes the round
  trip. Both fold into passes the `_inplace` methods already do (~1% overhead).

It is on automatically with the M55 toolchain; force the Ooura path with
`-DMUTAP_FFT_CMSIS=OFF`:

```sh
# CMSIS backend (default on M55)
cmake -B build-m55 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DCMAKE_BUILD_TYPE=Release
# Ooura fallback
cmake -B build-m55-ooura -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DCMAKE_BUILD_TYPE=Release -DMUTAP_FFT_CMSIS=OFF
```

Forcing the option ON on a non-Arm processor is a hard error (Helium/NEON only).

### What is validated

- **`tests/test_fft_backend.cpp`** — asserts the CMSIS forward output matches a
  direct Ooura `rdft_f` reference bin-for-bin (<5e-6 relative) and that the
  round trip reproduces the input, at both certified sizes (512, 2048). Runs on
  the M55-CMSIS leg (in `tests/bare_metal_main.cpp`'s selection).
- **The whole `test_fft.cpp` contract suite** (packing, +i sign convention,
  Parseval, float-tracks-double) exercises `basic_real_fft<float>`, so it
  re-validates the CMSIS backend automatically when the option is on.
- **The full emulated float32 ITU battery** runs on the M55 with the CMSIS
  backend (now the default): 58/58 tests pass — the AEC still meets every
  asserted float32 gate on CMSIS FFTs. A dedicated CI leg re-runs the same
  battery with `-DMUTAP_FFT_CMSIS=OFF` to keep the Ooura fallback honest.

### Hexagon: deferred

The swap is **Arm-only**. On Hexagon (HVX V68) the Ooura FFT compiles fully
scalar (0 HVX), so an FFT backend would be the biggest single lever there too —
but there is no free HVX FFT: Qualcomm's is a proprietary SDK component, and
HVX-float autovectorization does not fire on the strided packed-complex loop
shape. Hexagon stays on scalar Ooura until an HVX FFT is available.

### Refreshing the vendored CMSIS subset

`third_party/cmsis-dsp/` is a minimal subset (8 sources + header closure),
pinned by commit in `third_party/cmsis-dsp/VENDOR.md`. To bump it: re-run the
`gcc -M` closure over the eight sources for `-mcpu=cortex-m55`, copy exactly the
files it opens, update `VENDOR.md`, then re-run `tests/test_fft_backend.cpp` and
the full float32 battery on the M55 leg. Do not hand-edit vendored sources.

## Suppressor pass-1: branch-free on Helium

The suppressor's pass-1 per-bin estimator (`residual_suppressor::process_block`)
has two **bit-identical** shapes, selected by `MUTAP_SUPPRESSOR_BRANCHLESS`
(`include/mutap/postfilter.h`):

- **Branch-free** (default when `__ARM_FEATURE_MVE`) — the two data-dependent
  branches (the no-echo coherence zero and the coherence-gated leakage re-learn)
  are lowered to selects whose kept values equal the branchy form's. GCC's MVE
  autovectorizer bails on the branchy form's control flow but lowers this one.
- **Branchy** (default elsewhere) — the original scalar form the compliance
  battery certified. On scalar targets the selects would do unconditional work
  the vectorizer can't hoist, so branchy is faster there.

Measured (icount, vs the branchy form on each target):

| target | suppressor | chain |
|---|--:|--:|
| M55 (Helium, branch-free) | **−2.9%** | −1.7 to −2.0% |
| Hexagon (HVX, branchy) | ±0.0% (unchanged) | ±0.0% |

So each target runs its faster form; neither regresses. Because it is
per-target, no single build compiles both shapes — the `branchless-parity` CI
job compiles `tests/branchless_parity_check.cpp` once per macro value and diffs
an output fingerprint over a 600-block double-talk corpus, guaranteeing the two
forms stay sample-exact. The m55 baselines record the branch-free form.
