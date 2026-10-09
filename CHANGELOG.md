# Changelog

MuTap follows [semantic versioning](https://semver.org): until 1.0.0 the minor
version moves on any change to the public headers' interface or to measured
behaviour (a fingerprint that moves, a threshold that moves), the patch
version on everything else. Every release is a tag `vMAJOR.MINOR.PATCH` on
`main`; MuTap-Max pins a tag at releases and a main-reachable SHA between
them (`scripts/repin.sh`). Dates are the tag's.

## Unreleased

Production-readiness milestone M0 (hygiene).

- **The NaN watchdog** (`include/mutap/watchdog.h`): one finite check per
  block on the input and the residual in `partitioned_fdaf`,
  `partitioned_fdkf`, `pem_afc`, `residual_suppressor`, `aec_chain` and
  `howl_guard::analyze`, read off sums the stages already compute (nothing
  per sample); a non-finite block resets the stage as `reset()` does (the
  guard sends the mic to ARMING), comes out as zeros, and counts on the
  stage's `watchdog_trips()`. Fingerprints unchanged on finite input (all
  nine legs); `tests/test_watchdog.cpp` asserts recovery within one block
  and the counter at 1 for every stage. The Max externals report the
  counter in the MuTap-Max change that follows.
- **README** names the namespace the library has, `tap::mu` (it quoted a
  `mutap::` that never existed), its quick start gained the canceller, and
  every `cpp` block is extracted and compiled as
  `tests/test_readme_snippets.cpp` on every leg. Test counts corrected in
  README and HANDOFF.
- **`PROTOCOL.md`**: the real-room measurement protocol every existing
  citation (§5.1, §5.3, §7.3, §8) resolves to, written from the tools.
- **`docs/claims.md`**: the hand-maintained table of every number README's
  Status quotes, with its gating test, threshold, host and date.
- **`THIRD_PARTY_NOTICES.md`**: the literature-level patent search (not a
  clearance) and the Hilbert coefficients' source terms.
- **`scripts/repin.sh`**: the MuTap-Max submodule re-pin, scripted.
- `CHANGELOG.md` (this file).

## 0.1.0 — 2026-10-09

The state of `main` at the start of the production-readiness effort
(`docs/production-readiness-plan.md`), tagged so the effort has a baseline:
the FDAF-PEM-AFROW feedback canceller on the NLMS and FD-Kalman cores with
the speech and warped predictors, the AEC chain with the residual suppressor
and the learned post-filter certified on the ITU-T battery at 48 and 16 kHz,
`afc_chain` with the frequency shifter, the reverb stages and the howl
guard, the closed-loop and echo harnesses, the RIR fixtures, the fingerprint
gate on nine CI legs and the instruction-count ratchet on three targets, the
book "Quieting the Loop" and the executed notebooks. HANDOFF.md Rev 5 and
README.md Status carry the measured numbers.
