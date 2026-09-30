// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// THE TWO-MIC GO/NO-GO SWEEP (the anti-howl PoC plan's §2.5 and D11). SLOW:
// skipped unless MUTAP_SLOW=1 (tests/CMakeLists.txt); jobs run on all but
// one hardware thread unless MUTAP_SLOW_THREADS says otherwise. It measures
// and prints; it asserts nothing about ASG (the decision is D11's).
//
//   MUTAP_SLOW=1 MUTAP_SLOW_THREADS=4 build/tests/mutap_tests --gtest_filter='TwoMicSweep.*'
//
// Measurement (support/two_mic_loop.h): band-limited 1024-tap paths, block
// 64, PEM + FD-Kalman (speech cascade) per mic on the one shared, aligned
// reference, double precision; converge 1500 blocks at exact_msg_db(sum F,
// d) - 6, bisect to 0.1 dB, seeds 2 / 22 / 42 / 62 / 82. Two-mic ASG = the
// two-mic chain's limit - the dry two-mic loop's (both mics at unit gain, no
// cancellers, plain, no aux) on the same probe material and length.
// Single-mic ASG, the reference: each mic alone with its own singer, in the
// same room position, delay and forward path, against its own dry loop.
//
// Room pairs (two_mic::room_pair): "cabin", "rehearsal", "hall" are one
// image-source fixture read at two truncations (taps [0, 1024) and [256,
// 1280)); "mt5+mt9" is two random_decaying_rir seeds.
//
//   ProbeConvergence  cabin (TWO_MIC_CONV_PAIRS), S1 and S3 (TWO_MIC_DELAYS),
//                     unison and speech, -10 dB leakage, canceller alone:
//                     probes 5 / 10 / 20 / 40 s. 480 bisections, 6998 s wall.
//   Conditions        S1 / S3 x singers (unison, separated, speech) x
//                     cross-leakage (none, -20, -10 dB) x forward path
//                     (canceller, + 2 Hz shift, + aux at -12 dB, + both) on
//                     cabin and mt5+mt9, and the canceller alone on rehearsal
//                     and hall; 10 s probes. 2100 bisections, 12462 + 2747 s
//                     wall (run as two invocations). TWO_MIC_PAIRS replaces
//                     both grids with one from TWO_MIC_PAIRS, TWO_MIC_DELAYS
//                     ("480,960"), TWO_MIC_SINGERS ("unison,separated,speech";
//                     "detuned" also exists), TWO_MIC_LEAKS ("none,-20,-10"),
//                     TWO_MIC_FWD ("plain,shift,aux,shift+aux") and
//                     TWO_MIC_PROBE (seconds, 10).
//   LongProbeCheck    the held-note unison rows at S1 nearest the D11 line,
//                     again at 40 s probes. 175 bisections, 1549 + 1499 s.
//   DetunedUnison     a beating unison (300 / 298.1 Hz) against the
//                     phase-locked one. 160 bisections, 629 s.
//   Cost              ns per block: one canceller, two cancellers + the bus.
//
// THE PROBE (10 s) is not converged by the karaoke rule (within 0.25 dB of
// the 40 s median) on every row: ProbeConvergence puts the held-note
// medians at 10 s within 0.64 dB of their 40 s values and the speech ones
// within 0.81 dB (speech drifts down to 40 s, as in the karaoke sweep).
// The D11 ratio moves less: 10 s against 40 s differs by at most 0.029 in
// ProbeConvergence and 0.007 in LongProbeCheck.
//
// Wall times: 4 worker threads (MUTAP_SLOW_THREADS=4) on the 12-thread Intel
// Mac while other jobs shared it (load average 5 to 50). TWO_MIC_RAW=<file>
// appends every finished bisection to <file> as CSV and a rerun resumes
// from it, so a table can be reprinted from a kept file without rerunning.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "support/rooms.h"
#include "support/two_mic_loop.h"

namespace {

    namespace tmic = mutap_test::two_mic;
    using mutap_test::median;
    using mutap_test::seed_in_set;

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

