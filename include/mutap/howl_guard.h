/// @file howl_guard.h
/// @brief The anti-howl safety layer: per-mic gain policy over the
///        canceller's convergence verdict and a howl detector on each
///        residual - arming, duck, hold, re-arm, back-off and latch.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// WHY. A feedback canceller protects the loop only once it has identified
// the room, and it says so through two raw statistics (pem_afc.h): the
// shadow comparator ratio D and the identification progress A'. The release
// experiment found that D and A' read "ok" DURING an established howl, that
// every sustained howl above the dry limit came from a run whose verdict had
// never declared, and that a louder coupling (F -> 2F) leaves the verdict
// "not ok" for good. So the guard (1) ducks from reset until the verdict
// first declares, (2) ducks on a howl detector over each mic's residual
// (howl_detector.h), and (3) re-arms on a timer when the verdict cannot
// recover, with an edge-triggered verdict trigger so it does not pump.
//
// THE VERDICT, per mic: ok = D < d_db AND A' < a_db (dB of the canceller's
// shadow_residual_ratio() and uncertainty_ratio(); defaults the phase-0
// calibration, -1.235 / -23.842 dB). QUIET = no detector verdict of any path
// in the last quiet_s.
//
// TRIGGERS, per mic and tick (one tick = one block):
//   * TRIP (a detector trip): the level catch (the absolute ceiling or the
//     relative catch; howl_detector's ceiling_db is a deployment
//     calibration), always; or the growth path while the verdict is NOT ok.
//     Growth while the verdict IS ok ducks nothing: it is counted as a hint
//     (hints()), because howl_detector.h measured its growth path tripping
//     on stable loops' residuals (12 clean trips in 4560 s, 10 of them with
//     a 2 Hz shift).
//   * LOST (the verdict trigger): the verdict has been not-ok for
//     trip_hold_s, counted from an ok tick seen since the last re-arm (a
//     fresh ok -> not-ok edge). Armed only in OPEN and RELEASING after a
//     declaration; a timer re-arm disarms it until the verdict reads ok
//     once, so F -> 2F does not pump duck / open.
//
// STATES (guard_state) and every transition. "Gain" is the per-mic gain in
// dB relative to the host's operating gain (0 = pass). level = -strike_db x
// strikes (the gain the guard restores TO). arming gain = -arming_duck_db,
// further limited by cap_db when the host set one. Every gain change is a
// linear ramp in dB over a fixed duration - attack_ms down, release_ramp_ms
// up - that lands exactly on its target.
//
//   ARMING       entered on construction, reset() (and afc_chain::reset()),
//                and from any other state when A' RETURNS to restart_a_db
//                (it fell below a_db since ARMING, then regrew: a canceller
//                restart). Gain = arming gain.
//     -> OPEN         ok held release_hold_s AND quiet (the declaration;
//                     ramps up over release_ramp_ms, probation starts).
//     -> OPEN_CAPPED  arming_timeout_s in ARMING, cap set, quiet: ramp to
//                     the cap; `unprotected`.
//     (timeout, no cap: stays ARMING at the arming gain, `unprotected`.)
//     TRIP: stays ARMING, a strike (never latches here), restarts the hold.
//     ARMING never re-arms on a timer.
//   OPEN         gain = level. Probation (probation_s) from entry.
//     -> DUCKED  TRIP (a strike within probation); LOST (no strike).
//     Strike decay: one strike per strike_decay_s in OPEN without a trip
//     (the level ramps up strike_db).
//   OPEN_CAPPED  gain = min(cap, level); no declaration yet; `unprotected`.
//     -> DUCKED  TRIP (a strike within probation). LOST is disarmed: the
//                verdict never declared.
//     -> RELEASING  ok held release_hold_s AND quiet (a late declaration:
//                   ramps to level, then OPEN).
//   DUCKED       gain = restore - duck_db (restore: level; min(cap, level)
//                before a declaration; the floor while latched). The hold
//                timer starts at entry and restarts on every TRIP.
//     -> RELEASING  ok held release_hold_s AND quiet          [walk path]
//     -> RELEASING  hold reached rearm_timeout_s x 2^strikes AND quiet
//                   (disarms LOST until ok is seen)           [re-arm path]
//   RELEASING    gain ramps to restore over release_ramp_ms.
//     -> DUCKED  TRIP (a strike); LOST (no strike).
//     -> on arrival: LATCHED while latched, else OPEN after a declaration,
//        else OPEN_CAPPED; probation starts.
//   LATCHED      gain = the floor (cap if set, else level when it latched);
//                `unprotected`. Entered (through DUCKED, then RELEASING) by
//                the strike that brings strikes to max_strikes outside
//                ARMING.
//     -> DUCKED  TRIP (no strike; the release returns to LATCHED).
//     -> RELEASING  a FRESH ok edge (not-ok seen since latching, then ok
//                   held release_hold_s) AND quiet: unlatched, ramps to
//                   level, then OPEN with strikes still at max_strikes (one
//                   strike in probation latches again).
//     clear() (host): strikes 0, level 0, unlatched -> RELEASING.
//
// A strike: a TRIP in ARMING, in RELEASING, or in OPEN / OPEN_CAPPED within
// probation_s of entering it. Each lowers level by strike_db and doubles
// the re-arm timeout.
//
// PER-MIC ATTRIBUTION (M > 1). A howl lives in the shared speaker signal,
// so every residual carries it. On a tick with TRIPs, the shared peak is
// the peak band of the tripping mic with the largest block power; the mics
// whose band_level_db at that band is within attrib_db of the loudest get
// the TRIP. If a mic was left out and any mic still TRIPs attack_ms + 50 ms
// later, every mic gets the TRIP (the fallback; fallbacks()).
//
// THE BUS STAGE (bus_stage(), an afc_stage for afc_chain::set_safety()).
// The per-mic gains act before the decorrelator and reverb, so a ducked
// howl keeps ringing in a charged reverb. The bus stage applies, after the
// reverb, the deepest DUCK among the mics in DUCKED or RELEASING (each mic's
// gain relative to its restore target, so it ramps with the mic and is 0 dB
// in every other state): on a trip the post-reverb bus is cut too. On the
// dry path a trip then counts twice (per mic, and on the bus).
//
// THE CANCELLER MUST HAVE THE SHADOW. Without it D is 1 (0 dB) and the
// verdict can never declare. afc_chain::set_guard() static_asserts that the
// canceller has uncertainty_ratio() and shadow_residual_ratio(), and
// refuses (returns false, leaving the guard detached) when any canceller's
// shadow_enabled() is false.
//
// MEASURED (tests/test_howl_guard_host.cpp, the gated rows; docs/howl-guard.md
// has the MUTAP_SLOW grids. macOS x86_64, i9-8950HK, AppleClang 17,
// Release, double; afc_chain with PEM + FD-Kalman and the 2-partition shadow,
// 1024 taps, block 64 at 48 kHz, S1; cabin and mt5, seeds 1 / 21 / 41;
// every run from reset; the harness's ceiling at +30 dB re the unit near end):
//   * Cold start at the canceller's limit - 6 dB: 0 blocks at the 40 dB
//     rule in 36 runs. With the backing track every run declares (median
//     1.77 to 2.07 s); without it none declares in 20 s - at the arming
//     gain the canceller sees no excitation - and every run leaves ARMING
//     through the cap at 10 s. At exact_msg_db + 3 / + 6: 0 howl blocks in
//     24 runs; at the limit + 6: 0 guarded, against 575 blocks in 3 of 6
//     unguarded voiced+aux runs.
//   * Walks (S2a) at exact_msg_db - 6: 3 of 12 ducked (LOST, 0.36 s after
//     the change); release (ramp start) - misalignment-oracle reconvergence
//     median 1.59 s, minimum 1.38 s, 0 early. At the limit - 6 no walk
//     ducked (D's pre-walk median -8.46 dB; the verdict lost for at most
//     0.22 s, under trip_hold_s) and none howled.
//   * Above the canceller's limit (+6, rehearsal -> hall): 0 howl blocks
//     guarded (5 strikes in 3 runs, no latch), 1203 unguarded.
//   * F -> 2F (S2b): every run ducks; the timer re-arm comes at 5.00 s
//     (10.01 s after a strike); 0 howl blocks; but the edge rule does NOT
//     stop a duck / open cycle there: the verdict reads ok again after the
//     re-arm, which re-arms LOST (16 LOST-ducks after a re-arm in 6 of 8
//     runs). An open question for the policy.
//   * A 20 s gap at digital zero: A' plateaus at -15.21 / -15.30 dB (not
//     ~0 dB), so restart_a_db (-1) never fires on silence; LOST ducks in the
//     gap and the re-arm opens again before the singer returns; 0 howl.
//   * Two mics, mic 1's path x4 at 10 s: the first TRIP ducks mic 1 alone in
//     7 of 8 runs, both in 1, the wrong mic alone in 0 (1 of 8 on macOS
//     arm64 CI, 1 of 20 in the sweep); 0 fallbacks, 0
//     cascades.
//   * Audible cost at the limit - 6 (0 / 2 / 5 Hz shift, 30 s): 0 ducks in
//     24 runs; the loudest residual block +19.56 dB against the +30 dB
//     ceiling.
//   * Dattorro (wet 0.5) in the reverb slot: with the bus stage the voice
//     0.5 s after a trip is 19.40 dB lower than without it.
//   * Cost per block and mic: 2246 ns float, 3180 ns double - 0.60 / 0.88 %
//     of one canceller's process_block.
// The policy defaults are the design note's; this PR's tables moved none
// of them (docs/howl-guard.md says what each row could and could not test).
//
// Real-time contract (as fd_kalman.h): the constructor validates, may throw
// std::invalid_argument, and allocates (M detectors, M x block_size gains).
// Every other entry point is noexcept and allocation-free; set_policy()
// clamps and never changes M, the block size or the detector's bands. The
// policy is double (host-facing configuration, converted once); no double
// arithmetic runs in the float hot path.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "mutap/howl_detector.h"

