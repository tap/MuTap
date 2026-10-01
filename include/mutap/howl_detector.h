/// @file howl_detector.h
/// @brief Per-signal howl detector for the anti-howl safety layer: a
///        resonator bank, per-band growth per loop pass with an
///        integrated-rise trip, and a level catch.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHY. The anti-howl safety layer (the PoC's phase 2 item 6b) ducks a mic
// whose canceller residual starts to howl. The canceller's own convergence
// verdict cannot do that job (it reads "ok" during an established howl), so
// the layer needs a detector on the signal: one per mic, on the residual e_k,
// cheap enough for the embedded targets, and early - before the harness's
// 40 dB rule, not after it.
//
// HOW. Three features and two verdict paths.
//
//   * A bank of `bands` log-spaced 2nd-order resonators (faust-icc's
//     howlDetect geometry: unity-peak bandpass by the bilinear transform,
//     Q 14), each followed by an attack/release envelope (5 / 50 ms). The
//     default range, 150 Hz - 16 kHz, covers every measured howl (below).
//   * Prominence of band b = 20 log10(band b / mean of the other bands).
//     (faust-icc divides by the mean of ALL its bands, the peak included, so
//     its figure cannot exceed 20 log10 N; the tests convert between the two.)
//   * Growth, per band, per loop pass. A howl grows by its excess loop gain
//     on every trip round the loop, so the rate threshold is set in dB per
//     pass (loop_period_s = the chain's latency plus the acoustic flight) and
//     measured on every band, not only the loudest: a singer's partial in
//     one band must not mask a howl growing in another. Each tick, each
//     band's line level - the loudest of the band and its two neighbours,
//     so a howl a frequency shifter drags across bands stays one line - goes
//     into a ring of growth_window_s; a least-squares line through the ring
//     gives the band's slope and its RMS fit residual.
//
//   GROWTH PATH. A band GROWS on a tick when its line is prominent (the band
//   or a neighbour at prominence_db), its slope reaches growth_db_per_pass
//   and its fit residual is at most linearity_db (the log envelope is a
//   straight line: an exponential, not a note onset's step). A growing band
//   integrates its slope tick by tick, inheriting the integral of any
//   neighbour that grew on the previous tick; the verdict fires when an
//   integral reaches rise_db. So persistence shrinks as the rate rises: a
//   fast howl needs few ticks, a slow one many, and a step (onset) never
//   passes the linearity gate long enough.
//
//   LEVEL PATH (a backstop, level_catch): an absolute ceiling on the block
//   RMS (ceiling_db, dB re 1.0), and the block power over a program-level
//   tracker (one pole, program_s) at level_db, gated on the peak band's
//   prominence (level_prominence_db) so a singer's entrance after an intro
//   does not fire it; the tracker is frozen while any band grows, so it
//   cannot climb with a slow howl.
//
//   A verdict holds the trip for hold_s; confidence() is faust-icc's
//   asymmetric follower of the verdict (rise over hold_s, fall over 4x).
//
//   Harmonic partners (bands nearest 2f, 3f, and f/2, f/3 of the loudest
//   band) are READOUTS, not gates: symmetric clipping adds 3f, a howl at a
//   voice partial has partners, nearest-band scalloping is several dB, and
//   3f leaves the bank near its top.
//
// MEASURED (tests/test_howl_detector_host.cpp; macOS x86_64, i9-8950HK,
// AppleClang 17, Release, double; the harness's rooms are the four
// band-limited image-source fixtures and the random_decaying_rir rooms mt5 and
// mt9, five seed sets, voiced and speech near ends; the 40 dB rule is
// closed_loop.h's block RMS >= 100 over the unit-RMS near end; the ceiling is
// placed at +30 dB re that near end, 10 dB under the rule).
//
//   Lead over the 40 dB rule, ms (negative = first). The growth path is the
//   level catch DISABLED; "any" is the default verdict, ceiling and relative
//   catch included; faust-icc is the vendored howlDetect(32, 150, 6000, 14,
//   15, 0.2), confidence > 0.5:
//
//                         growth alone             any path        faust-icc
//                      miss   med    p95    max   miss   max     miss   med    max
//   dry +1/+3, S1 0 Hz  10/120 -74.7 152.0  422.7     0  -41.3      0 -166.7  292.0
//   dry +1/+3, S3 0 Hz   6/120 -98.7 146.7  465.3     0  -77.3     16 -278.7  229.3
//   dry, S1 2 Hz        31/120 -12.0  76.0  134.7     0  -26.7      5   38.7  357.3
//   dry, S1 5 Hz        39/120   8.0 149.3  214.7     0  -21.3      7  104.0  476.0
//   dry, S3 2 Hz        20/120  13.3 165.3  274.7     0  -41.3      0   -1.3  430.7
//   dry, S3 5 Hz        67/120   5.3 284.0  288.0     0  -32.0      9   69.3  500.0
//   PEM limit+1, S1 0   14/20   -2.7   5.3    5.3     0  -13.3      6    8.0  325.3
//   PEM limit+1, S1 2   10/19    6.7 354.7  354.7     0  -17.3      5   68.0  374.7
//   PEM limit+1, S1 5    4/17    2.7 496.0  496.0     0  -14.7      1   36.0  436.0
//   PEM limit+1, S3 0   14/18  -28.0  -9.3   -9.3     0  -22.7      4   40.0  344.0
//   PEM limit+1, S3 2   13/19    9.3  84.0   84.0     0   -5.3      3   48.0  373.3
//   PEM limit+1, S3 5   11/16   18.7  34.7   34.7     0   -2.7      0   33.3  241.3
//
//   (Shifted dry loops run at their own bisected limit + 1 / + 3 dB; the PEM
//   rows are one loop per run, cabin and mt5: 2 s converging at
//   exact_msg_db - 6, then the gain steps to the canceller's bisected limit
//   + 1 dB; runs that never reached the rule in 20 s are not counted.)
//
//   Howl frequencies (the spectral peak at the rule, 829 howls): 618 Hz to
//   15428 Hz; 40 above 6 kHz (faust-icc's top band), 31 above 8 kHz; none
//   outside 150 Hz - 16 kHz, hence the default range.
//
//   False trips. Open signals (no loop), 600 s per material at each of the
//   S1 / S3 loop periods (2400 s each): voiced, music, unison, speech, white,
//   voiced over white at -12 / 0 dB, note onsets, vibrato, crescendo, entrance
//   after silence and after a backing track - 0 trips on every path; the
//   extremes: prominence at most 18.57 dB (music), integrated rise at most
//   0.72 dB (music) against X = 20, gated relative level at most 3.72 dB
//   against L = 25. faust-icc: 232 trips on music, 0 elsewhere.
//   Stable loops (the residual at the canceller's limit - 6 dB, cabin and
//   mt5, S1, 0 / 2 / 5 Hz, 13 materials incl. the backing track: 76 runs,
//   4560 s after the step; 2 onsets runs at 2 Hz reached the 40 dB rule and
//   count as howls): 35 trips, of which 12 lie more than 0.5 s from any
//   block at +20 dB over the near end (all 12 on the growth path; 10 of them
//   in the 2 Hz rows); the other 23 coincide with feedback bursts the loop
//   itself produced. faust-icc on the same residuals: 552 trips, 535 clean.
//
// THRESHOLDS, AND WHAT DID NOT SEPARATE. The growth path's thresholds came
// out of a scratch sweep over the runs above (window, P, growth per pass,
// linearity, X, with and without a level gate; the grid is in the PR), and
// NO setting separates the stable-loop residuals from the howls. A loop
// 6 dB under its canceller's limit still rings: in the stable table its
// lines integrate rises of up to 31.87 dB (mt5, 0 Hz, entrance), its
// residual reaches +37.81 dB over the near end (cabin, 2 Hz, entrance after
// silence), and two runs howled outright. Every growth setting that catches
// most howls before the rule trips there, and every setting quiet there
// misses most howls. The defaults are the best trade found:
//   * P = 18 dB keeps every open material out of the growth path (their
//     prominence tops out at 18.57 dB, music; a pure tone in this bank
//     reaches 20.49, SteadyToneIsProminentButNotAHowl);
//   * X = 20 dB is far above the open-signal extreme (0.72 dB) and holds the
//     stable rows to the 12 clean trips above;
//   * growth 0.3 dB per pass, linearity 3 dB RMS and a 16-tick (21 ms)
//     window trade misses against clean stable trips at that X (a 24-tick
//     window missed fewer howls and tripped more, 12 ticks the reverse);
//   * the slope is a least-squares fit, not an endpoint difference: in the
//     first sweep (an earlier, sustained-condition design) the endpoint
//     difference over the same window tripped on open speech where the fit
//     did not;
//   * the relative catch (L 25 dB over a 4 s tracker frozen while a line
//     grows, gated at prominence 18) adds no clean trip.
// The price is in the latency table: the growth path alone misses broadband
// and multi-line onsets (the PEM rows at limit + 1) and is late in the
// median behind a frequency shifter; the ceiling then does the work. The
// guard (PR B) should treat the growth path as an early hint and the
// ceiling as the guarantee, with ceiling_db calibrated per deployment.
//
// COST (bench/bench_howl_detector.cpp, defaults, block 64 at 48 kHz, the same
// i9-8950HK at load 4.1): 1786 ns per block in float32, 3039 ns in double -
// 0.55 % / 0.94 % of one pem_afc process_block (pem_afc.h: 324455.2 /
// 324621.6 ns).
//
// Real-time contract (as frequency_shifter.h): the constructor validates its
// config, may throw std::invalid_argument and allocates (the bank's state
// and the growth ring: (6 + growth window ticks) x bands values, about 3.6 KB
// in float at the defaults). process_block() and every other
// post-construction entry point are noexcept and allocation-free. No double
// arithmetic runs in the float hot path. Denormal-safe: the resonators run
// on a DC bias their bandpass rejects, and every envelope has a floor.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