    std::vector<std::string> env_list(const char* name, const char* fallback) {
        const char*              v = std::getenv(name);
        std::stringstream        ss((v != nullptr && *v != '\0') ? v : fallback);
        std::vector<std::string> out;
        std::string              item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                out.push_back(item);
            }
        }
        return out;
    }

    double env_double(const char* name, double fallback) {
        const char* v = std::getenv(name);
        return (v != nullptr && *v != '\0') ? std::strtod(v, nullptr) : fallback;
    }

    tmic::singers parse_singers(const std::string& s) {
        if (s == "separated") {
            return tmic::singers::separated;
        }
        if (s == "speech") {
            return tmic::singers::speech;
        }
        if (s == "detuned") {
            return tmic::singers::detuned;
        }
        return tmic::singers::unison;
    }

    constexpr double k_no_leak = -300.0;

    std::string leak_name(double db) {
        if (db <= -200.0) {
            return "none";
        }
        char b[16];
        std::snprintf(b, sizeof b, "%g dB", db);
        return b;
    }

    std::string fwd_name(const tmic::condition& c) {
        if (c.shift && c.aux) {
            return "+2Hz +aux";
        }
        if (c.shift) {
            return "+2Hz";
        }
        if (c.aux) {
            return "+aux";
        }
        return "canceller";
    }

    /// One bisection.
    struct job {
        enum class kind { open2, chain2, open1, chain1 };
        kind            k = kind::chain2;
        std::string     pair;
        tmic::condition c;
        int             mic     = -1; ///< single-mic jobs: which mic
        unsigned        seed    = 2;
        double          probe_s = 10.0;
        double          value   = 0.0; ///< the bisected limit, dB
        double          exact   = 0.0; ///< exact_msg_db of the job's dry loop
        double          seconds = 0.0; ///< wall time
    };

    bool two(const job& j) {
        return j.k == job::kind::open2 || j.k == job::kind::chain2;
    }
    bool open(const job& j) {
        return j.k == job::kind::open2 || j.k == job::kind::open1;
    }

    /// A job's identity in the TWO_MIC_RAW file.
    std::string job_key(const job& j) {
        static const char* const k_kinds[] = {"open2", "chain2", "open1", "chain1"};
        char                     b[256];
        std::snprintf(b, sizeof b, "%s,%s,%zu,%s,%g,%d,%d,%d,%u,%g", k_kinds[static_cast<int>(j.k)], j.pair.c_str(),
                      j.c.delay, tmic::name(j.c.who), j.c.leak_db, j.c.shift ? 1 : 0, j.c.aux ? 1 : 0, j.mic, j.seed,
                      j.probe_s);
        return b;
    }

    void run_jobs(std::vector<job>& jobs) {
        // Longest first: two-mic chains, then single-mic chains, then the dry loops.
        std::stable_sort(jobs.begin(), jobs.end(), [](const job& a, const job& b) {
            const auto rank = [](const job& j) {
                return (open(j) ? 2 : 0) + (two(j) ? 0 : 1) - (j.probe_s > 20.0 ? 4 : 0) - (j.probe_s > 10.0 ? 2 : 0);
            };
            return rank(a) < rank(b);
        });
        // TWO_MIC_RAW names a CSV file that every finished bisection is
        // appended to, and that a rerun resumes from: a job whose key line
        // is already there is not run again.
        const char*                raw = std::getenv("TWO_MIC_RAW");
        std::map<std::string, job> have;
        std::FILE*                 out = nullptr;
        if (raw != nullptr && *raw != '\0') {
            if (std::FILE* in = std::fopen(raw, "r")) {
                char line[512];
                while (std::fgets(line, sizeof line, in) != nullptr) {
                    std::string s(line);
                    // key = everything before the last three fields
                    size_t cut = s.size();
                    for (int f = 0; f < 3 && cut != std::string::npos; ++f) {
                        cut = s.rfind(',', cut - 1);
                    }
                    if (cut == std::string::npos) {
                        continue;
                    }
                    job r;
                    if (std::sscanf(s.c_str() + cut, ",%lf,%lf,%lf", &r.value, &r.exact, &r.seconds) == 3) {
                        have[s.substr(0, cut)] = r;
                    }
                }
                std::fclose(in);
            }
            out = std::fopen(raw, "a");
        }
        std::vector<size_t> todo;
        for (size_t i = 0; i < jobs.size(); ++i) {
            const auto it = have.find(job_key(jobs[i]));
            if (it != have.end()) {
                jobs[i].value   = it->second.value;
                jobs[i].exact   = it->second.exact;
                jobs[i].seconds = it->second.seconds;
            }
            else {
                todo.push_back(i);
            }
        }
        std::printf("  %zu bisections, %zu to run (%zu from %s)\n", jobs.size(), todo.size(), jobs.size() - todo.size(),
                    out != nullptr ? raw : "-");
        std::fflush(stdout);
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        std::mutex               io;
        size_t                   done  = 0;
        const auto               start = std::chrono::steady_clock::now();
        for (unsigned t = 0; t < worker_count(); ++t) {
            pool.emplace_back([&] {
                for (size_t n = next++; n < todo.size(); n = next++) {
                    job&           j     = jobs[todo[n]];
                    const auto     t0    = std::chrono::steady_clock::now();
                    const auto     paths = tmic::room_pair(j.pair);
                    tmic::protocol p;
                    const auto     sc = tmic::make_scenario<double>(paths, j.c, j.seed, p.converge_blocks,
                                                                    tmic::probe_blocks(j.probe_s), two(j) ? -1 : j.mic);
                    j.exact           = tmic::exact_db(sc);
                    j.value           = open(j) ? tmic::open_limit_db(sc, p) : tmic::chain_limit_db(sc, p);
                    j.seconds         = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    const std::lock_guard<std::mutex> lock(io);
                    if (out != nullptr) {
                        std::fprintf(out, "%s,%.4f,%.4f,%.1f\n", job_key(j).c_str(), j.value, j.exact, j.seconds);
                        std::fflush(out);
                    }
                    if (++done % 50 == 0 || done == todo.size()) {
                        const double el =
                            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                        std::printf("  ... %zu / %zu bisections, %.0f s\n", done, todo.size(), el);
                        std::fflush(stdout);
                    }
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
        if (out != nullptr) {
            std::fclose(out);
        }
    }

    /// Two conditions share a dry loop when only the forward path differs
    /// (two-mic), or when the mic's singer material is the same (single).
    bool same_open(const job& o, const job& j) {
        if (o.pair != j.pair || o.seed != j.seed || o.probe_s != j.probe_s || o.c.delay != j.c.delay
            || o.c.who != j.c.who) {
            return false;
        }
        return two(j) ? (o.k == job::kind::open2 && o.c.leak_db == j.c.leak_db)
                      : (o.k == job::kind::open1 && o.mic == j.mic);
    }

    /// Enqueue the chain jobs of condition `c` on `pair` (two-mic and both
    /// singles) for every seed set, plus the dry loops they need (deduplicated).
    void enqueue(std::vector<job>& jobs, const std::string& pair, const tmic::condition& c, double probe_s) {
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            for (int mic = -1; mic < 2; ++mic) {
                job j;
                j.k       = (mic < 0) ? job::kind::chain2 : job::kind::chain1;
                j.pair    = pair;
                j.c       = c;
                j.mic     = mic;
                j.seed    = seed_in_set(2, set);
                j.probe_s = probe_s;
                if (mic >= 0) {
                    j.c.leak_db = k_no_leak; // one singer: nothing leaks
                }
                bool have_chain = false;
                for (const auto& o : jobs) {
                    have_chain = have_chain
                                 || (o.k == j.k && o.pair == j.pair && o.mic == j.mic && o.seed == j.seed
                                     && o.probe_s == j.probe_s && o.c.delay == j.c.delay && o.c.who == j.c.who
                                     && o.c.leak_db == j.c.leak_db && o.c.shift == j.c.shift && o.c.aux == j.c.aux);
                }
                if (!have_chain) {
                    jobs.push_back(j);
                }
                job o          = j;
                o.k            = (mic < 0) ? job::kind::open2 : job::kind::open1;
                bool have_open = false;
                for (const auto& x : jobs) {
                    have_open = have_open || (open(x) && same_open(x, o));
                }
                if (!have_open) {
                    jobs.push_back(o);
                }
            }
        }
    }

    /// Per-seed-set ASG of condition `c` on `pair`: the two-mic chain
    /// (mic = -1) or mic `mic` alone.
    std::vector<double> asg(const std::vector<job>& jobs, const std::string& pair, const tmic::condition& c, int mic,
                            double probe_s) {
        std::vector<double> out;
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            const unsigned seed = seed_in_set(2, set);
            const job*     ch   = nullptr;
            const job*     op   = nullptr;
            for (const auto& j : jobs) {
                if (j.pair != pair || j.seed != seed || j.probe_s != probe_s || j.c.delay != c.delay
                    || j.c.who != c.who) {
                    continue;
                }
                if (mic < 0 && j.k == job::kind::chain2 && j.c.leak_db == c.leak_db && j.c.shift == c.shift
                    && j.c.aux == c.aux) {
                    ch = &j;
                }
                if (mic >= 0 && j.k == job::kind::chain1 && j.mic == mic && j.c.shift == c.shift && j.c.aux == c.aux) {
                    ch = &j;
                }
                if (mic < 0 && j.k == job::kind::open2 && j.c.leak_db == c.leak_db) {
                    op = &j;
                }
                if (mic >= 0 && j.k == job::kind::open1 && j.mic == mic) {
                    op = &j;
                }
            }
            if (ch != nullptr && op != nullptr) {
                out.push_back(ch->value - op->value);
            }
        }
        return out;
    }

    std::string seeds(const std::vector<double>& v) {
        std::string s;
        for (const double x : v) {
            char b[16];
            std::snprintf(b, sizeof b, " %+6.2f", x);
            s += b;
        }
        return s;
    }

    std::vector<size_t> parse_delays(const std::vector<std::string>& v) {
        std::vector<size_t> out;
        for (const auto& d : v) {
            out.push_back(static_cast<size_t>(std::strtoul(d.c_str(), nullptr, 10)));
        }
        return out;
    }

    std::vector<double> parse_leaks(const std::vector<std::string>& v) {
        std::vector<double> out;
        for (const auto& l : v) {
            out.push_back(l == "none" ? k_no_leak : std::strtod(l.c_str(), nullptr));
        }
        return out;
    }

    std::vector<size_t> delays() {
        return parse_delays(env_list("TWO_MIC_DELAYS", "480,960"));
    }

    /// One block of the sweep: every combination of its lists.
    struct grid {
        std::vector<std::string> pairs;
        std::vector<size_t>      delays;
        std::vector<std::string> singers;
        std::vector<double>      leaks;
        std::vector<std::string> fwd; ///< "plain", "shift", "aux", "shift+aux"
        double                   probe_s = 10.0;
    };

    /// The grid's conditions, pair by pair, in print order.
    std::vector<std::pair<std::string, tmic::condition>> conditions_of(const grid& g) {
        std::vector<std::pair<std::string, tmic::condition>> out;
        for (const auto& pair : g.pairs) {
            for (const size_t d : g.delays) {
                for (const auto& s : g.singers) {
                    for (const double l : g.leaks) {
                        for (const auto& f : g.fwd) {
                            tmic::condition c;
                            c.delay   = d;
                            c.who     = parse_singers(s);
                            c.leak_db = l;
                            c.shift   = (f == "shift" || f == "shift+aux");
                            c.aux     = (f == "aux" || f == "shift+aux");
                            out.emplace_back(pair, c);
                        }
                    }
                }
            }
        }
        return out;
    }

    void print_dry(const std::vector<job>& jobs) {
        std::printf("\nDry loops: bisected limit - exact_msg_db, per seed set, dB\n");
        for (const auto& j : jobs) {
            if (!open(j) || j.seed != seed_in_set(2, 0)) {
                continue;
            }
            std::vector<double> v;
            for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                for (const auto& o : jobs) {
                    if (o.k == j.k && o.pair == j.pair && o.seed == seed_in_set(2, set) && o.probe_s == j.probe_s
                        && o.c.delay == j.c.delay && o.c.who == j.c.who && o.c.leak_db == j.c.leak_db
                        && o.mic == j.mic) {
                        v.push_back(o.value - o.exact);
                    }
                }
            }
            std::printf("%-10s d=%-4zu %-9s %-7s %-6s exact %+7.2f %4.0f s |%s\n", j.pair.c_str(), j.c.delay,
                        tmic::name(j.c.who), j.k == job::kind::open2 ? "two" : (j.mic == 0 ? "mic 0" : "mic 1"),
                        j.k == job::kind::open2 ? leak_name(j.c.leak_db).c_str() : "", j.exact, j.probe_s,
                        seeds(v).c_str());
        }
    }

} // namespace

