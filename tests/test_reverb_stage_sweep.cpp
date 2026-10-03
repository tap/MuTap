// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The reverb-behind-the-canceller sweep behind test_reverb_stage.cpp and
// docs/reverb-afc.md. SLOW: skipped unless MUTAP_SLOW=1 (the repo's C++
// slow-test convention; see tests/CMakeLists.txt). Jobs run on at most 4
// threads unless MUTAP_SLOW_THREADS says otherwise; every table the doc
// quotes is printed, each finished bisection prints a "job" line (the
// checkpoint of a run that takes hours), and each row's median is recorded
// with RecordProperty (see --gtest_output=xml).
//
//   MUTAP_SLOW=1 MUTAP_SLOW_THREADS=4 build/tests/mutap_tests --gtest_filter='ReverbStageSweep.*'
//
// Measurement (support/karaoke_asg.h, the karaoke suites' protocol, with
// the reverb as a forward-path stage of decorrelated_loop, support/
// reverb_rig.h): band-limited 1024-tap rooms; PEM + FD-Kalman (speech
// cascade) converged 1500 blocks at exact_msg_db - 6 WITH the reverb in the
// loop; the forward gain bisected to 0.1 dB over [exact - 20, exact + 30]
// with the 40 dB runaway rule on a second seed's material (seed + 10); a
// fresh canceller copy and a silent reverb per probe; seeds 2 / 22 / 42 /
// 62 / 82 (rooms.h's five seed sets). Like for like:
//
//   ASG  = the chain's bisected limit - the dry open loop's (no canceller, no
//          reverb), on the same probe material and length;
//   cost = canceller-alone limit - canceller-plus-reverb limit, per seed
//          (positive = the reverb costs stable gain);
//   mix level = the mix's broadband level change for white input (10 log10
//          of the energy of (1 - w) delta + w r; reverb_rig.h), and
//   matched-loudness cost = cost - mix level: the cost had the mix been
//          made as loud as the dry bus.
//
//   ProbeConvergence  cabin and mt5 at S1, wet 0.30, probes 10 / 20 / 40 /
//                     80 / 160 s: the as-shipped plate at decay 0.5 / 0.7
//                     / 0.85 on the held note and the speech envelope; the
//                     paper plate at decay 0.5 on the held note; the 5 Hz
//                     shift with the as-shipped plate at decay 0.7, whole
//                     bus and dry-only, held note, to 80 s; the canceller
//                     alone to 80 s. "Converged" = the shortest probe from
//                     which every longer probe's five-seed median ASG is
//                     within 0.25 dB of the row's longest probe's.
//   Grid              six rooms (cabin, studio, rehearsal, hall; mt5, mt9).
//                     Held note, S1 and S3, 20 s probes: wet 0.15 / 0.30 /
//                     0.50 x decay 0.5 / 0.7 / 0.85 on the as-shipped plate
//                     and at decay 0.5 on the paper plate (damping 0.0005,
//                     L return), damping 0.5 and the (L + R) / 2 return at
//                     wet 0.30 on the as-shipped plate. Speech envelope, S1,
//                     80 s probes: the as-shipped plate at wet 0.30. Probes
//                     from ProbeConvergence (grid_probe).
//   Shift             the same rooms, held note, 20 s probes; 2 and 5 Hz:
//                     the shift alone, and with the as-shipped plate at
//                     decay 0.5 / 0.7, wet 0.30, the plate alone, the
//                     whole-bus shift + plate and the dry-only shift.
//                     Runaway limits. REVERB_SWEEP_DELAYS picks S1 / S3.
//   BareLoopCost      phase 0's analytic bare-loop table for both plates
//                     (no canceller): four fixtures at 4096 taps, d = 480,
//                     magnitude bound / exact Nyquist cost, and each
//                     plate's T30 per decay.

#include <algorithm>
#include <atomic>
#include <chrono>
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

#include "support/karaoke_asg.h"
#include "support/reverb_rig.h"
#include "support/rooms.h"

namespace {

