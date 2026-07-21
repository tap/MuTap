/// @file postfilter.h
/// @brief Residual-echo suppressor + comfort noise (the AEC post-filter),
///        and aec_chain — the linear canceller and the post-filter composed
///        into the unit the ITU compliance matrix measures.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "mutap/fd_kalman.h"
#include "mutap/fft.h"
#include "mutap/pem_afc.h"

// The suppressor's pass-1 per-bin estimator exists in two bit-identical
// shapes (proven by the itu_dump cmp; see docs/optimization.md). On Arm
// Helium the GCC autovectorizer bails on the branchy form's control flow, so
// a branch-free form (data-dependent branches lowered to selects) vectorizes
// and measures ~2.9% cheaper on the M55 suppressor. On scalar targets
// (Hexagon HVX, desktop) the selects do unconditional work the vectorizer
// cannot hoist, so the branchy form is faster (~1.2% on Hexagon) and is the
// reference the compliance battery certified. Default to the branch-free form
// only where MVE is present; override (e.g. -DMUTAP_SUPPRESSOR_BRANCHLESS=1 on
// desktop) to compile the other path for the bit-identity check.
#if !defined(MUTAP_SUPPRESSOR_BRANCHLESS)
#if defined(__ARM_FEATURE_MVE)
#define MUTAP_SUPPRESSOR_BRANCHLESS 1
#else
#define MUTAP_SUPPRESSOR_BRANCHLESS 0
#endif
#endif

namespace tap::mu {

    /// Residual-echo suppressor with matched comfort noise
    /// (docs/itu-compliance.md, Stage 2).
    ///
    /// Linear cancellation measures ~20-25 dB of echo suppression; the
    /// single-talk clauses of the automotive recs want a residual below
    /// -58 dBm0(A) — every compliant product closes that gap with a
    /// post-filter, and the double-talk clauses bound what it may cost:
    /// no more than 3 dB of near-end attenuation (our margin target:
    /// 1.5 dB). Both sides fall out of one rule:
    ///
    ///   coh(k)  = |S_dy(k)|^2 / (S_dd(k) S_yy(k))   MIC-vs-Yhat coherence
    ///   lam(k) <- S_ee(k) / S_yy(k)   learned ONLY while coh(k) > gate
    ///   G(k)    = max(g_min, 1 - beta lam(k) S_yy(k) / S_ee(k))
    ///
    /// where Yhat is the linear canceller's own echo-estimate spectrum
    /// and D = E + Yhat reconstructs the microphone. Two estimators
    /// split the two jobs a residual suppressor has:
    ///
    ///  - coh(D, Yhat) answers "is this bin echo right now?". The
    ///    discriminator deliberately correlates the MIC — not the
    ///    canceller output E — against Yhat: a converged adaptive
    ///    filter keeps E orthogonal to its reference by construction,
    ///    and its weight jitter makes the tiny residual's phase wander,
    ///    so coh(E, Yhat) saturates well below 1 (measured 0.34
    ///    adapting / 0.53 frozen — the freeze experiment that located
    ///    this). The mic instead carries the FULL echo H x, so per bin
    ///    D/Yhat ~ H/Hhat ~ 1: insensitive to adaptation jitter and to
    ///    the convolution-tail (frame-multiplicative) error, since both
    ///    signals traverse nearly the same filter.
    ///
    ///  - lam(k), the LEAKAGE (residual power per unit estimated-echo
    ///    power ~ the canceller's per-bin misalignment), answers "how
    ///    much of E does echo explain?". It is learned only while the
    ///    coherence certifies the bin echo-dominant, and held through
    ///    double talk. The applied gain is the Wiener gain ON E: during
    ///    single talk S_ee ~ lam S_yy so the gain floors; during double
    ///    talk near-end energy inflates S_ee while lam S_yy holds, so
    ///    the gain rides to 1 — even in bins where the analysis
    ///    resolution cannot separate the near-end line from an echo
    ///    line 20 Hz away (P.501's AM-FM plans interleave that tightly
    ///    at the low end; a gain keyed to coherence alone measurably
    ///    took 5.5 dB off the near end there, hitting both signals
    ///    1-for-1). Transparency is intrinsic: not a detector decision
    ///    but the shape of the rule.
    ///
    /// Suppressed bins are refilled with COMFORT NOISE shaped to the
    /// tracked near-end noise floor N(k) (two-window minimum
    /// statistics): out = G E + sqrt(max(0, N - G^2 |E|^2)) . unit-noise,
    /// so the output sits at the true noise floor instead of pumping
    /// between "noise" and "digital silence" (P.111x comfort-noise
    /// clauses: level within +2/-5 dB, spectrum within mask).
    ///
    /// Framing matches the canceller: overlap-save on [previous block,
    /// current block], per-bin gains, last half kept — zero added
    /// latency beyond the canceller's own block. Gains are time-smoothed
    /// (fast attack toward suppression, slower release) and floor-capped
    /// to bound musical noise.
    ///
    /// Real-time contract: constructor allocates and may throw; every
    /// post-construction entry point is noexcept and allocation-free
    /// (the comfort-noise source is a xorshift PRNG, not <random>).
    template <typename Sample>
    class residual_suppressor {
      public:
        struct config {
            size_t block_size = 256; ///< must match the canceller's block size
            /// Analysis window = this many blocks (power of 2). The
            /// suppressor's frequency resolution is what separates echo
            /// from near-end structure — at block 256 / 48 kHz one block
            /// is 93.75 Hz per bin, too coarse for the AM-FM double-talk
            /// combs (~90-160 Hz interleave); 8 blocks (23.4 Hz bins,
            /// Hann-windowed estimation) resolves them.
            size_t analysis_blocks    = 8;
            Sample max_suppression_db = 40;          ///< g_min = -this in dB (floor per bin)
            Sample over_subtraction   = Sample(1.2); ///< beta on the coherence (>= 1)
            /// One-pole smoothing of the coherence/power accumulators,
            /// [0, 1) (~100 ms at block 256 / 48 kHz).
            Sample leakage_smoothing = Sample(0.95);
            /// Coherence above which a bin is certified echo-dominant
            /// and its leakage may be re-learned, [0, 1).
            Sample leakage_gate = Sample(0.9);
            /// Per-bin gain smoothing toward MORE suppression (attack)
            /// and toward LESS (release), [0, 1). Attack is the SLOW one
            /// (~265 ms at block 256 / 48 kHz): estimator variance at
            /// bins the analysis cannot fully resolve makes an
            /// instantaneous attack chatter, and the duty-cycle loss
            /// measured 3 dB off the 250 Hz near-end comb line during
            /// double talk (1.6 dB integrated; 1.0 dB at 0.98). Echo
            /// containment doesn't need the postfilter to be fast — the
            /// canceller's Kalman gain is the fast defense. Release is
            /// the near-end onset path — the switching clauses' build-up
            /// budget (T_r <= 25 ms target): 0.9 measured 26.0 ms to
            /// within 3 dB of the settled double-talk send level, 0.8
            /// measures 15.7 ms and slower release saturates there.
            Sample gain_attack  = Sample(0.98);
            Sample gain_release = Sample(0.85);
            /// Release SNAP: when the target gain exceeds the smoothed
            /// gain by more than this factor, the smoothing is bypassed
            /// and the gain jumps in one block (0 disables). Release-
            /// direction only, so double-talk transparency and echo
            /// containment are untouched — an echo burst raises |Yhat|^2
            /// with |E|^2 and never produces a high target gain. What it
            /// buys (measured, Stage 3): gains can START at the floor
            /// (see reset()) so activation/convergence-phase echo is
            /// suppressed from block one — the P.1110 convergence-in-
            /// noise mask (echo <= BGN+10 dB from 100 ms) and G.168's
            /// combined-loss-at-t0 clauses are unmeetable without it —
            /// while a genuine near-end onset still opens the path in a
            /// single block.
            Sample release_snap = Sample(4);
            /// LOW-BAND SUPPRESSION CAP: analysis bins below this index
            /// whose coherence does NOT certify them echo-dominant never
            /// suppress more than low_band_cap_db (0 bins = off, the
            /// default). Rationale (Stage 3, measured): below ~300 Hz
            /// the P.501 double-talk material and its echo overlap inside
            /// any realizable analysis resolution, so suppression there
            /// takes near-end voice fundamentals 1-for-1 — P.340's
            /// transfer-function constancy bound (+-3 dB) measured a
            /// 3.5 dB violation at the 200 Hz band. The cap is GATED on
            /// the same coherence that gates the leakage learner: an
            /// echo-certified bin (single talk, or an echo-only comb
            /// line) still suppresses fully — an unconditional cap
            /// measurably cost 12 dB of unweighted early convergence,
            /// speech-shaped CSS being low-frequency-heavy. The
            /// compliance chain preset caps bins below 300 Hz at 1.5 dB;
            /// the library default leaves the cap off. Certification is
            /// SUSTAINED: a bin uncaps only after this many consecutive
            /// echo-dominant blocks with real echo-estimate power —
            /// frozen (not reset) while the far end is silent, reset by
            /// active contamination. Instantaneous certification leaked
            /// through the near end's own pauses inside double talk
            /// (P.501 DT CSS pauses 127 ms every 400 ms) and the P.340
            /// transfer bound failed again (measured 3.21 dB).
            size_t low_band_bins           = 0;
            Sample low_band_cap_db         = Sample(1.5);
            size_t low_band_certify_blocks = 56;
            /// Noise-floor tracker (two-window minimum statistics on
            /// smoothed |E|^2): power smoothing [0, 1), window length in
            /// blocks (floor reacts within 1-2 windows; speech bursts
            /// shorter than a window cannot drag it up), and the bias
            /// factor compensating the minimum's undershoot of the mean
            /// (calibrated against the P.111x comfort-noise level
            /// clause; see the measured table in test_postfilter.cpp).
            Sample floor_smoothing = Sample(0.9);
            size_t floor_window    = 128;
            Sample floor_bias      = Sample(4);
            bool   comfort_noise   = true; ///< fill suppressed bins to the noise floor
        };