// No ABI tag: nothing here holds an FFT (the analysis is a resonator bank),
// so the layout does not depend on the build's FFT engine and the class sits
// in plain tap::mu (as frequency_shifter.h's and lpc.h's do).
namespace tap::mu {

    /// Which path fired the detector's verdict on the last tick.
    enum class howl_trigger : std::uint8_t {
        none,    ///< no verdict
        growth,  ///< a prominent line's integrated growth reached rise_db
        ceiling, ///< block RMS reached ceiling_db
        level    ///< block power over the program level reached level_db, gated on prominence
    };

    namespace howl_detail {

        /// Everything the per-tick decision needs, derived once from the
        /// detector's config (times converted to ticks).
        template <typename Sample>
        struct decision_params {
            size_t              bands        = 2;
            size_t              slots        = 2; ///< growth window, ticks
            Sample              inv_others   = Sample(1);
            Sample              prominence   = Sample(0); ///< P, dB
            Sample              growth_tick  = Sample(0); ///< minimum slope, dB per tick
            Sample              linearity    = Sample(0); ///< maximum fit residual, dB RMS
            Sample              rise         = Sample(0); ///< X, dB
            bool                level_catch  = true;
            Sample              ceiling_pow  = Sample(0); ///< block power ceiling
            Sample              level        = Sample(0); ///< L, dB
            Sample              level_prom   = Sample(0); ///< prominence gate of the relative catch, dB
            Sample              program_coef = Sample(0); ///< one-pole coefficient per tick
            size_t              hold_ticks   = 0;
            Sample              conf_att     = Sample(0);
            Sample              conf_rel     = Sample(0);
            std::vector<Sample> slope_w; ///< LS slope weights, dB per tick per dB
        };

