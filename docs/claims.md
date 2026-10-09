# Claims — the numbers README.md quotes, and what gates them

This table is hand-maintained (production-readiness plan M0e) until the claims
tier's `RecordProperty` output generates it (M6d). One row per number that
README.md's "## Status" section and the "## ITU-T compliance" table quote,
in README order. Compiled 9 October 2026 against README.md at `393c666`
(2026-10-08) on branch `feat/m0-hygiene`.

**The rule: a row with no gate is a finding.** "Gate" means a gtest
assertion in `tests/` whose bound the number sits inside; "doc table" means
the number lives in a document's measured table and nothing asserts it;
"none" means the number appears only in README or only in a code comment.
A README number that no longer matches the test's current comment is marked
`DRIFT` in Note — recorded here, not fixed.

Conventions. *Threshold* quotes the `EXPECT`/`ASSERT` bound and, after a
semicolon, the measured value in the comment beside it (house convention:
every assertion carries its measured value). *Host* is only what the test
or doc states; "not stated" means exactly that — most pre-#66 comments name
no host.¹ *Date* is the first commit that introduced the quoted comment
fragment or threshold (`git log -S` on the test file; footnote 2 where a
different method was used). Typed suites: `/0` = float, `/1` = double.

Two README bullets quote no numbers and have no rows: the anti-howl safety
layer (`docs/howl-guard.md`) and the reverb stage (`docs/reverb-afc.md`).

## Status — FFT, FDAF, closed-loop simulator (README lines 38–72)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 1 | CMSIS Helium FFT vs srdif on the bare-metal M55 | ~42% fewer chain instructions | none — `docs/optimization.md` "~42% reduction in total M55 instructions"; `bench/README.md` repeats it | none (the icount ratchet gates absolute baselines, not the ratio) | not stated | 2026-07-19 | descriptive; doc, not a gate |
| 2 | CMSIS FFT size range every constructor checks | FFT sizes 32 … 4096 | none located — `docs/optimization.md` "Size range: 32 … 4096 only" | none located in this audit | n/a | 2026-07-19 | descriptive; doc |
| 3 | FDAF white-noise identification, float64 | below −100 dB misalignment | `tests/test_fdaf.cpp :: fdaf_test/1.WhiteNoiseIdentificationConverges` | `EXPECT_LT(final_misalignment_db, -100.0)`; no measured value in comment | not stated | 2026-07-14 | gate; also `FdafCrossPrecision.FloatTracksDouble` asserts r64 < −100 |
| 4 | FDAF white-noise identification, float32 | below −55 dB | `tests/test_fdaf.cpp :: fdaf_test/0.WhiteNoiseIdentificationConverges` | `EXPECT_LT(final_misalignment_db, -55.0)`; no measured value in comment | not stated | 2026-07-14 | gate |
| 5 | float32 tracks the float64 golden model | "to single-precision depth" | `tests/test_fdaf.cpp :: FdafCrossPrecision.FloatTracksDouble` | `EXPECT_LT(power_db(err/ref), -55.0)` on the recovered IRs; no measured value in comment | not stated | 2026-07-14 | gate |
| 6 | Loudspeaker band on every closed-loop path | 80 Hz highpass, 16 kHz lowpass | none — `tests/support/loudspeaker_band.h` header (80 Hz 2nd-order HP, 16 kHz 4th-order LP) | none (fixture definition) | n/a | — | descriptive; fixture, not a claim |
| 7 | Closed-loop claims are medians over seed sets | five seed sets | none — `tests/support/rooms.h` `k_claim_seed_sets = 5` | none (fixture constant) | n/a | — | descriptive |
| 8 | Measured open-loop MSG over the −20·log₁₀ max\|F\| bound | median +0.79 dB | `tests/test_closed_loop.cpp :: ClosedLoopMsg.MeasuredOpenLoopMsgMatchesTheory` | `EXPECT_NEAR(median(over_theory), 0.0, 3.0)`; "measured median +0.79 dB" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 9 | Measured MSG over the phase-exact limit | median +0.26 dB | same test | `EXPECT_GE(median(over_exact), 0.0)` and `EXPECT_LE(…, 1.0)`; "measured +0.26" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 10 | Naive FDAF adds stable gain on white near-end (M2 baseline) | median +7.95 dB | `tests/test_closed_loop.cpp :: closed_loop_host_test/{0,1}.NaiveCancellerAddsStableGainOnWhiteNearEnd` | `EXPECT_GT(median(asg), 3.0)`; "measured ASG median +7.95 dB (per set +4.19..+11.12), float = double" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-25 (gate) | gate; same number re-quoted at README line 87 |
| 11 | Naive FDAF destabilizes on tonal near-end | median ASG −15 dB (probe floor) | `tests/test_closed_loop.cpp :: closed_loop_host_test/{0,1}.NaiveCancellerDestabilizesOnTonalNearEnd` | `EXPECT_LT(median(asg), -1.0)`; "measured median −15.00 dB, the probe's floor"; also `EXPECT_GT(median(mis), 0.0)` (+12.95 / +13.97) | Linux x86-64, GCC 13.3, Release¹ | 2026-09-25 (gate) | gate; the −15 floor is the probe's lower search bound (`open_msg - 15.0`), so the gate can only bound it from above |