        explicit residual_suppressor(const config& cfg)
            : m_cfg(validated(cfg))
            , m_n(cfg.analysis_blocks * cfg.block_size)
            , m_fft(m_n)
            , m_g_min(std::pow(Sample(10), -cfg.max_suppression_db / Sample(20)))
            , m_g_low(std::pow(Sample(10), -cfg.low_band_cap_db / Sample(20)))
            , m_input(m_n)
            , m_ywin(m_n)
            , m_yspec(m_n)
            , m_dspec(m_n)
            , m_window(m_n)
            , m_spec(m_n)
            , m_time(m_n)
            , m_sdy_re(m_n / 2 + 1)
            , m_sdy_im(m_n / 2 + 1)
            , m_sdd(m_n / 2 + 1)
            , m_syy(m_n / 2 + 1)
            , m_see(m_n / 2 + 1)
            , m_leak(m_n / 2 + 1)
            , m_gain(m_n / 2 + 1)
            , m_low_cnt(m_n / 2 + 1)
            , m_psm(m_n / 2 + 1)
            , m_min_cur(m_n / 2 + 1)
            , m_min_prev(m_n / 2 + 1)
            , m_gspec(m_n)
            , m_gtime(m_n) {
            reset();
        }

        size_t block_size() const noexcept { return m_cfg.block_size; }
        size_t analysis_bins() const noexcept { return m_n / 2 + 1; }
        /// Introspection for tests/diagnostics: the smoothed per-bin gain
        /// and tracked leakage (analysis-bin index).
        Sample gain_at(size_t k) const noexcept { return m_gain[k]; }
        /// The mic-vs-echo-estimate coherence gating the leakage learner.
        Sample coherence_at(size_t k) const noexcept {
            const Sample c =
                (m_sdy_re[k] * m_sdy_re[k] + m_sdy_im[k] * m_sdy_im[k]) / (m_sdd[k] * m_syy[k] + Sample(1e-20));
            return c > Sample(1) ? Sample(1) : c;
        }
        /// The learned per-bin leakage lam (the canceller's effective
        /// per-bin misalignment; 1 until first certified echo-dominant).
        Sample leakage_at(size_t k) const noexcept { return m_leak[k]; }
        /// Broadband fraction of smoothed mic power the echo estimate
        /// explains (0 with an unconverged canceller, -> ~1 in echo-
        /// dominant single talk). aec_chain's initial receive guard
        /// certifies convergence on this.
        Sample echo_explained() const noexcept { return m_echo_explained; }

        void reset() noexcept {
            for (size_t i = 0; i < m_n; ++i) { // Hann for the ESTIMATION ffts
                m_window[i] = Sample(0.5)
                              - Sample(0.5)
                                    * std::cos(Sample(2) * static_cast<Sample>(std::numbers::pi)
                                               * static_cast<Sample>(i) / static_cast<Sample>(m_n - 1));
            }
            for (auto& v : m_input) {
                v = Sample(0);
            }
            for (auto& v : m_ywin) {
                v = Sample(0);
            }
            for (auto& v : m_sdy_re) {
                v = Sample(0);
            }
            for (auto& v : m_sdy_im) {
                v = Sample(0);
            }
            for (auto& v : m_sdd) {
                v = Sample(0);
            }
            for (auto& v : m_see) {
                v = Sample(0);
            }
            for (auto& v : m_leak) {
                v = Sample(1); // pessimistic: unconverged canceller leaks everything
            }
            for (auto& v : m_syy) {
                v = Sample(0);
            }
            // Gains START at the floor: until the estimators have seen
            // evidence, the safe state is "suppress" — a fresh (or
            // reset) canceller passes raw echo, and the convergence
            // masks begin at t = 0. A genuine near-end onset is rescued
            // by the release snap within one block.
            for (auto& v : m_gain) {
                v = m_g_min;
            }
            for (auto& v : m_psm) {
                v = Sample(0);
            }
            for (auto& v : m_low_cnt) {
                v = 0;
            }
            // The PREVIOUS-window minimum starts at 0: the floor — and
            // with it the comfort fill — stays OFF until one full window
            // of real signal has been observed.
            for (auto& v : m_min_cur) {
                v = std::numeric_limits<Sample>::infinity();
            }
            for (auto& v : m_min_prev) {
                v = Sample(0);
            }
            m_min_count = 0;
            m_rng       = 0x2545F491U;
        }