        /// Per-tick state and readouts.
        template <typename Sample>
        struct decision_state {
            std::vector<Sample> level_db; ///< 20 log10 band envelope
            std::vector<Sample> prom;     ///< per-band prominence
            std::vector<Sample> ring;     ///< slots x bands of line levels
            std::vector<Sample> rise;     ///< integrated rise per band (0 = no line)
            std::vector<Sample> rise_prev;
            size_t              ring_pos   = 0;
            size_t              ring_count = 0;
            Sample              program    = Sample(0);
            bool                program_ok = false;
            size_t              hold_left  = 0;
            // readouts
            size_t       peak_band  = 0;
            size_t       line_band  = 0;
            Sample       prominence = Sample(0);
            Sample       growth     = Sample(0); ///< dB per tick, the peak band's line
            Sample       max_rise   = Sample(0);
            Sample       level      = Sample(0); ///< dB over the program level
            Sample       power_db   = Sample(0); ///< block power, dB re 1
            Sample       conf       = Sample(0);
            bool         raw        = false;
            bool         tripped    = false;
            howl_trigger trigger    = howl_trigger::none;

            void resize(size_t bands, size_t slots) {
                level_db.assign(bands, Sample(0));
                prom.assign(bands, Sample(0));
                ring.assign(bands * slots, Sample(0));
                rise.assign(bands, Sample(0));
                rise_prev.assign(bands, Sample(0));
            }

            void reset() noexcept {
                std::fill(level_db.begin(), level_db.end(), Sample(0));
                std::fill(prom.begin(), prom.end(), Sample(0));
                std::fill(ring.begin(), ring.end(), Sample(0));
                std::fill(rise.begin(), rise.end(), Sample(0));
                std::fill(rise_prev.begin(), rise_prev.end(), Sample(0));
                ring_pos   = 0;
                ring_count = 0;
                program    = Sample(0);
                program_ok = false;
                hold_left  = 0;
                peak_band  = 0;
                line_band  = 0;
                prominence = Sample(0);
                growth     = Sample(0);
                max_rise   = Sample(0);
                level      = Sample(0);
                power_db   = Sample(0);
                conf       = Sample(0);
                raw        = false;
                tripped    = false;
                trigger    = howl_trigger::none;
            }
        };

