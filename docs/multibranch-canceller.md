# The multi-branch canceller: nonlinear-basis echo cancellation

Planning and architecture record for the first follow-up of the outdoor
close-range effort (HANDOFF.md Rev 6). Status: DESIGN — nothing in this
document is landed; every hypothesis below is written to be replaced by
a measured number, per house workflow.

## 1. Why (the measured motivation)

The Rev 6 Stage 0 baseline (`tests/test_outdoor.cpp`, full table in its
banner) measured the certified chain against a distorting loudspeaker
one inch from the mic. The headline rows, at ERL −20 dB, 48 kHz
(raw `partitioned_fdkf` suppression / full-chain suppression / send
residual dBm0(A)):

| drive                | raw    | chain  | residual |
|----------------------|--------|--------|----------|
| linear               | 50.2   | 58.8   | −46.5    |
| mild (~1 % THD)      | 25.6   | 29.8   | −18.2    |
| moderate (~4.4 %)    | 15.6   | 19.3   | −10.3    |
| severe (~13 %)       | 10.2   | 20.6   | −11.4    |

One percent of THD costs the linear canceller ~25 dB, and the coherence
suppressor recovers only 2..4 dB on top — its evidence (correlation of
the mic against the echo estimate) lives at the reference's own
frequencies, while the residual's energy sits at harmonics the linear
echo estimate cannot produce. Under permanent double talk the same
operating point leaves the send path 20 dB ABOVE the near-end talker.
The distortion is deterministic — a function of the reference we hold
in our hands — so the model, not the suppressor, is the right place to
take it out. That is this effort.

Scope boundary, stated up front: this canceller addresses the
LOUDSPEAKER-side (pre-path, Hammerstein) nonlinearity only. Mic-side
overload (post-path: clipping after the near end has been added) is
structurally invisible to any reference-driven model — the baseline's
MicClipping row (~32 dB lost at 52 % clipped samples) is the clip
guard's problem, follow-up (c) of Rev 6, not this one.

## 2. Background and model choice

The physical chain is: reference x → amp/driver nonlinearity → one
short acoustic path F → mic. That is a **Hammerstein system** (static
nonlinearity, then linear filter). The standard model families, and
where we land:

- **Full Volterra filters** (Guérin et al. 2003; Zeller & Kellermann):
  general nonlinearity WITH memory, cost and misadjustment grow with
  (memory × order)^2 in the quadratic kernel alone. Rejected: the
  driver distortion that matters here is well-modeled as memoryless
  (the Stage 0 fixture's tanh model is, and measured device curves are
  close at these block sizes), and the outdoor path is 7 ms — we would
  be buying generality the scenario does not need at a price the M55
  target cannot pay.
- **Adaptive memoryless preprocessor + single linear filter** (Stenger
  & Kellermann 2000): adapt polynomial coefficients c_b feeding one
  shared F. Fewest degrees of freedom, but the estimation is bilinear
  (c and F multiply), needing alternating or gradient updates with
  their own stability story — a second adaptation loop with a second
  set of failure modes, outside our Kalman frame. Rejected for v1;
  revisit only if the MISO misadjustment measures poorly.
- **Power filters / MISO Hammerstein** (Küch & Kellermann 2006): run B
  fixed basis signals φ_b(x) through B parallel linear filters and sum.
  If the true nonlinearity is NL(x) = Σ c_b φ_b(x), the true system is
  exactly Σ_b (c_b F) ∗ φ_b(x): a MISO system whose b-th path is c_b F.
  Nothing bilinear — the branch filters absorb the basis weights, the
  estimation stays linear-in-parameters, and it drops straight into
  the partitioned frequency-domain machinery we already trust. **This
  is the structure we build**, in the diagonalized-Kalman frame — the
  same move Malik & Enzner 2012 made for NLMS-family nonlinear AEC,
  from the same Enzner state-space lineage as `partitioned_fdkf`.
