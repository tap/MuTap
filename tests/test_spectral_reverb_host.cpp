// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The spectral reverb behind the canceller (docs/reverb-afc.md, "Spectral
// reverb"): what the reverb is (T30 against the plate's, its level) and the
// gated in-loop rows, on support/spectral_rig.h's measurement. Host-only,
// double precision, like every closed-loop claim (tests/CMakeLists.txt); the
// class's plumbing on every target is test_spectral_reverb.cpp, and the
// MUTAP_SLOW sweep behind these rows is test_spectral_reverb_sweep.cpp.
//
// Measured on macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release; each
// test's comment has its numbers.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "support/karaoke_asg.h"
#include "support/reverb_rig.h"
#include "support/rooms.h"
#include "support/spectral_rig.h"

namespace {

    namespace kk = mutap_test::karaoke;
    namespace rv = mutap_test::reverb;
    namespace sp = mutap_test::spectral;
    using mutap_test::median;
    using mutap_test::seed_in_set;

    /// The as-shipped plate's decay-0.5 T30, L (ReverbStage.PlateDecayAndMixLevel).
    constexpr double k_plate_t30 = 1.1846;

    /// At most 4 workers (the suites share their machines);
    /// MUTAP_SLOW_THREADS overrides.
    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw = std::thread::hardware_concurrency();
        return std::clamp(hw > 1 ? hw - 1 : 1U, 1U, 4U);
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

    std::string per_seed(const std::vector<double>& v) {
        std::string s;
        for (const double x : v) {
            char b[16];
            std::snprintf(b, sizeof b, " %+.2f", x);
            s += b;
        }
        return s;
    }

    /// The held note on a band-limited room at S1, wet 0.30, rt60 1 s
    /// (ProbeConvergence's configuration), five seed sets. Per seed: the dry
    /// open loop's limit, and for each spectral row (shape_max 0 = flat) the
    /// limit of the open loop with the reverb (no canceller) or of the chain;
    /// absolute forward gains, dB; bisection to 0.5 dB over the given
    /// brackets (dB re exact_msg_db) on `probe_s` probes.
    struct spectral_rows {
        std::vector<double>              open;
        std::vector<std::vector<double>> value; ///< [row][seed set]
        std::vector<double>              asg(size_t row) const {
            std::vector<double> a;
            for (size_t s = 0; s < open.size(); ++s) {
                a.push_back(value[row][s] - open[s]);
            }
            return a;
        }
    };

    spectral_rows run_spectral(const std::string& room, double probe_s, const std::vector<double>& shapes, bool chain,
                               double lo, double hi, unsigned sets = mutap_test::k_claim_seed_sets) {
        const auto    path = kk::room(room);
        spectral_rows r;
        r.open.assign(sets, 0.0);
        r.value.assign(shapes.size(), std::vector<double>(sets, 0.0));
        std::vector<std::function<void()>> jobs;
        for (unsigned set = 0; set < sets; ++set) {
            const unsigned seed = seed_in_set(2, set);
            jobs.emplace_back([&, set, seed] {
                kk::protocol p;
                p.delay        = kk::k_s1;
                p.probe_blocks = kk::probe_blocks(probe_s);
                r.open[set] =
                    kk::open_loop_db(path, kk::k_s1, kk::near_end(kk::material::held, p.probe_blocks, seed + 10), p);
            });
            for (size_t k = 0; k < shapes.size(); ++k) {
                jobs.emplace_back([&, set, seed, k] {
                    kk::protocol p;
                    p.delay        = kk::k_s1;
                    p.probe_blocks = kk::probe_blocks(probe_s);
                    p.chain_lo     = lo;
                    p.chain_hi     = hi;
                    sp::params prm;
                    prm.rt60        = 1.0;
                    prm.wet         = 0.30;
                    prm.shape_max   = shapes[k];
                    const auto run  = sp::measure(path, kk::material::held, p, seed, prm, chain, !chain, lo, hi);
                    r.value[k][set] = chain ? run.chain_db : run.open_rev_db;
                });
            }
        }
        run_parallel(jobs);
        return r;
    }