// No ABI tag: the guard holds howl detectors (resonator banks, no FFT), so
// its layout does not depend on the build's FFT engine (as howl_detector.h).
namespace tap::mu {

    /// The guard's per-mic state (see the file comment).
    enum class guard_state : std::uint8_t { arming, open, open_capped, ducked, releasing, latched };

    /// The guard's policy: every threshold and time. Defaults are the
    /// release experiment's and the design note's starting points;
    /// docs/howl-guard.md records what this library's tests measured with
    /// them.
    struct guard_policy {
        double                d_db             = -1.235;  ///< verdict ok needs D below this (dB)
        double                a_db             = -23.842; ///< ... and A' below this (dB)
        double                trip_hold_s      = 0.3;     ///< LOST: verdict not-ok this long after an ok
        double                release_hold_s   = 1.5;     ///< declarations and walk releases: ok held this long
        double                quiet_s          = 1.0;     ///< no detector verdict this long = quiet
        double                arming_duck_db   = 30.0;    ///< ARMING's gain is -this (and at most cap_db)
        double                arming_timeout_s = 10.0;    ///< ARMING without a declaration this long: unprotected
        double                duck_db          = 20.0;    ///< DUCKED sits this far under the restore target
        double                attack_ms        = 5.0;     ///< every gain decrease ramps over this
        double                release_ramp_ms  = 200.0;   ///< every gain increase ramps over this
        double                rearm_timeout_s  = 5.0;     ///< DUCKED re-arms after this x 2^strikes (and quiet)
        double                probation_s      = 10.0;    ///< a trip this soon after entering OPEN is a strike
        double                strike_db        = 3.0;     ///< each strike lowers the restore level by this
        size_t                max_strikes      = 3;       ///< this many strikes (outside ARMING) latch
        double                strike_decay_s   = 60.0;    ///< one strike decays per this long in OPEN without a trip
        double                attrib_db        = 6.0;     ///< attribution: mics within this of the loudest at the peak
        double                restart_a_db     = -1.0;    ///< A' at or above this: the canceller restarted -> ARMING
        std::optional<double> cap_db; ///< the host's ceiling (dry MSG - 6 dB, relative); none = unset
    };

