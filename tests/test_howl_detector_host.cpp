// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The howl detector's acoustic claims (mutap/howl_detector.h), host-only:
// latency on loop-born howls against the 40 dB rule, false trips per
// material, and faust-icc's detector side by side on the same signals
// (tests/support/howl_runs.h has the measurement). The gated rows below run
// in about a minute on 4 threads; the MUTAP_SLOW sweep (HowlDetectorSweep.*)
// has the shifted, live and long rows the header's tables quote.

#include <algorithm>
#include <atomic>
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

#include "support/howl_runs.h"

namespace {

    namespace hw = mutap_test::howl;
    using hw::material;

    constexpr double k_ms_per_block = 64.0 / 48.0;

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

    /// Nearest-rank percentile (p in [0, 100]); NaN for an empty sample.
    double percentile(std::vector<double> v, double p) {
        if (v.empty()) {
            return std::nan("");
        }
        std::sort(v.begin(), v.end());
        const auto k = static_cast<size_t>(std::ceil(p / 100.0 * static_cast<double>(v.size())));
        return v[std::min(v.size() - 1, k == 0 ? 0 : k - 1)];
    }

    // ------------------------------------------------------------- latency

    /// One latency cell: the per-path leads (ms, negative = before the 40 dB
    /// rule) of the runs that howled, and the misses (a path that never
    /// fired before the run ended, 0.5 s past the rule).
    struct path_stats {
        std::vector<double> lead_ms;
        size_t              misses = 0;
        void                add(long first, long rule) {
            if (first < 0) {
                ++misses;
            }
            else {
                lead_ms.push_back(static_cast<double>(first - rule) * k_ms_per_block);
            }
        }
        double median() const { return percentile(lead_ms, 50.0); }
        double p95() const { return percentile(lead_ms, 95.0); }
        double max() const {
            return lead_ms.empty() ? std::nan("") : *std::max_element(lead_ms.begin(), lead_ms.end());
        }
    };

    struct latency_row {
        std::string         label;
        size_t              runs = 0, no_howl = 0, early_trips = 0;
        path_stats          growth, ceiling, level, any, icc;
        std::vector<double> hz;      ///< line_hz at the growth path's trip
        std::vector<double> howl_hz; ///< the howl's spectral peak at the rule
        void                add(const hw::trace& t) {
            ++runs;
            if (t.step > 0 && t.first_any(0) >= 0 && t.first_any(0) < t.step) {
                ++early_trips; // fired while the loop was still converging below its limit
            }
            const long rule = t.first_rule(t.step);
            if (rule < 0) {
                ++no_howl;
                return;
            }
            howl_hz.push_back(t.howl_hz);
            const long g = t.first_growth(t.step);
            growth.add(g, rule);
            ceiling.add(t.first_ceiling(t.step), rule);
            level.add(t.first_level(t.step), rule);
            any.add(t.first_any(t.step), rule);
            icc.add(t.first_icc(t.step), rule);
            if (g >= 0) {
                hz.push_back(t.line_hz[static_cast<size_t>(g)]);
            }
        }
    };

    void print_latency_header() {
        std::printf("lead of each path's first trip over the 40 dB rule, ms (negative = first); miss = never fired\n");
        std::printf("%-22s %5s %4s | %-27s | %-18s | %-18s | %-18s | %-18s\n", "loop, delay, shift", "howls", "none",
                    "growth miss  med  p95  max", "ceiling miss med max", "level miss  med max", "any miss med max",
                    "icc miss  med  max");
    }

    void print_latency(const latency_row& r) {
        std::printf("%-22s %5zu %4zu | %4zu %7.1f %6.1f %6.1f | %3zu %6.1f %6.1f | %3zu %6.1f %6.1f | %3zu %6.1f "
                    "%6.1f | %3zu %6.1f %6.1f\n",
                    r.label.c_str(), r.runs - r.no_howl, r.no_howl, r.growth.misses, r.growth.median(), r.growth.p95(),
                    r.growth.max(), r.ceiling.misses, r.ceiling.median(), r.ceiling.max(), r.level.misses,
                    r.level.median(), r.level.max(), r.any.misses, r.any.median(), r.any.max(), r.icc.misses,
                    r.icc.median(), r.icc.max());
        if (!r.howl_hz.empty()) {
            const auto out =
                std::count_if(r.howl_hz.begin(), r.howl_hz.end(), [](double f) { return f < 150.0 || f > 16000.0; });
            std::printf("%-22s howl frequency at the rule %.0f..%.0f Hz (median %.0f), %td outside 150-16000 Hz; "
                        "above 6000 Hz: %td, above 8000 Hz: %td\n",
                        "", *std::min_element(r.howl_hz.begin(), r.howl_hz.end()),
                        *std::max_element(r.howl_hz.begin(), r.howl_hz.end()), percentile(r.howl_hz, 50.0), out,
                        std::count_if(r.howl_hz.begin(), r.howl_hz.end(), [](double f) { return f > 6000.0; }),
                        std::count_if(r.howl_hz.begin(), r.howl_hz.end(), [](double f) { return f > 8000.0; }));
        }
    }

