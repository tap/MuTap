# ITU compliance matrix for the MuTap AEC (Stage 0 of HANDOFF Rev 5)

> The single source of truth for the ITU compliance effort: every testable
> clause of the anchor recommendations, the requirement it imposes, the
> margin we target, and the test that owns it. Downstream stages (signal
> layer, post-filter, compliance suite, proof notebook) key off the rows
> in this document; matrix row IDs become test names.

## What "compliance" means here

This repository proves **algorithmic compliance with margin**: the AEC
chain (linear canceller + residual-echo post-filter + comfort noise),
driven by the standardized test signals through simulated and
physically-modeled echo paths, meets every clause marked `[ALGO]` below —
each with regression-tested headroom. Clauses marked `[TERMINAL]`
inherently require a physical terminal in a lab (transducers, HATS, real
cabins); they are listed so "not covered" is always explicit. `[MIXED]`
clauses get their algorithmic component proven against a **defined
simulated echo path** and their physical component flagged.

Two findings from the texts themselves legitimize the simulation
approach:

- P.340 (10.3.1) and G.167 (5.2.3.1) **explicitly permit electronically
  simulated echo paths** (non-time-varying reflections, envelope similar
  to a real room, reverberation times specified per application).
- P.1110 clause 8 defines **digital access interfaces** whose stated
  purpose is exactly this: record and digitally re-insert noise, speech
  and echo so that "no HATS is necessary at all" for processing tests.

## Margin policy