    /// Per-mic gain policy over M canceller residuals. See the file comment.
    template <typename Sample>
    class howl_guard {
      public:
        struct config {
            size_t microphones = 1;       ///< M >= 1
            size_t block_size  = 64;      ///< samples per tick (the chain's)
            double sample_rate = 48000.0; ///< Hz; finite, > 0
            typename howl_detector<Sample>::config
                         detector; ///< every mic's; block_size and sample_rate are the guard's
            guard_policy policy;
        };

        /// The post-reverb bus stage (afc_stage): see the file comment.
        class bus_stage_type {
          public:
            void process_block(const Sample* in, Sample* out, size_t n) noexcept {
                const size_t  b = m_guard->m_block;
                const Sample* g = m_guard->m_bus_gain.data();
                for (size_t i = 0; i < n; ++i) {
                    out[i] = g[i < b ? i : b - 1] * in[i];
                }
            }

          private:
            friend class howl_guard;
            explicit bus_stage_type(const howl_guard* guard) noexcept
                : m_guard(guard) {}
            const howl_guard* m_guard;
        };

        /// @throws std::invalid_argument on microphones == 0, block_size ==
        ///         0, a bad sample rate, or a detector config the detector
        ///         rejects.
        explicit howl_guard(const config& cfg)
            : m_block(validated(cfg).block_size)
            , m_rate(cfg.sample_rate)
            , m_tick_s(static_cast<double>(cfg.block_size) / cfg.sample_rate)
            , m_bus(this) {
            auto dc        = cfg.detector;
            dc.block_size  = cfg.block_size;
            dc.sample_rate = cfg.sample_rate;
            m_detectors.reserve(cfg.microphones);
            for (size_t m = 0; m < cfg.microphones; ++m) {
                m_detectors.emplace_back(dc);
            }
            m_det_cfg = m_detectors.front().cfg();
            m_mics.resize(cfg.microphones);
            m_gain.resize(cfg.microphones * cfg.block_size);
            m_bus_gain.resize(cfg.block_size);
            m_policy = clamped(cfg.policy, guard_policy{});
            derive();
            reset();
        }

        // The bus stage points back at this object.
        howl_guard(const howl_guard&)            = delete;
        howl_guard& operator=(const howl_guard&) = delete;
        howl_guard(howl_guard&&)                 = delete;
        howl_guard& operator=(howl_guard&&)      = delete;
        ~howl_guard()                            = default;

        size_t              microphones() const noexcept { return m_mics.size(); }
        size_t              block_size() const noexcept { return m_block; }
        const guard_policy& policy() const noexcept { return m_policy; }

        /// Replace the policy between blocks (clamped; a non-finite field
        /// keeps its current value). State, strikes and timers carry over;
        /// targets follow on the next update().
        void set_policy(const guard_policy& p) noexcept {
            m_policy = clamped(p, m_policy);
            derive();
            on_cap_change();
        }

        /// The host's ceiling (relative dB, at most 0), or none. Removing it
        /// while a mic runs on it before a declaration (OPEN_CAPPED, or
        /// ducked / releasing toward the cap) sends that mic to ARMING.
        void set_cap_db(std::optional<double> cap_db) noexcept {
            guard_policy p = m_policy;
            p.cap_db       = cap_db;
            m_policy       = clamped(p, m_policy);
            derive();
            on_cap_change();
        }