    // --------------------------------------------------------- false trips

    struct false_row {
        std::string label;
        double      seconds = 0.0;
        size_t      trips = 0, growth = 0, ceiling = 0, level = 0, icc = 0;
        double      max_prom = -1e9, max_rise = 0.0, max_level_gated = std::nan(""), max_power = -1e9;
        double      icc_max_prom = -1e9, icc_max_conf = 0.0;
        size_t      clean = 0, growth_clean = 0, icc_clean = 0, cold = 0, howled = 0;
        void        add(const hw::trace& t) {
            const size_t n    = t.size();
            const auto   from = static_cast<size_t>(t.step);
            if (t.first_rule(t.step) >= 0) {
                ++howled; // a "stable" loop that reached the 40 dB rule: a howl, not a false trip
                return;
            }
            const auto burst        = hw::burst_oracle(t.rms);
            auto       clean_events = [&](auto cond) {
                return t.events([&](size_t i) { return cond(i) && burst[i] == 0; }, from, n);
            };
            seconds += static_cast<double>(n - from) * k_ms_per_block / 1000.0;
            cold += t.events([&](size_t i) { return t.tripped[i] != 0; }, 0, from);
            trips += t.events([&](size_t i) { return t.tripped[i] != 0; }, from, n);
            clean += clean_events([&](size_t i) { return t.tripped[i] != 0; });
            growth += t.events([&](size_t i) { return t.growth_at(i); }, from, n);
            growth_clean += clean_events([&](size_t i) { return t.growth_at(i); });
            ceiling += t.events([&](size_t i) { return t.ceiling_at(i); }, from, n);
            level += t.events([&](size_t i) { return t.level_at(i); }, from, n);
            icc += t.events([&](size_t i) { return t.icc_at(i); }, from, n);
            icc_clean += clean_events([&](size_t i) { return t.icc_at(i); });
            for (size_t i = from; i < n; ++i) {
                max_prom  = std::max(max_prom, t.prom[i]);
                max_rise  = std::max(max_rise, t.rise[i]);
                max_power = std::max(max_power, t.power_db[i]);
                if (t.prom[i] >= t.cfg.level_prominence_db) {
                    // NaN (printed nan) until a block passes the gate: std::max(NaN, x) is NaN, so start from x
                    max_level_gated =
                        std::isnan(max_level_gated) ? t.level_db[i] : std::max(max_level_gated, t.level_db[i]);
                }
                icc_max_prom = std::max(icc_max_prom, t.icc_prom[i]);
                icc_max_conf = std::max(icc_max_conf, t.icc_conf[i]);
            }
        }
    };

    void print_false_header() {
        std::printf("trips = rising edges of tripped(); /c = of those, not within 0.5 s of a block at +20 dB (the "
                    "burst oracle); X = integrated rise\n");
        std::printf("%-34s %5s | %8s %8s %4s %4s | %6s %6s %6s %7s | %8s %6s %5s | %4s\n", "material", "s", "trips/c",
                    "growth/c", "ceil", "lvl", "max P", "max X", "L|P_L", "dBre1", "icc/c", "iccP", "conf", "cold");
    }

    void print_false(const false_row& r) {
        if (r.howled > 0 && r.seconds == 0.0) {
            std::printf("%-34s howled (reached the 40 dB rule): counted as a howl\n", r.label.c_str());
            return;
        }
        std::printf(
            "%-34s %5.0f | %4zu/%-3zu %4zu/%-3zu %4zu %4zu | %6.2f %6.2f %6.2f %7.2f | %4zu/%-3zu %6.2f %5.3f | %4zu\n",
            r.label.c_str(), r.seconds, r.trips, r.clean, r.growth, r.growth_clean, r.ceiling, r.level, r.max_prom,
            r.max_rise, r.max_level_gated, r.max_power, r.icc, r.icc_clean,
            hw::icc_prominence_over_others(r.icc_max_prom), r.icc_max_conf, r.cold);
    }