## Status — PEM prewhitening (README lines 74–89)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 12 | PEM tonal ASG, double | +9.39 dB | `tests/test_pem_afc.cpp :: pem_afc_host_test/1.StabilizesTonalNearEndWhereNaiveHowls` | `EXPECT_GT(median(asg), 3.0)`; "measured median +9.39 double / +7.08 float"; stability `EXPECT_GE(stable_minus3, 4)`, `EXPECT_GE(stable_plus1, 4)` | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 13 | PEM tonal ASG, float | +7.08 dB | `… :: pem_afc_host_test/0.StabilizesTonalNearEndWhereNaiveHowls` | as row 12 | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 14 | PEM speech-envelope ASG | +10.55 dB | `tests/test_pem_afc.cpp :: PemAfc.AddsStableGainOnSpeechEnvelopeNearEnd` | `EXPECT_GT(median(asg), 4.0)`; "measured median +10.55 dB" (per set +4.19..+15.75) | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 15 | PEM voiced (pitch-periodic) ASG | +3.90 dB | `tests/test_pem_afc.cpp :: PemAfc.AddsStableGainOnVoicedNearEnd` | `EXPECT_GT(median(asg), 0.5)`; "measured median +3.90 dB"; `EXPECT_GE(stable, 4)` | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 16 | PEM white ASG | +10.26 dB | `tests/test_pem_afc.cpp :: PemAfc.MatchesNaiveOnWhiteNearEnd` | `EXPECT_GT(median(asg), 5.0)`; assertion message "measured median +9.68 dB"; file header "ASG median +10.26 dB" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 (header) / 2026-09-25 (message) | **DRIFT: test's assertion message says +9.68** (file header says +10.26; README copies the header) |
| 17 | PEM tonal misalignment vs naive (not in README prose, cited file) | — | `tests/test_pem_afc.cpp :: PemAfc.ReducesTonalBiasVersusNaive` | `EXPECT_LT(median(pem_mis), 11.0)`; "+5.91"; gap > 3 ("7.47") | not stated | 2026-07-14 | listed for completeness; README cites the file, not the number |

## Status — adaptation control (README lines 91–111)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 18 | IPC while unconverged (open-loop AEC) | ~0.6 | `tests/test_adaptation_control.cpp :: AdaptationControl.IpcTracksEchoVsDoubleTalk` | `EXPECT_GT(early/n_early, 0.5)`; "measured 0.64" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 19 | IPC converged | 0.00 | same test | `EXPECT_LT(conv/n_conv, 0.1)`; "measured 0.00" | as row 18 | 2026-09-30 | gate |
| 20 | IPC under double-talk | 0.02 | same test | `EXPECT_LT(dt/n_dt, 0.1)`; "measured 0.02" | as row 18 | 2026-09-30 | gate |
| 21 | Raw-pair IPC in the tonal closed loop | 0.82 | `tests/test_adaptation_control.cpp :: AdaptationControl.PemPrewhiteningReducesIpc` | `EXPECT_GT(median(naive_ipc), 0.55)`; "measured median 0.82" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 22 | Prewhitened-pair IPC | 0.08 | same test | `EXPECT_LT(median(pem_ipc), 0.25)`; "measured median 0.08"; gap > 0.3 | as row 21 | 2026-09-30 | gate |
| 23 | +20 dB burst: gated vs ungated worst block RMS, double | 60.6 vs 27,076 | `tests/test_adaptation_control.cpp :: burst_host_test/1.GatingContainsNearEndBurst` | `EXPECT_GT(median(ungated), 3000.0)`; `EXPECT_LT(median(gated), 1000.0)`; `EXPECT_LT(gated, ungated/10)`; "measured median 27,076 / 30,248", "60.59 / 67.53" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 24 | Same, float | 67.5 vs 30,248 | `… :: burst_host_test/0.GatingContainsNearEndBurst` | as row 23 | as row 23 | 2026-09-30 | gate |
| 25 | Gated loop under 1000 in 5 of 5 seed sets, both precisions | 5 of 5 | same tests — `contained_seed_sets` is `RecordProperty`'d and printed, **not asserted** | none on the count (the median gate < 1000 is the only assertion) | as row 23 | 2026-09-30 | **no gate on the count** (recorded only); finding |
| 26 | Gated loop contained 10 of 10 over sets 0..9 | 10 of 10 | none — comment in `burst_host_test` only | none | not stated | 2026-09-30 | **no gate**; one-off sweep; finding |
| 27 | Another standard library's rooms left 2 of 5 float trajectories uncontained | 2 of 5 | none — `HANDOFF.md` line 217; `tests/test_adaptation_control.cpp` canary comment ("5993.48 on the other standard library's rooms") | none | not stated (libc++ scenario per `tests/support/portable_random.h`) | — | descriptive; historical note |
| 28 | Variable regularization: scale-invariant 10⁻⁵× … 10³× | 10⁻⁵× to 10³× | `tests/test_adaptation_control.cpp :: AdaptationControl.RelativeRegularizationIsScaleInvariant` | `EXPECT_LT(identify(scale,…), -60.0)` at 1e-5, 1, 1e3; "measured −163 dB" | not stated | 2026-07-14 | gate |
| 29 | Fixed epsilon degrades by ~150 dB at 10⁻⁵× | ~150 dB | same test | `EXPECT_GT(identify(1e-5, 0, 1e-6), -20.0)`; "measured −8 dB" (−163 → −8 = 155 dB) | not stated | 2026-07-14 | gate (the degradation direction; the ~150 figure is the difference of two comments) |
| 30 | Variable regularization alone softens the naive tonal bias | ASG −12 → −3.5 dB | none — comment in `tests/test_closed_loop.cpp` `converge_naive_canceller` ("measured ASG −12 → −3.5 dB"); the M2/M3 tests pin `relative_regularization = 0` instead | none | not stated | 2026-07-14 | **no gate**; finding |

