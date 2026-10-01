/// @file guard_runs.h
/// @brief The safety layer's measurement runs on guard_loop.h: the
///        canceller's bisected limit (the operating point), and live runs
///        from reset with per-block traces of the guard and the oracles.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Host-only: shared by tests/test_howl_guard_host.cpp's gated rows and its
// MUTAP_SLOW sweep, so a row means the same measurement in both.
//
// THE OPERATING POINT. limit_db() bisects the converged canceller's runaway
// limit in guard_loop's loop as karaoke_asg.h / two_mic_loop.h do: 1500
// blocks of convergence at exact_msg_db - 6 dB on the material's seed, then
// the forward gain bisected (0.25 dB) on 10 s of seed + 10 with a fresh copy
// of the converged cancellers and a fresh loop per probe; howling = a
// residual block at RMS >= 100. The guard's protection claims run at that
// limit - 6 dB; the stress rows at exact_msg_db + 3 / + 6. cap_db is set as
// the protocol would: the dry limit - 6 dB relative to the operating gain,
// cap = (exact_msg_db - 6) - K.
//
// A LIVE RUN starts from reset (cold cancellers, the guard in ARMING) at the
// operating gain and records, per block: the largest residual RMS (the 40 dB
// rule), each mic's guard state, gain and verdict, and (every k_oracle
// blocks, where defined) the frozen-estimate margin and the misalignment.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "guard_loop.h"
#include "two_mic_loop.h"

namespace mutap_test::guard {

    inline const char* state_name(tap::mu::guard_state s) {
        switch (s) {
        case tap::mu::guard_state::arming:
            return "ARMING";
        case tap::mu::guard_state::open:
            return "OPEN";
        case tap::mu::guard_state::open_capped:
            return "OPEN_CAPPED";
        case tap::mu::guard_state::ducked:
            return "DUCKED";
        case tap::mu::guard_state::releasing:
            return "RELEASING";
        case tap::mu::guard_state::latched:
        default:
            return "LATCHED";
        }
    }

    /// Converged-canceller runaway limit, dB (file comment).
    inline double limit_db(const std::string& room, const material_spec& mat, size_t delay, unsigned seed,
                           double tol_db = 0.25) {
        const auto                            path  = karaoke::room(room);
        const double                          exact = exact_msg_db(path, delay);
        const size_t                          conv  = 1500;
        const size_t                          probe = blocks_of(10.0);
        const auto                            vc    = singer(mat, conv * k_block, seed);
        const auto                            vp    = singer(mat, probe * k_block, seed + 10);
        const auto                            aux   = backing_track(seed);
        typename guarded_loop<double>::config cfg;
        cfg.paths   = {path};
        cfg.delay   = delay;
        cfg.gain_db = exact - 6.0;
        if (mat.aux) {
            cfg.aux    = &aux;
            cfg.aux_db = mat.aux_db;
        }
        guarded_loop<double> converged(cfg, nullptr);
        for (size_t blk = 0; blk < conv; ++blk) {
            const double* x[1] = {&vc[blk * k_block]};
            converged.step(x);
        }
        auto howls = [&](double g) {
            auto c    = cfg;
            c.gain_db = g;
            guarded_loop<double> loop(c, nullptr);
            loop.load_cancellers(converged);
            for (size_t blk = 0; blk < probe; ++blk) {
                const double* x[1] = {&vp[blk * k_block]};
                if (loop.step(x) >= 100.0) {
                    return true;
                }
            }
            return false;
        };
        return two_mic::bisect(howls, exact - 15.0, exact + 30.0, tol_db);
    }