    const std::vector<std::string> k_rooms = {"cabin", "studio", "rehearsal", "hall", "mt5", "mt9"};

    std::string row_label(const char* loop, size_t delay, double shift_hz) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s, %s, %.0f Hz", loop, delay == 480 ? "S1" : "S3", shift_hz);
        return buf;
    }

    /// The latency table over rooms x {+1, +3} x {voiced, speech} x seed
    /// sets for each (delay, shift) of a dry loop; rows in (delay, shift)
    /// order.
    std::vector<latency_row> dry_latency(const std::vector<double>& shifts, const std::vector<size_t>& delays) {
        std::vector<latency_row>           rows;
        std::vector<std::function<void()>> jobs;
        std::mutex                         mu;
        for (const size_t d : delays) {
            for (const double sh : shifts) {
                rows.emplace_back();
                rows.back().label = row_label("dry", d, sh);
            }
        }
        size_t r = 0;
        for (const size_t d : delays) {
            for (const double sh : shifts) {
                for (const auto& room : k_rooms) {
                    for (const double over : {1.0, 3.0}) {
                        for (const material m : {material::voiced, material::speech}) {
                            for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                                const unsigned seed = mutap_test::seed_in_set(m == material::speech ? 2U : 1U, set);
                                jobs.emplace_back([&rows, &mu, r, room, d, sh, over, m, seed] {
                                    const hw::trace t =
                                        hw::dry_howl(room, d, sh, over, m, seed, sh > 0.0 ? 20.0 : 30.0);
                                    const std::lock_guard<std::mutex> lock(mu);
                                    rows[r].add(t);
                                });
                            }
                        }
                    }
                }
                ++r;
            }
        }
        run_parallel(jobs);
        return rows;
    }

} // namespace

// ------------------------------------------------------------------ latency

// Dry loops (no canceller) just above their limit: plain at exact_msg_db +1
// and +3 dB, six rooms (both generator families), voiced and speech, five
// seed sets, S1 and S3: 240 howls. Measured on the host (macOS x86_64,
// AppleClang 17, Release), lead over the 40 dB rule in ms:
//
//                 growth path alone          ceiling (+30)      any path   faust-icc
//               miss   med    p95    max    miss   med   max    miss  max  miss   med   max
//   S1, 0 Hz     10  -74.7  152.0  422.7      0 -118.7 -38.7      0 -41.3    0 -166.7 292.0
//   S3, 0 Hz      6  -98.7  146.7  465.3      0 -218.7 -70.7      0 -77.3   16 -278.7 229.3
//
// The claims: the growth path alone (level catch off) leads the rule in the
// median by more than 30 ms and misses at most 1 in 6 (the misses are howls
// whose line never reaches P = 18 dB or never rises 20 dB linearly before
// the rule: broadband and multi-line onsets); the ceiling, 10 dB under the
// rule, is first in every run by construction, so "any path" misses none.
TEST(HowlDetectorHost, DryLoopLatencyPlain) {
    const auto rows = dry_latency({0.0}, {480, 960});
    print_latency_header();
    for (const auto& r : rows) {
        print_latency(r);
        EXPECT_EQ(r.no_howl, 0U) << r.label;
        EXPECT_LT(r.growth.median(), -30.0) << r.label << ": measured -74.7 (S1) / -98.7 (S3)";
        EXPECT_LE(r.growth.misses, 20U) << r.label << ": measured 10 (S1) / 6 (S3) of 120";
        EXPECT_EQ(r.any.misses, 0U) << r.label;
        EXPECT_LT(r.any.max(), 0.0) << r.label << ": measured -41.3 (S1) / -77.3 (S3)";
    }
    RecordProperty("growth_median_lead_ms_s1", std::to_string(rows[0].growth.median()));
    RecordProperty("growth_median_lead_ms_s3", std::to_string(rows[1].growth.median()));
}

// ------------------------------------------------------------ false trips

