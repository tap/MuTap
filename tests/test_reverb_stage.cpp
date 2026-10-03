// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The reverb behind the canceller (docs/reverb-afc.md): mutap/reverb_stage.h
// over the vendored Dattorro plates (third_party/faust/), what the plates
// are (T30 per decay, the mix's level), what they cost a bare loop, and the
// gated in-loop rows. Host-only, like every FAUST and closed-loop claim
// (tests/CMakeLists.txt); the stages' plumbing on every target is
// test_reverb_mix.cpp.
//
// Measured on macOS 15.7 x86_64 (i9-8950HK), AppleClang 17, Release; each
// test's comment has its numbers. The in-loop rows bisect on 10 s probes,
// the length every held-note row of the MUTAP_SLOW sweep's
// ProbeConvergence converged at (test_reverb_stage_sweep.cpp), and bound
// medians over the five seed sets with several dB of margin for other
// hosts; single seeds move by up to 6.5 dB here.

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

#include "faust_generated.h"
#include "mutap/frequency_shifter.h"
#include "mutap/reverb_stage.h"
#include "support/karaoke_asg.h"
#include "support/reverb_rig.h"
#include "support/rooms.h"

namespace {

    namespace rv = mutap_test::reverb;
    namespace kk = mutap_test::karaoke;
    using mutap_test::median;
    using mutap_test::seed_in_set;
    using tap::mu::reverb_return;

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

    /// One in-loop configuration: the canceller alone (no plate) or with a
    /// rig, and the chain bracket it is bisected over (dB re exact_msg_db).
    struct loop_row {
        const char* name;
        bool        with_rig = false;
        rv::params  p;
        double      lo = -15.0;
        double      hi = 30.0;
    };

    /// Per seed set: the dry open loop and each row's chain limit, on the
    /// band-limited `room` at `delay`, `mat`, a `probe_s` probe; bisection
    /// to 0.5 dB (support/karaoke_asg.h). The rows' ASG and cost per seed
    /// are printed.
    struct loop_result {
        std::vector<double>              open;
        std::vector<std::vector<double>> chain; ///< [row][seed set]
        std::vector<double>              asg(size_t row) const {
            std::vector<double> a;
            for (size_t s = 0; s < open.size(); ++s) {
                a.push_back(chain[row][s] - open[s]);
            }
            return a;
        }
        /// chain[0] - chain[row]: the plate's cost against row 0 (the
        /// canceller alone), per seed.
        std::vector<double> cost(size_t row) const {
            std::vector<double> c;
            for (size_t s = 0; s < open.size(); ++s) {
                c.push_back(chain[0][s] - chain[row][s]);
            }
            return c;
        }
    };

    loop_result run_loop_rows(const std::string& room, size_t delay, kk::material mat, double probe_s,
                              const std::vector<loop_row>& rows) {
        const auto                         path = kk::room(room);
        const unsigned                     sets = mutap_test::k_claim_seed_sets;
        loop_result                        r;
        std::vector<std::function<void()>> jobs;
        r.open.assign(sets, 0.0);
        r.chain.assign(rows.size(), std::vector<double>(sets, 0.0));
        for (unsigned set = 0; set < sets; ++set) {
            const unsigned seed = seed_in_set(2, set);
            jobs.emplace_back([&, set, seed] {
                kk::protocol p;
                p.delay        = delay;
                p.probe_blocks = kk::probe_blocks(probe_s);
                r.open[set]    = kk::open_loop_db(path, delay, kk::near_end(mat, p.probe_blocks, seed + 10), p);
            });
            for (size_t k = 0; k < rows.size(); ++k) {
                jobs.emplace_back([&, set, seed, k] {
                    kk::protocol p;
                    p.delay        = delay;
                    p.probe_blocks = kk::probe_blocks(probe_s);
                    p.chain_lo     = rows[k].lo;
                    p.chain_hi     = rows[k].hi;
                    kk::setup s;
                    s.mat = mat;
                    if (rows[k].with_rig) {
                        rv::rig rig(rows[k].p);
                        s.stage         = mutap_test::forward_stage<double>::of(&rig);
                        r.chain[k][set] = kk::measure(path, s, p, seed, false).chain_db;
                    }
                    else {
                        r.chain[k][set] = kk::measure(path, s, p, seed, false).chain_db;
                    }
                });
            }
        }
        run_parallel(jobs);
        for (size_t k = 0; k < rows.size(); ++k) {
            std::string line = std::string(rows[k].name) + " ASG";
            for (const double a : r.asg(k)) {
                char b[16];
                std::snprintf(b, sizeof b, " %+.2f", a);
                line += b;
            }
            char m[48];
            std::snprintf(m, sizeof m, "  median %+.2f", median(r.asg(k)));
            line += m;
            if (k > 0) {
                line += " | cost";
                for (const double c : r.cost(k)) {
                    char b[16];
                    std::snprintf(b, sizeof b, " %+.2f", c);
                    line += b;
                }
                std::snprintf(m, sizeof m, "  median %+.2f", median(r.cost(k)));
                line += m;
            }
            std::printf("%s %s d=%zu: %s\n", room.c_str(), mat == kk::material::held ? "held" : "speech", delay,
                        line.c_str());
        }
        return r;
    }

