// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The safety layer's live claims (mutap/howl_guard.h), host-only: the guard
// in afc_chain's loop (tests/support/guard_loop.h), from reset, against
// guard-independent oracles (tests/support/guard_runs.h): the harness's 40 dB
// rule on each residual, the frozen-estimate margin, the misalignment oracle
// and the burst oracle. Double precision, PEM + FD-Kalman with the 2-partition
// shadow, 1024 taps, block 64 at 48 kHz, S1 (480 samples) unless a row says
// S3 (960).
//
// OPERATING POINTS, stated per row: the converged canceller's limit - 6 dB
// (k_ops below: the median over seed sets of gd::limit_db, re-measured by
// HowlGuardSweep.OperatingPoints) for the protection claims; exact_msg_db
// + 3 / + 6 dB (over the dry limit) and the limit + 6 dB for the stress rows;
// exact_msg_db - 6 dB (the release experiment's point) for the walk timing.
// The host's cap is set as the protocol would: the dry limit - 6 dB, relative.
//
// The gated rows (HowlGuardHost.*) run seeds 1, 21, 41 (or 1, 21) on cabin
// and mt5 (both generator families); each takes about 90 s or less on 4 threads
// (877.17 s for all 16 in one run on the Intel Mac). HowlGuardSweep.*
// (MUTAP_SLOW=1) re-measures the operating points and runs the long grids
// (six rooms, five seed sets) that docs/howl-guard.md quotes, including the
// soundcheck margin grid (CalibrationMargins: shadow guards, see
// gd::run_spec::shadows). Every threshold below is a
// measured number with margin, the measurement in the comment beside it;
// chaotic rows are gated as directions and medians, never single runs.
//
// Host for every number here: macOS 15.7 x86_64 (i9-8950HK), AppleClang 17,
// Release, a shared machine.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "faust_generated.h"
#include "support/guard_runs.h"

namespace {

    namespace gd = mutap_test::guard;
    namespace hw = mutap_test::howl;
    using mutap_test::median;
    using tap::mu::guard_state;

    bool slow_enabled() {
        const char* v = std::getenv("MUTAP_SLOW");
        return v != nullptr && std::string(v) == "1";
    }

    /// At most 4 workers by default (the suites share their machines);
    /// MUTAP_SLOW_THREADS overrides.
    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw_threads = std::thread::hardware_concurrency();
        return std::clamp(hw_threads > 1 ? hw_threads - 1 : 1U, 1U, 4U);
    }

    void run_parallel(const std::vector<std::function<void()>>& jobs) {
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < worker_count(); ++t) {
            pool.emplace_back([&] {
                for (size_t j = next++; j < jobs.size(); j = next++) {
                    jobs[j]();
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
    }

    double max_of(const std::vector<double>& v) {
        return v.empty() ? std::nan("") : *std::max_element(v.begin(), v.end());
    }
    double min_of(const std::vector<double>& v) {
        return v.empty() ? std::nan("") : *std::min_element(v.begin(), v.end());
    }
    double med(const std::vector<double>& v) {
        return v.empty() ? std::nan("") : median(v);
    }
    double secs(long blocks) {
        return static_cast<double>(blocks) * gd::block_s();
    }

    // ---------------------------------------------------- materials

    gd::material_spec mat(hw::material m, bool aux) {
        gd::material_spec s;
        s.mat = m;
        s.aux = aux;
        return s;
    }
    const gd::material_spec              k_v         = mat(hw::material::voiced, true);  ///< V: voiced + backing track
    const gd::material_spec              k_h         = mat(hw::material::voiced, false); ///< H: the held note alone
    const gd::material_spec              k_m         = mat(hw::material::music, false);  ///< M: music, no backing track
    const gd::material_spec              k_m_aux     = mat(hw::material::music, true);
    const gd::material_spec              k_sp        = mat(hw::material::speech, false);
    const gd::material_spec              k_sp_aux    = mat(hw::material::speech, true);
    const std::vector<gd::material_spec> k_cold_mats = {k_v, k_h, k_m, k_m_aux, k_sp, k_sp_aux};

    // ---------------------------------------------------- operating points

    /// The converged canceller's runaway limit over exact_msg_db (dB), the
    /// median over seed sets 1, 21, 41, 61, 81 (gd::limit_db; printed by
    /// HowlGuardSweep.OperatingPoints, which re-measures it). The guard's
    /// protection claims run at this - 6 dB. NaN: not measured.
    struct op_row {
        const char* room;
        const char* material;
        double      s1;
        double      s3;
    };
    const double k_nan = std::numeric_limits<double>::quiet_NaN();
    // OPERATING_POINTS_TABLE
    const op_row k_ops[] = {
        {"cabin", "voiced+aux", 11.89, 13.12},    {"cabin", "voiced", 9.26, 14.88},
        {"cabin", "music", 10.14, 15.76},         {"cabin", "music+aux", 9.96, 13.48},
        {"cabin", "speech", 19.63, 20.16},        {"cabin", "speech+aux", 19.45, 19.98},
        {"mt5", "voiced+aux", 11.02, 12.42},      {"mt5", "voiced", 11.02, 15.76},
        {"mt5", "music", 11.19, 16.99},           {"mt5", "music+aux", 11.89, 16.11},
        {"mt5", "speech", 22.62, 22.62},          {"mt5", "speech+aux", 21.56, 22.62},
        {"studio", "voiced+aux", 9.08, k_nan},    {"studio", "voiced", 10.84, k_nan},
        {"studio", "music", 10.84, k_nan},        {"studio", "music+aux", 11.02, k_nan},
        {"studio", "speech", 19.28, k_nan},       {"studio", "speech+aux", 18.93, k_nan},
        {"rehearsal", "voiced+aux", 9.61, k_nan}, {"rehearsal", "voiced", 10.66, k_nan},
        {"rehearsal", "music", 12.07, k_nan},     {"rehearsal", "music+aux", 11.54, k_nan},
        {"rehearsal", "speech", 15.06, k_nan},    {"rehearsal", "speech+aux", 13.83, k_nan},
        {"hall", "voiced+aux", 9.26, k_nan},      {"hall", "voiced", 7.32, k_nan},
        {"hall", "music", 11.89, k_nan},          {"hall", "music+aux", 11.37, k_nan},
        {"hall", "speech", 19.63, k_nan},         {"hall", "speech+aux", 19.98, k_nan},
        {"mt9", "voiced+aux", 8.73, k_nan},       {"mt9", "voiced", 10.31, k_nan},
        {"mt9", "music", 11.19, k_nan},           {"mt9", "music+aux", 11.02, k_nan},
        {"mt9", "speech", 21.39, k_nan},          {"mt9", "speech+aux", 20.51, k_nan},
    };

    double asg_db(const std::string& room, const gd::material_spec& m, size_t delay) {
        const std::string name = gd::material_name(m);
        for (const auto& r : k_ops) {
            if (room == r.room && name == r.material) {
                return delay == gd::k_s1 ? r.s1 : r.s3;
            }
        }
        return std::nan("");
    }

    double exact_db(const std::string& room, size_t delay) {
        return mutap_test::exact_msg_db(mutap_test::karaoke::room(room), delay);
    }

    /// limit - 6 dB, absolute.
    double operating_gain(const std::string& room, const gd::material_spec& m, size_t delay) {
        return exact_db(room, delay) + asg_db(room, m, delay) - 6.0;
    }

    /// The protocol's cap: the dry limit - 6 dB, relative to the operating gain.
    double cap_for(double exact, double gain_db) {
        return (exact - 6.0) - gain_db;
    }

    /// The two-mic loop's limit over exact_msg_db(sum F) (dB), the median
    /// over the same seed sets (gd::limit2_db), without / with the backing
    /// track.
    struct op2_row {
        const char* pair;
        double      plain;
        double      aux;
    };
    // OPERATING_POINTS_TWO_MIC
    const op2_row k_ops2[] = {
        {"cabin", 9.43, 11.72},
        {"mt5+mt9", 8.55, 8.73},
    };

    const std::vector<unsigned> k_seeds3 = {1, 21, 41};
    const std::vector<unsigned> k_seeds5 = {1, 21, 41, 61, 81};

    // ---------------------------------------------------- cold start

    struct cold_stats {
        std::string         label;
        size_t              runs = 0;
        std::vector<double> t_open;        ///< declared runs: first OPEN, s
        size_t              never  = 0;    ///< never left ARMING
        size_t              capped = 0;    ///< reached OPEN_CAPPED
        std::vector<double> t_capped;      ///< ... first OPEN_CAPPED, s
        std::vector<double> t_unprot;      ///< first block with unprotected() raised, s
        size_t              ducks     = 0; ///< DUCKED entries
        size_t              duck_runs = 0;
        size_t              off_burst = 0; ///< ... not within 0.5 s of a block at +20 dB (the burst oracle)
        size_t              off_runs  = 0;
        std::vector<double> arming_s;      ///< time in ARMING, s
        size_t              howl      = 0; ///< blocks at the 40 dB rule
        size_t              howl_runs = 0;
        std::vector<double> unstable_s; ///< frozen-estimate margin < 0, s (where defined)
        size_t              ungrd_howl      = 0;
        size_t              ungrd_howl_runs = 0;
        size_t              ungrd_runs      = 0;
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const long open = gd::first_state(t, guard_state::open);
            if (open >= 0) {
                t_open.push_back(secs(open));
            }
            if (gd::blocks_in(t, guard_state::arming) == t.size()) {
                ++never;
            }
            const long cap = gd::first_state(t, guard_state::open_capped);
            if (cap >= 0) {
                ++capped;
                t_capped.push_back(secs(cap));
            }
            if (t.unprot_from >= 0) {
                t_unprot.push_back(secs(t.unprot_from));
            }
            const size_t d = gd::entries(t, guard_state::ducked);
            ducks += d;
            duck_runs += d > 0 ? 1U : 0U;
            if (d > 0) {
                // iterators clamped to the trace (GCC-safe; guard_runs.h)
                const auto b        = gd::bursts(t);
                const auto [sb, se] = gd::clamped_range(t.state, 1, t.size());
                auto   bi           = b.begin() + (sb - t.state.begin());
                size_t off          = 0;
                for (auto it = sb; it != se; ++it, ++bi) {
                    off += (*it == guard_state::ducked && *(it - 1) != guard_state::ducked && *bi == 0) ? 1U : 0U;
                }
                off_burst += off;
                off_runs += off > 0 ? 1U : 0U;
            }
            arming_s.push_back(secs(static_cast<long>(gd::blocks_in(t, guard_state::arming))));
            const size_t h = gd::howl_blocks(t);
            howl += h;
            howl_runs += h > 0 ? 1U : 0U;
            if (!t.o_blk.empty()) {
                unstable_s.push_back(gd::unstable_s(t));
            }
        }
        void add_unguarded(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++ungrd_runs;
            const size_t h = gd::howl_blocks(t);
            ungrd_howl += h;
            ungrd_howl_runs += h > 0 ? 1U : 0U;
        }
        void print() const {
            std::printf(
                "  %-26s %4zu | %3zu %6.2f %6.2f | %3zu %3zu %6.2f %6.2f | %6.2f %6.2f | %3zu %3zu %3zu %3zu | %5zu %3zu | "
                "%6.2f %6.2f | %5zu %3zu/%zu\n",
                label.c_str(), runs, t_open.size(), med(t_open), max_of(t_open), never, capped, med(t_capped),
                max_of(t_capped), med(t_unprot), med(arming_s), ducks, duck_runs, off_burst, off_runs, howl, howl_runs,
                med(unstable_s), max_of(unstable_s), ungrd_howl, ungrd_howl_runs, ungrd_runs);
        }
    };

    void print_cold_header() {
        std::printf(
            "  %-26s %4s | %3s %6s %6s | %3s %3s %6s %6s | %6s %6s | %3s %3s %3s %3s | %5s %3s | %6s %6s | %5s %s\n",
            "row", "runs", "dec", "open", "max", "nvr", "cap", "capd", "max", "unprot", "arm s", "dck", "run", "off",
            "run", "howl", "run", "unst", "max", "ungd", "runs");
    }

    struct cold_spec {
        std::vector<std::string>       rooms;
        std::vector<gd::material_spec> mats;
        std::vector<unsigned>          seeds;
        size_t                         delay      = gd::k_s1;
        double                         rel        = std::nan(""); ///< dB over exact_msg_db; NaN: over_limit
        double                         over_limit = -6.0;         ///< dB over the canceller's limit
        double                         shift_hz   = 0.0;
        double                         seconds    = 20.0;
        std::vector<std::string>       twins; ///< materials also run unguarded (oracle off)
        bool                           cap = true;
    };

    std::vector<cold_stats> cold_rows(const cold_spec& c) {
        std::vector<cold_stats>            out(c.mats.size());
        std::vector<std::function<void()>> jobs;
        for (size_t mi = 0; mi < c.mats.size(); ++mi) {
            std::string label = gd::material_name(c.mats[mi]) + (c.delay == gd::k_s1 ? " S1" : " S3");
            if (c.shift_hz > 0.0) {
                label += " " + std::to_string(static_cast<int>(c.shift_hz)) + " Hz";
            }
            if (!std::isnan(c.rel)) {
                label += " dry+" + std::to_string(static_cast<int>(c.rel));
            }
            else if (c.over_limit != -6.0) {
                label += " lim+" + std::to_string(static_cast<int>(c.over_limit));
            }
            out[mi].label = label;
            for (const auto& room : c.rooms) {
                for (const unsigned seed : c.seeds) {
                    gd::run_spec s;
                    s.room          = room;
                    s.mat           = c.mats[mi];
                    s.seed          = seed;
                    s.delay         = c.delay;
                    const double ex = exact_db(room, c.delay);
                    s.gain_db  = std::isnan(c.rel) ? ex + asg_db(room, c.mats[mi], c.delay) + c.over_limit : ex + c.rel;
                    s.shift_hz = c.shift_hz;
                    if (c.cap) {
                        s.cap_db = cap_for(ex, s.gain_db);
                    }
                    s.seconds = c.seconds;
                    jobs.emplace_back([s, &st = out[mi]] { st.add(gd::live_run(s)); });
                    const std::string name = gd::material_name(c.mats[mi]);
                    if (std::find(c.twins.begin(), c.twins.end(), name) != c.twins.end()) {
                        auto u    = s;
                        u.guarded = false;
                        u.oracle  = false;
                        jobs.emplace_back([u, &st = out[mi]] { st.add_unguarded(gd::live_run(u)); });
                    }
                }
            }
        }
        run_parallel(jobs);
        return out;
    }

    // ---------------------------------------------------- walks (S2a)

    struct walk_stats {
        std::string         label;
        size_t              runs           = 0;
        size_t              open_at_change = 0;
        size_t              ducked         = 0; ///< runs with a DUCKED after the change
        size_t              by_lost        = 0; ///< ... whose first duck was LOST
        std::vector<double> duck_s;             ///< first duck - change, s
        std::vector<double> delta_s;            ///< first release (ramp start) - reconvergence, s
        size_t              early        = 0;   ///< first releases before the oracle reconverged
        size_t              releases     = 0;   ///< every post-change release
        size_t              early_all    = 0;   ///< ... before the oracle reconverged
        size_t              never_reconv = 0;
        size_t              rehowl       = 0; ///< releases with a howl block within 3 s
        size_t              howl_post    = 0; ///< howl blocks after the change
        std::vector<double> longest_s;
        size_t              strikes = 0;
        size_t              latched = 0;
        std::vector<double> d_pre;   ///< median D over the 2 s before the change, dB
        std::vector<double> notok_s; ///< longest not-ok run in the 3 s after the change, s
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const long   c    = t.change;
            const auto   from = static_cast<long>(static_cast<size_t>(c) - gd::blocks_of(2.0));
            const size_t to   = std::min(t.size(), static_cast<size_t>(c) + gd::blocks_of(3.0));
            d_pre.push_back(median(std::vector<double>(t.d_db.begin() + from, t.d_db.begin() + c)));
            size_t best = 0;
            size_t cur  = 0;
            for (auto i = static_cast<size_t>(c); i < to; ++i) {
                cur  = t.ok[i] != 0 ? 0 : cur + 1;
                best = std::max(best, cur);
            }
            notok_s.push_back(secs(static_cast<long>(best)));
            open_at_change += t.state[static_cast<size_t>(c)] == guard_state::open ? 1U : 0U;
            const long reconv = gd::reconverged(t, c);
            never_reconv += reconv < 0 ? 1U : 0U;
            const long duck = gd::first_state(t, guard_state::ducked, c);
            if (duck >= 0) {
                ++ducked;
                by_lost += t.trip[static_cast<size_t>(duck)] == 0 ? 1U : 0U;
                duck_s.push_back(secs(duck - c));
                const long rel = gd::first_state(t, guard_state::releasing, duck);
                if (rel >= 0) {
                    if (reconv >= 0) {
                        delta_s.push_back(secs(rel - reconv));
                    }
                    early += (reconv < 0 || rel < reconv) ? 1U : 0U;
                }
            }
            for (size_t i = static_cast<size_t>(c) + 1; i < t.size(); ++i) {
                if (t.state[i] == guard_state::releasing && t.state[i - 1] != guard_state::releasing) {
                    ++releases;
                    early_all += (reconv < 0 || static_cast<long>(i) < reconv) ? 1U : 0U;
                    const size_t end = std::min(t.size(), i + gd::blocks_of(3.0));
                    rehowl += gd::howl_blocks(t, i, end) > 0 ? 1U : 0U;
                }
            }
            howl_post += gd::howl_blocks(t, static_cast<size_t>(c));
            longest_s.push_back(gd::longest_howl_s(t, static_cast<size_t>(c)));
            strikes += t.strikes;
            latched += t.latched ? 1U : 0U;
        }
        void merge(const walk_stats& o) {
            runs += o.runs;
            open_at_change += o.open_at_change;
            ducked += o.ducked;
            by_lost += o.by_lost;
            duck_s.insert(duck_s.end(), o.duck_s.begin(), o.duck_s.end());
            delta_s.insert(delta_s.end(), o.delta_s.begin(), o.delta_s.end());
            early += o.early;
            releases += o.releases;
            early_all += o.early_all;
            never_reconv += o.never_reconv;
            rehowl += o.rehowl;
            howl_post += o.howl_post;
            longest_s.insert(longest_s.end(), o.longest_s.begin(), o.longest_s.end());
            strikes += o.strikes;
            latched += o.latched;
            d_pre.insert(d_pre.end(), o.d_pre.begin(), o.d_pre.end());
            notok_s.insert(notok_s.end(), o.notok_s.begin(), o.notok_s.end());
        }
        void print() const {
            std::printf("  %-26s %3zu | %3zu %3zu %3zu | %5.2f | %6.2f %6.2f %3zu | %3zu %3zu %3zu | %3zu %5zu %5.2f | "
                        "%3zu %3zu | %6.2f %5.2f %5.2f\n",
                        label.c_str(), runs, open_at_change, ducked, by_lost, med(duck_s), med(delta_s),
                        min_of(delta_s), early, releases, early_all, never_reconv, rehowl, howl_post, max_of(longest_s),
                        strikes, latched, med(d_pre), med(notok_s), max_of(notok_s));
        }
    };

    void print_walk_header() {
        std::printf(
            "  %-26s %3s | %3s %3s %3s | %5s | %6s %6s %3s | %3s %3s %3s | %3s %5s %5s | %3s %3s | %6s %5s %5s\n",
            "row", "n", "opn", "dck", "lst", "duck", "d med", "d min", "erl", "rel", "ear", "nrc", "rhw", "howl",
            "long", "stk", "lat", "D pre", "nok", "max");
    }

} // namespace