- **End-to-end / learned**: measured in Stage 6 of Rev 5 (tools/ml):
  end-to-end NNs delete out-of-domain near ends. The learned engine
  stays a POST-filter (`nn_suppressor`), and the device-trained variant
  is Rev 6 follow-up (b), complementary to this one: the multi-branch
  canceller removes the deterministic distortion, the suppressor mops
  the model-mismatch remainder.

### Papers

- **S. Malik, G. Enzner**, "State-Space Frequency-Domain Adaptive
  Filtering for Nonlinear Acoustic Echo Cancellation," *IEEE TASLP*
  20(7):2065–2079, 2012. — The anchor: the state-space FD frame we
  already run, extended for Hammerstein AEC.
- **F. Küch, W. Kellermann**, "Orthogonalized power filters for
  nonlinear acoustic echo cancellation," *Signal Processing*
  86(6):1168–1181, 2006. — The MISO structure and the branch-
  decorrelation problem and remedy.
- **A. Stenger, W. Kellermann**, "Adaptation of a memoryless
  preprocessor for nonlinear acoustic echo cancelling," *Signal
  Processing* 80(9):1747–1760, 2000. — The preprocessor alternative
  (rejected above, recorded here).
- **A. Guérin, G. Faucon, R. Le Bouquin-Jeannès**, "Nonlinear acoustic
  echo cancellation based on Volterra filters," *IEEE TSAP* 11(6),
  2003. — The Volterra yardstick.
- **D. Comminiello, M. Scarpiniti, L. A. Azpicueta-Ruiz, J. Arenas-
  García, A. Uncini**, "Functional Link Adaptive Filters for Nonlinear
  Acoustic Echo Cancellation," *IEEE TASLP* 21(7), 2013. — Alternative
  basis families (trigonometric FLAF), if polynomials measure poorly.

## 3. The math: MISO extension of the diagonalized PBFDKF

Today's update (fd_kalman.h), per bin k and partition p, with shared
innovation denominator:

    denom(k)   = Σ_p |U_p(k)|² P_p(k) + Ψ_s(k) + ε
    W_p(k)    += (P_p(k) / denom(k)) · conj(U_p(k)) · E(k)
    P_p(k)    *= 1 − c |U_p(k)|² P_p(k) / denom(k)

The multi-branch form indexes the input spectra, weights and
uncertainties by (branch b, partition p) and keeps ONE joint
denominator and ONE error spectrum:

    Y_hat(k)   = Σ_b Σ_p U_{b,p}(k) W_{b,p}(k)
    denom(k)   = Σ_b Σ_p |U_{b,p}(k)|² P_{b,p}(k) + Ψ_s(k) + ε
    W_{b,p}   += (P_{b,p} / denom) · conj(U_{b,p}) · E
    P_{b,p}   *= 1 − c |U_{b,p}|² P_{b,p} / denom

where U_{b,p} is the spectrum of φ_b(x) delayed by p blocks. The joint
denominator is the point of doing this inside the Kalman rather than
running B independent filters on a shared error: gain is allocated
across branches in proportion to each branch's uncertainty-weighted
excitation — the credit-assignment problem between collinear branches
is at least *represented*, and a converged branch (small P) stops
competing. Everything else carries over verbatim: the gradient
constraint applies per (b, p) partition exactly as per p today; Ψ_s is
tracked from the shared residual; `reinflate_uncertainty()` lifts every
(b, p) plane; the transition/process-noise story is unchanged.

Structurally this is "more partitions, whose input ring belongs to a
different signal" — the implementation can flatten (b, p) into one
partition axis with a per-partition branch id, keeping today's loop
nest and the packed-spectrum layout.