    using shipped_t = mutap_faust::faust_block<mutap_faust::dattorro_f64, double>;
    using paper_t   = mutap_faust::faust_block<mutap_faust::dattorro_paper_f64, double>;

    std::vector<double> test_signal(size_t n) {
        std::vector<double> x(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            x[i] = std::sin(0.013 * static_cast<double>(i)) * ((i % 480) < 240 ? 1.0 : 0.25);
        }
        return x;
    }

    /// The plate's own outputs for x, run in 64-sample blocks.
    template <class Block>
    std::vector<std::vector<double>> plate_outputs(Block& plate, const std::vector<double>& x) {
        std::vector<std::vector<double>> y(2, std::vector<double>(x.size()));
        for (size_t i = 0; i < x.size(); i += rv::k_block) {
            double* outs[2] = {&y[0][i], &y[1][i]};
            plate.process_mono(&x[i], outs, static_cast<int>(rv::k_block));
        }
        return y;
    }

} // namespace

// reverb_mix over the vendored plate is the crossfade of the input and the
// plate's own return, both returns, both plates.
TEST(ReverbStage, MixOverTheVendoredPlates) {
    const size_t n = 64 * 200;
    const auto   x = test_signal(n);
    for (const auto mode : {reverb_return::left, reverb_return::mid}) {
        shipped_t ref_plate(rv::k_fs);
        ref_plate.set("decay", 0.7);
        const auto ref = plate_outputs(ref_plate, x);

        shipped_t plate(rv::k_fs);
        plate.set("decay", 0.7);
        tap::mu::reverb_mix<double, shipped_t>::config cfg;
        cfg.wet  = 0.3;
        cfg.mode = mode;
        tap::mu::reverb_mix<double, shipped_t> mix(plate, cfg);
        std::vector<double>                    y(n);
        for (size_t i = 0; i < n; i += 128) { // two plate blocks per call
            mix.process_block(&x[i], &y[i], 128);
        }
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double r = (mode == reverb_return::left) ? ref[0][i] : 0.5 * (ref[0][i] + ref[1][i]);
            worst          = std::max(worst, std::abs(y[i] - (0.7 * x[i] + 0.3 * r)));
        }
        EXPECT_LT(worst, 1e-12) << (mode == reverb_return::left ? "L" : "M");
    }
    // The paper plate fits the same stage, and a channel count it does not
    // have is refused.
    paper_t                                      paper(rv::k_fs);
    tap::mu::reverb_mix<double, paper_t>::config cfg;
    EXPECT_NO_THROW((tap::mu::reverb_mix<double, paper_t>(paper, cfg)));
    cfg.channels = 1;
    EXPECT_THROW((tap::mu::reverb_mix<double, paper_t>(paper, cfg)), std::invalid_argument);
}

