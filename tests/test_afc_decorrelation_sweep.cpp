// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The karaoke measurement sweep behind test_afc_decorrelation.cpp and
// docs/karaoke-afc.md. SLOW: skipped unless MUTAP_SLOW=1 (the repo's C++
// slow-test convention; see tests/CMakeLists.txt). It runs the jobs on all
// but one hardware thread (MUTAP_SLOW_THREADS overrides) and prints every
// table the doc quotes; each row's five-seed median is also recorded with
// RecordProperty (see --gtest_output=xml).
//
//   MUTAP_SLOW=1 build/tests/mutap_tests --gtest_filter='AfcDecorrelationSweep.*'
//
// Measurement (support/karaoke_asg.h), bisection to 0.1 dB, seeds 2 / 22 /
// 42 / 62 / 82 (rooms.h's five seed sets), ASG = chain limit - the dry open
// loop's bisected limit on the same probe material and length:
//
//   ProbeConvergence  the cabin (band-limited) at 2.7 ms / S1 / S3: canceller,
//                     + 2 Hz and + 5 Hz shift, + aux feed, naive + 5 Hz on
//                     the held note; canceller and + 5 Hz on the speech-
//                     envelope material; probes 0.8 / 5 / 10 / 20 / 40 s.
//   Rooms             cabin, studio, rehearsal (image-source fixtures) and
//                     mt5, mt9 (random_decaying_rir): both generator
//                     families, band-limited, at S1 and S3; canceller, + 2 Hz,
//                     + 5 Hz, delay modulation (+-8 samples, 1.3 Hz), + aux,
//                     naive + 5 Hz; held note; 20 s probes (naive 40 s).
//   RawPaths          the raw (not band-limited) cabin at 2.7 ms / S1 / S3,
//                     canceller and + 5 Hz, 20 s probes: what the branch's
//                     unbanded numbers turn into.
//
// Wall time on the portable-random scenario: 2:49:54 with
// MUTAP_SLOW_THREADS=6 on the 12-thread Intel Mac (macOS 15, AppleClang,
// Release; HANDOFF.md, working note 9).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "support/decorrelated_loop.h"
#include "support/karaoke_asg.h"
#include "support/rooms.h"

namespace {

    using mutap_test::forward_mode;
    using mutap_test::median;
    using mutap_test::seed_in_set;
    namespace kk = mutap_test::karaoke;

