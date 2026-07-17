// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Float32 parity gates: the deployment-precision correctness oracle for
// the embedded targets (Cortex-M55 / Hexagon run the same float32 core
// as the desktop golden model). The certified compliance battery is
// double precision; these gates pin what chain<float> at the SAME
// certified preset measures on the headline rows, so performance work
// in float32 has a behavioral baseline that CI enforces.
//
// Measured (s9 scratch series; double reference in parentheses):
//
//   single talk, CSS -16 dBm0, last-third max, dBm0(A):
//     48 kHz cabin -75.1 (-74.4)   studio -85.0 (-84.9)
//     16 kHz cabin -80.9 (-80.9)   studio -97.1 (-97.4)
//   cold-start convergence ERL, dB by 0.6 / 1.2 s:
//     48 kHz 33.6 / 47.8 (same)    16 kHz 34.1 / 45.4 (same)
//   AM-FM double talk, settled near-end level change through the chain:
//     48 kHz -0.5 dB               16 kHz -1.8 dB (comfort-noise add)
//   leak, 45 s silence (16 kHz): -71.6 -> -89.4 (improves, no leak)
//   30 s 1 kHz on-bin tone, max after 10 s (G.168-derived gate -49.3):
//     48 kHz -146.6 (-231.1)       16 kHz -64.8 (-94.8)
//
// The tone row is the one place float32 needed a design response, not
// just a measurement: the gradient constraint's per-block projection
// churns the partition-redistribution null space of an on-bin tone into
// a noise-driven weight walk whose equilibrium scales with rounding
// (float32 read -20 dBm0(A) against the -49.3 gate; even double pays
// -101 vs -233 unconstrained, passing on margin alone). The float32
// preset therefore enables the core's narrowband guard — the classical
// tone-disabler discipline; fd_kalman.h's config comment carries the
// mechanism and the measured separation (fires on tones and DTMF, never
// on CSS, the AM-FM combs, or noise). The double preset keeps the guard
// off: the certified battery stays bit-identical.

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/fd_kalman.h"
#include "mutap/postfilter.h"
#include "support/echo_scenario.h"
#include "support/itu_chain.h"
#include "support/itu_levels.h"
#include "support/itu_signals.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::itu;

    /// The harness stays double; conversion happens at the chain
    /// boundary (input quantization to float32 is benign next to
    /// internal state precision).
    struct f32_chain {
        mutap::aec_chain<float> chain;
        std::vector<float>      xb, yb, eb;
        explicit f32_chain(const rate_setup& rs)
            : chain(mutap::aec_chain_preset<float>(rs.block, rs.taps / rs.block, rs.fs))
            , xb(rs.block)
            , yb(rs.block)
            , eb(rs.block) {}
        void process_block(const double* x, const double* y, double* e) noexcept {
            for (size_t i = 0; i < xb.size(); ++i) {
                xb[i] = static_cast<float>(x[i]);
                yb[i] = static_cast<float>(y[i]);
            }
            chain.process_block(xb.data(), yb.data(), eb.data());
            for (size_t i = 0; i < xb.size(); ++i) {
                e[i] = static_cast<double>(eb[i]);
            }
        }
    };

    std::vector<double> css_at(const rate_setup& rs, size_t periods) {
        css_config cc;
        cc.periods = periods;
        cc.shaped  = true;
        auto x     = make_css_at(cc, rs.fs);
        set_level_dbm0(x, -16.0);
        return x;
    }

    TEST(Float32Parity, SingleTalkResidual) {
        struct row {
            rate_setup rs;
            room       rm;
            double     gate; // measured-with-margin, dBm0(A)
        };
        const row rows[] = {
            {setup_48k(), room::cabin, -70.0},  // measured -75.1
            {setup_48k(), room::studio, -79.0}, // measured -85.0
            {setup_16k(), room::cabin, -75.0},  // measured -80.9
            {setup_16k(), room::studio, -90.0}, // measured -97.1
        };
        for (const auto& r : rows) {
            auto         path = compliance_path(r.rm, r.rs);
            auto         x    = css_at(r.rs, 30);
            f32_chain    c(r.rs);
            auto         rr  = run_chain(c, path, r.rs.block, x);
            const double res = max_level_dbm0a(rr.out, r.rs.fs, rr.out.size() * 2 / 3, rr.out.size());
            EXPECT_LT(res, r.gate) << "fs " << r.rs.fs << (r.rm == room::cabin ? " cabin" : " studio");
        }
    }

    TEST(Float32Parity, Convergence) {
        for (const auto& rs : required_rates()) {
            auto       path = compliance_path(room::cabin, rs);
            auto       x    = css_at(rs, 16);
            f32_chain  c(rs);
            auto       rr = run_chain(c, path, rs.block, x);
            erl_reader erl(rr.echo, rr.out, rs.fs);
            EXPECT_GE(erl.by(0.6), 30.0) << "fs " << rs.fs; // measured 33.6 / 34.1
            EXPECT_GE(erl.by(1.2), 40.0) << "fs " << rs.fs; // measured 47.8 / 45.4
        }
    }

    TEST(Float32Parity, DoubleTalkTransparency) {
        for (const auto& rs : required_rates()) {
            auto         path = compliance_path(room::cabin, rs);
            auto         xc   = css_at(rs, 12);
            const size_t n_dt = static_cast<size_t>(8.0 * rs.fs);
            auto         xa   = make_amfm(amfm_receive_plan(), n_dt, rs.fs);
            set_level_dbm0(xa, -16.0);
            auto ns = make_amfm(amfm_send_plan(), n_dt, rs.fs);
            set_level_dbm0(ns, -16.0);
            std::vector<double> x(xc);
            x.insert(x.end(), xa.begin(), xa.end());
            std::vector<double> v(xc.size(), 0.0);
            v.insert(v.end(), ns.begin(), ns.end());
            f32_chain    c(rs);
            auto         rr     = run_chain(c, path, rs.block, x, &v);
            const double out_dt = max_level_dbm0a(rr.out, rs.fs, xc.size() + n_dt / 2, xc.size() + n_dt);
            const double in_dt  = max_level_dbm0a(v, rs.fs, xc.size() + n_dt / 2, xc.size() + n_dt);
            EXPECT_LT(std::abs(in_dt - out_dt), 3.0) << "fs " << rs.fs; // measured -0.5 / -1.8
        }
    }

    // The G.168-derived tone gate the double battery meets at -231/-95:
    // float32 needs the narrowband guard to meet it (without: -147/-33).
    TEST(Float32Parity, ToneRowWithNarrowbandGuard) {
        struct row {
            rate_setup rs;
            double     gate;
        };
        const row rows[] = {
            {setup_48k(), -100.0}, // measured -146.6
            {setup_16k(), -55.0},  // measured -64.8 (requirement -49.3)
        };
        for (const auto& r : rows) {
            auto                path = compliance_path(room::cabin, r.rs);
            const size_t        n    = static_cast<size_t>(30.0 * r.rs.fs);
            std::vector<double> x(n);
            for (size_t i = 0; i < n; ++i) {
                x[i] = std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / r.rs.fs);
            }
            set_level_dbm0(x, -16.0);
            f32_chain c(r.rs);
            auto      rr = run_chain(c, path, r.rs.block, x);
            EXPECT_LT(max_level_dbm0a(rr.out, r.rs.fs, static_cast<size_t>(10.0 * r.rs.fs), rr.out.size()), r.gate)
                << "fs " << r.rs.fs;
        }
    }

    TEST(Float32Parity, NoLeakOverSilence) {
        const auto          rs   = setup_16k();
        auto                path = compliance_path(room::cabin, rs);
        auto                x    = css_at(rs, 12);
        std::vector<double> x2   = x;
        x2.insert(x2.end(), static_cast<size_t>(45.0 * rs.fs), 0.0);
        x2.insert(x2.end(), x.begin(), x.end());
        f32_chain    c(rs);
        auto         rr     = run_chain(c, path, rs.block, x2);
        const double before = max_level_dbm0a(rr.out, rs.fs, x.size() * 2 / 3, x.size());
        const size_t a0     = x.size() + static_cast<size_t>(45.0 * rs.fs);
        const double after =
            max_level_dbm0a(rr.out, rs.fs, a0 + x.size() * 2 / 3, std::min(rr.out.size(), a0 + x.size()));
        EXPECT_LE(after, before + 5.0); // G.168 leak rule; measured -71.6 -> -89.4
    }

    // Preset policy: the guard is a float32 deployment decision — the
    // certified double preset stays guard-off (bit-identical battery).
    TEST(Float32Parity, NarrowbandGuardPolicy) {
        const auto f = mutap::aec_chain_preset<float>(256, 4, 16000.0).canceller;
        EXPECT_GT(f.narrowband_guard, 0.0F);
        EXPECT_GE(f.narrowband_hold_blocks, size_t{8});
        const auto d48 = mutap::aec_chain_preset<double>(256, 8, 48000.0).canceller;
        EXPECT_EQ(d48.narrowband_guard, 0.0);
        const auto d16 = mutap::aec_chain_preset<double>(256, 4, 16000.0).canceller;
        EXPECT_EQ(d16.narrowband_guard, 0.0);

        // The guard engages on a sustained tone and reports it.
        {
            const auto          rs   = setup_16k();
            auto                path = compliance_path(room::cabin, rs);
            const size_t        n    = static_cast<size_t>(5.0 * rs.fs);
            std::vector<double> x(n);
            for (size_t i = 0; i < n; ++i) {
                x[i] = std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / rs.fs);
            }
            set_level_dbm0(x, -16.0);
            struct raw_f32 {
                mutap::partitioned_fdkf<float> core;
                std::vector<float>             xb, yb, eb;
                explicit raw_f32(const mutap::partitioned_fdkf<float>::config& c)
                    : core(c)
                    , xb(c.block_size)
                    , yb(c.block_size)
                    , eb(c.block_size) {}
                void process_block(const double* x, const double* y, double* e) noexcept {
                    for (size_t i = 0; i < xb.size(); ++i) {
                        xb[i] = static_cast<float>(x[i]);
                        yb[i] = static_cast<float>(y[i]);
                    }
                    core.process_block(xb.data(), yb.data(), eb.data());
                    for (size_t i = 0; i < xb.size(); ++i) {
                        e[i] = static_cast<double>(eb[i]);
                    }
                }
            };
            raw_f32 b(mutap::aec_chain_preset<float>(rs.block, rs.taps / rs.block, rs.fs).canceller);
            EXPECT_FALSE(b.core.narrowband_frozen());
            (void)run_chain(b, path, rs.block, x);
            EXPECT_TRUE(b.core.narrowband_frozen());
        }

        // knob validation
        using fdkf = mutap::partitioned_fdkf<float>;
        fdkf::config good;
        {
            auto c             = good;
            c.narrowband_guard = 1.5F;
            EXPECT_THROW(fdkf{c}, std::invalid_argument);
        }
        {
            auto c                   = good;
            c.narrowband_guard       = 0.8F;
            c.narrowband_hold_blocks = 0;
            EXPECT_THROW(fdkf{c}, std::invalid_argument);
        }
    }

} // namespace