namespace {

    // ---------------------------------------------------- the stress rows

    /// Walks (S2a) or louder couplings (S2b, room2 empty): rows of (room,
    /// room2) x seeds, V, at `rel` over exact_msg_db (NaN: the start room's
    /// limit - 6 dB), the change at 10 s; with `twin`, unguarded twins too.
    std::vector<walk_stats> walk_rows(const std::vector<std::pair<std::string, std::string>>& rows,
                                      const std::vector<unsigned>& seeds, double rel, double seconds, bool twin,
                                      std::vector<walk_stats>* unguarded = nullptr) {
        std::vector<walk_stats>            out(rows.size());
        std::vector<walk_stats>            ug(rows.size());
        std::vector<std::function<void()>> jobs;
        for (size_t r = 0; r < rows.size(); ++r) {
            const auto& [room, room2] = rows[r];
            out[r].label              = room + (room2.empty() ? std::string(" x2") : " -> " + room2);
            ug[r].label               = out[r].label + " (none)";
            for (const unsigned seed : seeds) {
                gd::run_spec s;
                s.room          = room;
                s.room2         = room2;
                s.scale2        = room2.empty() ? 2.0 : 1.0;
                s.t_change_s    = 10.0;
                s.mat           = k_v;
                s.seed          = seed;
                const double ex = exact_db(room, gd::k_s1);
                s.gain_db       = std::isnan(rel) ? operating_gain(room, k_v, gd::k_s1) : ex + rel;
                s.cap_db        = cap_for(ex, s.gain_db);
                s.seconds       = seconds;
                jobs.emplace_back([s, &st = out[r]] { st.add(gd::live_run(s)); });
                if (twin) {
                    auto u    = s;
                    u.guarded = false;
                    jobs.emplace_back([u, &st = ug[r]] { st.add(gd::live_run(u)); });
                }
            }
        }
        run_parallel(jobs);
        if (unguarded != nullptr) {
            *unguarded = std::move(ug);
        }
        return out;
    }

