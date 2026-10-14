# Support — what is validated, what is scaled by construction, what is not

The production-readiness plan's M9b writes this file in full at the first
release; this is its first section, written at M2c (14 October 2026) because
M2c's pass criterion asks where every sample rate stands. Nothing here is a
promise beyond what the named test measures.

## Sample rates and geometries

The library is rate-agnostic by construction: every constant that is a time
or a frequency is derived from the sample rate by a preset — `aec_chain_preset`
(`postfilter.h`) for the echo chain, `pem_afc_preset` and `afc_guard_preset`
(`afc_preset.h`) for the feedback canceller and its guard. What is *validated*
is narrower, and the table says which test does it.

| Rate | Feedback canceller (`pem_afc`, `mutap.afc~`) | Echo chain (`aec_chain`, `mutap.aec~`) |
|---|---|---|
| 48 kHz | **Validated.** Every closed-loop claim in README's Status (`tests/test_pem_afc.cpp`, `test_fd_kalman.cpp`, `test_rir_fixtures.cpp`, `test_afc_chain.cpp`, the karaoke, two-mic, reverb and guard suites) runs at 48 kHz, block 64 / 1024 taps; `mutap.afc~` ships block 256 / 2048 taps, and the preset reproduces its configuration at both geometries field for field (`tests/test_afc_preset.cpp`). The product geometry is M2b's decision, after M1. | **Validated.** The ITU battery at block 256 (`docs/itu-compliance.md`). |
| 16 kHz | Not run. | **Validated.** The ITU battery's second required rate, block 256. |
| 44.1 kHz | **Scaled by construction, not loop-validated.** The preset puts the pitch search at 120–1500 Hz (29–368 samples), the window at 21.3 ms (1024 samples at block 256) and the shadow's time constant at 51 ms; open-loop identification on a synthetic echo reaches the same misalignment as at 48 kHz (`AfcPreset.OpenLoopIdentificationMatchesAcrossRates`, the measured values in its header). No closed-loop row exists at this rate: the rooms, the loudspeaker band and the materials of `tests/support/` are 48 kHz fixtures. | The P.501 signals are native at 44.1 kHz; the chain is validated at 48 and 16 only. |
| 96 kHz | **Scaled by construction, not loop-validated**, as 44.1 kHz: lags 64–800 samples, window 2048 samples at block 256 (the capacity constraint `analysis_capacity ≥ 2·max_lag` holds, which the sample-count defaults would have broken), same open-loop test. The warped predictor's warping coefficient is a 48 kHz tuning and is deliberately not rescaled (`afc_preset.h`). | Not run; the AEC presets' two stated exceptions (transition, floor bias) apply. |
| other | Any finite rate builds a valid configuration (`AfcPreset.ConstructsEveryInstantiationAtEveryRate` covers 44.1 / 48 / 96 kHz at blocks 64 and 256; the guard preset 16 / 44.1 / 48 / 96 kHz at blocks 64, 256 and 1024). Unvalidated. | Unvalidated. |

What validating 44.1 or 96 kHz in the loop would take: the RIR fixtures
regenerated at that rate (`tools/fixtures/make_rir_fixtures.py` from the
same image-source geometry), `tests/support/loudspeaker_band.h`'s sections
and `closed_loop.h`'s materials parameterised by the rate, and the claims'
rows re-measured with fresh thresholds — a measurement campaign, not a
code change, and one M8's measured rooms would also need.

## Channels

Single-channel only: one microphone, one loudspeaker feed, per `mutap.afc~`
object; one far end per `mutap.aec~`. A stereo far end is scoped out of the
production-readiness quarter (plan §2). Multi-microphone feedback
cancellation on one shared reference exists in the library (`afc_chain`,
`docs/two-mic-afc.md`) with no external yet.

## Clocks

The microphone and the loudspeaker feed are assumed to share one clock (one
interface). There is no clock-drift compensation; two-device echo
cancellation is a known gap (plan §7).
