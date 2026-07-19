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
~43% reduction in total M55 instructions** (icount workloads, CMSIS vs the
Ooura baselines in `bench/baselines.json`):

| scenario        | Ooura        | CMSIS        | delta   |
|-----------------|-------------:|-------------:|---------|
| chain_48k       | 729,807,065  | 417,303,122  | −42.8%  |
| chain_16k       | 629,556,308  | 360,481,402  | −42.7%  |
| suppressor_48k  | 399,774,768  | 224,696,582  | −43.8%  |
| suppressor_16k  | 403,403,614  | 228,296,904  | −43.4%  |
| fdkf_48k        | 243,300,702  | 139,974,811  | −42.5%  |
| fdkf_16k        | 140,681,366  |  80,854,232  | −42.5%  |
| shadow_48k      |  89,371,476  |  51,279,103  | −42.6%  |

### What it costs: bit-identity

CMSIS uses different butterflies and twiddle factors than Ooura, so its output
is **not** bit-identical — it agrees only to single-precision rounding
(measured max relative error 1.3e-7 at N=512, 2.0e-7 at N=2048, i.e. float32
epsilon). That breaks the certified bit-identity guarantee, which is why the
backend is **opt-in and default OFF**. Enabling it is a separate, explicitly
reviewed step; the default build stays on Ooura and bit-identical.

### How it is wired

`include/mutap/fft.h` routes `basic_real_fft<float>` through CMSIS when
`MUTAP_FFT_CMSIS` is defined; `double` and every non-Arm build keep Ooura.
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

Enable it with the M55 toolchain:

```sh
cmake -B build-m55 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DCMAKE_BUILD_TYPE=Release -DMUTAP_FFT_CMSIS=ON
```

The option errors on non-Arm processors (the backend is Helium/NEON only).

### What is validated

- **`tests/test_fft_backend.cpp`** — asserts the CMSIS forward output matches a
  direct Ooura `rdft_f` reference bin-for-bin (<5e-6 relative) and that the
  round trip reproduces the input, at both certified sizes (512, 2048). Runs on
  the M55-CMSIS leg (in `tests/bare_metal_main.cpp`'s selection).
- **The whole `test_fft.cpp` contract suite** (packing, +i sign convention,
  Parseval, float-tracks-double) exercises `basic_real_fft<float>`, so it
  re-validates the CMSIS backend automatically when the option is on.
- **The full emulated float32 ITU battery** was re-run on the M55 with
  `MUTAP_FFT_CMSIS=ON`: 58/58 tests pass — the AEC still meets every asserted
  float32 gate on CMSIS FFTs.

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
