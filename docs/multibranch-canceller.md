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

### Stage 4 delivered (measured)

Float32 (the deployment precision) holds the outdoor chain to within
**0.04 dB** of the double golden model on every drive row at both
rates — better than the classical chain's own 0.1..0.7 dB parity; the
short path and structure-limited suppression leave rounding nothing to
bind on. The §5.1 tone-walk re-check: a 30 s on-bin tone through the
float32 outdoor chain (branches ON, narrowband guard per the float
preset) reads −115.3 dBm0(A) residual at 48 kHz and −42.8 at 16 kHz on
a +10 dBm0 echo, no walk, no divergence — the branch-augmented null
space stays contained by the guard + constraint discipline. Bench
(`bench_outdoor_chain`, same-machine ratios per the README rule): the
outdoor chain is **cost-neutral** against the certified chain (207.3
vs 207.5 µs at 48 kHz f64) — the short geometry pays for the branches
almost exactly, and the suppressor term is unchanged. The remaining
Stage 4 item deferred to the M55 milestone as planned: the icount
ratchet rows.

**Post-audit addendum — the ratchet caught what nothing else did.**
The existing M55 icount CI gate flagged the chain workload at
+4.25/+4.92% instructions (gate 3 %) with EMPTY branch spec — while
the byte-identical dump, the full test suite, x86 wall-clock bench and
the Hexagon icount rows (+0.10/+0.14 %) all read clean, and the
standalone fdkf workload moved 0.02 %. Mechanism: the branch loops
written inline in process_block shifted GCC-arm's -O3 codegen for the
inlined-into-aec_chain context. Fix, reproduced and verified locally
under QEMU/MPS3: the four branch sections extracted into
MUTAP_NOINLINE private methods behind `if (!branches.empty())` —
chain rows back to +0.01/+0.00 % against the UNCHANGED baselines (no
re-record), dump still byte-identical to the pre-branch baseline,
branches-on batteries unchanged. Lesson recorded: on the deployment
target, code SIZE in the hot path is behavior, and only the
per-target instruction ratchet sees it.
- **Stage 5 — externals.** `mutap.aec~` branch attributes + help/ref,
  the submodule dance, after the core proves out.

### Stage 5 (out of order) — the adversarial audit (delivered)

Before externals, the whole effort was audited adversarially: a
high-effort code review of the branch diff plus counter-experiments
attacking the claims themselves. Everything below is measured; the
fixes landed with this record.

**Code review findings (all fixed):** (1) an unused helper broke the
three -Werror CI matrix builds; (2) the outdoor preset's "novelty
always on" claim was FALSE at 16 kHz — the coherence tracker needs the
partition ring (p_n >= 2) and the preset's natural 16 kHz geometry is
1 partition, so the knobs were silently inert. The audit then measured
the naive fix (force 2 partitions) and REJECTED it: 1 partition
without the discount beats 2 with it by 10 dB on the 16 kHz clean row
(66.6 vs 56.5; mild 54.1 vs 51.4) — a 1-partition geometry has no
partition-redistribution null space at all, which is worth more than
the counter-measure. Resolution: geometry stands, knobs set only where
they act, comment corrected. (3) UB in branch validation (float->int
cast before the range check; NaN center/chain unvalidated) — fixed
with pre-cast range checks and finiteness validation. (4) The
calibration formulas existed in three drifting copies — consolidated
into tests/support/outdoor_scenario.h (branch_center_and_gain /
branch_gs_chain / winner_branches), now the single instrument every
test and the preset gate use.

**Counter-experiment verdicts:**

- **The "full duplex" claim was revised.** The DT level delta (+0.4 dB)
  cannot distinguish talker-preserved from talker-ducked, and a
  sharper instrument — the P.501 AM-FM orthogonal pair with a
  no-near-end control run — shows the send-band output at moderate
  drive is DOMINATED by residual echo intermodulation sitting ~9 dB
  above the talker's own in-band comb energy (floor 9.2/9.8 dB above
  at 48/16 kHz; the near end itself is not ducked). Honest claim:
  **level duplex** — the residual is pulled down TO the talker's
  level, from 20 dB above. The IMD floor is now a permanent gate
  (test_outdoor DtImdFloor) so the device-trained-suppressor work has
  its number to move. Caveat recorded: the AM-FM multi-sine through
  tanh is an IMD torture signal — speech-material IMD will read
  kinder; the gate is deliberately the harsh instrument.