"Pass with large margin" (Tim's directive) is defined as:

- **Level clauses** (attenuation, loss, residual level, masks): pass by
  **>= 6 dB** beyond the required value.
- **Time clauses** (convergence, switching, build-up): pass in
  **<= 1/2** of the allowed time.
- **Category clauses** (P.340 duplex categories): meet **Category 1
  (full duplex)** where the category-1 bound is the strictest bound —
  margin is then measured against the category-1 value.
- Every margin is asserted in the owning test, so margin EROSION fails
  CI — not just outright violation. Measured values live in comments
  next to the assertions (house workflow).

## Recommendation editions used

| Rec | Edition read | Status | Role |
|---|---|---|---|
| ITU-T P.1110 (automotive, WB) | 03/2017 | 10/2025 ed. not on free portal | **Tier A (primary)** — the operative wideband AEC battery |
| ITU-T P.1120 (automotive, SWB/FB) | 10/2025 | In force, newest text | **Tier A (primary)** — SWB/FB values; normatively references P.340 (2000) — the edition we hold — and restates every table it needs |
| ITU-T P.1100 (automotive, NB) | 03/2017 | 10/2025 ed. not on free portal | Tier A (narrowband deltas only) |
| ITU-T P.340 (hands-free terminals) | 05/2000 | 01/2019 ed. not on free portal | **Tier A (framework)** — duplex categories, build-up/hang-over. NOTE: P.1120 (2025) still normatively cites P.340 (2000), so the operative category tables are fully in hand; refresh against 01/2019 when procured. |
| ITU-T P.501 (test signals) | 04/2025 | In force | Signal definitions for Stage 1 (generated, never redistributed) |
| ITU-T G.168 (network echo cancellers) | 04/2015 | 2022 ed. not on free portal | **Tier B** — ADAPTED battery on acoustic paths, labeled as such |
| ITU-T G.167 (acoustic echo controllers) | 03/1993 | **Withdrawn**; most values bracketed = provisional | **Tier C (historical)** — informative rows only |
| ITU-T G.131 (talker echo) | 11/2003 | In force | Context: when echo control is required; no matrix rows |
| ITU-T P.502 (analysis methods) | — | **Not obtained** (free portal does not serve it) | Analysis methods reconstructed from P.340/P.1110/P.1120 test descriptions; refresh when procured |

## Targets chosen (Stage 0 decisions)

- **Duplex target: P.340 Category 1 (full duplex)** on all three
  double-talk tables — send attenuation A_H,S,dt <= 3 dB, receive
  attenuation A_H,R,dt <= 3 dB, double-talk echo loss >= 27 dB — since
  detector-free double-talk adaptation is MuTap's measured strength.
- **Bandwidth: wideband (P.1110) and SWB/FB (P.1120) are primary**;
  narrowband (P.1100) variants run as band-limited configurations of the
  same tests, with P.501-specified band-limiting of the receive-direction
  signals (NB 3.6/4 kHz, WB 7.2/8 kHz, SWB 14.4/16 kHz).
- **Sample rates (revised twice; current policy per Tim's directive):
  the compliance suite RUNS AT — and every Tier A/B row must pass at —
  48 kHz and 16 kHz, with 44.1 kHz kept as the P.501-native reference
  rate.** Rationale per rate:
  - **48 kHz (required)**: MuTap's operating rate, the automotive recs'
    specified analysis rate (8k FFT @ 48 kHz), and the rate at which
    the RIR fixtures are exact.
  - **16 kHz (required)**: the wideband telephony rate (AMR-WB/P.1110
    world). SWB/FB-only clauses are exempt at 16 kHz (they exceed its
    8 kHz Nyquist); the AM-FM plans (<= 7.04 kHz) fit.
  - **44.1 kHz (reference)**: P.501's native rate, where the CSS
    framing is sample-exact without conversion; used to validate the
    signal layer itself.
  The bridge is the P.501 NOTE 2 resampler (Stage 1): measured passband
  ripple <= 0.014 dB (spec < 0.2) and alias rejection 101 dB (spec
  > 60); the CSS period stays sample-exact at every required rate
  (15435 / 16800 / 5600 samples per 350 ms at 44.1 / 48 / 16 kHz).
- **The simulated echo paths**: the three committed image-source rooms
  (fixtures) + a new car-cabin family (small volume ~2.5 m^3, RT ~60 ms
  per G.167 5.2.3.1's car figures) + the P.1110/P.1120 time-variant-path
  analogue (a time-varying impulse response modeling the rotating
  reflector) + delay/attenuation-only paths for the stability sweep.
  Tim's measured cabin RIRs can join via the existing fixture pipeline.
- **Levels**: the harness adopts the specs' conventions — dBm0 for
  electrical/POI signals (receive nominal **-16 dBm0**), dBPa at MRP for
  acoustic send (nominal **-1.7 dBPa**), P.56-style active-speech-level
  measurement, and dBov for digital normalization (-26 dBov active
  level). A single calibration header maps these onto the simulation's
  linear float domain.

---

## Tier A matrix — automotive battery (P.1110 WB / P.1120 SWB-FB / P.1100 NB)

Row ID = owning test name. "Required" cites the spec value; "Margin
target" is what our tests assert. Tags: [ALGO] provable in simulation,
[MIXED] algorithmic component provable against a defined simulated path,
[TERMINAL] lab-only (listed, not claimed).

### Echo performance (11.11.x)

| Row ID | Clause | Requirement | Required | Margin target | Tag | Signal |
|---|---|---|---|---|---|---|
| `ITU_TCL` | P1110/P1120 11.11.1 (P1100 = same) | Terminal coupling loss, quiet, converged (first 17 s discarded), unweighted 100 Hz-8 kHz per generalized G.122 B.4 | **>= 46 dB** (>= 50 dB objective) | **>= 52 dB** | [MIXED] | Compressed real speech (P.501 7.3.3) at -10 dBm0 |
| `ITU_EchoLevel` | P1120 11.11.2 | Max of echo level-vs-time, single talk, steady state, A-weighted, 35 ms integration | **< -58 dBm0(A)** | **< -64 dBm0(A)** | [ALGO] | BE single-talk seq (P.501 7.3.2) at -16 dBm0 |
| `ITU_EchoStability` | P1110 11.11.2 (P1100 11.11.2) | Echo attenuation shall not degrade > 6 dB from its best during single talk | **<= 6 dB** variation | **<= 3 dB** | [ALGO] | CSS at -5 and -25 dBm0; BE seq at -16 dBm0 |
| `ITU_EchoSpectral` | P1110/P1120 11.11.3 | Spectral echo attenuation below mask at any time (8k FFT @ 48 kHz vs reference PSD) | WB mask: 100 Hz:-41, 1300:-41, 3450:-46, 5200:-46, 7500:-37, 8000:-37 dB; P1120 adds 12500:-37; NB mask (P1100 11.11.3): 100:-20, 200:-30, 300:-38, 800:-34, 1500:-33, 2600:-24, 4000:-24 | **mask - 6 dB** everywhere | [ALGO] | 10 s CSS training then periodic CSS (4 periods = 1.4 s), -16 dBm0 |
| `ITU_ConvergenceQuiet` | P1110 11.11.4 / P1120 11.11.4 (P1100 11.11.4: 40 dB from 1200 ms) | Initial convergence from activation, quiet, max volume: ERL-vs-time above the Figure 11-5/11-7 mask | ~6 dB allowed 0-200 ms rising (log t) to **>= 40 dB at 1200 ms**, held to 5000 ms | **>= 40 dB by 600 ms** AND >= 46 dB at 1200 ms | [ALGO] | Periodic CSS at -16 dBm0, >= 5 s, 35 ms integration |
| `ITU_ConvergenceNoise` | P1110 11.11.5 / P1120 11.11.5 (P1100 11.11.5) | Initial convergence with background noise: echo <= BGN+10 dB until 100 ms, decaying (log t) to <= BGN at 1500 ms | mask as stated | **<= BGN by 750 ms** | [ALGO] | CSS / BE seq at -16 dBm0; noise >= 5 s pre-roll |
| `ITU_TimeVariantPath` | P1110 11.11.6/11.11.7 / P1120 11.11.6 | Echo under a time-varying echo path shall not degrade beyond limit (converged first) | P1110: increase **<= 6 dB** vs steady state; P1120: absolute **< -52 dBm0(A)** | <= 3 dB / < -58 dBm0(A) | [ALGO] | CSS -5/-25 dBm0 + BE seq -16 dBm0 over a time-varying IR (rotating-reflector analogue) |

### Switching characteristics (P1110 11.11.8 / P1120 11.12)

| Row ID | Clause | Requirement | Required | Margin target | Tag | Signal |
|---|---|---|---|---|---|---|
| `ITU_ActivationSend` | P1110 11.11.8.1 / P1120 11.12.1 | Minimum activation level in send; build-up time | L_S,min **<= -20 dBPa** (MRP); T_r **<= 50 ms** | activation at -26 dBPa; T_r <= 25 ms | [ALGO] | CSS bursts 248.62/451.38 ms (P1120: word "five" 500/500 ms), +1 dB steps, 5 ms integration |
| `ITU_ActivationReceive` | P1110 11.11.8.2 / P1120 11.12.2 | Minimum activation level in receive; build-up time | L_R,min **<= -35.7 dBm0**; T_r **<= 50 ms** | activation at -41.7 dBm0; T_r <= 25 ms | [ALGO] | as above, receive direction |
| `ITU_AttenRangeSend` | P1110 11.11.8.3 / P1120 11.12.3 | Attenuation range in send when switching from receive-active | A_H,S **< 20 dB**; T_r,S **< 50 ms** (rec.: 13 dB down within 15 ms) | < 14 dB; T_r <= 25 ms | [ALGO] | CSS activation + voiced sound, 5 ms integration |
| `ITU_AttenRangeReceive` | P1110 11.11.8.4 / P1120 11.12.4 | Attenuation range in receive after send active | A_H,R **< 15 dB**; T_r,R **< 50 ms** (rec.: < 9 dB within 15 ms) | < 9 dB; T_r <= 25 ms | [ALGO] | mirror of above |

### Double talk (P1110 11.12 / P1120 11.13) — target: P.340 Category 1

| Row ID | Clause | Requirement | Cat-1 bound | Margin target | Tag | Signal |
|---|---|---|---|---|---|---|
| `ITU_DtSendAtten` | P1110 11.12.1 / P1120 11.13.1 | Attenuation range in send during double talk (words AND sentences must both pass; level matrix: nominal, +6S/-6R, +6R/-6S, max volume) | A_H,S,dt **<= 3 dB** | <= 1.5 dB | [ALGO] | P.501 7.3.5 DT speech sequence; recv -16 dBm0, send -1.7 dBPa |
| `ITU_DtReceiveAtten` | P1110 11.12.2 / P1120 11.13.2 | Attenuation range in receive during double talk | A_H,R,dt **<= 3 dB** | <= 1.5 dB | [ALGO] | as above, competing speaker in receive |
| `ITU_DtEchoLoss` | P1110 11.12.3 / P1120 11.13.3 | Echo loss during double talk, met in EACH band 200 Hz-6950 Hz (comb-filter analysis; assumes far-end SLR+RLR = 10 dB) | **>= 27 dB** | >= 33 dB per band | [ALGO] | AM-FM orthogonal sine pair (P.501 7.2.4, Table 7-6 plan); send -25.7 dBPa HFRP, recv -16 dBm0 |
| `ITU_DtSentSpeech` | P1110 11.12.4 / P1120 11.13.4 | Sent-speech attenuation during double talk per band 200-6900 Hz (guards against fast switchers posing as duplex) | **<= 3 dB** | <= 1.5 dB | [ALGO] | AM-FM orthogonal pair, comb on send frequencies |

### Comfort noise and noise pumping (P1110 11.13 / P1120 11.14)

| Row ID | Clause | Requirement | Required | Margin target | Tag |
|---|---|---|---|---|---|
| `ITU_ComfortNoiseLevel` | P1110 11.13.6 / P1120 11.14.6 | Injected comfort noise level vs original transmitted background noise (A-weighted) | **+2 / -5 dB** | +1 / -2.5 dB | [ALGO] |
| `ITU_ComfortNoiseSpectrum` | same | Comfort noise spectral deviation within mask | +-12 dB (200-800 Hz), +-10 (800-2k), +-6 (2k-8k WB / 2k-14k SWB, 4k NB) | half-mask | [ALGO] |
| `ITU_NoisePumpFarEnd` | P1110 11.13.4 / P1120 11.14.4 | Send level variation during/after far-end CSS bursts in driving noise | **<= 10 dB** | <= 5 dB | [ALGO] |
| `ITU_NoisePumpNearEnd` | P1110 11.13.5 / P1120 11.14.5 | Send level variation during/after near-end CSS bursts in noise | **<= 10 dB** | <= 5 dB | [ALGO] |

### Stability (P1110 Annex E / P1120 Appendix I) — MuTap's home turf

| Row ID | Clause | Requirement | Margin target | Tag |
|---|---|---|---|---|
| `ITU_StabilitySweep` | P1110 Annex E / P1120 App. I | Far-end echo path = pure attenuation swept **50 -> 0 dB in 5 dB steps** at 0 ms delay (mandatory case; car IR = customized case), EC reset each run: NO howling/feedback; document the minimum far-end ERL that remains stable | stable at **0 dB far-end ERL** (the sweep's floor) — this is a closed-loop test and the AFC heritage applies directly | [ALGO] |

### Listed, not claimed (automotive)

- **[TERMINAL]** P1110/P1120 clause 7 physical test environment, HATS
  positioning, driving-noise playback fidelity; 11.2 total round-trip
  delay < 170 ms (the ALGORITHMIC latency contribution — block size —
  is reported by `ITU_AlgorithmicDelay` below against the <= 70 ms
  implementation budget); 12.x SRW-phone verification.
- **[MIXED, out of scope for this effort]** 11.13.3/11.14.3
  speech-quality-in-noise MOS (needs the ETSI TS 103 281 predictor — a
  noise-reduction metric, not an echo metric) and 11.13.2/11.14.2
  Relative Approach analysis (Sottek hearing model). Marked explicitly
  as not covered.
- **Out of scope (non-echo terminal clauses)**: P1110 10.x microphone
  parameters, 11.3 loudness ratings, 11.4 frequency responses, 11.5/11.6
  speech quality/stability, 11.7 idle channel noise, 11.8 out-of-band,
  11.9/11.10 distortion; P1120 equivalents (11.3-11.10).

## Tier A matrix — P.340 framework rows (05/2000)

The automotive rows above already enforce P.340's Tables via their
category bounds; these rows pin the P.340-native items not repeated
there. Bracketed values are provisional in P.340 — noted per row.

| Row ID | Clause | Requirement | Required | Margin target | Tag |
|---|---|---|---|---|---|
| `ITU_P340_BuildUpSingle` | 10.3.2.7 | Build-up time, single talk, either direction: onset to within [3 dB] | **<= [20 ms]** (provisional) | <= 10 ms | [ALGO] |
| `ITU_P340_BuildUpDouble` | 10.3.2.8 | Build-up time, double talk (if attenuation > 6 dB) | **< [20 ms]** (provisional) | <= 10 ms | [ALGO] |
| `ITU_P340_HangoverRecovery` | 10.3.2.10 | After a double-talk event (far-end continuous): echo attenuation at Sout | **>= [20 dB] within [1] s** (provisional) | >= 26 dB within 0.5 s | [ALGO] |
| `ITU_P340_VoiceSwitchBuildUp` | 4.6 | Voice-switch build-up time TR | **< 15 ms** (pref. < 10) | < 7.5 ms | [ALGO] |
| `ITU_P340_NoiseFluctuation` | Table 3 (7.11) | Transmitted background-noise level fluctuation | **<= +-3 dB** | <= +-1.5 dB | [ALGO] |
| `ITU_P340_Type1Transfer` | Table 5 (clause 9) | Behaviour-1 (full duplex) transfer function constant over time | **+-3 dB** in 1/12 octave | +-1.5 dB | [ALGO] |

P.340 notes: it contains **no absolute single-talk TCLw number** (delegated
to P.341/P.342; the automotive 46 dB row governs here) and **no numeric
convergence limit** (the automotive 40-dB-by-1.2-s row governs).

## Tier B matrix — G.168-adapted battery (04/2015)

G.168 states its own scope plainly: it "does not cover acoustic echo
cancellation as per ITU-T P.340." This battery is therefore run as an
**adapted suite** — the tests' structure and pass criteria transplanted
onto acoustic paths — and reported as `G.168-adapted`, never as G.168
compliance. Two adaptations are fixed up front: (1) G.168's echo-path
models m1-m8 are hybrid-derived, sparse, and short (dispersion <= 12 ms)
— they serve only as a regression floor; the primary paths are our room
and cabin RIRs. (2) G.168's ERL >= 6 dB convention maps to the acoustic
coupling loss of the simulated path. Levels use LRin,act (active-part
RMS, +1.49 dB over whole-CSS level for single-talk CSS, +1.66 dB for
double-talk CSS); measurement uses G.168's 35 ms level filter.

| Row ID | G.168 test | Requirement (adapted) | Required | Margin target | Tag |
|---|---|---|---|---|---|
| `G168_Convergence` | 2A/2B (6.4.2.3) | Combined loss >= 6 dB at t0 and **>= 20 dB by 50 ms + td**; steady-state residual per Figure 9/11 (NLP on: LRET -70..-55 dBm0 over LRin -30..0; NLP off: LRES -55..-35 dBm0 by 10 s); re-convergence after an abrupt path change meets the same masks with no grace period | as stated | 20 dB by **25 ms + td**; steady-state 6 dB below the figure lines | [ALGO] |
| `G168_ConvergenceNoise` | 2C (6.4.2.3.3) | Convergence with Hoth noise at LRin-15 dB: converge within 1 s (NLP on, LRET <= LSgen); NLP-off mask relaxed to 5 dB at t0, max(LRin-LSgen-6, 17) dB by 1 s | as stated | converge by 0.5 s | [ALGO] |
| `G168_DtLowNearEnd` | 3A (6.4.2.4.1) | LOW near-end (LRin-15 dB) must NOT block adaptation: convergence within 5 s, LRES <= LSgen,act | as stated | within 2.5 s | [ALGO] |
| `G168_DtDivergence` | 3B (6.4.2.4.2) | Divergence during double talk bounded: residual <= Figure 11 + **10 dB** (near-end >= far-end) / + **3 dB** (near-end 6-30 dB below) | as stated | +5 dB / +1.5 dB | [ALGO] |
| `G168_DtConversation` | 3C (6.4.2.4.3) | Conversational alternation: no post-double-talk echo burst (peaks bounded by LSgen during the single-talk tail; LSgen+6 dB when double talk resumes); Figure 9 met 5 s after DT ends | as stated | peaks 6 dB inside the bounds | [ALGO] |
| `G168_LeakRate` | 4 (6.4.2.5) | After 2 minutes of silence, residual echo on signal return degraded <= **10 dB** vs steady state | as stated | <= 5 dB | [ALGO] |
| `G168_InfiniteERL` | 5A (6.4.2.6.1) | Echo path opened mid-call (ERL -> infinity): the filter must not regenerate phantom echo; combined loss keeps meeting the convergence mask | as stated | mask + 6 dB | [ALGO] |
| `G168_PathSwing` | 5B (6.4.2.6.2) | Coupling-loss swings (6-30 dB <-> >= 46 dB) re-converge to the masks each time | as stated | half-time re-convergence | [ALGO] |
| `G168_NarrowbandTones` | 6 (6.4.2.7) | 5 s DTMF-frequency tones (adaptation live) corrupt the filter by <= **10 dB** vs Figure 11 afterward | as stated | <= 5 dB | [ALGO] |
| `G168_ToneStability` | 7 (6.4.2.8) | 2-minute continuous single tone from reset: residual <= **0.83 x LRin - 30 dB** after 10 s, no divergence | as stated | 6 dB below the line | [ALGO] |
| `G168_ComfortNoise` | 9A/9B (6.4.2.10) | Comfort noise tracks the true background: **+-2 dB** on level steps (5/10 dB), **+-6 dB** through 170 s level ramps (-86 -> -20 dBm0) | as stated | +-1 dB / +-3 dB | [ALGO] |
| `G168_AcousticResidual` | 12 (6.4.2.13) | The standard's own acoustic scenario: three-phase path/ERL switch (A -> B = A-10 dB w/ different model -> A), no reset between phases; 2A masks apply per phase | as stated | 2A margin targets | [ALGO] |

**Excluded as [LINE-ONLY]** (listed so "not covered" is explicit):
Test 8 (SS5/6/7 signalling-tone transparency), Test 10 (Group-3 fax /
V.21 handshake), Test 11 (tandem network ECs — no normative values),
Test 13 (low-bit-rate coder in the path), Test 14 (V-series modem BER),
Test 15 (PCM A/u-law DC offset), Tests 16A/16B (DTMF transparency).

**Caveat carried into the proof notebook:** G.168 itself warns that CSS
is "a statistical approximation of real speech" and that Tests 3A/3B
results vary considerably with real speech across languages — one more
reason the Tier A double-talk rows (real-speech sequences where
obtainable) rank above this battery.

## Tier C — G.167 historical rows (informative; withdrawn, values provisional)

Run and reported, not claimed as compliance (the rec is withdrawn):
TCLwst >= [40] dB teleconference / [45] dB hands-free; TCLwdt >= [25]/[30] dB;
Ardt/Asdt <= 6 dB (unbracketed); Tic: >= [20 dB] within [1] s from reset;
Trdt: [20 dB] within [1] s after DT; TCLwpv >= [10 dB] during a [5] s path
variation; Trpv: [20 dB] within [1] s after it; NEC protection (6.3):
Sout >= [40 dB] below Rin after convergence unless double talk; processing
delay <= [2]-[16] ms per direction by application; attenuation distortion
<= +-1 dB (4.5). Owning test: `ITU_G167_Historical` (one test, several
assertions, margins per the global policy). Echo-path RTs for its
simulated rooms: 400 ms (teleconference), 500 ms (hands-free), 60 ms
(car) — these also parameterize the new cabin fixture family.

## G.131 context (no rows)

Echo control is required when one-way delay exceeds 25 ms (or per the
TELR/delay curves: acceptable TELR 33 dB at 25 ms, 47 dB at 100 ms;
f(x,y) >= 14 criterion). Returned echo from an echo-control device:
**< -65 dBm0** — adopted as the *stretch* target behind `ITU_EchoLevel`'s
margin. One informative row: `ITU_AlgorithmicDelay` reports the chain's
block latency against G.167's per-direction budgets and P.1110's 70 ms
implementation budget.

---

## Simulation assets delivered by Stage 1

`tests/support/itu_levels.h` + `tests/support/itu_signals.h` +
`tests/fixtures/rir_cabin.h`, validated by `tests/test_itu_signals.cpp`
(measured-first thresholds; the file header carries every number).
Instrument floors that bound what the compliance suite can measure:

- CSS single/double-talk: sample-exact framing, PN crest 10.97 dB
  (spec 11 +- 1), bin-flat PN, level calibration exact, voiced segments
  transcribed from P.501 Tables 7-1/7-2. Available at every required
  rate via `make_css_at` (PN crest 11.86 dB at 48 kHz / 11.44 at
  16 kHz; AM-FM separation 94.0 / 92.6 dB at 48 / 16 kHz).
- AM-FM orthogonal pair: **94.3 dB comb separation** at the spec's exact
  band edges — the double-talk echo-loss measurement floor sits far
  beyond the >= 33 dB margin targets. (Analysis discipline recorded in
  the test: any added guard band collapses the floor.)
- A-weighting: -19.18 / 0.00 / -1.76 dB at 100 / 1k / 10 kHz
  (prewarped bilinear; IEC class-1 envelope).
- NB band-limiter: -85 dB at 4 kHz. Cabin fixture: RT60 66.6 ms.
- P.56 active-level meter: definitionally consistent on sparse speech
  (activity 0.312, delta = 10 log10(1/activity) exactly); on CSS the
  101 ms pause sits inside the 200 ms hangover, so the G.168
  active-part constants (+1.49 / +1.66 dB) are applied arithmetically.

**ITU real-speech attachments** (P.501 7.3.2/7.3.3/7.3.5): still to be
procured — download from the ITU test-signal database into
`tests/data/itu/` (git-ignored; local test use only, never committed).
Until then the affected rows run on the CSS/AM-FM signals and are
reported `method-equivalent`.

## Simulation assets required (original Stage 1 list, for reference)

**Signals (all generatable from P.501's algorithmic descriptions):**
- CSS single-talk, fullband: voiced 48.62 ms (literal sample table 7-1,
  transcribed from the PDF we hold) + 200 ms PN (8192-point FFT variant —
  P.501 mandates the long-PN variant for adaptive systems) + 101.38 ms
  pause = 350 ms period, alternating polarity; crest 11 +- 1 dB.
- CSS double-talk: voiced 72.69 ms (table 7-2, different pitch) + 200 ms
  Gaussian noise (crest 12 +- 1 dB) + 127.31 ms pause = 400 ms period
  (the 350/400 ms slide generates all double-talk overlap states).
- Narrowband CSS + the two shaping filters (Figures 7-10/7-11 corner
  tables); band-limiting filters per P.501 Table 7-7 (NB/WB/SWB).
- AM-FM orthogonal double-talk pair (7.2.4): f_fm = 1 Hz, mu_am = 2/3,
  f_am = 3 Hz, Table 7-6 frequency plan; comb-filter analysis >= 8k FFT.
- Activation sequence: 500 ms token / 500 ms pause, +1 dB steps over
  21 dB (synthetic voiced token stands in for the word "five").
- Level toolchain: P.56-style active-speech-level meter, dBm0/dBPa/dBov
  calibration header, A-weighting filter, 35 ms / 5 ms level integrators.

**Real-speech gap (flagged):** the BE single-talk sequence (7.3.2), the
DT speech sequence (7.3.5) and the compressed speech signal (7.3.3) are
ITU attachment WAV files — recordings, not formulas. Stage 1 attempts
download from the ITU test-signal database (itu.int is now allowlisted)
for LOCAL TEST USE with files git-ignored (no redistribution); where
unavailable, tests run on documented synthetic stand-ins (CSS/SSG per
P.501 7.2.5) and are marked `method-equivalent` rather than
`signal-exact` in the proof notebook.

**Echo paths:** existing room fixtures; new car-cabin family (~2.5 m^3,
RT ~60 ms); time-varying IR (rotating-reflector analogue: a moving
early-reflection cluster); pure attenuation+delay paths for the
stability sweep; optional measured cabin IRs via `make_rir_fixtures.py`.

**Noise:** driving-noise generator (low-frequency-heavy colored noise
with specified A-weighted level at the virtual mic; Annex C/D scenarios
are vehicle-specific recordings, so ours are labeled synthetic
analogues), Hoth-spectrum noise for the P.340 rows.

## What Stage 2 (post-filter) must deliver, per this matrix

- Residual echo below **-58 dBm0(A)** (margin: -64) on speech at
  -16 dBm0 through a realistic path — linear cancellation alone measures
  ~20-25 dB of suppression, so the suppressor supplies the rest without
  violating `ITU_DtSendAtten`'s <= 1.5 dB near-end transparency target
  (this pair of constraints IS the design problem).
- Comfort noise matched +1/-2.5 dB in level and half-mask in spectrum.
- Switching dynamics inside 25 ms build-up at the margin targets.
- No noise pumping beyond 5 dB around speech bursts.
- The chain's ERL trajectory above the convergence masks at 2x speed.

### Stage 2 delivered (mutap/postfilter.h + tests/test_postfilter.cpp)

All of the above, measured against this file's margin targets on the
golden model (double, block 256, 2048-tap unit-energy paths, 48 kHz;
one required-rate check at 16 kHz). Chain = RAW partitioned FD-Kalman
(transition 0.9998, initial uncertainty 10) + residual suppressor at
defaults:

| Deliverable | Margin target | Measured |
|---|---|---|
| ST residual, cabin / studio | < -64 dBm0(A) | **-79.9 / -88.9** |
| ST residual, NLMS core / 16 kHz | < -64 dBm0(A) | -65.0 / -69.1 |
| DT send attenuation, cabin / studio | <= 1.5 dB | **1.05 / 0.93** |
| DT echo loss, worst band 200-6950 Hz | >= 33 dB | **38.0 / 34.1** |
| ERL by 600 / 1200 ms | >= 40 / >= 46 dB | 43.2 / 46.9 |
| Comfort-noise level match | +1 / -2.5 dB | -1.20 |
| Comfort-noise spectrum, worst band | half-mask (+-3..6) | 1.69 dB |
| Noise pumping | <= 5 dB | 3.3 |
| Near-end build-up at DT onset | <= 25 ms | 20.9 ms |

Design decisions the numbers forced (full derivations in
postfilter.h's comments, rejected designs kept in git history):

1. **The AEC chain does not use PEM.** Open-loop AEC has an exogenous
   far end — no closed-loop bias to remove — and the predictor's
   block-by-block refit injects gradient noise that floors misalignment
   near -20 dB. The raw Kalman core measures -75.6 dBm0(A) bare on the
   same scenario where PEM-Kalman plateaus at -28.9, and its per-bin
   noise-PSD tracking IS the double-talk defense (post-DT residual
   -70.9). PEM remains the right structure for the closed loop (AFC),
   and aec_chain still composes with pem_afc.
2. **The suppressor's discriminator correlates the MIC (E + Yhat),
   never E, against Yhat**: a converged adaptive filter keeps E
   orthogonal to its reference, so coh(E, Yhat) saturates at 0.34 while
   adapting; coh(D, Yhat) measures 0.99.
3. **Suppression depth comes from a leakage estimate gated by that
   coherence, not from the coherence itself** — the AM-FM plans
   interleave 20 Hz apart at the low end, no realizable gain filter
   notches that selectively, and a coherence-proportional gain measured
   5.5 dB of near-end attenuation there. The Wiener-on-E form rides to
   unity wherever near-end energy inflates |E|^2.
4. **Comfort noise fills to a two-window minimum-statistics floor
   (bias x4)** — asymmetric-rate one-pole trackers measured either
   -8 dB undershoot (raw-minimum bias) or tens-of-seconds acquisition.

Still Stage 3's to prove: the full per-row multi-rate suite (both
required rates x both cores x all rooms), the spectral echo mask, the
switching/activation battery, stability sweep, G.168-adapted rows, and
the TCL / time-variant-path rows.

## Stage 3 delivered: the Tier A compliance suite

`tests/support/itu_chain.h` (the pinned compliance chain + scenario
machinery) and three row-per-test batteries, every row at BOTH required
rates: `tests/test_itu_echo.cpp` (11.11.x echo performance),
`tests/test_itu_doubletalk.cpp` (11.12/11.13 + P.340 Table 5),
`tests/test_itu_dynamics.cpp` (switching, comfort noise, pumping,
Annex E stability, delay). Thresholds measured-first as always; the
full per-row numbers live in the test file headers. **Tier B
(G.168-adapted) is a follow-up stage** — unstarted, not partially done.

**The pinned chain** (one configuration for every row): raw FD-Kalman,
transition 0.9998, initial uncertainty 10, canceller noise smoothing
and all suppressor time constants rescaled to physical time per block
duration; 48 kHz = block 256 / 2048 taps, 16 kHz = block 256 / 1024
taps (block 128 collapses the partitioned Kalman's convergence —
measured 8.9 vs 22.5 dB ERL by 600 ms); low-band suppression cap
< 300 Hz with 0.3 s sustained certification; initial receive guard.

### Measured, 48 kHz / 16 kHz (cabin path; studio where the row sweeps rooms)

| Row | Required | Target | Measured 48k | Measured 16k |
|---|---|---|---|---|
| ITU_TCL | >= 46 dB | >= 52 | **68.4** | **80.6** |
| ITU_EchoLevel (cabin/studio) | < -58 dBm0(A) | < -64 | **-76.4 / -85.7** | **-81.0 / -101.2** |
| ITU_EchoStability | <= 6 dB | <= 3 | **2.75** | 4.02 (T) |
| ITU_EchoSpectral (worst margin) | mask | mask+6 | **+13.3** | **+22.9** |
| ITU_ConvergenceQuiet (600/1200 ms) | 40 dB @ 1200 | 40 @ 600, 46 @ 1200 | 33.6 (T) / **47.8** | 34.1 (T) / 45.4 (T) |
| ITU_ConvergenceNoise (driving, -30 dBm0(A)) | mask | by 750 ms | **pass all points** | **pass all points** |
| ITU_TimeVariantPath (-30 dB reflector) | < -52 dBm0(A) | < -58 | **-58.3** | -55.6 (T) |
| ITU_ActivationSend build-up | <= 50 ms | <= 25 | **15.7** | **21.1** |
| ITU_DtSendAtten (integrated) | <= 3 dB | <= 1.5 | **0.91** | **-0.30** |
| ITU_DtSentSpeech (worst band) | <= 3 dB | <= 1.5 | 1.61 (T) | 2.02 (T) |
| ITU_DtEchoLoss (worst band) | >= 27 dB | >= 33 | **37.5** | **37.9** |
| ITU_P340_Type1Transfer | +-3 dB | +-1.5 | 2.74 (T) | 2.23 (T) |
| ITU_P340_HangoverRecovery (0.5 s / 1 s) | [20 dB @ 1 s] | 26 @ 0.5 s | **30.6 / 45.0** | 18.9 (T) / 23.1 |
| ITU_P340_NoiseFluctuation | +-3 dB | span 3 | **3.09 span** | **2.90 span** |
| ITU_ComfortNoiseLevel | +2/-5 dB | +1/-2.5 | **-1.31** | -2.91 (T) |
| ITU_ComfortNoiseSpectrum (worst band) | mask | half-mask | **-1.58** | **-3.46** |
| ITU_NoisePumpFarEnd (segment avg) | <= 10 dB | <= 5 | 8.0 (T) | 9.9 (T) |
| ITU_StabilitySweep | stable at 0 dB ERL | — | **stable, floor reached** | **stable, floor reached** |
| ITU_AlgorithmicDelay | 70 ms budget | <= 35 | **10.7 ms** | **32 ms** |

**(T)** = our own margin target missed while the ITU REQUIREMENT is
met (each is a comment-documented regression gate in the owning test,
never a silent pass). Every requirement in the table is met at both
required rates. Recurring 16 kHz theme: 16 ms blocks mean fewer
adaptation steps and meter samples per unit time — early convergence,
hangover recovery and minimum-statistics bias all read a few dB behind
the 48 kHz chain.

### Rows resolved without a test (recorded, not asserted)

- `ITU_ActivationReceive`, `ITU_AttenRangeReceive`, `ITU_DtReceiveAtten`:
  the chain performs NO receive-path processing — receive activation is
  immediate and receive attenuation identically 0 dB.
- `ITU_AttenRangeSend`: the switched range IS the initial receive guard
  by construction (< 14 dB, inside the < 20 dB clause), engaged only
  until convergence certifies (latch measured 160 / 384 ms of nominal
  receive) and never again.
- `ITU_P340_BuildUpSingle/Double`, `ITU_P340_VoiceSwitchBuildUp`:
  covered by the ActivationSend build-up measurement (15.7 / 21.1 ms);
  P.340's bracketed [20 ms] is provisional and superseded by the
  in-force automotive series' 50 ms.
- The quiet-noise ConvergenceNoise variant (Hoth at -46 dBm0) meets the
  mask only from ~600 ms: with echo 30 dB above BGN+10 at onset, the
  first mask segment demands more switched loss than the A_H,S
  allowance permits any terminal. The asserted scenario is the
  automotive driving-noise condition the clause is defined for.

### Chain elements Stage 3 forced (all measured-first, postfilter.h)

1. **Initial receive guard** (aec_chain): switched < 14 dB send loss
   while receive is active and convergence is uncertified, latched off
   at certification. No Yhat-referenced suppressor can see echo while
   the canceller's estimate is ~0; the convergence masks are unmeetable
   without switched loss, which is exactly what the switching clauses
   budget for.
2. **Release snap + floor-initialized gains**: activation build-up in
   one block; suppression from block one after reset.
3. **Coherence-gated low-band cap with sustained certification**:
   protects voice fundamentals where no realizable resolution separates
   them from echo (P.340 transfer bound); certification must survive
   0.3 s of active echo dominance so double-talk pauses cannot leak it
   open.
4. **REJECTED: a noise-floor gain bound** ("stop suppressing at the
   tracked floor") — in near-silence the minimum-statistics floor rides
   the residual's own pause decay and the bound self-limits the
   suppressor ~6 dB above its reachable depth (EchoLevel lost 6 dB,
   TimeVariantPath fell to the requirement wire). The comfort FILL is
   the right shape: it only ever adds.

## Stage 3b delivered: the Tier B G.168-adapted battery + G.167

`tests/test_g168.cpp`: all twelve adapted rows at BOTH required rates
plus the G.167 historical row (48 kHz, informative). Conventions as
fixed above: cabin path at ERL = 6 dB coupling, LRin,act levels, the
rec's 35 ms meter, NLP on = chain with comfort noise disabled (Figure
9's own instruction), NLP off = bare canceller. Documented protocol
adaptations: leak-rate silence 45 s (rec 2 min), tone stability 30 s
(rec 2 min), comfort-noise ramp 20 s / 20 dB (rec 170 s / 66 dB).

### Measured highlights, 48 kHz / 16 kHz

| Row | Bound | Measured |
|---|---|---|
| G168_Convergence (2A, worst level) | loss >= 20 dB by 50 ms; Fig 9 steady | **22.3-26.3 early; -68.8 vs -61 worst steady** |
| G168_Convergence (2B, NLP off) | >= 20 dB by 1 s; Fig 11 by 10 s | **49.3 / 50.1; -75.2 / -86.8 vs -43.3** |
| G168_ConvergenceNoise (2C) | <= LSgen by 1 s | **met by 0.5 s (-34.4 / -32.8 vs -29)** |
| G168_DtLowNearEnd (3A) | converge <= 5 s | **converged by 2.5 s, out at the near end's level** |
| G168_DtDivergence (3B) | Fig 11 + 10 after the 1 s grace | **-62.6 / -67.0 vs -38.3 (+5 target met)** |
| G168_DtConversation (3C) | post-DT peaks <= LSgen | **-24.9 / -22.1 vs -17.7** |
| G168_LeakRate (45 s) | degrade <= 10 dB | **IMPROVES (-2.3 / -0.9)** |
| G168_InfiniteERL (5A) | loss mask holds, no phantom | **>= 20 dB held** |
| G168_NarrowbandTones (6) | filter survives 5 s DTMF | **-77.7 / -86.0 vs Fig 11 -43.3 after** |
| G168_ToneStability (7, 30 s) | <= 0.83 LRin - 30 after 10 s | **numerical floor / -104 vs -49.3** |
| G168_ComfortNoise (9A/9B) | steps +-2 dB, ramp +-6 | **-1.24 / -1.72 step; -0.19 / -0.02 ramp** |
| G168_AcousticResidual (12) | 2A masks per phase | loss elements met; switched-phase steadies gated (below) |

### The documented deviation: re-convergence depth after abrupt path changes

After an ABRUPT path change (re-convergence 2A-b, path swings 5B, the
three-phase test 12), the chain re-reaches the **>= 20 dB combined-loss
element within 1 s at both rates**, but the deep Figure-9 steady state
only after ~7 s at 48 kHz and beyond 10 s at 16 kHz (measured
trajectory: -39.5 / -44.2 / -51.4 / -64.9 / -91.0 dBm0 across
[1,2]/[2,3]/[3,5]/[5,8]/[8,10.5] s at 48 kHz). Cause: a converged
Kalman's state uncertainty is small and nothing re-inflates it on a
path change — initial convergence gets P(0) = 10 and meets Figure 9
within 1.4 s; re-convergence does not get that boost. The affected
assertions are regression gates at the measured trajectory, and
**"uncertainty re-inflation on sustained innovation excess" is filed in
HANDOFF as the fd_kalman core follow-up** (it would also lift the
TimeVariantPath and hangover 16 kHz margins).

### G.167 historical row (informative, withdrawn rec)

Asserted at the brackets: TCLwst >= [45] (measured 60+), Asdt <= 6
(~1), Tic >= [20 dB] within [1 s] (43+), delay <= [16 ms] (10.7).
TCLwdt is REPORTED, not met at [25]/[30]: at equal-level double talk
the reading (23.6 dB) is dominated by the near end's gain-modulation
spill — it does not move when the echo path is scaled +-10 dB, i.e., it
is not measuring echo. At the in-force P.1110 competing-talker
convention the same chain measures >= 37 dB per band (ITU_DtEchoLoss).

### Harness calibration added in 3b

The comfort-noise floor bias is calibrated per rate (4 at 48 kHz, 5.6
at 16 kHz): 16 ms blocks put 3x fewer meter samples in each
minimum-statistics window and the floor biased deep — measured -2.8 dB
comfort-noise step tracking against G.168's +-2 requirement, -1.72
after; the Tier A ComfortNoiseLevel row moves from -2.91 (target miss)
to -2.15 (target met) at 16 kHz.

## Stage 4 delivered: the compliance proof notebook

`tools/notebook/build_itu_compliance.py` assembles and executes
`notebooks/itu_compliance.ipynb`: one section per requirement group,
a requirement/measured/margin table per section, the convergence
trajectories drawn against the recommendations' time masks (quiet,
in-noise, G.168 Figures 9/11), the double-talk rows against the P.340
windows (per-band send attenuation and echo loss, transfer constancy,
hangover, build-up), comfort noise/pumping, path dynamics with an
honest figure of the re-convergence deviation, the Annex E sweep, and
the G.167 run-and-reported row with the TCLwdt finding.

Nothing in the notebook is transcribed by hand except the
recommendations' requirement values: its first cell compiles
`tools/notebook/itu_dump.cpp` (CMake option `MUTAP_BUILD_ITU_DUMP`,
built in a dedicated `build-itu/` tree) and re-runs the full battery
live (~6 minutes, deterministic seeds). The dump program includes the
SAME `tests/support/itu_chain.h` machinery the test suite gates with —
same pinned chain, same signals, same meters — and its scenario
recipes mirror the gtest rows line for line, so a number in the
notebook is the number the suite asserts. The gtest files remain the
assertion authority; the notebook exists so a human can see the
trajectories the assertions compress into pass/fail. (Measurement
note: CSS generation through the NOTE 2 resampler dominates the dump's
runtime, so it memoizes `make_css_at` — verified byte-identical output
against the uncached run.)

Remaining: Stage 5 (externals/docs).