**The branch prior.** `initial_uncertainty_decay` encodes "echo paths
decay along the filter" as a per-partition P(0) prior; the branch axis
needs the analogous prior: **echo is mostly linear**. Branch 0 (linear)
keeps P(0) = initial_uncertainty; nonlinear branches start at
P(0) · branch_prior, branch_prior ∈ (0, 1] (order 0.1, measured, not
guessed). This is not an optimization: on excitation that cannot
separate the branches (see §5), the diagonal Kalman splits the
minimum-norm solution proportional to P — the flat prior spreads
linear echo INTO the nonlinear branches, which is the block-128-notch
failure wearing a new axis. The decaying prior makes the split land
where the physics says.

## 4. Basis design (what scratch must decide)

Candidates, all memoryless and odd (the fixture's tanh is odd; even
components measured negligible for symmetric drivers — revisit if a
device says otherwise):

- **B1: odd powers** {x, x³} and {x, x³, x⁵}. Exact for polynomial
  nonlinearities; tanh's series converges fast in our amplitude range.
  Collinearity with x is severe (corr(x, x³) ≈ 0.77 for Gaussian x).
- **B2: moment-orthogonalized odd powers** (Küch & Kellermann):
  φ₃ = x³ − 3σ̂²x with σ̂² the running input power — E[x·φ₃] = 0 for
  Gaussian x, approximately decorrelated for speech. Costs a moment
  tracker; removes most of the collinearity BETWEEN branch inputs
  (the per-bin, per-partition collinearity remains — see §5).
- **B3: clipped-difference basis** {x, x − clip_c(x)}: the second
  branch is only the part of the signal beyond the knee — near-zero
  most of the time, active exactly where distortion is generated.
  Naturally decorrelated at low drive; matches hard-limiting devices
  better than polynomials (a hard clip has a slowly-converging odd
  series). Knee c is a per-device calibration.
- **B4: tanh-difference bank** {x, tanh(g₁x)/g₁ − x, ...}: on-model by
  construction for the fixture; risks overfitting the fixture — the
  scratch battery must include an off-model drive (hard clip) so B4
  cannot win by rigging.

Per-branch **normalization gains** are part of the basis spec: branch
signals at the −10 dBm0 operating level differ by orders of magnitude
in power (x³ at RMS 0.22 has RMS ≈ 0.05·x-scale³), and P(0) semantics,
the regularization floor and float32 headroom all assume O(1) signals.
Fixed gains calibrated at the operating level (like the drive presets:
measured, pinned in a fixture test), not adaptive.

Scratch measures, per basis, on the Stage 0 outdoor battery: raw
suppression at mild/moderate/severe (on-model tanh drive), the same
against a HARD-CLIP drive (off-model generalization — requires adding
a clip mode to `speaker_drive`, a fixture extension), convergence time
to the linear row's 1.2 s clock, misadjustment cost at drive 0 (the
price of carrying unused branches — B−1 extra branches of pure
misadjustment on clean linear echo), and the §5 safety rows.

## 5. Watch-list (the lessons, ranked)

1. **Branch collinearity is the block-128 notch's family.** The
   diagonal approximation ignores cross-branch coupling exactly as it
   ignores cross-partition coupling; collinear branch excitation
   creates a branch-redistribution null space invisible to the error.
   We have measured three members of this family (CSS-comb partition
   split, the SS7 tone weight walk, the vDSP empty-bin kernel) and the
   remedies that worked: priors that encode the physics (§3), novelty
   discounts, and freezing on degenerate excitation. Expect to need
   the prior from day one; hold the others in reserve. The float32
   walk specifically: branch collinearity plus the gradient constraint
   is the tone-row mechanism with MORE null space — the float32 parity
   stage must re-run the tone battery with branches ON.
2. **Ψ_s absorbing what the branches should learn.** During branch
   convergence the residual contains still-unmodeled distortion; the
   noise tracker booking it as near end collapses the very gains the
   branches need — the self-lock the re-convergence rescue treats,
   at cold start, on the branch axis. The branch prior slows branch
   convergence (small P(0)) and so widens this window: measure the
   race explicitly (suppression trajectory vs Ψ_s trajectory at
   moderate drive). If it locks, the options are ordered: raise
   branch_prior; branch-aware novelty; a one-shot branch reinflate
   wired to the chain's over-explanation trigger.