        /// Every mic to ARMING as constructed: gains at the arming gain at
        /// once (no ramp), strikes, timers, counters and detectors cleared.
        void reset() noexcept {
            for (auto& d : m_detectors) {
                d.reset();
            }
            const Sample g  = arming_db();
            const Sample gl = db_to_lin(g);
            for (auto& s : m_mics) {
                s       = mic_state{};
                s.cur   = g;
                s.tgt   = g;
                s.tgt_l = gl;
                s.since = m_quiet_ticks; // quiet from the start
            }
            std::fill(m_gain.begin(), m_gain.end(), gl);
            std::fill(m_bus_gain.begin(), m_bus_gain.end(), Sample(1));
            m_episode   = false;
            m_fallbacks = 0;
        }

        /// The host clears the back-off: strikes 0, level 0 dB, the latch
        /// released (a LATCHED mic ramps up through RELEASING), the ARMING
        /// timeout restarted.
        void clear() noexcept {
            for (auto& s : m_mics) {
                s.strikes   = 0;
                s.level     = Sample(0);
                s.decay     = 0;
                s.timed_out = false;
                if (s.latched) {
                    s.latched = false;
                    if (s.state == guard_state::latched) {
                        enter(s, guard_state::releasing);
                    }
                }
                if (s.state == guard_state::arming) {
                    s.in_state = 0;
                }
            }
        }

        // ------------------------------------------------ the block's work

        /// Analyse mic `mic`'s residual (block_size() samples) with its
        /// canceller's statistics, linear as the canceller returns them
        /// (uncertainty_ratio(), shadow_residual_ratio()). Call for every
        /// mic, then update(). @pre mic < microphones()
        void analyze(size_t mic, const Sample* e, Sample uncertainty_ratio, Sample shadow_ratio) noexcept {
            m_detectors[mic].process_block(e, m_block);
            mic_state&       s    = m_mics[mic];
            constexpr Sample tiny = std::numeric_limits<Sample>::min();
            s.a_db                = Sample(10) * std::log10(std::max(uncertainty_ratio, tiny));
            s.d_db                = Sample(10) * std::log10(std::max(shadow_ratio, tiny));
        }

        /// One tick of every mic's policy from this block's analyses:
        /// triggers, attribution, state, and this block's gains.
        void update() noexcept {
            // Triggers per mic.
            bool any_trip = false;
            for (size_t m = 0; m < m_mics.size(); ++m) {
                mic_state&                   s = m_mics[m];
                const howl_detector<Sample>& d = m_detectors[m];
                s.ok                           = s.d_db < m_d_db && s.a_db < m_a_db;
                const howl_trigger tr          = d.trigger();
                const bool         growth      = tr == howl_trigger::growth;
                bool               catch_now   = tr == howl_trigger::ceiling || tr == howl_trigger::level;
                if (growth && m_det_cfg.level_catch) {
                    // The detector reports one path; a growth verdict can
                    // hide a level catch on the same tick.
                    catch_now =
                        d.power_db() >= m_det_cfg.ceiling_db
                        || (d.level_db() >= m_det_cfg.level_db && d.prominence_db() >= m_det_cfg.level_prominence_db);
                }
                s.raw_trip = catch_now || (growth && !s.ok);
                s.trip     = false;
                if (growth && s.ok && !catch_now) {
                    ++s.hints;
                }
                // Quiet: ticks since any detector verdict.
                s.since  = d.verdict() ? 0 : std::min(s.since + 1, k_big);
                any_trip = any_trip || s.raw_trip;
            }
            attribute(any_trip);
            for (auto& s : m_mics) {
                step(s);
            }
            render();
        }

        /// out = mic `mic`'s gains of this block x in (block_size() samples;
        /// in may equal out).
        void apply(size_t mic, const Sample* in, Sample* out) const noexcept {
            const Sample* g = &m_gain[mic * m_block];
            for (size_t i = 0; i < m_block; ++i) {
                out[i] = g[i] * in[i];
            }
        }

        /// bus += mic `mic`'s gains of this block x in.
        void accumulate(size_t mic, const Sample* in, Sample* bus) const noexcept {
            const Sample* g = &m_gain[mic * m_block];
            for (size_t i = 0; i < m_block; ++i) {
                bus[i] += g[i] * in[i];
            }
        }

        /// The post-reverb bus stage: afc_chain::set_safety(guard.bus_stage()).
        bus_stage_type* bus_stage() noexcept { return &m_bus; }

        // ------------------------------------------------------- readouts

