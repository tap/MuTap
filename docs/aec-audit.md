# AEC adversarial audit (Rev 5 follow-up): what the certification proves, and what it cannot see

> An independent wringer pass over the certified AEC chain
> (docs/itu-compliance.md), run on this branch with the probe harness in
> `tools/audit/`. Motivation: "how confident can I be that the AEC is
> actually good — am I being tricked?" The short answer: **the battery's
> numbers are real and reproduce exactly, but they certify an idealized
> world** — every simulated echo path is truncated to exactly the
> canceller's tap count, time-aligned to within 32 samples, driven by a
> perfectly linear loudspeaker on a shared sample clock. Each of those
> four idealizations was attacked here; each one, relaxed to a realistic
> value, takes the chain below the ITU requirement it certifies. None of
> this contradicts the [ALGO]-scope framing the compliance doc states —
> but it sharply bounds what "certified" may be read to mean for a
> deployed terminal.

## What was verified as genuine (no tricks found)

- **The suite is green as claimed**: 181/181 tests pass on this
  container (Release, GCC, ~16 min), including every typed
  float32/double ITU and G.168-adapted row.
- **The certified numbers reproduce**: an independent re-implementation
  of the ITU_EchoLevel scenario in `tools/audit/probe_aec.cpp`
  (`baseline` probe) measures **−76.4 dBm0(A)** on the cabin path —
  identical to the value in the matrix.
- **Thresholds are real regression gates**, not aspirations: every
  assertion carries its measured value, misses of the project's own
  margin targets are called out as (T) rather than hidden, rejected
  designs are documented with their measured failures, and the battery
  runs at both deployment precisions. The methodology is unusually
  honest. The findings below are **scope gaps** — conditions the harness
  never simulates — not falsified claims.

## Findings

Measured with the compliance chain at the certified 48 kHz / block 256 /
2048-tap geometry, CSS at −16 dBm0, same meters as the battery
(A-weighted 35 ms max over the settled tail; ITU_EchoLevel requirement
< −58 dBm0(A), certified margin target < −64).

### F1 — the echo paths cannot exceed the filter, by construction (major)

`compliance_path()` (tests/support/itu_chain.h) resizes every room RIR
to exactly `rs.taps` — the canceller's length — **before**
unit-normalizing. The certified world therefore contains zero unmodeled
tail: the filter can always represent the true path perfectly. The
fixture RIRs are 4096 taps (85 ms at 48 kHz); the canceller models
2048 (43 ms). Measured with the untruncated fixtures:

| Path | Certified (truncated) | Untruncated 4096-tap | Verdict |
|---|---|---|---|
| Cabin (RT60 66.6 ms) | −76.4 dBm0(A) | **−61.5** (ERLE 38.4 dB) | requirement met, margin target lost |
| Studio | −85.7 dBm0(A) | **−34.5** (ERLE 16.2 dB) | **fails the −58 requirement by 24 dB** |

The linear canceller's floor is the tail-to-total energy ratio, and the
suppressor's leakage learner recovers only part of the gap. The cabin
row survives because a car cabin's RT fits mostly inside 43 ms — that
is, the automotive claim stands *for cabins*; the "general hands-free"
studio claim does not survive its own room's real tail. (And the 4096-tap
fixtures are themselves truncations of longer rooms, so reality is worse
for reverberant spaces.)

### F2 — zero clock-skew tolerance, untested (major for deployment)

The harness derives mic and reference from one sample clock. Real
deployments (USB/Bluetooth audio, split render/capture devices) carry
tens-to-hundreds of ppm of skew. Measured (reference drifted vs the
path input by a constant rate offset, 14 s run):

| Skew | Settled residual | ERLE |
|---|---|---|
| 20 ppm | −51.4 dBm0(A) | 31.8 dB |
| 50 ppm | −41.7 | 22.9 |
| 100 ppm | −21.6 | 5.7 |
| 300 ppm | −19.5 | 0.8 |

**20 ppm — a good crystal pair — already fails the −58 requirement**;
100 ppm (ordinary consumer hardware) effectively disables cancellation.
The chain has no drift estimator/compensator and no test asserts any
tolerance. In the Max external's home use (one interface, one clock)
this is moot, and that constraint should be documented; any roadmap
toward split-device use needs drift tracking first.

### F3 — bulk delay beyond the tap span, untested (major for deployment)

The fixture pipeline trims RIR onset delay to 32 samples, so the battery
never sees meaningful reference-to-mic delay (loopback buffering, BT
stacks: 10–100 ms). Measured, cabin path shifted by a bulk delay:

- Delay + path **inside** the 2048-tap span: fine (−77..−81 dBm0(A) —
  the Kalman prior handles in-span delay; note the preset's
  `initial_uncertainty_decay` is correctly off at this geometry).
