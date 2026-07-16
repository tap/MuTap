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
