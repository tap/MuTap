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
// Measured (block 64, 16 partitions over the first 1024 taps, band-limited
// through the loudspeaker model (support/rooms.h), converge 3000 blocks at
// MSG-6 on speech-envelope material; ASG over max|F|, medians over seed
// sets 0..4; the NLMS column for rehearsal and hall from the same scratch
// sweep, the test asserts studio only):
//
//   room       max|F| MSG   exact MSG   Kalman ASG   NLMS ASG
//   studio      -7.00 dB    -6.64 dB     +19.38 dB    +11.88 dB
//   rehearsal   -7.92 dB    -5.30 dB     +18.44 dB    +11.88 dB
//   hall        -6.91 dB    -6.36 dB     +19.38 dB    +11.88 dB
//
// Thresholds sit well inside those numbers so they gate regressions.

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_hall.h"
#include "fixtures/rir_rehearsal.h"
#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/rooms.h"

namespace {

    using mutap_test::closed_loop_sim;
    using mutap_test::k_claim_seed_sets;
    using mutap_test::median;
    using mutap_test::seed_in_set;

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 1024; ///< first 21 ms: direct + early reflections
    constexpr size_t k_parts = k_taps / k_block;

    struct room {
        const char*  name;
        const float* rir;
        size_t       full_taps;
    };

    const room k_rooms[] = {
        {"studio", mutap_test::fixtures::k_rir_studio, mutap_test::fixtures::k_rir_studio_taps},
        {"rehearsal", mutap_test::fixtures::k_rir_rehearsal, mutap_test::fixtures::k_rir_rehearsal_taps},
        {"hall", mutap_test::fixtures::k_rir_hall, mutap_test::fixtures::k_rir_hall_taps},
    };

    // The tests model the first k_taps of the room (a practical canceller
    // length); the truncation is re-normalized to unit energy so gain
    // numbers stay comparable across rooms and with the synthetic tests,
    // then band-limited by the loudspeaker model (not re-normalized). The
    // model is causal, so this equals band-limiting the full fixture and
    // keeping its first k_taps, up to the normalization's scale.
    std::vector<double> loop_path(const room& r) {
        std::vector<double> f(r.rir, r.rir + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return mutap_test::band_limited(f);
    }

    template <typename Pem>
    double converge_and_measure_asg(const std::vector<double>& path, unsigned set) {
        const double open_msg = mutap_test::theoretical_msg_db(path);

        typename closed_loop_sim<double>::config lc;
        lc.feedback_path = path;
        lc.block_size    = k_block;
        lc.forward_delay = 2 * k_block;

        typename Pem::config pc;
        pc.fdaf.block_size = k_block;
        pc.fdaf.partitions = k_parts;
        Pem pem(pc);

        const auto v_converge = mutap_test::ar_near_end<double>(3000 * k_block, seed_in_set(2, set));
        const auto v_probe    = mutap_test::ar_near_end<double>(600 * k_block, seed_in_set(12, set));

        auto converge_cfg            = lc;
        converge_cfg.forward_gain_db = open_msg - 6.0;
        closed_loop_sim<double> sim(converge_cfg);
        for (size_t blk = 0; blk < 3000; ++blk) {
            sim.step(&v_converge[blk * k_block], &pem);
        }
        return mutap_test::measured_msg_db(lc, &pem, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5) - open_msg;
    }

    template <typename Pem>
    double median_asg(const room& r) {
        const auto          path = loop_path(r);
        std::vector<double> asg;
        for (unsigned set = 0; set < k_claim_seed_sets; ++set) {
            asg.push_back(converge_and_measure_asg<Pem>(path, set));
        }
        return median(asg);
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
    // show (measured medians +18.44..+19.38 dB across the three rooms, per
    // set +18.44..+20.62).
    TEST(RirFixtures, KalmanPemAddsStableGainOnModeledRooms) {
        using pem = tap::mu::pem_afc<double, tap::mu::speech_predictor<double>, tap::mu::partitioned_fdkf<double>>;
        for (const auto& r : k_rooms) {
            const double med = median_asg<pem>(r);
            RecordProperty(std::string("median_asg_db_") + r.name, med);
            EXPECT_GT(med, 14.0) << r.name << " (measured medians >= +18.44 dB)";
        }
    }

    // And the classic engine's reference point on one room (measured median
    // +11.88 dB, per set +10.31..+12.19) — the gap between these two tests
    // is the v2 story told on realistic acoustics.
    TEST(RirFixtures, NlmsPemAddsStableGainOnStudio) {
        const double med = median_asg<tap::mu::pem_afc<double>>(k_rooms[0]);
        RecordProperty("median_asg_db", med);
        EXPECT_GT(med, 6.0) << "measured median +11.88 dB";
    }

} // namespace
