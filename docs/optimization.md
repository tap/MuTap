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
Ooura float FFT (measured on the vendored Ooura C, `fftsg_float.c`, at that
pin; since replaced by the bit-identical C++20 port `fft/split_radix.h` — routed
to at DspTap `bbfa48d`, Stage 2b, with the C leaving the shipping tree at Stage
2c, `8350f13`) — but not nearly
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

- **The double golden model is unaffected.** `basic_real_fft<double>` never
  routes through CMSIS, so the double leg of the certification (and every
  bit-identity claim in `docs/itu-compliance.md`) stands unchanged.
- **The float32 embedded path stays within its asserted gates.** The ITU
  battery is now certified at float32 as well as double (typed `<float, double>`
  suites, `docs/itu-compliance.md`); that full battery is host-only, and on the
  M55 the float32 gate battery `tests/test_float32.cpp` — the headline ITU rows
  at deployment precision — passes on the CMSIS backend. The parity oracle was
  always a tolerance oracle, never bit-exact, precisely because float32 is a
  rounding-level approximation of the double reference.

So the backend is **default ON for the bare-metal M55 embedded profile** — the
deployment target — and OFF everywhere else. The scalar float32 path (the
split-radix engine, "Ooura" in older logs) remains one flag away
(`-DTAP_DSP_FFT_CMSIS=OFF`) and is kept alive by a dedicated CI leg.
(History: the option was renamed from `MUTAP_FFT_CMSIS` to `TAP_DSP_FFT_CMSIS`
when the FFT moved to DspTap, commit `14116f0`; the leg kept the old name, which
CMake ignored, so from that commit until tap/MuTap#50 it silently rebuilt CMSIS.
The job now asserts the typed cache entry `TAP_DSP_FFT_CMSIS:BOOL=OFF` — an
unknown `-D` lands as `:UNINITIALIZED`, so a rename fails the leg — and the
harness binary that ran prints `backend=split_radix abi=fft_split_radix` —
`backend=ooura` up to the DspTap `8350f13` pin — which the leg greps for.)

### How it is wired