    double mean(const std::vector<double>& v) {
        double sum = 0.0;
        for (const double x : v) {
            sum += x;
        }
        return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
    }

    /// The rows the default run cannot afford (ASan legs): MUTAP_SLOW=1.
    bool slow_enabled() {
        const char* v = std::getenv("MUTAP_SLOW");
        return v != nullptr && std::string(v) == "1";
    }

    /// The canceller alone's limit (karaoke_asg.h's measure, absolute dB) on
    /// the same room and material, per seed set, bisected to 0.5 dB.
    std::vector<double> canceller_alone(const std::string& room, double probe_s) {
        const auto                         path = kk::room(room);
        std::vector<double>                a(mutap_test::k_claim_seed_sets, 0.0);
        std::vector<std::function<void()>> jobs;
        for (unsigned set = 0; set < a.size(); ++set) {
            jobs.emplace_back([&, set] {
                kk::protocol p;
                p.delay        = kk::k_s1;
                p.probe_blocks = kk::probe_blocks(probe_s);
                p.chain_lo     = 2.0;
                p.chain_hi     = 20.0;
                kk::setup s;
                s.mat  = kk::material::held;
                a[set] = kk::measure(path, s, p, seed_in_set(2, set), false).chain_db;
            });
        }
        run_parallel(jobs);
        return a;
    }

} // namespace

// What the spectral reverb is, beside the plate it is compared with: its
// all-wet flat impulse response's T30 (Schroeder, -5 to -35 dB; reverb_rig.h's
// t30_seconds) at rt60 1 s and at the plate's 1.1846 s, and the stage's level
// change for white input (spectral_rig.h's level_db), flat, at wet 0.15 and
// 0.30. Measured (macOS x86_64, AppleClang 17, Release): T30 1.0000 s and
// 1.1846 s; level +3.40 / +7.60 dB at rt60 1 s and +3.81 / +8.22 dB at
// 1.1846 s (wet 0.15 / 0.30), where the plate's mix at decay 0.5 reads
// -0.956 / -1.687 dB: at equal wet the spectral reverb is the louder by 4.4
// to 9.9 dB. Deterministic; the allowances are for other hosts' libm.
TEST(SpectralReverbHost, DecayAndLevelBesideThePlate) {
    for (const double rt : {1.0, k_plate_t30}) {
        const auto   h   = sp::wet_impulse_response(rt, 4.0 * rt);
        const double t30 = rv::t30_seconds(h);
        std::printf("spectral reverb rt60 %.4f s: T30 %.4f s; level", rt, t30);
        EXPECT_NEAR(t30, rt, 0.005) << "rt60 " << rt;
        for (const double w : {0.15, 0.30}) {
            sp::params p;
            p.rt60           = rt;
            p.wet            = w;
            const double lvl = sp::level_db(sp::reverb(sp::config_of(p)));
            std::printf(" w%.2f %+.2f dB", w, lvl);
            EXPECT_GT(lvl, 0.0) << "the flat spectral reverb is louder than the dry bus";
        }
        std::printf("\n");
    }
    const auto plate = rv::plate_ir(rv::variant::shipped, 0.5, 0.0005);
    EXPECT_NEAR(rv::t30_seconds(plate.l), k_plate_t30, 0.01);
}