## Status — warped (music) predictor (README lines 116–139)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 31 | At λ = 0 the warped predictor reduces exactly to the plain one | exact | `tests/test_lpc.cpp :: WarpedLpcPredictor.LambdaZeroMatchesPlain` | not read in this audit beyond the test's existence (line 295) | n/a | — | gate exists; threshold not transcribed |
| 32 | Chord material: three fundamentals, a dozen partials below 500 Hz | 3 / ~12 / 500 Hz | none — comment in `tests/test_pem_afc.cpp` `WarpedPredictorRobustOnMusicAcrossRooms` | none (fixture description) | n/a | 2026-07-15 | descriptive |
| 33 | Warped without IPC scaling howls 15 dB below MSG, ~1 room in 5 | 15 dB, ~1 in 5 | none — comment in the same test; the failing configuration is not run | none | not stated | 2026-07-15 | **no gate** (negative result not asserted); finding |
| 34 | Warped+IPC music ASG across rooms 5..9 | +7.19…+9.38 dB | `tests/test_pem_afc.cpp :: PemAfc.WarpedPredictorRobustOnMusicAcrossRooms` | `EXPECT_GT(median(asg), 4.0)` per room; "+8.12/+8.44/+7.19/+9.38/+9.06 dB (lowest single set +4.38, room 8)" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 35 | Room 9, ten seed sets: warped+IPC never reached the −15 dB floor | 0 of 10 | none — comment in the same test | none (gated test runs five sets, asserts the median) | not stated | 2026-09-25 | **no gate**; one-off sweep; finding |
| 36 | Speech cascade collapsed 1 of 10 on room 9 | 1 of 10 (2026-09-25) | none — comment ("did once in ten") | none | not stated | 2026-09-25 | **no gate**; finding |
| 37 | Warped+IPC speech-envelope ASG | +17.81 dB | `tests/test_pem_afc.cpp :: PemAfc.WarpedPredictorHandlesSpeechEnvelopeToo` | `EXPECT_GT(median(asg), 10.0)`; "measured median +17.81 dB" (per set +17.19..+20.94) | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 38 | Warped defaults | λ = 0.5, order 16 | none — `include/mutap/lpc.h` config comment | none (defaults) | n/a | 2026-07-15 | descriptive |
| 39 | Bark-scale alternative | λ = 0.766 / order 24, ~1.5× cost | none — `include/mutap/lpc.h` line 207 | none | not stated | 2026-07-15 | descriptive; cost figure unmeasured in tests |

## Status — Cortex-M55 and Hexagon builds (README lines 140–163)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 40 | Tests run on-target (M55) | 128 tests | `tests/bare_metal_main.cpp` (positive filter; `selected < 30` fails the run) | `if (selected < 30) return 1`; comment "The on-target selection is 128 tests" | n/a | 2026-10-09 (M0a) | weak gate on the count. README said 63 and the comment 118 until M0a corrected both (the identical filter on the host binary lists 131: the three host-only rows it also matches); the on-target count itself is only ever printed by the leg |
| 41 | Hexagon HVX geometry | 128-byte vectors, 32 fp32 lanes | none — `cmake/hexagon-linux-musl.cmake` line 19 | none | n/a | — | descriptive |
| 42 | Hexagon per-push selection | 128 tests, ~8 min of TCG | `tests/CMakeLists.txt` `TEST_FILTER` (same list as bare_metal_main) | none on the count | n/a | 2026-10-09 (M0a; README said 63 from 2026-07-15²) | doc; "~8 min" is HANDOFF.md working note 4's figure ("the per-push selection takes ~8"), undated and not re-measured |
| 43 | Full suite validated once on the Hexagon ISA | 439 tests on the host build; validated on the ISA when it was 74 | none | none | n/a | 2026-10-09 (M0a; "74-test" from 2026-07-15²) | doc. README said "the full 74-test suite" until M0a; the ISA validation was of that 74-test suite and has not been repeated on the 439 |

## Status — PEM-FD-Kalman core (README lines 165–183)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 44 | Kalman misalignment at 0 dB SNR by block 300 | −15.5 dB | `tests/test_fd_kalman.cpp :: FdKalman.BeatsNlmsSpeedDepthTradeoffInNoise` | `EXPECT_LT(kal_mis, -12.0)`; "measured −15.5 dB" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 45 | NLMS μ = 0.5 at block 300 | −5.6 dB | same test | `EXPECT_LT(kal_mis, nlms_mis - 5.0)`; "measured gap 9.9 dB (NLMS mu=0.5: −5.6 dB)" | as row 44 | 2026-09-30 | gate (relative) |
| 46 | NLMS μ = 0.1 "slow start" | (−13.7 in header) | none — file header only; the test runs one NLMS at its default step, not μ = 0.1 | none | not stated | — | **no gate**; finding |
| 47 | Kalman tonal ASG, zero knobs | +7.19 / +7.19 dB double/float | `tests/test_fd_kalman.cpp :: kalman_loop_host_test/{1,0}.PemAddsStableGainOnTonal` | `EXPECT_GT(median(asg), 3.0)`; "measured median +7.19 (double) / +7.19 (float)" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 48 | Kalman speech-envelope ASG | +21.56 dB | `tests/test_fd_kalman.cpp :: KalmanPem.HugeGainOnBroadbandNearEnd` | `EXPECT_GT(median(asg), 15.0)`; "measured median +21.56 dB (the probe search ceiling is +25)" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 49 | Probe ceiling | +25 dB | none — `measured_msg_db(…, open_msg + 25.0, …)` in the test code | none (search bound) | n/a | — | descriptive |
| 50 | Warped+Kalman music ASG across rooms, no IPC | +10.62…+14.69 dB | `tests/test_fd_kalman.cpp :: KalmanPem.RobustOnMusicAcrossRooms` | `EXPECT_GT(med, 6.0)` per room; "+11.56/+13.44/+14.69/+12.50/+10.62 dB (lowest single set +9.38)" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 51 | Speech cascade + Kalman on room 9 | +11.25 dB | same test | `EXPECT_GT(speech, 5.0)`; "measured median +11.25" | as row 50 | 2026-09-30 | gate |
| 52 | +20 dB burst survived ungated | survived | `tests/test_fd_kalman.cpp :: KalmanPem.BurstSurvivedUngatedContainedWithFloor` | `EXPECT_LT(median(ungated_tail), 100.0)`; "measured 0.42" (one set in five still ringing at 59) | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate (tail RMS, not excursion) |
| 53 | Ungated burst excursion | ~2 dB | none — the figure appears in README only; not in `test_fd_kalman.cpp`, `fd_kalman.h` or `HANDOFF.md` (grepped) | none | not stated | — | **no gate, untraceable**; finding |
| 54 | Opt-in transient floor contains the hit to gated-NLMS quality | (11.34 median worst RMS) | same test as row 52 | `EXPECT_LT(median(floored_worst), 1000.0)`; "measured median 11.34"; `EXPECT_LT(median(floored_tail), 100.0)` ("0.36") | as row 52 | 2026-09-30 | gate |
| 55 | Transient floor's tonal-ASG cost | ~2–6 dB | none — `include/mutap/fd_kalman.h` line 91 config comment | none | not stated | 2026-07-15 | **no gate**; finding |