        guard_state state(size_t mic) const noexcept { return m_mics[mic].state; }
        /// The gain at the end of the last block, dB relative to the host's.
        Sample gain_db(size_t mic) const noexcept { return m_mics[mic].cur; }
        /// This block's per-sample gains (block_size() values, linear).
        const Sample* gain_block(size_t mic) const noexcept { return &m_gain[mic * m_block]; }
        /// This block's post-reverb bus gains (linear).
        const Sample* bus_gain_block() const noexcept { return m_bus_gain.data(); }
        /// Seconds in the current state.
        double time_in_state_s(size_t mic) const noexcept {
            return static_cast<double>(m_mics[mic].in_state) * m_tick_s;
        }
        size_t strikes(size_t mic) const noexcept { return m_mics[mic].strikes; }
        /// The restore level after strikes, dB (0 or below).
        Sample level_db(size_t mic) const noexcept { return m_mics[mic].level; }
        bool   latched(size_t mic) const noexcept { return m_mics[mic].latched; }
        /// The canceller has not certified this mic: ARMING timed out,
        /// OPEN_CAPPED, or latched.
        bool unprotected(size_t mic) const noexcept {
            const mic_state& s = m_mics[mic];
            return s.timed_out || s.latched || s.state == guard_state::open_capped;
        }
        /// Whether the verdict has declared since reset (or the last restart).
        bool declared(size_t mic) const noexcept { return m_mics[mic].declared; }
        /// Last block's verdict and its inputs, dB.
        bool   verdict_ok(size_t mic) const noexcept { return m_mics[mic].ok; }
        Sample uncertainty_db(size_t mic) const noexcept { return m_mics[mic].a_db; }
        Sample shadow_db(size_t mic) const noexcept { return m_mics[mic].d_db; }
        /// Whether mic `mic` got a TRIP this tick (after attribution).
        bool tripped(size_t mic) const noexcept { return m_mics[mic].trip; }
        /// Counters since reset(): TRIPs applied, entries into DUCKED,
        /// timer re-arms, growth-while-ok hints.
        size_t trips(size_t mic) const noexcept { return m_mics[mic].trips; }
        size_t ducks(size_t mic) const noexcept { return m_mics[mic].ducks; }
        size_t rearms(size_t mic) const noexcept { return m_mics[mic].rearms; }
        size_t hints(size_t mic) const noexcept { return m_mics[mic].hints; }
        /// Attribution fallbacks (every mic ducked) since reset().
        size_t                       fallbacks() const noexcept { return m_fallbacks; }
        const howl_detector<Sample>& detector(size_t mic) const noexcept { return m_detectors[mic]; }

      private:
        static constexpr size_t k_big = std::numeric_limits<size_t>::max() / 4;

        struct mic_state {
            guard_state state = guard_state::arming;
            // gain ramp (dB), per sample
            Sample cur   = Sample(0); ///< gain at the end of the last block
            Sample tgt   = Sample(0);
            Sample tgt_l = Sample(1);
            Sample rate  = Sample(0); ///< dB per sample while ramping
            Sample ratio = Sample(1); ///< 10^(rate / 20)
            size_t left  = 0;         ///< ramp samples left
            // verdict
            Sample a_db = Sample(0);
            Sample d_db = Sample(0);
            bool   ok   = false;
            // timers, ticks
            size_t in_state  = 0;
            size_t ok_run    = 0;
            size_t notok_run = 0;
            size_t since     = 0; ///< since the last detector verdict
            size_t hold      = 0; ///< DUCKED: since entry or the last TRIP
            size_t probation = 0; ///< ticks of probation left
            size_t decay     = 0; ///< OPEN ticks without a trip toward a strike decay
            // flags
            bool ok_seen     = false; ///< an ok tick since the last re-arm
            bool fell        = false; ///< A' below a_db since entering ARMING (a restart needs it)
            bool declared    = false;
            bool latched     = false;
            bool latch_notok = false; ///< LATCHED: not-ok seen (a fresh edge can follow)
            bool timed_out   = false;
            bool raw_trip    = false; ///< this tick, before attribution
            bool trip        = false; ///< this tick, after attribution
            // back-off
            size_t strikes = 0;
            Sample level   = Sample(0);
            Sample floor   = Sample(0);
            // counters
            size_t trips  = 0;
            size_t ducks  = 0;
            size_t rearms = 0;
            size_t hints  = 0;
        };

        static config validated(const config& cfg) {
            if (cfg.microphones == 0) {
                throw std::invalid_argument("howl_guard: microphones must be >= 1");
            }
            if (cfg.block_size == 0) {
                throw std::invalid_argument("howl_guard: block_size must be >= 1");
            }
            if (!std::isfinite(cfg.sample_rate) || !(cfg.sample_rate > 0.0)) {
                throw std::invalid_argument("howl_guard: sample_rate must be finite and > 0");
            }
            return cfg;
        }