    bool slow_enabled() {
        const char* v = std::getenv("MUTAP_SLOW");
        return v != nullptr && std::string(v) == "1";
    }

    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw = std::thread::hardware_concurrency();
        return hw > 1 ? hw - 1 : 1;
    }

    /// A named configuration.
    struct config {
        std::string name;
        kk::setup   setup;
    };

    config cfg_plain(kk::material m = kk::material::held) {
        kk::setup s;
        s.mat = m;
        return {m == kk::material::held ? "canceller" : "speech canceller", s};
    }
    config cfg_shift(double hz, kk::material m = kk::material::held) {
        kk::setup s;
        s.mode     = forward_mode::shift;
        s.shift_hz = hz;
        s.mat      = m;
        char n[48];
        std::snprintf(n, sizeof n, "%s+ %g Hz shift", m == kk::material::held ? "" : "speech ", hz);
        return {n, s};
    }
    config cfg_naive() {
        kk::setup s;
        s.mode = forward_mode::shift;
        s.core = kk::engine::naive;
        return {"naive + 5 Hz shift", s};
    }
    config cfg_aux() {
        kk::setup s;
        s.aux = true;
        return {"+ aux feed", s};
    }
    config cfg_dmod() {
        kk::setup s;
        s.mode = forward_mode::delay_modulation;
        return {"delay modulation", s};
    }

    /// One bisection: the chain (or, with open_only, the dry open loop).
    struct job {
        std::string room;
        bool        banded = true;
        config      cfg;
        size_t      delay     = kk::k_s1;
        double      probe_s   = 20.0;
        unsigned    seed      = 2;
        bool        open_only = false;
        double      value     = 0.0; ///< the bisected limit, dB
        double      exact     = 0.0;
    };

    void run_jobs(std::vector<job>& jobs) {
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        std::mutex               io;
        size_t                   done = 0;
        for (unsigned t = 0; t < worker_count(); ++t) {
            pool.emplace_back([&] {
                for (size_t i = next++; i < jobs.size(); i = next++) {
                    job&         j    = jobs[i];
                    const auto   path = kk::room(j.room, j.banded);
                    kk::protocol p;
                    p.delay        = j.delay;
                    p.probe_blocks = kk::probe_blocks(j.probe_s);
                    p.tol_db       = 0.1;
                    j.exact        = mutap_test::exact_msg_db(path, j.delay);
                    if (j.open_only) {
                        const auto v = kk::near_end(j.cfg.setup.mat, p.probe_blocks, j.seed + 10);
                        j.value      = kk::open_loop_db(path, j.delay, v, p);
                    }
                    else {
                        j.value = kk::measure(path, j.cfg.setup, p, j.seed, false).chain_db;
                    }
                    const std::lock_guard<std::mutex> lock(io);
                    if (++done % 25 == 0) {
                        std::printf("  ... %zu / %zu bisections\n", done, jobs.size());
                        std::fflush(stdout);
                    }
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
    }

    using row_key  = std::tuple<std::string, bool, size_t, std::string, double>;   // room, banded, delay, config, probe
    using open_key = std::tuple<std::string, bool, size_t, int, double, unsigned>; // + material, seed

    /// Enqueue the chain jobs for every seed set plus the open-loop
    /// references they need (deduplicated).
    void enqueue(std::vector<job>& jobs, std::map<open_key, size_t>& opens, const std::string& room, bool banded,
                 const config& c, size_t delay, double probe_s) {
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            job j;
            j.room    = room;
            j.banded  = banded;
            j.cfg     = c;
            j.delay   = delay;
            j.probe_s = probe_s;
            j.seed    = seed_in_set(2, set);
            jobs.push_back(j);
            const open_key k{room, banded, delay, static_cast<int>(c.setup.mat), probe_s, j.seed};
            if (opens.find(k) == opens.end()) {
                job o       = j;
                o.open_only = true;
                opens[k]    = 0;
                jobs.push_back(o);
            }
        }
    }

    struct table {
        std::map<row_key, std::vector<double>> asg;   ///< per seed set, in seed order
        std::map<row_key, std::vector<double>> chain; ///< per seed set
        std::map<row_key, double>              exact;
    };

    table collect(const std::vector<job>& jobs) {
        std::map<open_key, double> open;
        for (const auto& j : jobs) {
            if (j.open_only) {
                open[{j.room, j.banded, j.delay, static_cast<int>(j.cfg.setup.mat), j.probe_s, j.seed}] = j.value;
            }
        }
        table t;
        for (const auto& j : jobs) {
            if (j.open_only) {
                continue;
            }
            const row_key k{j.room, j.banded, j.delay, j.cfg.name, j.probe_s};
            t.asg[k].push_back(
                j.value - open.at({j.room, j.banded, j.delay, static_cast<int>(j.cfg.setup.mat), j.probe_s, j.seed}));
            t.chain[k].push_back(j.value);
            t.exact[k] = j.exact;
        }
        return t;
    }

    std::string fmt(double v) {
        char b[16];
        std::snprintf(b, sizeof b, "%+7.2f", v);
        return b;
    }

    void print_row(const row_key& k, const std::vector<double>& v, const char* suite) {
        std::string line;
        for (const double x : v) {
            line += " " + fmt(x);
        }
        std::printf("%-9s %-4s d=%-4zu %-24s %5.1f s |%s | median %s\n", std::get<0>(k).c_str(),
                    std::get<1>(k) ? "BL" : "raw", std::get<2>(k), std::get<3>(k).c_str(), std::get<4>(k), line.c_str(),
                    fmt(median(v)).c_str());
        char name[128];
        std::snprintf(name, sizeof name, "%s_%s_%s_d%zu_%s_%gs", suite, std::get<0>(k).c_str(),
                      std::get<1>(k) ? "bl" : "raw", std::get<2>(k), std::get<3>(k).c_str(), std::get<4>(k));
        std::string key = name;
        std::replace_if(key.begin(), key.end(), [](char c) { return c == ' ' || c == '+'; }, '_');
        ::testing::Test::RecordProperty(key, fmt(median(v)));
    }

} // namespace

// Probe-length convergence of every gated row (and the ones that are not
// gated because they do not converge).
TEST(AfcDecorrelationSweep, ProbeConvergence) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the karaoke sweep";
    }
    const double               probes[] = {0.8, 5.0, 10.0, 20.0, 40.0};
    const std::vector<config>  configs  = {cfg_plain(),
                                           cfg_shift(2.0),
                                           cfg_shift(5.0),
                                           cfg_aux(),
                                           cfg_naive(),
                                           cfg_plain(kk::material::speech),
                                           cfg_shift(5.0, kk::material::speech)};
    std::vector<job>           jobs;
    std::map<open_key, size_t> opens;
    for (const size_t d : {kk::k_low, kk::k_s1, kk::k_s3}) {
        for (const auto& c : configs) {
            for (const double p : probes) {
                enqueue(jobs, opens, "cabin", true, c, d, p);
            }
        }
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const job& a, const job& b) { return a.probe_s > b.probe_s; });
    run_jobs(jobs);
    const auto t = collect(jobs);

    std::printf("\nProbe convergence, cabin band-limited, ASG per seed set (2 22 42 62 82), dB\n");
    for (const auto& [k, v] : t.asg) {
        print_row(k, v, "conv");
    }
    std::printf(
        "\nexact_msg_db: 2.7 ms %.3f, S1 %.3f, S3 %.3f; theoretical_msg_db %.3f\n",
        mutap_test::exact_msg_db(kk::room("cabin"), kk::k_low), mutap_test::exact_msg_db(kk::room("cabin"), kk::k_s1),
        mutap_test::exact_msg_db(kk::room("cabin"), kk::k_s3), mutap_test::theoretical_msg_db(kk::room("cabin")));
    std::printf("\nOpen-loop over-read of each probe vs the 40 s probe (median over seed sets), dB\n");
    for (const size_t d : {kk::k_low, kk::k_s1, kk::k_s3}) {
        std::printf("d=%-4zu", d);
        for (const double p : probes) {
            std::vector<double> over;
            for (const auto& j : jobs) {
                if (j.open_only && j.delay == d && j.probe_s == p && j.cfg.setup.mat == kk::material::held) {
                    for (const auto& r : jobs) {
                        if (r.open_only && r.delay == d && r.probe_s == 40.0 && r.seed == j.seed
                            && r.cfg.setup.mat == kk::material::held) {
                            over.push_back(j.value - r.value);
                        }
                    }
                }
            }
            std::printf("  %4.1f s %s", p, fmt(median(over)).c_str());
        }
        std::printf("\n");
    }
    std::printf("\nPer-seed + 5 Hz shift - canceller (chain limits), medians, dB\n");
    for (const size_t d : {kk::k_low, kk::k_s1, kk::k_s3}) {
        std::printf("d=%-4zu", d);
        for (const double p : probes) {
            const auto&         plain = t.chain.at({"cabin", true, d, "canceller", p});
            const auto&         s5    = t.chain.at({"cabin", true, d, "+ 5 Hz shift", p});
            std::vector<double> diff;
            for (size_t i = 0; i < plain.size(); ++i) {
                diff.push_back(s5[i] - plain[i]);
            }
            std::printf("  %4.1f s %s", p, fmt(median(diff)).c_str());
        }
        std::printf("\n");
    }
    for (const auto& [k, v] : t.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}

