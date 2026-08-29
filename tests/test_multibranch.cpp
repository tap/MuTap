// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The multi-branch (nonlinear-basis, Hammerstein-MISO) canceller —
// Stage 2 of docs/multibranch-canceller.md: contract/validation, the
// basis bake-off verdict, and the winner's characterization, all
// against the outdoor close-range scenario (the baseline these rows
// move is tests/test_outdoor.cpp NonlinearDrive: raw linear 25.6/15.6/
// 10.2 dB at mild/moderate/severe, 48 kHz). Gates pin measured values
// with margin; the measured number sits beside every assertion.
//
// THE BAKE-OFF RECORD (raw canceller, outdoor path erl -20, CSS at
// -10 dBm0, suppression dB from 4 s; drives: none / tanh mild 1.1 /
// moderate 2.5 / severe 5.0 / hard clip 0.32 — the OFF-MODEL drive the
// tanh-family basis cannot match by construction):
//
//   48 kHz          none  mild  mod   sev   hclip
//   lin             50.2  25.6  15.6  10.2  12.4
//   p3 (plain)      34.9  30.3  24.9  17.0  21.6   round 1
//   p3p5 (plain)    35.1  31.4  23.6  16.3  20.6   round 1
//   p3orth          47.7  44.7  27.9  17.4  23.1
//   clipo           47.7  35.7  29.2  19.4  32.3
//   tanho (g=2.5)   47.3  38.8  49.9  22.6  25.0
//   p3o+p5o (no GS) 39.0  38.6  30.2  20.6  24.4   round 2
//   p3o+clipo (")   39.5  38.9  31.6  19.6  33.0   round 2
//   p3o+p5oGS       44.6  46.4  39.7  24.6  26.8   <- WINNER
//   p3o+clipoGS     44.8  45.2  31.9  19.9  33.7
//
//   16 kHz          none  mild  mod   sev   hclip
//   lin             53.7  25.9  15.9  10.6  12.8
//   p3orth          39.2  39.5  28.6  18.4  24.5
//   clipo           39.3  35.6  29.9  20.3  32.5
//   tanho           39.0  37.2  42.0  23.5  26.0
//   p3o+p5oGS       35.6  35.8  36.2  26.2  27.8
//   p3o+clipoGS     35.4  35.5  31.7  21.2  33.1
//
// What the table decided (full narrative in the doc's Stage 2 section):
//  1. ORTHOGONALIZATION IS THE LEVER (doc §5.1 confirmed): plain x^3
//     pays 15 dB of clean-drive misadjustment; LS-centering against x
//     restores it and buys +19 dB at mild. Extended to every kind, and
//     to branch PAIRS via the Gram-Schmidt chain coefficient (round 2's
//     pairs paid ~8 dB against each other; GS recovered ~5.5).
//  2. WINNER {x^3 orth, x^5 orth GS}: best or near-best in every
//     distortion column at 48 kHz (+20.8 mild, +24.1 moderate, +14.4
//     severe and off-model over linear), 5.6 dB clean cost — erased by
//     the novelty discount below. tanho at its exact knee reaches 49.9
//     (the exactly-representable theory): the per-device preset when a
//     measured curve exists. clipo stays the hard-limiter pick (32+ on
//     hclip at both rates).
//  3. THE 16 kHz CLEAN COST IS THE COMB FAMILY (§5.1's prediction, new
//     axis): EVERY branched config pays ~14 dB on the 16 kHz clean row.
//     The novelty discount (0.8/0.1, the block-128-notch counter-
//     measure) recovers it to full at 48 kHz (44.6 -> 52.8 clean, the
//     linear core's own row is 50.2) and by +6 at 16 kHz (35.6 -> 41.5,
//     moderate 36.2 -> 39.6); the remaining 16 kHz gap (~12 dB vs the
//     linear core's exceptional 53.7) is the recorded cost of branches
//     at that rate — future work, not a blocker: every DISTORTION row
//     still improves 2..24 dB, and clean-drive outdoor rigs are what
//     the single-branch or branchless presets are for.
//  4. BRANCH PRIOR: moderate-drive suppression is insensitive across
//     1.0..0.03 (39.4..39.8 at 48 kHz); the clean row prefers small
//     (42.5 -> 45.4). Default 0.1 held (the Psi_s race of §5.2 argues
//     against going minimal on a sweep this benign).
//  5. DT SAFETY (§5.3): permanent double talk at moderate drive, near
//     end 40 dB under the echo — linear 15.6/15.9 dB, winner 38.3/35.1:
//     the model-based DT immunity holds on the branch axis, and the
//     winner's DT row nearly matches its single-talk row.
//
// The Stage 2/3 batteries run the double golden model; Stage 4's rows
// at the bottom of this file cover deployment precision — float32
// parity within 0.04 dB and the §5.1 tone-walk re-check, both gated.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "support/itu_chain.h"
#include "support/outdoor_scenario.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::outdoor;
    using mutap_test::itu::chain_config;
    using mutap_test::itu::rate_setup;
    using tap::mu::partitioned_fdkf;

    using kf_config = partitioned_fdkf<double>::config;
    using kf_branch = kf_config::branch;

    constexpr double k_level    = -10.0; ///< scenario far-end operating level
    constexpr double k_clip_off = 0.32;  ///< off-model hard-clip knee (clips CSS crests;
                                         ///< collapses the linear core to 12.4/12.8 dB)

    std::vector<double> far_end(const rate_setup& rs, double seconds) {
        itu::css_config cc;
        cc.periods = static_cast<size_t>(seconds / 0.35);
        cc.shaped  = true;
        auto x     = itu::make_css_at(cc, rs.fs);
        itu::set_level_dbm0(x, k_level);
        return x;
    }

    echo_sim<double> make_sim(const std::vector<double>& path, size_t block) {
        echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = block;
        return echo_sim<double>(sc);
    }

    /// Per-(rate, sweep-index) gate table.
    double at(const rate_setup& rs, std::initializer_list<double> at48, std::initializer_list<double> at16, size_t i) {
        return rs.fs == 48000.0 ? *(at48.begin() + static_cast<long>(i)) : *(at16.begin() + static_cast<long>(i));
    }

    // --------------------------------------------------- branch builders
    //
    // Calibration replicates the core's evaluation order exactly
    // (kf_branch::eval applies gain * (raw - center * x); the chain
    // subtraction happens on the previous branch's FINAL signal):
    // center = E[x phi]/E[x^2] over the material, gain normalizes the
    // centered signal, chain is the residual correlation between
    // finished branch signals.

    template <typename Phi>
    std::pair<double, double> center_and_gain(const std::vector<double>& x, Phi&& phi) {
        double s2  = 0.0;
        double sxp = 0.0;
        for (const double v : x) {
            s2 += v * v;
            sxp += v * phi(v);
        }
        const double c  = sxp / s2;
        double       sq = 0.0;
        for (const double v : x) {
            const double p = phi(v) - c * v;
            sq += p * p;
        }
        return {c, 1.0 / std::sqrt(sq / static_cast<double>(x.size()))};
    }

    kf_branch pow_branch(const std::vector<double>& x, double p) {
        kf_branch b;
        b.kind            = kf_branch::basis::odd_power;
        b.power           = p;
        const auto [c, g] = center_and_gain(x, [&](double v) {
            double y = v;
            for (int i = 1; i < static_cast<int>(p); ++i) {
                y *= v;
            }
            return y;
        });
        b.center          = c;
        b.gain            = g;
        b.partitions      = 2;
        return b;
    }

    kf_branch clip_branch(const std::vector<double>& x, double knee) {
        kf_branch b;
        b.kind            = kf_branch::basis::clip_difference;
        b.knee            = knee;
        const auto [c, g] = center_and_gain(x, [&](double v) { return v - std::clamp(v, -knee, knee); });
        b.center          = c;
        b.gain            = g;
        b.partitions      = 2;
        return b;
    }

    kf_branch gs_chain(const std::vector<double>& x, const kf_branch& first, kf_branch second) {
        double num = 0.0;
        double den = 0.0;
        for (const double v : x) {
            const double e1 = first.eval(v);
            num += second.eval(v) * e1;
            den += e1 * e1;
        }
        second.chain = num / den;
        return second;
    }

    /// The bake-off winner: {x^3 orth, x^5 orth GS-chained}.
    std::vector<kf_branch> winner_branches(const std::vector<double>& x) {
        const kf_branch b3 = pow_branch(x, 3.0);
        return {b3, gs_chain(x, b3, pow_branch(x, 5.0))};
    }

    // ------------------------------------------------------- contract

    TEST(MultibranchConfigValidation, RejectsBadBranches) {
        kf_config base;
        auto      expect_throw = [&](kf_branch br) {
            kf_config c = base;
            c.branches.push_back(br);
            EXPECT_THROW(partitioned_fdkf<double>{c}, std::invalid_argument);
        };
        kf_branch b;
        b.power = 2.0; // even
        expect_throw(b);
        b.power = 3.5; // non-integer
        expect_throw(b);
        b      = kf_branch{};
        b.kind = kf_branch::basis::clip_difference;
        b.knee = 0.0;
        expect_throw(b);
        b      = kf_branch{};
        b.gain = 0.0;
        expect_throw(b);
        b       = kf_branch{};
        b.prior = 0.0;
        expect_throw(b);
        b.prior = 1.5;
        expect_throw(b);

        kf_config ok;
        ok.branches.push_back(kf_branch{});
        EXPECT_NO_THROW(partitioned_fdkf<double>{ok});
    }

    TEST(MultibranchRtContract, PostConstructionEntryPointsAreNoexcept) {
        using kf = partitioned_fdkf<float>;
        static_assert(noexcept(std::declval<kf&>().process_block(nullptr, nullptr, nullptr)));
        static_assert(noexcept(std::declval<kf&>().reinflate_uncertainty()));
        static_assert(noexcept(std::declval<kf&>().reset()));
        static_assert(noexcept(std::declval<kf&>().branch_count()));
        static_assert(noexcept(std::declval<const kf_branch&>().eval(0.0f)));
        SUCCEED();
    }

    // On-model identification: white noise through the moderate tanh
    // Hammerstein path; a tanh_difference branch at the exact knee makes
    // the true system exactly representable. Measured: linear 16.6 dB,
    // multibranch 39.4 — the +22.8 dB that proves the structure.
    TEST(Multibranch, OnModelIdentification) {
        const rate_setup rs   = itu::setup_48k();
        const auto       path = make_outdoor_path(rs.fs, rs.taps, -20.0);

        std::mt19937                     gen(41);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              x(static_cast<size_t>(6.0 * rs.fs));
        for (auto& v : x) {
            v = dist(gen);
        }
        itu::set_level_dbm0(x, k_level);
        speaker_drive sp{k_drive_moderate};
        const auto    xd = sp.apply(x);

        auto run = [&](const kf_config& cfg) {
            partitioned_fdkf<double> core(cfg);
            auto                     sim = make_sim(path, rs.block);
            return run_outdoor(sim, &core, x, xd, nullptr, static_cast<size_t>(3.0 * rs.fs) / rs.block).suppression_db;
        };

        const kf_config lin = chain_config<double>(rs).canceller;
        kf_config       mb  = lin;
        kf_branch       br;
        br.kind = kf_branch::basis::tanh_difference;
        br.knee = k_drive_moderate;
        br.gain = branch_gain([&](double v) { return std::tanh(k_drive_moderate * v) / k_drive_moderate - v; }, k_level,
                              rs.fs);
        br.partitions = 2;
        mb.branches.push_back(br);

        const double s_lin = run(lin);
        const double s_mb  = run(mb);
        EXPECT_GT(s_mb, s_lin + 18.0); // measured delta +22.8
    }

    // ------------------------------------------------------- the verdict
    //
    // The pinned battery runs the linear control, the two single-branch
    // finalists and the winner (the full ten-config table above was
    // measured once and lives in the banner; tanho and the non-GS pairs
    // are recorded, not re-run).
    TEST(Multibranch, BasisBakeoff) {
        for (const auto& rs : itu::required_rates()) {
            const auto path = make_outdoor_path(rs.fs, rs.taps, -20.0);
            const auto x    = far_end(rs, 8.4);

            struct base_spec {
                const char*            name;
                std::vector<kf_branch> branches;
            };
            std::vector<base_spec> bases;
            bases.push_back({"lin", {}});
            bases.push_back({"p3orth", {pow_branch(x, 3.0)}});
            bases.push_back({"clipo", {clip_branch(x, 0.30)}});
            bases.push_back({"winner", winner_branches(x)});

            struct drive_spec {
                const char*   name;
                speaker_drive sp;
            };
            const std::vector<drive_spec> drives = {{"none", {0.0, 0.0}},
                                                    {"mild", {k_drive_mild, 0.0}},
                                                    {"moderate", {k_drive_moderate, 0.0}},
                                                    {"severe", {k_drive_severe, 0.0}},
                                                    {"hardclip", {0.0, k_clip_off}}};

            // Measured values (banner table), gate = measured - 3.
            const std::initializer_list<double> gates[4][2] = {
                /* lin    */ {{47.2, 22.5, 12.5, 7.2, 9.4}, {50.7, 22.9, 12.9, 7.6, 9.8}},
                /* p3orth */ {{44.7, 41.7, 24.9, 14.4, 20.1}, {36.2, 36.5, 25.6, 15.4, 21.5}},
                /* clipo  */ {{44.7, 32.7, 26.2, 16.4, 29.3}, {36.3, 32.6, 26.9, 17.3, 29.5}},
                /* winner */ {{41.6, 43.4, 36.7, 21.6, 23.8}, {32.6, 32.8, 33.2, 23.2, 24.8}},
            };

            for (size_t bi = 0; bi < bases.size(); ++bi) {
                kf_config cfg = chain_config<double>(rs).canceller;
                cfg.branches  = bases[bi].branches;
                for (size_t di = 0; di < drives.size(); ++di) {
                    const auto               xd = drives[di].sp.apply(x);
                    partitioned_fdkf<double> core(cfg);
                    auto                     sim = make_sim(path, rs.block);
                    auto r = run_outdoor(sim, &core, x, xd, nullptr, static_cast<size_t>(4.0 * rs.fs) / rs.block);
                    ASSERT_TRUE(r.finite) << bases[bi].name << " " << drives[di].name;
                    const double gate = rs.fs == 48000.0 ? *(gates[bi][0].begin() + static_cast<long>(di))
                                                         : *(gates[bi][1].begin() + static_cast<long>(di));
                    EXPECT_GE(r.suppression_db, gate)
                        << "fs " << rs.fs << " " << bases[bi].name << " " << drives[di].name;
                }
            }
        }
    }

    // Winner characterization: the novelty-discount pairing and the
    // permanent-double-talk safety row. (The prior sweep is recorded in
    // the banner, point 4 — default 0.1 held.)
    TEST(Multibranch, WinnerCharacterization) {
        for (const auto& rs : itu::required_rates()) {
            const auto path = make_outdoor_path(rs.fs, rs.taps, -20.0);
            const auto x    = far_end(rs, 8.4);
            const auto br   = winner_branches(x);

            speaker_drive sp{k_drive_moderate};
            const auto    xd = sp.apply(x);

            // Winner + novelty discount (0.8/0.1): moderate / clean,
            // measured 40.0/52.8 at 48 kHz, 39.6/41.5 at 16 kHz.
            {
                kf_config cfg         = chain_config<double>(rs).canceller;
                cfg.branches          = br;
                cfg.novelty_smoothing = 0.8;
                cfg.novelty_floor     = 0.1;
                partitioned_fdkf<double> c_mod(cfg);
                auto                     sim_m = make_sim(path, rs.block);
                auto rm = run_outdoor(sim_m, &c_mod, x, xd, nullptr, static_cast<size_t>(4.0 * rs.fs) / rs.block);
                partitioned_fdkf<double> c_cln(cfg);
                auto                     sim_c = make_sim(path, rs.block);
                auto rc = run_outdoor(sim_c, &c_cln, x, x, nullptr, static_cast<size_t>(4.0 * rs.fs) / rs.block);
                ASSERT_TRUE(rm.finite && rc.finite);
                EXPECT_GE(rm.suppression_db, rs.fs == 48000.0 ? 37.0 : 36.5) << "fs " << rs.fs;
                EXPECT_GE(rc.suppression_db, rs.fs == 48000.0 ? 49.5 : 38.5) << "fs " << rs.fs;
            }

            // Permanent double talk (near end CSS-DT at -30 dBm0, 40 dB
            // below the echo), moderate drive: linear 15.6/15.9 dB,
            // winner 38.3/35.1 — DT immunity holds on the branch axis.
            itu::css_config nc;
            nc.kind    = itu::css_kind::double_talk;
            nc.periods = static_cast<size_t>(8.4 / 0.4) + 1;
            nc.seed    = 977;
            auto v     = itu::make_css_at(nc, rs.fs);
            itu::set_level_dbm0(v, -30.0);
            v.resize(x.size(), 0.0);

            kf_config mb = chain_config<double>(rs).canceller;
            mb.branches  = br;
            partitioned_fdkf<double> c_mb(mb);
            auto                     sim_b = make_sim(path, rs.block);
            auto rb = run_outdoor(sim_b, &c_mb, x, xd, &v, static_cast<size_t>(4.0 * rs.fs) / rs.block);
            ASSERT_TRUE(rb.finite);
            EXPECT_GE(rb.suppression_db, rs.fs == 48000.0 ? 35.0 : 32.0) << "fs " << rs.fs;
        }
    }

    // ------------------------------------------------------- Stage 4
    //
    // Float32 is the deployment precision (M55/Hexagon have no double);
    // the branched chain must hold at it, and the §5.1 concern — the
    // constraint-churn weight walk with MORE null space — must be
    // re-checked on a sustained tone with branches ON (a tone through
    // x^3/x^5 is spectrally concentrated in every branch; the
    // narrowband guard reads branch 0 and its freeze covers all).

    // The outdoor chain at float32 vs the double golden model, same
    // scenario, boundary-quantized exactly like the compliance battery
    // (itu_chain.h compliance_dut).
    TEST(Multibranch, Float32ChainParity) {
        for (const auto& rs : itu::required_rates()) {
            const auto path = make_outdoor_path(rs.fs, rs.taps, -20.0);
            const auto x    = far_end(rs, 8.4);
            for (const double drive : {0.0, k_drive_mild, k_drive_moderate}) {
                speaker_drive sp{drive};
                const auto    xd = sp.apply(x);

                itu::compliance_dut<double> cd(tap::mu::aec_chain_outdoor_preset<double>(rs.block, rs.fs), rs.block);
                itu::compliance_dut<float>  cf(tap::mu::aec_chain_outdoor_preset<float>(rs.block, rs.fs), rs.block);
                auto                        sim_d = make_sim(path, rs.block);
                auto                        sim_f = make_sim(path, rs.block);
                auto rd = run_outdoor(sim_d, &cd, x, xd, nullptr, static_cast<size_t>(4.0 * rs.fs) / rs.block);
                auto rf = run_outdoor(sim_f, &cf, x, xd, nullptr, static_cast<size_t>(4.0 * rs.fs) / rs.block);
                ASSERT_TRUE(rd.finite && rf.finite) << "fs " << rs.fs << " drive " << drive;
                // Measured parity within 0.04 dB on every row (clean/mild/
                // moderate: 61.76/57.21/44.36 vs 61.76/57.18/44.36 at
                // 48 kHz; 66.63/54.07/43.29 vs 66.61/54.11/43.29 at
                // 16 kHz) — the short path and structure-limited
                // suppression leave float32 nothing to lose.
                EXPECT_NEAR(rf.suppression_db, rd.suppression_db, 0.5) << "fs " << rs.fs << " drive " << drive;
            }
        }
    }

    // 30 s on-bin 1 kHz tone at the operating level through the float32
    // outdoor chain (linear drive: the G.168 SS7 discipline; the
    // narrowband guard is float32-preset-enabled and must contain the
    // branch-augmented weight walk).
    TEST(Multibranch, Float32ToneGuard) {
        for (const auto& rs : itu::required_rates()) {
            const auto path = make_outdoor_path(rs.fs, rs.taps, -20.0);

            const size_t n    = static_cast<size_t>(30.0 * rs.fs);
            const size_t nfft = 8192;
            const double f    = std::round(1000.0 * static_cast<double>(nfft) / rs.fs) * rs.fs
                             / static_cast<double>(nfft); // on-bin at the analysis size
            std::vector<double> x(n);
            for (size_t i = 0; i < n; ++i) {
                x[i] = std::sin(2.0 * std::numbers::pi * f * static_cast<double>(i) / rs.fs);
            }
            itu::set_level_dbm0(x, k_level);

            itu::compliance_dut<float> cf(tap::mu::aec_chain_outdoor_preset<float>(rs.block, rs.fs), rs.block);
            auto                       sim = make_sim(path, rs.block);
            auto rf = run_outdoor(sim, &cf, x, x, nullptr, static_cast<size_t>(10.0 * rs.fs) / rs.block);
            ASSERT_TRUE(rf.finite) << "fs " << rs.fs;
            // Measured: suppression 128.6 dB / residual -115.3 dBm0(A)
            // at 48 kHz, 57.4 / -42.8 at 16 kHz (the echo rides at
            // +10 dBm0 here — 52.8 dB down). No walk, no divergence:
            // the branch-augmented null space stays contained by the
            // guard + constraint discipline at deployment precision.
            const double tail =
                itu::max_level_dbm0a(rf.residual, rs.fs, static_cast<size_t>(20.0 * rs.fs), rf.residual.size());
            EXPECT_GE(rf.suppression_db, rs.fs == 48000.0 ? 100.0 : 53.0) << "fs " << rs.fs;
            EXPECT_LE(tail, rs.fs == 48000.0 ? -110.0 : -39.0) << "fs " << rs.fs;
        }
    }

} // namespace