        /// p clamped field by field; a non-finite field takes prev's value.
        static guard_policy clamped(const guard_policy& p, const guard_policy& prev) noexcept {
            const auto pick = [](double v, double old, double lo, double hi) {
                return std::isfinite(v) ? std::clamp(v, lo, hi) : old;
            };
            constexpr double big = 1e6;
            guard_policy     q;
            q.d_db             = pick(p.d_db, prev.d_db, -120.0, 120.0);
            q.a_db             = pick(p.a_db, prev.a_db, -300.0, 120.0);
            q.trip_hold_s      = pick(p.trip_hold_s, prev.trip_hold_s, 0.0, big);
            q.release_hold_s   = pick(p.release_hold_s, prev.release_hold_s, 0.0, big);
            q.quiet_s          = pick(p.quiet_s, prev.quiet_s, 0.0, big);
            q.arming_duck_db   = pick(p.arming_duck_db, prev.arming_duck_db, 0.0, 120.0);
            q.arming_timeout_s = pick(p.arming_timeout_s, prev.arming_timeout_s, 0.0, big);
            q.duck_db          = pick(p.duck_db, prev.duck_db, 0.0, 120.0);
            q.attack_ms        = pick(p.attack_ms, prev.attack_ms, 0.0, 1e5);
            q.release_ramp_ms  = pick(p.release_ramp_ms, prev.release_ramp_ms, 0.0, 1e5);
            q.rearm_timeout_s  = pick(p.rearm_timeout_s, prev.rearm_timeout_s, 0.0, big);
            q.probation_s      = pick(p.probation_s, prev.probation_s, 0.0, big);
            q.strike_db        = pick(p.strike_db, prev.strike_db, 0.0, 60.0);
            q.max_strikes      = std::clamp<size_t>(p.max_strikes, 1, 16);
            q.strike_decay_s   = pick(p.strike_decay_s, prev.strike_decay_s, 0.0, big);
            q.attrib_db        = pick(p.attrib_db, prev.attrib_db, 0.0, 120.0);
            q.restart_a_db     = pick(p.restart_a_db, prev.restart_a_db, -300.0, 120.0);
            if (p.cap_db.has_value()) {
                const double old = prev.cap_db.value_or(0.0);
                q.cap_db         = pick(*p.cap_db, old, -120.0, 0.0);
            }
            return q;
        }

        size_t ticks(double seconds) const noexcept {
            const double t = std::ceil(seconds / m_tick_s - 1e-6); // 1.5 s at 750 ticks/s is 1125, not 1126
            return t < 1.0 ? size_t{1} : (t > 1e12 ? k_big : static_cast<size_t>(t));
        }
        size_t samples(double ms) const noexcept {
            const double n = std::round(ms * 1e-3 * m_rate);
            return n < 1.0 ? size_t{0} : static_cast<size_t>(n);
        }

        /// Policy -> ticks, samples and Sample thresholds.
        void derive() noexcept {
            const guard_policy& p = m_policy;
            m_d_db                = static_cast<Sample>(p.d_db);
            m_a_db                = static_cast<Sample>(p.a_db);
            m_restart_db          = static_cast<Sample>(p.restart_a_db);
            m_trip_ticks          = ticks(p.trip_hold_s);
            m_release_ticks       = ticks(p.release_hold_s);
            m_quiet_ticks         = p.quiet_s > 0.0 ? ticks(p.quiet_s) : 0;
            m_timeout_ticks       = ticks(p.arming_timeout_s);
            m_rearm_ticks         = ticks(p.rearm_timeout_s);
            m_probation_ticks     = p.probation_s > 0.0 ? ticks(p.probation_s) : 0;
            m_decay_ticks         = ticks(p.strike_decay_s);
            m_window_ticks        = ticks((p.attack_ms + 50.0) * 1e-3);
            m_attack              = samples(p.attack_ms);
            m_release             = samples(p.release_ramp_ms);
            m_duck                = static_cast<Sample>(p.duck_db);
            m_strike              = static_cast<Sample>(p.strike_db);
            m_attrib              = static_cast<Sample>(p.attrib_db);
            m_has_cap             = p.cap_db.has_value();
            m_cap                 = static_cast<Sample>(p.cap_db.value_or(0.0));
            m_arming              = static_cast<Sample>(-p.arming_duck_db);
        }

        static Sample db_to_lin(Sample db) noexcept { return std::pow(Sample(10), db / Sample(20)); }

        Sample arming_db() const noexcept { return m_has_cap ? std::min(m_arming, m_cap) : m_arming; }

        /// The gain RELEASING ramps to and OPEN / OPEN_CAPPED / LATCHED hold.
        Sample restore_db(const mic_state& s) const noexcept {
            if (s.latched) {
                return s.floor;
            }
            if (s.declared) {
                return s.level;
            }
            return m_has_cap ? std::min(m_cap, s.level) : arming_db();
        }

        Sample target_db(const mic_state& s) const noexcept {
            switch (s.state) {
            case guard_state::arming:
                return arming_db();
            case guard_state::ducked:
                return restore_db(s) - m_duck;
            case guard_state::open:
            case guard_state::open_capped:
            case guard_state::releasing:
            case guard_state::latched:
            default:
                return restore_db(s);
            }
        }

        void enter(mic_state& s, guard_state next) noexcept {
            s.state    = next;
            s.in_state = 0;
            switch (next) {
            case guard_state::ducked:
                s.hold    = 0;
                s.ok_run  = 0;
                s.ok_seen = false;
                ++s.ducks;
                break;
            case guard_state::open:
            case guard_state::open_capped:
                s.probation = m_probation_ticks;
                s.decay     = 0;
                break;
            case guard_state::latched:
                s.latch_notok = false;
                break;
            case guard_state::arming:
            case guard_state::releasing:
            default:
                break;
            }
        }

        /// Without a cap, a mic running on one before a declaration re-arms.
        void on_cap_change() noexcept {
            if (m_has_cap) {
                return;
            }
            for (auto& s : m_mics) {
                if (!s.declared && !s.latched && s.state != guard_state::arming) {
                    enter_arming(s);
                }
            }
        }