- **Level sensitivity is real and now documented as a deployment
  requirement.** Pinned-at-−10 dBm0 constants cost ~7 dB at a −4 dBm0
  operating level and ~16 dB at −16 dBm0 versus level-matched
  recalibration (43.4 vs 59.2 at −16; 21.8 vs 28.6 at −4; identical
  at −10 by construction). The preset doc now says MUST recalibrate
  off-plane; **level-adaptive centering is the filed follow-up** for
  level-varying rigs.
- **Material-shape robustness passed cleanly**: CSS-calibrated
  constants on white noise measure 39.03 vs 39.02 recalibrated — the
  calibration captures level moments, not CSS structure. The
  overfitting risk was level, not material.
- **Seed/geometry robustness passed**: the headline chain row across
  four structure seeds spans 43.8..46.0 dB, and a hostile geometry
  (0.5 m mount, 0.95 ground reflectance, −10 dB structure) reads
  43.9 — every pinned gate holds with >= 2.8 dB headroom.

**What the audit did NOT find:** any correctness defect in the core
branch update path — the (b, p) extension, empty-spec bit-identity,
ring/stride indexing and gate arithmetic all held under line-level
review; and the two Stage 3/2 filed observations (16 kHz branch clean
cost at the certified geometry, clean-drive onset residual) remain
open items, unchanged by the audit.

## Stage 6 — level-adaptive centering (PLAN, awaiting Tim's review;
## nothing below is implemented)

The Stage 5 audit's one open vulnerability: the branch constants are
pinned at the −10 dBm0 shaped-CSS plane, and off-plane the pinned
constants cost ~7 dB at −4 dBm0 and ~16 dB at −16 dBm0 against
level-matched recalibration — while material SHAPE costs nothing
(white noise: 39.03 vs 39.02). This stage makes the constants track
level, so the preset works on rigs whose playback level varies.

### 6.1 The mechanism, and why the fix is one scalar

The centering exists to decorrelate each branch input from x; the LS
center is a moment ratio, and moments scale with level. Under pure
amplitude scaling x → s·x of fixed-shape material the laws are CLOSED
FORM and material-independent (verified numerically to 6 digits on
heavy-tailed material):

    c_p(s) = c_p(1) · s^(p−1)      (c3 ∝ s², c5 ∝ s⁴)
    g_p(s) = g_p(1) · s^(−p)       (g3 ∝ s⁻³, g5 ∝ s⁻⁵)
    chain  : INVARIANT in s

The audit already established shape-invariance is what holds and
level is what breaks — exactly the regime where these laws are exact.
So no adaptive moment estimation (the Küch-style tracker, option B
below) is needed: ONE slowly tracked scale statistic ŝ = rms(x)/rms₀
(rms₀ = the calibration plane's RMS, a new config field since the
library is unit-agnostic) corrects everything.

### 6.2 Proposed form (A2): normalize-in, scale-out

Rather than rescaling three constants per branch, evaluate the basis
on the normalized input and restore the scale on the way out — the
algebra collapses to two extra multiplies per branch:

    phi_adapted(x) = ŝ · g⁰ · ((x/ŝ)^p − c⁰ · (x/ŝ))
                   = g⁰ · (x^p · ŝ^(1−p)  −  c⁰ · x)

Properties: at ŝ = 1 it is bit-identical to today's evaluation (cold
start initializes ŝ to the plane, so behavior is EXACTLY pinned until
evidence accrues); the branch signal's amplitude tracks x linearly,
so the branch path weight cleanly absorbs the physical level-
dependent distortion fraction (which adaptation must track anyway —
that is the physics, not a defect); decorrelation against x holds at
every level by the laws above; and the GS chain subtraction needs no
change (both branch signals scale as s, chain invariant).

The ŝ tracker: one-pole on the REFERENCE block power (never the mic —
x is exogenous, so the tracker is double-talk-immune by
construction), smoothing ~1 s of real time, updated only on active
blocks (block power above a small fraction of the tracked level —
CSS pauses must not drag ŝ toward zero, since ŝ^(1−p) grows as ŝ
shrinks), hard-clamped to ±18 dB around the plane, updated inside
branch_gather (already out of the hot path and MUTAP_NOINLINE — the
icount lesson is baked into the placement, and empty-spec parity is
structural since the tracker only runs with branches present).