        /// Least-squares slope (dB per tick) and RMS fit residual (dB) of
        /// band b's line level over the ring, oldest slot first.
        template <typename Sample>
        void fit(const decision_params<Sample>& p, const decision_state<Sample>& s, size_t b, Sample& slope,
                 Sample& residual) noexcept {
            const size_t nb   = p.bands;
            const size_t m    = p.slots;
            Sample       mean = Sample(0);
            size_t       k    = s.ring_pos;
            for (size_t i = 0; i < m; ++i) {
                mean += s.ring[k * nb + b];
                k = (k + 1 == m) ? 0 : k + 1;
            }
            mean /= static_cast<Sample>(m);
            Sample sl = Sample(0);
            Sample ss = Sample(0);
            k         = s.ring_pos;
            for (size_t i = 0; i < m; ++i) {
                const Sample d = s.ring[k * nb + b] - mean;
                sl += p.slope_w[i] * d;
                ss += d * d;
                k = (k + 1 == m) ? 0 : k + 1;
            }
            // The fit's residual sum of squares is Syy - slope^2 Sxx, with
            // Syy = sum d^2 and Sxx = sum (i - mean i)^2 = m (m^2 - 1) / 12.
            const Sample sxx = static_cast<Sample>(m) * static_cast<Sample>(m * m - 1) / Sample(12);
            const Sample rss = std::max(ss - sl * sl * sxx, Sample(0));
            slope            = sl;
            residual         = std::sqrt(rss / static_cast<Sample>(m));
        }

        /// One detector tick from the band envelopes (linear, floored above 0)
        /// and the block's mean-square power.
        template <typename Sample>
        void decide(const decision_params<Sample>& p, decision_state<Sample>& s, const Sample* env,
                    Sample power) noexcept {
            constexpr Sample eps = Sample(1e-9);
            const size_t     nb  = p.bands;

            // Levels and per-band prominence over the mean of the others.
            Sample sum  = Sample(0);
            size_t peak = 0;
            for (size_t b = 0; b < nb; ++b) {
                sum += env[b];
                s.level_db[b] = Sample(20) * std::log10(env[b]);
                if (env[b] > env[peak]) {
                    peak = b;
                }
            }
            for (size_t b = 0; b < nb; ++b) {
                const Sample others = std::max(sum - env[b], Sample(0)) * p.inv_others;
                s.prom[b]           = Sample(20) * std::log10((env[b] + eps) / (others + eps));
            }
            s.peak_band  = peak;
            s.prominence = s.prom[peak];

            // Line levels into the ring: the loudest of each band and its
            // neighbours, so a line moving one band (a shifted howl's climb)
            // stays one line.
            Sample* slot = &s.ring[s.ring_pos * nb];
            for (size_t b = 0; b < nb; ++b) {
                const size_t lo = (b == 0) ? 0 : b - 1;
                const size_t hi = (b + 1 == nb) ? b : b + 1;
                Sample       m  = s.level_db[lo];
                for (size_t k = lo + 1; k <= hi; ++k) {
                    m = std::max(m, s.level_db[k]);
                }
                slot[b] = m;
            }
            s.ring_pos = (s.ring_pos + 1 == p.slots) ? 0 : s.ring_pos + 1;
            if (s.ring_count < p.slots) {
                ++s.ring_count;
            }
            const bool full = s.ring_count == p.slots;

            // Per-band growth and the integrated rise of each line.
            std::swap(s.rise, s.rise_prev);
            Sample max_rise = Sample(0);
            size_t line     = 0;
            bool   growing  = false;
            s.growth        = Sample(0);
            for (size_t b = 0; b < nb; ++b) {
                s.rise[b] = Sample(0);
                if (!full) {
                    continue;
                }
                const size_t lo = (b == 0) ? 0 : b - 1;
                const size_t hi = (b + 1 == nb) ? b : b + 1;
                Sample       pm = s.prom[lo];
                Sample       rp = s.rise_prev[lo];
                for (size_t k = lo + 1; k <= hi; ++k) {
                    pm = std::max(pm, s.prom[k]);
                    rp = std::max(rp, s.rise_prev[k]);
                }
                if (pm < p.prominence && b != peak) {
                    continue;
                }
                Sample slope;
                Sample residual;
                fit(p, s, b, slope, residual);
                if (b == peak) {
                    s.growth = slope;
                }
                if (pm >= p.prominence && slope >= p.growth_tick && residual <= p.linearity) {
                    s.rise[b] = (rp > Sample(0)) ? rp + slope : slope;
                    growing   = true;
                    if (s.rise[b] > max_rise) {
                        max_rise = s.rise[b];
                        line     = b;
                    }
                }
            }
            s.max_rise  = max_rise;
            s.line_band = growing ? line : peak;

            // Level catch: an absolute ceiling, and the block over a program
            // level that is frozen while any line grows, gated on prominence.
            const Sample pw = power + Sample(1e-30);
            s.power_db      = Sample(10) * std::log10(pw);
            if (!s.program_ok) {
                s.program    = pw;
                s.program_ok = true;
            }
            s.level = Sample(10) * std::log10(pw / s.program);
            if (!growing) {
                s.program += p.program_coef * (pw - s.program);
            }

            // Verdict.
            s.trigger = howl_trigger::none;
            if (max_rise >= p.rise) {
                s.trigger = howl_trigger::growth;
            }
            else if (p.level_catch && pw >= p.ceiling_pow) {
                s.trigger = howl_trigger::ceiling;
            }
            else if (p.level_catch && s.level >= p.level && s.prominence >= p.level_prom) {
                s.trigger = howl_trigger::level;
            }
            s.raw = s.trigger != howl_trigger::none;
            if (s.raw) {
                s.hold_left = p.hold_ticks;
                s.tripped   = true;
            }
            else if (s.hold_left > 0) {
                --s.hold_left;
                s.tripped = true;
            }
            else {
                s.tripped = false;
            }
            const Sample target = s.raw ? Sample(1) : Sample(0);
            const Sample c      = s.raw ? p.conf_att : p.conf_rel;
            s.conf              = target + c * (s.conf - target);
        }

    } // namespace howl_detail