// The dry-only shift feeds the plate the unshifted input: its wet part is
// the plate's return of x, not of shift(x).
TEST(ReverbStage, DryOnlyShiftKeepsTheTailOutOfTheShifter) {
    const size_t                               n = 64 * 200;
    const auto                                 x = test_signal(n);
    shipped_t                                  ref_plate(rv::k_fs);
    const auto                                 ref = plate_outputs(ref_plate, x);
    tap::mu::frequency_shifter<double>::config sc;
    sc.shift_hz = 5.0;
    tap::mu::frequency_shifter<double> ref_shift(sc);
    std::vector<double>                s(n);
    ref_shift.process_block(x.data(), s.data(), n);

    shipped_t                          plate(rv::k_fs);
    tap::mu::frequency_shifter<double> shift(sc);
    using dry_t = tap::mu::shifted_dry_mix<double, tap::mu::frequency_shifter<double>, shipped_t>;
    dry_t::config cfg;
    cfg.wet = 0.3;
    dry_t               mix(shift, plate, cfg);
    std::vector<double> y(n);
    for (size_t i = 0; i < n; i += 64) {
        mix.process_block(&x[i], &y[i], 64);
    }
    double worst = 0.0;
    for (size_t i = 0; i < n; ++i) {
        worst = std::max(worst, std::abs(y[i] - (0.7 * s[i] + 0.3 * ref[0][i])));
    }
    EXPECT_LT(worst, 1e-12);
}

// What the two plates are: T30 per decay (Schroeder, -5 to -35 dB) and the
// mix's broadband level for white input at each wet, damping 0.0005.
// Measured (macOS x86_64, AppleClang 17, Release; the impulse response is
// deterministic, so the margins absorb only libm / FMA differences):
//
//   T30, L / (L + R) / 2, s     decay 0.3        0.5              0.7              0.85
//   as shipped                 1.0144 / 0.8620  1.1846 / 1.3082  2.1173 / 2.1530  4.6220 / 4.5825
//   paper lengths              1.7252 / 1.5975  2.1186 / 2.3626  3.7882 / 3.8598  8.2457 / 8.1939
//
//   mix level, L, dB           w 0.15   w 0.30   w 0.50
//   decay 0.5 (both plates)    -0.956   -1.687   -2.033
//   decay 0.7 (both plates)    -0.925   -1.546   -1.622
//   decay 0.85 (both plates)   -0.833   -1.134   -0.547
//
// The two plates' mix levels agree to the printed digit at every decay
// (measured; not derived here).
TEST(ReverbStage, PlateDecayAndMixLevel) {
    struct expect {
        rv::variant v;
        double      decay;
        double      t30_l;
        double      t30_m;
    };
    const expect rows[] = {
        {rv::variant::shipped, 0.3, 1.0144, 0.8620}, {rv::variant::shipped, 0.5, 1.1846, 1.3082},
        {rv::variant::shipped, 0.7, 2.1173, 2.1530}, {rv::variant::shipped, 0.85, 4.6220, 4.5825},
        {rv::variant::paper, 0.3, 1.7252, 1.5975},   {rv::variant::paper, 0.5, 2.1186, 2.3626},
        {rv::variant::paper, 0.7, 3.7882, 3.8598},   {rv::variant::paper, 0.85, 8.2457, 8.1939},
    };
    for (const auto& e : rows) {
        const auto   h   = rv::plate_ir(e.v, e.decay, 0.0005);
        const double t_l = rv::t30_seconds(h.l);
        const double t_m = rv::t30_seconds(rv::mono_return(h, reverb_return::mid));
        std::printf("%-7s decay %.2f: T30 L %.4f s, (L+R)/2 %.4f s, IR %zu samples%s; mix level L", rv::name(e.v),
                    e.decay, t_l, t_m, h.l.size(), h.capped ? " (capped)" : "");
        for (const double w : {0.15, 0.30, 0.50}) {
            std::printf(" w%.2f %+.3f", w, rv::mix_energy_db(h.l, w));
        }
        std::printf(" dB\n");
        EXPECT_FALSE(h.capped);
        EXPECT_NEAR(t_l, e.t30_l, 0.01) << rv::name(e.v) << " decay " << e.decay;
        EXPECT_NEAR(t_m, e.t30_m, 0.01) << rv::name(e.v) << " decay " << e.decay;
        if (e.decay == 0.5) {
            EXPECT_NEAR(rv::mix_energy_db(h.l, 0.30), -1.687, 0.005);
        }
        if (e.decay == 0.85) {
            EXPECT_NEAR(rv::mix_energy_db(h.l, 0.30), -1.134, 0.005);
        }
    }
}