    /// F -> 2F live: per run, the first duck after the change, the first
    /// timer re-arm, LOST-ducks after it (the pump), howl blocks after it.
    struct rearm_stats {
        std::string         label;
        size_t              runs   = 0;
        size_t              ducked = 0;
        std::vector<double> duck_s; ///< first duck - change
        size_t              rearmed = 0;
        std::vector<double> rearm_s;         ///< first re-arm - first duck
        size_t              pumps       = 0; ///< LOST-ducks after the first re-arm
        size_t              pump_runs   = 0;
        size_t              lost_ducks  = 0; ///< LOST-ducks after the change
        size_t              trip_ducks  = 0; ///< TRIP-ducks after the change
        size_t              howl_after  = 0; ///< howl blocks after the change
        size_t              rehowl      = 0; ///< howl blocks after the first re-arm (re-howls)
        size_t              rehowl_runs = 0;
        std::vector<double> ducked_frac;     ///< share of the post-change time ducked or releasing
        size_t              strikes = 0;     ///< strikes at the end (after any decay)
        size_t              latched = 0;     ///< runs latched at the end
        std::vector<double> level_db;        ///< the restore level at the end, dB
        std::vector<double> end_gain_db;     ///< the gain at the end, dB
        std::vector<double> lost_per_run;    ///< LOST-ducks after the change, per run
        std::vector<double> latch_s;         ///< first LATCHED block - change, s (runs that reached it)
        size_t              after_latch = 0; ///< DUCKED entries after the first LATCHED block
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            strikes += t.strikes;
            latched += t.latched ? 1U : 0U;
            level_db.push_back(static_cast<double>(t.level_db));
            end_gain_db.push_back(static_cast<double>(t.gain_db.back()));
            const size_t lost_before = lost_ducks;
            const auto   c           = static_cast<size_t>(t.change);
            const long   duck        = gd::first_state(t, guard_state::ducked, t.change);
            if (duck >= 0) {
                ++ducked;
                duck_s.push_back(secs(duck - t.change));
            }
            long first_rearm = -1;
            for (size_t i = c; i < t.size() && first_rearm < 0; ++i) {
                if (t.rearm[i] != 0) {
                    first_rearm = static_cast<long>(i);
                }
            }
            size_t ducked_blocks = 0;
            size_t p             = 0;
            for (size_t i = c; i < t.size(); ++i) {
                const bool entry = t.state[i] == guard_state::ducked && t.state[i - 1] != guard_state::ducked;
                trip_ducks += (entry && t.trip[i] != 0) ? 1U : 0U;
                lost_ducks += (entry && t.trip[i] == 0) ? 1U : 0U;
                if (entry && t.trip[i] == 0 && first_rearm >= 0 && static_cast<long>(i) > first_rearm) {
                    ++p;
                }
                ducked_blocks += (t.state[i] == guard_state::ducked || t.state[i] == guard_state::releasing) ? 1U : 0U;
            }
            pumps += p;
            pump_runs += p > 0 ? 1U : 0U;
            if (first_rearm >= 0) {
                ++rearmed;
                if (duck >= 0) {
                    rearm_s.push_back(secs(first_rearm - duck));
                }
            }
            howl_after += gd::howl_blocks(t, c);
            if (first_rearm >= 0) {
                const size_t h = gd::howl_blocks(t, static_cast<size_t>(first_rearm));
                rehowl += h;
                rehowl_runs += h > 0 ? 1U : 0U;
            }
            ducked_frac.push_back(static_cast<double>(ducked_blocks) / static_cast<double>(t.size() - c));
            lost_per_run.push_back(static_cast<double>(lost_ducks - lost_before));
            const long latch = gd::first_state(t, guard_state::latched, t.change);
            if (latch >= 0) {
                latch_s.push_back(secs(latch - t.change));
                after_latch += gd::entries(t, guard_state::ducked, static_cast<size_t>(latch));
            }
        }
        void print() const {
            std::printf("  %-22s %3zu | %3zu %5.2f | %3zu %5.2f | %4zu %3zu | %3zu %5.2f %3zu %3zu | %5zu %4zu %3zu | "
                        "%5.2f | %3zu %3zu %6.2f %6.2f %6.2f | %3zu %6.2f %3zu\n",
                        label.c_str(), runs, ducked, med(duck_s), rearmed, med(rearm_s), pumps, pump_runs, lost_ducks,
                        med(lost_per_run), static_cast<size_t>(max_of(lost_per_run)), trip_ducks, howl_after, rehowl,
                        rehowl_runs, med(ducked_frac), strikes, latched, med(level_db), min_of(level_db),
                        med(end_gain_db), latch_s.size(), med(latch_s), after_latch);
        }
    };

    void print_rearm_header() {
        std::printf("  %-22s %3s | %3s %5s | %3s %5s | %4s %3s | %3s %5s %3s %3s | %5s %4s %3s | %5s | %3s %3s %6s %6s "
                    "%6s | %3s %6s %3s\n",
                    "row", "n", "dck", "duck", "rea", "rearm", "pump", "run", "lst", "l/run", "max", "trp", "howl",
                    "rhwl", "run", "dfrac", "stk", "lat", "lvl", "min", "gain", "LAT", "at", "dk>");
    }

    /// F -> 2F (S2b) at 10 s, voiced + aux at the limit - 6, `seconds` from
    /// reset: one row per room over `seeds`, with or without a LOST in
    /// probation counted as a strike.
    std::vector<rearm_stats> louder_rows(const std::vector<std::string>& rooms, const std::vector<unsigned>& seeds,
                                         bool lost_strikes, double seconds = 30.0) {
        std::vector<rearm_stats>           rows(rooms.size());
        std::vector<std::function<void()>> jobs;
        for (size_t r = 0; r < rooms.size(); ++r) {
            rows[r].label = rooms[r] + " x2" + (lost_strikes ? " (L)" : "");
            for (const unsigned seed : seeds) {
                gd::run_spec s;
                s.room                             = rooms[r];
                s.scale2                           = 2.0;
                s.t_change_s                       = 10.0;
                s.mat                              = k_v;
                s.seed                             = seed;
                s.gain_db                          = operating_gain(s.room, k_v, gd::k_s1);
                s.cap_db                           = cap_for(exact_db(s.room, gd::k_s1), s.gain_db);
                s.seconds                          = seconds;
                s.oracle                           = false;
                s.policy.lost_in_probation_strikes = lost_strikes;
                jobs.emplace_back([s, &st = rows[r]] { st.add(gd::live_run(s)); });
            }
        }
        run_parallel(jobs);
        return rows;
    }

    /// The pooled pump numbers over rearm rows.
    struct rearm_totals {
        size_t              runs        = 0;
        size_t              rearmed     = 0;
        size_t              pumps       = 0;
        size_t              pump_runs   = 0;
        size_t              howl_after  = 0;
        size_t              rehowl      = 0;
        size_t              rehowl_runs = 0;
        std::vector<double> rearm_s;
    };

    rearm_totals rearm_total(const std::vector<rearm_stats>& rows) {
        rearm_totals t;
        for (const auto& r : rows) {
            t.runs += r.runs;
            t.rearmed += r.rearmed;
            t.pumps += r.pumps;
            t.pump_runs += r.pump_runs;
            t.howl_after += r.howl_after;
            t.rehowl += r.rehowl;
            t.rehowl_runs += r.rehowl_runs;
            t.rearm_s.insert(t.rearm_s.end(), r.rearm_s.begin(), r.rearm_s.end());
        }
        std::printf("  pooled: %zu runs, %zu re-armed (re-arm after the duck, median %.2f s, min %.2f, max %.2f); "
                    "LOST-ducks after the first re-arm %zu in %zu runs; howl blocks after the change %zu, after the "
                    "first re-arm %zu in %zu runs\n",
                    t.runs, t.rearmed, med(t.rearm_s), min_of(t.rearm_s), max_of(t.rearm_s), t.pumps, t.pump_runs,
                    t.howl_after, t.rehowl, t.rehowl_runs);
        return t;
    }

    // ---------------------------------------------------- two mics

    struct two_mic_stats {
        std::string         label;
        size_t              runs         = 0;
        size_t              tripped      = 0; ///< runs with a TRIP after the force
        size_t              mic1_only    = 0; ///< ... whose first TRIP ducked mic 1 alone (the forced one)
        size_t              both         = 0; ///< ... ducked both at once
        size_t              mic0_only    = 0; ///< ... ducked the wrong mic alone
        size_t              fallbacks    = 0;
        size_t              cascade      = 0; ///< mic 0 ducks after the force by LOST (not a TRIP)
        size_t              cascade_runs = 0;
        size_t              howl         = 0; ///< howl blocks after the force
        std::vector<double> longest_s;
        std::mutex          mu;

        void add(const gd::two_mic_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const auto f = static_cast<size_t>(t.force);
            for (size_t i = f; i < t.size(); ++i) {
                if (t.trip[0][i] != 0 || t.trip[1][i] != 0) {
                    ++tripped;
                    mic1_only += (t.trip[1][i] != 0 && t.trip[0][i] == 0) ? 1U : 0U;
                    both += (t.trip[1][i] != 0 && t.trip[0][i] != 0) ? 1U : 0U;
                    mic0_only += (t.trip[0][i] != 0 && t.trip[1][i] == 0) ? 1U : 0U;
                    break;
                }
            }
            fallbacks += t.fallbacks;
            size_t c = 0;
            for (size_t i = f; i < t.size(); ++i) {
                const bool entry = t.state[0][i] == guard_state::ducked && t.state[0][i - 1] != guard_state::ducked;
                c += (entry && t.trip[0][i] == 0) ? 1U : 0U;
            }
            cascade += c;
            cascade_runs += c > 0 ? 1U : 0U;
            size_t h    = 0;
            size_t cur  = 0;
            size_t best = 0;
            for (size_t i = f; i < t.size(); ++i) {
                const bool howling = t.e_rms[i] >= 100.0F;
                h += howling ? 1U : 0U;
                cur  = howling ? cur + 1 : 0;
                best = std::max(best, cur);
            }
            howl += h;
            longest_s.push_back(secs(static_cast<long>(best)));
        }
        void print() const {
            std::printf("  %-20s %3zu | %3zu %3zu %3zu %3zu | %3zu | %3zu %3zu | %5zu %5.2f\n", label.c_str(), runs,
                        tripped, mic1_only, both, mic0_only, fallbacks, cascade, cascade_runs, howl, max_of(longest_s));
        }
    };

    // ---------------------------------------------------- audible cost

    struct cost_stats {
        std::string         label;
        size_t              runs       = 0;
        size_t              opened     = 0;
        size_t              ducks      = 0; ///< DUCKED entries after the first OPEN
        size_t              on_burst   = 0; ///< ... within 0.5 s of a block at +20 dB
        size_t              clean_runs = 0; ///< runs with a duck off any burst
        std::vector<double> ducked_s;       ///< ducked or releasing after the first OPEN, s
        size_t              bursts = 0;     ///< runs with a +20 dB block after the first OPEN
        size_t              howl   = 0;
        std::vector<double> max_e_db; ///< loudest residual block after the first OPEN, dB re the unit near end
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const long open = gd::first_state(t, guard_state::open);
            if (open < 0) {
                return;
            }
            ++opened;
            const auto b     = gd::bursts(t);
            size_t     cl    = 0;
            size_t     d     = 0;
            bool       burst = false;
            for (auto i = static_cast<size_t>(open) + 1; i < t.size(); ++i) {
                const bool entry = t.state[i] == guard_state::ducked && t.state[i - 1] != guard_state::ducked;
                if (entry) {
                    ++ducks;
                    if (b[i] != 0) {
                        ++on_burst;
                    }
                    else {
                        ++cl;
                    }
                }
                d += (t.state[i] == guard_state::ducked || t.state[i] == guard_state::releasing) ? 1U : 0U;
                burst = burst || t.e_rms[i] >= 10.0F;
            }
            clean_runs += cl > 0 ? 1U : 0U;
            ducked_s.push_back(secs(static_cast<long>(d)));
            float peak = 0.0F;
            for (auto i = static_cast<size_t>(open) + 1; i < t.size(); ++i) {
                peak = std::max(peak, t.e_rms[i]);
            }
            max_e_db.push_back(20.0 * std::log10(static_cast<double>(peak)));
            bursts += burst ? 1U : 0U;
            howl += gd::howl_blocks(t, static_cast<size_t>(open));
        }
        void print() const {
            std::printf("  %-26s %3zu %3zu | %4zu %4zu %3zu | %6.2f %6.2f | %3zu | %4zu | %6.2f %6.2f\n", label.c_str(),
                        runs, opened, ducks, on_burst, clean_runs, med(ducked_s), max_of(ducked_s), bursts, howl,
                        med(max_e_db), max_of(max_e_db));
        }
    };

    void print_cost_header() {
        std::printf("  %-26s %3s %3s | %4s %4s %3s | %6s %6s | %3s | %4s | %6s %6s\n", "row", "n", "opn", "duck",
                    "brst", "cln", "dk s", "max", "brs", "howl", "e dB", "max");
    }

    void print_two_mic_header() {
        std::printf("  %-20s %3s | %3s %3s %3s %3s | %3s | %3s %3s | %5s %5s\n", "row", "n", "trp", "m1", "bth", "m0",
                    "fbk", "cas", "run", "howl", "long");
    }

    /// A gap between songs: the singer and the backing track at digital
    /// zero from gap_from_s for gap_s.
    struct gap_stats {
        std::string         label;
        size_t              runs = 0;
        std::vector<double> max_a_db;        ///< A' peak in the gap, dB
        size_t              gap_ducks   = 0; ///< DUCKED entries in the gap
        size_t              restarts    = 0; ///< ARMING entries after the first OPEN
        size_t              open_at_end = 0; ///< runs OPEN when the singer returns
        size_t              howl        = 0; ///< howl blocks after the gap
        std::vector<double> reentry_db;      ///< loudest residual in the 1 s after the gap, dB re the unit near end
        std::mutex          mu;

        void add(const gd::run_trace& t, const gd::run_spec& s) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const size_t g0   = gd::blocks_of(s.gap_from_s);
            const size_t g1   = std::min(t.size(), gd::blocks_of(s.gap_from_s + s.gap_s));
            float        amax = -1e9F;
            for (size_t i = g0; i < g1; ++i) {
                amax = std::max(amax, t.a_db[i]);
                gap_ducks += (t.state[i] == guard_state::ducked && t.state[i - 1] != guard_state::ducked) ? 1U : 0U;
            }
            max_a_db.push_back(static_cast<double>(amax));
            const long open = gd::first_state(t, guard_state::open);
            if (open >= 0) {
                restarts += gd::entries(t, guard_state::arming, static_cast<size_t>(open));
            }
            open_at_end += (g1 > 0 && t.state[g1 - 1] == guard_state::open) ? 1U : 0U;
            howl += gd::howl_blocks(t, g1);
            float peak = 0.0F;
            for (size_t i = g1; i < std::min(t.size(), g1 + gd::blocks_of(1.0)); ++i) {
                peak = std::max(peak, t.e_rms[i]);
            }
            reentry_db.push_back(20.0 * std::log10(static_cast<double>(peak)));
        }
        void print() const {
            std::printf("  %-20s %3zu | %7.2f %7.2f | %3zu %3zu %3zu | %4zu | %6.2f %6.2f\n", label.c_str(), runs,
                        med(max_a_db), max_of(max_a_db), gap_ducks, restarts, open_at_end, howl, med(reentry_db),
                        max_of(reentry_db));
        }
    };

    void print_gap_header() {
        std::printf("  %-20s %3s | %7s %7s | %3s %3s %3s | %4s | %6s %6s\n", "row", "n", "A' max", "max", "dck", "rst",
                    "opn", "howl", "re dB", "max");
    }

    /// The vendored Dattorro (FAUST's re.dattorro_rev, paper defaults: T30
    /// 1.18 s) as a reverb-slot stage: out = in + wet x (L + R) / 2.
    struct dattorro_stage {
        explicit dattorro_stage(double wet_gain)
            : rev(48000)
            , wet(wet_gain) {}
        mutap_faust::faust_block<mutap_faust::dattorro_f64, double> rev;
        double                                                      wet;
        std::vector<double>                                         l = std::vector<double>(gd::k_block);
        std::vector<double>                                         r = std::vector<double>(gd::k_block);
        std::vector<double>                                         x = std::vector<double>(gd::k_block);
        void process_block(const double* in, double* out, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) {
                x[i] = in[i];
            }
            double* outs[2] = {l.data(), r.data()};
            rev.process_mono(x.data(), outs, static_cast<int>(n));
            for (size_t i = 0; i < n; ++i) {
                out[i] = in[i] + wet * 0.5 * (l[i] + r[i]);
            }
        }
    };

} // namespace

