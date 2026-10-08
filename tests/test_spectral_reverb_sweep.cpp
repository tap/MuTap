// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The spectral reverb behind the canceller (docs/reverb-afc.md, "Spectral
// reverb"; HANDOFF item 11). SLOW: skipped unless MUTAP_SLOW=1 (the repo's
// C++ slow-test convention; see tests/CMakeLists.txt). Jobs run on at most 4
// threads unless MUTAP_SLOW_THREADS says otherwise; each finished bisection
// prints a "job" line (the checkpoint of a run that takes hours), every table
// the doc quotes is printed, and each row's median is recorded with
// RecordProperty.
//
//   MUTAP_SLOW=1 MUTAP_SLOW_THREADS=3 build/tests/mutap_tests --gtest_filter='SpectralReverbSweep.*'
//
// Measurement: support/spectral_rig.h (the reverb suites' protocol,
// support/karaoke_asg.h, with tap::mu::spectral_reverb as decorrelated_loop's
// forward stage: converged 1500 blocks at exact_msg_db - 6 with the reverb
// flat in the loop, shaped once from the converged F_hat, the shape held
// through the bisection; its one block of latency inside the forward delay).
// Bisection to 0.1 dB over [exact - 20, exact + 30] with the canceller and
// [exact - 40, exact + 10] without; seed sets 2 / 22 / 42 / 62 / 82; six
// band-limited rooms of both generator families. Like for like with the
// plates (test_reverb_stage_sweep.cpp):
//
//   ASG          = the chain's limit - the dry open loop's (no canceller, no
//                  reverb), same probe material and length;
//   cost         = the canceller-alone limit - the chain's, per seed;
//   open cost    = the dry open loop's limit - the open loop's with the
//                  reverb (no canceller), per seed: HANDOFF item 11's
//                  "flat is unsafe open loop";
//   level        = the stage's broadband level change for white input (the
//                  reverb as shaped in that run); matched = cost - level.
//
//   ProbeConvergence  cabin and mt5, S1, wet 0.30, rt60 1 s, flat and shaped
//                     (shape_max 1, 2, 4), held note and speech envelope,
//                     probes 10 / 20 / 40 / 80 / 160 s, with the open-loop
//                     limit of the same reverb; the canceller alone to 80 s.
//                     "Converged" as in the plates' sweep: the shortest probe
//                     from which every longer probe's five-seed median is
//                     within 0.25 dB of the longest's.
//   Grid              six rooms, S1 and S3: the held note, flat and shaped
//                     (1, 2, 4) at wet 0.15 / 0.30 and rt60 1 s and 1.1846 s
//                     (the as-shipped plate's decay-0.5 T30, L), beside the
//                     as-shipped plate at decay 0.5 and the same wets; the
//                     speech envelope at wet 0.30, rt60 1.1846 s. Probes from
//                     ProbeConvergence (grid_probe). Prints the cost, open
//                     cost and the hypothesis statistics (spectral_rig.h).
//
// Environment: SPECTRAL_SWEEP_PROBES ("10,20"), SPECTRAL_SWEEP_MATS ("held",
// "speech" or both), SPECTRAL_SWEEP_SHAPES ("0,1,2,4"; 0 = flat),
// SPECTRAL_SWEEP_DELAYS ("480,960"), SPECTRAL_SWEEP_RT60S, SPECTRAL_SWEEP_WETS,
// SPECTRAL_SWEEP_ROOMS, SPECTRAL_SWEEP_PROBE_HELD / _SPEECH,
// SPECTRAL_SWEEP_PLATES=0 (skip the plate rows).

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
#include "support/spectral_rig.h"

namespace {

    using mutap_test::median;
    using mutap_test::seed_in_set;
    namespace kk = mutap_test::karaoke;
    namespace rv = mutap_test::reverb;
    namespace sp = mutap_test::spectral;

    /// The as-shipped plate's decay-0.5 T30 (L return, damping 0.0005;
    /// ReverbStage.PlateDecayAndMixLevel): the matched rt60.
    constexpr double k_plate_t30 = 1.1846;