        /// Process one block: e is the linear canceller's output,
        /// yhat_block the echo estimate for the SAME block
        /// (pem_afc::echo_estimate_block(), block_size time samples —
        /// windowed HERE at analysis resolution: the canceller's own
        /// 2*block bins cannot resolve the AM-FM double-talk combs, and
        /// a coarse reference bleeds echo credit onto near-end bins),
        /// out receives the suppressed block. e and out may alias. The
        /// constrained gain filter is linear-phase with block_size
        /// samples of delay — the postfilter's only added latency.
        void process_block(const Sample* e, const Sample* yhat_block, Sample* out) noexcept {
            const size_t b    = m_cfg.block_size;
            const size_t bins = m_n / 2 + 1;
            const Sample eps  = Sample(1e-20);

            // Slide both analysis windows by one block (memmove: the
            // same element moves as the loop it replaces — bit-identical,
            // measurably cheaper on every target).
            std::memmove(m_input.data(), m_input.data() + b, (m_n - b) * sizeof(Sample));
            std::memmove(m_ywin.data(), m_ywin.data() + b, (m_n - b) * sizeof(Sample));
            std::memcpy(m_input.data() + (m_n - b), e, b * sizeof(Sample));
            std::memcpy(m_ywin.data() + (m_n - b), yhat_block, b * sizeof(Sample));
            // Signal-path FFT stays RECTANGULAR (exact overlap-save
            // filtering); the ESTIMATION FFTs are Hann-windowed —
            // rectangular sidelobes (-13 dB) bleed echo energy into
            // near-end bins and were a measured double-talk
            // transparency failure.
            std::memcpy(m_spec.data(), m_input.data(), m_n * sizeof(Sample));
            for (size_t i = 0; i < m_n; ++i) {
                // d = e + yhat reconstructs the MICROPHONE frame — the
                // discriminator's left-hand signal (see class comment).
                m_dspec[i] = m_window[i] * (m_input[i] + m_ywin[i]);
                m_yspec[i] = m_window[i] * m_ywin[i];
            }
            m_fft.forward_inplace(m_spec.data());
            m_fft.forward_inplace(m_dspec.data());
            m_fft.forward_inplace(m_yspec.data());

#if MUTAP_SUPPRESSOR_BRANCHLESS
            // Pass 1 per analysis bin: gated power-domain leakage ->
            // Wiener gain -> time smoothing.
            // THE DISCRIMINATOR: magnitude-squared coherence between the
            // MIC (d = e + yhat) and the echo estimate, from smoothed
            // COMPLEX cross-spectra. In single talk D/Yhat ~ H/Hhat ~ 1
            // per bin — stable against the canceller's weight jitter — so
            // coh -> 1 and the gain collapses. Near-end energy is
            // incoherent with Yhat and DILUTES coh: g = 1 - coh is the
            // Wiener near-end-preserving gain, so double-talk transparency
            // is intrinsic, not a detector decision. (Three rejected
            // designs are in git history: power-leakage ratios hug their
            // noise floor or re-learn near-end energy during sustained
            // double talk; rules keyed to INSTANTANEOUS |E|^2 excursions
            // read loud residual as near-end and pass exactly the blocks
            // that matter; and coh(E, Yhat) saturates at 0.34 while
            // adapting — the update keeps E orthogonal to its reference,
            // so the canceller output is the one signal the echo estimate
            // cannot explain.)
            //
            // The per-bin body is written BRANCH FREE so the target
            // compilers vectorize it: the profile showed both the GCC/MVE
            // (M55) and LLVM/HVX (Hexagon) autovectorizers bail on
            // "control flow in loop" here. The two data-dependent branches
            // — the no-echo coherence zero and the coherence-gated leakage
            // re-learn — become selects whose kept values are identical to
            // the original if/else, so the battery stays bit-identical.
            Sample       sum_syy = Sample(0);
            Sample       sum_sdd = Sample(0);
            const Sample a_r     = m_cfg.leakage_smoothing;
            const Sample one_ar  = Sample(1) - a_r;
            const Sample gate    = m_cfg.leakage_gate;
            const Sample beta    = m_cfg.over_subtraction;
            const bool   snap_on = m_cfg.release_snap > Sample(0);

            struct bin_result {
                Sample g;
                Sample coh;
            };
            // Estimator update + Wiener gain for one bin. Returns the
            // target gain and the coherence (the low-band cap needs it).
            const auto wiener_gain = [&](size_t k) noexcept -> bin_result {
                Sample d_re, d_im, y_re, y_im;
                packed_bin(m_dspec.data(), k, m_n, d_re, d_im);
                packed_bin(m_yspec.data(), k, m_n, y_re, y_im);
                const Sample pd   = d_re * d_re + d_im * d_im + eps;
                const Sample py   = y_re * y_re + y_im * y_im + eps;
                const Sample e_re = d_re - y_re; // E = D - Yhat, windowed transform is linear
                const Sample e_im = d_im - y_im;
                const Sample pe   = e_re * e_re + e_im * e_im + eps;
                sum_syy += py;
                sum_sdd += pd;
                m_sdy_re[k] += one_ar * ((d_re * y_re + d_im * y_im) - m_sdy_re[k]);
                m_sdy_im[k] += one_ar * ((d_im * y_re - d_re * y_im) - m_sdy_im[k]);
                m_sdd[k] += one_ar * (pd - m_sdd[k]);
                m_syy[k] += one_ar * (py - m_syy[k]);
                m_see[k] += one_ar * (pe - m_see[k]);

                Sample coh = (m_sdy_re[k] * m_sdy_re[k] + m_sdy_im[k] * m_sdy_im[k]) / (m_sdd[k] * m_syy[k] + eps);
                coh        = coh > Sample(1) ? Sample(1) : coh;
                coh        = m_syy[k] <= eps * Sample(100) ? Sample(0) : coh; // no estimated echo

                // Leakage re-learn only while coh certifies the bin (a
                // masked update: the select keeps the old value otherwise;
                // lam is finite for all k, so computing it always is safe).
                const Sample lam       = m_see[k] / (m_syy[k] + eps);
                const Sample relearned = m_leak[k] + one_ar * (lam - m_leak[k]);
                m_leak[k]              = coh > gate ? relearned : m_leak[k];

                // Wiener gain ON E: the denominator takes the INSTANTANEOUS
                // |E|^2 when it exceeds the smoothed power, so a near-end
                // onset inside the smoother's lag rides through.
                const Sample se = m_see[k] > pe ? m_see[k] : pe;
                Sample       r  = m_leak[k] * m_syy[k] / (se + eps);
                r               = r > Sample(1) ? Sample(1) : r;
                Sample g        = Sample(1) - beta * r;
                g               = g < m_g_min ? m_g_min : (g > Sample(1) ? Sample(1) : g);
                return {g, coh};
            };

            // Commit the smoothed/snapped gain, branch free (the release
            // snap and the attack/release select yield the same value the
            // original if/else did — a_g/smoothed computed then discarded
            // on the snap path change nothing kept).
            const auto commit_gain = [&](size_t k, Sample g) noexcept {
                const Sample a_g      = g < m_gain[k] ? m_cfg.gain_attack : m_cfg.gain_release;
                const Sample smoothed = m_gain[k] + (Sample(1) - a_g) * (g - m_gain[k]);
                const bool   snap     = snap_on && g > m_cfg.release_snap * m_gain[k];
                m_gain[k]             = snap ? g : smoothed;
            };

            // Low-band cap prefix: the coherence-gated suppression cap and
            // its sustained-certification counter only touch bins below
            // low_band_bins (~300 Hz). Short prefix, and the integer
            // counter is genuinely conditional, so it keeps the exact
            // scalar logic and stays out of the vectorized main loop.
            const size_t lb = m_cfg.low_band_bins < bins ? m_cfg.low_band_bins : bins;
            for (size_t k = 0; k < lb; ++k) {
                auto [g, coh] = wiener_gain(k);
                if (m_syy[k] > eps * Sample(100)) { // real echo estimate present
                    if (coh > gate) {
                        if (m_low_cnt[k] < m_cfg.low_band_certify_blocks) {
                            ++m_low_cnt[k];
                        }
                    }
                    else {
                        m_low_cnt[k] = 0; // active contamination: back to capped
                    }
                }
                if (m_low_cnt[k] < m_cfg.low_band_certify_blocks && g < m_g_low) {
                    g = m_g_low; // uncertified low bin: protect the near end
                }
                commit_gain(k, g);
            }
            // Main band (k >= low_band_bins): no cap, fully branch free.
            for (size_t k = lb; k < bins; ++k) {
                commit_gain(k, wiener_gain(k).g);
            }
#else
            // Pass 1 per analysis bin: gated power-domain leakage ->
            // Wiener gain -> time smoothing.
            Sample sum_syy = Sample(0);
            Sample sum_sdd = Sample(0);
            for (size_t k = 0; k < bins; ++k) {
                Sample d_re;
                Sample d_im;
                Sample y_re;
                Sample y_im;
                packed_bin(m_dspec.data(), k, m_n, d_re, d_im);
                packed_bin(m_yspec.data(), k, m_n, y_re, y_im);

                const Sample pd = d_re * d_re + d_im * d_im + eps;
                const Sample py = y_re * y_re + y_im * y_im + eps;
                // E = D - Yhat by linearity of the windowed transform.
                const Sample e_re = d_re - y_re;
                const Sample e_im = d_im - y_im;
                const Sample pe   = e_re * e_re + e_im * e_im + eps;

                // THE DISCRIMINATOR: magnitude-squared coherence between
                // the MIC (d = e + yhat) and the echo estimate, from
                // smoothed COMPLEX cross-spectra. In single talk
                // D/Yhat ~ H/Hhat ~ 1 per bin — stable against the
                // canceller's weight jitter — so coh -> 1 and the gain
                // collapses. Near-end energy is incoherent with Yhat
                // and DILUTES coh: g = 1 - coh is the Wiener near-end-
                // preserving gain, so double-talk transparency is
                // intrinsic, not a detector decision. (Three rejected
                // designs are in git history: power-leakage ratios hug
                // their noise floor or re-learn near-end energy during
                // sustained double talk; rules keyed to INSTANTANEOUS
                // |E|^2 excursions read loud residual as near-end and
                // pass exactly the blocks that matter; and coh(E, Yhat)
                // saturates at 0.34 while adapting — the update keeps E
                // orthogonal to its reference, so the canceller output
                // is the one signal the echo estimate cannot explain.)
                sum_syy += py;
                sum_sdd += pd;
                const Sample a_r = m_cfg.leakage_smoothing;
                m_sdy_re[k] += (Sample(1) - a_r) * ((d_re * y_re + d_im * y_im) - m_sdy_re[k]);
                m_sdy_im[k] += (Sample(1) - a_r) * ((d_im * y_re - d_re * y_im) - m_sdy_im[k]);
                m_sdd[k] += (Sample(1) - a_r) * (pd - m_sdd[k]);
                m_syy[k] += (Sample(1) - a_r) * (py - m_syy[k]);
                m_see[k] += (Sample(1) - a_r) * (pe - m_see[k]);

                Sample coh = (m_sdy_re[k] * m_sdy_re[k] + m_sdy_im[k] * m_sdy_im[k]) / (m_sdd[k] * m_syy[k] + eps);
                coh        = coh > Sample(1) ? Sample(1) : coh;
                if (m_syy[k] <= eps * Sample(100)) {
                    coh = Sample(0); // no estimated echo: nothing to suppress
                }

                // Re-learn the leakage only while the coherence certifies
                // the bin echo-dominant; hold it through double talk.
                if (coh > m_cfg.leakage_gate) {
                    const Sample lam = m_see[k] / (m_syy[k] + eps);
                    m_leak[k] += (Sample(1) - a_r) * (lam - m_leak[k]);
                }

                // Wiener gain ON E, with the estimated residual PSD
                // lam S_yy in the numerator. The denominator takes the
                // INSTANTANEOUS |E|^2 when it exceeds the smoothed power
                // — a near-end onset inside the smoother's lag must ride
                // through, not get clipped by yesterday's S_ee.
                const Sample se = m_see[k] > pe ? m_see[k] : pe;
                Sample       r  = m_leak[k] * m_syy[k] / (se + eps);
                r               = r > Sample(1) ? Sample(1) : r;
                Sample g        = Sample(1) - m_cfg.over_subtraction * r;
                g               = g < m_g_min ? m_g_min : (g > Sample(1) ? Sample(1) : g);
                if (k < m_cfg.low_band_bins) {
                    if (m_syy[k] > eps * Sample(100)) { // real echo estimate present
                        if (coh > m_cfg.leakage_gate) {
                            if (m_low_cnt[k] < m_cfg.low_band_certify_blocks) {
                                ++m_low_cnt[k];
                            }
                        }
                        else {
                            m_low_cnt[k] = 0; // active contamination: back to capped
                        }
                    }
                    if (m_low_cnt[k] < m_cfg.low_band_certify_blocks && g < m_g_low) {
                        g = m_g_low; // uncertified low bin: protect the near end
                    }
                }
                if (m_cfg.release_snap > Sample(0) && g > m_cfg.release_snap * m_gain[k]) {
                    m_gain[k] = g; // release snap: overwhelming evidence, open now
                }
                else {
                    const Sample a_g = g < m_gain[k] ? m_cfg.gain_attack : m_cfg.gain_release;
                    m_gain[k] += (Sample(1) - a_g) * (g - m_gain[k]);
                }
            }
#endif

            m_echo_explained = sum_syy / (sum_sdd + eps);

            // Pass 2: CONSTRAIN the gain response to a causal block_size-
            // tap linear-phase filter (the postfilter's version of the
            // FDAF's gradient constraint). Unconstrained per-bin gains
            // act as a CIRCULAR filter on the frame and smear suppression
            // into bins that must stay transparent — measured: 4-8 dB of
            // near-end attenuation during double talk from this alone.
            // Constrained and causalized, the last block of the frame is
            // a true linear convolution; the price is the filter's
            // block_size/2-sample linear-phase delay.
            m_gspec[0] = m_gain[0];
            m_gspec[1] = m_gain[m_n / 2];
            for (size_t k = 1; k < m_n / 2; ++k) {
                m_gspec[2 * k]     = m_gain[k];
                m_gspec[2 * k + 1] = Sample(0);
            }
            m_fft.inverse(m_gspec.data(), m_gtime.data());
            // Causal window: taps [0, 2b) of h_c[tau] = g_ir[(tau - b) mod n]
            // — a 2*block-tap filter resolves gain structure down to
            // fs/(2*block) (needed for the double-talk combs; a
            // block-tap filter measurably smeared suppression onto the
            // near-end lines). Its linear phase is the postfilter's
            // latency: one block.
            std::fill(m_gspec.begin() + static_cast<long>(2 * b), m_gspec.end(), Sample(0));
            for (size_t tau = 0; tau < 2 * b; ++tau) {
                // Rectangular cut, deliberately: a Hann taper here was
                // measured WORSE for double-talk transparency (2.25 dB
                // near-end attenuation vs 1.60) — the taper's doubled
                // mainlobe smears deep echo notches onto near-end comb
                // lines more than the rectangular sidelobes do.
                m_gspec[tau] = m_gtime[(tau + m_n - b) % m_n];
            }
            m_fft.forward_inplace(m_gspec.data());

            // Pass 3: apply the constrained gains + comfort-noise fill.
            for (size_t k = 0; k < bins; ++k) {
                Sample e_re;
                Sample e_im;
                Sample g_re;
                Sample g_im;
                packed_bin(m_spec.data(), k, m_n, e_re, e_im);
                packed_bin(m_gspec.data(), k, m_n, g_re, g_im);

                Sample out_re = g_re * e_re - g_im * e_im;
                Sample out_im = g_re * e_im + g_im * e_re;

                // Near-end noise floor by TWO-WINDOW MINIMUM STATISTICS
                // on the smoothed PRE-gain power |E|^2: the minimum of
                // the last one-to-two windows of smoothed power is the
                // stationary floor — speech and echo bursts shorter than
                // a window cannot drag it up, and the floor is acquired
                // within one window of the first pause. (Two rejected
                // trackers, both measured: asymmetric fast-fall/slow-rise
                // one-poles either chase every downward fluctuation of
                // the exponentially-distributed bin powers — the raw-
                // minimum bias, -8 dB comfort-noise undershoot — or,
                // rise-limited, take tens of seconds to acquire the
                // floor at all.) The bias factor compensates what is
                // left of the minimum's undershoot of the mean. Fill
                // the gap between floor and suppressed power with
                // random-phase noise so suppression never gates below
                // the true near-end floor.
                const Sample p_in = e_re * e_re + e_im * e_im;
                m_psm[k] += (Sample(1) - m_cfg.floor_smoothing) * (p_in - m_psm[k]);
                if (m_psm[k] < m_min_cur[k]) {
                    m_min_cur[k] = m_psm[k];
                }
                // NOISE-FLOOR GAIN BOUND for the next block's gain rule:
                // suppression must stop AT the tracked near-end floor —
                // the Wiener rule otherwise PARTIALLY suppresses bins
                // where echo and loud noise share power, landing above
                // the per-bin floor where the comfort fill cannot
                // engage, and the transmitted noise pumps with the far
                // end's cadence (measured 20 dB in driving noise at
                // -30 dBm0(A); the fill-only design read 2.9 dB in
                // quiet Hoth and hid this).
                if (m_cfg.comfort_noise) {
                    const Sample fl = m_cfg.floor_bias * (m_min_cur[k] < m_min_prev[k] ? m_min_cur[k] : m_min_prev[k]);
                    const Sample po = out_re * out_re + out_im * out_im;
                    const Sample deficit = fl - po;
                    if (deficit > Sample(0)) {
                        const Sample amp = std::sqrt(deficit / Sample(2));
                        out_re += amp * next_noise();
                        out_im += amp * next_noise();
                    }
                }
                store_bin(m_spec.data(), k, m_n, out_re, out_im);
            }

            // Rotate the minimum-statistics windows.
            if (++m_min_count >= m_cfg.floor_window) {
                m_min_count = 0;
                for (size_t k = 0; k < bins; ++k) {
                    m_min_prev[k] = m_min_cur[k];
                    m_min_cur[k]  = std::numeric_limits<Sample>::infinity();
                }
            }

            m_fft.inverse(m_spec.data(), m_time.data());
            for (size_t i = 0; i < b; ++i) {
                out[i] = m_time[m_n - b + i];
            }
        }