// WITH NO CANCELLER THE SPECTRAL REVERB IS UNSAFE (HANDOFF item 11's
// claim, re-measured with converged probes): the open loop with the reverb
// in it, against the dry open loop, held note, S1, wet 0.30, rt60 1 s, 80 s
// probes (SpectralReverbSweep.ProbeConvergence: the flat and shape_max-1
// open rows converged by 80 s in the cabin and mt5). Shaped rows are shaped
// from the F_hat a canceller converged to in the same loop (spectral_rig.h)
// and then run without it. Measured (macOS x86_64, AppleClang 17, Release;
// per seed set 2 / 22 / 42 / 62 / 82, open-loop ASG, dB):
//
//   cabin  flat          -30.51 -30.51 -30.51 -30.51 -30.51   median -30.51
//   cabin  shape_max 1   -20.66 -19.61 -16.45 -20.31 -16.80   median -19.61
//   mt5    flat          -22.77 -22.77 -22.77 -22.77 -22.77   median -22.77
//   mt5    shape_max 1   -13.98 -14.69 -15.04 -15.39 -14.34   median -14.69
//
// (The flat rows repeat to the digit across seed sets: the held note's
// period is the same in every set and the open loop has no canceller to
// differ by.) Gates: flat median < -15 dB (largest measured -22.77),
// shape_max 1 median < -8 dB (largest -14.69). The plate costs a bare loop at
// most +5.79 dB by the magnitude bound (docs/reverb-afc.md). 189.41 s on 3
// threads of the Intel Mac, so MUTAP_SLOW only; the default run carries
// UnsafeWithoutTheCancellerShortProbe below.
TEST(SpectralReverbHost, UnsafeWithoutTheCanceller) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 (189 s on 3 threads); UnsafeWithoutTheCancellerShortProbe runs by default";
    }
    for (const std::string room : {"cabin", "mt5"}) {
        const auto r = run_spectral(room, 80.0, {0.0, 1.0}, false, -40.0, 5.0);
        for (size_t k = 0; k < 2; ++k) {
            const auto a = r.asg(k);
            std::printf("%s open loop, %s: ASG%s  median %+.2f\n", room.c_str(), k == 0 ? "flat" : "shape_max 1",
                        per_seed(a).c_str(), median(a));
        }
        EXPECT_LT(median(r.asg(0)), -15.0) << room << " flat (measured -30.51 cabin, -22.77 mt5)";
        EXPECT_LT(median(r.asg(1)), -8.0) << room << " shape_max 1 (measured -19.61 cabin, -14.69 mt5)";
    }
}

// BEHIND THE CANCELLER THE SPECTRAL REVERB COSTS MORE THAN THE PLATE
// (runaway, held note, cabin, S1, wet 0.30, rt60 1 s). The cost is the
// canceller-alone limit minus the limit with the reverb, per seed set; the
// spectral rows bisect on 80 s probes (ProbeConvergence: the cabin's flat,
// shape_max 1 and 2 chain rows converged at 80 s; shape_max 4 had not by
// 160 s and is not gated), the canceller alone on 20 s (converged at 10 s,
// its 20 to 80 s medians identical). ReverbStage.CancellerHoldsBehindThePlates
// bounds the plate's cost at +3.5 dB (measured <= +0.56 there, the as-shipped
// plate at decay 0.5 and 0.7, w 0.30, cabin S1). Measured (macOS x86_64,
// AppleClang 17, Release; per seed set, dB):
//
//                  ASG                                  cost
//   flat           -5.94 -7.81 -5.62 -6.56 -5.00       +13.56 +21.62 +17.19 +18.69 +15.44  median +17.19
//   shape_max 1    -0.62 -0.62 +3.12 -0.94 +4.38       +8.25 +14.44 +8.44 +13.06 +6.06    median +8.44
//
// against the canceller alone's median ASG of +11.74 there (ProbeConvergence,
// 20 to 80 s; its limits here, absolute dB: +0.73 +6.92 +4.67 +5.23 +3.54
// at 20 s). Gates: the
// median cost of flat > +8 dB and of shape_max 1 > +4 dB, both above the
// plate's +3.5 dB ceiling (smallest measured +8.44; smallest single seed
// +6.06). 696.95 s on 3 threads of the Intel Mac, so MUTAP_SLOW only; the
// default run carries CostsMoreThanThePlateShortProbe below.
TEST(SpectralReverbHost, CostsMoreThanThePlateBehindTheCanceller) {
    if (!slow_enabled()) {
        GTEST_SKIP() << "set MUTAP_SLOW=1 (697 s on 3 threads); CostsMoreThanThePlateShortProbe runs by default";
    }
    const auto canc = canceller_alone("cabin", 20.0);
    const auto r    = run_spectral("cabin", 80.0, {0.0, 1.0}, true, -20.0, 20.0);
    std::printf("cabin canceller alone: limit%s\n", per_seed(canc).c_str());
    for (size_t k = 0; k < 2; ++k) {
        std::vector<double> cost;
        for (size_t s = 0; s < canc.size(); ++s) {
            cost.push_back(canc[s] - r.value[k][s]);
        }
        std::printf("cabin chain, %s: ASG%s  median %+.2f | cost%s  median %+.2f\n", k == 0 ? "flat" : "shape_max 1",
                    per_seed(r.asg(k)).c_str(), median(r.asg(k)), per_seed(cost).c_str(), median(cost));
        EXPECT_GT(median(cost), k == 0 ? 8.0 : 4.0)
            << (k == 0 ? "flat (measured +17.19)" : "shape_max 1 (measured +8.44)");
    }
}