    /// One single-mic live run's set-up.
    struct run_spec {
        std::string                    room = "cabin";
        std::string                    room2;             ///< S2a: the walk's destination ("" = none)
        double                         scale2     = 1.0;  ///< S2b: F -> scale2 x F at t_change (room2 empty)
        double                         t_change_s = -1.0; ///< < 0: no change
        material_spec                  mat;
        unsigned                       seed     = 1;
        size_t                         delay    = k_s1;
        double                         gain_db  = 0.0; ///< K, absolute
        double                         shift_hz = 0.0;
        double                         seconds  = 20.0;
        bool                           guarded  = true;
        std::optional<double>          cap_db; ///< relative; none = unset
        tap::mu::guard_policy          policy;
        bool                           oracle   = true;
        double                         t_step_s = -1.0; ///< >= 0: K steps by step_db here (a forced howl)
        double                         step_db  = 0.0;
        tap::mu::afc_stage_ref<double> reverb;            ///< the reverb slot (caller-owned); null = none
        bool                           bus_stage  = true; ///< attach the guard's post-reverb stage
        double                         gap_from_s = -1.0; ///< >= 0: singer and backing track silent (digital zero) ...
        double                         gap_s      = 0.0;  ///< ... for this long (a gap between songs)
    };

    /// Per-block traces of one run (mic 0 for the guard columns).
    struct run_trace {
        std::vector<float>                e_rms;     ///< largest residual RMS
        std::vector<float>                spk_rms;   ///< speaker RMS
        std::vector<float>                voice_rms; ///< speaker minus the backing track
        std::vector<tap::mu::guard_state> state;
        std::vector<float>                gain_db;
        std::vector<char>                 ok;
        std::vector<float>                a_db;  ///< A', dB
        std::vector<float>                d_db;  ///< D, dB
        std::vector<char>                 trip;  ///< a TRIP applied this block
        std::vector<char>                 rearm; ///< a timer re-arm this block
        std::vector<long>                 o_blk; ///< oracle blocks
        std::vector<float>                o_margin;
        std::vector<float>                o_mis;
        long                              change  = -1; ///< the change block
        size_t                            strikes = 0;
        bool                              latched = false;
        size_t                            rearms  = 0;
        size_t                            hints   = 0;
        size_t                            size() const { return e_rms.size(); }
    };

    inline run_trace live_run(const run_spec& s) {
        const size_t n_blk = blocks_of(s.seconds);
        auto         v     = singer(s.mat, n_blk * k_block, s.seed);
        auto         aux   = backing_track(s.seed);
        if (s.gap_from_s >= 0.0) {
            // The track unrolled to the run's length (the loop reads it
            // cyclically: the same samples), then both silenced in the gap.
            std::vector<double> full(n_blk * k_block);
            for (size_t i = 0; i < full.size(); ++i) {
                full[i] = aux[i % aux.size()];
            }
            aux             = std::move(full);
            const size_t g0 = blocks_of(s.gap_from_s) * k_block;
            const size_t g1 = std::min(v.size(), blocks_of(s.gap_from_s + s.gap_s) * k_block);
            for (size_t i = g0; i < g1; ++i) {
                v[i]   = 0.0;
                aux[i] = 0.0;
            }
        }
        typename guarded_loop<double>::config cfg;
        cfg.paths    = {karaoke::room(s.room)};
        cfg.delay    = s.delay;
        cfg.gain_db  = s.gain_db;
        cfg.shift_hz = s.shift_hz;
        if (s.mat.aux) {
            cfg.aux    = &aux;
            cfg.aux_db = s.mat.aux_db;
        }
        if (s.t_change_s >= 0.0) {
            cfg.change_block = static_cast<long>(blocks_of(s.t_change_s));
            if (!s.room2.empty()) {
                cfg.paths2 = {karaoke::room(s.room2)};
            }
            else {
                cfg.change_scale = s.scale2;
            }
        }
        auto gc          = guard_config<double>(1, s.delay);
        gc.policy        = s.policy;
        gc.policy.cap_db = s.cap_db;
        guard_t<double>      g(gc);
        guarded_loop<double> loop(cfg, s.guarded ? &g : nullptr, s.reverb, s.bus_stage);
        const auto           step_blk = s.t_step_s >= 0.0 ? static_cast<long>(blocks_of(s.t_step_s)) : -1L;
        run_trace            t;
        t.change = cfg.change_block;
        t.e_rms.reserve(n_blk);
        size_t rearms = 0;
        for (size_t blk = 0; blk < n_blk; ++blk) {
            if (static_cast<long>(blk) == step_blk) {
                loop.set_gain_db(s.gain_db + s.step_db);
            }
            const double* x[1] = {&v[blk * k_block]};
            t.e_rms.push_back(static_cast<float>(loop.step(x)));
            t.spk_rms.push_back(static_cast<float>(loop.speaker_rms()));
            t.voice_rms.push_back(static_cast<float>(loop.voice_rms()));
            t.state.push_back(g.state(0));
            t.gain_db.push_back(s.guarded ? static_cast<float>(g.gain_db(0)) : 0.0F);
            t.ok.push_back(g.verdict_ok(0) ? 1 : 0);
            t.a_db.push_back(static_cast<float>(g.uncertainty_db(0)));
            t.d_db.push_back(static_cast<float>(g.shadow_db(0)));
            t.trip.push_back(g.tripped(0) ? 1 : 0);
            t.rearm.push_back(g.rearms(0) > rearms ? 1 : 0);
            rearms = g.rearms(0);
            if (s.oracle && loop.margin_defined() && (blk % k_oracle == 0)) {
                t.o_blk.push_back(static_cast<long>(blk));
                t.o_margin.push_back(static_cast<float>(loop.frozen_margin_db()));
                t.o_mis.push_back(static_cast<float>(loop.misalignment_db(0)));
            }
        }
        t.strikes = g.strikes(0);
        t.latched = g.latched(0);
        t.rearms  = g.rearms(0);
        t.hints   = g.hints(0);
        return t;
    }