        /// ARMING after a restart: the declaration, the latch and the
        /// timeout are forgotten; strikes are kept.
        void enter_arming(mic_state& s) noexcept {
            enter(s, guard_state::arming);
            s.fell      = false;
            s.latched   = false;
            s.declared  = false;
            s.ok_seen   = false;
            s.timed_out = false;
            s.ok_run    = 0;
        }

        /// A strike: lowers the level, doubles the re-arm timeout; outside
        /// ARMING, reaching max_strikes latches.
        void strike(mic_state& s) noexcept {
            s.strikes = std::min(s.strikes + 1, size_t{30});
            s.level   = -m_strike * static_cast<Sample>(std::min(s.strikes, m_policy.max_strikes));
            s.decay   = 0;
            if (s.state != guard_state::arming && s.strikes >= m_policy.max_strikes && !s.latched) {
                s.latched = true;
                s.floor   = m_has_cap ? m_cap : s.level;
            }
        }

        /// Attribution of this tick's detector trips (file comment).
        void attribute(bool any_trip) noexcept {
            const size_t mics = m_mics.size();
            if (!any_trip) {
                if (m_episode && ++m_episode_quiet >= std::max<size_t>(m_quiet_ticks, 1)) {
                    m_episode = false;
                }
                return;
            }
            m_episode_quiet = 0;
            if (mics == 1) {
                m_mics[0].trip = true;
                return;
            }
            if (m_episode && m_episode_age >= m_window_ticks) {
                for (auto& s : m_mics) {
                    s.trip = true;
                }
                ++m_fallbacks;
                m_episode = false;
                return;
            }
            // The shared peak: the tripping mic with the largest block power.
            size_t ref  = 0;
            Sample best = -std::numeric_limits<Sample>::infinity();
            for (size_t m = 0; m < mics; ++m) {
                if (m_mics[m].raw_trip && m_detectors[m].power_db() > best) {
                    best = m_detectors[m].power_db();
                    ref  = m;
                }
            }
            const howl_detector<Sample>& rd   = m_detectors[ref];
            const Sample                 hz   = rd.peak_hz();
            size_t                       band = 0;
            for (size_t b = 0; b < rd.bands(); ++b) {
                if (rd.band_hz(b) == hz) {
                    band = b;
                    break;
                }
            }
            Sample loudest = -std::numeric_limits<Sample>::infinity();
            for (size_t m = 0; m < mics; ++m) {
                loudest = std::max(loudest, m_detectors[m].band_level_db(band));
            }
            bool left_out = false;
            for (size_t m = 0; m < mics; ++m) {
                const bool in  = m_detectors[m].band_level_db(band) >= loudest - m_attrib;
                m_mics[m].trip = in;
                left_out       = left_out || !in;
            }
            if (left_out && !m_episode) {
                m_episode     = true;
                m_episode_age = 0;
            }
        }

        /// One tick of one mic's state machine.
        void step(mic_state& s) noexcept {
            s.ok_run    = s.ok ? std::min(s.ok_run + 1, k_big) : 0;
            s.notok_run = s.ok ? 0 : std::min(s.notok_run + 1, k_big);
            if (s.ok) {
                s.ok_seen = true;
            }
            s.in_state       = std::min(s.in_state + 1, k_big);
            const bool quiet = s.since >= m_quiet_ticks;
            const bool lost  = s.declared && s.ok_seen && s.notok_run >= m_trip_ticks;
            const bool held  = s.ok_run >= m_release_ticks && quiet;
            if (s.trip) {
                ++s.trips;
            }

            if (s.a_db < m_a_db) {
                s.fell = true;
            }
            if (s.state != guard_state::arming && s.fell && s.a_db >= m_restart_db) {
                enter_arming(s);
                return;
            }
            switch (s.state) {
            case guard_state::arming:
                if (s.trip) {
                    strike(s);
                    s.ok_run = 0;
                }
                else if (held) {
                    s.declared  = true;
                    s.timed_out = false;
                    enter(s, guard_state::open);
                }
                else if (s.in_state >= m_timeout_ticks) {
                    s.timed_out = true;
                    if (m_has_cap && quiet) {
                        enter(s, guard_state::open_capped);
                    }
                }
                break;
            case guard_state::open:
                if (s.trip) {
                    if (s.probation > 0) {
                        strike(s);
                    }
                    enter(s, guard_state::ducked);
                }
                else if (lost) {
                    enter(s, guard_state::ducked);
                }
                else {
                    if (s.probation > 0) {
                        --s.probation;
                    }
                    if (s.strikes > 0 && ++s.decay >= m_decay_ticks) {
                        --s.strikes;
                        s.level = -m_strike * static_cast<Sample>(std::min(s.strikes, m_policy.max_strikes));
                        s.decay = 0;
                    }
                }
                break;
            case guard_state::open_capped:
                if (s.trip) {
                    if (s.probation > 0) {
                        strike(s);
                    }
                    enter(s, guard_state::ducked);
                }
                else if (held) {
                    s.declared  = true;
                    s.timed_out = false;
                    enter(s, guard_state::releasing);
                }
                else if (s.probation > 0) {
                    --s.probation;
                }
                break;
            case guard_state::ducked:
                if (s.trip) {
                    s.hold   = 0;
                    s.ok_run = 0;
                }
                else {
                    s.hold = std::min(s.hold + 1, k_big);
                    if (held) {
                        s.declared  = true;
                        s.timed_out = false;
                        enter(s, guard_state::releasing);
                    }
                    else if (quiet && s.hold >= rearm_ticks(s)) {
                        s.ok_seen   = false;
                        s.notok_run = 0;
                        ++s.rearms;
                        enter(s, guard_state::releasing);
                    }
                }
                break;
            case guard_state::releasing:
                if (s.trip) {
                    strike(s);
                    enter(s, guard_state::ducked);
                }
                else if (lost) {
                    enter(s, guard_state::ducked);
                }
                else if (s.left == 0 && s.cur == restore_db(s)) {
                    if (s.latched) {
                        enter(s, guard_state::latched);
                    }
                    else {
                        enter(s, s.declared ? guard_state::open : guard_state::open_capped);
                    }
                }
                break;
            case guard_state::latched:
                if (!s.ok) {
                    s.latch_notok = true;
                }
                if (s.trip) {
                    enter(s, guard_state::ducked);
                }
                else if (s.latch_notok && held) {
                    s.latched  = false;
                    s.declared = true;
                    enter(s, guard_state::releasing);
                }
                break;
            default:
                break;
            }
        }