    bool slow_enabled() {
        const char* v = std::getenv("MUTAP_SLOW");
        return v != nullptr && std::string(v) == "1";
    }

    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw = std::thread::hardware_concurrency();
        return std::clamp(hw > 1 ? hw - 1 : 1U, 1U, 4U);
    }

    std::string fmt(double v) {
        if (std::isnan(v)) {
            return "   n/a";
        }
        char b[16];
        std::snprintf(b, sizeof b, "%+6.2f", v);
        return b;
    }

    std::string fmt3(double v) {
        if (std::isnan(v)) {
            return "  n/a";
        }
        char b[16];
        std::snprintf(b, sizeof b, "%+.3f", v);
        return b;
    }

    const char* mat_name(kk::material m) {
        return m == kk::material::held ? "held" : "speech";
    }

    /// Comma-separated numbers from the environment, or the default.
    std::vector<double> list_from_env(const char* var, std::vector<double> dflt) {
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

    std::vector<kk::material> mats_from_env(std::vector<kk::material> dflt) {
        const char* v = std::getenv("SPECTRAL_SWEEP_MATS");
        if (v == nullptr) {
            return dflt;
        }
        const std::string         s = v;
        std::vector<kk::material> out;
        if (s.find("held") != std::string::npos) {
            out.push_back(kk::material::held);
        }
        if (s.find("speech") != std::string::npos) {
            out.push_back(kk::material::speech);
        }
        return out;
    }

    std::vector<std::string> rooms_from_env(std::vector<std::string> dflt) {
        const char* v = std::getenv("SPECTRAL_SWEEP_ROOMS");
        if (v == nullptr) {
            return dflt;
        }
        std::vector<std::string> out;
        std::string              s = v;
        size_t                   a = 0;
        while (a < s.size()) {
            const size_t b = s.find(',', a);
            out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) {
                break;
            }
            a = b + 1;
        }
        return out;
    }

    std::string spectral_label(const sp::params& p) {
        char b[96];
        if (p.shape_max > 0.0) {
            std::snprintf(b, sizeof b, "spectral shaped %g rt%.4g w%.2f", p.shape_max, p.rt60, p.wet);
        }
        else {
            std::snprintf(b, sizeof b, "spectral flat rt%.4g w%.2f", p.rt60, p.wet);
        }
        return b;
    }

    std::string plate_label(const rv::params& p) {
        char b[96];
        std::snprintf(b, sizeof b, "plate %s d%.2f w%.2f", rv::name(p.plate), p.decay, p.wet);
        return b;
    }

    enum class kind { open, canceller, plate, spectral };

    /// One bisection (a spectral job is the chain bisection and, with
    /// open_reverb, the open loop with the reverb).
    struct job {
        std::string  room;
        size_t       delay   = kk::k_s1;
        kk::material mat     = kk::material::held;
        double       probe_s = 20.0;
        unsigned     seed    = 2;
        kind         what    = kind::canceller;
        rv::params   plate;
        sp::params   spec;
        bool         chain       = true; ///< spectral: bisect the chain
        bool         open_reverb = true; ///< spectral: bisect the open loop with the reverb
        // results
        double  value = 0.0; ///< the bisected chain (or open) limit, dB
        double  exact = 0.0;
        sp::run run;
        double  seconds = 0.0;
    };

    std::string job_label(const job& j) {
        switch (j.what) {
        case kind::open:
            return "open";
        case kind::canceller:
            return "canceller";
        case kind::plate:
            return plate_label(j.plate);
        case kind::spectral:
        default:
            return spectral_label(j.spec);
        }
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
        switch (j.what) {
        case kind::open:
            j.value = kk::open_loop_db(path, j.delay, kk::near_end(j.mat, p.probe_blocks, j.seed + 10), p);
            break;
        case kind::canceller:
            j.value = kk::measure(path, s, p, j.seed, false).chain_db;
            break;
        case kind::plate: {
            rv::rig rig(j.plate);
            s.stage = mutap_test::forward_stage<double>::of(&rig);
            j.value = kk::measure(path, s, p, j.seed, false).chain_db;
            break;
        }
        case kind::spectral:
        default:
            j.run   = sp::measure(path, j.mat, p, j.seed, j.spec, j.chain, j.open_reverb);
            j.value = j.chain ? j.run.chain_db : j.run.open_rev_db;
            break;
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
                    std::printf("  job %s d=%zu %s %g s seed %u %s: %+.2f", j.room.c_str(), j.delay, mat_name(j.mat),
                                j.probe_s, j.seed, job_label(j).c_str(), j.value - j.exact);
                    if (j.what == kind::spectral) {
                        std::printf(" open %+.2f level %+.2f rhoT %+.3f share %.3f mis %+.2f",
                                    j.run.open_rev_db - j.exact, j.run.level, j.run.bins.rho_t_mis,
                                    j.run.bins.share_top, j.run.bins.misalign_db);
                    }
                    std::printf(" (%.0f s)\n", j.seconds);
                    std::fflush(stdout); // the job lines are the checkpoint of a run that takes hours
                    if (++done % 50 == 0 || done == jobs.size()) {
                        std::printf("  ... %zu / %zu jobs, %.0f s\n", done, jobs.size(),
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
        std::printf("%zu jobs on %u threads: wall %.1f s, cpu-sum %.1f s\n", jobs.size(), worker_count(),
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), cpu);
    }

    /// Row key: room, delay, material, probe, label.
    using row_key  = std::tuple<std::string, size_t, int, double, std::string>;
    using seed_key = std::tuple<std::string, size_t, int, double, unsigned>;

    struct results {
        std::map<row_key, std::vector<double>>         asg;      ///< per seed set, seed order
        std::map<row_key, std::vector<double>>         cost;     ///< canceller - this
        std::map<row_key, std::vector<double>>         open_asg; ///< open with reverb - dry open (spectral)
        std::map<row_key, std::vector<double>>         level;    ///< spectral
        std::map<row_key, std::vector<const job*>>     runs;     ///< spectral jobs, seed order
        std::map<seed_key, double>                     open;
        std::map<seed_key, double>                     canc;
        std::map<std::pair<row_key, unsigned>, double> chain; ///< (row, seed) -> limit
    };

    /// Enqueue the references (dry open loop, canceller alone) once per
    /// (room, delay, material, probe).
    using ref_map = std::map<std::tuple<std::string, size_t, int, double>, unsigned>;

    void enqueue_refs(std::vector<job>& jobs, ref_map& refs, const std::string& room, size_t delay, kk::material mat,
                      double probe_s, bool canceller) {
        const auto     ref  = std::make_tuple(room, delay, static_cast<int>(mat), probe_s);
        const unsigned have = refs[ref];
        const unsigned want = canceller ? 3U : 1U;
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            job j;
            j.room    = room;
            j.delay   = delay;
            j.mat     = mat;
            j.probe_s = probe_s;
            j.seed    = seed_in_set(2, set);
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

    void enqueue_row(std::vector<job>& jobs, const std::string& room, size_t delay, kk::material mat, double probe_s,
                     const job& proto) {
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            job j     = proto;
            j.room    = room;
            j.delay   = delay;
            j.mat     = mat;
            j.probe_s = probe_s;
            j.seed    = seed_in_set(2, set);
            jobs.push_back(j);
        }
    }

    results collect(const std::vector<job>& jobs) {
        results r;
        for (const auto& j : jobs) {
            const seed_key k{j.room, j.delay, static_cast<int>(j.mat), j.probe_s, j.seed};
            if (j.what == kind::open) {
                r.open[k] = j.value;
            }
            else if (j.what == kind::canceller) {
                r.canc[k] = j.value;
            }
        }
        std::vector<const job*> rows;
        for (const auto& j : jobs) {
            if (j.what != kind::open) {
                rows.push_back(&j);
            }
        }
        std::stable_sort(rows.begin(), rows.end(), [](const job* a, const job* b) { return a->seed < b->seed; });
        for (const job* j : rows) {
            const row_key  k{j->room, j->delay, static_cast<int>(j->mat), j->probe_s, job_label(*j)};
            const seed_key s{j->room, j->delay, static_cast<int>(j->mat), j->probe_s, j->seed};
            const auto     o = r.open.find(s);
            if (o == r.open.end()) {
                continue;
            }
            if (j->what == kind::spectral && !j->chain) { // an open-loop-only job
                r.open_asg[k].push_back(j->run.open_rev_db - o->second);
                continue; // its shape repeats the chain job's (same seed, same convergence)
            }
            r.asg[k].push_back(j->value - o->second);
            r.chain[{k, j->seed}] = j->value;
            const auto c          = r.canc.find(s);
            if (j->what != kind::canceller && c != r.canc.end()) {
                r.cost[k].push_back(c->second - j->value);
            }
            if (j->what == kind::spectral) {
                r.runs[k].push_back(j);
                r.level[k].push_back(j->run.level);
                if (!std::isnan(j->run.open_rev_db)) {
                    r.open_asg[k].push_back(j->run.open_rev_db - o->second);
                }
            }
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

    void print_rows(const results& r, const char* suite) {
        for (const auto& [k, v] : r.asg) {
            std::printf("%-9s d=%-4zu %-6s %5.0f s  %-40s ASG%s | median %s", std::get<0>(k).c_str(), std::get<1>(k),
                        mat_name(static_cast<kk::material>(std::get<2>(k))), std::get<3>(k), std::get<4>(k).c_str(),
                        per_seed(v).c_str(), fmt(median(v)).c_str());
            record(suite, k, "asg", median(v));
            const auto c = r.cost.find(k);
            if (c != r.cost.end()) {
                std::printf(" | cost%s | median %s", per_seed(c->second).c_str(), fmt(median(c->second)).c_str());
                record(suite, k, "cost", median(c->second));
            }
            const auto o = r.open_asg.find(k);
            if (o != r.open_asg.end()) {
                std::printf(" | open ASG%s | median %s", per_seed(o->second).c_str(), fmt(median(o->second)).c_str());
                record(suite, k, "open_asg", median(o->second));
            }
            const auto l = r.level.find(k);
            if (l != r.level.end()) {
                std::printf(" | level median %s", fmt(median(l->second)).c_str());
            }
            std::printf("\n");
        }
    }

    /// Across rooms: the median of the per-room medians, their range, and
    /// the per-seed range over every room.
    struct across {
        bool   found             = false;
        double median_of_medians = std::nan("");
        double room_lo           = std::nan("");
        double room_hi           = std::nan("");
        double seed_lo           = std::nan("");
        double seed_hi           = std::nan("");
    };

    across summarize(const std::map<row_key, std::vector<double>>& m, const std::vector<std::string>& rooms,
                     size_t delay, kk::material mat, double probe, const std::string& l) {
        std::vector<double> meds;
        std::vector<double> all;
        for (const auto& room : rooms) {
            const auto it = m.find(row_key{room, delay, static_cast<int>(mat), probe, l});
            if (it == m.end() || it->second.empty()) {
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

    std::string across_str(const across& a) {
        return fmt(a.median_of_medians) + " [" + fmt(a.room_lo) + ", " + fmt(a.room_hi) + "] {" + fmt(a.seed_lo) + ", "
               + fmt(a.seed_hi) + "}";
    }

    /// The converged-probe summary: each row's median by probe and the
    /// shortest converged probe (0.25 dB of the longest).
    void convergence_table(const std::map<row_key, std::vector<double>>& m, const std::string& room, kk::material mat,
                           const std::string& l, const std::vector<double>& probes, const char* what) {
        std::vector<double> med;
        std::vector<double> used;
        for (const double p : probes) {
            const auto a = m.find(row_key{room, kk::k_s1, static_cast<int>(mat), p, l});
            if (a == m.end() || a->second.empty()) {
                continue;
            }
            med.push_back(median(a->second));
            used.push_back(p);
        }
        if (med.empty()) {
            return;
        }
        size_t conv = med.size();
        for (size_t i = med.size(); i-- > 0;) {
            if (std::abs(med[i] - med.back()) <= 0.25) {
                conv = i;
            }
            else {
                break;
            }
        }
        const bool ok = conv + 1 < med.size();
        std::printf("%-6s %-6s %-9s %-40s", room.c_str(), mat_name(mat), what, l.c_str());
        for (size_t i = 0; i < probes.size(); ++i) {
            std::printf(" %8s", i < med.size() ? fmt(med[i]).c_str() : "");
        }
        if (ok) {
            std::printf("   %5.0f s\n", used[conv]);
        }
        else {
            std::printf("   not converged\n");
        }
        char name[192];
        std::snprintf(name, sizeof name, "conv_%s_%s_%s_%s_converged_s", room.c_str(), mat_name(mat), what, l.c_str());
        std::string key = name;
        std::replace_if(key.begin(), key.end(), [](char ch) { return ch == ' ' || ch == '.'; }, '_');
        ::testing::Test::RecordProperty(key, ok ? fmt(used[conv]) : std::string("none"));
    }

    /// The plate mix's level change for white input (reverb_rig.h), dB.
    double plate_level_db(const rv::params& p) {
        const auto h = rv::plate_ir(p.plate, p.decay, p.damping);
        return rv::mix_energy_db(rv::mono_return(h, p.ret), p.wet);
    }

    /// The probe the open-loop rows (no canceller, the reverb in the loop)
    /// run with (ProbeConvergence; SPECTRAL_SWEEP_PROBE_OPEN overrides).
    double open_probe(kk::material /*mat*/) {
        if (const char* e = std::getenv("SPECTRAL_SWEEP_PROBE_OPEN")) {
            return std::stod(e);
        }
        return 160.0;
    }

    /// The probe each material's grid rows run with (ProbeConvergence;
    /// SPECTRAL_SWEEP_PROBE_HELD / _SPEECH override). On the held note the
    /// spectral chain rows converged at 80 s where they converged at all
    /// (cabin flat, shape_max 1 and 2; the rest not by 160 s), and the 40 s
    /// median ASG read at or above the 160 s one in 7 of the 8 cabin and mt5
    /// rows (mt5 shape_max 2: +5.98 against +6.46); the plate and canceller
    /// rows converged at 10 s. 40 s is the budget: the held grid is not
    /// converged, mostly understates the spectral reverb's cost, and is
    /// reported, not gated.
    double grid_probe(kk::material mat) {
        if (mat == kk::material::held) {
            if (const char* e = std::getenv("SPECTRAL_SWEEP_PROBE_HELD")) {
                return std::stod(e);
            }
            return 40.0;
        }
        if (const char* e = std::getenv("SPECTRAL_SWEEP_PROBE_SPEECH")) {
            return std::stod(e);
        }
        return 80.0;
    }

} // namespace

// Probe-length convergence of the spectral rows, the chain's and the open
// loop's: how long a probe the claims need.
TEST(SpectralReverbSweep, ProbeConvergence) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the spectral reverb sweep";
    }
    const auto   probes            = list_from_env("SPECTRAL_SWEEP_PROBES", {10.0, 20.0, 40.0, 80.0, 160.0});
    const auto   shapes            = list_from_env("SPECTRAL_SWEEP_SHAPES", {0.0, 1.0, 2.0, 4.0});
    const auto   mats              = mats_from_env({kk::material::held, kk::material::speech});
    const auto   rooms             = rooms_from_env({"cabin", "mt5"});
    const double canceller_longest = 80.0;

    std::vector<job> jobs;
    ref_map          refs;
    for (const auto& room : rooms) {
        for (const auto mat : mats) {
            for (const double probe : probes) {
                enqueue_refs(jobs, refs, room, kk::k_s1, mat, probe, probe <= canceller_longest);
                for (const double sm : shapes) {
                    job proto;
                    proto.what           = kind::spectral;
                    proto.spec.rt60      = 1.0;
                    proto.spec.wet       = 0.30;
                    proto.spec.shape_max = sm;
                    enqueue_row(jobs, room, kk::k_s1, mat, probe, proto);
                }
            }
        }
    }
    run_jobs(jobs);
    const auto r = collect(jobs);
    std::printf("\nProbe convergence, S1, wet 0.30, rt60 1 s; per seed set (2 22 42 62 82), dB\n");
    print_rows(r, "sconv");

    std::printf("\nMedian ASG by probe (dB); chain = with the canceller, open = the open loop with the reverb (no "
                "canceller), both re the dry open loop; converged = the shortest probe from which every longer "
                "probe's median is within 0.25 dB of the longest's\n");
    std::printf("%-6s %-6s %-9s %-40s", "room", "mat", "", "row");
    for (const double p : probes) {
        std::printf(" %6.0f s", p);
    }
    std::printf("   converged\n");
    for (const auto& room : rooms) {
        for (const auto mat : mats) {
            convergence_table(r.asg, room, mat, "canceller", probes, "chain");
            for (const double sm : shapes) {
                sp::params p;
                p.rt60      = 1.0;
                p.wet       = 0.30;
                p.shape_max = sm;
                convergence_table(r.asg, room, mat, spectral_label(p), probes, "chain");
                convergence_table(r.open_asg, room, mat, spectral_label(p), probes, "open");
                convergence_table(r.cost, room, mat, spectral_label(p), probes, "cost");
            }
        }
    }
    for (const auto& [k, v] : r.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}

// The grid: the spectral reverb against the plate behind the canceller and
// with no canceller, and the per-bin hypothesis, six rooms.
TEST(SpectralReverbSweep, Grid) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 for the spectral reverb sweep";
    }
    const auto  rooms  = rooms_from_env({"cabin", "studio", "rehearsal", "hall", "mt5", "mt9"});
    const auto  shapes = list_from_env("SPECTRAL_SWEEP_SHAPES", {0.0, 1.0, 2.0, 4.0});
    const auto  wets   = list_from_env("SPECTRAL_SWEEP_WETS", {0.15, 0.30});
    const auto  rt60s  = list_from_env("SPECTRAL_SWEEP_RT60S", {1.0, k_plate_t30});
    const auto  delays = list_from_env("SPECTRAL_SWEEP_DELAYS", {480.0, 960.0});
    const auto  mats   = mats_from_env({kk::material::held, kk::material::speech});
    const char* pe     = std::getenv("SPECTRAL_SWEEP_PLATES");
    const bool  plates = pe == nullptr || std::string(pe) != "0";

    // The rows (the budget: ProbeConvergence put the spectral rows at 80 s
    // and beyond, against the plates' 10 s). Held: every shape at both wets
    // at the matched rt60 (the plate's decay-0.5 T30), and at rt60 1 s at
    // wet 0.30 (ProbeConvergence's configuration), the plate at each wet;
    // the open loop (no canceller) with each spectral row at the matched
    // rt60. Speech, S1 only: wet 0.30, rt60 1 s (ProbeConvergence's), and the
    // plate there; no open loop.
    struct row {
        job          proto;
        kk::material mat;
    };
    std::vector<row> rows;
    for (const auto mat : mats) {
        const bool held = mat == kk::material::held;
        for (const double w : wets) {
            if (!held && w != 0.30) {
                continue;
            }
            if (plates) {
                job pj;
                pj.what        = kind::plate;
                pj.plate.plate = rv::variant::shipped;
                pj.plate.decay = 0.5;
                pj.plate.wet   = w;
                rows.push_back({pj, mat});
            }
            for (const double rt : rt60s) {
                if (held ? (rt != k_plate_t30 && w != 0.30) : rt != 1.0) {
                    continue;
                }
                for (const double sm : shapes) {
                    job sj;
                    sj.what           = kind::spectral;
                    sj.spec.rt60      = rt;
                    sj.spec.wet       = w;
                    sj.spec.shape_max = sm;
                    rows.push_back({sj, mat});
                }
            }
        }
    }
    std::vector<job> jobs;
    ref_map          refs;
    for (const auto& room : rooms) {
        for (const double dd : delays) {
            const auto d = static_cast<size_t>(dd);
            for (const auto& rw : rows) {
                if (rw.mat == kk::material::speech && d != kk::k_s1) {
                    continue;
                }
                enqueue_refs(jobs, refs, room, d, rw.mat, grid_probe(rw.mat), true);
                job chain_job         = rw.proto;
                chain_job.open_reverb = false;
                enqueue_row(jobs, room, d, rw.mat, grid_probe(rw.mat), chain_job);
                if (rw.proto.what == kind::spectral && rw.mat == kk::material::held
                    && rw.proto.spec.rt60 == k_plate_t30) {
                    // The open loop with the same reverb, on its own probe.
                    enqueue_refs(jobs, refs, room, d, rw.mat, open_probe(rw.mat), false);
                    job open_job   = rw.proto;
                    open_job.chain = false;
                    enqueue_row(jobs, room, d, rw.mat, open_probe(rw.mat), open_job);
                }
            }
        }
    }
    run_jobs(jobs);
    const auto r = collect(jobs);
    std::printf("\nGrid, band-limited rooms, per seed set (2 22 42 62 82), dB\n");
    print_rows(r, "sgrid");

    std::printf("\nGrid summary: median over the rooms of each room's five-seed median [per-room range] {per-seed "
                "range}; cost = canceller alone - chain; open = the open loop with the reverb (no canceller) re the "
                "dry open loop; level = the stage's level change for white input (median over runs); matched = "
                "cost - level\n");
    for (const double dd : delays) {
        const auto d = static_cast<size_t>(dd);
        for (const auto mat : mats) {
            const double probe = grid_probe(mat);
            const auto   c     = summarize(r.asg, rooms, d, mat, probe, "canceller");
            if (!c.found) {
                continue;
            }
            std::printf("\n%s, %s, %g s probes. Canceller alone: ASG %s\n", d == kk::k_s1 ? "S1 (10 ms)" : "S3 (20 ms)",
                        mat_name(mat), probe, across_str(c).c_str());
            std::printf("  %-40s %-34s %-34s %-34s %7s %8s\n", "row", "ASG", "cost", "open ASG", "level", "matched");
            for (const auto& rw : rows) {
                if (rw.mat != mat) {
                    continue;
                }
                const std::string l  = job_label(rw.proto);
                const auto        a  = summarize(r.asg, rooms, d, mat, probe, l);
                const auto        co = summarize(r.cost, rooms, d, mat, probe, l);
                const auto        o  = summarize(r.open_asg, rooms, d, mat, open_probe(mat), l);
                auto              lv = summarize(r.level, rooms, d, mat, probe, l);
                if (rw.proto.what == kind::plate) {
                    lv.median_of_medians = plate_level_db(rw.proto.plate);
                }
                if (!a.found) {
                    continue;
                }
                std::printf("  %-40s %-34s %-34s %-34s %7s %8s\n", l.c_str(), across_str(a).c_str(),
                            across_str(co).c_str(), o.found ? across_str(o).c_str() : "",
                            fmt(lv.median_of_medians).c_str(),
                            fmt(co.median_of_medians - lv.median_of_medians).c_str());
            }
        }
    }

    // Spectral against the plate at the same wet, per seed (chain limits),
    // and shaped against flat.
    std::printf("\nPer-seed differences of chain limits, median over the rooms of each room's median [per-room "
                "range] (count of rooms where the room median > 0)\n");
    auto paired = [&](size_t d, kk::material mat, const std::string& a_label, const std::string& b_label) {
        const double        probe = grid_probe(mat);
        std::vector<double> meds;
        for (const auto& room : rooms) {
            const row_key       ka{room, d, static_cast<int>(mat), probe, a_label};
            const row_key       kb{room, d, static_cast<int>(mat), probe, b_label};
            std::vector<double> diff;
            for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                const unsigned seed = seed_in_set(2, set);
                const auto     ia   = r.chain.find({ka, seed});
                const auto     ib   = r.chain.find({kb, seed});
                if (ia != r.chain.end() && ib != r.chain.end()) {
                    diff.push_back(ia->second - ib->second);
                }
            }
            if (!diff.empty()) {
                meds.push_back(median(diff));
            }
        }
        if (meds.empty()) {
            return;
        }
        const auto pos = std::count_if(meds.begin(), meds.end(), [](double x) { return x > 0.0; });
        std::printf("  %s %-6s %-40s - %-30s %s [%s, %s] (%ld of %zu)\n", d == kk::k_s1 ? "S1" : "S3", mat_name(mat),
                    a_label.c_str(), b_label.c_str(), fmt(median(meds)).c_str(),
                    fmt(*std::min_element(meds.begin(), meds.end())).c_str(),
                    fmt(*std::max_element(meds.begin(), meds.end())).c_str(), static_cast<long>(pos), meds.size());
    };
    for (const double dd : delays) {
        const auto d = static_cast<size_t>(dd);
        for (const auto& rw : rows) {
            if (rw.proto.what != kind::spectral) {
                continue;
            }
            const std::string l = job_label(rw.proto);
            if (plates) {
                rv::params pp;
                pp.plate = rv::variant::shipped;
                pp.decay = 0.5;
                pp.wet   = rw.proto.spec.wet;
                paired(d, rw.mat, l, plate_label(pp));
            }
            if (rw.proto.spec.shape_max > 0.0) {
                sp::params flat = rw.proto.spec;
                flat.shape_max  = 0.0;
                paired(d, rw.mat, l, spectral_label(flat));
            }
        }
    }

    // The hypothesis, per shaped row: medians over every run (rooms x seeds)
    // of the per-run rank correlations and the worst-quarter energy share,
    // and across runs, Spearman's rho of the share against the run's shaped
    // - flat chain limit (same room, delay, seed).
    std::printf("\nShaping hypothesis (spectral_rig.h): per run at the moment of shaping, rho = Spearman over the 65 "
                "bins; medians over the runs [min, max]; flat share = 16/65 = 0.246\n");
    std::printf("  %-9s %-40s %-26s %-26s %-26s %-26s %-26s %-14s %-14s\n", "", "row", "rho(T60, |F-F^|)",
                "rho(T60, |F-F^|/|F|)", "share(E, top quarter)", "peak m*g re flat, dB", "misalignment dB",
                "rho(share, s-f)", "rho(peak, s-f)");
    for (const double dd : delays) {
        const auto d = static_cast<size_t>(dd);
        for (const auto& rw : rows) {
            if (rw.proto.what != kind::spectral || rw.proto.spec.shape_max <= 0.0) {
                continue;
            }
            const std::string l     = job_label(rw.proto);
            const double      probe = grid_probe(rw.mat);
            sp::params        flat  = rw.proto.spec;
            flat.shape_max          = 0.0;
            std::vector<double> rt;
            std::vector<double> pk;
            std::vector<double> rr;
            std::vector<double> peak_x;
            std::vector<double> sh;
            std::vector<double> mi;
            std::vector<double> share_x;
            std::vector<double> gain_y;
            for (const auto& room : rooms) {
                const row_key k{room, d, static_cast<int>(rw.mat), probe, l};
                const row_key kf{room, d, static_cast<int>(rw.mat), probe, spectral_label(flat)};
                const auto    it = r.runs.find(k);
                if (it == r.runs.end()) {
                    continue;
                }
                for (const job* j : it->second) {
                    rt.push_back(j->run.bins.rho_t_mis);
                    pk.push_back(j->run.bins.peak_rel_db);
                    rr.push_back(j->run.bins.rho_t_rel);
                    sh.push_back(j->run.bins.share_top);
                    mi.push_back(j->run.bins.misalign_db);
                    const auto f = r.chain.find({kf, j->seed});
                    if (f != r.chain.end()) {
                        share_x.push_back(j->run.bins.share_top);
                        peak_x.push_back(j->run.bins.peak_rel_db);
                        gain_y.push_back(j->value - f->second);
                    }
                }
            }
            if (rt.empty()) {
                continue;
            }
            auto mm = [](const std::vector<double>& v) {
                return fmt3(median(v)) + " [" + fmt3(*std::min_element(v.begin(), v.end())) + ", "
                       + fmt3(*std::max_element(v.begin(), v.end())) + "]";
            };
            const double rho_sg = share_x.size() > 2 ? sp::spearman(share_x, gain_y) : std::nan("");
            const double rho_pg = peak_x.size() > 2 ? sp::spearman(peak_x, gain_y) : std::nan("");
            std::printf("  %s %-6s %-40s %-26s %-26s %-26s %-26s %-26s %-14s %s (n %zu)\n", d == kk::k_s1 ? "S1" : "S3",
                        mat_name(rw.mat), l.c_str(), mm(rt).c_str(), mm(rr).c_str(), mm(sh).c_str(), mm(pk).c_str(),
                        mm(mi).c_str(), fmt3(rho_sg).c_str(), fmt3(rho_pg).c_str(), share_x.size());
            const row_key rk{"all", d, static_cast<int>(rw.mat), probe, l};
            record("hyp", rk, "rho_t_mis", median(rt));
            record("hyp", rk, "share_top", median(sh));
            record("hyp", rk, "rho_share_gain", rho_sg);
            record("hyp", rk, "peak_rel_db", median(pk));
            record("hyp", rk, "rho_peak_gain", rho_pg);
        }
    }
    for (const auto& [k, v] : r.asg) {
        for (const double x : v) {
            EXPECT_TRUE(std::isfinite(x));
        }
    }
}