    // ------------------------------------------------------------ two mics

    /// The two-mic loop's material: two_mic_loop.h's separated singers
    /// (300 / 200 Hz) with -10 dB cross leakage, optionally the backing
    /// track at -12 dB.
    struct two_mic_material {
        std::vector<std::vector<double>> x; ///< per mic: own singer + the other's leakage
    };

    inline two_mic_material two_mic_singers(size_t blocks, unsigned seed) {
        namespace tm  = two_mic;
        const auto s0 = tm::singer(tm::singers::separated, 0, blocks, seed);
        const auto s1 = tm::singer(tm::singers::separated, 1, blocks, seed);
        return {{tm::near_end_at(0, s0, s1, -10.0), tm::near_end_at(1, s1, s0, -10.0)}};
    }

    /// The two-mic loop's converged-chain limit, bisected as limit_db() on
    /// guard_loop with both mics.
    inline double limit2_db(const std::string& pair, bool aux, size_t delay, unsigned seed, double tol_db = 0.25) {
        const auto                            paths = two_mic::room_pair(pair);
        const double                          exact = exact_msg_db(two_mic::path_sum(paths), delay);
        const size_t                          conv  = 1500;
        const size_t                          probe = blocks_of(10.0);
        const auto                            vc    = two_mic_singers(conv, seed);
        const auto                            vp    = two_mic_singers(probe, seed + 10);
        const auto                            track = backing_track(seed);
        typename guarded_loop<double>::config cfg;
        cfg.paths   = paths;
        cfg.delay   = delay;
        cfg.gain_db = exact - 6.0;
        if (aux) {
            cfg.aux = &track;
        }
        guarded_loop<double> converged(cfg, nullptr);
        for (size_t blk = 0; blk < conv; ++blk) {
            const double* x[2] = {&vc.x[0][blk * k_block], &vc.x[1][blk * k_block]};
            converged.step(x);
        }
        auto howls = [&](double g) {
            auto c    = cfg;
            c.gain_db = g;
            guarded_loop<double> loop(c, nullptr);
            loop.load_cancellers(converged);
            for (size_t blk = 0; blk < probe; ++blk) {
                const double* x[2] = {&vp.x[0][blk * k_block], &vp.x[1][blk * k_block]};
                if (loop.step(x) >= 100.0) {
                    return true;
                }
            }
            return false;
        };
        return two_mic::bisect(howls, exact - 15.0, exact + 30.0, tol_db);
    }