// Probe-length convergence: the shortest probe whose five-seed median sits
// within 0.25 dB of the 40 s probe's is the Conditions probe.
TEST(TwoMicSweep, ProbeConvergence) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the two-mic sweep";
    }
    const std::vector<double> probes = {5.0, 10.0, 20.0, 40.0};
    std::vector<job>          jobs;
    const auto                pairs = env_list("TWO_MIC_CONV_PAIRS", "cabin");
    for (const auto& pair : pairs) {
        for (const size_t d : delays()) {
            for (const auto& s : env_list("TWO_MIC_CONV_SINGERS", "unison,speech")) {
                tmic::condition c;
                c.delay   = d;
                c.who     = parse_singers(s);
                c.leak_db = -10.0;
                for (const double p : probes) {
                    enqueue(jobs, pair, c, p);
                }
            }
        }
    }
    const auto start = std::chrono::steady_clock::now();
    run_jobs(jobs);
    std::printf("\nwall %.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());

    std::printf("\nProbe convergence: ASG medians over seed sets (2 22 42 62 82), -10 dB leakage, canceller, dB\n");
    std::printf("%-10s %-6s %-9s %-6s", "pair", "d", "singers", "who");
    for (const double p : probes) {
        std::printf(" %6.0f s", p);
    }
    std::printf("   per seed at 40 s\n");
    for (const auto& pair : pairs) {
        for (const size_t d : delays()) {
            for (const auto& s : env_list("TWO_MIC_CONV_SINGERS", "unison,speech")) {
                tmic::condition c;
                c.delay   = d;
                c.who     = parse_singers(s);
                c.leak_db = -10.0;
                for (int mic = -1; mic < 2; ++mic) {
                    std::printf("%-10s %-6zu %-9s %-6s", pair.c_str(), d, tmic::name(c.who),
                                mic < 0 ? "two" : (mic == 0 ? "mic 0" : "mic 1"));
                    std::vector<double> last;
                    for (const double p : probes) {
                        last = asg(jobs, pair, c, mic, p);
                        std::printf(" %+7.2f", median(last));
                    }
                    std::printf("  |%s\n", seeds(last).c_str());
                    for (const double x : last) {
                        EXPECT_TRUE(std::isfinite(x));
                    }
                }
            }
        }
    }
    print_dry(jobs);
}