// Both generator families, S1 and S3, every forward path.
TEST(AfcDecorrelationSweep, Rooms) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the karaoke sweep";
    }
    const std::vector<std::string> rooms = {"cabin", "studio", "rehearsal", "mt5", "mt9"};
    std::vector<job>               jobs;
    std::map<open_key, size_t>     opens;
    for (const auto& r : rooms) {
        for (const size_t d : {kk::k_s1, kk::k_s3}) {
            for (const auto& c : {cfg_plain(), cfg_shift(2.0), cfg_shift(5.0), cfg_dmod(), cfg_aux()}) {
                enqueue(jobs, opens, r, true, c, d, 20.0);
            }
            enqueue(jobs, opens, r, true, cfg_naive(), d, 40.0);
        }
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const job& a, const job& b) { return a.probe_s > b.probe_s; });
    run_jobs(jobs);
    const auto t = collect(jobs);

    std::printf("\nRooms, band-limited, held note, ASG per seed set (2 22 42 62 82), dB\n");
    for (const auto& [k, v] : t.asg) {
        print_row(k, v, "rooms");
    }
    std::printf("\nPer-seed differences, medians, dB (chain limits)\n");
    for (const auto& r : rooms) {
        for (const size_t d : {kk::k_s1, kk::k_s3}) {
            const auto&         plain = t.chain.at({r, true, d, "canceller", 20.0});
            const auto&         s5    = t.chain.at({r, true, d, "+ 5 Hz shift", 20.0});
            const auto&         s2    = t.chain.at({r, true, d, "+ 2 Hz shift", 20.0});
            const auto&         naive = t.chain.at({r, true, d, "naive + 5 Hz shift", 40.0});
            std::vector<double> d5;
            std::vector<double> d2;
            std::vector<double> dn;
            for (size_t i = 0; i < plain.size(); ++i) {
                d5.push_back(s5[i] - plain[i]);
                d2.push_back(s2[i] - plain[i]);
                dn.push_back(s5[i] - naive[i]);
            }
            std::printf("%-9s d=%-4zu  5 Hz - canceller %s   2 Hz - canceller %s   PEM - naive (5 Hz) %s\n", r.c_str(),
                        d, fmt(median(d5)).c_str(), fmt(median(d2)).c_str(), fmt(median(dn)).c_str());
            // The one cross-room claim the sweep asserts: behind the shift,
            // PEM beats the naive core everywhere (measured >= +9.05 dB, the cabin
            // at S3).
            EXPECT_GT(median(dn), 5.0) << r << " d=" << d;
        }
    }
}

// The same cabin without the loudspeaker band: the branch's paths.
TEST(AfcDecorrelationSweep, RawPaths) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the karaoke sweep";
    }
    std::vector<job>           jobs;
    std::map<open_key, size_t> opens;
    for (const size_t d : {kk::k_low, kk::k_s1, kk::k_s3}) {
        for (const auto& c : {cfg_plain(), cfg_shift(5.0)}) {
            enqueue(jobs, opens, "cabin", false, c, d, 20.0);
        }
    }
    run_jobs(jobs);
    const auto t = collect(jobs);
    std::printf("\nRaw (unbanded) cabin, held note, ASG per seed set (2 22 42 62 82), dB\n");
    for (const auto& [k, v] : t.asg) {
        print_row(k, v, "raw");
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}