namespace {

    /// Median ns per call of f over `reps` calls after `warm` warm-up calls,
    /// over 5 repetitions.
    template <typename F>
    double ns_per_call(F f, size_t warm, size_t reps) {
        for (size_t i = 0; i < warm; ++i) {
            f(i);
        }
        std::vector<double> runs;
        size_t              k = warm;
        for (int r = 0; r < 5; ++r) {
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < reps; ++i) {
                f(k++);
            }
            const auto t1 = std::chrono::steady_clock::now();
            runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(reps));
        }
        return median(runs);
    }

    /// The guard's own cost per block (M mics: M analyses, one update, M
    /// gain applications) and one canceller's process_block, on white noise
    /// at unit RMS (no trip in the timed loop).
    template <typename Sample>
    std::pair<double, double> guard_and_canceller_ns(size_t mics) {
        constexpr size_t    k_blocks = 256;
        std::vector<Sample> x(k_blocks * gd::k_block);
        const auto          w = mutap_test::white_near_end<double>(x.size(), 99);
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = static_cast<Sample>(w[i]);
        }
        gd::guard_t<Sample> g(gd::guard_config<Sample>(mics, gd::k_s1));
        std::vector<Sample> out(gd::k_block);
        const double        guard_ns = ns_per_call(
            [&](size_t i) {
                const Sample* e = &x[(i % k_blocks) * gd::k_block];
                for (size_t m = 0; m < mics; ++m) {
                    g.analyze(m, e, Sample(1e-3), Sample(0.5));
                }
                g.update();
                for (size_t m = 0; m < mics; ++m) {
                    g.apply(m, e, out.data());
                }
            },
            2000, 20000);
        gd::afc<Sample>     afc(gd::afc_config<Sample>());
        std::vector<Sample> e(gd::k_block);
        const double        afc_ns = ns_per_call(
            [&](size_t i) {
                const Sample* u = &x[(i % k_blocks) * gd::k_block];
                const Sample* y = &x[((i + 7) % k_blocks) * gd::k_block];
                afc.process_block(u, y, e.data());
            },
            500, 2000);
        return {guard_ns, afc_ns};
    }

} // namespace

namespace {

    // ---------------------------------------------------- soundcheck calibration

    constexpr double k_cal_s      = 30.0; ///< the protocol's soundcheck
    constexpr double k_cal_walk_s = 40.0; ///< the walk, 10 s after the soundcheck
    constexpr double k_cal_win_s  = 5.0;  ///< a duck this soon after the walk detected it

    /// The release experiment's six walks (S2a).
    const std::vector<std::pair<std::string, std::string>> k_walks = {{"studio", "rehearsal"}, {"rehearsal", "hall"},
                                                                      {"hall", "cabin"},       {"cabin", "studio"},
                                                                      {"mt5", "mt105"},        {"mt9", "mt109"}};

    /// The shadow guards' margin grid (dB over the soundcheck medians; 120:
    /// that statistic never makes the verdict not ok).
    const std::vector<double> k_cal_d_margins = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 120.0};
    const std::vector<double> k_cal_a_margins = {0.0, 1.0, 2.0, 3.0, 4.0, 6.0, 10.0, 120.0};

    std::vector<std::pair<double, double>> margin_grid() {
        std::vector<std::pair<double, double>> g;
        for (const double d : k_cal_d_margins) {
            for (const double a : k_cal_a_margins) {
                g.emplace_back(d, a);
            }
        }
        return g;
    }

    /// What the soundcheck would print for one mic.
    void print_soundcheck(const char* label, const tap::mu::guard_calibration& c) {
        std::printf("  %-34s soundcheck %5zu blocks: D median %+7.2f dB (p95 %+7.2f, max %+7.2f); A' median %+7.2f dB "
                    "(p95 %+7.2f, max %+7.2f) -> d_db %+7.2f, a_db %+7.2f%s\n",
                    label, c.blocks, c.d_median_db, c.d_p95_db, c.d_max_db, c.a_median_db, c.a_p95_db, c.a_max_db,
                    c.d_db, c.a_db, c.applied ? " (applied)" : "");
    }

    /// One margin pair's record over the sweep's shadow guards.
    struct cal_cell {
        double              d_margin         = 0.0;
        double              a_margin         = 0.0;
        size_t              stable_runs      = 0;
        size_t              stable_duck_runs = 0; ///< a duck after the soundcheck
        size_t              stable_inexact   = 0; ///< ... where the live loop had diverged first
        size_t              walk_runs        = 0;
        size_t              walk_pre         = 0; ///< ducked after the soundcheck, before the walk
        size_t              walk_detected    = 0; ///< first duck within k_cal_win_s after the walk
        size_t              walk_inexact     = 0;
        std::vector<double> walk_latency_s;
    };

    /// The soundcheck readouts and the margin grid over a sweep.
    struct cal_stats {
        std::vector<cal_cell>                                           cells;
        std::vector<std::pair<std::string, tap::mu::guard_calibration>> soundchecks; ///< (group, readout)
        std::mutex                                                      mu;

        explicit cal_stats(const std::vector<std::pair<double, double>>& grid) {
            for (const auto& [d, a] : grid) {
                cal_cell c;
                c.d_margin = d;
                c.a_margin = a;
                cells.push_back(c);
            }
        }
        void add(const std::string& group, const gd::run_trace& t, bool walk) {
            std::lock_guard<std::mutex> lock(mu);
            soundchecks.emplace_back(group, t.cal);
            for (size_t k = 0; k < t.shadow.size(); ++k) {
                const gd::shadow_result& r = t.shadow[k];
                cal_cell&                c = cells[k];
                if (!walk) {
                    ++c.stable_runs;
                    c.stable_duck_runs += r.first_duck >= 0 ? 1U : 0U;
                    c.stable_inexact += r.exact ? 0U : 1U;
                    continue;
                }
                ++c.walk_runs;
                c.walk_inexact += r.exact ? 0U : 1U;
                if (r.first_duck < 0) {
                    continue;
                }
                if (r.first_duck < t.change) {
                    ++c.walk_pre;
                }
                else if (r.first_duck < t.change + static_cast<long>(gd::blocks_of(k_cal_win_s))) {
                    ++c.walk_detected;
                    c.walk_latency_s.push_back(secs(r.first_duck - t.change));
                }
            }
        }
        void print_soundchecks() const {
            std::printf("  soundcheck readouts per group (medians over runs [min, max])\n");
            std::printf("  %-22s %3s | %20s | %7s | %20s | %7s\n", "group", "n", "D median", "D p95", "A' median",
                        "A' p95");
            std::vector<std::string> groups;
            for (const auto& [g, c] : soundchecks) {
                if (std::find(groups.begin(), groups.end(), g) == groups.end()) {
                    groups.push_back(g);
                }
            }
            for (const auto& g : groups) {
                std::vector<double> dm;
                std::vector<double> dp;
                std::vector<double> am;
                std::vector<double> ap;
                for (const auto& [gg, c] : soundchecks) {
                    if (gg == g) {
                        dm.push_back(c.d_median_db);
                        dp.push_back(c.d_p95_db);
                        am.push_back(c.a_median_db);
                        ap.push_back(c.a_p95_db);
                    }
                }
                std::printf("  %-22s %3zu | %+6.2f [%+6.2f %+6.2f] | %+7.2f | %+6.2f [%+6.2f %+6.2f] | %+7.2f\n",
                            g.c_str(), dm.size(), med(dm), min_of(dm), max_of(dm), med(dp), med(am), min_of(am),
                            max_of(am), med(ap));
            }
        }
        void print_grid() const {
            std::printf("  margin grid (shadow guards): D margin, A' margin | stable runs with a duck (inexact) | "
                        "walks: ducked before the walk, detected within %.0f s (inexact), latency median\n",
                        k_cal_win_s);
            for (const auto& c : cells) {
                std::printf("  %+6.1f %+6.1f | %3zu of %3zu (%zu) | %3zu %3zu of %3zu (%zu) %5.2f\n", c.d_margin,
                            c.a_margin, c.stable_duck_runs, c.stable_runs, c.stable_inexact, c.walk_pre,
                            c.walk_detected, c.walk_runs, c.walk_inexact, med(c.walk_latency_s));
            }
        }
    };

    /// A soundcheck run: from reset, the soundcheck over the first k_cal_s,
    /// stable material (or a walk at k_cal_walk_s) after it.
    gd::run_spec soundcheck_spec(const std::string& room, const gd::material_spec& m, unsigned seed, double shift_hz,
                                 const std::string& walk_to) {
        gd::run_spec s;
        s.room        = room;
        s.mat         = m;
        s.seed        = seed;
        s.gain_db     = operating_gain(room, m, gd::k_s1);
        s.cap_db      = cap_for(exact_db(room, gd::k_s1), s.gain_db);
        s.shift_hz    = shift_hz;
        s.calibrate_s = k_cal_s;
        s.oracle      = false;
        s.seconds     = 2.0 * k_cal_s;
        if (!walk_to.empty()) {
            s.room2      = walk_to;
            s.t_change_s = k_cal_walk_s;
            s.seconds    = k_cal_walk_s + 15.0;
        }
        return s;
    }

    /// The live guard's record after its soundcheck was applied.
    struct cal_live_stats {
        std::string         label;
        size_t              runs      = 0;
        size_t              ducks     = 0; ///< DUCKED entries after the soundcheck (before a walk)
        size_t              duck_runs = 0;
        size_t              on_burst  = 0; ///< ... within 0.5 s of a block at +20 dB
        size_t              howl      = 0; ///< howl blocks after the soundcheck
        std::vector<double> d_thr;         ///< the applied thresholds, dB
        std::vector<double> a_thr;
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const auto   from = static_cast<size_t>(t.cal_block + 1);
            const size_t to   = t.change >= 0 ? static_cast<size_t>(t.change) : t.size();
            const auto   b    = gd::bursts(t);
            size_t       d    = 0;
            // iterators clamped to the trace (GCC-safe; guard_runs.h)
            const auto [sb, se] = gd::clamped_range(t.state, std::max<size_t>(from, 1), to);
            auto bi             = b.begin() + (sb - t.state.begin());
            for (auto it = sb; it != se; ++it, ++bi) {
                if (*it == guard_state::ducked && *(it - 1) != guard_state::ducked) {
                    ++d;
                    on_burst += *bi != 0 ? 1U : 0U;
                }
            }
            ducks += d;
            duck_runs += d > 0 ? 1U : 0U;
            howl += gd::howl_blocks(t, from);
            d_thr.push_back(t.cal.d_db);
            a_thr.push_back(t.cal.a_db);
        }
        void print() const {
            std::printf("  %-26s %3zu | %3zu %3zu %3zu | %4zu | %+7.2f [%+7.2f %+7.2f] | %+7.2f [%+7.2f %+7.2f]\n",
                        label.c_str(), runs, ducks, duck_runs, on_burst, howl, med(d_thr), min_of(d_thr), max_of(d_thr),
                        med(a_thr), min_of(a_thr), max_of(a_thr));
        }
    };

    void print_cal_live_header() {
        std::printf("  %-26s %3s | %3s %3s %3s | %4s | %25s | %25s\n", "row", "n", "dck", "run", "brs", "howl",
                    "d_db applied [min max]", "a_db applied [min max]");
    }

} // namespace