    /// One two-mic live run: from reset at `gain_db`, mic 1's path scaled by
    /// `force_scale` at `t_force_s` (a single-mic howl: that mic moved toward
    /// the speaker).
    struct two_mic_spec {
        std::string           pair        = "cabin";
        bool                  aux         = false;
        unsigned              seed        = 1;
        size_t                delay       = k_s1;
        double                gain_db     = 0.0;
        double                t_force_s   = 10.0;
        double                force_scale = 4.0;
        double                seconds     = 20.0;
        std::optional<double> cap_db;
    };

    struct two_mic_trace {
        std::vector<float>                             e_rms; ///< largest residual RMS
        std::vector<std::vector<tap::mu::guard_state>> state; ///< per mic
        std::vector<std::vector<char>>                 trip;  ///< per mic: a TRIP applied
        std::vector<std::vector<char>>                 ok;    ///< per mic: verdict
        long                                           force     = 0;
        size_t                                         fallbacks = 0;
        size_t                                         size() const { return e_rms.size(); }
    };

    inline two_mic_trace two_mic_run(const two_mic_spec& s) {
        const size_t                          n_blk = blocks_of(s.seconds);
        const auto                            v     = two_mic_singers(n_blk, s.seed);
        const auto                            track = backing_track(s.seed);
        const auto                            paths = two_mic::room_pair(s.pair);
        typename guarded_loop<double>::config cfg;
        cfg.paths        = paths;
        cfg.paths2       = paths;
        cfg.change_block = static_cast<long>(blocks_of(s.t_force_s));
        for (auto& f : cfg.paths2[1]) {
            f *= s.force_scale;
        }
        cfg.delay   = s.delay;
        cfg.gain_db = s.gain_db;
        if (s.aux) {
            cfg.aux = &track;
        }
        auto gc          = guard_config<double>(2, s.delay);
        gc.policy.cap_db = s.cap_db;
        guard_t<double>      g(gc);
        guarded_loop<double> loop(cfg, &g);
        two_mic_trace        t;
        t.force = cfg.change_block;
        t.state.resize(2);
        t.trip.resize(2);
        t.ok.resize(2);
        for (size_t blk = 0; blk < n_blk; ++blk) {
            const double* x[2] = {&v.x[0][blk * k_block], &v.x[1][blk * k_block]};
            t.e_rms.push_back(static_cast<float>(loop.step(x)));
            for (size_t m = 0; m < 2; ++m) {
                t.state[m].push_back(g.state(m));
                t.trip[m].push_back(g.tripped(m) ? 1 : 0);
                t.ok[m].push_back(g.verdict_ok(m) ? 1 : 0);
            }
        }
        t.fallbacks = g.fallbacks();
        return t;
    }

    // ------------------------------------------------------------ metrics

    inline double block_s() {
        return static_cast<double>(k_block) / k_fs;
    }

    /// First block at or after `from` in state `st` (-1: none).
    inline long first_state(const run_trace& t, tap::mu::guard_state st, long from = 0) {
        for (auto i = static_cast<size_t>(std::max(from, 0L)); i < t.size(); ++i) {
            if (t.state[i] == st) {
                return static_cast<long>(i);
            }
        }
        return -1;
    }