Scope limit, stated now: this applies to the odd_power bases (the
shipped winner). The knee bases (clip_difference / tanh_difference)
have an ABSOLUTE knee — the speaker clips at a fixed signal level
regardless of program level — so normalizing their input would
wrongly move the knee; their centers could be tracked separately, but
that is deferred until a knee-based preset exists to need it.

### 6.3 Config surface (all default-off, empty/off bit-identical)

    Sample level_adapt = 0;   // one-pole smoothing of the reference
                              // power tracker, 0 = off (today)
    Sample level_ref   = 1;   // rms the branch constants were
                              // calibrated at (the preset sets
                              // 0.2203 = -10 dBm0)

Clamp (±18 dB) and activity gate (~−20 dB relative) as documented
constants first; promoted to knobs only if the scratch says they need
tuning.

### 6.4 Scratch battery (measure first; every number below is a
### hypothesis until then)

1. DECOMPOSITION: at −16/−4 dBm0, recalibrate ONLY the centers vs
   ONLY the gains vs both — which half of the 16/7 dB is collinearity
   (center) and which is prior/normalization (gain). Understanding
   for the record; A2 fixes both jointly regardless.
2. STATIC LEVELS: moderate-drive row at {−16, −10, −4} dBm0 —
   tracker-on vs pinned vs the recalibrated oracle. Acceptance
   hypothesis: recover ≥ 80 % of the oracle gap off-plane (43.4 →
   ≥ 55 at −16; 21.8 → ≥ 27 at −4) at ≤ 1 dB cost on-plane.
3. SMOOTHING SWEEP: ŝ time constant {0.25, 1, 4} s — the
   nonstationarity-churn cost at steady level vs step-response speed.
4. LEVEL STEPS (the actual use case): −10 → −4 and −10 → −16 mid-run;
   settle to within 2 dB of the static tracker-on figure within a few
   seconds; watch the rescue/shadow machinery for false fires (a
   level step is not a path change; the shadow's DT-immune statistic
   should not care, but that is a claim to measure, not assume).
5. SILENCE / COLD START: long CSS pauses, start-from-silence — ŝ must
   hold the plane, never walk; the activity gate's job.
6. SAFETY ROWS: permanent DT at moderate drive with the tracker on
   (must hold ~38 dB — x-side tracker, expect no effect); float32
   parity; the AM-FM DtImdFloor gate unmoved.
7. PARITY: empty-spec and level_adapt=0 byte-identical on the dump;
   local M55 icount (toolchain now in-container) before push, per the
   Stage 5 addendum's lesson.

### 6.5 Rejected-unless-scratch-disagrees alternatives