## Status — RIR fixtures (README lines 184–192)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 56 | Three physically-modeled rooms, conditioned | three rooms | `tests/test_rir_fixtures.cpp :: RirFixtures.FixturesAreConditionedAsDocumented` | `EXPECT_EQ(full_taps, 4096)`, `EXPECT_NEAR(energy, 1.0, 1e-3)`, `EXPECT_LT(argmax, 64)` | n/a | 2026-07-15² | gate (fixture contract) |
| 57 | Protocol | block 64, 16 partitions | none — test constants `k_block = 64`, `k_taps = 1024` | none | n/a | — | descriptive |
| 58 | Kalman ASG on the modeled rooms | +18.12…+19.38 dB | `tests/test_rir_fixtures.cpp :: RirFixtures.KalmanPemAddsStableGainOnModeledRooms` | `EXPECT_GT(med, 14.0)` per room; "measured medians >= +18.12 dB" (header: studio +18.12, rehearsal +19.38, hall +19.38) | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 59 | Classic stack on the studio | +11.25 dB | `tests/test_rir_fixtures.cpp :: RirFixtures.NlmsPemAddsStableGainOnStudio` | `EXPECT_GT(med, 6.0)`; "measured median +11.25 dB" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |

## Status — open-loop AEC (README lines 193–213)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 60 | Naive NLMS post-double-talk misalignment | +3.4 dB | `tests/test_aec.cpp :: AecDoubleTalk.NaiveDivergesWherePemHolds` | `EXPECT_GT(median3(naive_mis), -1.0)`; "measured median +3.4 dB" | not stated | 2026-07-15 | gate; medians over 3 seeds |
| 61 | PEM holds | −8.5 dB | same test | `EXPECT_LT(median3(pem_mis), -5.0)`; "measured median −8.5 dB" | not stated | 2026-07-15 | gate |
| 62 | Kalman core holds | −13.3 dB | same test | `EXPECT_LT(median3(kalman_mis), -10.0)`; "measured median −13.3 dB" | not stated | 2026-07-15 | gate |
| 63 | Kalman echo suppression through double-talk | 13.4 dB | same test | `EXPECT_GT(median3(kalman_sup), 10.0)`; "measured median 13.4 dB"; vs naive + 3 ("gap ~7 dB") | not stated | 2026-07-15 | gate |
| 64 | Naive single-talk ERLE on colored far-end vs PEM | ~44 dB vs ~20 dB | `tests/test_aec.cpp :: AecSingleTalk.NaiveTradesUniformDepthForExcitationWeightedErle` | `EXPECT_GT(rn.erle_db, rp.erle_db + 10.0)`; "measured 44.3 vs 20.0 dB" | not stated | 2026-07-15 | gate (relative) |
| 65 | PEM uniform depth | −20 dB | same test | `EXPECT_LT(mis(pem), mis(naive) - 12.0)`; "measured −20.4 vs −2.6 dB" | not stated | 2026-07-15 | gate (relative) |
| 66 | Gated M4 stack freezes through double-talk | kick ±0.1 dB | `tests/test_aec.cpp :: AecDoubleTalk.GatedNlmsFreezesThroughDoubleTalk` | `EXPECT_NEAR(mis_after_dt, mis_converged, 1.5)`; "measured kick +0.1 dB"; ERLE > 33 ("39.8"), suppression > 12 ("16.2") | not stated | 2026-07-15 | gate; single seed |
| 67 | Warped beat speech cascade on music double-talk | all 18 room × seed pairs | `tests/test_aec.cpp :: AecDoubleTalk.WarpedPredictorImprovesMusicDoubleTalk` | `EXPECT_GT(median3(warped_sup), median3(speech_sup) + 0.5)` on 2 rooms × 3 seeds | not stated | 2026-07-15 | partial: the gate covers 6 pairs (medians); "18 pairs across six rooms" is a comment |
| 68 | Per-room medians, warped vs speech | 17.3…19.6 vs 15.2…16.7 dB | same test | `EXPECT_GT(median3(warped_sup), 15.0)`; "measured medians 19.6 / 17.3 dB"; gaps "2.9 / 2.2 dB" | not stated | 2026-07-15 | gate on the warped medians; the speech-cascade values are only asserted via the gap |