    /// Howl detector over one signal (the per-mic canceller residual). See the
    /// file comment for the features, the verdict and the measured numbers.
    template <typename Sample>
    class howl_detector {
      public:
        struct config {
            double sample_rate   = 48000.0;       ///< Hz; finite, > 0
            size_t block_size    = 64;            ///< samples per detector tick; >= 1
            size_t bands         = 32;            ///< resonators, log-spaced; >= 3
            Sample f_lo_hz       = Sample(150);   ///< lowest band centre, Hz
            Sample f_hi_hz       = Sample(16000); ///< highest band centre, Hz; < sample_rate / 2
            Sample q             = Sample(14);    ///< resonator Q; > 0
            Sample attack_s      = Sample(0.005); ///< band envelope attack; > 0
            Sample release_s     = Sample(0.050); ///< band envelope release; > 0
            Sample loop_period_s = Sample(0.010); ///< one trip round the loop (chain latency + flight); > 0

            Sample growth_window_s = Sample(0.0213); ///< line-level fit window (16 blocks of 64 at 48 kHz); >= 3 ticks
            Sample prominence_db   = Sample(18);     ///< P: a growing line's prominence
            Sample growth_db_per_pass = Sample(0.3); ///< a tick counts if its slope reaches this
            Sample linearity_db       = Sample(3);   ///< ... and its fit residual is at most this (RMS)
            Sample rise_db            = Sample(20);  ///< X: integrated growth that trips

            bool   level_catch         = true;       ///< false: the growth path alone
            Sample ceiling_db          = Sample(-6); ///< block RMS, dB re 1.0 (a host calibration)
            Sample level_db            = Sample(25); ///< L: block power over the program level
            Sample level_prominence_db = Sample(18); ///< the relative catch needs this prominence
            Sample program_s           = Sample(4);  ///< program tracker time constant; > 0

            Sample hold_s = Sample(0.2); ///< a trip holds this long after its cause; >= 0
        };