// THE DEFAULT RUN'S PROXY FOR UnsafeWithoutTheCanceller (MUTAP_SLOW): the same
// open loops on 20 s probes and two seed sets (2, 22), cabin and mt5. Short
// probes UNDERSTATE how unsafe the open loop is (ProbeConvergence: every
// open row's median falls with probe length, e.g. cabin flat -29.75 at 20 s
// against -30.43 at 160 s), so a pass here is conservative. The gate is the
// mean of the two seed sets. Measured (macOS x86_64, AppleClang 17, Release;
// open-loop ASG, seed sets 2 / 22, dB; the 80 s medians from the slow row):
//
//                        20 s, sets 2 / 22     mean      80 s median (5 sets)
//   cabin  flat          -29.80 -29.80        -29.80    -30.51
//   cabin  shape_max 1   -20.31 -19.26        -19.79    -19.61
//   mt5    flat          -22.42 -22.42        -22.42    -22.77
//   mt5    shape_max 1   -13.63 -14.34        -13.98    -14.69
//
// Gates, the slow row's: flat mean < -15 dB (largest measured -22.42),
// shape_max 1 mean < -8 dB (largest -13.98). 27.03 s on 3 threads of the
// Intel Mac. (Other hosts: not yet recorded; CI's legs run it.)
TEST(SpectralReverbHost, UnsafeWithoutTheCancellerShortProbe) {
    for (const std::string room : {"cabin", "mt5"}) {
        const auto r = run_spectral(room, 20.0, {0.0, 1.0}, false, -40.0, 5.0, 2);
        for (size_t k = 0; k < 2; ++k) {
            const auto a = r.asg(k);
            std::printf("%s open loop, 20 s, %s: ASG%s  mean %+.2f\n", room.c_str(), k == 0 ? "flat" : "shape_max 1",
                        per_seed(a).c_str(), mean(a));
        }
        EXPECT_LT(mean(r.asg(0)), -15.0) << room << " flat (measured -29.80 cabin, -22.42 mt5)";
        EXPECT_LT(mean(r.asg(1)), -8.0) << room << " shape_max 1 (measured -19.79 cabin, -13.98 mt5)";
    }
}