namespace {

    /// Run the grids and print the D11 table, the single-mic rows and the
    /// dry loops.
    void run_grids(const std::vector<grid>& grids) {
        std::vector<job> jobs;
        for (const auto& g : grids) {
            for (const auto& [pair, c] : conditions_of(g)) {
                enqueue(jobs, pair, c, g.probe_s);
            }
        }
        const auto start = std::chrono::steady_clock::now();
        run_jobs(jobs);
        const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        std::printf("\nTwo-mic ASG against single-mic ASG, seed sets 2 22 42 62 82, dB\n");
        std::printf("ratio = two-mic median / mean of the two single-mic medians; D11 passes at >= 0.667\n");
        std::printf("%-10s %-4s %-9s %-7s %-10s %5s | %-7s %-7s | %-7s | %-6s %-6s | two-mic per seed\n", "pair", "d",
                    "singers", "leak", "fwd", "probe", "mic 0", "mic 1", "two", "ratio", "vs max");
        size_t rows = 0;
        size_t pass = 0;
        for (const auto& g : grids) {
            for (const auto& [pair, c] : conditions_of(g)) {
                const auto   s0    = asg(jobs, pair, c, 0, g.probe_s);
                const auto   s1    = asg(jobs, pair, c, 1, g.probe_s);
                const auto   t     = asg(jobs, pair, c, -1, g.probe_s);
                const double m0    = median(s0);
                const double m1    = median(s1);
                const double mt    = median(t);
                const double ratio = mt / (0.5 * (m0 + m1));
                const double vsmax = mt / std::max(m0, m1);
                ++rows;
                // (a single-mic median at or below 0 dB makes the ratio meaningless: FAIL)
                const bool ok = (m0 + m1 > 0.0) && ratio >= 2.0 / 3.0;
                pass += ok ? 1 : 0;
                std::printf("%-10s %-4zu %-9s %-7s %-10s %4.0fs | %+7.2f %+7.2f | %+7.2f | %6.3f %6.3f %s |%s\n",
                            pair.c_str(), c.delay, tmic::name(c.who), leak_name(c.leak_db).c_str(), fwd_name(c).c_str(),
                            g.probe_s, m0, m1, mt, ratio, vsmax, ok ? "pass" : "FAIL", seeds(t).c_str());
                for (const double x : t) {
                    EXPECT_TRUE(std::isfinite(x));
                }
            }
        }
        std::printf("\n%zu / %zu conditions pass (ratio vs the mean single-mic median)\n", pass, rows);

        std::printf("\nSingle-mic ASG per seed set (each mic alone, its own singer), dB\n");
        for (const auto& g : grids) {
            for (const auto& [pair, c] : conditions_of(g)) {
                if (c.leak_db != g.leaks.front()) {
                    continue; // one row per singer and forward path
                }
                for (int mic = 0; mic < 2; ++mic) {
                    const auto v = asg(jobs, pair, c, mic, g.probe_s);
                    std::printf("%-10s %-4zu %-9s %-10s %4.0fs mic %d |%s | median %+7.2f\n", pair.c_str(), c.delay,
                                tmic::name(c.who), fwd_name(c).c_str(), g.probe_s, mic, seeds(v).c_str(), median(v));
                }
            }
        }
        print_dry(jobs);
        double cpu = 0.0;
        for (const auto& j : jobs) {
            cpu += j.seconds;
        }
        std::printf("\n%zu bisections, wall %.0f s, summed job time %.0f s\n", jobs.size(), wall, cpu);
    }

} // namespace