// The bare-loop table for the paper plate (no canceller; the method of the
// anti-howl PoC's phase 0, test_reverb_stage_sweep.cpp's BareLoopCost):
// the mix's cost in open-loop stable gain by the magnitude bound and by the
// exact Nyquist crossing at d = 480, median over the four fixtures at 4096
// taps (mean of the middle two), L return, damping 0.0005, w 0.30. The
// as-shipped row reproduces phase 0's published +0.12 / -0.60 dB, the
// method check. Measured (bound / exact, dB):
//
//   as shipped, decay 0.5 (T30 1.18 s)    +0.1245 / -0.6004
//   paper,      decay 0.5 (T30 2.12 s)    +0.2990 / +0.0625
//   paper,      decay 0.7 (T30 3.79 s)    +1.6114 / +1.1364
//
// Deterministic: the impulse response and the FFTs repeat exactly on a
// host, so the 0.02 dB allowance is for other hosts' libm and FMA, not
// noise. (The grid itself is not that fine: over the sweep's 144 cells the
// cost moves up to 0.0309 dB by the bound and 0.0693 dB exact from the N
// grid to the 2N grid used here.)
TEST(ReverbStage, PaperPlateBareLoopCost) {
    struct expect {
        rv::variant v;
        double      decay;
        double      bound;
        double      exact;
    };
    const expect rows[] = {
        {rv::variant::shipped, 0.5, 0.1245, -0.6004},
        {rv::variant::paper, 0.5, 0.2990, 0.0625},
        {rv::variant::paper, 0.7, 1.6114, 1.1364},
    };
    const std::vector<std::string> rooms = {"studio", "rehearsal", "hall", "cabin"};
    constexpr size_t               k_d   = 480;
    for (const auto& e : rows) {
        const auto          h = rv::plate_ir(e.v, e.decay, 0.0005);
        const size_t        n = 2 * rv::next_pow2(4 * (h.l.size() + 4095 + k_d));
        const auto          R = rv::spectrum(h.l, n);
        std::vector<double> bounds;
        std::vector<double> exacts;
        for (const auto& room : rooms) {
            const auto F   = rv::spectrum(rv::fixture_4096(room), n);
            const auto dry = rv::analytic_limits(F, nullptr, 0.0, k_d);
            const auto mix = rv::analytic_limits(F, &R, 0.30, k_d);
            bounds.push_back(dry.bound_db - mix.bound_db);
            exacts.push_back(dry.exact_db - mix.exact_db);
        }
        const double b = rv::median_of_four(bounds);
        const double x = rv::median_of_four(exacts);
        std::printf("%-7s decay %.2f, w 0.30, L: bare-loop cost bound %+.4f dB, exact %+.4f dB\n", rv::name(e.v),
                    e.decay, b, x);
        EXPECT_NEAR(b, e.bound, 0.02) << rv::name(e.v) << " decay " << e.decay;
        EXPECT_NEAR(x, e.exact, 0.02) << rv::name(e.v) << " decay " << e.decay;
    }
}

// THE CANCELLER HOLDS BEHIND THE PLATES (runaway, held note, cabin, S1).
// Bisected like the sweep (support/karaoke_asg.h, the plate in the loop
// through tap::mu::reverb_mix) but to 0.5 dB over a narrowed bracket, on
// 10 s probes: every held-note row of ReverbStageSweep.ProbeConvergence
// converged at 10 s (its 20 to 160 s medians identical). The claim is a
// ceiling on the plate's cost, the canceller-alone limit minus the limit
// with the plate, as a median over the five seed sets, and a floor on the
// chain's ASG. Measured (macOS x86_64, AppleClang 17, Release; per seed set
// 2 / 22 / 42 / 62 / 82, dB):
//
//                          ASG                                  cost
//   canceller alone        +7.62 +13.81 +11.56 +12.12 +10.44   (median +11.56)
//   shipped d0.5  w0.15    +11.56 +8.19 +8.75 +12.41 +10.72    -3.94 +5.62 +2.81 -0.28 -0.28  median -0.28
//   shipped d0.5  w0.30    +11.84 +9.59 +11.00 +12.41 +9.59    -4.22 +4.22 +0.56 -0.28 +0.84  median +0.56
//   shipped d0.7  w0.30    +11.00 +10.72 +9.59 +11.84 +11.28   -3.37 +3.09 +1.97 +0.28 -0.84  median +0.28
//   shipped d0.85 w0.30    +14.09 +12.41 +10.72 +13.81 +13.81  -6.47 +1.41 +0.84 -1.69 -3.38  median -1.69
//   paper   d0.5  w0.30    +11.00 +10.16 +11.56 +10.16 +12.97  -3.37 +3.66 +0.00 +1.97 -2.53  median +0.00
//
// Gates: every plate's median cost < +3.5 dB (largest measured +0.56) and
// every row's median ASG > +7 dB (smallest +10.72). Single seeds swing by
// up to 6.5 dB either way, so no single-seed or direction claim is made
// (the sweep's six rooms carry the rest; docs/reverb-afc.md). 133.29 s on
// 4 threads of the Intel Mac in a full ctest run.
TEST(ReverbStage, CancellerHoldsBehindThePlates) {
    std::vector<loop_row> rows = {{"canceller", false, {}, 2.0, 20.0}};
    rows.push_back({"shipped d0.5 w0.15", true, {}, 2.0, 20.0});
    rows.back().p.wet = 0.15;
    rows.push_back({"shipped d0.5 w0.30", true, {}, 2.0, 20.0});
    rows.push_back({"shipped d0.7 w0.30", true, {}, 2.0, 20.0});
    rows.back().p.decay = 0.7;
    rows.push_back({"shipped d0.85 w0.30", true, {}, 2.0, 20.0});
    rows.back().p.decay = 0.85;
    rows.push_back({"paper d0.5 w0.30", true, {}, 2.0, 20.0});
    rows.back().p.plate = rv::variant::paper;
    const auto r        = run_loop_rows("cabin", kk::k_s1, kk::material::held, 10.0, rows);
    for (size_t k = 0; k < rows.size(); ++k) {
        EXPECT_GT(median(r.asg(k)), 7.0) << rows[k].name << " (measured >= +10.72)";
        if (k > 0) {
            EXPECT_LT(median(r.cost(k)), 3.5) << rows[k].name << " (measured <= +0.56)";
        }
    }
}