      private:
        static config validated(const config& cfg) {
            if (cfg.block_size < 4 || (cfg.block_size & (cfg.block_size - 1)) != 0) {
                throw std::invalid_argument("residual_suppressor: block_size must be a power of 2 >= 4");
            }
            if (cfg.analysis_blocks < 4 || (cfg.analysis_blocks & (cfg.analysis_blocks - 1)) != 0) {
                throw std::invalid_argument("residual_suppressor: analysis_blocks must be a power of 2 >= 4");
            }
            if (cfg.max_suppression_db <= Sample(0)) {
                throw std::invalid_argument("residual_suppressor: max_suppression_db must be positive");
            }
            if (cfg.over_subtraction < Sample(1)) {
                throw std::invalid_argument("residual_suppressor: over_subtraction must be >= 1");
            }
            auto unit = [](Sample v) { return v >= Sample(0) && v < Sample(1); };
            if (!unit(cfg.leakage_smoothing) || !unit(cfg.gain_attack) || !unit(cfg.gain_release)
                || !unit(cfg.floor_smoothing)) {
                throw std::invalid_argument("residual_suppressor: smoothing constants must be in [0, 1)");
            }
            if (!unit(cfg.leakage_gate)) {
                throw std::invalid_argument("residual_suppressor: leakage_gate must be in [0, 1)");
            }
            if (cfg.floor_window == 0 || cfg.floor_bias <= Sample(0)) {
                throw std::invalid_argument("residual_suppressor: floor_window/floor_bias must be positive");
            }
            if (cfg.release_snap < Sample(0)) {
                throw std::invalid_argument("residual_suppressor: release_snap must be >= 0");
            }
            return cfg;
        }