// The grid: cabin and mt5+mt9 (one pair from each generator family) in full,
// rehearsal and hall with the canceller alone. TWO_MIC_PAIRS replaces both
// with one grid built from the TWO_MIC_* overrides.
TEST(TwoMicSweep, Conditions) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the two-mic sweep";
    }
    grid full;
    full.pairs   = env_list("TWO_MIC_PAIRS", "cabin,mt5+mt9");
    full.delays  = delays();
    full.singers = env_list("TWO_MIC_SINGERS", "unison,separated,speech");
    full.leaks   = parse_leaks(env_list("TWO_MIC_LEAKS", "none,-20,-10"));
    full.fwd     = env_list("TWO_MIC_FWD", "plain,shift,aux,shift+aux");
    full.probe_s = env_double("TWO_MIC_PROBE", 10.0);
    std::vector<grid> grids{full};
    if (std::getenv("TWO_MIC_PAIRS") == nullptr) {
        grid more  = full;
        more.pairs = {"rehearsal", "hall"};
        more.fwd   = {"plain"};
        grids.push_back(more);
    }
    run_grids(grids);
}

// The near-threshold rows again at 40 s: the held-note unison at S1 in
// mt5+mt9 (canceller and + aux), rehearsal and hall (canceller).
TEST(TwoMicSweep, LongProbeCheck) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the two-mic sweep";
    }
    grid mt;
    mt.pairs   = {"mt5+mt9"};
    mt.delays  = {tmic::k_s1};
    mt.singers = {"unison"};
    mt.leaks   = parse_leaks({"none", "-20", "-10"});
    mt.fwd     = {"plain", "aux"};
    mt.probe_s = 40.0;
    grid fx    = mt;
    fx.pairs   = {"rehearsal", "hall"};
    fx.fwd     = {"plain"};
    run_grids({mt, fx});
}