// ============================================================ gated rows

TEST(HowlGuardHost, ColdStartWithBackingTrack) {
    cold_spec c;
    c.rooms = {"cabin", "mt5"};
    c.mats  = {k_v, k_m_aux, k_sp_aux};
    c.seeds = k_seeds3;
    c.twins = {"voiced+aux"};
    print_cold_header();
    const auto rows = cold_rows(c);
    for (const auto& r : rows) {
        r.print();
    }
    // Measured (cabin, mt5; seeds 1, 21, 41): 0 howl blocks in every row;
    // 6 of 6 declared in every row, median time to OPEN 1.78 / 1.77 /
    // 2.07 s (voiced+aux / music+aux / speech+aux); the unguarded voiced+aux
    // twins: 0 howl blocks in 6.
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
        EXPECT_EQ(r.t_open.size(), r.runs) << r.label;
        EXPECT_LT(med(r.t_open), 3.0) << r.label;
    }
}

TEST(HowlGuardHost, ColdStartWithoutBackingTrack) {
    cold_spec c;
    c.rooms = {"cabin", "mt5"};
    c.mats  = {k_h, k_m, k_sp};
    c.seeds = k_seeds3;
    c.twins = {"speech"};
    print_cold_header();
    const auto rows = cold_rows(c);
    for (const auto& r : rows) {
        r.print();
    }
    // Measured (the cap = the dry limit - 6 dB): 0 howl blocks in every row;
    // no run declared in 20 s (the canceller at the arming gain sees no
    // excitation without the backing track); every run left ARMING through
    // the cap (6 of 6 OPEN_CAPPED per row) at 10.00 s, median and max, with
    // `unprotected` raised from 10.00 s. The unguarded speech twins: 1 run
    // of 6 reached the 40 dB rule (1 block).
    // Ducks after OPEN_CAPPED (detector TRIPs: LOST is disarmed before a
    // declaration), held note / music / speech:
    //   macOS x86_64 (Intel): 2 of 6 / 0 / 0 runs, every duck within 0.5 s
    //     of a loop-born burst (+20 dB; 0 ducks off a burst).
    //   macOS arm64 CI (jobs 110465782241, 110465811203): 5 of 6 / 0 / 0
    //     runs (not classified against the burst oracle there).
    // A duck on a burst is the guard doing its job, so the duck count is
    // reported, not gated. Ducks off any burst are gated with margin: at
    // most 6 of the 18 runs (Intel: 0).
    size_t off_runs = 0;
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
        EXPECT_EQ(r.never, 0U) << r.label;
        EXPECT_EQ(r.capped, r.runs) << r.label;
        // OPEN_CAPPED needs the timeout (10 s) and quiet: the 7500th block,
        // index 7499 (9.9987 s)
        EXPECT_GE(min_of(r.t_capped), 10.0 - 1.5 * gd::block_s()) << r.label;
        EXPECT_LT(med(r.t_capped), 11.0) << r.label;
        off_runs += r.off_runs;
    }
    EXPECT_LE(off_runs, 6U) << "runs with a duck off any loop-born burst: measured 0 of 18 (Intel)";
}

TEST(HowlGuardHost, ColdStartWithoutACapStaysArmed) {
    // The cap is mandatory for opening without a declaration: the same
    // track-off rows with no cap set.
    cold_spec c;
    c.rooms = {"cabin", "mt5"};
    c.mats  = {k_h, k_m, k_sp};
    c.seeds = k_seeds3;
    c.cap   = false;
    print_cold_header();
    const auto rows = cold_rows(c);
    for (const auto& r : rows) {
        r.print();
    }
    // Measured: 0 howl blocks; every run (6 of 6 per row) stayed in ARMING
    // for the whole 20 s, 30 dB down, with `unprotected` raised at 10.00 s.
    // Structural: no run reaches OPEN_CAPPED, and a run that does not
    // declare never leaves ARMING. Whether a track-off run declares is the
    // canceller's (chaotic) business, so the count is gated with margin.
    size_t never = 0;
    size_t runs  = 0;
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
        EXPECT_EQ(r.never + r.t_open.size(), r.runs) << r.label;
        EXPECT_EQ(r.capped, 0U) << r.label;
        EXPECT_NEAR(med(r.t_unprot), 10.0, 0.01) << r.label;
        never += r.never;
        runs += r.runs;
    }
    EXPECT_GE(never + 3, runs) << "measured: all 18 runs stayed in ARMING";
}

TEST(HowlGuardHost, ColdStartAboveTheDryLimit) {
    print_cold_header();
    for (const double rel : {3.0, 6.0}) {
        cold_spec c;
        c.rooms         = {"cabin", "mt5"};
        c.mats          = {k_v, k_h};
        c.seeds         = k_seeds3;
        c.rel           = rel;
        c.twins         = {"voiced+aux"};
        const auto rows = cold_rows(c);
        for (const auto& r : rows) {
            r.print();
        }
        // Measured at +3 and +6: 0 howl blocks guarded (and 0 in the 6
        // unguarded voiced+aux twins at each gain); voiced+aux declared 6 of
        // 6 (median 1.77 / 1.78 s); voiced left ARMING through the cap in 6
        // of 6.
        for (const auto& r : rows) {
            EXPECT_EQ(r.howl, 0U) << r.label;
            EXPECT_EQ(r.never, 0U) << r.label;
        }
        EXPECT_EQ(rows[0].t_open.size(), rows[0].runs);
    }
}

TEST(HowlGuardHost, ColdStartAboveTheCancellerLimit) {
    cold_spec c;
    c.rooms      = {"cabin", "mt5"};
    c.mats       = {k_v, k_h};
    c.seeds      = k_seeds3;
    c.over_limit = 6.0;
    c.twins      = {"voiced+aux", "voiced"};
    print_cold_header();
    const auto rows = cold_rows(c);
    for (const auto& r : rows) {
        r.print();
    }
    // Measured: guarded 0 howl blocks in both rows; the unguarded
    // voiced+aux twins howled in 3 of 6 runs (575 blocks), the voiced twins
    // in 0 of 6.
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
    }
    EXPECT_GE(rows[0].ungrd_howl_runs, 1U) << "the stress row must howl unguarded (measured 3 of 6)";
}

TEST(HowlGuardHost, ColdStartShiftedAndAtS3) {
    print_cold_header();
    std::vector<std::vector<cold_stats>> sets;
    for (const double hz : {2.0, 5.0}) {
        cold_spec c;
        c.rooms    = {"cabin", "mt5"};
        c.mats     = {k_v, k_h};
        c.seeds    = k_seeds3;
        c.shift_hz = hz;
        sets.push_back(cold_rows(c));
    }
    cold_spec c;
    c.rooms = {"cabin", "mt5"};
    c.mats  = {k_v, k_h};
    c.seeds = k_seeds3;
    c.delay = gd::k_s3;
    sets.push_back(cold_rows(c));
    for (const auto& set : sets) {
        for (const auto& r : set) {
            r.print();
        }
    }
    // Measured: 0 howl blocks in every row; voiced+aux declared 6 of 6
    // (median 1.77 / 1.78 / 1.77 s at 2 Hz / 5 Hz / S3); voiced left ARMING
    // through the cap in 6 of 6.
    for (const auto& set : sets) {
        for (const auto& r : set) {
            EXPECT_EQ(r.howl, 0U) << r.label;
            EXPECT_EQ(r.never, 0U) << r.label;
        }
        EXPECT_EQ(set[0].t_open.size(), set[0].runs) << set[0].label;
    }
}

TEST(HowlGuardHost, WalkReleasesAfterTheMisalignmentOracle) {
    const std::vector<std::pair<std::string, std::string>> walks = {{"studio", "rehearsal"}, {"rehearsal", "hall"},
                                                                    {"hall", "cabin"},       {"cabin", "studio"},
                                                                    {"mt5", "mt105"},        {"mt9", "mt109"}};
    print_walk_header();
    std::printf("  at exact_msg_db - 6 dB (the release experiment's operating point)\n");
    const auto low = walk_rows(walks, {1, 21}, -6.0, 25.0, false);
    for (const auto& r : low) {
        r.print();
    }
    walk_stats lo;
    lo.label = "pooled";
    for (const auto& r : low) {
        lo.merge(r);
    }
    lo.print();
    std::printf("  at the canceller's limit - 6 dB\n");
    const auto high = walk_rows(walks, {1, 21}, std::nan(""), 25.0, false);
    for (const auto& r : high) {
        r.print();
    }
    walk_stats hi;
    hi.label = "pooled";
    for (const auto& r : high) {
        hi.merge(r);
    }
    hi.print();
    // Measured at exact_msg_db - 6: 3 of 12 walks ducked (all on LOST,
    // 0.36 s after the change), 3 releases, 0 before the misalignment
    // oracle reconverged; release (ramp start) - reconvergence median
    // 1.59 s, minimum 1.38 s. At the canceller's limit - 6: 0 of 12 walks
    // ducked (D's median over the 2 s before the walk -8.46 dB; the verdict
    // lost for at most 0.22 s after it, under the 0.3 s trip hold; at
    // exact_msg_db - 6, -7.91 dB and at most 0.47 s). 0 howl blocks after
    // any walk at either point.
    ASSERT_GE(lo.delta_s.size(), 1U) << "the walk gate needs at least one release";
    EXPECT_GT(med(lo.delta_s), 0.5);
    EXPECT_EQ(lo.howl_post, 0U);
    EXPECT_EQ(hi.howl_post, 0U);
}

