# Third-Party Notices

MuTap itself is licensed under the MIT License (see `LICENSE`), © 2026 MuTap
contributors. It bundles or fetches the third-party components listed below,
each of which remains under its own license. Nothing here is GPL- or
LGPL-encumbered.

If you redistribute binaries built from MuTap, you are responsible for
carrying forward the notices of whichever components you link in (notably the
Ooura FFT, which compiles into every consumer via `MuTap::fft`).

---

## Vendored (committed into this repository)

### Ooura FFT — `third_party/ooura/fftsg.c`
Takuya Ooura's General Purpose FFT Package (split-radix "Fast Version III").
Only the single file `fftsg.c` is bundled (plus `fftsg_float.c`, an in-house
single-precision instantiation of the same source); `third_party/ooura/readme.txt`
is the upstream package description.

> Copyright(C) 1996-2001 Takuya OOURA
> (email: ooura@mmm.t.u-tokyo.ac.jp,
> download: http://momonga.t.u-tokyo.ac.jp/~ooura/fft.html)
> You may use, copy, modify this code for any purpose and without fee.
> You may distribute this ORIGINAL package.

This is a permissive grant, compatible with redistribution inside an
MIT-licensed project. The copyright/permission notice is retained at the top of
`fftsg.c`.

---

## Fetched at build time (not committed, not redistributed by MuTap)

| Component | License | When fetched | Notes |
|---|---|---|---|
| **GoogleTest** 1.14.0 | BSD-3-Clause | Only when `MUTAP_BUILD_TESTS=ON` | Test-only; `INSTALL_GTEST=OFF`. Never linked into a distributed MuTap artifact. |

---

## Algorithm / formula references (cited, not code dependencies)

MuTap implements published algorithms; algorithms and mathematical formulas
are not copyrightable, and no code is taken from these sources. The key papers
(PEM-AFROW, FDAF-PEM-AFROW, the acoustic feedback control survey) are listed in
`HANDOFF.md` and cited in source comments where implemented.