        /// Packed-format bin access (Ooura layout: [DC, Nyquist, re, im, ...]).
        static void packed_bin(const Sample* s, size_t k, size_t n, Sample& re, Sample& im) noexcept {
            if (k == 0) {
                re = s[0];
                im = Sample(0);
            }
            else if (k == n / 2) {
                re = s[1];
                im = Sample(0);
            }
            else {
                re = s[2 * k];
                im = s[2 * k + 1];
            }
        }

        static void store_bin(Sample* s, size_t k, size_t n, Sample re, Sample im) noexcept {
            if (k == 0) {
                s[0] = re;
            }
            else if (k == n / 2) {
                s[1] = re;
            }
            else {
                s[2 * k]     = re;
                s[2 * k + 1] = im;
            }
        }

        /// Allocation-free noise source in [-1, 1] (xorshift32).
        Sample next_noise() noexcept {
            m_rng ^= m_rng << 13;
            m_rng ^= m_rng >> 17;
            m_rng ^= m_rng << 5;
            return Sample(2) * (static_cast<Sample>(m_rng) / Sample(4294967296.0)) - Sample(1);
        }

        config                 m_cfg;
        size_t                 m_n; ///< FFT size, 2 * block_size
        basic_real_fft<Sample> m_fft;
        Sample                 m_g_min;
        Sample                 m_g_low;
        std::vector<Sample>    m_input; ///< sliding analysis window of e
        std::vector<Sample>    m_ywin;  ///< sliding analysis window of the echo estimate
        std::vector<Sample>    m_yspec; ///< Hann-windowed estimation spectrum of the echo estimate
        std::vector<Sample>    m_dspec; ///< Hann-windowed estimation spectrum of the mic d = e + yhat
        std::vector<Sample>    m_window;
        std::vector<Sample>    m_spec;
        std::vector<Sample>    m_time;
        std::vector<Sample>    m_sdy_re; ///< smoothed complex cross-spectrum D . conj(Yhat)
        std::vector<Sample>    m_sdy_im;
        std::vector<Sample>    m_sdd;      ///< smoothed |D|^2
        std::vector<Sample>    m_syy;      ///< smoothed |Yhat|^2
        std::vector<Sample>    m_see;      ///< smoothed |E|^2
        std::vector<Sample>    m_leak;     ///< per-bin leakage lam (residual per unit Yhat power)
        std::vector<Sample>    m_gain;     ///< smoothed per-bin gain
        std::vector<size_t>    m_low_cnt;  ///< consecutive certified blocks per low bin
        std::vector<Sample>    m_psm;      ///< smoothed |E|^2 for the floor tracker
        std::vector<Sample>    m_min_cur;  ///< current-window minimum of m_psm
        std::vector<Sample>    m_min_prev; ///< previous-window minimum
        size_t                 m_min_count = 0;
        std::vector<Sample>    m_gspec; ///< constrained gain spectrum (packed)
        std::vector<Sample>    m_gtime; ///< gain impulse response workspace
        Sample                 m_echo_explained = Sample(0);
        std::uint32_t          m_rng            = 0x2545F491U;
    };

