# Vendored FAUST material

FAUST sources, and the C++ FAUST generates from them, for the anti-howl PoC
(karaoke / cabin feedback control on Hexagon and Cortex-A). Two things live
here:

- **the reference reverb**: FAUST's `re.dattorro_rev`, as shipped and with the
  paper's delay lengths. The PoC puts a plate inside the feedback loop and
  needs a known one to measure against.
- **the baseline suppressor**: faust-icc's adaptive notch bank and frequency
  shifter, corrected. Its "+12 dB" is the published number the PoC's own
  results get compared with, so the comparison has to run on a version whose
  defects are fixed and documented.

This is reference and test material. It is not part of MuTap's library:
generated code never enters `include/mutap/`, nothing under `include/`
includes it, and it is compiled only by `tests/test_faust_vendored.cpp`, on
host builds (the cross and bare-metal test builds skip it; see
`tests/CMakeLists.txt`). Like every `third_party/` directory it is excluded
from the clang-format hook (`.pre-commit-config.yaml`: `^third_party/`) and
from the clang-tidy sweep (`scripts/tidy.sh`: `'third_party' not in f`;
`.clang-tidy`'s `HeaderFilterRegex` covers only `include/` and `tests/`).

## Contents

| Path | What | License |
|---|---|---|
| `faust-icc/icc.lib`, `faust-icc/LICENSE` | faust-icc's library and license, byte-identical to upstream | MIT, © 2026 Ashmita Chakraborty |
| `faust-icc/UPSTREAM` | the upstream URL and commit | |
| `dsp/dattorro.dsp` | `re.dattorro_rev` as shipped: mono in, L/R out, seven runtime `nentry`s at the paper's defaults, pre-delay 0. Byte-identical to phase 0's `_afc_poc/max/dattorro.dsp`, the source of the `dattorro~` Max external | MuTap's (MIT); the reverb is STK-4.3 |
| `dsp/dattorro_paper.dsp` | a local copy of `dattorro_rev` with the paper's tank delays and rate-scaled lengths (below) | STK-4.3 (the copied body, author Jakob Zerbian); MuTap's changes MIT |
| `dsp/icc_48k.lib` | imports the pristine `icc.lib`; overrides `freqShift`, `notchSlot` and `notchBank`; forwards the rest | MIT |
| `dsp/icc_suppressor.dsp` | `notchBank(3, 32, 150, 6000, 14, prom, hold, 60, depth) : freqShift(6, guard, shift)`, mono in / mono out | MIT |
| `dsp/icc_howl_detect.dsp` | `howlDetect(32, 150, 6000, 14, 15, 0.20)`: confidence, frequency, prominence | MIT |
| `generate.sh` | regenerates `generated/` (FAUST 2.88.0 only) | MIT |
| `generated/<name>_f32.hpp`, `_f64.hpp` | FAUST output, committed, never hand-edited | see `THIRD_PARTY_NOTICES.md` |
| `faust_shim.h` | the `dsp` / `Meta` / `UI` bases the generated code needs, and `faust_block<Dsp, Sample>` | MIT (MuTap's) |
| `faust_generated.h` | includes all eight generated classes with the right `FAUSTFLOAT` each, warnings suppressed | MIT (MuTap's) |

Upstream provenance, and the license of every FAUST library function the
generated code contains, are in the repository's `THIRD_PARTY_NOTICES.md`.

## Regenerating

```sh
third_party/faust/generate.sh            # or FAUST=/path/to/faust third_party/faust/generate.sh
```

It refuses to run unless `faust --version` reports 2.88.0 (the committed
headers are that compiler's output with its bundled libraries; phase 0 built
it from the release tarball into `~/.local`). For each `dsp/<name>.dsp` it
writes `generated/<name>_f32.hpp` and `generated/<name>_f64.hpp`:

```sh
faust -I faust-icc -I dsp -lang cpp -single|-double -ftz 1 -ns mutap_faust -cn <name>_f32|<name>_f64 dsp/<name>.dsp
```

`-ftz 1` flushes subnormals portably (FAUST 2.88.0's `-ftz 2` output takes the
address of an rvalue and does not compile with clang). `-ns mutap_faust` puts
each class in the shim's namespace so it binds to the shim's bases. Commit the
regenerated headers together with the `.dsp` change that caused them.

## Using the classes

```cpp
#include "faust_generated.h"   // with third_party/faust on the include path

mutap_faust::faust_block<mutap_faust::icc_suppressor_f32, float> sup(48000); // allocates, may throw
sup.set("shift", 2.0f);                                    // noexcept; false if no such control
sup.process_mono(in, out, n);                              // noexcept, allocation-free
```

The `_f32` classes compute and exchange samples in `float`, the `_f64`
classes in `double`; `Sample` must match (a `static_assert` checks it). A
class's control labels are the `nentry` names in its `.dsp`. FAUST's `init()`
refills class-static oscillator tables (65,536 entries each, in every
translation unit that includes the header), so do not construct two
instances of one class concurrently. Instance sizes (measured by the test):
`dattorro_f32` 128,664 bytes, `dattorro_f64` 257,320, `dattorro_paper_f32`
819,408, `dattorro_paper_f64` 1,638,800, `icc_suppressor_f32` 3,144,
`icc_howl_detect_f32` 1,576.

## Dattorro: what `re.dattorro_rev` is, and what `dattorro_paper` changes

Read from `reverbs.lib` 1.5.1 (phase 0, `_afc_poc/PHASE0.md` §3):

- license STK-4.3 (MIT-style), author Jakob Zerbian; no LFO excursion (the
  plate is time-invariant, so it is not a decorrelator);
- the delays are the paper's sample counts at 29.761 kHz, unscaled, so at
  48 kHz the plate is 1.61× smaller than the paper's;
- the output is the two tank-input nodes, not the paper's 14-tap output network;
- **the tank bug**: `block(i)` reads `ba.take(i+5, d)` for both the second
  tank allpass and the delay after it (`reverbs.lib:906`), so the paper's
  3720 / 3163-sample delays are never used (the generated code has 1800 and
  2656 twice each).

`dattorro_paper.dsp` keeps everything else and changes two things: the
tank's second delay reads `ba.take(i+7, d)` (3720 / 3163), and every delay
length is `int(n * ma.SR / 29761 + 0.5)`, the paper's size at any rate.

FAUST sizes an SR-dependent delay line from the interval of its length, and
`ma.SR`'s interval is [1, 192000] (`platform.lib`), so each line is sized for
192 kHz (rounded up to a power of two) whatever rate `init()` gets: the
float instance is 819,408 bytes against 128,664 for the as-shipped plate,
about four times what 48 kHz alone needs. That is correct at every rate up to
192 kHz, so the rate was not frozen at 48 kHz; freeze it (a constant in place
of `ma.SR`) if the footprint matters on a target.

Measured at 48 kHz, paper defaults (`tests/test_faust_vendored.cpp`; T30 by
Schroeder backward integration, a least-squares line from −5 to −35 dB):

| | T30 L | T30 R |
|---|---|---|
| as shipped (73,992-sample IR, phase 0's length) | 1.184593 s | 1.173498 s |
| as shipped (4 s IR) | 1.184580 s | 1.173501 s |
| paper lengths (4 s IR; float identical to 6 digits) | 2.118617 s | 2.089709 s |

With the diffusers and damping at 0 the IR is a train of impulses at
predictable samples; in `dattorro_paper` the first recirculation lands with
gain exactly 0.25 (decay²) at L[19117] and R[18636], where the 3163 / 3720
delays put it, and nothing lands where upstream's repeated 2656 / 1800 would
(L[18300], R[15539]).

`dattorro_f64` (`-double -ftz 1`) is bit-identical over 20 s of impulse
response, at decay 0.3 / 0.5 / 0.7 / 0.85, to the class FAUST generates from
the same `.dsp` with `faust2max6`'s options (`-double`, no `-ftz`), the class
`dattorro~.mxo` wraps (checked with a scratch harness, not committed).
`faust2max6`'s default C++ flags are `-O3 -ffast-math`, so the `.mxo`'s
arithmetic can still differ from a test build's in the last bits; the Max
external itself was not run here.

## faust-icc: the defects and the fixes

faust-icc (https://github.com/Dhwaani/In-CarCommunication, commit
`3626a940a4f511d8c21f2b0cb40af1e8de9e2f1e`, MIT) is vendored unmodified;
`dsp/icc_48k.lib` overrides three functions. Phase 0 (`_afc_poc/PHASE0.md` §4)
found, with faust-icc's own MSG method (one impulse, late RMS 4.5–5.5 s >
−60 dBFS) on its own cabin:

- reproduced as published at 16 kHz: +4.0 dB bypassed, +16.0 suppressed,
  **12.0 dB added**; the ablation attributes about 6 dB to defect 1 below,
  about 5 dB to defect 2 removing the 310 Hz cabin mode, about 1 dB to the
  notches (+1.1 alone) and under 1 dB to decorrelation;
- at 48 kHz as shipped (guard 4 kHz) the chain adds +17.5 dB, +16.6 of it the
  shifter at 0 Hz; with the guard at 100 Hz (x2 not restored, poles not
  retuned) +9.9 dB, +3.9 of it the 0 Hz shifter.

`freqShift(order, shiftHz)` (`icc.lib:285–290`) → `freqShift(order, guardHz, shiftHz)`:

1. **Missing ×2.** It takes the real part of `fi.pospass`'s output without the
   ×2 FAUST documents (`hilbert(N) = pospass(N) : !,*(2)`), a flat −6 dB inside
   the loop. Restored.
2. **Hard-wired guard.** `fc = SR/(2·order)`: 1333 Hz at 16 kHz, 4 kHz at
   48 kHz; below it content is attenuated, not shifted (−14.4 dB at 1 kHz and
   −28.1 dB at 310 Hz at 16 kHz; −17.7 dB at 2.5 kHz at 48 kHz). Now a parameter.
3. **Backwards shift.** The modulator goes to `si.cmul` as (sin, cos), so
   "+4 Hz" gave 996 Hz from 1000 Hz, and 0 Hz gave the Hilbert (quadrature)
   output rather than the input. Now (cos, sin): measured +4 Hz on 1000 Hz →
   1003.999753 Hz, image at 996 Hz −7.3 dB (guard 100 Hz).

`notchSlot` / `notchBank`: the latched-frequency and depth smoothers,
`si.smooth(0.999)` and `si.smooth(0.9995)`, are per-sample poles: 62.47 ms and
124.97 ms at 16 kHz (−1/(16000 ln s); ≈ 62.5 and 125 ms), three times faster
at 48 kHz. Each pole is now `ba.tau2pole` of its 16 kHz time constant, so it
holds at any rate and equals upstream's exactly at 16 kHz. `notchBank` is
re-declared only to cascade the new `notchSlot`. `loopManager` is not
forwarded (it calls the two-argument shifter); `gainCeiling` is forwarded
unchanged and keeps its per-sample `si.smooth(0.999)`.

**Not fixable by retuning: `fi.pospass(6)` is a poor Hilbert in the voice
band at any guard.** It is an order-6 Butterworth half-band lowpass modulated
to SR/4, so its transition band is thousands of Hz wide; the guard only moves
the cutoff. Measured, corrected shifter, notches bypassed, guard 20 Hz (an
independent scipy model of the same filter agrees to 1e-4 dB):

| | 100 Hz | 200 Hz | 300 Hz | 1 kHz | 3 kHz | 6 kHz | 12 kHz |
|---|---|---|---|---|---|---|---|
| gain at 0 Hz shift (wanted + unrejected image, phase-summed) | | −18.8857 dB | | −6.0318 dB | −0.8318 dB | −0.0434 dB | +0.0000 dB |
| image (f − 4 Hz) re wanted (f + 4 Hz), +4 Hz shift | −0.6928 dB | | −2.0785 dB | −6.9306 dB | | | |

(guard 100 Hz, 0 Hz shift: −15.97 / −5.74 / −0.78 / −0.04 / 0.00 dB at 200 /
1000 / 3000 / 6000 / 12000 Hz, from the scipy model.) The ×2 shows where the
image is gone: 0 dB at 12 kHz, where the uncorrected shifter reads −6.02 dB.

## Which file reproduces which phase-0 result

| Phase-0 result (`_afc_poc/PHASE0.md`) | Reproduce with |
|---|---|
| §3 Dattorro float vs double (2.48e-08, −137 to −140 dB), T30 at the paper defaults (1.1846 s, L) | `dsp/dattorro.dsp`; `FaustVendored.DattorroFloatTracksDouble` / `DattorroAsShippedT30` (2.481e-08, −139.42 dB, 1.184593 s) |
| §3 decay table and the bare-loop cost table | `_afc_poc/phase0-data/dattorro/` (`dattorro_msg.py`), whose headers are generated from this same `.dsp` with the same flags (`-single`/`-double -ftz 1`); they differ from `generated/dattorro_f32.hpp` / `_f64.hpp` only in class name and namespace |
| §3 `dattorro~.mxo` | `dsp/dattorro.dsp` through `faust2max6` (see above) |
| §3 the tank bug | `dsp/dattorro_paper.dsp` fixes it; `FaustVendored.DattorroPaper*` |
| §4 +4.0 / +16.0 / 12.0 dB at 16 kHz | upstream's own `make msg-sweep` (its `tools/`, not vendored) |
| §4 ablation table | `_afc_poc/phase0-data/faust-icc-ablation/msg_ablate.dsp` with `-I third_party/faust/faust-icc` (it uses the as-shipped `icc.lib`) |
| §4 shifter gain as shipped (−14.4 dB at 1 kHz, 16 kHz) | `_afc_poc/phase0-data/review/shiftgain.dsp` against `faust-icc/icc.lib` |
| §4 consequence 1 (the corrected baseline row) and consequence 3 (the 48 kHz retuning) | `dsp/icc_48k.lib`, `dsp/icc_suppressor.dsp`; `FaustVendored.Suppressor*` |
| the howl detector | `dsp/icc_howl_detect.dsp` (upstream's, unchanged); `FaustVendored.HowlDetectorFindsAStationaryTone` |