// Every material for 60 s as an open signal (no loop), at the S1 and S3
// loop periods: 120 s each. Measured: 0 trips on any path for all twelve;
// the extremes beside the thresholds (P 18, X 20, L 25 dB gated at 18):
// prominence at most 18.85 dB (onsets) and 18.56 (music), integrated rise
// at most 0.68 dB (music), the gated relative level at most 3.66 dB (music)
// - so the growth path's margin is in X, not in P. faust-icc on the same
// signals: 24 trips on music, 0 elsewhere.
TEST(HowlDetectorHost, OpenMaterialsDoNotTrip) {
    std::vector<false_row>             rows;
    std::vector<std::function<void()>> jobs;
    std::mutex                         mu;
    for (const auto m : hw::k_all_materials) {
        rows.emplace_back();
        rows.back().label = std::string(hw::name(m));
    }
    for (size_t i = 0; i < hw::k_all_materials.size(); ++i) {
        for (const size_t d : {size_t{480}, size_t{960}}) {
            jobs.emplace_back([&rows, &mu, i, d] {
                const hw::trace                   t = hw::open_run(hw::k_all_materials[i], 1U, 60.0, d);
                const std::lock_guard<std::mutex> lock(mu);
                rows[i].add(t);
            });
        }
    }
    run_parallel(jobs);
    print_false_header();
    for (const auto& r : rows) {
        print_false(r);
        EXPECT_EQ(r.trips, 0U) << r.label;
        EXPECT_LT(r.max_rise, 10.0) << r.label << ": measured at most 0.68 (X = 20)";
    }
}

namespace {

    /// One stable-loop residual row: the live loop at the canceller's limit
    /// - 6 dB for `seconds` (convergence included).
    struct stable_case {
        std::string       room;
        double            shift_hz = 0.0;
        hw::live_material lm;
        std::string       label;
    };

    std::vector<stable_case> stable_cases(const std::vector<std::string>& rooms, const std::vector<double>& shifts) {
        std::vector<stable_case> out;
        const material           singer[] = {material::voiced,  material::music,     material::unison,
                                             material::speech,  material::white,     material::onsets,
                                             material::vibrato, material::crescendo, material::entrance};
        for (const auto& room : rooms) {
            for (const double sh : shifts) {
                const std::string where = room + ", S1, " + std::to_string(static_cast<int>(sh)) + " Hz: ";
                for (const material m : singer) {
                    out.push_back({room, sh, {m, false, std::nan("")}, where + std::string(hw::name(m))});
                }
                out.push_back({room, sh, {material::voiced, false, -12.0}, where + "voiced + aux -12"});
                out.push_back({room, sh, {material::voiced, false, 0.0}, where + "voiced + aux 0"});
                out.push_back({room, sh, {material::voiced, true, 0.0}, where + "aux 0 alone"});
                out.push_back({room, sh, {material::entrance, false, 0.0}, where + "entrance over aux 0"});
            }
        }
        return out;
    }

    std::vector<false_row> stable_rows(const std::vector<stable_case>& cases, double seconds) {
        std::vector<false_row>             rows(cases.size());
        std::vector<std::function<void()>> jobs;
        for (size_t i = 0; i < cases.size(); ++i) {
            rows[i].label = cases[i].label;
            jobs.emplace_back([&rows, &cases, i, seconds] {
                const auto& c = cases[i];
                rows[i].add(hw::live_run(c.room, 480, c.shift_hz, -6.0, c.lm, mutap_test::seed_in_set(1U, 0), seconds));
            });
        }
        run_parallel(jobs);
        return rows;
    }

} // namespace

// The residual of a stable loop at the canceller's limit - 6 dB, 60 s after
// 2 s of convergence: the voiced singer, and the entrance (3 s of silence,
// 4 s of voice) over a backing track at the singer's level, in the cabin at
// S1. Measured: 0 trips on both; integrated rise at most 12.54 / 16.29 dB
// (X = 20), prominence at most 21.99 / 25.87 dB. faust-icc: 30 trips on the
// voiced residual. A single closed-loop trajectory is chaotic across
// platforms (HANDOFF working note 2), so the gate is on trips the burst
// oracle does not explain (none within 0.5 s of a block at +20 dB), which
// is what the guard's audible cost counts; the MUTAP_SLOW sweep has the
// 78-run table, where the shifted loops do trip (see the header).
TEST(HowlDetectorHost, StableLoopResidualDoesNotTrip) {
    std::vector<stable_case> cases;
    cases.push_back({"cabin", 0.0, {material::voiced, false, std::nan("")}, "cabin, S1, 0 Hz: voiced"});
    cases.push_back({"cabin", 0.0, {material::entrance, false, 0.0}, "cabin, S1, 0 Hz: entrance over aux 0"});
    const auto rows = stable_rows(cases, 60.0);
    print_false_header();
    for (const auto& r : rows) {
        print_false(r);
        EXPECT_EQ(r.howled, 0U) << r.label;
        EXPECT_EQ(r.clean, 0U) << r.label << ": measured 0 trips";
    }
}

