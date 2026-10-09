// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The two-mic loop (support/two_mic_loop.h): plumbing and a smoke test. The
// go/no-go measurement itself (two-mic ASG against the single-mic ASG, the
// PoC plan's D11) is test_two_mic_sweep.cpp, MUTAP_SLOW=1; NOTHING HERE IS
// AN ASG CLAIM.
//
// HOST-ONLY (tests/CMakeLists.txt): the smoke test runs its seed sets on
// std::thread, at most 4 at a time by default (MUTAP_SLOW_THREADS
// overrides, as in the MUTAP_SLOW sweeps).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "support/closed_loop.h"
#include "support/rooms.h"
#include "support/two_mic_loop.h"

namespace {

    namespace tmic = mutap_test::two_mic;
    using mutap_test::median;
    using mutap_test::seed_in_set;

    /// At most 4 workers by default (the suites share their machines);
    /// MUTAP_SLOW_THREADS overrides (test_howl_guard_host.cpp's rule).
    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw_threads = std::thread::hardware_concurrency();
        return std::clamp(hw_threads > 1 ? hw_threads - 1 : 1U, 1U, 4U);
    }

    /// f(i) for every i in [0, n), on worker_count() threads.
    template <typename F>
    void parallel_for(size_t n, const F& f) {
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < worker_count(); ++t) {
            pool.emplace_back([&] {
                for (size_t i = next++; i < n; i = next++) {
                    f(i);
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
    }

    /// Misalignment (dB) of `ir` against `f` delayed by `lag` taps, over the
    /// filter's length.
    double misalignment_db(const std::vector<float>& f, size_t lag, const std::vector<float>& ir) {
        double num = 0.0;
        double den = 0.0;
        for (size_t i = 0; i < ir.size(); ++i) {
            const double t = (i >= lag && i - lag < f.size()) ? static_cast<double>(f[i - lag]) : 0.0;
            const double d = static_cast<double>(ir[i]) - t;
            num += d * d;
            den += t * t;
        }
        return 10.0 * std::log10(num / den);
    }

} // namespace

// One mic, no canceller: the loop is closed_loop_sim's at forward_delay =
// the electrical delay (K z^-d F), sample for sample. Measured on the Intel
// Mac: max |difference| 0 over 2000 blocks (the same k-ordered sums).
TEST(TwoMicLoop, OneMicDryLoopIsClosedLoopSim) {
    const auto   path = mutap_test::karaoke::room("rehearsal");
    const size_t d    = tmic::k_s1;
    const double gain = mutap_test::exact_msg_db(path, d) - 3.0;
    const auto   v    = mutap_test::voiced_near_end<double>(2000 * tmic::k_block, 2);
    using loop_t      = mutap_test::multi_mic_loop<double>;
    loop_t::config lc;
    lc.paths            = {path};
    lc.electrical_delay = d;
    lc.reference_delay  = tmic::aligned_reference_delay(d);
    lc.forward_gain_db  = gain;
    loop_t loop(lc);

    mutap_test::closed_loop_sim<double>::config cc;
    cc.feedback_path   = path;
    cc.forward_delay   = d;
    cc.forward_gain_db = gain;
    mutap_test::closed_loop_sim<double> ref(cc);

    double worst = 0.0;
    double peak  = 0.0;
    for (size_t blk = 0; blk < 2000; ++blk) {
        const double* x[] = {&v[blk * tmic::k_block]};
        loop.step(x, static_cast<std::vector<tmic::afc<double>>*>(nullptr));
        ref.step(x[0], static_cast<tap::mu::partitioned_fdaf<double>*>(nullptr));
        for (size_t i = 0; i < tmic::k_block; ++i) {
            worst = std::max(worst, std::abs(loop.bus_block()[i] - ref.error_block()[i]));
            peak  = std::max(peak, std::abs(ref.error_block()[i]));
        }
    }
    std::printf("max |bus - closed_loop_sim| %g, peak %g\n", worst, peak);
    EXPECT_GT(peak, 1.0); // the loop rings (3 dB under its limit)
    EXPECT_LE(worst, 1e-9 * peak) << "measured 0 on the Intel Mac";
}

// The reference is the speaker feed one block plus reference_delay late.
TEST(TwoMicLoop, ReferenceLagsTheSpeakerByBlockPlusDelay) {
    using loop_t = mutap_test::multi_mic_loop<double>;
    loop_t::config lc;
    lc.paths            = {std::vector<double>(8, 0.0)}; // no acoustic path
    lc.electrical_delay = 64;
    lc.reference_delay  = 100;
    std::vector<double> aux(4096);
    for (size_t i = 0; i < aux.size(); ++i) {
        aux[i] = static_cast<double>(i + 1);
    }
    lc.aux = &aux;
    loop_t                    loop(lc);
    const std::vector<double> silent(tmic::k_block, 0.0);
    for (size_t blk = 0; blk < 10; ++blk) {
        const double* x[] = {silent.data()};
        loop.step(x, static_cast<std::vector<tmic::afc<double>>*>(nullptr));
        for (size_t i = 0; i < tmic::k_block; ++i) {
            const long long n = static_cast<long long>(blk * tmic::k_block + i) - 64 - 100;
            EXPECT_EQ(loop.reference_block()[i], n >= 0 ? aux[static_cast<size_t>(n)] : 0.0);
        }
    }
}

// The dry two-mic loop's limit is the summed path's: bisected on 10 s of
// the unison pair (no leakage), it sits at exact_msg_db(F_0 + F_1, d) to
// within the bisection's reach. Measured (0.1 dB bisection, seed 2):
// cabin +0.08 / +0.16, mt5+mt9 +0.08 / +0.16 dB at S1 / S3.
TEST(TwoMicLoop, DryTwoMicLimitIsTheSummedPaths) {
    struct row {
        const char* pair  = "";
        size_t      delay = 0;
        double      open  = 0.0;
        double      exact = 0.0;
    };
    std::vector<row> rows = {
        {"cabin", tmic::k_s1}, {"cabin", tmic::k_s3}, {"mt5+mt9", tmic::k_s1}, {"mt5+mt9", tmic::k_s3}};
    parallel_for(rows.size(), [&rows](size_t i) {
        row&            r = rows[i];
        tmic::condition c;
        c.delay       = r.delay;
        c.leak_db     = -300.0;
        const auto sc = tmic::make_scenario<double>(tmic::room_pair(r.pair), c, 2, 1, tmic::probe_blocks(10.0));
        r.open        = tmic::open_limit_db(sc, tmic::protocol{});
        r.exact       = tmic::exact_db(sc);
    });
    for (const auto& r : rows) {
        std::printf("%-8s d=%zu  dry two-mic %+.2f  exact %+.2f  (%+.2f)\n", r.pair, r.delay, r.open, r.exact,
                    r.open - r.exact);
        EXPECT_NEAR(r.open, r.exact, 1.0) << r.pair << " d=" << r.delay << ": measured within +0.16";
    }
}

namespace {

    struct smoke_run {
        double unc_db[2] = {0.0, 0.0}; ///< uncertainty_ratio per mic, dB
        double mis_db[2] = {0.0, 0.0}; ///< misalignment against F_m delayed by the margin, dB
        double peak_rms  = 0.0;        ///< largest bus block RMS
        bool   finite    = true;
    };

    /// Float, S1, mt5+mt9, the aligned shared reference, -10 dB
    /// cross-leakage, the bus loop at exact_msg_db(F_0 + F_1) - 6 for
    /// `blocks` blocks from cold.
    smoke_run run_smoke(tmic::singers who, unsigned seed, size_t blocks) {
        const auto      paths = tmic::room_pair("mt5+mt9");
        tmic::condition c;
        c.who               = who;
        c.leak_db           = -10.0;
        const auto sc       = tmic::make_scenario<float>(paths, c, seed, blocks, 1);
        auto       cfg      = tmic::loop_config(sc, false);
        cfg.forward_gain_db = tmic::exact_db(sc) - 6.0;
        mutap_test::multi_mic_loop<float> loop(cfg);
        std::vector<tmic::afc<float>>     afc(2, tmic::afc<float>(tmic::afc_config<float>()));
        smoke_run                         r;
        for (size_t blk = 0; blk < blocks; ++blk) {
            const float* x[] = {&sc.conv[0][blk * tmic::k_block], &sc.conv[1][blk * tmic::k_block]};
            const double rms = loop.step(x, &afc);
            r.finite         = r.finite && std::isfinite(rms);
            r.peak_rms       = std::max(r.peak_rms, rms);
        }
        std::vector<float> ir(afc[0].filter_length());
        for (size_t m = 0; m < 2; ++m) {
            r.unc_db[m] = 10.0 * std::log10(static_cast<double>(afc[m].uncertainty_ratio()));
            afc[m].copy_impulse_response(ir.data());
            r.mis_db[m] = misalignment_db(sc.paths[m], tmic::k_margin, ir);
        }
        return r;
    }

} // namespace

// SMOKE: two mics, one shared reference, -10 dB cross-leakage, from cold at
// the bus loop's exact_msg_db - 6: both cancellers converge and the bus
// stays finite, for the speech pair and for the unison pair (the worst
// case: the two held notes are phase-locked pulse trains). Five seed sets,
// 1500 blocks (2 s), float, mt5+mt9. Measured on the Intel Mac, per mic,
// over the five sets:
//   speech   uncertainty -17.98 .. -17.67 dB (medians -17.80 / -17.89),
//            misalignment -5.99 .. -4.50 dB (medians -4.58 / -5.69),
//            peak bus RMS <= 3.78
//   unison   uncertainty -13.65 .. -10.34 dB (medians -10.98 / -12.89),
//            peak bus RMS <= 3.14; misalignment +8.36 .. +14.89 dB, NOT
//            asserted: after a held note the estimate carries the
//            closed-loop bias (afc_chain's readback note), not the room
TEST(TwoMicSmoke, BothMicsConvergeOnTheSharedReference) {
    constexpr unsigned k_sets = mutap_test::k_claim_seed_sets;
    for (const tmic::singers who : {tmic::singers::speech, tmic::singers::unison}) {
        std::vector<smoke_run> runs(k_sets);
        parallel_for(k_sets, [&runs, who](size_t set) {
            runs[set] = run_smoke(who, seed_in_set(2, static_cast<unsigned>(set)), 1500);
        });
        std::vector<double> unc[2];
        std::vector<double> mis[2];
        for (unsigned set = 0; set < k_sets; ++set) {
            const auto& r = runs[set];
            std::printf("%-7s set %u  unc %+.2f %+.2f dB  mis %+.2f %+.2f dB  peak bus rms %.2f\n", tmic::name(who),
                        set, r.unc_db[0], r.unc_db[1], r.mis_db[0], r.mis_db[1], r.peak_rms);
            EXPECT_TRUE(r.finite) << tmic::name(who) << " set " << set;
            EXPECT_LT(r.peak_rms, 20.0) << tmic::name(who) << " set " << set << ": measured <= 3.78";
            for (size_t m = 0; m < 2; ++m) {
                unc[m].push_back(r.unc_db[m]);
                mis[m].push_back(r.mis_db[m]);
            }
        }
        for (size_t m = 0; m < 2; ++m) {
            if (who == tmic::singers::speech) {
                EXPECT_LT(median(unc[m]), -14.0) << "speech mic " << m << ": measured <= -17.80";
                EXPECT_LT(median(mis[m]), -3.0) << "speech mic " << m << ": measured <= -4.58";
            }
            else {
                EXPECT_LT(median(unc[m]), -7.0) << "unison mic " << m << ": measured <= -10.98";
            }
        }
    }
}
