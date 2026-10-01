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
// and mt5 (both generator families); each takes under 90 s on 4 threads
// (596.71 s for all 13 in a full ctest run on the Intel Mac).
// HowlGuardSweep.* (MUTAP_SLOW=1) re-measures the operating points and runs
// the long grids (six rooms, five seed sets) that docs/howl-guard.md quotes. Every threshold below is a
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
            capped += gd::first_state(t, guard_state::open_capped) >= 0 ? 1U : 0U;
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
                "  %-26s %4zu | %3zu %6.2f %6.2f | %3zu %3zu | %6.2f | %5zu %3zu | %6.2f %6.2f | %5zu %3zu/%zu\n",
                label.c_str(), runs, t_open.size(), med(t_open), max_of(t_open), never, capped, med(arming_s), howl,
                howl_runs, med(unstable_s), max_of(unstable_s), ungrd_howl, ungrd_howl_runs, ungrd_runs);
        }
    };

    void print_cold_header() {
        std::printf("  %-26s %4s | %3s %6s %6s | %3s %3s | %6s | %5s %3s | %6s %6s | %5s %s\n", "row", "runs", "dec",
                    "open", "max", "nvr", "cap", "arm s", "howl", "run", "unst", "max", "ungd", "runs");
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
        std::vector<double> rearm_s;        ///< first re-arm - first duck
        size_t              pumps      = 0; ///< LOST-ducks after the first re-arm
        size_t              pump_runs  = 0;
        size_t              lost_ducks = 0; ///< LOST-ducks after the change
        size_t              trip_ducks = 0; ///< TRIP-ducks after the change
        size_t              howl_after = 0; ///< howl blocks after the change
        std::vector<double> ducked_frac;    ///< share of the post-change time ducked or releasing
        std::mutex          mu;

        void add(const gd::run_trace& t) {
            std::lock_guard<std::mutex> lock(mu);
            ++runs;
            const auto c    = static_cast<size_t>(t.change);
            const long duck = gd::first_state(t, guard_state::ducked, t.change);
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
            ducked_frac.push_back(static_cast<double>(ducked_blocks) / static_cast<double>(t.size() - c));
        }
        void print() const {
            std::printf("  %-22s %3zu | %3zu %5.2f | %3zu %5.2f | %4zu %3zu | %3zu %3zu | %5zu | %5.2f\n",
                        label.c_str(), runs, ducked, med(duck_s), rearmed, med(rearm_s), pumps, pump_runs, lost_ducks,
                        trip_ducks, howl_after, med(ducked_frac));
        }
    };

    void print_rearm_header() {
        std::printf("  %-22s %3s | %3s %5s | %3s %5s | %4s %3s | %3s %3s | %5s | %5s\n", "row", "n", "dck", "duck",
                    "rea", "rearm", "pump", "run", "lst", "trp", "howl", "dfrac");
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
    // Measured: 0 howl blocks in every row; no run declared in 20 s (the
    // canceller at the arming gain sees no excitation without the backing
    // track); every run left ARMING through the cap at arming_timeout_s
    // (6 of 6 OPEN_CAPPED per row). The unguarded speech twins: 1 run of 6
    // reached the 40 dB rule (1 block).
    for (const auto& r : rows) {
        EXPECT_EQ(r.howl, 0U) << r.label;
        EXPECT_EQ(r.never, 0U) << r.label;
        EXPECT_EQ(r.capped, r.runs) << r.label;
    }
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
    std::vector<rearm_stats>           rows(4);
    const char*                        rooms[] = {"cabin", "mt5", "studio", "hall"};
    std::vector<std::function<void()>> jobs;
    for (size_t r = 0; r < 4; ++r) {
        rows[r].label = std::string(rooms[r]) + " x2";
        for (const unsigned seed : {1U, 21U}) {
            gd::run_spec s;
            s.room       = rooms[r];
            s.scale2     = 2.0;
            s.t_change_s = 10.0;
            s.mat        = k_v;
            s.seed       = seed;
            s.gain_db    = operating_gain(s.room, k_v, gd::k_s1);
            s.cap_db     = cap_for(exact_db(s.room, gd::k_s1), s.gain_db);
            s.seconds    = 30.0;
            s.oracle     = false;
            jobs.emplace_back([s, &st = rows[r]] { st.add(gd::live_run(s)); });
        }
    }
    run_parallel(jobs);
    print_rearm_header();
    for (const auto& r : rows) {
        r.print();
    }
    // Measured: every run ducked after F -> 2F (2 of 2 per room; LOST 0.37
    // to 0.39 s after the change, cabin 4.65 s); the timer re-arm at 5.00 s
    // (10.01 s after a strike); 0 howl blocks after the change; the post-
    // change time ducked or releasing, median 0.56 / 0.96 / 0.94 / 0.93.
    // The edge rule does NOT stop the duck / open cycle: the verdict reads
    // ok again after the re-arm, which re-arms LOST: 16 LOST-ducks after a
    // re-arm in 6 of 8 runs (docs/howl-guard.md). Printed, not gated.
    for (const auto& r : rows) {
        EXPECT_EQ(r.ducked, r.runs) << r.label;
        EXPECT_EQ(r.howl_after, 0U) << r.label;
        EXPECT_GT(med(r.ducked_frac), 0.5) << r.label;
    }
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
        // Measured (1 / 2 mics): float 2246 / 3962 ns, 0.60 / 1.02 % of a
        // canceller block; double 3180 / 6311 ns, 0.88 / 1.59 %. Timing on a
        // shared machine: the bound has 3x margin over the 2-mic double row.
        EXPECT_LT(gf / af, 0.05 * static_cast<double>(mics) / 2.0 + 0.02);
        EXPECT_LT(gdb / ad, 0.05 * static_cast<double>(mics) / 2.0 + 0.02);
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
        c.rooms = {"cabin", "mt5"};
        c.mats  = {k_h, k_m};
        c.seeds = k_seeds5;
        c.cap   = false;
        std::printf("  (no cap)\n");
        for (const auto& r : cold_rows(c)) {
            r.print();
        }
    }
}

TEST(HowlGuardSweep, WalksAndLouderCoupling) {
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
    std::vector<rearm_stats>           rows(6);
    const char*                        rooms[] = {"cabin", "mt5", "studio", "rehearsal", "hall", "mt9"};
    std::vector<std::function<void()>> jobs;
    for (size_t r = 0; r < 6; ++r) {
        rows[r].label = std::string(rooms[r]) + " x2";
        for (const unsigned seed : k_seeds5) {
            gd::run_spec s;
            s.room       = rooms[r];
            s.scale2     = 2.0;
            s.t_change_s = 10.0;
            s.mat        = k_v;
            s.seed       = seed;
            s.gain_db    = operating_gain(s.room, k_v, gd::k_s1);
            s.cap_db     = cap_for(exact_db(s.room, gd::k_s1), s.gain_db);
            s.seconds    = 30.0;
            s.oracle     = false;
            jobs.emplace_back([s, &st = rows[r]] { st.add(gd::live_run(s)); });
        }
    }
    run_parallel(jobs);
    print_rearm_header();
    for (const auto& r : rows) {
        r.print();
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