## Status — residual-echo post-filter (README lines 214–230)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 69 | PEM refit floors misalignment near −20 dB; raw core reaches −75 dBm0(A) bare | −20 dB / −75 dBm0(A) | none — `tests/test_postfilter.cpp` header; `docs/itu-compliance.md` Stage 2 ("−75.6 dBm0(A) bare … PEM-Kalman plateaus at −28.9"); `ItuChain.PemAfcCancellerStillComposes` asserts only `le < ly - 12` ("16.5 dB") | none on −75 | not stated (double) | 2026-07-16 | **no gate** on the bare figure; doc |
| 70 | Single-talk residual, cabin | −79.9 dBm0(A) | `tests/test_postfilter.cpp :: ItuChain.SingleTalkResidualKalman` | `EXPECT_LT(residual, -72.0)`; "Measured: cabin −79.9" | not stated (double, block 256, 48 kHz) | 2026-07-16 | gate |
| 71 | Single-talk residual, studio | −88.9 dBm0(A) | same test | `EXPECT_LT(residual, -80.0)`; "studio −88.9" | as row 70 | 2026-07-16 | gate |
| 72 | P.1120 clause / margin target | < −58 / < −64 | none — `docs/itu-compliance.md` `ITU_EchoLevel` row | none (requirement) | n/a | 2026-07-16 | descriptive |
| 73 | Double-talk near-end attenuation | 1.05 dB | `tests/test_postfilter.cpp :: ItuChain.DoubleTalkSendAttenuation` | `EXPECT_LE(atten, 1.5)` cabin and studio; "cabin 1.05, studio 0.93 dB" | as row 70 | 2026-07-16 | gate |
| 74 | Clause / target | ≤ 3 / ≤ 1.5 | none — `docs/itu-compliance.md` `ITU_DtSendAtten` | none | n/a | 2026-07-16 | descriptive |
| 75 | Double-talk echo loss, every band | ≥ 34.1 dB | `tests/test_postfilter.cpp :: ItuChain.DoubleTalkEchoLossPerBand` | `EXPECT_GE(worst, {35.0 cabin, 33.0 studio})`; "cabin 38.0 dB (6660 Hz), studio 34.1 (270 Hz)" | as row 70 | 2026-07-16 | gate |
| 76 | Clause / target | ≥ 27 / ≥ 33 | none — `docs/itu-compliance.md` `ITU_DtEchoLoss` | none | n/a | 2026-07-16 | descriptive |
| 77 | Comfort noise level match | −1.2 dB | `tests/test_postfilter.cpp :: ItuChain.ComfortNoiseMatchesFloor` | `EXPECT_LE(lt - lq, 1.0)`, `EXPECT_GE(lt - lq, -2.5)`; "level delta −1.21 dB" | Linux x86-64, GCC 13.3, Release¹ | 2026-09-30 | gate |
| 78 | Comfort noise spectrum, per band | ≤ 1.7 dB | same test | `EXPECT_LE(abs(bt-bq), mask)` with mask {6,6,5,3,3,3}; "worst band deviation 1.48 dB" | as row 77 | 2026-09-30 | **DRIFT: test says 1.48** (doc Stage 2 table also 1.48) |
| 79 | Clause | +2/−5 dB, half-mask | none — `docs/itu-compliance.md` `ITU_ComfortNoiseLevel/Spectrum` | none | n/a | 2026-07-16 | descriptive |
| 80 | Noise pumping | 3.3 dB | same test | `EXPECT_LE(vmax - vmin, 5.0)`; "pumping 3.4 dB" | as row 77 | 2026-09-30 | **DRIFT: test says 3.4** (doc Stage 2 table 3.4) |
| 81 | Near-end build-up at DT onset | 20.9 ms | `tests/test_postfilter.cpp :: ItuChain.NearEndBuildUpTime` | `EXPECT_LE(t_hit_ms, 25.0)`; "reaches within 3 dB … in 15.8 ms (measured)" | as row 77 | 2026-09-30 | **DRIFT: test says 15.8 ms** (doc Stage 2 table 15.8) |

## Status — Tier A ITU suite (README lines 231–246)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 82 | Both required rates | 48 and 16 kHz | every `itu_*`/`g168_adapted` test loops `required_rates()` (`tests/support/itu_chain.h`) | structural | n/a | — | descriptive |
| 83 | TCL vs the 46 dB clause | 68 / 81 dB | `tests/test_itu_echo.cpp :: itu_echo/{0,1}.Tcl` | `EXPECT_GE(tcl, {62.0 @48k, 72.0 @16k})`; "Measured 68.4 dB at 48 kHz, 80.6 at 16 kHz" | not stated | 2026-07-20 | gate |
| 84 | Spectral mask clearance | +13 dB | `tests/test_itu_echo.cpp :: itu_echo.EchoSpectral` | `EXPECT_GE(atten, mask + 6.0)` per band; "worst margins: +13.2 dB (48 kHz), +22.3 dB (16 kHz)" | not stated | 2026-07-16 | gate; see row 109 for the +13.3 vs +13.2 discrepancy |
| 85 | Convergence rows | (no number here) | `itu_echo.ConvergenceQuiet`, `itu_echo.ConvergenceNoise` | see rows 110–111 | — | — | — |
| 86 | P.340 Cat. 1 echo loss during double talk, every band | ≥ 37 dB | `tests/test_itu_doubletalk.cpp :: itu_doubletalk.EchoLossDuringDoubleTalkPerBand` | `EXPECT_GE(worst, 33.0)` both rates; **no measured value in the test comment** — 37.5 / 37.9 come from `docs/itu-compliance.md` Stage 3 table | not stated | 2026-07-20 | gate; measured value is doc-only |
| 87 | Near-end cost during double talk | ≤ 1 dB | `tests/test_itu_doubletalk.cpp :: itu_doubletalk.SendAttenuationDuringDoubleTalk` | `EXPECT_LE(integ, 1.5)`; worst band `EXPECT_LE(worst, {2.0, 2.5})`; measured 0.91 / −0.30 integrated and 1.61 / 2.02 worst band per the doc table only | not stated | 2026-07-20 | gate; measured value is doc-only; worst band misses the 1.5 target at both rates (T) |
| 88 | Annex E stability at 0 dB far-end ERL | 0 dB | `tests/test_itu_dynamics.cpp :: itu_dynamics.ClosedLoopStabilitySweep` | `EXPECT_FALSE(howl)` at every ERL 50 → 0 in 5 dB steps; "measured stable at both rates" | not stated | 2026-07-16 | gate |
| 89 | Half-margin targets missed at 16 kHz | "a handful" | `docs/itu-compliance.md` Stage 3 table `(T)` marks: EchoStability 4.02, ConvergenceQuiet 34.1/45.4, TimeVariantPath −55.6, DtSentSpeech 2.02, Type1Transfer 1.52, Hangover 18.9, NoisePump 9.6 | each is a measured-value gate in its owning test (e.g. `TimeVariantPath` `< -54`, `Hangover` `≥ 15`) | not stated | 2026-07-16 | doc table + per-row gates |