        /// @throws std::invalid_argument on a config the field comments'
        ///         ranges reject, or a resonator whose rounded coefficients
        ///         would not be stable in Sample.
        explicit howl_detector(const config& cfg)
            : m_cfg(validated(cfg)) {
            const size_t nb    = cfg.bands;
            const double fs    = cfg.sample_rate;
            const double lo    = static_cast<double>(cfg.f_lo_hz);
            const double hi    = static_cast<double>(cfg.f_hi_hz);
            const double q     = static_cast<double>(cfg.q);
            const double tick  = static_cast<double>(cfg.block_size) / fs; // seconds per tick
            const double ratio = hi / lo;
            m_a1.resize(nb);
            m_a2.resize(nb);
            m_g.resize(nb);
            m_w1.resize(nb);
            m_w2.resize(nb);
            m_env.resize(nb);
            m_hz.resize(nb);
            m_partner.resize(4 * nb);
            for (size_t b = 0; b < nb; ++b) {
                // faust-icc's logBandFreq and fi.resonbp(fc, q, 1): a
                // unity-peak 2nd-order bandpass by the bilinear transform,
                // prewarped at fc.
                const double fc = lo * std::pow(ratio, static_cast<double>(b) / static_cast<double>(nb - 1));
                const double k  = std::tan(std::numbers::pi * fc / fs);
                const double a0 = 1.0 + k / q + k * k;
                m_hz[b]         = static_cast<Sample>(fc);
                m_g[b]          = static_cast<Sample>((k / q) / a0);
                m_a1[b]         = static_cast<Sample>(2.0 * (k * k - 1.0) / a0);
                m_a2[b]         = static_cast<Sample>((1.0 - k / q + k * k) / a0);
                // Stability of the rounded recursion: poles inside the unit
                // circle iff |a2| < 1 and |a1| < 1 + a2.
                if (!(m_a2[b] < Sample(1)) || !(std::abs(m_a1[b]) < Sample(1) + m_a2[b])) {
                    throw std::invalid_argument("howl_detector: a resonator is unstable in this precision");
                }
            }
            for (size_t b = 0; b < nb; ++b) {
                const double f       = static_cast<double>(m_hz[b]);
                m_partner[4 * b + 0] = nearest_band(2.0 * f, lo, ratio, nb);
                m_partner[4 * b + 1] = nearest_band(3.0 * f, lo, ratio, nb);
                m_partner[4 * b + 2] = nearest_band(f / 2.0, lo, ratio, nb);
                m_partner[4 * b + 3] = nearest_band(f / 3.0, lo, ratio, nb);
            }
            m_att = static_cast<Sample>(std::exp(-1.0 / (static_cast<double>(cfg.attack_s) * fs)));
            m_rel = static_cast<Sample>(std::exp(-1.0 / (static_cast<double>(cfg.release_s) * fs)));

            auto& p = m_params;
            p.bands = nb;
            p.slots =
                std::max<size_t>(static_cast<size_t>(std::lround(static_cast<double>(cfg.growth_window_s) / tick)), 3);
            p.inv_others      = static_cast<Sample>(1.0 / static_cast<double>(nb - 1));
            p.prominence      = cfg.prominence_db;
            p.growth_tick     = static_cast<Sample>(static_cast<double>(cfg.growth_db_per_pass) * tick
                                                    / static_cast<double>(cfg.loop_period_s));
            p.linearity       = cfg.linearity_db;
            p.rise            = cfg.rise_db;
            p.level_catch     = cfg.level_catch;
            p.ceiling_pow     = static_cast<Sample>(std::pow(10.0, static_cast<double>(cfg.ceiling_db) / 10.0));
            p.level           = cfg.level_db;
            p.level_prom      = cfg.level_prominence_db;
            p.program_coef    = static_cast<Sample>(1.0 - std::exp(-tick / static_cast<double>(cfg.program_s)));
            p.hold_ticks      = static_cast<size_t>(std::lround(static_cast<double>(cfg.hold_s) / tick));
            const double hold = static_cast<double>(cfg.hold_s);
            p.conf_att        = hold > 0.0 ? static_cast<Sample>(std::exp(-tick / hold)) : Sample(0);
            p.conf_rel        = hold > 0.0 ? static_cast<Sample>(std::exp(-tick / (4.0 * hold))) : Sample(0);
            p.slope_w.resize(p.slots);
            const double mean = 0.5 * static_cast<double>(p.slots - 1);
            double       sxx  = 0.0;
            for (size_t k = 0; k < p.slots; ++k) {
                sxx += (static_cast<double>(k) - mean) * (static_cast<double>(k) - mean);
            }
            for (size_t k = 0; k < p.slots; ++k) {
                p.slope_w[k] = static_cast<Sample>((static_cast<double>(k) - mean) / sxx);
            }
            m_ticks_per_s    = static_cast<Sample>(1.0 / tick);
            m_ticks_per_pass = static_cast<Sample>(static_cast<double>(cfg.loop_period_s) / tick);
            m_inv_block      = static_cast<Sample>(1.0 / static_cast<double>(cfg.block_size));
            m_state.resize(nb, p.slots);
            reset();
        }

        const config& cfg() const noexcept { return m_cfg; }
        size_t        bands() const noexcept { return m_cfg.bands; }
        /// Centre frequency of band b (b < bands()).
        Sample band_hz(size_t b) const noexcept { return m_hz[b]; }

