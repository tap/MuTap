// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The RIR fixtures: physically-modeled rooms (image-source method; see
// tools/fixtures/make_rir_fixtures.py for the geometry, conditioning and
// how to add measured rooms) used as feedback paths in the closed loop.
// Unlike the synthetic random-decay rooms used elsewhere, these have real
// early-reflection structure: a direct sound, discrete wall reflections at
// geometry-determined delays, and a dense decaying tail.
//
// Measured (block 64, 16 partitions over the first 1024 taps, converge
// 3000 blocks at MSG-6 on speech-envelope material):
//
//   room       MSG      Kalman ASG   NLMS ASG
//   studio     -7.7 dB    +17.8 dB     +9.4 dB
//   rehearsal  -8.0 dB    +18.8 dB     +9.7 dB
//   hall       -7.1 dB    +19.1 dB     +9.7 dB
//
// Thresholds sit well inside those numbers so they gate regressions.

#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_hall.h"
#include "fixtures/rir_rehearsal.h"
#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"

namespace {

    using mutap_test::closed_loop_sim;

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 1024; ///< first 21 ms: direct + early reflections
    constexpr size_t k_parts = k_taps / k_block;

    struct room {
        const char*  name;
        const float* rir;
        size_t       full_taps;
    };

    const room k_rooms[] = {
        {"studio", mutap_test::fixtures::rir_studio, mutap_test::fixtures::rir_studio_taps},
        {"rehearsal", mutap_test::fixtures::rir_rehearsal, mutap_test::fixtures::rir_rehearsal_taps},
        {"hall", mutap_test::fixtures::rir_hall, mutap_test::fixtures::rir_hall_taps},
    };

    // The tests model the first k_taps of the room (a practical canceller
    // length); the truncation is re-normalized to unit energy so gain
    // numbers stay comparable across rooms and with the synthetic tests.
    std::vector<double> truncated_path(const room& r) {
        std::vector<double> f(r.rir, r.rir + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return f;
    }

    template <typename Pem>
    double converge_and_measure_asg(const std::vector<double>& path) {
        const double open_msg = mutap_test::theoretical_msg_db(path);

        typename closed_loop_sim<double>::config lc;
        lc.feedback_path = path;
        lc.block_size    = k_block;
        lc.forward_delay = 2 * k_block;

        typename Pem::config pc;
        pc.fdaf.block_size = k_block;
        pc.fdaf.partitions = k_parts;
        Pem pem(pc);

        const auto v_converge = mutap_test::ar_near_end<double>(3000 * k_block, 2);
        const auto v_probe    = mutap_test::ar_near_end<double>(600 * k_block, 12);

        auto converge_cfg            = lc;
        converge_cfg.forward_gain_db = open_msg - 6.0;
        closed_loop_sim<double> sim(converge_cfg);
        for (size_t blk = 0; blk < 3000; ++blk) {
            sim.step(&v_converge[blk * k_block], &pem);
        }
        return mutap_test::measured_msg_db(lc, &pem, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5) - open_msg;
    }

    // The generator's contract: full-length, unit-energy, and the direct
    // sound inside the leading guard (the bulk delay was trimmed).
    TEST(RirFixtures, FixturesAreConditionedAsDocumented) {
        for (const auto& r : k_rooms) {
            EXPECT_EQ(r.full_taps, 4096U) << r.name;
            double energy = 0.0;
            double peak   = 0.0;
            size_t argmax = 0;
            for (size_t i = 0; i < r.full_taps; ++i) {
                const double v = static_cast<double>(r.rir[i]);
                energy += v * v;
                if (std::abs(v) > peak) {
                    peak   = std::abs(v);
                    argmax = i;
                }
            }
            EXPECT_NEAR(energy, 1.0, 1e-3) << r.name;
            EXPECT_LT(argmax, 64U) << r.name << ": direct sound should sit near the trimmed onset";
        }
    }

    // The headline: on rooms with REAL reflection structure, the Kalman
    // canceller holds the same large broadband gains the synthetic rooms
    // showed (measured +17.8..+19.1 dB across the three rooms).
    TEST(RirFixtures, KalmanPemAddsStableGainOnModeledRooms) {
        using pem = mutap::pem_afc<double, mutap::speech_predictor<double>, mutap::partitioned_fdkf<double>>;
        for (const auto& r : k_rooms) {
            EXPECT_GT(converge_and_measure_asg<pem>(truncated_path(r)), 10.0) << r.name << " (measured >= +17.8 dB)";
        }
    }

    // And the classic engine's reference point on one room (measured
    // +9.4 dB) — the gap between these two tests is the v2 story told on
    // realistic acoustics.
    TEST(RirFixtures, NlmsPemAddsStableGainOnStudio) {
        EXPECT_GT(converge_and_measure_asg<mutap::pem_afc<double>>(truncated_path(k_rooms[0])), 4.0)
            << "measured +9.4 dB";
    }

} // namespace