3. **Double talk must not erode.** The DT immunity argument is
   unchanged (shared Ψ_s, model-based gains), but B× more weights
   under loud near ends means B× more weight-motion surface — the
   TCLwdt (H − Ŵ)×X re-modulation term the G.168 battery documented
   applies per branch. Gate: the Stage 0 PermanentDoubleTalk linear
   row and the certified AM-FM/CSS DT rows, branches ON, must hold
   within margin.
4. **Tones.** A tone through x³ is a tone (plus harmonics): every
   branch input is spectrally concentrated, the narrowband guard's
   detector reads branch 0's spectrum and its freeze covers all
   branches. Believed sufficient; the G.168 tone rows with branches
   ON are the check, not the belief.
5. **Cost.** bench baseline: fdkf 63 µs of the 208 µs / 5.33 ms chain
   budget (48 kHz f64, 8 partitions). Update cost is ~linear in total
   partitions Σ_b P_b, plus one forward FFT per branch per block.
   Per-branch partition counts keep this honest: distortion rides the
   same short path as the linear echo, so nonlinear branches need only
   the direct+structure support (2 partitions at the certified
   geometry; at a future outdoor short-geometry preset everything is
   short). Ballpark {8 + 2 + 2}: ~+35 %, well inside desktop budget;
   M55 verdict deferred to its milestone with the icount ratchet.

## 6. Architecture in the codebase

- **Extend `partitioned_fdkf` in place** (option A), config gaining a
  branch spec: `std::vector<branch>` where
  `branch = {basis kind, parameter, normalization gain, partitions}`,
  empty vector = today's single linear branch. Rationale: the update
  is one loop-nest generalization (flattened (b, p) axis, per-branch
  input windows/rings), and the alternative — a sibling class — forks
  400 lines of certified update code that then drifts. The
  non-negotiable gate: **empty-spec configs are bit-identical** to
  today's core, dump-verified over the full certified battery exactly
  like the notch knobs and the mechanical pass were. If bit-identity
  proves fragile under the refactor, fall back to a sibling type and
  accept the duplication (recorded here so the fallback is a decision,
  not a drift).
- **Branch signal generation lives in the core.** `process_block`
  keeps its (x, y, e, yhat) signature; the core applies φ_b elementwise
  to the new B input samples into per-branch sliding windows (memoryless
  basis commutes with the overlap-save window; one φ evaluation per
  sample per branch). aec_chain, pem_afc call sites, compliance_dut,
  the C ABI: all untouched.
- **`aec_chain` needs one deliberate decision, not a redesign:** the
  chain constructs its shadow from `cfg.canceller` overriding only
  partitions/transition — with branches in the config the shadow would
  inherit them. The shadow stays LINEAR (clear the branch spec in the
  shadow config): its job is a fast path-mismatch comparator, its
  DT-immunity argument (performance comparison, not correlation) does
  not need distortion modeling, and a linear shadow keeps its cost at
  today's ~25 %. A one-line, gated chain change.
- **Presets.** `aec_chain_preset` is untouched (certified). A new
  `outdoor` preset family (this effort's Stage 3, folding in the
  short-geometry follow-up (d)) enables branches with the measured
  basis, branch prior and per-branch partitions.