TEST(HowlGuardHost, LateHowlAfterAWalkIsCaught) {
    print_walk_header();
    std::vector<walk_stats> ug6;
    std::vector<walk_stats> ugl;
    const auto              dry6 = walk_rows({{"rehearsal", "hall"}}, k_seeds3, 6.0, 25.0, true, &ug6);
    std::printf("  exact_msg_db + 6 dB\n");
    dry6[0].print();
    ug6[0].print();
    const double over = asg_db("rehearsal", k_v, gd::k_s1) + 6.0;
    const auto   lim6 = walk_rows({{"rehearsal", "hall"}}, k_seeds3, over, 25.0, true, &ugl);
    std::printf("  the canceller's limit + 6 dB (exact_msg_db %+.2f dB)\n", over);
    lim6[0].print();
    ugl[0].print();
    // Measured: at exact_msg_db + 6, 0 howl blocks guarded or unguarded
    // (this loop's canceller holds +6); at the limit + 6, guarded 0 howl
    // blocks (5 strikes over 3 runs, no latch), unguarded 1203 blocks, the
    // longest howl 0.55 s.
    EXPECT_EQ(dry6[0].howl_post, 0U);
    EXPECT_EQ(lim6[0].howl_post, 0U);
    EXPECT_GE(ugl[0].howl_post, 1U) << "the stress row must howl unguarded (measured 1203 blocks)";
}

TEST(HowlGuardHost, LouderCouplingRearms) {
    const std::vector<std::string> rooms = {"cabin", "mt5", "studio", "hall"};
    print_rearm_header();
    std::printf("  lost_in_probation_strikes off\n");
    const auto rows = louder_rows(rooms, {1, 21}, false);
    for (const auto& r : rows) {
        r.print();
    }
    const rearm_totals tot = rearm_total(rows);
    std::printf("  lost_in_probation_strikes on\n");
    const auto rows_l = louder_rows(rooms, {1, 21}, true);
    for (const auto& r : rows_l) {
        r.print();
    }
    rearm_total(rows_l);
    // Measured (macOS x86_64): every run ducked after F -> 2F (2 of 2 per
    // room; LOST 0.37 to 0.39 s after the change, cabin 4.65 s); the timer
    // re-arm at 5.00 s (10.01 s after a strike) in 6 of 8 runs; 0 howl
    // blocks after the change, 0 after the re-arm.
    // The pump: LOST-ducks after the first re-arm, 0 in 8 runs with LOST
    // armed only once ok has been held release_hold_s after a re-arm
    // (16 in 6 of 8 when one ok tick armed it; the MUTAP_SLOW sweep: 1 in
    // 30 runs, against 47 in 20 of 30). Gated as a rate with margin, at most
    // 4 over the 8 runs (0.5 a run, a quarter of the old 2.0): a chaotic loop
    // moves single runs between hosts. Cabin never re-arms (the verdict is
    // held ok while ducked and released on the walk path) and still cycles
    // there: 7 LOST-ducks in 2 runs, unchanged; printed, not gated.
    for (const auto& r : rows) {
        EXPECT_EQ(r.ducked, r.runs) << r.label;
        EXPECT_EQ(r.howl_after, 0U) << r.label;
    }
    EXPECT_GE(tot.rearmed, 4U) << "measured 6 of 8 re-armed";
    EXPECT_LE(tot.pumps, 4U) << "LOST-ducks after a re-arm: measured 0 in 8 runs (16 before the held-ok rule)";
    // With lost_in_probation_strikes (a LOST in probation is a strike;
    // off by default, docs/howl-guard.md says why): cabin still took 7
    // LOST-ducks in the 2 runs (median 4 a run, as without), with 5 strikes
    // and 1 run latched by 30 s; the MUTAP_SLOW sweep's 120 s rows latch
    // every cabin run at the cap after a median 4 LOST-ducks and duck no
    // more. Elsewhere the first LOST after the change falls inside the
    // cold start's probation (OPEN at 1.76-1.79 s, the change at 10 s): one
    // strike, the level at -3.00 dB and the re-arm at 10.00 s, no new duck.
    // Gated: 0 howl blocks, cabin strikes at all (the rule acts), and no
    // other room backed off more than 6 dB (measured -3.00).
    size_t cabin_strikes = 0;
    for (const auto& r : rows_l) {
        EXPECT_EQ(r.ducked, r.runs) << r.label;
        EXPECT_EQ(r.howl_after, 0U) << r.label;
        if (r.label.rfind("cabin", 0) == 0) {
            cabin_strikes += r.strikes;
        }
        else {
            EXPECT_GE(min_of(r.level_db), -6.0) << r.label << ": measured -3.00";
        }
    }
    EXPECT_GE(cabin_strikes, 1U) << "measured 5 strikes in 2 runs";
}

TEST(HowlGuardHost, SongGapDoesNotRestart) {
    std::vector<gap_stats>             rows(2);
    std::vector<std::function<void()>> jobs;
    const char*                        rooms[] = {"cabin", "mt5"};
    for (size_t r = 0; r < 2; ++r) {
        rows[r].label = std::string(rooms[r]) + " 20 s gap";
        for (const unsigned seed : k_seeds3) {
            gd::run_spec s;
            s.room       = rooms[r];
            s.mat        = k_v;
            s.seed       = seed;
            s.gain_db    = operating_gain(s.room, k_v, gd::k_s1);
            s.cap_db     = cap_for(exact_db(s.room, gd::k_s1), s.gain_db);
            s.seconds    = 34.0;
            s.gap_from_s = 8.0;
            s.gap_s      = 20.0;
            s.oracle     = false;
            jobs.emplace_back([s, &st = rows[r]] { st.add(gd::live_run(s), s); });
        }
    }
    run_parallel(jobs);
    print_gap_header();
    for (const auto& r : rows) {
        r.print();
    }
    // Measured: A' rose to -15.21 / -15.30 dB (median peak; max -14.96) in
    // the gap - far under restart_a_db (-1), so 0 restarts; LOST ducked in
    // the gap in 3 of 3 runs per room and the timer re-arm opened again
    // before the singer returned (3 of 3 OPEN); 0 howl blocks after the
    // gap; the loudest residual block in the 1 s after it +3.91 dB.
    for (const auto& r : rows) {
        EXPECT_EQ(r.restarts, 0U) << r.label;
        EXPECT_LT(max_of(r.max_a_db), -5.0) << r.label;
        EXPECT_EQ(r.howl, 0U) << r.label;
    }
}

TEST(HowlGuardHost, TwoMicsAttributeASingleMicHowl) {
    std::vector<two_mic_stats>         rows(2 * std::size(k_ops2));
    std::vector<std::function<void()>> jobs;
    size_t                             k = 0;
    for (const auto& op : k_ops2) {
        for (const bool aux : {false, true}) {
            rows[k].label = std::string(op.pair) + (aux ? " +aux" : "");
            for (const unsigned seed : {1U, 21U}) {
                gd::two_mic_spec s;
                s.pair          = op.pair;
                s.aux           = aux;
                s.seed          = seed;
                const double ex = mutap_test::exact_msg_db(
                    mutap_test::two_mic::path_sum(mutap_test::two_mic::room_pair(op.pair)), gd::k_s1);
                s.gain_db = ex + (aux ? op.aux : op.plain) - 6.0;
                s.cap_db  = cap_for(ex, s.gain_db);
                jobs.emplace_back([s, &st = rows[k]] { st.add(gd::two_mic_run(s)); });
            }
            ++k;
        }
    }
    run_parallel(jobs);
    print_two_mic_header();
    for (const auto& r : rows) {
        r.print();
    }
    // Measured, two hosts (the loop is chaotic: single runs move between
    // platforms, so the gates are counts with margin, not zeros):
    //   macOS x86_64 (Intel): every run tripped after the force; the first
    //     TRIP ducked mic 1 (the forced one) alone in 7 of 8, both in 1, mic 0
    //     alone in 0 of 8; 0 fallbacks; 0 cascades; 1 howl block in total,
    //     the longest howl one block. The MUTAP_SLOW sweep (20 runs): mic 0
    //     alone in 1 of 20, 3 howl blocks.
    //   macOS arm64 (CI run 36828198214): mic 1 alone 7 of 8, mic 0 alone
    //     1 of 8 (cabin, no aux); 0 fallbacks; 3 howl blocks, the longest one
    //     block.
    // The claim that matters is that the forced howl is stopped: the
    // longest run of howl blocks stays a block or two (gate 0.1 s, 75
    // blocks) and the total stays small (gate 24 over 8 runs). The wrong mic
    // alone is gated at a margin over the sweep's rate (1 of 20): <= 2 of 8.
    size_t tripped   = 0;
    size_t mic1      = 0;
    size_t wrong     = 0;
    size_t howl      = 0;
    double longest_s = 0.0;
    for (const auto& r : rows) {
        EXPECT_EQ(r.tripped, r.runs) << r.label;
        tripped += r.tripped;
        mic1 += r.mic1_only;
        wrong += r.mic0_only;
        howl += r.howl;
        longest_s = std::max(longest_s, max_of(r.longest_s));
    }
    std::printf("two mics: forced mic alone %zu, wrong mic alone %zu of %zu; howl blocks %zu, longest %.4f s\n", mic1,
                wrong, tripped, howl, longest_s);
    EXPECT_LE(wrong, 2U) << "measured 0 of 8 (Intel), 1 of 8 (arm64); sweep 1 of 20";
    EXPECT_GT(2 * mic1, tripped)
        << "attribution picks the forced mic alone in most runs (measured 7 of 8 on both hosts)";
    EXPECT_LE(howl, 24U) << "measured 1 (Intel), 3 (arm64)";
    EXPECT_LT(longest_s, 0.1) << "measured one block on both hosts";
}

TEST(HowlGuardHost, AudibleCostOnStableMaterial) {
    const std::vector<gd::material_spec> mats = {k_v, k_sp_aux};
    std::vector<cost_stats>              rows(mats.size() * 3);
    std::vector<std::function<void()>>   jobs;
    size_t                               k = 0;
    for (const auto& m : mats) {
        for (const double hz : {0.0, 2.0, 5.0}) {
            rows[k].label = gd::material_name(m) + " " + std::to_string(static_cast<int>(hz)) + " Hz";
            for (const char* room : {"cabin", "mt5"}) {
                for (const unsigned seed : {1U, 21U}) {
                    gd::run_spec s;
                    s.room     = room;
                    s.mat      = m;
                    s.seed     = seed;
                    s.gain_db  = operating_gain(room, m, gd::k_s1);
                    s.cap_db   = cap_for(exact_db(room, gd::k_s1), s.gain_db);
                    s.shift_hz = hz;
                    s.seconds  = 30.0;
                    s.oracle   = false;
                    jobs.emplace_back([s, &st = rows[k]] { st.add(gd::live_run(s)); });
                }
            }
            ++k;
        }
    }
    run_parallel(jobs);
    print_cost_header();
    for (const auto& r : rows) {
        r.print();
    }
    // Measured (cabin, mt5; seeds 1, 21; 30 s): every run declared, 0 ducks
    // after the first OPEN in all 24 runs, 0 blocks at +20 dB; the loudest
    // residual block after OPEN +8.82 / +7.54 / +9.42 dB (voiced+aux, 0 / 2 /
    // 5 Hz) and +19.56 / +15.56 / +18.12 dB (speech+aux), against the
    // harness's +30 dB ceiling.
    for (const auto& r : rows) {
        EXPECT_EQ(r.opened, r.runs) << r.label;
        EXPECT_EQ(r.ducks, 0U) << r.label;
        EXPECT_LT(max_of(r.max_e_db), 25.0) << r.label;
    }
}