The FFT lives in DspTap (`submodules/dsptap`, `tap::dsp`; `include/mutap/fft.h`
is a re-export). Its `fft.h` routes `basic_real_fft<float>` through CMSIS when
`TAP_DSP_FFT_CMSIS` is defined; the CMake option of that name defaults ON for
the bare-metal M55 profile (`CMAKE_SYSTEM_NAME=Generic` + arm) and OFF
everywhere else, so desktop, the Max/C-ABI host builds (including Apple Silicon
arm64), and Hexagon are untouched. Everywhere the option is off, and for
`double` always, `basic_real_fft` is the split-radix engine
(`fft/split_radix.h`, DspTap's C++20 port of the vendored Ooura C, routed since
DspTap `bbfa48d`, Stage 2b; the C left the shipping tree at Stage 2c, `8350f13`,
and DspTap Decision D6, tap/DspTap#36 (`0db95b6`), deleted the reference copy its parity
gate compiled; the bit identity is now held by pinned fingerprints,
DspTap's `tests/test_fft_split_radix_fingerprint.cpp`).
Since DspTap Stage 4 (tap/DspTap#35, pinned here from `0db95b6`) the engine is
a template parameter, `basic_real_fft<Sample, Engine>`, whose default is what
the option selects; the CMSIS engine is `detail::cmsis_real_fft_f32` in DspTap's
`fft/backends/cmsis.h`.
The wrapper re-presents CMSIS in **Ooura's exact numeric contract** so nothing
downstream changes and every intermediate spectrum matches the split-radix
build to float epsilon:

- **Sign convention.** CMSIS uses the engineering convention exp(−i2π/N);
  Ooura (and our documented packed layout) uses exp(+i2π/N). The wrapper
  conjugates the imaginary bins on every transform. Verified by DspTap's
  `real_fft_test/0.SignConventionIsPlusI` running on the CMSIS backend.
- **Inverse scaling.** CMSIS's inverse RFFT is 1/N-normalized; Ooura's is
  unnormalized (the caller applies 2/N). The wrapper scales the CMSIS inverse
  by N/2, so the existing `inverse()` 2/N normalization still closes the round
  trip. Both fold into passes the `_inplace` methods already do (~1% overhead).
- **Size range: 32 … 4096 only.** `arm_rfft_fast_init_f32` supports exactly
  those powers of two. Since Stage 4 DspTap states each engine's range as
  `k_min_size` / `k_max_size` with the predicate `supports_size(n)`, and the
  constructor's check is a debug-only precondition: in a release build an
  unsupported N is undefined behaviour, on the M55 a HardFault at the first
  transform (N = 4 did exactly that before Stage 4). So every MuTap class that
  turns a configuration number into an FFT size checks it first and throws
  `std::invalid_argument` (`fft_detail::checked_fft_size` in
  `include/mutap/fft.h`): under CMSIS the cancellers take block sizes
  16 … 2048 (N = 2B), the residual suppressor needs
  `analysis_blocks * block_size` in 32 … 4096 (the default 8 blocks allow
  block sizes up to 512), and a learned-suppressor model's hop must be in
  16 … 2048. `double`, and float on every other build, runs the split-radix engine
  (4 … 2^30). `tests/test_fft_engine_contract.cpp` pins each path on every leg.
- **ABI tag.** Because `basic_real_fft<float>`'s layout follows the selected
  engine, so does the layout of every MuTap class that holds one by value;
  those five classes are defined inside DspTap's inline namespace for the
  engine (`tap::mu::fft_cmsis::partitioned_fdaf<float>` on the M55,
  `tap::mu::fft_split_radix::…` elsewhere), so two images built with
  different engines cannot share one weak definition. Lookup is unchanged
  (`include/mutap/fft.h` has the full statement).

It is on automatically with the M55 toolchain; force the split-radix engine with
`-DTAP_DSP_FFT_CMSIS=OFF`:

```sh
# CMSIS backend (default on M55)
cmake -B build-m55 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DCMAKE_BUILD_TYPE=Release
# split-radix fallback
cmake -B build-m55-split-radix -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55-mps3.cmake \
    -DCMAKE_BUILD_TYPE=Release -DTAP_DSP_FFT_CMSIS=OFF
```

Forcing the option ON on a non-Arm processor is a hard error (Helium/NEON only).

### What is validated

- **DspTap's `tests/test_fft_backend.cpp`** (`fft_backend_parity/cmsis`,
  typed rows beside `fft_backend_parity/split_radix` in one binary since Stage
  4) asserts the CMSIS forward output matches the reference float engine
  bin-for-bin (<5e-6 relative) and that the round trip reproduces the input,
  at both certified sizes (512, 2048). The reference is
  `detail::split_radix_rdft<float>`, the port that is bit-identical to Ooura's
  `rdft_f`, called directly; until DspTap `bbfa48d` (Stage 2b) it was the raw
  `rdft_f` of `fftsg_float.c`. Since DspTap's embedded legs landed the suite runs under
  QEMU on DspTap's own `cortex-m55` leg, where the CMSIS backend is on
  (`test_fft_backend.cpp` is in `tap_dsp_tests`, DspTap `tests/CMakeLists.txt`);
  before that, the float32 battery below was the only CMSIS gate anywhere and
  the suites were gone from MuTap's `tests/bare_metal_main.cpp` selection
  (they moved to DspTap with the FFT).
- **The whole DspTap `test_fft.cpp` contract suite** (packing, +i sign
  convention, Parseval, float-tracks-double) exercises `basic_real_fft<float>`,
  so it re-validates the CMSIS backend automatically wherever it runs with the
  option on (same status as above for the M55).
- **The emulated float32 battery** (`mutap_tests_emulated`, the 59-test
  selection in `tests/bare_metal_main.cpp`) runs on the M55 with the CMSIS
  backend (the default): 59/59 pass — the AEC still meets every asserted
  float32 gate on CMSIS FFTs. A dedicated CI leg re-runs the same battery with
  `-DTAP_DSP_FFT_CMSIS=OFF` to keep the split-radix fallback honest.
- **`tests/fingerprint_harness.cpp`** (`mutap_fingerprint`) prints one FNV-1a
  fingerprint per (component, profile) over a fixed corpus on every CI leg,
  including both M55 legs, so a pin-to-pin difference in any output sample is
  visible as a diff of two logs; it is the bit-identity gate every DspTap pin
  bump runs (procedure at the top of the file). Between the two M55 legs it
  shows what the contract predicts — the seven `double` lines identical, the
  seven `float` lines all different — which documents the backends' difference
  but asserts nothing about CMSIS accuracy; that is the parity gate's job.

### Hexagon: deferred

The swap is **Arm-only**. On Hexagon (HVX V68) the Ooura FFT compiles fully
scalar (0 HVX), so an FFT backend would be the biggest single lever there too —
but there is no free HVX FFT: Qualcomm's is a proprietary SDK component, and
HVX-float autovectorization does not fire on the strided packed-complex loop
shape. Hexagon stays on the scalar Ooura-lineage engine (the vendored C when
this was measured; from DspTap `bbfa48d` the bit-identical split-radix port)
until an HVX FFT is available.

### Refreshing the vendored CMSIS subset

DspTap's `third_party/cmsis-dsp/` is a minimal subset (8 sources + header
closure), pinned by commit in its `VENDOR.md`. To bump it (in DspTap): re-run
the `gcc -M` closure over the eight sources for `-mcpu=cortex-m55`, copy exactly
the files it opens, update `VENDOR.md`, then re-run DspTap's
`tests/test_fft_backend.cpp` and, after bumping the pin here, the full float32
battery on the M55 leg. Do not hand-edit vendored sources.