- **`copy_impulse_response` / `partition_spectrum`** grow branch-aware
  overloads; the existing signatures keep returning branch 0 (the
  linear path — what misalignment metrics and pem_afc mean by "the
  filter").

## 7. Validation plan and acceptance

Instrument first (fixture extensions, measured-first like Stage 0):
hard-clip mode on `speaker_drive`; basis-normalization calibration
test; off-model drive rows in the scratch battery.

The acceptance ladder, each a regression gate when it lands:

1. **Parity**: empty branch spec bit-identical on the certified dump;
   branchless-parity fingerprint unchanged; full suite green.
2. **On-model recovery** (tanh drive, erl −20): raw suppression at
   mild/moderate must move decisively toward the linear row's 50.2 dB
   — the static nonlinearity is exactly representable, so the gap that
   remains measures misadjustment + collinearity, and the measured
   number becomes the gate. Hypothesis to beat: literature power-filter
   gains are 10..20 dB over linear; anything under +10 dB at mild says
   the structure is losing to §5.1/5.2 and the remedies escalate.
3. **Off-model generalization** (hard-clip drive): the basis choice is
   made on THIS row, not row 2.
4. **No regression where it must not**: drive-0 misadjustment cost on
   the outdoor linear rows (< 1 dB target); Stage 0 DT rows branches-ON
   within margin; certified ITU/G.168 battery with branches ON at the
   certified geometry measured and recorded (characterization, like the
   nn engine — certification stays branches-OFF until Tim says
   otherwise).
5. **float32 parity + tone battery** branches-ON (the §5.1 walk check),
   then bench (the §5.5 numbers), M55 deferred.

## 8. Staged plan

- **Stage 1 — this document.** Scope, structure, watch-list, gates.
- **Stage 2 — scratch prototype + basis bake-off.** Core extension
  behind the empty-spec parity gate; scratch battery per §4/§7 rows
  2–4; basis and branch_prior chosen by the table, decisions recorded
  here.

### Stage 2 delivered (measured; the full tables live in
### tests/test_multibranch.cpp's banner)

The core extension landed as designed (§6 option A): branch spec on
`partitioned_fdkf::config`, per-branch windows/rings/state, joint
denominator, constraint per (branch, partition), branch-0 statements
untouched; the empty-spec configuration is byte-identical on the full
certified ITU dump, and the shadow-stays-linear line landed in
aec_chain. Two design deltas the measurements forced, both now in §4's
spirit rather than its letter:

- **`center` generalized to every basis kind** (not just powers).
  Round 1 measured the collinearity cost directly: plain x³ pays 15 dB
  of clean-drive misadjustment (50.2 → 34.9) and every un-centered
  basis paid 14..18; LS-centering (c\* = E[xφ]/E[x²] over the actual
  material, not the Gaussian moment formula) restored the clean rows to
  ~47.5 and bought +19 dB at mild drive.
- **A Gram-Schmidt `chain` coefficient per branch** — round 2 found
  branch PAIRS each orthogonal to x still pay ~8 dB against *each
  other*; chaining the second branch against the first's finished
  signal recovered ~5.5 dB and produced the winner.

**The verdict: {x³ orth, x⁵ orth GS-chained}, partitions 2 per branch,
prior 0.1** — at 48 kHz, erl −20: mild 46.4 (linear 25.6, **+20.8**),
moderate 39.7 (**+24.1**), severe 24.6 (+14.4), off-model hard clip
26.8 (+14.4), clean-drive cost 5.6 dB. Runner-up notes that survive
into presets: `tanh_difference` at the device's measured knee hits
49.9 at moderate (the exactly-representable case — the per-device
preset when a curve is measured); `clip_difference` wins the hard-
limiter column (32+ both rates). The §7-row-2 hypothesis ("under
+10 dB at mild says the structure is losing") was beaten by 2x.

**The watch-list scored** (§5, in order): (1) collinearity was indeed
the #1 binding constraint and the remedies were exactly the predicted
family — centering, GS, and the novelty discount: the 16 kHz clean row
showed EVERY branched config paying ~14 dB, and the block-128-notch
counter-measure values (novelty 0.8/0.1) erased the cost at 48 kHz
(clean 44.6 → 52.8) and recovered +6 at 16 kHz (35.6 → 41.5; the
remaining ~12 dB against the linear core's exceptional 53.7 is the
recorded cost of branches at 16 kHz — future work, and clean-drive
rigs take the single-branch or branchless preset anyway). (2) The
Ψ_s race never bit: the prior sweep (1.0..0.03) moved moderate-drive
suppression by only 0.4 dB at 48 kHz — default 0.1 held. (3) DT
safety passed emphatically: permanent double talk at moderate drive,
winner 38.3/35.1 dB vs linear 15.6/15.9 — the model-based immunity
holds on the branch axis. (4) Tones and (5) M55 cost: Stage 4.
- **Stage 3 — land + outdoor preset.** Gates pinned in test_outdoor
  (new rows) and test_fd_kalman (parity, contract, validation);
  outdoor preset with short geometry; HANDOFF Rev 6 updated.

### Stage 3 delivered (measured)

`tap::mu::aec_chain_outdoor_preset(block_size, sample_rate)` landed in
postfilter.h: the certified chain preset re-based onto the short
outdoor geometry (partitions cover ~8.5 ms and no more: 2 at
48 kHz / block 256, 1 at 16 kHz), the Stage 2 winner branches with the
calibration constants pinned at the −10 dBm0 shaped-CSS operating
plane (rate-invariant to 0.1 %; test_outdoor gates the pinned values
against a fresh calibration at both rates), and the novelty discount
always on. The shadow never exceeds the short main geometry and stays
linear.

The chain-level table (erl −20, vs the certified chain's Stage 0 rows
on the identical scenario; supp dB / residual dBm0(A), 48 kHz):
clean 58.8/−46.5 → 61.8/−39.5; mild 29.8/−18.2 → **57.2/−43.2**;
moderate 19.3/−10.3 → **44.4/−33.8**; severe 20.6/−11.4 → 28.6/−21.5
(16 kHz equivalent or better — moderate 43.3/−33.8). Permanent double
talk at moderate drive: suppression 36.0/35.3 with send delta
**+0.4/−0.3 dB** where the certified chain read +20.1 — full duplex
restored under distortion. Convergence by 1.2 s at parity with the
certified chain (49.9/40.1 vs 51.1/40.1) with deeper steady state.
The suppressor multiplies rather than merely adds: at moderate drive
the raw winner reads 39.7 and the chain 44.4 — with the harmonic
content in the echo estimate, the coherence machinery finally has
evidence at the distortion frequencies (the certified chain could add
only 2..4 dB over ITS raw core on these rows).

FILED OBSERVATION: the clean-drive 48 kHz residual MAX reads −39.5 vs
the certified chain's −46.5 while average suppression is 3 dB deeper —
an instantaneous-onset effect (novelty-on weight motion at CSS voiced
onsets is the suspect); no distorted row is affected (mild's residual
is 25 dB BELOW the certified chain's). Worth its own look alongside
the 16 kHz branch clean-cost item.
- **Stage 4 — float32 + bench.** Parity pass, tone battery, bench
  rows; icount/M55 with that milestone, not this one.
- **Stage 5 — externals.** `mutap.aec~` branch attributes + help/ref,
  the submodule dance, after the core proves out.

## 9. Open decisions (for Tim)

- **Default basis family** — B2 vs B3 is a philosophy call if the
  bake-off is close: polynomials (device-agnostic, more collinear) vs
  clipped-difference (a per-device knee to calibrate, cleaner
  separation). The bake-off table decides unless you have a device
  preference now.
- **Characterize vs certify** — running the certified ITU battery
  branches-ON: the plan records numbers only (like the nn engine). Say
  the word if branches-ON should become the certified configuration
  for any geometry.
- **mutap.aec~ exposure timing** — Stage 5 here, or defer externals
  until the outdoor preset (Rev 6 (d)) also exists so Max gets one
  coherent "outdoor" story instead of two attribute drops.