TEST(HowlGuardHost, SoundcheckCalibrationOnStableMaterial) {
    // The soundcheck from reset (30 s), its thresholds applied (the policy's
    // default margins), then 30 s more of the same song. Shadow guards over
    // the margin grid ride along; the one with the live guard's margins must
    // track the live guard exactly (the sweep's method).
    const tap::mu::guard_policy        def;
    std::vector<cal_live_stats>        rows(2);
    std::vector<std::function<void()>> jobs;
    std::mutex                         print_mu;
    std::vector<std::string>           prints;
    std::atomic<long>                  twin_diverged{-1};
    // A slice of the sweep's grid (D margin at A' + 3), and the live margins.
    std::vector<std::pair<double, double>> grid = {{0.0, 3.0}, {1.0, 3.0}, {2.0, 3.0}, {3.0, 3.0}};
    grid.emplace_back(def.cal_d_margin_db, def.cal_a_margin_db);
    const size_t twin = grid.size() - 1;
    cal_stats    st(grid);
    size_t       k = 0;
    for (const auto& m : {k_v, k_sp_aux}) {
        rows[k].label = gd::material_name(m);
        for (const char* room : {"cabin", "mt5"}) {
            for (const unsigned seed : {1U, 21U}) {
                auto s      = soundcheck_spec(room, m, seed, 0.0, "");
                s.cal_apply = true;
                s.shadows   = grid;
                jobs.emplace_back([s, room, seed, twin, &st, &rows, k, &print_mu, &prints, &twin_diverged] {
                    const auto t = gd::live_run(s);
                    rows[k].add(t);
                    st.add(std::string(room) + " " + gd::material_name(s.mat), t, false);
                    if (t.shadow[twin].diverged >= 0) {
                        twin_diverged = t.shadow[twin].diverged;
                    }
                    char label[64];
                    std::snprintf(label, sizeof(label), "%s %s seed %u", room, gd::material_name(s.mat).c_str(), seed);
                    std::lock_guard<std::mutex> lock(print_mu);
                    prints.emplace_back(label);
                    print_soundcheck(label, t.cal);
                });
            }
        }
        ++k;
    }
    run_parallel(jobs);
    print_cal_live_header();
    for (const auto& r : rows) {
        r.print();
    }
    st.print_grid();
    // Measured (cabin, mt5; seeds 1, 21; the default margins D + 4 dB,
    // A' + 3 dB): the soundcheck read D's median at -7.05 to -8.35 dB and
    // A''s at -27.15 to -29.55 dB, so d_db landed at -3.05 to -4.35 and
    // a_db at -24.15 to -26.55; 0 ducks after the soundcheck in 8 runs, 0
    // howl blocks. The shadows at A' + 3: D + 0 ducked in 7 of 8, + 1 in 1
    // of 8, + 2 / + 3 / + 4 in 0 (the sweep: + 2 in 3 of 180, + 3 and + 4 in
    // 0 of 180). Gated with margin: at most 2 of 8 runs with a duck.
    size_t duck_runs = 0;
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
        duck_runs += r.duck_runs;
        // the thresholds moved off the factory calibration, by the margin
        EXPECT_LT(max_of(r.d_thr), -1.235) << r.label;
    }
    EXPECT_LE(duck_runs, 2U) << "measured 0 of 8";
    EXPECT_EQ(twin_diverged.load(), -1L) << "the shadow with the live margins must track the live guard";
}

TEST(HowlGuardHost, SoundcheckCalibrationSeesAWalk) {
    // The soundcheck from reset (30 s) at the canceller's limit - 6, applied
    // (the policy's default margins); the walk at 40 s. The factory
    // thresholds saw none of these walks (PR B: 0 of 12 gated, 0 of 30 in
    // the sweep).
    std::vector<walk_stats>            rows(k_walks.size());
    std::vector<cal_live_stats>        pre(k_walks.size());
    std::vector<std::function<void()>> jobs;
    for (size_t r = 0; r < k_walks.size(); ++r) {
        rows[r].label = k_walks[r].first + " -> " + k_walks[r].second;
        pre[r].label  = rows[r].label;
        for (const unsigned seed : {1U, 21U}) {
            auto s      = soundcheck_spec(k_walks[r].first, k_v, seed, 0.0, k_walks[r].second);
            s.cal_apply = true;
            s.oracle    = true;
            jobs.emplace_back([s, &w = rows[r], &p = pre[r]] {
                const auto t = gd::live_run(s);
                w.add(t);
                p.add(t);
            });
        }
    }
    run_parallel(jobs);
    print_cal_live_header();
    for (const auto& r : pre) {
        r.print();
    }
    print_walk_header();
    walk_stats pool;
    pool.label = "pooled";
    for (const auto& r : rows) {
        r.print();
        pool.merge(r);
    }
    pool.print();
    // Measured: 0 ducks between the soundcheck and the walk; 12 of 12 walks
    // ducked (11 on LOST, 0.31 s after the walk, one on a detector TRIP),
    // against 0 of 12 on the factory thresholds; 12 releases, 0 before the
    // misalignment oracle reconverged, release - reconvergence median
    // 1.74 s, minimum 1.63 s; 0 howl blocks. Gated as directions with
    // margin: most walks seen, the release median well after the oracle.
    size_t pre_ducks = 0;
    for (const auto& r : pre) {
        pre_ducks += r.duck_runs;
    }
    EXPECT_LE(pre_ducks, 2U) << "measured 0 of 12";
    EXPECT_GE(pool.ducked, 8U) << "measured 12 of 12 (factory thresholds: 0 of 12)";
    ASSERT_GE(pool.delta_s.size(), 4U);
    EXPECT_GT(med(pool.delta_s), 0.5) << "measured 1.74 s";
    EXPECT_LE(pool.early_all, 2U) << "measured 0 of 12";
    EXPECT_EQ(pool.howl_post, 0U);
}

TEST(HowlGuardHost, BusStageCutsTheReverbRing) {
    double       voice_at[2][6] = {};
    const double offsets[6]     = {0.05, 0.1, 0.25, 0.5, 1.0, 1.5};
    double       trip_after[2]  = {-1.0, -1.0};
    for (const bool bus : {true, false}) {
        dattorro_stage rev(0.5);
        gd::run_spec   s;
        s.room          = "cabin";
        s.mat           = k_v;
        s.seed          = 1;
        s.gain_db       = operating_gain("cabin", k_v, gd::k_s1);
        s.cap_db        = cap_for(exact_db("cabin", gd::k_s1), s.gain_db);
        s.seconds       = 14.0;
        s.oracle        = false;
        s.reverb        = &rev;
        s.bus_stage     = bus;
        s.t_step_s      = 10.0;
        s.step_db       = 12.0;
        const auto t    = gd::live_run(s);
        const auto step = static_cast<long>(gd::blocks_of(10.0));
        long       trip = -1;
        for (auto i = static_cast<size_t>(step); i < t.size() && trip < 0; ++i) {
            if (t.trip[i] != 0) {
                trip = static_cast<long>(i);
            }
        }
        const int b = bus ? 0 : 1;
        std::printf("bus stage %s: first OPEN %.3f s; howl blocks before the step %zu; first TRIP %.3f s after the "
                    "+12 dB step\n",
                    bus ? "on " : "off", secs(gd::first_state(t, guard_state::open)),
                    gd::howl_blocks(t, 0, static_cast<size_t>(step)), trip >= 0 ? secs(trip - step) : -1.0);
        ASSERT_GE(trip, 0);
        trip_after[b]   = secs(trip - step);
        const double at = t.voice_rms[static_cast<size_t>(trip)];
        for (size_t k = 0; k < 6; ++k) {
            const auto i   = static_cast<size_t>(trip) + gd::blocks_of(offsets[k]);
            voice_at[b][k] = 20.0 * std::log10(t.voice_rms[i] / at);
            std::printf("  +%.2f s: voice %.2f dB re the trip block, %s\n", offsets[k], voice_at[b][k],
                        gd::state_name(t.state[i]));
        }
    }
    // Measured (cabin, voiced+aux, Dattorro wet 0.5, +12 dB at 10 s): the
    // first TRIP 0.115 s after the step with and without the bus stage; the
    // voice 0.5 s after it -66.53 dB re the trip block with the bus stage,
    // -47.13 without (19.40 dB apart); 1 s after it -81.39 and -66.25.
    EXPECT_LT(trip_after[0], 0.5);
    EXPECT_LT(voice_at[0][3], voice_at[1][3] - 10.0);
    EXPECT_LT(voice_at[0][4], voice_at[1][4] - 10.0);
}

TEST(HowlGuardHost, CostPerBlock) {
    for (const size_t mics : {size_t{1}, size_t{2}}) {
        const auto [gf, af]  = guard_and_canceller_ns<float>(mics);
        const auto [gdb, ad] = guard_and_canceller_ns<double>(mics);
        std::printf("guard, %zu mic(s): float %.0f ns per block (%.2f %% of one canceller's %.0f ns), "
                    "double %.0f ns (%.2f %% of %.0f ns)\n",
                    mics, gf, 100.0 * gf / af, af, gdb, 100.0 * gdb / ad, ad);
        // Measured (1 / 2 mics; guard ns per block and its share of one
        // canceller's process_block):
        //   macOS x86_64 (Intel, AppleClang): float 2180 / 4983 ns, 0.57 /
        //     1.09 %; double 3292 / 7826 ns, 0.84 / 1.73 % (PR B's run:
        //     2246 / 3962 and 3180 / 6311 ns).
        //   Linux GCC CI (job 110465810942): float 8575 / 12794 ns, 4.61 /
        //     6.86 %; double 8706 / 13571 ns, 4.59 / 7.15 % (the canceller
        //     there 186-190 us, the guard 4x Intel's): GCC did not vectorize
        //     the detector's bank. Since tap/MuTap#87 (run_bank's arrays are
        //     __restrict parameters; bench/README.md), job 113795845819:
        //     float 1998 / 3986 ns, 0.62 / 1.24 %; double 3371 / 6793 ns,
        //     1.03 / 2.07 % (that runner's canceller 321-327 us).
        // A wall-clock ratio on shared CI hardware moves by the factor
        // between those hosts, so it is not gated tightly: the bound only
        // catches a gross regression (25 %, 3.5x over the worst CI row).
        EXPECT_LT(gf / af, 0.25) << "float, " << mics << " mic(s)";
        EXPECT_LT(gdb / ad, 0.25) << "double, " << mics << " mic(s)";
        RecordProperty("guard_ratio_float_m" + std::to_string(mics), std::to_string(gf / af));
        RecordProperty("guard_ratio_double_m" + std::to_string(mics), std::to_string(gdb / ad));
        RecordProperty("guard_ns_float_m" + std::to_string(mics), std::to_string(gf));
        RecordProperty("guard_ns_double_m" + std::to_string(mics), std::to_string(gdb));
    }
}

// ============================================================ the sweep