// THE TWO SHIFT TOPOLOGIES BY RUNAWAY (held note, cabin, S1, 10 s probes,
// 0.5 dB): with the as-shipped plate at decay 0.5, wet 0.30 and a 2 Hz
// shift, the whole-bus shift (shifter in the decorrelator slot, the plate
// behind it) against the dry-only shift (shifted_dry_mix). Measured (macOS
// x86_64, AppleClang 17, Release; seed sets 2 / 22 / 42 / 62 / 82):
//
//   whole-bus ASG   +20.38 +18.12 +19.81 +17.00 +15.88  median +18.12
//   dry-only ASG    +16.44 +15.03 +12.22 +16.16 +13.34  median +15.03
//   whole-bus - dry-only, per seed: +3.94 +3.09 +7.59 +0.84 +2.53, median +3.09
//
// Gate: the per-seed median of whole-bus - dry-only > 0 (margin 3.09 dB).
// In the sweep (ReverbStageSweep.Shift, 20 s, six rooms, 2 and 5 Hz, decay
// 0.5 and 0.7) the per-room median is >= 0 in 23 of 24 at S1 (one -0.98).
// THIS IS RUNAWAY ONLY: on the ramp with the criterion the dry-only shift
// is the one audible later, in 13 of 16 pairs (docs/reverb-afc.md). 63.73 s
// on 4 threads of the Intel Mac in a full ctest run.
TEST(ReverbStage, WholeBusShiftHoldsMoreRunawayThanDryOnly) {
    std::vector<loop_row> rows = {{"canceller", false, {}, 2.0, 20.0}};
    rows.push_back({"bus-shift 2 Hz d0.5 w0.30", true, {}, 8.0, 26.0});
    rows.back().p.topo     = rv::topology::bus_shift;
    rows.back().p.shift_hz = 2.0;
    rows.push_back({"dry-shift 2 Hz d0.5 w0.30", true, {}, 8.0, 26.0});
    rows.back().p.topo     = rv::topology::dry_shift;
    rows.back().p.shift_hz = 2.0;
    const auto          r  = run_loop_rows("cabin", kk::k_s1, kk::material::held, 10.0, rows);
    std::vector<double> diff;
    for (size_t s = 0; s < r.open.size(); ++s) {
        diff.push_back(r.chain[1][s] - r.chain[2][s]);
    }
    std::string line = "whole-bus - dry-only, per seed:";
    for (const double d : diff) {
        char b[16];
        std::snprintf(b, sizeof b, " %+.2f", d);
        line += b;
    }
    std::printf("%s  median %+.2f\n", line.c_str(), median(diff));
    EXPECT_GT(median(diff), 0.0) << "measured +3.09";
}