    using mutap_test::median;
    using mutap_test::seed_in_set;
    namespace kk = mutap_test::karaoke;
    namespace rv = mutap_test::reverb;

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
        const unsigned hw = std::thread::hardware_concurrency();
        return std::clamp(hw > 1 ? hw - 1 : 1U, 1U, 4U);
    }

    std::string fmt(double v) {
        char b[16];
        std::snprintf(b, sizeof b, "%+6.2f", v);
        return b;
    }

    const char* mat_name(kk::material m) {
        return m == kk::material::held ? "held" : "speech";
    }

    /// A row's label: "canceller", or the reverb configuration.
    std::string label(const rv::params* p) {
        if (p == nullptr) {
            return "canceller";
        }
        char b[128];
        if (p->topo == rv::topology::shift_only) {
            std::snprintf(b, sizeof b, "shift %g Hz", p->shift_hz);
            return b;
        }
        std::snprintf(b, sizeof b, "%s d%.2f w%.2f m%g %s%s", rv::name(p->plate), p->decay, p->wet, p->damping,
                      p->ret == tap::mu::reverb_return::left ? "L" : "M", "");
        std::string s = b;
        if (p->topo == rv::topology::bus_shift || p->topo == rv::topology::dry_shift) {
            char t[64];
            std::snprintf(t, sizeof t, " %s %g Hz", p->topo == rv::topology::bus_shift ? "bus-shift" : "dry-shift",
                          p->shift_hz);
            s += t;
        }
        return s;
    }

    enum class kind { open, canceller, rig };

    /// One bisection.
    struct job {
        std::string  room;
        size_t       delay   = kk::k_s1;
        kk::material mat     = kk::material::held;
        double       probe_s = 20.0;
        unsigned     seed    = 2;
        kind         what    = kind::canceller;
        rv::params   p;
        double       value   = 0.0; ///< the bisected limit, dB
        double       exact   = 0.0;
        double       seconds = 0.0;
    };

    std::string job_label(const job& j) {
        return j.what == kind::rig ? label(&j.p) : (j.what == kind::open ? "open" : "canceller");
    }

    kk::protocol protocol_for(const job& j) {
        kk::protocol p;
        p.delay        = j.delay;
        p.probe_blocks = kk::probe_blocks(j.probe_s);
        p.tol_db       = 0.1;
        p.chain_lo     = -20.0;
        p.chain_hi     = 30.0;
        return p;
    }

    void run_one(job& j) {
        const auto   t0   = std::chrono::steady_clock::now();
        const auto   path = kk::room(j.room);
        kk::protocol p    = protocol_for(j);
        j.exact           = mutap_test::exact_msg_db(path, j.delay);
        kk::setup s;
        s.mat = j.mat;
        if (j.what == kind::open) {
            const auto v = kk::near_end(j.mat, p.probe_blocks, j.seed + 10);
            j.value      = kk::open_loop_db(path, j.delay, v, p);
        }
        else if (j.what == kind::canceller) {
            j.value = kk::measure(path, s, p, j.seed, false).chain_db;
        }
        else {
            rv::rig rig(j.p);
            s.stage = mutap_test::forward_stage<double>::of(&rig);
            j.value = kk::measure(path, s, p, j.seed, false).chain_db;
        }
        j.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    void run_jobs(std::vector<job>& jobs) {
        // Longest probes first, so the pool drains evenly.
        std::stable_sort(jobs.begin(), jobs.end(), [](const job& a, const job& b) { return a.probe_s > b.probe_s; });
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        std::mutex               io;
        size_t                   done = 0;
        const auto               t0   = std::chrono::steady_clock::now();
        for (unsigned t = 0; t < worker_count(); ++t) {
            pool.emplace_back([&] {
                for (size_t i = next++; i < jobs.size(); i = next++) {
                    run_one(jobs[i]);
                    const std::lock_guard<std::mutex> lock(io);
                    const job&                        j = jobs[i];
                    std::printf("  job %s d=%zu %s %g s seed %u %s: %+.2f (%.0f s)\n", j.room.c_str(), j.delay,
                                mat_name(j.mat), j.probe_s, j.seed, job_label(j).c_str(), j.value - j.exact, j.seconds);
                    std::fflush(stdout); // the job lines are the checkpoint of a run that takes hours
                    if (++done % 50 == 0 || done == jobs.size()) {
                        std::printf("  ... %zu / %zu bisections, %.0f s\n", done, jobs.size(),
                                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
                        std::fflush(stdout);
                    }
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
        double cpu = 0.0;
        for (const auto& j : jobs) {
            cpu += j.seconds;
        }
        std::printf("%zu bisections on %u threads: wall %.1f s, cpu-sum %.1f s\n", jobs.size(), worker_count(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), cpu);
    }

    /// Row key: room, delay, material, probe, label.
    using row_key = std::tuple<std::string, size_t, int, double, std::string>;

    struct results {
        std::map<row_key, std::vector<double>> chain; ///< per seed set, seed order
        std::map<row_key, std::vector<double>> asg;
        std::map<row_key, std::vector<double>> cost; ///< canceller - this, per seed (rig rows)
        std::map<row_key, double>              exact;
    };

    /// Enqueue `what` for every seed set at (room, delay, mat, probe), plus
    /// the open loop and the canceller alone there (deduplicated).
    using ref_map = std::map<std::tuple<std::string, size_t, int, double>, unsigned>; ///< 1 open, 2 canceller

    /// Enqueue `p` (null: nothing but the references) for every seed set at
    /// (room, delay, mat, probe), plus the open loop there and, with
    /// `canceller_ref`, the canceller alone (each reference once).
    void enqueue(std::vector<job>& jobs, ref_map& refs, const std::string& room, size_t delay, kk::material mat,
                 double probe_s, const rv::params* p, bool canceller_ref = true) {
        const auto     ref  = std::make_tuple(room, delay, static_cast<int>(mat), probe_s);
        const unsigned have = refs[ref];
        const unsigned want = canceller_ref ? 3U : 1U;
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            job j;
            j.room    = room;
            j.delay   = delay;
            j.mat     = mat;
            j.probe_s = probe_s;
            j.seed    = seed_in_set(2, set);
            if (p != nullptr) {
                j.what = kind::rig;
                j.p    = *p;
                jobs.push_back(j);
            }
            if ((want & 1U) != 0 && (have & 1U) == 0) {
                job o  = j;
                o.what = kind::open;
                jobs.push_back(o);
            }
            if ((want & 2U) != 0 && (have & 2U) == 0) {
                job c  = j;
                c.what = kind::canceller;
                jobs.push_back(c);
            }
        }
        refs[ref] = have | want;
    }

    results collect(const std::vector<job>& jobs) {
        std::map<std::tuple<std::string, size_t, int, double, unsigned>, double> open;
        std::map<std::tuple<std::string, size_t, int, double, unsigned>, double> canc;
        for (const auto& j : jobs) {
            const auto k = std::make_tuple(j.room, j.delay, static_cast<int>(j.mat), j.probe_s, j.seed);
            if (j.what == kind::open) {
                open[k] = j.value;
            }
            else if (j.what == kind::canceller) {
                canc[k] = j.value;
            }
        }
        results r;
        // Seed order: jobs were sorted by probe only (stable), so within a
        // row the seed sets stay in enqueue order; sort explicitly anyway.
        std::vector<const job*> rows;
        for (const auto& j : jobs) {
            if (j.what != kind::open) {
                rows.push_back(&j);
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [](const job* a, const job* b) { return a->seed < b->seed; });
        for (const job* j : rows) {
            const row_key k{j->room, j->delay, static_cast<int>(j->mat), j->probe_s, job_label(*j)};
            const auto    s = std::make_tuple(j->room, j->delay, static_cast<int>(j->mat), j->probe_s, j->seed);
            r.chain[k].push_back(j->value);
            r.asg[k].push_back(j->value - open.at(s));
            const auto c = canc.find(s);
            if (j->what == kind::rig && c != canc.end()) {
                r.cost[k].push_back(c->second - j->value);
            }
            r.exact[k] = j->exact;
        }
        return r;
    }

    std::string per_seed(const std::vector<double>& v) {
        std::string s;
        for (const double x : v) {
            s += " " + fmt(x);
        }
        return s;
    }

    void record(const char* suite, const row_key& k, const char* what, double v) {
        char name[256];
        std::snprintf(name, sizeof name, "%s_%s_d%zu_%s_%gs_%s_%s", suite, std::get<0>(k).c_str(), std::get<1>(k),
                      mat_name(static_cast<kk::material>(std::get<2>(k))), std::get<3>(k), std::get<4>(k).c_str(),
                      what);
        std::string key = name;
        std::replace_if(key.begin(), key.end(), [](char c) { return c == ' ' || c == '+' || c == '.'; }, '_');
        ::testing::Test::RecordProperty(key, fmt(v));
    }

    /// Print every row: ASG per seed and median, and the cost per seed and
    /// median for reverb rows.
    void print_rows(const results& r, const char* suite) {
        for (const auto& [k, v] : r.asg) {
            std::printf("%-9s d=%-4zu %-6s %5.0f s  %-44s ASG%s | median %s", std::get<0>(k).c_str(), std::get<1>(k),
                        mat_name(static_cast<kk::material>(std::get<2>(k))), std::get<3>(k), std::get<4>(k).c_str(),
                        per_seed(v).c_str(), fmt(median(v)).c_str());
            record(suite, k, "asg", median(v));
            const auto c = r.cost.find(k);
            if (c != r.cost.end()) {
                std::printf(" | cost%s | median %s", per_seed(c->second).c_str(), fmt(median(c->second)).c_str());
                record(suite, k, "cost", median(c->second));
            }
            std::printf("\n");
        }
    }

    rv::params plate(rv::variant v, double decay, double wet, double damping = 0.0005,
                     tap::mu::reverb_return ret = tap::mu::reverb_return::left) {
        rv::params p;
        p.plate   = v;
        p.decay   = decay;
        p.wet     = wet;
        p.damping = damping;
        p.ret     = ret;
        return p;
    }

    /// Probes from an environment override ("10,20,40") or the default.
    std::vector<double> probes_from_env(const char* var, std::vector<double> dflt) {
        const char* v = std::getenv(var);
        if (v == nullptr) {
            return dflt;
        }
        std::vector<double> out;
        std::string         s = v;
        size_t              a = 0;
        while (a < s.size()) {
            const size_t b = s.find(',', a);
            out.push_back(std::stod(s.substr(a, b == std::string::npos ? std::string::npos : b - a)));
            if (b == std::string::npos) {
                break;
            }
            a = b + 1;
        }
        return out;
    }

} // namespace

// Probe-length convergence of the reverb rows: how long a probe the
// in-loop claims need.
TEST(ReverbStageSweep, ProbeConvergence) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the reverb sweep";
    }
    const auto probes = probes_from_env("REVERB_SWEEP_PROBES", {10.0, 20.0, 40.0, 80.0, 160.0});
    const auto both   = std::vector<kk::material>{kk::material::held, kk::material::speech};
    const auto held   = std::vector<kk::material>{kk::material::held};

    /// One convergence row: a configuration, the materials and the longest
    /// probe it runs to.
    struct row {
        rv::params                p;
        std::vector<kk::material> mats;
        double                    longest = 160.0;
    };
    // The as-shipped plate at the three decays carries the full probe set
    // on both materials; the paper plate at decay 0.5 (the as-shipped
    // decay-0.7 T30) on the held note only; the 5 Hz shift with the plate,
    // whole-bus and dry-only, to 80 s on the held note (the budget).
    std::vector<row> rows;
    for (const double decay : {0.5, 0.7, 0.85}) {
        rows.push_back({plate(rv::variant::shipped, decay, 0.30), both, 160.0});
    }
    rows.push_back({plate(rv::variant::paper, 0.5, 0.30), held, 160.0});
    for (const auto t : {rv::topology::bus_shift, rv::topology::dry_shift}) {
        rv::params p = plate(rv::variant::shipped, 0.7, 0.30);
        p.topo       = t;
        p.shift_hz   = 5.0;
        rows.push_back({p, held, 80.0});
    }
    if (const char* only = std::getenv("REVERB_SWEEP_ROWS")) { // e.g. "0,1" (indices into rows)
        const auto       keep = probes_from_env("REVERB_SWEEP_ROWS", {});
        std::vector<row> kept;
        for (const double k : keep) {
            kept.push_back(rows.at(static_cast<size_t>(k)));
        }
        rows = kept;
        (void)only;
    }
    const double canceller_longest = 80.0; // the canceller alone (the cost reference) up to 80 s

    std::vector<job> jobs;
    ref_map          refs;
    for (const std::string room : {"cabin", "mt5"}) {
        for (const auto& rw : rows) {
            for (const auto mat : rw.mats) {
                for (const double probe : probes) {
                    if (probe <= rw.longest) {
                        enqueue(jobs, refs, room, kk::k_s1, mat, probe, &rw.p, probe <= canceller_longest);
                    }
                }
            }
        }
    }
    run_jobs(jobs);
    const auto r = collect(jobs);
    std::printf("\nProbe convergence, S1, wet 0.30, damping 0.0005, L return; per seed set (2 22 42 62 82), dB\n");
    print_rows(r, "conv");

    // The summary: each row's median ASG per probe, and the converged probe.
    std::printf("\nMedian ASG by probe (dB); converged = the shortest probe from which every longer probe's median "
                "is within 0.25 dB of the row's longest probe's\n");
    std::printf("%-9s %-6s %-44s", "room", "mat", "row");
    for (const double p : probes) {
        std::printf(" %7.0f s", p);
    }
    std::printf("   converged | cost by probe\n");
    for (const std::string room : {"cabin", "mt5"}) {
        for (const auto mat : both) {
            std::vector<std::pair<std::string, double>> labels = {{"canceller", canceller_longest}};
            for (const auto& rw : rows) {
                if (std::find(rw.mats.begin(), rw.mats.end(), mat) != rw.mats.end()) {
                    labels.emplace_back(label(&rw.p), rw.longest);
                }
            }
            for (const auto& [l, longest] : labels) {
                std::vector<double> med;
                std::vector<double> cost;
                for (const double p : probes) {
                    const row_key k{room, kk::k_s1, static_cast<int>(mat), p, l};
                    const auto    a = r.asg.find(k);
                    if (p > longest || a == r.asg.end()) {
                        continue;
                    }
                    med.push_back(median(a->second));
                    const auto c = r.cost.find(k);
                    cost.push_back(c == r.cost.end() ? std::nan("") : median(c->second));
                }
                if (med.empty()) {
                    continue;
                }
                size_t conv = med.size(); // none
                for (size_t i = med.size(); i-- > 0;) {
                    if (std::abs(med[i] - med.back()) <= 0.25) {
                        conv = i;
                    }
                    else {
                        break;
                    }
                }
                // The longest probe alone agrees with itself: converged
                // only if the next-shorter one agrees too.
                const bool ok = conv + 1 < med.size();
                std::printf("%-9s %-6s %-44s", room.c_str(), mat_name(mat), l.c_str());
                for (size_t i = 0; i < probes.size(); ++i) {
                    std::printf(" %9s", i < med.size() ? fmt(med[i]).c_str() : "");
                }
                if (ok) {
                    std::printf("   %7.0f s |", probes[conv]);
                }
                else {
                    std::printf("   not conv. |");
                }
                if (l != "canceller") {
                    for (const double c : cost) {
                        std::printf(" %s", std::isnan(c) ? "   n/a" : fmt(c).c_str());
                    }
                }
                std::printf("\n");
                char name[160];
                std::snprintf(name, sizeof name, "conv_%s_%s_%s_converged_s", room.c_str(), mat_name(mat), l.c_str());
                std::string key = name;
                std::replace_if(key.begin(), key.end(), [](char ch) { return ch == ' ' || ch == '.'; }, '_');
                ::testing::Test::RecordProperty(key, ok ? fmt(probes[conv]) : std::string("none"));
            }
        }
    }
    for (const auto& [k, v] : r.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}

namespace {

    const std::vector<std::string>& sweep_rooms() {
        static const std::vector<std::string> k_rooms = {"cabin", "studio", "rehearsal", "hall", "mt5", "mt9"};
        return k_rooms;
    }

    /// The mix level (dB, white input) of one plate configuration, cached.
    double mix_level_db(const rv::params& p) {
        static std::map<std::tuple<int, double, double, int, double>, double> cache;
        static std::mutex                                                     m;
        const auto key = std::make_tuple(static_cast<int>(p.plate), p.decay, p.damping, static_cast<int>(p.ret), p.wet);
        {
            const std::lock_guard<std::mutex> lock(m);
            const auto                        it = cache.find(key);
            if (it != cache.end()) {
                return it->second;
            }
        }
        const auto                        h = rv::plate_ir(p.plate, p.decay, p.damping);
        const double                      v = rv::mix_energy_db(rv::mono_return(h, p.ret), p.wet);
        const std::lock_guard<std::mutex> lock(m);
        cache[key] = v;
        return v;
    }

    /// Across rooms: the median of the per-room medians, their range, and
    /// the per-seed range over every room.
    struct across {
        bool   found             = false;
        double median_of_medians = 0.0;
        double room_lo           = 0.0;
        double room_hi           = 0.0;
        double seed_lo           = 0.0;
        double seed_hi           = 0.0;
    };

    across summarize(const std::map<row_key, std::vector<double>>& m, size_t delay, kk::material mat, double probe,
                     const std::string& l) {
        std::vector<double> meds;
        std::vector<double> all;
        for (const auto& room : sweep_rooms()) {
            const auto it = m.find(row_key{room, delay, static_cast<int>(mat), probe, l});
            if (it == m.end()) {
                continue;
            }
            meds.push_back(median(it->second));
            all.insert(all.end(), it->second.begin(), it->second.end());
        }
        across a;
        if (meds.empty()) {
            return a;
        }
        a.found             = true;
        a.median_of_medians = median(meds);
        a.room_lo           = *std::min_element(meds.begin(), meds.end());
        a.room_hi           = *std::max_element(meds.begin(), meds.end());
        a.seed_lo           = *std::min_element(all.begin(), all.end());
        a.seed_hi           = *std::max_element(all.begin(), all.end());
        return a;
    }

    /// The probe a grid or shift row on `mat` runs with, from
    /// ProbeConvergence (macOS x86_64): every held-note row there (both
    /// plates, decay 0.5 to 0.85, the 5 Hz shift rows; cabin and mt5)
    /// converged at 10 s, its 20 to 160 s medians identical, so the held
    /// rows run 20 s, twice that; the speech rows converged at 40 or 80 s
    /// where they converged at all (three of eight did not by 160 s), so
    /// they run 80 s and are reported, not gated.
    /// REVERB_SWEEP_PROBE overrides every row.
    double grid_probe(kk::material mat) {
        if (const char* e = std::getenv("REVERB_SWEEP_PROBE")) {
            return std::stod(e);
        }
        return mat == kk::material::held ? 20.0 : 80.0;
    }

    /// The decays the grid runs (REVERB_SWEEP_DECAYS, e.g. "0.5,0.7").
    std::vector<double> grid_decays() {
        return probes_from_env("REVERB_SWEEP_DECAYS", {0.5, 0.7, 0.85});
    }

    /// A delay and a material.
    struct condition {
        size_t       delay;
        kk::material mat;
    };

} // namespace

// ASG and the reverb's cost versus wet, decay, damping and return, behind
// the canceller, six rooms of both generator families.
TEST(ReverbStageSweep, Grid) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the reverb sweep";
    }
    const condition s1_held{kk::k_s1, kk::material::held};
    const condition s1_speech{kk::k_s1, kk::material::speech};
    const condition s3_held{kk::k_s3, kk::material::held};
    const condition s3_speech{kk::k_s3, kk::material::speech};
    const auto      held_conds = std::vector<condition>{s1_held, s3_held};
    const auto      all_conds  = std::vector<condition>{s1_held, s3_held, s1_speech};

    // The held note at S1 and S3: wet 0.15 / 0.30 / 0.50 x decay 0.5 / 0.7
    // / 0.85 on the as-shipped plate and at decay 0.5 on the paper plate
    // (its convergence was measured there only), damping 0.0005, L return;
    // damping 0.5 and the (L + R) / 2 return at wet 0.30 on the as-shipped
    // plate. The speech envelope at S1, wet 0.30, the as-shipped plate, 80 s
    // probes (grid_probe). Not run (the budget): the speech envelope at S3,
    // and the paper plate at decay 0.7 and 0.85 (the bare loop covers them).
    std::vector<std::pair<rv::params, std::vector<condition>>> rows;
    for (const auto v : {rv::variant::shipped, rv::variant::paper}) {
        for (const double decay : grid_decays()) {
            if (v == rv::variant::paper && decay > 0.6) {
                continue;
            }
            for (const double wet : {0.15, 0.30, 0.50}) {
                const bool speech = v == rv::variant::shipped && wet == 0.30;
                rows.emplace_back(plate(v, decay, wet), speech ? all_conds : held_conds);
            }
            if (v == rv::variant::shipped) {
                rows.emplace_back(plate(v, decay, 0.30, 0.5), held_conds);
                rows.emplace_back(plate(v, decay, 0.30, 0.0005, tap::mu::reverb_return::mid), held_conds);
            }
        }
    }
    std::vector<job> jobs;
    ref_map          refs;
    for (const auto& room : sweep_rooms()) {
        for (const auto& [c, conds] : rows) {
            for (const auto& cond : conds) {
                enqueue(jobs, refs, room, cond.delay, cond.mat, grid_probe(cond.mat), &c);
            }
        }
    }
    run_jobs(jobs);
    const auto r = collect(jobs);
    std::printf("\nGrid, band-limited rooms, per seed set (2 22 42 62 82), dB\n");
    print_rows(r, "grid");

    std::printf("\nGrid summary: median over the six rooms of each room's five-seed median; [per-room median range]; "
                "{per-seed range, 30 runs}; mix level = the mix's broadband level change (white input); matched = "
                "cost - mix level\n");
    for (const auto& cond : {s1_held, s1_speech, s3_held, s3_speech}) {
        if (!summarize(r.asg, cond.delay, cond.mat, grid_probe(cond.mat), "canceller").found) {
            continue; // a condition this grid did not run
        }
        std::printf("\n%s, %s\n", cond.delay == kk::k_s1 ? "S1 (10 ms)" : "S3 (20 ms)", mat_name(cond.mat));
        for (const double probe : {10.0, 20.0, 40.0, 80.0, 160.0}) {
            const auto c = summarize(r.asg, cond.delay, cond.mat, probe, "canceller");
            if (!c.found) {
                continue;
            }
            std::printf("  canceller alone (%3.0f s probe): ASG %s [%s, %s] {%s, %s}\n", probe,
                        fmt(c.median_of_medians).c_str(), fmt(c.room_lo).c_str(), fmt(c.room_hi).c_str(),
                        fmt(c.seed_lo).c_str(), fmt(c.seed_hi).c_str());
        }
        std::printf("  %-32s %5s  %-26s %-40s %7s %8s\n", "row", "probe", "ASG [rooms]", "cost [rooms] {seeds}",
                    "mix lvl", "matched");
        for (const auto& [cfg, conds] : rows) {
            const std::string l     = label(&cfg);
            const double      probe = grid_probe(cond.mat);
            const auto        a     = summarize(r.asg, cond.delay, cond.mat, probe, l);
            const auto        c     = summarize(r.cost, cond.delay, cond.mat, probe, l);
            if (!a.found) {
                continue;
            }
            const double lvl = mix_level_db(cfg);
            std::printf("  %-32s %3.0f s  %s [%s, %s]   %s [%s, %s] {%s, %s}   %s   %s\n", l.c_str(), probe,
                        fmt(a.median_of_medians).c_str(), fmt(a.room_lo).c_str(), fmt(a.room_hi).c_str(),
                        fmt(c.median_of_medians).c_str(), fmt(c.room_lo).c_str(), fmt(c.room_hi).c_str(),
                        fmt(c.seed_lo).c_str(), fmt(c.seed_hi).c_str(), fmt(lvl).c_str(),
                        fmt(c.median_of_medians - lvl).c_str());
        }
    }
    for (const auto& [k, v] : r.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}

// The shift with the reverb: whole-bus shift + reverb (the chain's slot
// order, the tail recirculating through the shifter) against the dry-only
// shift (shifted_dry_mix), runaway limits.
TEST(ReverbStageSweep, Shift) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the reverb sweep";
    }
    const char*             env   = std::getenv("REVERB_SWEEP_SHIFT_PROBE");
    const double            probe = env != nullptr ? std::stod(env) : grid_probe(kk::material::held);
    std::vector<rv::params> configs;
    for (const double hz : {2.0, 5.0}) {
        rv::params s;
        s.topo     = rv::topology::shift_only;
        s.shift_hz = hz;
        configs.push_back(s);
    }
    // Wet 0.30 only (the budget; the audible driver covers 0.15 too).
    for (const double decay : {0.5, 0.7}) {
        for (const double wet : {0.30}) {
            configs.push_back(plate(rv::variant::shipped, decay, wet));
            for (const double hz : {2.0, 5.0}) {
                for (const auto t : {rv::topology::bus_shift, rv::topology::dry_shift}) {
                    rv::params p = plate(rv::variant::shipped, decay, wet);
                    p.topo       = t;
                    p.shift_hz   = hz;
                    configs.push_back(p);
                }
            }
        }
    }
    // REVERB_SWEEP_DELAYS ("480", "960" or "480,960") runs one delay per
    // invocation: the two halves' tables are independent.
    std::vector<size_t> delays;
    for (const double d : probes_from_env("REVERB_SWEEP_DELAYS", {480.0, 960.0})) {
        delays.push_back(static_cast<size_t>(d));
    }
    std::vector<job> jobs;
    ref_map          refs;
    for (const auto& room : sweep_rooms()) {
        for (const size_t d : delays) {
            for (const auto& c : configs) {
                enqueue(jobs, refs, room, d, kk::material::held, probe, &c);
            }
        }
    }
    run_jobs(jobs);
    const auto r = collect(jobs);
    std::printf("\nShift, band-limited rooms, held note, %g s probes, per seed set (2 22 42 62 82), dB\n", probe);
    print_rows(r, "shift");

    std::printf("\nShift summary: median over the six rooms of each room's five-seed median [per-room range]; "
                "bus - dry = per-seed (whole-bus shift + reverb) - (dry-only shift), chain limits\n");
    for (const size_t d : delays) {
        std::printf("\n%s, held\n", d == kk::k_s1 ? "S1 (10 ms)" : "S3 (20 ms)");
        const auto c = summarize(r.asg, d, kk::material::held, probe, "canceller");
        std::printf("  %-44s ASG %s [%s, %s]\n", "canceller", fmt(c.median_of_medians).c_str(), fmt(c.room_lo).c_str(),
                    fmt(c.room_hi).c_str());
        for (const auto& cfg : configs) {
            const std::string l = label(&cfg);
            const auto        a = summarize(r.asg, d, kk::material::held, probe, l);
            std::printf("  %-44s ASG %s [%s, %s]", l.c_str(), fmt(a.median_of_medians).c_str(), fmt(a.room_lo).c_str(),
                        fmt(a.room_hi).c_str());
            if (cfg.topo == rv::topology::bus_shift) {
                rv::params dry = cfg;
                dry.topo       = rv::topology::dry_shift;
                std::vector<double> room_meds;
                for (const auto& room : sweep_rooms()) {
                    const auto&         b = r.chain.at(row_key{room, d, 0, probe, l});
                    const auto&         o = r.chain.at(row_key{room, d, 0, probe, label(&dry)});
                    std::vector<double> diff;
                    for (size_t i = 0; i < b.size() && i < o.size(); ++i) {
                        diff.push_back(b[i] - o[i]);
                    }
                    room_meds.push_back(median(diff));
                }
                std::printf("   bus - dry, per-room medians:");
                for (const double x : room_meds) {
                    std::printf(" %s", fmt(x).c_str());
                }
                std::printf(" | median %s, %zu of %zu rooms < 0", fmt(median(room_meds)).c_str(),
                            static_cast<size_t>(
                                std::count_if(room_meds.begin(), room_meds.end(), [](double x) { return x < 0.0; })),
                            room_meds.size());
            }
            std::printf("\n");
        }
    }
    for (const auto& [k, v] : r.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}

// Phase 0's bare-loop cost table (no canceller), for both plates: what the
// mix costs the open loop by the magnitude bound and by the exact Nyquist
// crossing at d = 480, four fixtures at 4096 taps, and each plate's T30.
TEST(ReverbStageSweep, BareLoopCost) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the reverb sweep";
    }
    const std::vector<std::string> rooms  = {"studio", "rehearsal", "hall", "cabin"};
    const std::vector<double>      decays = {0.3, 0.5, 0.7, 0.85};
    const std::vector<double>      damps  = {0.0005, 0.2, 0.5};
    const std::vector<double>      wets   = {0.15, 0.30, 0.50};
    constexpr size_t               k_d    = 480;
    const auto                     t0     = std::chrono::steady_clock::now();

    struct cell {
        double bound    = 0.0;
        double exact    = 0.0;
        double dn_bound = 0.0;
        double dn_exact = 0.0;
        double level    = 0.0;
    };
    using cell_key = std::tuple<int, double, double, int, double>; // plate, decay, damping, return, wet
    std::map<cell_key, cell>                               table;
    std::map<std::tuple<int, double, double, int>, double> t30;
    std::map<std::tuple<int, double, double>, size_t>      ir_len;

    std::printf("\nBare-loop cost per room (dB, bound / exact at d = 480, on the 2N grid); rooms studio rehearsal "
                "hall cabin\n");
    for (const auto v : {rv::variant::shipped, rv::variant::paper}) {
        const int vi = static_cast<int>(v);
        for (const double decay : decays) {
            for (const double damping : damps) {
                const auto h                 = rv::plate_ir(v, decay, damping);
                ir_len[{vi, decay, damping}] = h.l.size();
                const size_t n1              = rv::next_pow2(4 * (h.l.size() + 4095 + k_d));
                for (const auto mode : {tap::mu::reverb_return::left, tap::mu::reverb_return::mid}) {
                    const int  mi                 = static_cast<int>(mode);
                    const auto r                  = rv::mono_return(h, mode);
                    t30[{vi, decay, damping, mi}] = rv::t30_seconds(r);
                    // [grid][wet] -> per room (bound cost, exact cost)
                    std::map<std::pair<size_t, double>, std::vector<std::pair<double, double>>> costs;
                    for (const size_t n : {n1, 2 * n1}) {
                        const auto R = rv::spectrum(r, n);
                        for (const auto& room : rooms) {
                            const auto F   = rv::spectrum(rv::fixture_4096(room), n);
                            const auto dry = rv::analytic_limits(F, nullptr, 0.0, k_d);
                            for (const double w : wets) {
                                const auto mix = rv::analytic_limits(F, &R, w, k_d);
                                costs[{n, w}].emplace_back(dry.bound_db - mix.bound_db, dry.exact_db - mix.exact_db);
                            }
                        }
                    }
                    for (const double w : wets) {
                        const auto&         a = costs.at({n1, w});
                        const auto&         b = costs.at({2 * n1, w});
                        std::vector<double> bounds;
                        std::vector<double> exacts;
                        cell                c;
                        std::printf("%-7s decay %.2f damping %-6g %s w %.2f:", rv::name(v), decay, damping,
                                    mode == tap::mu::reverb_return::left ? "L" : "M", w);
                        for (size_t i = 0; i < rooms.size(); ++i) {
                            bounds.push_back(b[i].first);
                            exacts.push_back(b[i].second);
                            c.dn_bound = std::max(c.dn_bound, std::abs(b[i].first - a[i].first));
                            c.dn_exact = std::max(c.dn_exact, std::abs(b[i].second - a[i].second));
                            std::printf("  %s / %s", fmt(b[i].first).c_str(), fmt(b[i].second).c_str());
                        }
                        c.bound                            = rv::median_of_four(bounds);
                        c.exact                            = rv::median_of_four(exacts);
                        c.level                            = rv::mix_energy_db(r, w);
                        table[{vi, decay, damping, mi, w}] = c;
                        std::printf("  | median %s / %s, max |2N - N| %.4f / %.4f, mix level %s\n",
                                    fmt(c.bound).c_str(), fmt(c.exact).c_str(), c.dn_bound, c.dn_exact,
                                    fmt(c.level).c_str());
                    }
                }
            }
        }
    }

    std::printf("\nT30 (s; Schroeder, -5 to -35 dB) and IR length to -90 dB re peak (samples)\n");
    std::printf("%-8s %6s %8s %10s %12s %10s\n", "plate", "decay", "damping", "T30 L", "T30 (L+R)/2", "IR len");
    for (const auto v : {rv::variant::shipped, rv::variant::paper}) {
        const int vi = static_cast<int>(v);
        for (const double decay : decays) {
            for (const double damping : damps) {
                std::printf("%-8s %6.2f %8g %10.4f %12.4f %10zu\n", rv::name(v), decay, damping,
                            t30.at({vi, decay, damping, 0}), t30.at({vi, decay, damping, 1}),
                            ir_len.at({vi, decay, damping}));
            }
        }
    }
    for (const int mi : {0, 1}) {
        std::printf("\nBare-loop cost, %s return: median over the four rooms (mean of the middle two, as phase 0), "
                    "bound / exact at d = 480; matched = the w 0.30 bound - the mix level at w 0.30\n",
                    mi == 0 ? "L" : "(L + R) / 2");
        std::printf("%-8s %6s %8s %7s | %-16s %-16s %-16s | %8s %8s\n", "plate", "decay", "damping", "T30", "w 0.15",
                    "w 0.30", "w 0.50", "lvl 0.30", "matched");
        for (const auto v : {rv::variant::shipped, rv::variant::paper}) {
            const int vi = static_cast<int>(v);
            for (const double decay : decays) {
                for (const double damping : damps) {
                    std::printf("%-8s %6.2f %8g %7.2f |", rv::name(v), decay, damping,
                                t30.at({vi, decay, damping, mi}));
                    for (const double w : wets) {
                        const auto& c = table.at({vi, decay, damping, mi, w});
                        std::printf(" %s / %s ", fmt(c.bound).c_str(), fmt(c.exact).c_str());
                    }
                    const auto& c = table.at({vi, decay, damping, mi, 0.30});
                    std::printf("| %s %s\n", fmt(c.level).c_str(), fmt(c.bound - c.level).c_str());
                    char name[96];
                    std::snprintf(name, sizeof name, "bare_%s_decay%g_damping%g_%s_w030_bound", rv::name(v), decay,
                                  damping, mi == 0 ? "L" : "M");
                    std::string key = name;
                    std::replace(key.begin(), key.end(), '.', '_');
                    ::testing::Test::RecordProperty(key, fmt(c.bound));
                }
            }
        }
    }
    std::printf("\nBareLoopCost wall %.1f s\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const auto& [k, c] : table) {
        EXPECT_TRUE(std::isfinite(c.bound) && std::isfinite(c.exact));
    }
}