    /// The unit the compliance matrix measures: a linear canceller
    /// followed by the residual suppressor, sharing one block size.
    /// Same process_block(x, y, e) signature and real-time contract as
    /// the cores themselves.
    ///
    /// The canceller is pluggable, and the default is the RAW
    /// frequency-domain Kalman core — deliberately not pem_afc. PEM
    /// prewhitening earns its keep in the CLOSED loop (feedback), where
    /// the loudspeaker signal is correlated with the near end and a
    /// naive update is biased; open-loop AEC has an exogenous far end,
    /// no bias to fix, and the predictor's block-by-block refit just
    /// injects gradient noise that floors the misalignment near -20 dB
    /// (measured: raw fdkf single-talk residual -73 dBm0(A) where
    /// PEM-Kalman plateaus at -29 on the same scenario, and double talk
    /// barely moves the raw core — its per-bin noise-PSD tracker IS the
    /// double-talk defense). Any type with the 4-argument raw-core
    /// surface process_block(x, y, e, yhat) works, as does pem_afc's
    /// 3-argument surface with echo_estimate_block().
    template <typename Sample, typename Canceller = partitioned_fdkf<Sample>>
    class aec_chain {
      public:
        using canceller_type = Canceller;

        struct config {
            typename Canceller::config                   canceller;
            typename residual_suppressor<Sample>::config postfilter;
            /// INITIAL RECEIVE GUARD: switched send attenuation while the
            /// far end is active and the canceller has not yet certified
            /// convergence, latched OFF permanently once it has. An
            /// Yhat-referenced suppressor is structurally blind during
            /// initial convergence (the echo estimate is ~0, so nothing
            /// marks the mic signal as echo) and raw echo passes for the
            /// first few hundred ms — the P.1110 convergence masks
            /// (echo <= BGN+10 dB from 100 ms in noise) are unmeetable
            /// without switched loss, which is exactly what the recs'
            /// switching allowance (A_H,S < 20 dB, attenuation in send
            /// when receive-active) exists for. Depth stays inside the
            /// margin target (< 14 dB) so the guard itself remains
            /// switching-compliant. 0 disables.
            Sample guard_attenuation_db = Sample(14);
            /// Receive-activity threshold: x block power must exceed this
            /// factor times the tracked x floor (16 = +12 dB).
            Sample guard_activity_ratio = Sample(16);
            /// Convergence certification: echo_explained() must exceed
            /// this fraction for guard_hold_blocks consecutive blocks.
            /// 0.9 is where the suppressor's own coherence gate opens —
            /// the guard hands over exactly when the Wiener rule can
            /// take the load (0.5 released after ~3 dB of ERLE and the
            /// convergence-in-noise mask was missed by 12 dB).
            Sample guard_converge_ratio = Sample(0.9);
            size_t guard_hold_blocks    = 20;
            /// RE-CONVERGENCE RESCUE: once convergence has certified,
            /// the chain watches the suppressor's echo-explained fraction
            /// (estimated-echo power over mic power); when it exceeds
            /// 1/rescue_drop_ratio for rescue_hold_blocks consecutive
            /// receive-active blocks, the canceller's state uncertainty
            /// is lifted back to P(0) ONCE (weights kept), so
            /// re-convergence runs at cold-start speed instead of the
            /// measured ~7 s self-lock (P starved AND the residual
            /// misbooked into the noise PSD — see fd_kalman.h, which
            /// records the rejected in-core detector).
            ///
            /// The trigger deliberately uses ONLY over-explanation, the
            /// one signal no near end can fake: double talk ADDS mic
            /// power and can only depress the ratio, while an estimate
            /// exceeding the mic means the path it learned is gone (a
            /// swap to a quieter/different room). Three DT-confounded
            /// trigger variants are recorded in git history with their
            /// measured failures: the under-side of the same ratio and a
            /// certified-leakage escalation both fire on the P.501 AM-FM
            /// double-talk plans (interleaved comb lines share analysis
            /// bins, so even coherence-certified bins carry near-end
            /// power at exactly the deep-mismatch scale — echo loss
            /// collapsed to 2 dB), and the in-core momentum detector
            /// diverged outright. The cost of the safe side: a change to
            /// a LOUDER path does not over-explain and keeps the
            /// baseline (coarse-fast, deep-slow) trajectory — closing
            /// that direction needs a dual-path/shadow comparator, filed
            /// in HANDOFF. rescue_drop_ratio 0 disables. Hold ~0.25 s /
            /// cooldown ~2 s at the reference geometry (preset-rescaled).
            Sample rescue_drop_ratio      = Sample(0.5);
            size_t rescue_hold_blocks     = 24;
            size_t rescue_cooldown_blocks = 375;
            /// SHADOW COMPARATOR — the rescue's second trigger, for the
            /// direction over-explanation cannot see (a change toward a
            /// LOUDER path). A small fast canceller (shadow_partitions
            /// of the main's partitions; transition shadow_transition,
            /// fast enough that it never deep-converges but always
            /// tracks coarse) runs on the same (x, y), and the smoothed
            /// residual-power ratio main/shadow is compared: normally
            /// the converged main wins (ratio < 1); when the freshly
            /// adapting shadow out-cancels the main by shadow_ratio for
            /// shadow_hold_blocks consecutive receive-active blocks,
            /// the main's estimate is worse than a cold start would
            /// already be — fire the same one-shot uncertainty lift.
            /// The comparison is double-talk-immune BY CONSTRUCTION: a
            /// near end lands in both residuals equally, so it moves
            /// the ratio toward 1, never past the threshold (measured:
            /// zero fires across the loud/quiet AM-FM double-talk and
            /// noise-pumping batteries at both rates — the performance
            /// comparison succeeds where three correlation-family
            /// detectors failed, see fd_kalman.h). The loud-DT onset
            /// transient (~0.5 s of ratio inflation from smoothing
            /// dynamics) is bounded by the sustained-hold requirement
            /// plus the activity tracker's slow-rise floor, which
            /// freezes counting on CONTINUOUS material. Measured wins
            /// (cabin swap toward 10 dB louder coupling): fires at
            /// 0.8 s / 1.8 s (48 / 16 kHz) and the deep steady moves
            /// from -47 to -79 dBm0 (48 kHz) and -47 to -84 (16 kHz).
            /// shadow_partitions 0 disables (also disabled for
            /// cancellers without reinflate_uncertainty()). Cost when
            /// on: shadow_partitions/partitions of the canceller
            /// (~25 % at the preset geometry).
            size_t shadow_partitions  = 2;
            Sample shadow_transition  = Sample(0.999);
            Sample shadow_ratio       = Sample(2); ///< 3 dB
            size_t shadow_hold_blocks = 56;        ///< ~0.3 s at the reference geometry
        };