- Path tail pushed **past** the span: 10 ms → **−56.3** (requirement
  lost), 20 ms → −54.5, 45 ms → −22.0 (ERLE 7 dB).

There is no delay estimation/alignment stage. Every millisecond of
system delay spends a millisecond of the 43 ms modeling budget.

### F4 — loudspeaker nonlinearity, untested (moderate)

The battery's "max volume" clauses drive a perfectly linear path. With
the path fed a soft-clipped reference (tanh, drive normalized to signal
peak) while the canceller sees the clean reference:

| Drive | Settled residual | ERLE |
|---|---|---|
| 0.5 (mild) | −51.7 dBm0(A) | 34.9 dB |
| 1.0 | −44.2 | 23.6 |
| 2.0 | −38.1 | 13.6 |
| 4.0 (hot small speaker) | −36.2 | 8.8 |

Even mild compression fails the single-talk requirement. Real automotive
certification happens against real loudspeakers at max volume; the
[ALGO] scope legitimately excludes transducers, but the claim table
should carry this asterisk, and a nonlinear-path battery row
(recorded-not-asserted first) would bound the exposure.

### F5 — 44.1 kHz / block 256 has a requirement-level convergence deficit (moderate; the external's most common host rate)

The preset's rank-deficiency countermeasures engage only in the 6–12 ms
hop band. 44.1 kHz / block 256 hops at 5.805 ms — just outside — and
measures (cabin, ConvergenceQuiet protocol):

| Geometry | ERL @ 600 ms | @ 1200 ms | @ 2000 ms |
|---|---|---|---|
| 48 kHz (certified) | 33.6 dB | 47.8 | 54.4 |
| 44.1 kHz, stock preset | **17.4** | **35.3** | 45.5 |
| 44.1 kHz + countermeasures forced on | 24.7 | 37.5 | 46.3 |