        size_t rearm_ticks(const mic_state& s) const noexcept {
            const size_t shift = std::min(s.strikes, size_t{30});
            return m_rearm_ticks > (k_big >> shift) ? k_big : m_rearm_ticks << shift;
        }

        /// This block's per-sample gains: every mic's ramp toward its
        /// state's target, and the bus stage's gain.
        void render() noexcept {
            const size_t b = m_block;
            if (m_episode) {
                m_episode_age = std::min(m_episode_age + 1, k_big);
            }
            std::fill(m_bus_gain.begin(), m_bus_gain.end(), Sample(1));
            for (size_t m = 0; m < m_mics.size(); ++m) {
                mic_state&   s   = m_mics[m];
                const Sample tgt = target_db(s);
                if (tgt != s.tgt) {
                    s.tgt          = tgt;
                    s.tgt_l        = db_to_lin(tgt);
                    const size_t n = (tgt < s.cur) ? m_attack : m_release;
                    if (n == 0) {
                        s.cur  = tgt;
                        s.left = 0;
                    }
                    else {
                        s.rate  = (tgt - s.cur) / static_cast<Sample>(n);
                        s.ratio = db_to_lin(s.rate);
                        s.left  = n;
                    }
                }
                Sample* g = &m_gain[m * b];
                size_t  i = 0;
                if (s.left > 0) {
                    const size_t len = std::min(b, s.left);
                    // Sample i carries the ramp's (i + 1)-th step; the block
                    // re-anchors from cur, so rounding never accumulates past
                    // one block, and the last step lands on the target.
                    Sample x = db_to_lin(s.cur + s.rate);
                    for (; i < len; ++i) {
                        g[i] = x;
                        x *= s.ratio;
                    }
                    s.left -= len;
                    if (s.left == 0) {
                        s.cur      = s.tgt;
                        g[len - 1] = s.tgt_l;
                    }
                    else {
                        s.cur += s.rate * static_cast<Sample>(len);
                    }
                }
                for (; i < b; ++i) {
                    g[i] = s.tgt_l;
                }
                if (s.state == guard_state::ducked || s.state == guard_state::releasing) {
                    const Sample inv = Sample(1) / db_to_lin(restore_db(s));
                    for (size_t k = 0; k < b; ++k) {
                        m_bus_gain[k] = std::min(m_bus_gain[k], std::min(g[k] * inv, Sample(1)));
                    }
                }
            }
        }

        size_t                                 m_block;
        double                                 m_rate;
        double                                 m_tick_s;
        std::vector<howl_detector<Sample>>     m_detectors;
        typename howl_detector<Sample>::config m_det_cfg;
        std::vector<mic_state>                 m_mics;
        std::vector<Sample>                    m_gain;     ///< M x block, linear
        std::vector<Sample>                    m_bus_gain; ///< block, linear
        bus_stage_type                         m_bus;
        guard_policy                           m_policy;

        // derived from the policy
        Sample m_d_db            = Sample(0);
        Sample m_a_db            = Sample(0);
        Sample m_restart_db      = Sample(0);
        Sample m_duck            = Sample(0);
        Sample m_strike          = Sample(0);
        Sample m_attrib          = Sample(0);
        Sample m_cap             = Sample(0);
        Sample m_arming          = Sample(0);
        bool   m_has_cap         = false;
        size_t m_trip_ticks      = 1;
        size_t m_release_ticks   = 1;
        size_t m_quiet_ticks     = 0;
        size_t m_timeout_ticks   = 1;
        size_t m_rearm_ticks     = 1;
        size_t m_probation_ticks = 0;
        size_t m_decay_ticks     = 1;
        size_t m_window_ticks    = 1;
        size_t m_attack          = 0;
        size_t m_release         = 0;

        // attribution episode
        bool   m_episode       = false;
        size_t m_episode_age   = 0;
        size_t m_episode_quiet = 0;
        size_t m_fallbacks     = 0;
    };

} // namespace tap::mu