// THE DEFAULT RUN'S PROXY FOR CostsMoreThanThePlateBehindTheCanceller
// (MUTAP_SLOW): direction only, cabin, S1, wet 0.30, two seed sets (2, 22),
// 20 s probes. Per seed set, the limit of the canceller behind the
// as-shipped plate (decay 0.5) minus its limit behind the spectral reverb
// (rt60 1 s; flat and shape_max 1): positive = the spectral reverb costs more
// than the plate (the canceller-alone limit cancels out of the difference).
// Short probes UNDERSTATE the spectral reverb's cost (ProbeConvergence: the
// cabin's flat and shape_max 1 chain medians fall from 20 to 160 s, -4.67
// to -6.82 and +2.27 to -0.27, where the plate's converged at 10 s), so a
// pass is conservative. The gate is the mean of the two seed sets.
// Measured (macOS x86_64, AppleClang 17, Release; dB):
//
//                        limits re exact_msg_db,   plate - spectral,
//                        sets 2 / 22               sets 2 / 22       mean
//   plate 0.5            +11.88 +9.69
//   flat                 -3.88 -3.88               +15.75 +13.56     +14.66
//   shape_max 1          +0.25 +1.75               +11.62 +7.94      +9.78
//
// beside the slow row's 80 s costs against the canceller alone, +17.19
// (flat) and +8.44 (shape_max 1), medians of five sets. Gates: the mean of
// plate - spectral > +6 dB for flat and > +3 dB for shape_max 1 (measured
// +14.66 and +9.78; smallest single set +7.94). 77.25 s on 3 threads of the
// Intel Mac. (Other hosts: not yet recorded; CI's legs run it.)
TEST(SpectralReverbHost, CostsMoreThanThePlateShortProbe) {
    // One pool for the six bisections (plate, flat, shape_max 1 x two seed
    // sets), each to 0.5 dB over a bracket around the 20 s limits measured
    // (dB re exact_msg_db): the plate over [+4, +18], the spectral rows over
    // [-14, +10].
    const auto                         path   = kk::room("cabin");
    constexpr unsigned                 k_sets = 2;
    std::vector<double>                plate(k_sets, 0.0);
    std::vector<std::vector<double>>   spec(2, std::vector<double>(k_sets, 0.0));
    std::vector<std::function<void()>> jobs;
    auto                               proto = [] {
        kk::protocol p;
        p.delay        = kk::k_s1;
        p.probe_blocks = kk::probe_blocks(20.0);
        return p;
    };
    for (unsigned set = 0; set < k_sets; ++set) {
        const unsigned seed = seed_in_set(2, set);
        jobs.emplace_back([&, set, seed] {
            kk::protocol p = proto();
            p.chain_lo     = 4.0;
            p.chain_hi     = 18.0;
            rv::params pp;
            pp.decay = 0.5;
            pp.wet   = 0.30;
            rv::rig   rig(pp);
            kk::setup st;
            st.mat     = kk::material::held;
            st.stage   = mutap_test::forward_stage<double>::of(&rig);
            plate[set] = kk::measure(path, st, p, seed, false).chain_db;
        });
        for (size_t k = 0; k < 2; ++k) {
            jobs.emplace_back([&, set, seed, k] {
                kk::protocol p = proto();
                p.chain_lo     = -14.0;
                p.chain_hi     = 10.0;
                sp::params prm;
                prm.rt60      = 1.0;
                prm.wet       = 0.30;
                prm.shape_max = (k == 0) ? 0.0 : 1.0;
                spec[k][set]  = sp::measure(path, kk::material::held, p, seed, prm, true, false).chain_db;
            });
        }
    }
    run_parallel(jobs);
    const double exact = mutap_test::exact_msg_db(path, kk::k_s1);
    std::printf("cabin plate 0.5 w0.30, 20 s: limit re exact_msg_db %+.2f %+.2f\n", plate[0] - exact, plate[1] - exact);
    for (size_t k = 0; k < 2; ++k) {
        std::vector<double> diff;
        for (unsigned set = 0; set < k_sets; ++set) {
            diff.push_back(plate[set] - spec[k][set]);
        }
        std::printf("cabin chain, 20 s, %s: limit re exact_msg_db %+.2f %+.2f | plate - spectral%s  mean %+.2f\n",
                    k == 0 ? "flat" : "shape_max 1", spec[k][0] - exact, spec[k][1] - exact, per_seed(diff).c_str(),
                    mean(diff));
        EXPECT_GT(mean(diff), k == 0 ? 6.0 : 3.0)
            << (k == 0 ? "flat (measured +14.66)" : "shape_max 1 (measured +9.78)");
    }
}