        explicit aec_chain(const config& cfg)
            : m_cfg(cfg)
            , m_afc(cfg.canceller)
            , m_post(matched(cfg.postfilter, m_afc.block_size()))
            , m_mid(m_afc.block_size())
            , m_yhat(m_afc.block_size())
            , m_guard_gain(std::pow(Sample(10), -cfg.guard_attenuation_db / Sample(20))) {
            if constexpr (requires(Canceller& c) { c.reinflate_uncertainty(); }) {
                if (cfg.shadow_partitions > 0 && cfg.rescue_drop_ratio > Sample(0)) {
                    auto sc       = cfg.canceller;
                    sc.partitions = cfg.shadow_partitions;
                    sc.transition = cfg.shadow_transition;
                    m_shadow.emplace(sc);
                    m_shadow_e.resize(m_afc.block_size());
                }
            }
        }

        size_t block_size() const noexcept { return m_afc.block_size(); }

        Canceller&                         canceller() noexcept { return m_afc; }
        const Canceller&                   canceller() const noexcept { return m_afc; }
        residual_suppressor<Sample>&       postfilter() noexcept { return m_post; }
        const residual_suppressor<Sample>& postfilter() const noexcept { return m_post; }

        void reset() noexcept {
            m_afc.reset();
            m_post.reset();
            m_x_floor         = Sample(0);
            m_conv_count      = 0;
            m_converged       = false;
            m_guard           = Sample(1);
            m_drop_count      = 0;
            m_rescue_cooldown = 0;
            if (m_shadow.has_value()) {
                m_shadow->reset();
            }
            m_pm           = Sample(0);
            m_ps           = Sample(0);
            m_shadow_count = 0;
        }

        /// True once the initial receive guard has certified convergence
        /// (always true when the guard is disabled).
        bool converged() const noexcept { return m_converged || m_cfg.guard_attenuation_db <= Sample(0); }

        void set_adaptation(bool enabled) noexcept { m_afc.set_adaptation(enabled); }

        void process_block(const Sample* x, const Sample* y, Sample* e) noexcept {
            if constexpr (requires(Canceller& c) { c.process_block(x, y, e, e); }) {
                m_afc.process_block(x, y, m_mid.data(), m_yhat.data());
                m_post.process_block(m_mid.data(), m_yhat.data(), e);
            }
            else {
                m_afc.process_block(x, y, m_mid.data());
                m_post.process_block(m_mid.data(), m_afc.echo_estimate_block(), e);
            }
            const bool receive_active = track_receive_activity(x);
            apply_guard(receive_active, e);
            apply_rescue(receive_active);
            apply_shadow(x, y, receive_active);
        }

      private:
        static typename residual_suppressor<Sample>::config matched(typename residual_suppressor<Sample>::config pf,
                                                                    size_t block_size) {
            pf.block_size = block_size; // one block size for the chain
            return pf;
        }

        /// Shared receive-activity detector (asymmetric x-power floor:
        /// fast fall, slow rise, so it rides under talk) — feeds both
        /// the initial receive guard and the re-convergence rescue.
        bool track_receive_activity(const Sample* x) noexcept {
            const size_t b     = block_size();
            Sample       x_pow = Sample(0);
            for (size_t i = 0; i < b; ++i) {
                x_pow += x[i] * x[i];
            }
            const Sample a_f = x_pow < m_x_floor ? Sample(0.7) : Sample(0.999);
            m_x_floor += (Sample(1) - a_f) * (x_pow - m_x_floor);
            return x_pow > m_cfg.guard_activity_ratio * m_x_floor + Sample(1e-20);
        }

        void apply_guard(bool receive_active, Sample* e) noexcept {
            if (m_cfg.guard_attenuation_db <= Sample(0) || m_converged) {
                return;
            }
            const size_t b = block_size();
            if (m_post.echo_explained() > m_cfg.guard_converge_ratio) {
                if (++m_conv_count >= m_cfg.guard_hold_blocks) {
                    m_converged = true; // latch: the guard never re-engages
                    return;
                }
            }
            else {
                m_conv_count = 0;
            }

            const Sample target =
                receive_active ? std::pow(Sample(10), -m_cfg.guard_attenuation_db / Sample(20)) : Sample(1);
            // ~10 ms transitions at block 256 / 48 kHz; click-free.
            m_guard += Sample(0.5) * (target - m_guard);
            for (size_t i = 0; i < b; ++i) {
                e[i] *= m_guard;
            }
        }

        /// The re-convergence rescue (see the config comment). Inert for
        /// cancellers without reinflate_uncertainty().
        void apply_rescue(bool receive_active) noexcept {
            if constexpr (requires(Canceller& c) { c.reinflate_uncertainty(); }) {
                if (m_cfg.rescue_drop_ratio <= Sample(0) || !converged()) {
                    return;
                }
                if (m_rescue_cooldown > 0) {
                    --m_rescue_cooldown;
                    m_drop_count = 0;
                    return;
                }
                if (!receive_active) {
                    return;
                }
                if (m_post.echo_explained() <= Sample(1) / m_cfg.rescue_drop_ratio) {
                    // Decay, not reset: the smoothed ratio grazes the band
                    // edge inside CSS bursts (measured), and a hard reset
                    // never accumulates across the grazes.
                    m_drop_count /= 2;
                    return;
                }
                if (++m_drop_count >= m_cfg.rescue_hold_blocks) {
                    m_afc.reinflate_uncertainty();
                    m_drop_count      = 0;
                    m_rescue_cooldown = m_cfg.rescue_cooldown_blocks;
                }
            }
            else {
                (void)receive_active;
            }
        }

        /// The shadow comparator (see the config comment): the rescue's
        /// second trigger. No-op unless the shadow was constructed.
        void apply_shadow(const Sample* x, const Sample* y, bool receive_active) noexcept {
            if constexpr (requires(Canceller& c) { c.reinflate_uncertainty(); }) {
                if (!m_shadow.has_value()) {
                    return;
                }
                m_shadow->process_block(x, y, m_shadow_e.data());
                const size_t b  = block_size();
                Sample       m2 = Sample(0);
                Sample       s2 = Sample(0);
                for (size_t i = 0; i < b; ++i) {
                    m2 += m_mid[i] * m_mid[i];
                    s2 += m_shadow_e[i] * m_shadow_e[i];
                }
                const Sample beta = m_cfg.canceller.noise_smoothing;
                m_pm += (Sample(1) - beta) * (m2 - m_pm);
                m_ps += (Sample(1) - beta) * (s2 - m_ps);
                if (!converged() || m_rescue_cooldown > 0 || !receive_active) {
                    return; // cooldown decrement is apply_rescue's job
                }
                if (m_pm <= m_cfg.shadow_ratio * m_ps + m_cfg.canceller.regularization) {
                    m_shadow_count /= 2;
                    return;
                }
                if (++m_shadow_count >= m_cfg.shadow_hold_blocks) {
                    m_afc.reinflate_uncertainty();
                    m_shadow_count    = 0;
                    m_drop_count      = 0;
                    m_rescue_cooldown = m_cfg.rescue_cooldown_blocks;
                }
            }
            else {
                (void)x;
                (void)y;
                (void)receive_active;
            }
        }