TEST(HowlGuardSweep, OperatingPoints) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    struct job {
        std::string room;
        size_t      mat;
        size_t      delay;
        unsigned    seed;
        double      asg = 0.0;
    };
    const std::vector<std::string> names = {"voiced+aux", "voiced", "music", "music+aux", "speech", "speech+aux"};
    std::vector<job>               jobs;
    for (const char* room : {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"}) {
        for (size_t m = 0; m < k_cold_mats.size(); ++m) {
            for (const unsigned seed : k_seeds5) {
                jobs.push_back({room, m, gd::k_s1, seed});
            }
        }
    }
    for (const char* room : {"cabin", "mt5"}) {
        for (size_t m = 0; m < k_cold_mats.size(); ++m) {
            for (const unsigned seed : k_seeds5) {
                jobs.push_back({room, m, gd::k_s3, seed});
            }
        }
    }
    struct job2 {
        std::string pair;
        bool        aux;
        unsigned    seed;
        double      asg = 0.0;
    };
    std::vector<job2> jobs2;
    for (const auto& op : k_ops2) {
        for (const bool aux : {false, true}) {
            for (const unsigned seed : k_seeds5) {
                jobs2.push_back({op.pair, aux, seed});
            }
        }
    }
    std::vector<std::function<void()>> fns;
    for (auto& j : jobs) {
        fns.emplace_back(
            [&j] { j.asg = gd::limit_db(j.room, k_cold_mats[j.mat], j.delay, j.seed) - exact_db(j.room, j.delay); });
    }
    for (auto& j : jobs2) {
        fns.emplace_back([&j] {
            const double ex = mutap_test::exact_msg_db(
                mutap_test::two_mic::path_sum(mutap_test::two_mic::room_pair(j.pair)), gd::k_s1);
            j.asg = gd::limit2_db(j.pair, j.aux, gd::k_s1, j.seed) - ex;
        });
    }
    run_parallel(fns);
    std::printf("operating points: converged-canceller limit over exact_msg_db, dB, per seed and the median\n");
    for (const char* room : {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"}) {
        for (size_t m = 0; m < k_cold_mats.size(); ++m) {
            std::vector<double> s1;
            std::vector<double> s3;
            for (const auto& j : jobs) {
                if (j.room == room && j.mat == m) {
                    (j.delay == gd::k_s1 ? s1 : s3).push_back(j.asg);
                }
            }
            char s3_text[32] = "k_nan";
            if (!s3.empty()) {
                std::snprintf(s3_text, sizeof(s3_text), "%.2f", med(s3));
            }
            std::printf("        {\"%s\", \"%s\", %.2f, %s},  // S1", room, gd::material_name(k_cold_mats[m]).c_str(),
                        med(s1), s3_text);
            for (const double v : s1) {
                std::printf(" %+.2f", v);
            }
            if (!s3.empty()) {
                std::printf("; S3");
                for (const double v : s3) {
                    std::printf(" %+.2f", v);
                }
            }
            std::printf("\n");
        }
    }
    for (const auto& op : k_ops2) {
        std::vector<double> plain;
        std::vector<double> aux;
        for (const auto& j : jobs2) {
            if (j.pair == op.pair) {
                (j.aux ? aux : plain).push_back(j.asg);
            }
        }
        std::printf("        {\"%s\", %.2f, %.2f},  // plain", op.pair, med(plain), med(aux));
        for (const double v : plain) {
            std::printf(" %+.2f", v);
        }
        std::printf("; aux");
        for (const double v : aux) {
            std::printf(" %+.2f", v);
        }
        std::printf("\n");
    }
}

TEST(HowlGuardSweep, ColdStart) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    const std::vector<std::string> rooms6 = {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"};
    print_cold_header();
    {
        cold_spec c;
        c.rooms = rooms6;
        c.mats  = k_cold_mats;
        c.seeds = k_seeds5;
        c.twins = {"voiced+aux", "speech"};
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
    {
        cold_spec c;
        c.rooms = {"cabin", "mt5"};
        c.mats  = k_cold_mats;
        c.seeds = k_seeds5;
        c.delay = gd::k_s3;
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
    for (const double hz : {2.0, 5.0}) {
        cold_spec c;
        c.rooms    = {"cabin", "mt5"};
        c.mats     = k_cold_mats;
        c.seeds    = k_seeds5;
        c.shift_hz = hz;
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
    for (const double rel : {3.0, 6.0}) {
        cold_spec c;
        c.rooms = {"cabin", "mt5", "rehearsal", "hall"};
        c.mats  = k_cold_mats;
        c.seeds = k_seeds5;
        c.rel   = rel;
        c.twins = {"voiced+aux", "speech"};
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
    {
        cold_spec c;
        c.rooms = rooms6;
        c.mats  = {k_h, k_m, k_sp};
        c.seeds = k_seeds5;
        c.cap   = false;
        std::printf("  (no cap)\n");
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
}

TEST(HowlGuardSweep, Walks) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    const std::vector<std::pair<std::string, std::string>> walks = {{"studio", "rehearsal"}, {"rehearsal", "hall"},
                                                                    {"hall", "cabin"},       {"cabin", "studio"},
                                                                    {"mt5", "mt105"},        {"mt9", "mt109"}};
    print_walk_header();
    for (const double rel : {-6.0, std::nan("")}) {
        std::printf(std::isnan(rel) ? "  at the canceller's limit - 6 dB\n" : "  at exact_msg_db - 6 dB\n");
        walk_stats pool;
        pool.label = "pooled";
        for (const auto& r : walk_rows(walks, k_seeds5, rel, 25.0, false)) {
            r.print();
            pool.merge(r);
        }
        pool.print();
    }
    std::vector<walk_stats> ug;
    const auto              hot = walk_rows({{"rehearsal", "hall"}, {"hall", "cabin"}}, k_seeds5, 6.0, 25.0, true, &ug);
    for (size_t i = 0; i < hot.size(); ++i) {
        hot[i].print();
        ug[i].print();
    }
}

TEST(HowlGuardSweep, LouderCoupling) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    const std::vector<std::string> rooms = {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"};
    print_rearm_header();
    // 30 s (the gated rows' length) and 120 s (where the back-off ends).
    for (const double seconds : {30.0, 120.0}) {
        for (const bool lost_strikes : {false, true}) {
            std::printf("  %.0f s, lost_in_probation_strikes %s\n", seconds, lost_strikes ? "on" : "off");
            const auto rows = louder_rows(rooms, k_seeds5, lost_strikes, seconds);
            for (const auto& r : rows) {
                r.print();
            }
            rearm_total(rows);
        }
    }
}

TEST(HowlGuardSweep, TwoMicsAndAudibleCost) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    {
        std::vector<two_mic_stats>         rows(2 * std::size(k_ops2));
        std::vector<std::function<void()>> jobs;
        size_t                             k = 0;
        for (const auto& op : k_ops2) {
            for (const bool aux : {false, true}) {
                rows[k].label = std::string(op.pair) + (aux ? " +aux" : "");
                for (const unsigned seed : k_seeds5) {
                    gd::two_mic_spec s;
                    s.pair          = op.pair;
                    s.aux           = aux;
                    s.seed          = seed;
                    const double ex = mutap_test::exact_msg_db(
                        mutap_test::two_mic::path_sum(mutap_test::two_mic::room_pair(op.pair)), gd::k_s1);
                    s.gain_db = ex + (aux ? op.aux : op.plain) - 6.0;
                    s.cap_db  = cap_for(ex, s.gain_db);
                    jobs.emplace_back([s, &st = rows[k]] { st.add(gd::two_mic_run(s)); });
                }
                ++k;
            }
        }
        run_parallel(jobs);
        print_two_mic_header();
        for (const auto& r : rows) {
            r.print();
        }
    }
    const std::vector<gd::material_spec> mats = {k_v, k_sp_aux};
    std::vector<cost_stats>              rows(mats.size() * 3);
    std::vector<std::function<void()>>   jobs;
    size_t                               k = 0;
    for (const auto& m : mats) {
        for (const double hz : {0.0, 2.0, 5.0}) {
            rows[k].label = gd::material_name(m) + " " + std::to_string(static_cast<int>(hz)) + " Hz";
            for (const char* room : {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"}) {
                for (const unsigned seed : k_seeds5) {
                    gd::run_spec s;
                    s.room     = room;
                    s.mat      = m;
                    s.seed     = seed;
                    s.gain_db  = operating_gain(room, m, gd::k_s1);
                    s.cap_db   = cap_for(exact_db(room, gd::k_s1), s.gain_db);
                    s.shift_hz = hz;
                    s.seconds  = 30.0;
                    s.oracle   = false;
                    jobs.emplace_back([s, &st = rows[k]] { st.add(gd::live_run(s)); });
                }
            }
            ++k;
        }
    }
    run_parallel(jobs);
    print_cost_header();
    for (const auto& r : rows) {
        r.print();
    }
}

TEST(HowlGuardSweep, CalibrationMargins) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    // Shadow guards over the margin grid; the live guard keeps the factory
    // thresholds, so every shadow's first duck is exact unless the live
    // loop diverged first (counted as inexact).
    const auto                         grid = margin_grid();
    cal_stats                          st(grid);
    std::vector<std::function<void()>> jobs;
    for (const char* room : {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"}) {
        for (const auto& m : {k_v, k_sp_aux}) {
            for (const double hz : {0.0, 2.0, 5.0}) {
                for (const unsigned seed : k_seeds5) {
                    auto s    = soundcheck_spec(room, m, seed, hz, "");
                    s.shadows = grid;
                    const std::string group =
                        std::string(room) + " " + gd::material_name(m) + " " + std::to_string(static_cast<int>(hz));
                    jobs.emplace_back([s, group, &st] { st.add(group, gd::live_run(s), false); });
                }
            }
        }
    }
    for (const auto& [from, to] : k_walks) {
        for (const unsigned seed : k_seeds5) {
            auto s                  = soundcheck_spec(from, k_v, seed, 0.0, to);
            s.shadows               = grid;
            const std::string group = from + " to " + to;
            jobs.emplace_back([s, group, &st] { st.add(group, gd::live_run(s), true); });
        }
    }
    run_parallel(jobs);
    st.print_soundchecks();
    st.print_grid();
}

TEST(HowlGuardSweep, CalibrationApplied) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the guard sweep";
    }
    // The live guard on its soundcheck thresholds (the policy's default
    // margins) over CalibrationMargins' grid.
    std::vector<cal_live_stats>        stable(6);
    std::vector<walk_stats>            walks(k_walks.size());
    std::vector<cal_live_stats>        pre(k_walks.size());
    std::vector<std::function<void()>> jobs;
    size_t                             k = 0;
    for (const auto& m : {k_v, k_sp_aux}) {
        for (const double hz : {0.0, 2.0, 5.0}) {
            stable[k].label = gd::material_name(m) + " " + std::to_string(static_cast<int>(hz)) + " Hz";
            for (const char* room : {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"}) {
                for (const unsigned seed : k_seeds5) {
                    auto s      = soundcheck_spec(room, m, seed, hz, "");
                    s.cal_apply = true;
                    jobs.emplace_back([s, &st = stable[k]] { st.add(gd::live_run(s)); });
                }
            }
            ++k;
        }
    }
    for (size_t r = 0; r < k_walks.size(); ++r) {
        walks[r].label = k_walks[r].first + " -> " + k_walks[r].second;
        pre[r].label   = walks[r].label;
        for (const unsigned seed : k_seeds5) {
            auto s      = soundcheck_spec(k_walks[r].first, k_v, seed, 0.0, k_walks[r].second);
            s.cal_apply = true;
            s.oracle    = true;
            jobs.emplace_back([s, &w = walks[r], &p = pre[r]] {
                const auto t = gd::live_run(s);
                w.add(t);
                p.add(t);
            });
        }
    }
    run_parallel(jobs);
    print_cal_live_header();
    for (const auto& r : stable) {
        r.print();
    }
    for (const auto& r : pre) {
        r.print();
    }
    print_walk_header();
    walk_stats pool;
    pool.label = "pooled";
    for (const auto& r : walks) {
        r.print();
        pool.merge(r);
    }
    pool.print();
}