## FFT backend: Apple vDSP on macOS

The same seam carries a second backend: on macOS the float32 real FFT can
route through Apple's **vDSP** (Accelerate) instead of the split-radix engine
(`TAP_DSP_FFT_ACCELERATE`, DspTap's default ON for Apple). It is the *only*
fast FFT Apple Silicon gets — the CMSIS backend is scoped to the bare-metal
M55, so arm64 macOS was on Ooura before this. **MuTap now turns it back OFF**
(root `CMakeLists.txt`, tap/MuTap#31): on spectra with exactly-empty bins the
alignment-selected vDSP kernel measured far less accurate than Ooura and the
G.168 tone rows failed on it, and Apple documents the routines as free to
rearrange arithmetic. The numbers below stand as the measurement behind that
decision; `-DTAP_DSP_FFT_ACCELERATE=ON` still selects it for anyone who wants
to re-measure.

### Measured (Apple Silicon, macOS CI runner)

Per forward transform, ns, **including** the split-complex conversion vDSP needs
(its data layout is split real/imag, ours is interleaved — the conversion is the
part that could have eaten the win; it didn't):

| N (use)                    | Ooura (clang autovec) | vDSP  | advantage |
|----------------------------|----------------------:|------:|-----------|
| 2048 (suppressor analysis) | 4885 ns               | 1552 ns | 3.15x   |
| 512  (canceller)           | 1103 ns               | 347 ns  | 3.18x   |

Unlike the M55 — where the win was load-bearing against a tight budget — a Mac
has enormous headroom, so this is a "best FFT per platform" consistency win, not
a necessity. It was cheap to add because the backend seam already existed.

### Reconciliation

vDSP works in split-complex form, the engineering convention exp(−i2π/N), and a
2×-scaled forward. The wrapper (`detail::accelerate_real_fft_f32`, in DspTap's
`fft/backends/accelerate.h` since Stage 4; in `fft.h` when this was written)
re-presents it in Ooura's exact contract with constants **measured on Apple
Silicon** (not derived), verified to <4e-7 relative error and an exact round
trip at N=512 and N=2048:

- **Forward** — `vDSP_ctoz` (deinterleave) → `vDSP_fft_zrip` → conjugate the
  imaginary bins and scale by 0.5 (undo vDSP's 2×).
- **Inverse** — rebuild vDSP's split spectrum (undo the 0.5, conjugate back),
  `vDSP_fft_zrip` inverse, `vDSP_ztoc` (reinterleave), scale by 0.25 to land on
  Ooura's unnormalized inverse (the caller's 2/N then normalizes).

`double` stays on the split-radix engine (golden model), so the double leg of
the certification is unaffected. Not bit-identical (float epsilon), same as
CMSIS. While vDSP was the macOS default, the full float32 ITU battery (the
typed `<float, double>` suites, `docs/itu-compliance.md`) ran on the
`macos-latest` CI host through vDSP itself, not merely parity against Ooura —
which is how its tone-row failures surfaced (tap/MuTap#31). Since that change
the job runs the split-radix engine.

### How it is wired / validated

In DspTap: default ON for Apple (`APPLE` in CMake), OFF elsewhere, mutually
exclusive with the CMSIS backend; CMake links `-framework Accelerate` and
defines the macro on the `tap::dsp` interface target. MuTap's root
`CMakeLists.txt` sets `TAP_DSP_FFT_ACCELERATE` OFF (not FORCE), so an explicit
`-DTAP_DSP_FFT_ACCELERATE=ON` still wins. DspTap's `tests/test_fft_backend.cpp`
(bin-for-bin against the split-radix engine in the same binary) and
`test_fft.cpp` contract suite validate the backend in DspTap's own macOS CI;
MuTap's `macos-latest` job gates the split-radix float32 path it actually
ships and records the backend it built. The vDSP
setup's read-only twiddle tables are held by a shared_ptr
so `basic_real_fft` keeps value semantics; transforms are noexcept and
allocation-free.

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
job compiles the fingerprint harness (`tests/fingerprint_harness.cpp`, the
`mutap_fingerprint` target; see "What is validated" above) once per macro value
and diffs every `FINGERPRINT` line — 14 of them, seven components in both
profiles over the 400-block corpus — guaranteeing the two forms stay
sample-exact. The m55 baselines record the branch-free form.