        config                      m_cfg;
        Canceller                   m_afc;
        residual_suppressor<Sample> m_post;
        std::vector<Sample>         m_mid;
        std::vector<Sample>         m_yhat;
        Sample                      m_guard_gain;
        Sample                      m_x_floor         = Sample(0);
        size_t                      m_conv_count      = 0;
        bool                        m_converged       = false;
        Sample                      m_guard           = Sample(1);
        size_t                      m_drop_count      = 0;
        size_t                      m_rescue_cooldown = 0;
        std::optional<Canceller>    m_shadow;
        std::vector<Sample>         m_shadow_e;
        Sample                      m_pm           = Sample(0); ///< smoothed main residual power
        Sample                      m_ps           = Sample(0); ///< smoothed shadow residual power
        size_t                      m_shadow_count = 0;
    };

    /// THE COMPLIANCE PRESET: the aec_chain configuration MuTap's ITU-T
    /// battery certifies (docs/itu-compliance.md — every requirement of
    /// the automotive/hands-free recommendations met at both required
    /// rates), generalized to any (block_size, sample_rate) geometry.
    ///
    /// The measurements behind every constant were taken at the reference
    /// geometry, block 256 at 48 kHz; portability to other geometries is
    /// one rule applied uniformly: every per-block constant is rescaled so
    /// the PHYSICAL time constants hold — smoothing factors a' =
    /// a^(block_s / ref_block_s), windows counted in blocks divide by the
    /// same ratio, and the low-band suppression cap keeps covering
    /// 0..300 Hz whatever the bin width. Two constants deliberately do
    /// NOT follow the rule:
    ///   - the canceller transition stays 0.9998 per block (rescaling it
    ///     traded the time-variant-path row to the wire for hangover);
    ///   - the comfort-noise floor bias is calibrated per geometry, not
    ///     rescaled: longer blocks put fewer meter samples in each
    ///     minimum-statistics window and the minima bias deeper. Two
    ///     points are measured (ratio 1: 4.0; ratio 3, the 16 kHz rate:
    ///     5.6); between and near them the preset interpolates linearly
    ///     in the ratio and clamps to the measured neighborhood — treat
    ///     readings far outside block 256 at 16..96 kHz as uncalibrated.
    ///
    /// The Tier A / Tier B batteries pin their chain through this preset
    /// (tests/support/itu_chain.h), so the configuration it returns for
    /// block 256 at 48 and 16 kHz is exactly the measured one; the
    /// mutap.aec~ external's @postfilter mode builds from it as well.
    template <typename Sample>
    typename aec_chain<Sample>::config aec_chain_preset(size_t block_size, size_t partitions, double sample_rate) {
        typename aec_chain<Sample>::config cfg;
        cfg.canceller.block_size          = block_size;
        cfg.canceller.partitions          = partitions;
        cfg.canceller.transition          = Sample(0.9998); // the measured AEC sweet spot; NOT rescaled (see above)
        cfg.canceller.initial_uncertainty = Sample(10);
        const double ratio                = (static_cast<double>(block_size) / sample_rate) / (256.0 / 48000.0);
        cfg.canceller.noise_smoothing     = Sample(std::pow(0.9, ratio));
        // Block-128-class notch (measured, Stage 3 anomaly closed): a hop
        // of ~8 ms resolves the P.501 CSS voiced comb in the analysis
        // window AND holds it for many blocks per segment — the diagonal
        // Kalman then splits the rank-deficient solution uniformly across
        // partitions and burns P on repeated regressors (ERL 8.9 dB by
        // 600 ms at 16 kHz / block 128 vs 22.5 at block 256; the notch
        // follows the hop to 32 kHz / block 256). Inside the prone band
        // the preset enables the core's two counter-measures (novelty
        // discount + decaying uncertainty prior; measured together:
        // 8.9 -> 15.5 by 600 ms, 27.9 -> 46.3 by 3 s at 16 kHz / block
        // 128, AM-FM double talk and the 30 s tone unmoved). Outside it —
        // including both certified geometries (block 256 at 48 / 16 kHz,
        // hops 5.33 / 16 ms) — both stay off: bit-identical behavior.
        const double hop_ms = 1000.0 * static_cast<double>(block_size) / sample_rate;
        if (hop_ms >= 6.0 && hop_ms <= 12.0) {
            cfg.canceller.novelty_smoothing         = Sample(0.8);
            cfg.canceller.novelty_floor             = Sample(0.1);
            cfg.canceller.initial_uncertainty_decay = Sample(0.5);
        }
        // Float32 deployments additionally get the narrowband guard (the
        // fd_kalman.h config comment carries the measured story): a
        // sustained on-bin tone drives a constraint-churn weight walk
        // whose equilibrium scales with rounding — double passes the
        // G.168 tone row at -101 dBm0(A) with 50 dB of margin and stays
        // guard-off (the certified double battery is bit-identical);
        // float32 reads -20 against the -49.3 gate without the guard and
        // -72 worst-case with it. The 1 s hold is scaled like every
        // other real-time window and must outwait a CSS voiced segment.
        if constexpr (std::is_same_v<Sample, float>) {
            cfg.canceller.narrowband_guard       = 0.8F;
            cfg.canceller.narrowband_hold_blocks = std::max<size_t>(8, static_cast<size_t>(187.0 / ratio));
        }
        // Re-convergence rescue windows in blocks follow real time (the
        // hold is ~1 s, the cooldown ~2 s; see the chain config).
        cfg.rescue_hold_blocks     = std::max<size_t>(4, static_cast<size_t>(24.0 / ratio));
        cfg.rescue_cooldown_blocks = std::max<size_t>(16, static_cast<size_t>(375.0 / ratio));
        cfg.shadow_hold_blocks     = std::max<size_t>(6, static_cast<size_t>(56.0 / ratio));
        auto& pf                   = cfg.postfilter;
        pf.leakage_smoothing       = Sample(std::pow(static_cast<double>(pf.leakage_smoothing), ratio));
        pf.gain_attack             = Sample(std::pow(static_cast<double>(pf.gain_attack), ratio));
        pf.gain_release            = Sample(std::pow(static_cast<double>(pf.gain_release), ratio));
        pf.floor_smoothing         = Sample(std::pow(static_cast<double>(pf.floor_smoothing), ratio));
        pf.floor_window = std::max<size_t>(8, static_cast<size_t>(static_cast<double>(pf.floor_window) / ratio));
        // Low-band suppression cap from 300 Hz (protect voice fundamentals
        // no analysis resolution can separate from echo; the canceller
        // owns low-frequency echo) with sustained certification held at
        // 0.3 s of real time.
        const double n_analysis    = static_cast<double>(pf.analysis_blocks * block_size);
        pf.low_band_bins           = static_cast<size_t>(300.0 * n_analysis / sample_rate) + 1;
        pf.low_band_certify_blocks = std::max<size_t>(8, static_cast<size_t>(56.0 / ratio));
        pf.floor_bias              = Sample(std::clamp(4.0 + 0.8 * (ratio - 1.0), 3.0, 5.6));
        return cfg;
    }

} // namespace tap::mu