- B: adaptive per-moment tracking (running E[x²], E[x⁴], E[x⁶],
  Gram-Schmidt online, Küch & Kellermann style). Strictly more
  general — it would also track material-shape drift — but the audit
  measured shape-invariance, it adds four tracked statistics with
  their own silence/DT pathologies, and it cannot beat closed-form
  laws in the regime that actually failed. Revisit only if scratch
  row 2 shows the laws missing the oracle badly (which would mean
  level steps change the material's SHAPE, e.g. a limiter upstream).
- C: multi-level calibration tables with interpolation. Clunky,
  material-plane-multiplying, and dominated by A2's exact laws.

### 6.6 Landing plan

S1 scratch (rows 1–6) → decisions recorded here; S2 land the knob
default-off with gates (parity trio + the static-level and step rows
pinned measured-first) + HANDOFF; S3 the exposure decision — see
§9 addendum below.

### 6.7 ADVERSARIAL AUDIT OF THIS PLAN (measured; plan amended below)

Tim's directive before implementation: attack the plan down to the
physics. Three counter-experiments ran (25 s runs, 48 kHz, erl −20,
moderate drive; suppression over the plan's original window 4..8.4 s
["early"] and 18..25 s ["late"], plus the A-weighted send residual
over the tail):

    level  variant   early    late    residual dBm0(A)
    −16    pinned    43.4     45.6    −40.4
    −16    oracle    59.2     56.8    −50.9
    −16    loudcal   40.4     44.7    −41.0
    −10    pinned    44.4     44.0    −33.3
    −10    loudcal   34.3     35.7    −26.1
    −4     pinned    21.8     26.4    −13.6
    −4     oracle    28.6     28.6    −15.4

**What survived attack.** The off-plane penalty at the QUIET side is
a genuine steady-state loss, not a transient: the representability
argument (mis-centering only adds a component along x, and
Span{x, φ+δx} = Span{x, φ}, so a joint estimator loses nothing) is
true but does not rescue the diagonal update — the late window still
shows an 11 dB ratio gap at −16. The mechanism is persistent
misadjustment from cross-channel gain competition (the TCLwdt
weight-motion family), not slow convergence. The closed-form scaling
laws and the DT-immunity of a reference-side statistic also survived.

**What fell.**
1. **The value was framed in the wrong units.** In ABSOLUTE send
   residual — the product quantity — the quiet side is
   self-forgiving: pinned at −16 already reads −40.4 dBm0(A), seven
   dB QUIETER than the on-plane operating point, because the echo
   scales down with the program. The oracle's 10.5 dB there polishes
   a corner that does not bind. At the LOUD side, where residual
   genuinely worsens (−13.6), the early-window 6.8 dB gap mostly
   closes by itself (late: 2.2 dB) — the oracle chiefly buys
   SETTLING SPEED after arriving at a loud level, and ~2 dB steady.
2. **Alternative F (calibrate at the loud edge) is measured dead**:
   −4-calibrated constants cost 8.3 dB at the plane (44.0 → 35.7
   late). Quiet-side forgiveness is not symmetric in calibration
   error.
3. **The plan built an estimator for a control input.** Every real
   rig KNOWS its playback level — volume is set upstream of the
   (post-processing) reference tap the house doctrine already
   mandates. A `set_reference_level()` API applying the exact laws
   (alternative G, absent from the original plan) achieves the FULL
   oracle line deterministically — the oracle rows above ARE its
   measured performance, since set_level_dbm0 is pure scaling — with
   zero estimation hazards. The ŝ tracker is only needed by rigs
   that cannot plumb their own volume, and it carries all the
   hazards for the smaller remainder.
4. **Two defects in the tracker half as specified**: (a) a
   calibration-convention mismatch — the constants are calibrated on
   whole-material RMS but an activity-gated tracker measures active
   RMS, +1.74 dB apart on the CSS (measured, active fraction 0.67),
   so "bit-identical at the plane" fails once the tracker converges
   unless level_ref is defined in the tracker's own statistic; and
   (b) the on-plane cost test was too kind — ŝ rides the program's
   own phrase-scale envelope continuously (and A2's two terms scale
   differently in ŝ, so ŝ motion morphs the branch signal — a
   partial path change, not a gain change), so the honest on-plane
   row is envelope-wandering material at fixed volume, not steady
   CSS.

**Amended recommendation.** (i) If Stage 6 proceeds: G-FIRST —
land `set_reference_level(s)` applying the closed-form laws (small
RT-safe API, deterministic, gated by the identity set_level ≡
level-matched recalibration), and demote the ŝ tracker to an
optional follow-on for volume-blind rigs, with the convention fix
and the envelope-wander row added to its battery. (ii) It is equally
defensible to DEFER Stage 6 entirely: the absolute-residual analysis
says the binding outdoor problem is the loud edge, where even the
oracle only reaches 28.6 dB — basis saturation, which is the
device-trained suppressor's territory (the DtImdFloor gate), not
calibration's. Level adaptation polishes; the suppressor attacks the
bottleneck.

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
  *(RESOLVED: shipped together — @outdoor landed with the preset.)*
- **Stage 6 exposure (NEW, with the level-adaptive-centering plan
  above)** — once the tracker measures well: (i) does
  aec_chain_outdoor_preset enable it BY DEFAULT? Default-on changes
  every pinned outdoor gate (full re-measure — the on-plane cost had
  better measure ~0) but makes the preset honest for level-varying
  rigs, which is most outdoor rigs; opt-in keeps the certified-style
  stability and puts a knob in the deployment's hands. (ii) Does
  mutap.aec~ then need anything, or does @outdoor inherit the preset's
  choice silently? Recommendation deferred until scratch row 2/3
  numbers exist; the on-plane cost decides (i) almost by itself.