## Status — Tier B G.168-adapted battery (README lines 247–266)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 90 | All twelve rows pass at both rates | twelve | `tests/test_g168.cpp` — 14 `g168_adapted` typed tests covering the 12 matrix IDs in `docs/itu-compliance.md` | structural | n/a | 2026-07-16 | gate (count consistent: G168_Convergence spans 3 tests) |
| 91 | Convergence masks with early loss | 20+ dB | `tests/test_g168.cpp :: g168_adapted.ConvergenceNlpOn` | `EXPECT_GE(l - max[0,50ms], 20.0)`, `EXPECT_GE(l - max[50ms,1s], 20.0)`; "loss[0,50ms] 22.6 dB, loss[50ms,1s] 22.3" | not stated | 2026-07-16 | gate |
| 92 | Double-talk divergence bounded inside the limit | 24 dB inside | `tests/test_g168.cpp :: g168_adapted.DoubleTalkDivergenceBounded` | `EXPECT_LE(max[1,2s], fig11(-16) + 5.0)` (= −38.3); "−62.6 / −67.0 vs bound −38.3" | not stated | 2026-07-16 | gate |
| 93 | Leak rate improves over 45 s of silence | improves, 45 s | `tests/test_g168.cpp :: g168_adapted.LeakRate` | `EXPECT_LE(after, before + 5.0)`; "it IMPROVES (−82.5 → −84.8 / −96.5 → −97.4)" | not stated | 2026-07-16 | gate (bounds degradation; "improves" is the measured sign) |
| 94 | Tone stability cancelled to the numerical floor | numerical floor | `tests/test_g168.cpp :: g168_adapted.ToneStability` | `EXPECT_LE(max[10,30s], 0.83·(−16) − 30 − 6)` (= −49.28); "−245 (48 kHz) / −104 dBm0" | not stated | 2026-07-16 | gate |
| 95 | Comfort noise tracking steps | within ±1.8 dB | `tests/test_g168.cpp :: g168_adapted.ComfortNoiseTracksBackground` | `EXPECT_LE(abs(step), 2.0)`; "measured −1.24 / −1.72"; ramp `≤ 6.0` ("−0.19 / −0.02") | not stated | 2026-07-16 | gate; README rounds −1.72 up to ±1.8 |
| 96 | Re-convergence rescue: combined loss in [1,2] s after the swap | 46 / 49 dB | `tests/test_g168.cpp :: g168_adapted.ReConvergenceAfterPathChange` | `EXPECT_GE(l - max[1,2s], {42.0, 45.0})`; "46.1 / 49.2 dB" | not stated | 2026-07-20 | gate |
| 97 | Deep steadies after the swap | −96 / −123 dBm0 | same test | `EXPECT_LE(max[8,10.5s], {-90.0, -115.0})`; "−95.9 / −123.3 dBm0" | not stated | 2026-07-20 | gate |
| 98 | Every other battery row bit-identical with the rescue | bit-identical | none — `docs/itu-compliance.md` "Every other battery row measured bit-identical" | none (the rows' own gates hold; identity is not asserted) | not stated | 2026-07-17 | doc; no gate on identity |
| 99 | Shadow comparator geometry/cost | 2 of 8 partitions, ~25% | none — `docs/itu-compliance.md` "The SHADOW COMPARATOR" | none | n/a | 2026-07-17 | descriptive |
| 100 | Shadow trigger | out-cancels by 3 dB sustained 0.3 s | none — `docs/itu-compliance.md` | none | n/a | 2026-07-17 | descriptive (config in `postfilter.h`, not read here) |
| 101 | Zero false fires across the DT/noise batteries | zero | none — `docs/itu-compliance.md` "measured as zero false fires"; not asserted in `test_g168.cpp` or `test_itu_*.cpp` (grepped) | none | not stated | 2026-07-17 | **no gate**; finding |
| 102 | Louder-direction recovery (path swings / three-phase) | (no README number) | `g168_adapted.PathSwings` `≤ {-34, -52}` ("−36.9 / −57.5"); `g168_adapted.AcousticThreePhaseScenario` phase gates | — | not stated | 2026-07-20 | listed because README names the rows |

## Status — proof notebook and learned suppressor (README lines 267–295)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 103 | Notebook re-measures the battery live | ~6 minutes | none — `docs/itu-compliance.md` Stage 4; `tools/notebook/build_itu_compliance.py` line 8 | none | not stated | — | descriptive |
| 104 | GRU parameter count | 52k | none — `tools/ml/README.md` "52,570 parameters" (v2, 48 kHz) | none (`tools/ml/test_parity.py` pins parity, not the count) | n/a | 2026-08-08 | descriptive |
| 105 | Learned chain single-talk ERLE vs classical | 53.4 vs 34.5 dB | none — `tools/ml/README.md` measured table (speech, 3 scenarios, medians, through the C ABI chain); `tests/test_nn_suppressor.cpp` has no ERLE assertion | none | not stated | 2026-08-08 | **doc table, not a gate**; finding |

## ITU-T compliance table (README lines 327–338) and the float32 paragraph (352–363)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 106 | Terminal coupling loss | 68.4 dB, +22 dB | `tests/test_itu_echo.cpp :: itu_echo.Tcl` | `EXPECT_GE(tcl, 62.0)` @48k; "Measured 68.4 dB at 48 kHz" | not stated | 2026-07-20 | gate; doc table `docs/itu-compliance.md` Stage 3 agrees |
| 107 | Single-talk echo level | −76.4, +18 dB | `tests/test_itu_echo.cpp :: itu_echo.EchoLevel` | `EXPECT_LT(lvl, -70.0)` cabin @48k (studio −80; 16k −78/−90); "48 kHz −76.4 (cabin) / −85.7 (studio); 16 kHz −81.0 / −101.2" | not stated | 2026-07-20 | gate |
| 108 | Attenuation spectrum vs WB mask | mask +13.3 dB, +13 dB | `tests/test_itu_echo.cpp :: itu_echo.EchoSpectral` | `EXPECT_GE(atten, mask + 6.0)`; file header "+13.3 dB over mask", row comment "+13.2 dB (48 kHz)" | not stated | 2026-07-16 | **DRIFT (minor): row comment says +13.2**; file header and doc table say +13.3 |
| 109 | Convergence from cold start | 45.4 dB by 1.2 s, +5.4 dB | `tests/test_itu_echo.cpp :: itu_echo.ConvergenceQuiet` | `EXPECT_GE(erl.by(1.2), {46.0 @48k, 44.5 @16k})`; header "34.1 / 45.4 dB (16 kHz)", row comment "34.1 / 46.2 (16 kHz)" then "the 1200 ms read is 45.4" | not stated | 2026-07-20 | gate; the test comment is internally inconsistent (46.2 vs 45.4); README and doc table use 45.4; 600 ms gate `≥ 32` (33.6 / 34.1, target miss (T)) |
| 110 | Convergence in driving noise | at ref by 0.75 s (2×) | `tests/test_itu_echo.cpp :: itu_echo.ConvergenceNoise` | `EXPECT_LE(max[2.75,3.0], ref)`; "750–1000 ms −28.6 / −32.1 vs ref −24.5 / −25.5" | not stated | 2026-07-16 | gate |
| 111 | Double-talk send attenuation | 0.9 dB integrated, +2.1 dB | `tests/test_itu_doubletalk.cpp :: itu_doubletalk.SendAttenuationDuringDoubleTalk` | `EXPECT_LE(integ, 1.5)`; measured 0.91 (48k) / −0.30 (16k) in the doc table only | not stated | 2026-07-20 | gate; measured value doc-only (row 87); README's "worst rate" 0.9 is the 48 kHz figure |
| 112 | Double-talk echo loss, every band | 37.5 dB, +10 dB | `tests/test_itu_doubletalk.cpp :: itu_doubletalk.EchoLossDuringDoubleTalkPerBand` | `EXPECT_GE(worst, 33.0)`; measured 37.5 / 37.9 in the doc table only | not stated | 2026-07-20 | gate; measured value doc-only (row 86) |
| 113 | Comfort-noise level match | −2.15 dB, inside | `tests/test_itu_dynamics.cpp :: itu_dynamics.ComfortNoiseLevelAndSpectrum` | `EXPECT_LE(delta, 1.0)`, `EXPECT_GE(delta, -2.5)`; "Measured −1.31 dB (48 kHz) / −2.15 (16 kHz)" | not stated | 2026-07-20 (gate); doc value 2026-09-30 | gate |
| 114 | Closed-loop stability | stable at 0 dB far-end ERL | `tests/test_itu_dynamics.cpp :: itu_dynamics.ClosedLoopStabilitySweep` | `EXPECT_FALSE(howl)` at each step incl. 0 dB | not stated | 2026-07-16 | gate |
| 115 | Algorithmic delay | 10.7 / 32 ms vs 70 ms budget | `tests/test_itu_dynamics.cpp :: itu_dynamics.AlgorithmicDelayWithinBudget` | `EXPECT_LE(2·1000·block/fs, 35.0)`; computed 10.67 ms @48k (block 256), 32 ms @16k | n/a (geometry) | 2026-07-16 | gate (arithmetic) |
| 116 | Every row typed over <float, double>; float32 clears every gate | both precisions | all `itu_*`, `g168_adapted`, `itu_g167` suites are `TYPED_TEST` with per-precision `prec_gate` columns | float column = double column except `EchoStability` (float 4.0 / 5.0 vs double 3.0 / 4.5) | Linux GCC, Release (srdif bump, `docs/itu-compliance.md`) | 2026-07-20 | gate; the one loosened float gate is documented in `docs/itu-compliance.md` and the test comment |
| 117 | G.168 §7 tone row: float preset enables the narrowband guard, double does not | guard on/off | `tests/test_float32.cpp :: Float32Parity.NarrowbandGuardPolicy` | `EXPECT_GT(f.narrowband_guard, 0)`, `EXPECT_EQ(d48/d16.narrowband_guard, 0)`; guard engages on a 5 s tone (`EXPECT_TRUE(narrowband_frozen())`) | n/a | 2026-07-17² | gate |
| 118 | On-target float-vs-double deltas | 0.1–0.7 dB | none on the delta — `docs/itu-compliance.md` line 790; `tests/test_float32.cpp` header lists the pairs (e.g. −75.1 vs −74.4) and `Float32Parity.SingleTalkResidual` / `.Convergence` / `.DoubleTalkTransparency` / `.NoLeakOverSilence` gate the float absolutes (`< -70/-79/-75/-90`; `≥ 30 / ≥ 40`; `< 3.0`; `≤ before + 5`) | none on the delta itself | not stated ("s9 scratch series") | 2026-07-17 | **delta not gated** (absolutes are); finding |
| 119 | Tone row on target at 16 kHz | −64.8 dBm0(A) vs −49.3 | `tests/test_float32.cpp :: Float32Parity.ToneRowWithNarrowbandGuard` | `EXPECT_LT(max[10 s, end], -55.0)` @16k (−100 @48k); "measured −64.8 (requirement −49.3)"; 48k "measured −146.6" | not stated | 2026-07-17 | gate |

## Status — the NaN watchdog (README's watchdog bullet, M0c)

| # | Claim (short) | README number | Gating test (file :: suite.test) | Threshold asserted in the test | Host the number was measured on | Date | Note |
|---|---|---|---|---|---|---|---|
| 120 | A non-finite block recovers within one block, counter at 1, every stage | one block; 1 | `tests/test_watchdog.cpp :: watchdog_test/{0,1}.NlmsCoreTripsAndRecovers`, `.KalmanCoreTripsAndRecovers`, `.KalmanCoreTripsWhileTheNarrowbandGuardHolds`, `.PemAfcOnNlmsTripsAndRecovers`, `.PemAfcOnKalmanTripsAndRecovers`, `.SuppressorTripsAndRecovers`, `.AecChainTripsAndRecovers`, `.GuardTripsToArmingAndDeclaresAgain` | `EXPECT_EQ(watchdog_trips(), 1u)` after the fault; the faulted block `== 0` per sample; the next three blocks `EXPECT_EQ` per sample against a freshly constructed stage; the guard `state == arming` then declares again | n/a (exact; every leg, both precisions on the host, float on target) | 2026-10-09 | gate |
| 121 | No arithmetic touched on finite input | every fingerprint line held on all nine CI legs | `tests/fingerprint_harness.cpp` against the nine `tests/fingerprints/<leg>.txt` (unchanged by M0) | 14 lines per leg, bit-exact | the nine CI legs of the M0 PR's run; macOS 15 x86_64 AppleClang locally (14 of 14 identical before/after) | 2026-10-09 | gate (CI) |
| 122 | The watchdog's instruction-count cost per stage | recorded in `include/mutap/watchdog.h` | `bench/icount` scenarios `fdkf_48k`, `suppressor_48k`, `chain_48k`, `shadow_48k` (and the 16 kHz twins) against `bench/baselines.json` | the ratchet gates ±3 %; the "≤ 0.1 % of the stage" claim is read from the PR run's printed counts against the committed baselines (no local rig for any of the three targets) | m55 / m33 / hexagon under QEMU, the `icount-ratchet` job | 2026-10-09 | doc (ratchet counts); `pem_afc` and the guard have no icount scenario until M2a |

## Summary

- 122 rows (119 compiled against README at `393c666`; rows 120–122 added for the watchdog bullet M0 added, and rows 40, 42, 43 rewritten after M0a corrected the counts). **Gated** by a test assertion (a `tests/…` suite.test with a bound the number sits inside, incl. the structural rows 82, 90, 116 and the weak `selected < 30` check of row 40): 83 — rows 3–5, 8–24, 28, 29, 31, 34, 37, 40, 44, 45, 47, 48, 50–52, 54, 56, 58–68, 70, 71, 73, 75, 77, 78, 80–84, 86–97, 102, 106–117, 119. **Doc table / fixture or config constant**, nothing asserts the number: 23 — rows 1, 2, 6, 7, 27, 32, 38, 39, 41, 42, 43, 49, 57, 72, 74, 76, 79, 85, 98, 99, 100, 103, 104. **Nothing** — a behavioural number that lives only in README, a doc sentence or a code comment, with no assertion anywhere (findings): 13 — rows 25, 26, 30, 33, 35, 36, 46, 53, 55, 69, 101, 105, 118 (plus the untraceable "~8 min" inside row 42).
- Measured values quoted by README but present only in `docs/itu-compliance.md`, not beside the assertion: rows 86, 87, 111, 112 (the double-talk battery's test comments carry requirement/target but no measured value).
- DRIFT rows still open: 16 (white ASG +10.26 vs assertion message +9.68), 78 (≤ 1.7 dB vs 1.48), 80 (3.3 vs 3.4 dB), 81 (20.9 vs 15.8 ms), 108 (+13.3 vs +13.2, minor). Row 109 is an internal inconsistency inside the test comment (46.2 vs 45.4), not README drift. Resolved by M0a: rows 40, 42, 43 (the test counts: 63 → 128 on target, 74 → 439 on the host). The open rows are left for the re-measurement that settles which number is right; M6d's generated table and its README diff check close the class.

¹ Host: `tests/support/portable_random.h` states that every comment re-measured when the portable variates landed (tap/MuTap#66, 2026-09-30) was measured on "Linux x86-64, GCC 13.3, Release, DspTap 0c5bf59"; rows whose comment fragment dates to that commit carry it. Earlier comments name no host (the same header notes most pre-#66 numbers came from a macOS host on the libc++ scenario, which is why they were re-measured).

² Dated by `git log --format=%ad --date=short -- <file> | tail -1` (first commit of the file) or by `-S` on the README fragment, because the test comment has no distinctive fragment of its own: row 40 README fragment "63 tests" last changed by #66 (2026-09-30), first introduced 2026-07-15; rows 42–43 first introduced in README 2026-07-15 (`e318edd`); row 56 and 117 by first commit of the file.