// A unison that beats (160 vs 161 samples: 300 and 298.1 Hz) instead of the
// phase-locked one: cabin and mt5+mt9, S1 and S3, no and -10 dB leakage,
// canceller alone.
TEST(TwoMicSweep, DetunedUnison) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the two-mic sweep";
    }
    grid g;
    g.pairs   = {"cabin", "mt5+mt9"};
    g.delays  = {tmic::k_s1, tmic::k_s3};
    g.singers = {"detuned"};
    g.leaks   = parse_leaks({"none", "-10"});
    g.fwd     = {"plain"};
    g.probe_s = 10.0;
    run_grids({g});
}

// What a second canceller costs: ns per 64-sample block of the cancellers
// (and the bus sum) on the same recorded loop signals, float and double.
// Measured on the shared Intel Mac (wall clock, median of 3 x 3000 blocks):
// at load average ~4, float 470522 -> 931900 ns (x1.981), double 471502 ->
// 930436 (x1.973); at load ~35, float 948100 -> 1931235 (x2.037), double
// 966852 -> 2064040 (x2.135). pem_afc.h's own figure for one canceller on
// this machine unloaded: 324621.6 ns (double).
namespace {

    template <typename Sample>
    void time_cancellers() {
        const auto      paths = tmic::room_pair("mt5+mt9");
        tmic::condition c;
        c.who               = tmic::singers::speech;
        const size_t blocks = 3000;
        const auto   sc     = tmic::make_scenario<Sample>(paths, c, 2, blocks, 1);
        auto         cfg    = tmic::loop_config(sc, false);
        cfg.forward_gain_db = tmic::exact_db(sc) - 6.0;
        // Record the loop's reference and mic blocks with the chain in it.
        mutap_test::multi_mic_loop<Sample> loop(cfg);
        std::vector<tmic::afc<Sample>>     live(2, tmic::afc<Sample>(tmic::afc_config<Sample>()));
        std::vector<Sample>                u(blocks * tmic::k_block);
        std::vector<Sample>                y0(blocks * tmic::k_block);
        std::vector<Sample>                y1(blocks * tmic::k_block);
        for (size_t blk = 0; blk < blocks; ++blk) {
            const Sample* x[] = {&sc.conv[0][blk * tmic::k_block], &sc.conv[1][blk * tmic::k_block]};
            loop.step(x, &live);
            for (size_t i = 0; i < tmic::k_block; ++i) {
                u[blk * tmic::k_block + i]  = loop.reference_block()[i];
                y0[blk * tmic::k_block + i] = loop.mic_block(0)[i];
                y1[blk * tmic::k_block + i] = loop.mic_block(1)[i];
            }
        }
        std::vector<Sample> e0(tmic::k_block);
        std::vector<Sample> e1(tmic::k_block);
        std::vector<Sample> bus(tmic::k_block);
        double              ns[2] = {0.0, 0.0};
        Sample              sink  = Sample(0);
        for (int mics = 1; mics <= 2; ++mics) {
            std::vector<double> reps;
            for (int rep = 0; rep < 3; ++rep) {
                tmic::afc<Sample> a(tmic::afc_config<Sample>());
                tmic::afc<Sample> b(tmic::afc_config<Sample>());
                const auto        t0 = std::chrono::steady_clock::now();
                for (size_t blk = 0; blk < blocks; ++blk) {
                    const size_t off = blk * tmic::k_block;
                    a.process_block(&u[off], &y0[off], e0.data());
                    if (mics == 2) {
                        b.process_block(&u[off], &y1[off], e1.data());
                        for (size_t i = 0; i < tmic::k_block; ++i) {
                            bus[i] = e0[i] + e1[i];
                        }
                    }
                    sink += (mics == 2) ? bus[0] : e0[0];
                }
                reps.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count()
                               / static_cast<double>(blocks));
            }
            ns[mics - 1] = median(reps);
        }
        std::printf("%-6s one canceller %.0f ns/block, two + bus %.0f ns/block (x%.3f); budget at 48 kHz %.0f ns "
                    "(sink %g)\n",
                    sizeof(Sample) == 4 ? "float" : "double", ns[0], ns[1], ns[1] / ns[0], 64.0 / 48000.0 * 1e9,
                    static_cast<double>(sink));
    }

} // namespace

TEST(TwoMicSweep, Cost) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the two-mic sweep";
    }
    time_cancellers<float>();
    time_cancellers<double>();
}