    /// [from, to) clamped to [0, v.size()] as iterators. The [from, to)
    /// helpers below take an open-ended `to` (SIZE_MAX by default); walking
    /// iterators bounded by the vector's own end, not an index loop against
    /// min(to, size), keeps GCC 13's -Waggressive-loop-optimizations from
    /// proving an out-of-range iteration (it bounds size() only by the
    /// pointer difference / sizeof(T), 2^62 for a float).
    template <typename T>
    std::pair<typename std::vector<T>::const_iterator, typename std::vector<T>::const_iterator>
    clamped_range(const std::vector<T>& v, size_t from, size_t to) {
        const size_t end   = std::min(to, v.size());
        const size_t begin = std::min(from, end);
        return {v.begin() + static_cast<std::ptrdiff_t>(begin), v.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    /// Blocks in state `st` over [from, to).
    inline size_t blocks_in(const run_trace& t, tap::mu::guard_state st, size_t from = 0, size_t to = ~size_t{0}) {
        const auto [b, e] = clamped_range(t.state, from, to);
        return static_cast<size_t>(std::count(b, e, st));
    }

    /// Blocks at the 40 dB rule over [from, to).
    inline size_t howl_blocks(const run_trace& t, size_t from = 0, size_t to = ~size_t{0}) {
        const auto [b, e] = clamped_range(t.e_rms, from, to);
        return static_cast<size_t>(std::count_if(b, e, [](float r) { return r >= 100.0F; }));
    }

    /// Seconds the frozen-estimate margin was below 0 dB over [from, to)
    /// (each oracle sample stands for k_oracle blocks).
    inline double unstable_s(const run_trace& t, size_t from = 0, size_t to = ~size_t{0}) {
        size_t n = 0;
        for (size_t k = 0; k < t.o_blk.size(); ++k) {
            const auto b = static_cast<size_t>(t.o_blk[k]);
            if (b >= from && b < to && t.o_margin[k] < 0.0F) {
                ++n;
            }
        }
        return static_cast<double>(n * k_oracle) * block_s();
    }

    /// Longest run of consecutive blocks at the 40 dB rule, seconds.
    inline double longest_howl_s(const run_trace& t, size_t from = 0) {
        const auto [b, e] = clamped_range(t.e_rms, from, t.e_rms.size());
        size_t best       = 0;
        size_t cur        = 0;
        for (auto it = b; it != e; ++it) {
            cur  = *it >= 100.0F ? cur + 1 : 0;
            best = std::max(best, cur);
        }
        return static_cast<double>(best) * block_s();
    }

    /// Entries into state `st` over [from, to): blocks in `st` whose
    /// predecessor is not.
    inline size_t entries(const run_trace& t, tap::mu::guard_state st, size_t from = 0, size_t to = ~size_t{0}) {
        const auto [b, e] = clamped_range(t.state, std::max<size_t>(from, 1), to);
        size_t n          = 0;
        for (auto it = b; it != e; ++it) {
            n += (*it == st && *(it - 1) != st) ? 1U : 0U;
        }
        return n;
    }

    /// The burst oracle (howl_runs.h): block i lies within 0.5 s of a block
    /// at RMS >= 10.
    inline std::vector<char> bursts(const run_trace& t) {
        constexpr long    k_tail = 375;
        const size_t      n      = t.size();
        std::vector<char> out(n, 0);
        long              last = -1000000;
        for (size_t i = 0; i < n; ++i) {
            if (t.e_rms[i] >= 10.0F) {
                last = static_cast<long>(i);
            }
            out[i] = (static_cast<long>(i) - last <= k_tail) ? 1 : 0;
        }
        long next = 1L << 40;
        for (size_t i = n; i-- > 0;) {
            if (t.e_rms[i] >= 10.0F) {
                next = static_cast<long>(i);
            }
            out[i] = static_cast<char>(out[i] | ((next - static_cast<long>(i) <= k_tail) ? 1 : 0));
        }
        return out;
    }

    /// The misalignment oracle after a change at block c: mismatch when the
    /// misalignment exceeds the median over [c - 2 s, c) + 3 dB; returns the
    /// first block after c starting a 0.3 s run back under it (-1: never;
    /// -2: never detected the change).
    inline long reconverged(const run_trace& t, long c) {
        std::vector<double> base;
        for (size_t k = 0; k < t.o_blk.size(); ++k) {
            if (t.o_blk[k] >= c - static_cast<long>(blocks_of(2.0)) && t.o_blk[k] < c) {
                base.push_back(t.o_mis[k]);
            }
        }
        if (base.empty()) {
            return -1;
        }
        const double thr      = median(base) + 3.0;
        const auto   debounce = static_cast<long>(blocks_of(0.3));
        bool         detected = false;
        long         start    = -1;
        for (size_t k = 0; k < t.o_blk.size(); ++k) {
            if (t.o_blk[k] < c) {
                continue;
            }
            const bool mis = t.o_mis[k] > thr;
            if (mis) {
                detected = true;
                start    = -1;
            }
            else if (detected) {
                if (start < 0) {
                    start = t.o_blk[k];
                }
                if (t.o_blk[k] - start >= debounce) {
                    return start;
                }
            }
        }
        return detected ? -1 : -2;
    }

} // namespace mutap_test::guard
