# Third-Party Notices

MuTap itself is licensed under the MIT License (see `LICENSE`), © 2026 MuTap
contributors. It bundles or fetches the third-party components listed below,
each of which remains under its own license. Nothing here is GPL- or
LGPL-encumbered: the FAUST-generated code under `third_party/faust/` is
compiled from FAUST library files that are LGPL-2.1-or-later, and the
exception those files grant (quoted in its section below) leaves the compiled
code's license free.

If you redistribute binaries built from MuTap, you are responsible for
carrying forward the notices of whichever components you link in (notably,
for builds from DspTap pins before tap/DspTap#42, the Ooura FFT, whose derived
port compiled into every consumer via the header `tap::dsp` provides; see
below).

---

## Vendored (committed into this repository)

### Ooura FFT — license record only (no code in DspTap trees from `72977aa` on)
Takuya Ooura's General Purpose FFT Package (split-radix "Fast Version III").
**What MuTap ships today carries no code from it, in DspTap's maintainer's
judgement.** In DspTap trees from `72977aa` (tap/DspTap#42) on — MuTap pins
`2137d86`, which adds the Hexagon tuning of tap/DspTap#44 and the
`<windows.h>` macro hygiene of tap/DspTap#47 — every
floating-point transform MuTap runs on its default engine is DspTap's srdif
engine, `submodules/dsptap/include/tap/dsp/fft/srdif.h` — a split-radix DIF
engine written from the published literature under a recorded clean-room
procedure, MIT — and the fixed-point engine beside it had its real post-pass
re-derived from the literature at tap/DspTap#39. The double profile always
runs srdif; the float profile runs it everywhere except under DspTap's two
accelerated engines (CMSIS-DSP on the bare-metal Cortex-M55, below; Apple
vDSP, which MuTap's root `CMakeLists.txt` turns off by default). It is
compiled into every consumer through the header `tap::dsp` provides; no
`MuTap::fft` or `tap_dsp_fft` target exists in a MuTap build (the latter
appears only under DspTap's `TAP_DSP_FFT_CMSIS` and carries only Arm's
Apache-2.0 CMSIS-DSP objects). `submodules/dsptap/NOTICE.md` is the canonical
statement of that judgement, of the clean-room records behind it
(`submodules/dsptap/docs/fft-design.md`) and of its limits; it is a
maintainer judgement, not legal advice.

**History, for binaries built from older MuTap trees.** MuTap's own history
pins DspTap trees that did carry code from the package, and a binary built
from one of them carries it too:

- MuTap trees before `14116f0` (which moved the FFT to DspTap) vendor
  Ooura's C under `third_party/ooura/`.
- DspTap pins before Stage 2c (`8350f13`, tap/DspTap#32) compile Ooura's C
  itself (`fftsg.c` and DspTap's single-precision wrapper around it).
- Pins from Stage 2b (`bbfa48d`, tap/DspTap#31; from 2b to 2c beside the C)
  up to and including `0c5bf59` route every floating transform through DspTap's C++20 port of
  `rdft`, `include/tap/dsp/fft/split_radix.h` — a statement-for-statement
  transliteration, bit-identical to the C. The port was a **derivative work,
  not the ORIGINAL package**; it carried
  `SPDX-License-Identifier: LicenseRef-Ooura AND MIT` (Ooura's notice verbatim
  as the governing terms for the derived portion, MIT for the wrapper and
  DspTap's additions), and its redistribution relied on the **modification
  grant** of the notice ("modify this code for any purpose"), since
  distribution of a modified derivative is not expressly granted.
- Pins from Stage 3b (`b08f6c6`, tap/DspTap#27) up to and including
  `0c5bf59` also carry a fixed-point real post-pass transcribed from the
  package (`fft/fixed_point.h`), which MuTap does not instantiate.
- Pins from `72977aa` on carry only the license record below.

DspTap's `NOTICE.md` ("Which trees carry what") gives the same history by
DspTap tree.

- **Notice**, verbatim from `submodules/dsptap/LICENSES/LicenseRef-Ooura.txt`
  (the upstream package readme's copyright section; the readme itself is kept
  permanently at `submodules/dsptap/third_party/ooura/readme.txt` as the
  license record for the older trees above):

```text
Copyright:
    Copyright(C) 1996-2001 Takuya OOURA
    email: ooura@mmm.t.u-tokyo.ac.jp
    download: http://momonga.t.u-tokyo.ac.jp/~ooura/fft.html
    You may use, copy, modify this code for any purpose and
    without fee. You may distribute this ORIGINAL package.
```

### Toy dataset fixture — `tools/ml/kws/fixtures/toy/`
The wake-word dataset builder's bring-up corpus (wake-word plan §6 M4a): four
small archives of third-party audio and text (662,293 bytes in all, measured
9 September 2026), cut deterministically from the upstream releases by
`tools/ml/kws/fixtures/make_toy_fixture.py`. The authoritative, dated licence
and attribution rows are `tools/ml/kws/fixtures/toy/manifest.json` (one
`sources[]` entry per archive, attribution text as written there); the
machine-rendered per-file inventory is `tools/ml/kws/fixtures/toy/expected/ATTRIBUTION.csv`
(generated by `kws_dataset_card.py`, never typed by hand — this table points
at those files rather than copying their rows). No other corpus audio is
committed; Common Voice never is.

| Archive | Upstream | License | Contents |
|---|---|---|---|
| `speech_commands_v0.02_toy.tar.gz` | Speech Commands v0.02 (Warden 2018, arXiv:1804.03209) | CC BY 4.0 | 15 keyword clips, the reduced `validation_list.txt` / `testing_list.txt` and the upstream `LICENSE`, laid out as upstream |
| `musan_toy.tar.gz` | MUSAN, OpenSLR SLR17 (Snyder, Chen & Povey 2015, arXiv:1510.08484) | CC BY 4.0 corpus; per-file terms — 4 sound-bible noise files CC BY 3.0, 2 fma music tracks CC BY — in the subset `LICENSE` blocks and `ANNOTATIONS` rows retained inside the tarball | 2-second excerpts of 4 noise and 2 music files |
| `rirs_noises_toy.zip` | RIRs and Noises, OpenSLR SLR28 (Ko et al. 2017), simulated subset | Apache-2.0, as stated on the OpenSLR resource page (the release carries no LICENSE file); the subset `README` is retained | 2 simulated small-room RIRs and their `rir_list` lines |
| `speech_commands_keywords.tar.gz` | Speech Commands v0.02 keyword names | CC BY 4.0 | the 35 keyword names, one per line |

### faust-icc — `third_party/faust/faust-icc/`
`icc.lib` and `LICENSE` from https://github.com/Dhwaani/In-CarCommunication at
commit `3626a940a4f511d8c21f2b0cb40af1e8de9e2f1e`, byte-identical (their blob
hashes, `1869db4e…` and `e8ce238b…`, match the commit's; `faust-icc/UPSTREAM`
records the URL and commit). `third_party/faust/dsp/icc_48k.lib` imports it
unmodified and overrides three functions (`third_party/faust/README.md` says
which and why); the overrides are MuTap's, under MIT. Used only by the
anti-howl PoC's baseline suppressor and its tests. The license, verbatim:

> MIT License
>
> Copyright (c) 2026 Ashmita Chakraborty
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

### FAUST-generated C++ — `third_party/faust/generated/`
Generated by **FAUST 2.88.0** (GRAME) from the `.dsp` files in
`third_party/faust/dsp/` and the FAUST standard libraries bundled with that
release, by `third_party/faust/generate.sh`. The generated code inlines the
whole call tree of the library functions each `.dsp` uses. It is compiled
only by the host test `tests/test_faust_vendored.cpp`; nothing under
`include/` includes it, and no MuTap library target links it.
`third_party/faust/dsp/dattorro_paper.dsp` also carries a modified local copy
of `re.dattorro_rev`'s body, with its author and STK-4.3 license declaration
kept.

The functions the `.dsp` files call, and each one's license: as declared in
the library file (`declare <fn> license` / `licence`) where it is, otherwise
the license of the file section it sits in (the FAUST libraries mark "jos
sections" released under STK-4.3 and "GRAME" sections under the file's
LGPL-with-exception header). Each generated class's `metadata()` repeats
every per-function declaration FAUST found in its call tree; across the eight
classes those are STK-4.3 throughout, plus `icc.lib`'s MIT and `maths.lib`'s
LGPL-with-exception.

| Library (version in 2.88.0) | Functions | License |
|---|---|---|
| `reverbs.lib` 1.5.1 | `dattorro_rev` (author Jakob Zerbian) | STK-4.3, declared |
| `filters.lib` 1.9.0 | `pospass`, `lowpass`, `lowpass0_highpass1`, `iir`, `fir`, `tf2`, `tf2s`, `notchw`, `resonbp` (© 2003-2019 Julius O. Smith III) | STK-4.3, each declared |
| `filters.lib` 1.9.0 | `_pospass0` (`pospass`'s internal helper) | none declared; the file states that each function carries its own |
| `signals.lib` 1.7.0 | `onePoleSwitching` (Jonatan Liljedahl, revised by Dario Sanfilippo) | STK-4.3, declared |
| `signals.lib` 1.7.0 | `smooth`, `cmul` | jos section: STK-4.3 |
| `signals.lib` 1.7.0 | `bus` | GRAME section: LGPL-2.1-or-later with the FAUST exception |
| `analyzers.lib` 1.4.0 | `amp_follower_ar` (Jonatan Liljedahl, revised by Romain Michon) | jos section: STK-4.3 |
| `basics.lib` 1.23.0 | `tau2pole` | jos section: STK-4.3 |
| `basics.lib` 1.23.0 | `take`, `if`, `sAndH` (author Romain Michon) | GRAME section 2: LGPL-2.1-or-later with the FAUST exception |
| `routes.lib` 1.4.0 | `cross` | LGPL-2.1-or-later with the FAUST exception |
| `oscillators.lib` 1.8.0 | `oscsin`, `osccos`, `phasor`, `sinwaveform`, `coswaveform` | GRAME section: LGPL-2.1-or-later with the FAUST exception |
| `maths.lib` 2.9.0 | `SR`, `PI`, `EPSILON` | `LicenseRef-LGPL-2.1-or-later-with-Faust-exception`, declared for the file |
| `platform.lib` 1.3.0 | `SR`, `tablesize` | LGPL-2.1-or-later with the FAUST exception |
| `icc.lib` 0.1.0 (faust-icc) | `howlDetect`, `bandProbe`, `argmaxBands`, `pickLouder`, `meanBands`, `logBandFreq` | MIT, declared for the file |

**The FAUST exception.** Each LGPL FAUST library file above carries this in
its header (quoted from `maths.lib`):

> EXCEPTION TO THE LGPL LICENSE : As a special exception, you may create a
> larger FAUST program which directly or indirectly imports this library
> file and still distribute the compiled code generated by the FAUST
> compiler, or a modified version of this compiled code, under your own
> copyright and license. This EXCEPTION TO THE LGPL LICENSE explicitly
> grants you the right to freely choose the license for the resulting
> compiled code. In particular the resulting compiled code has no obligation
> to be LGPL or GPL. For example you are free to choose a commercial or
> closed source license or any other license if you decide so.

So the LGPL parts impose nothing on the generated headers. The STK-4.3 parts
are MIT-style and carry a notice requirement ("The above copyright notice and
this permission notice shall be included in all copies or substantial
portions of the Software"); the generated code is such a portion, so the
notice follows, as `filters.lib` states it. Its non-binding request that
modifications be sent upstream applies to the tank fix in
`dattorro_paper.dsp`, which phase 0 already recommends reporting.

> Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>
> (`dattorro_rev`: author Jakob Zerbian)
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of
> this software and associated documentation files (the "Software"), to deal in
> the Software without restriction, including without limitation the rights to
> use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
> of the Software, and to permit persons to whom the Software is furnished to do
> so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> Any person wishing to distribute modifications to the Software is asked to send
> the modifications to the original developer so that they can be incorporated
> into the canonical version.  For software copyrighted by Julius O. Smith III,
> email your modifications to <jos@ccrma.stanford.edu>.  This is, however, not a
> binding provision of this license.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

The shim (`third_party/faust/faust_shim.h`, `faust_generated.h`),
`generate.sh`, and the `.dsp` / `.lib` files other than the copied Dattorro
body are MuTap's own, under MuTap's MIT license.

---

## Fetched at build time (not committed, not redistributed by MuTap)

| Component | License | When fetched | Notes |
|---|---|---|---|
| **GoogleTest** 1.14.0 | BSD-3-Clause | Only when `MUTAP_BUILD_TESTS=ON` | Test-only; `INSTALL_GTEST=OFF`. Never linked into a distributed MuTap artifact. |

---

## Build-time subprocess tools (installed by the maintainer, never fetched, never redistributed)

The wake-word dataset builder (`tools/ml/kws/kws_build.py synth`, wake-word
plan §6 M4) synthesizes its training positives and TTS negatives by running
these as **separate processes** on the maintainer's machine. They are not
imported by any Python module in this repository, not linked into any binary,
not fetched by any build, not present in CI, and not shipped: what leaves the
build is synthesized audio and features derived from it, which the GPL does
not reach (the GPL covers the program and works derived from its code, not
the output of running it — GPL-3.0 §0 and §2). The "Nothing here is GPL- or
LGPL-encumbered" sentence above therefore stays true of everything MuTap
bundles, links or distributes; this table exists so that it is a documented
fact rather than an omission. `tools/ml/kws/kws_dataset_card.py` renders the
same rows into `DATASET.md` from the build's lock, with the versions the build
actually ran.

| Component | License | Role | Notes |
|---|---|---|---|
| **piper-tts** ≥ 1.3 (`piper`) | GPL-3.0-or-later | Text-to-speech synthesis of keyword positives and TTS negatives (`synth` stage) | Subprocess in the `--jobs` pool; embeds **espeak-ng**. Runs ONNX Runtime on CPU. Voices are pinned `.onnx` + `.json` models under their own terms (lineage verified at M0, recorded in the manifest and per clip in the lock); the sample generator's bundled Lessac-lineage `.pt` generator is excluded. Output is not a covered work. |
| **espeak-ng** (embedded in piper-tts) | GPL-3.0-or-later | Grapheme-to-phoneme inside Piper's phonemizer | Subprocess only, inside `piper`; no separate espeak-ng binary is installed or used at M4a. M4b's near-miss keyword mining will call the same phonemizer as a subprocess and store the mined list and the phonemizer version in the manifest, so a lexicon update cannot move the negative set silently. |

---

## Algorithm / formula references (cited, not code dependencies)

MuTap implements published algorithms; algorithms and mathematical formulas
are not copyrightable, and no code is taken from these sources. The key papers
(PEM-AFROW, FDAF-PEM-AFROW, the acoustic feedback control survey) are listed in
`HANDOFF.md` and cited in source comments where implemented.

### Patent literature search (9 October 2026) — a search, not a clearance

The production-readiness plan (M0f) asked for a literature-level search of
the patent landscape around the four techniques MuTap implements, recorded
here as exactly what it is: a search run by Claude Code over Google Patents
and the open literature on one day, with the status labels Google Patents
displayed ("Expired – Lifetime", "Expired – Fee Related", "Active",
"abandoned") copied as shown and **not** verified against USPTO PAIR,
maintenance-fee records or the EPO Register. Only US/EP/WO members were
looked at; family members elsewhere were not enumerated; claims were read
at claim 1. It is not a freedom-to-operate opinion, it cannot gate a
release, and nothing in it is an opinion on infringement. A formal review
is parked in the plan (§7).

Every technique is implemented from the published papers HANDOFF.md lists,
and in each area the publications predate the active filings found.

**1. PEM-based feedback cancellation** (prewhitening both the adaptive
filter's input and desired signal by a near-end model; `pem_afc.h`,
`lpc.h`). Prior art: Spriet, Proudler, Moonen, Wouters, IEEE TSP 53(10),
2005 (PEM-AFC); Rombouts, van Waterschoot, Moonen, JAES 55(11), 2007
(PEM-AFROW); Gil-Cacho, van Waterschoot, Moonen, Jensen, EUSIPCO 2012 and
IEEE/ACM TASLP 22(12), 2014 (FDAF-PEM); van Waterschoot & Moonen, Proc.
IEEE 99(2), 2011 (survey). Filings found: US8422708B2 (Oticon, priority
2008, active to 2031: long-term prediction filters for adaptive whitening
in a hearing instrument); US9271090B2 (Cirrus Logic, priority 2007, active
to 2031: whitening "tone-removal" blocks before the AFC update);
US11722819B2 (Meta, filed 2021, active: LP whitening of the error plus a
frequency-domain state-space AFC in an entrainment-mitigation pipeline);
US11849283B2 (Univ. of California, priority 2019, active: all-pass
frequency warping as a decorrelation preprocessor, not warped-LPC
prewhitening); WO2015044915A1 (Univ. of Porto, 2013; US phase abandoned);
US8218788B2 (Yamaha, 2008, expired – fee related; cites PEM-AFROW as
background). No KU Leuven- or Cochlear-assigned patent on PEM prewhitening
surfaced under the authors' names.

**2. Frequency-domain partitioned-block Kalman filtering** (`fd_kalman.h`).
Prior art: Enzner & Vary, Signal Processing 86(6), 2006; Malik & Enzner,
IEEE TASLP 20(7), 2012; Kuech, Mabande, Enzner, ICASSP 2014; Yang, Enzner,
Yang, IEEE SPL 24(12), 2017; Bernardi, van Waterschoot, Wouters, Moonen,
IEEE/ACM TASLP 25(9), 2017 (PEM-wrapped). Filings found: US5995620A
(Ericsson, 1995, expired – lifetime: a diagonal-covariance Kalman echo
canceller); US8924337B2 (Nokia, priority 2011, shown as expired – fee
related: multichannel frequency-domain state-space Kalman AEC with
estimated noise covariances plus a post-filter — the closest family, and
lapsed); EP3329594B1 / US10454454B2 (Fraunhofer / FAU, priority 2015,
granted: an approximated gradient constraint with later correction, a
complexity trick MuTap does not use); US11722819B2 (Meta, above);
US12401945B2 (Amazon, recent, active; not opened). No Enzner/Vary- or
RWTH-assigned patent on the 2006 filter was found.

**3. Dual-path / shadow-filter comparators** (`pem_afc.h`'s shadow,
`aec_chain`'s shadow trigger). Prior art: Ochiai, Araseki, Ogihara, IEEE
Trans. Commun. 25(6), 1977; Haneda, Makino, Kojima, Shimauchi, EUSIPCO
1996; ITU-T G.168's generic two-filter architecture. Filings found, every
one shown as expired, lapsed or abandoned (the newest filing year 2003):
US3787645A (NEC, 1972, the root patent); US5933797A (Ericsson, 1997);
US6163609A / EP0872962 (Nokia, 1997); US7031459B2 (Tellabs lineage, 1997);
US7035397B2 (Agere, 2001, expired – fee related 2023); US5649012A (Hughes,
1995); US6947549B2 (HK PolyU, 2003, expired – fee related 2023);
US20030219113A1 (Intel, 2002, abandoned); US7408891B2 (Mitel, 2002,
expired 2025).

**4. Residual-echo suppression with comfort noise matched to the near-end
floor** (`postfilter.h`: coherence-driven Wiener gains, two-window minimum
statistics, comfort fill). Prior art: Martin, IEEE TSAP 9(5), 2001 (minimum
statistics; EUSIPCO 1994); Gustafsson, Martin, Vary (1998–2002) and
Enzner, Mauler, Vary (DAGA 2004) on combined post-filters; Hänsler &
Schmidt, *Acoustic Echo and Noise Control*, 2004; ITU-T G.168's
comfort-noise requirements. Filings found: the 1990s families are expired
— US5937060A (Texas Instruments, 1997), US5949888A (Hughes, 1995),
US6622030B1 (Ericsson, 2000, expired 2021), US7027591B2 (Ericsson, 2002);
shown active: US7649988B2 (Cirrus Logic, priority 2004, to 2028: a
specific comfort-noise generator from a modified Doblinger estimate),
US8189766B1 (Audience/Samsung, 2007, to 2030: a blind sub-band
post-filter), US9167342B2 (Microsoft/Skype, 2012, to 2033: echo-power-
driven suppression from a time-domain FIR estimate), US8811601B2
(Qualcomm, 2011, to 2033: an integrated AEC + noise suppression + echo
post-processing pipeline); status unverified: WO2012158163A1 (Google,
2011, per-band coherence suppression factors; no US grant located),
EP2673777 (Dolby, 2011, granted; US member not identified); shown lapsed:
US9363600B2 (Apple, 2014), US9185506B1 (Amazon, 2013), US7433463B2
(Clarity/Qualcomm, 2004). No patent naming Martin on minimum statistics
was found.

**Frequency shifting** (`frequency_shifter.h`): Schroeder, JAES 10(2),
1962 and JASA 36(9), 1964 — publication prior art; the earliest
frequency-shift anti-singing patent found, US3429999A (Collins Radio, filed
1966), and US4039753A (1975) are expired. **IIR allpass-pair Hilbert
transformers**: Regalia, Mitra, Vaidyanathan, Proc. IEEE 76(1), 1988, and
Harris, Berdahl, Abel, AES 129th Convention, 2010 — publication prior art;
the allpass 90° network patents found (US5654909A / US5691929, Icom, 1994;
US5504455A, 1995) are shown as expired.

### Hilbert allpass-pair coefficients — `include/mutap/frequency_shifter.h`
The eight coefficients of `tap::mu::allpass_hilbert` (`hilbert_detail::k_coefficients`,
also in `tests/support/decorrelated_loop.h`'s history and
`tools/fixtures/test_howl_criterion.py`) are Olli Niemitalo's published 4+4
IIR allpass-pair 90-degree phase-difference ("Hilbert transformer") design,
which he published on his website (yehar.com). No code is taken from that
source: the filter structure (2nd-order allpass sections in z^-2) is the
standard one, and the implementation is MuTap's own, under MIT.

**Provenance, as far as this repository records it.** The numbers entered
the tree in `tools/fixtures/test_howl_criterion.py` (tap/MuTap#57, `0b25002`;
before that PR's squash they were read from the anti-howl PoC phase 0
review's untracked `dl_iir.h`), then `tests/support/decorrelated_loop.h`
(tap/MuTap#65, `b56c7a4`), and now the library. Neither those commits, their
comments, nor the review file name the page they were copied from or state
any licence or terms of use.

**Source and licence.** The page is Olli Niemitalo, "Hilbert transform",
https://yehar.com/blog/?p=368 (posted 2003-07-03, updated 2020-05-11; checked
2026-09-29 and again 2026-10-09), which lists exactly these eight
coefficients as its equations 2 and 3 and, in an update dated 2019-06-28,
refers to the author's Signal Processing Stack Exchange answer
https://dsp.stackexchange.com/a/59157/15347 for the coefficient calculation
(using Laurent de Soras's HIIR design code). **The page states no licence or
terms of use.** The Stack Exchange answer is user-contributed content, which
Stack Exchange's terms of service license under **CC BY-SA 4.0** for
contributions made from 2 May 2018 (this answer dates from June 2019); that
licence carries attribution and share-alike terms for the *text and code of
the answer*, which MuTap does not copy — the answer describes a design
procedure, and the eight numbers are that procedure's output. (Stack
Exchange could not be reached from the environment that made the 2026-10-09
check, so the answer's own licence line was not re-read; the terms quoted
are the site's published ones for its date.) Treat the status of the
numbers themselves as unstated rather than as permissive: they are the
outputs of a numerical design procedure, and whether such numbers are
protectable at all is a legal question this notice does not answer. The
production-readiness audit (9 October 2026, item 12) decided to keep them:
a redesign would move every frequency-shift row in the repository silently
(the shifter is in no fingerprint) and the sweeps behind those rows cost
hours each, so any redesign is scheduled with its re-measurement as a
stated cost.