        /// Clear every state (as constructed).
        void reset() noexcept {
            std::fill(m_w1.begin(), m_w1.end(), Sample(0));
            std::fill(m_w2.begin(), m_w2.end(), Sample(0));
            std::fill(m_env.begin(), m_env.end(), k_env_floor);
            m_pos      = 0;
            m_block_sq = Sample(0);
            m_state.reset();
        }

        /// Analyse n samples. The detector ticks (features and verdict
        /// update) every block_size samples of input, whatever the caller's
        /// partition: the readouts after a call are those of the last
        /// completed tick.
        void process_block(const Sample* in, size_t n) noexcept {
            size_t i = 0;
            while (i < n) {
                const size_t room = m_cfg.block_size - m_pos;
                const size_t len  = (n - i > room) ? room : n - i;
                run_bank(in + i, len);
                i += len;
                m_pos += len;
                if (m_pos == m_cfg.block_size) {
                    howl_detail::decide(m_params, m_state, m_env.data(), m_block_sq * m_inv_block);
                    m_block_sq = Sample(0);
                    m_pos      = 0;
                }
            }
        }

        // ------------------------------------------------------- readouts

        /// Whether the detector is tripped: from the tick the verdict fires
        /// until hold_s after it last fired.
        bool tripped() const noexcept { return m_state.tripped; }
        /// The last tick's verdict alone (no hold), and its path.
        bool         verdict() const noexcept { return m_state.raw; }
        howl_trigger trigger() const noexcept { return m_state.trigger; }
        /// 0..1: the verdict through faust-icc's asymmetric follower (rises
        /// over hold_s, falls over 4 hold_s).
        Sample confidence() const noexcept { return m_state.conf; }
        /// Centre frequency of the loudest band.
        Sample peak_hz() const noexcept { return m_hz[m_state.peak_band]; }
        /// Centre frequency of the band carrying the largest integrated rise
        /// (the loudest band when no line grows).
        Sample line_hz() const noexcept { return m_hz[m_state.line_band]; }
        /// 20 log10(loudest band / mean of the other bands), dB.
        Sample prominence_db() const noexcept { return m_state.prominence; }
        /// Slope of the loudest band's line level over growth_window_s, per
        /// loop pass and per second (0 until the window has filled).
        Sample growth_db_per_pass() const noexcept { return m_state.growth * m_ticks_per_pass; }
        Sample growth_db_per_s() const noexcept { return m_state.growth * m_ticks_per_s; }
        /// The largest integrated rise of a growing line, dB (0: none).
        Sample rise_db() const noexcept { return m_state.max_rise; }
        /// Energy in the bands nearest 2f and 3f over the loudest band at f,
        /// dB; -inf when neither lies inside the bank.
        Sample harmonic_ratio_db() const noexcept { return partner_ratio(0); }
        /// The same for f/2 and f/3 (the peak sitting on partial 2 or 3).
        Sample subharmonic_ratio_db() const noexcept { return partner_ratio(2); }
        /// Last tick's block power over the program level, dB.
        Sample level_db() const noexcept { return m_state.level; }
        /// Last tick's block power, dB re 1.0 (full scale for a +-1 signal).
        Sample power_db() const noexcept { return m_state.power_db; }
        /// 20 log10 of band b's envelope (b < bands()).
        Sample band_level_db(size_t b) const noexcept { return Sample(20) * std::log10(m_env[b]); }

      private:
        static constexpr size_t k_none = std::numeric_limits<size_t>::max();
        /// Added to the resonators' input: a bandpass rejects DC, so the
        /// constant keeps the recursion's state normal in digital silence
        /// without reaching the output.
        static constexpr Sample k_bias = Sample(1e-20);
        /// Added to every rectified band sample: the envelopes settle here
        /// in silence instead of decaying into denormals.
        static constexpr Sample k_env_floor = Sample(1e-20);