The ITU requirement analog (40 dB by 1200 ms) **fails at 44.1 kHz either
way**; steady state is fine (−103.9 dBm0(A)). So the notch is not fully
closed by the novelty/prior knobs at this hop, and the preset's
"uncalibrated far outside block 256 at 16..96 kHz" caveat does not cover
it — this *is* block 256 inside 16..96 kHz, and it is Max's default
rate, where `mutap.aec~ @postfilter 1`'s doc string says "certified".
Needs its own measurement campaign (and either a preset fix or an
explicit carve-out in the external's docs).

### F6 — the ERL meter reads best-in-window (minor, methodology)

`erl_reader::by()` returns the **max** ERL over the preceding 350 ms.
The justification (CSS pauses measure meter decay, not echo) is sound,
but the ITU convergence masks bound echo "at any time," and a
best-in-window read can hide brief within-window violations. The
EchoLevel row (strict max of the level trace) is conservative; the
convergence rows are generous. A one-off cross-check with a
mask-conform reading (worst active-segment value) would close the
question.

### F7 — attacked and held (no failure found)

- **Cold start under continuous far end** (no CSS pauses; the
  receive-activity floor de-asserts `receive_active` after ~0.3 s of
  continuous material, releasing the initial guard early): no echo
  burst observed — broadband material converges faster than the guard
  releases. Attenuation trace monotonic 11 → 53 dB over 4 s.
- **Level scale-invariance**: far end at −40/−50/−60 dBm0 holds ERLE
  59.6–59.9 dB (regularization floors do not bite). 
- **Certified-scenario reproduction**: exact match (−76.4).

### F8 — nits

- `residual_suppressor`'s comfort fill drops the imaginary half of the
  deficit at DC/Nyquist (fills half power there; inaudible).
- `aec_chain::m_guard_gain` is computed in the constructor and never
  read; `apply_guard()` recomputes the same `std::pow` every
  receive-active block.
- HANDOFF/itu-compliance still say the external "replicates the preset's
  scaling rule and will switch to calling the library preset at the next
  submodule re-pin" — it already calls `tap::mu::aec_chain_preset`
  directly; the note is stale.

## The wringer plan (what to do next, in order of information per effort)

1. **Add the four impairment axes to the battery as recorded rows**
   (undermodeled tail, bulk-delay sweep, clock-skew sweep, soft-clip
   sweep — `tools/audit/probe_aec.cpp` is the seed), then gate them at
   measured values so the envelope can only be *widened* consciously.
   Report the operating envelope in the README claims table: "certified
   for paths ≤ filter span, shared clock, delay ≤ span, linear speaker."
2. **Double-talk under an undermodeled tail** — untested here and
   plausibly worse than single talk: a large learned leakage λ interacts
   with the Wiener transparency rule exactly where the DT rows are
   tightest. One afternoon with the existing DT machinery.
3. **44.1 kHz campaign** (F5): re-run the Stage 3 hop-band measurement
   at 5.8 ms, decide preset band vs new countermeasure, certify 44.1 kHz
   as a third rate — it is the external's default habitat.
4. **Algorithmic roadmap for the deployment gaps**, if MuTap is to leave
   the same-clock/same-device niche: a bulk-delay estimator/aligner in
   front of the canceller (cheap, classical), then a drift tracker
   (fractional resampler steered by lag drift). Both are prerequisites
   for any claim outside a single audio interface.
5. **Real-path validation**: run Tim's measured cabin/room IRs through
   the fixture pipeline **without** the resize-to-taps step, and a live
   mic+speaker loopback session in Max (the real speaker supplies the
   nonlinearity and the real clock for free). Compare against F1/F4
   predictions.
6. **Long-run float32 soak** on-target (hours of CSS + music at M55/HVX,
   watching for weight drift/denormal stalls) — the QEMU CI legs run a
   test subset only, and the 30 s tone row is the longest float32
   exposure today.
7. **Procure the P.501 real-speech attachments** (already tracked in the
   matrix) — the DT rows are method-equivalent until then, and G.168
   itself warns CSS understates real-speech variance on exactly those
   rows.

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTAP_BUILD_TESTS=ON
cmake --build build -j
g++ -O2 -std=c++20 -I include -I tests -I submodules/dsptap/include \
    tools/audit/probe_aec.cpp build/submodules/dsptap/libtap_dsp_fft.a \
    -o probe_aec
./probe_aec            # all probes; or: baseline|fullpath|bulkdelay|drift|nonlinear|guard|level|rate441
```

`tools/audit/probe_441.cpp` (same build line) is the F5 follow-up: the
44.1 kHz ERL trajectory with and without the notch countermeasures
forced on, against the 48 kHz control.

## F2 follow-up: SampleRateTap closes most of the clock-skew gap

`tools/audit/probe_drift_asrc.cpp` (add
`-I <SampleRateTap>/include` to the build line) routes the drifted
reference through `tap::samplerate::async_sample_rate_converter` — the
sibling repo's near-unity ASRC — before the chain, modeling the real
split-clock deployment (push at the render clock, pull at the capture
clock). Measured, cabin path with 5 ms of playout delay, CSS −16 dBm0,
31.5 s:

| Skew | Naive (audit F2) | Via ASRC |
|---|---|---|
| 0 ppm (transparency control) | −85.8 dBm0(A) / 70.7 dB ERLE | **−91.8 / 75.5** |
| 50 ppm | −36.8 / 19.9 | **−55.9 / 36.1** |
| 100 ppm | −29.4 / 14.6 | **−58.7 / 35.7** |
| 300 ppm | −19.8 / 1.6 | **−58.9 / 37.0** |

The servo locks exactly (reported ppm 49.97/99.99/300.00, zero
underruns/overruns/resyncs) and the outcome is **drift-independent**:
catastrophic collapse becomes a stable ~36 dB ERLE operating point at
the −58 requirement wire (not the certified −76 margin). Two findings
that must travel with any integration:

1. **The ASRC's ~1.5 ms designed latency must be hidden inside the
   playout delay.** With the audit fixtures' zero-delay paths the
   recovered reference *lags* the echo, the direct sound goes
   non-causal, and cancellation collapses to ~12 dB ERLE **even at
   0 ppm**. Real render stacks always buffer more than 1.5 ms, but the
   simulation must model it, and the external's docs should say "tap
   the reference before the ASRC, at the point it is handed to the
   render device."
2. The residual ~36 dB ERLE ceiling under drift is a **lower bound** —
   this rig's render-clock stream is synthesized with a linear
   interpolator (its own distortion floor), where a real device stream
   is native. Re-measure with a high-quality synthesis resampler before
   attributing the ceiling to the ASRC's phase breathing. Transfer
   granularity here is 4-frame chunks (the servo's QUIET-capable
   regime); coarse-callback transfer (TRACK-mode "latency breathing")
   should be measured at the deployment's real callback size.

The other SampleRateTap pieces map onto the remaining drift roadmap:
`fractional_resampler::process()` takes the rate deviation as an
external per-call input, so it is the steerable engine for the
no-second-clock case (drift baked into pre-merged streams), needing
only a MuTap-side lag-drift estimator to steer it; and `pi_servo`'s
plant model (occupancy as a pure integrator of rate error) is formally
identical to inter-stream lag drift, so its three-stage
gain-scheduled loop, lock/unlock discipline and rate-scaling carry
over to that estimator unchanged.