// --------------------------------------------------------- MUTAP_SLOW sweeps

TEST(HowlDetectorSweep, DryLoopLatencyShifted) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the howl detector sweep";
    }
    const auto rows = dry_latency({2.0, 5.0}, {480, 960});
    print_latency_header();
    for (const auto& r : rows) {
        print_latency(r);
        RecordProperty(r.label + ": growth median lead ms", std::to_string(r.growth.median()));
    }
}

TEST(HowlDetectorSweep, LiveLoopLatency) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the howl detector sweep";
    }
    std::vector<latency_row>           rows;
    std::vector<std::function<void()>> jobs;
    std::mutex                         mu;
    const std::vector<size_t>          delays = {480, 960};
    const std::vector<double>          shifts = {0.0, 2.0, 5.0};
    for (const size_t d : delays) {
        for (const double sh : shifts) {
            rows.emplace_back();
            rows.back().label = row_label("PEM +1", d, sh);
        }
    }
    size_t r = 0;
    for (const size_t d : delays) {
        for (const double sh : shifts) {
            for (const std::string room : {"cabin", "mt5"}) {
                for (const material m : {material::voiced, material::speech}) {
                    for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                        const unsigned seed = mutap_test::seed_in_set(m == material::speech ? 2U : 1U, set);
                        jobs.emplace_back([&rows, &mu, r, room, d, sh, m, seed] {
                            const hw::trace t = hw::live_run(room, d, sh, 1.0, {m, false, std::nan("")}, seed, 20.0);
                            const std::lock_guard<std::mutex> lock(mu);
                            rows[r].add(t);
                        });
                    }
                }
            }
            ++r;
        }
    }
    run_parallel(jobs);
    print_latency_header();
    for (const auto& row : rows) {
        print_latency(row);
        RecordProperty(row.label + ": growth median lead ms", std::to_string(row.growth.median()));
        std::printf("    %s: tripped before the step (converging at exact - 6): %zu of %zu runs\n", row.label.c_str(),
                    row.early_trips, row.runs);
    }
}

TEST(HowlDetectorSweep, StableLoopResiduals) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the howl detector sweep";
    }
    const auto rows = stable_rows(stable_cases({"cabin", "mt5"}, {0.0, 2.0, 5.0}), 60.0);
    print_false_header();
    false_row total;
    total.label = "all stable rows";
    for (const auto& r : rows) {
        print_false(r);
        total.seconds += r.seconds;
        total.trips += r.trips;
        total.clean += r.clean;
        total.growth += r.growth;
        total.growth_clean += r.growth_clean;
        total.ceiling += r.ceiling;
        total.level += r.level;
        total.icc += r.icc;
        total.icc_clean += r.icc_clean;
        total.howled += r.howled;
    }
    std::printf("%s: %.0f s, trips %zu (clean %zu; growth %zu, clean %zu; ceiling %zu; level %zu), faust-icc %zu "
                "(clean %zu); %zu rows howled\n",
                total.label.c_str(), total.seconds, total.trips, total.clean, total.growth, total.growth_clean,
                total.ceiling, total.level, total.icc, total.icc_clean, total.howled);
    RecordProperty("stable trips", std::to_string(total.trips));
    RecordProperty("stable clean trips", std::to_string(total.clean));
}

TEST(HowlDetectorSweep, OpenMaterialsTenMinutes) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the howl detector sweep";
    }
    std::vector<false_row>             rows;
    std::vector<std::function<void()>> jobs;
    std::mutex                         mu;
    for (const auto m : hw::k_all_materials) {
        rows.emplace_back();
        rows.back().label = std::string(hw::name(m));
    }
    for (size_t i = 0; i < hw::k_all_materials.size(); ++i) {
        for (const size_t d : {size_t{480}, size_t{960}}) {
            jobs.emplace_back([&rows, &mu, i, d] {
                const hw::trace                   t = hw::open_run(hw::k_all_materials[i], 2U, 600.0, d);
                const std::lock_guard<std::mutex> lock(mu);
                rows[i].add(t);
            });
        }
    }
    run_parallel(jobs);
    print_false_header();
    for (const auto& r : rows) {
        print_false(r);
    }
}