        static config validated(const config& cfg) {
            const auto fin  = [](auto v) { return std::isfinite(v); };
            const auto rate = static_cast<Sample>(cfg.sample_rate);
            if (!fin(cfg.sample_rate) || !fin(rate) || !(cfg.sample_rate > 0.0)) {
                throw std::invalid_argument("howl_detector: sample_rate must be finite and > 0");
            }
            if (cfg.block_size == 0) {
                throw std::invalid_argument("howl_detector: block_size must be >= 1");
            }
            if (cfg.bands < 3) {
                throw std::invalid_argument("howl_detector: bands must be >= 3");
            }
            if (!fin(cfg.f_lo_hz) || !fin(cfg.f_hi_hz) || !(cfg.f_lo_hz > Sample(0)) || !(cfg.f_hi_hz > cfg.f_lo_hz)
                || !(static_cast<double>(cfg.f_hi_hz) < 0.5 * cfg.sample_rate)) {
                throw std::invalid_argument("howl_detector: need 0 < f_lo_hz < f_hi_hz < sample_rate / 2");
            }
            for (const Sample t : {cfg.q, cfg.attack_s, cfg.release_s, cfg.loop_period_s, cfg.program_s}) {
                if (!fin(t) || !(t > Sample(0))) {
                    throw std::invalid_argument(
                        "howl_detector: q, attack_s, release_s, loop_period_s, program_s must be finite and > 0");
                }
            }
            if (!fin(cfg.hold_s) || !(cfg.hold_s >= Sample(0))) {
                throw std::invalid_argument("howl_detector: hold_s must be finite and >= 0");
            }
            if (!fin(cfg.growth_window_s)
                || !(static_cast<double>(cfg.growth_window_s) * cfg.sample_rate
                     >= 3.0 * static_cast<double>(cfg.block_size))) {
                throw std::invalid_argument("howl_detector: growth_window_s must span at least 3 blocks");
            }
            for (const Sample t : {cfg.prominence_db, cfg.growth_db_per_pass, cfg.linearity_db, cfg.rise_db,
                                   cfg.ceiling_db, cfg.level_db, cfg.level_prominence_db}) {
                if (!fin(t)) {
                    throw std::invalid_argument("howl_detector: thresholds must be finite");
                }
            }
            return cfg;
        }

        /// The band whose centre is nearest f on the log axis, or k_none
        /// when f lies more than half a band step outside the bank.
        static size_t nearest_band(double f, double lo, double ratio, size_t nb) {
            const double pos = std::log(f / lo) / std::log(ratio) * static_cast<double>(nb - 1);
            const double idx = std::round(pos);
            if (idx < 0.0 || idx > static_cast<double>(nb - 1)) {
                return k_none;
            }
            return static_cast<size_t>(idx);
        }

        Sample partner_ratio(size_t first) const noexcept {
            const size_t p   = m_state.peak_band;
            const size_t a   = m_partner[4 * p + first];
            const size_t b   = m_partner[4 * p + first + 1];
            Sample       sum = Sample(0);
            if (a != k_none) {
                sum += m_env[a] * m_env[a];
            }
            if (b != k_none) {
                sum += m_env[b] * m_env[b];
            }
            if (a == k_none && b == k_none) {
                return -std::numeric_limits<Sample>::infinity();
            }
            return Sample(10) * std::log10(sum / (m_env[p] * m_env[p]));
        }

        /// The resonators and their envelopes over len samples.
        void run_bank(const Sample* in, size_t len) noexcept {
            const size_t  nb  = m_cfg.bands;
            const Sample* a1  = m_a1.data();
            const Sample* a2  = m_a2.data();
            const Sample* g   = m_g.data();
            Sample*       w1  = m_w1.data();
            Sample*       w2  = m_w2.data();
            Sample*       env = m_env.data();
            const Sample  att = m_att;
            const Sample  rel = m_rel;
            Sample        sq  = m_block_sq;
            for (size_t i = 0; i < len; ++i) {
                const Sample x  = in[i];
                const Sample xb = x + k_bias;
                sq += x * x;
                for (size_t b = 0; b < nb; ++b) {
                    const Sample w = xb - a1[b] * w1[b] - a2[b] * w2[b];
                    const Sample y = std::abs(g[b] * (w - w2[b])) + k_env_floor;
                    w2[b]          = w1[b];
                    w1[b]          = w;
                    const Sample c = (y > env[b]) ? att : rel;
                    env[b]         = y + c * (env[b] - y);
                }
            }
            m_block_sq = sq;
        }

        config m_cfg;

        // resonator bank, per band
        std::vector<Sample> m_a1;
        std::vector<Sample> m_a2;
        std::vector<Sample> m_g;
        std::vector<Sample> m_w1;
        std::vector<Sample> m_w2;
        std::vector<Sample> m_env;
        std::vector<Sample> m_hz;
        std::vector<size_t> m_partner; ///< per band: 2f, 3f, f/2, f/3
        Sample              m_att = Sample(0);
        Sample              m_rel = Sample(0);

        size_t m_pos            = 0;
        Sample m_block_sq       = Sample(0);
        Sample m_inv_block      = Sample(1);
        Sample m_ticks_per_s    = Sample(1);
        Sample m_ticks_per_pass = Sample(1);

        howl_detail::decision_params<Sample> m_params;
        howl_detail::decision_state<Sample>  m_state;
    };

} // namespace tap::mu
