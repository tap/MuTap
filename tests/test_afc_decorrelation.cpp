// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Forward-path decorrelation and auxiliary speaker excitation, measured in
// the car-cabin fixture: the two things a karaoke rig can put in the loop
// that the canceller does not control (support/decorrelated_loop.h).
//
// The case they exist for is the one test_pem_afc.cpp's material cannot
// reach: a SUSTAINED, pitched near-end (a held sung note) in a small cabin
// at low loop latency. There the loudspeaker signal is a near-perfect
// predictor of the near-end source, PEM's residual bias is largest, and
// every engine in this library sits at roughly zero added stable gain.
//
// Measured at this suite's own settings (cabin fixture, first 1024 taps,
// block 64, 16 partitions, forward delay 128 samples = 2.7 ms at 48 kHz,
// PEM + FD-Kalman with the speech cascade, converge 1500 blocks at MSG-6,
// held 300 Hz note, seeds 2 / 22 / 42; the tests run seed 2):
//
//   forward path                        ASG (seed 2)   all three seeds
//   plain                                  +0.6 dB     +0.6 / -1.6 / +3.4
//   5 Hz frequency shift                  +18.4 dB    +18.4 / +15.6 / +16.6
//   delay modulation, +-8 samples          +8.1 dB     +8.1 / +8.4 / +10.0
//   broadband aux feed at singer level    +19.7 dB    +19.7 / +19.1 / +18.1
//   5 Hz shift on the NAIVE core           +1.6 dB     +1.6 / -15.0 / -15.0
//
// (-15.0 dB is the probe floor: the loop howled at every gain probed. A
// longer 3000-block convergence and five seeds put the shifted Kalman median
// at +16.6 dB and the shifted naive core at -4.1 dB.)
//
// The last row is the point of the suite: decorrelation does not replace
// PEM prewhitening. They remove different terms, and the rig needs both.
// Thresholds sit well inside the measured values so they gate regressions.
//
// Host-only and double-only by design: this is a property of the algorithm,
// not of a target's arithmetic, and each row costs a gain bisection. The
// emulated selections (tests/bare_metal_main.cpp and the TEST_FILTER in
// tests/CMakeLists.txt) do not name this suite.

#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_cabin.h"
#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/decorrelated_loop.h"

namespace {

    using mutap_test::decorrelated_loop;
    using mutap_test::forward_mode;

    constexpr size_t   k_block    = 64;
    constexpr size_t   k_taps     = 1024; ///< first 21 ms: direct + early reflections
    constexpr size_t   k_parts    = k_taps / k_block;
    constexpr size_t   k_delay    = 128; ///< 2.7 ms at 48 kHz: the low-latency rig
    constexpr size_t   k_converge = 1500;
    constexpr size_t   k_probe    = 600;
    constexpr unsigned k_seed     = 2;

    using kalman_afc = tap::mu::pem_afc<double, tap::mu::speech_predictor<double>, tap::mu::partitioned_fdkf<double>>;

    /// The cabin fixture as a feedback path: the first k_taps, renormalized
    /// to unit energy so gain numbers stay comparable with the other suites.
    std::vector<double> cabin_path() {
        std::vector<double> f(mutap_test::fixtures::k_rir_cabin, mutap_test::fixtures::k_rir_cabin + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return f;
    }

    decorrelated_loop<double>::config loop_config(const std::vector<double>& path) {
        decorrelated_loop<double>::config cfg;
        cfg.feedback_path = path;
        cfg.block_size    = k_block;
        cfg.forward_delay = k_delay;
        return cfg;
    }

    template <typename Canceller>
    typename Canceller::config afc_config() {
        typename Canceller::config cfg;
        cfg.fdaf.block_size = k_block;
        cfg.fdaf.partitions = k_parts;
        return cfg;
    }

    /// Converge the canceller inside the loop at MSG-6 dB, then bisect the
    /// forward gain on fresh material. Returns added stable gain in dB.
    template <typename Canceller>
    double added_stable_gain(const decorrelated_loop<double>::config& cfg, Canceller canceller,
                             const std::vector<double>& v_converge, const std::vector<double>& v_probe) {
        const double open_msg = mutap_test::theoretical_msg_db(cfg.feedback_path);

        auto converge_cfg            = cfg;
        converge_cfg.forward_gain_db = open_msg - 6.0;
        decorrelated_loop<double> sim(converge_cfg);
        for (size_t blk = 0; blk < k_converge; ++blk) {
            sim.step(&v_converge[blk * k_block], &canceller);
        }
        return mutap_test::decorrelated_msg_db(cfg, &canceller, v_probe, open_msg - 15.0, open_msg + 25.0, 0.5)
               - open_msg;
    }

    /// A held, pitched near-end: the material that defeats every engine here.
    std::vector<double> held_note(size_t blocks, unsigned seed) {
        return mutap_test::voiced_near_end<double>(blocks * k_block, seed, 160); // 300 Hz
    }

} // namespace

// The headline: at 2.7 ms of loop latency the canceller alone cannot hold a
// held note (measured +0.6 dB, i.e. no useful gain), and a 5 Hz forward-path
// frequency shift turns the same rig into a working one (measured +18.4 dB).
TEST(AfcDecorrelation, FrequencyShiftRescuesTheHeldNote) {
    const auto path    = cabin_path();
    const auto v_conv  = held_note(k_converge, k_seed);
    const auto v_probe = held_note(k_probe, k_seed + 10);

    const double plain = added_stable_gain(loop_config(path), kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);

    auto shifted       = loop_config(path);
    shifted.mode       = forward_mode::shift;
    shifted.shift_hz   = 5.0;
    const double shift = added_stable_gain(shifted, kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);

    EXPECT_LT(plain, 6.0) << "measured +0.6 dB: the canceller alone has no answer to a held note here";
    EXPECT_GT(shift, 10.0) << "measured +18.4 dB";
    EXPECT_GT(shift, plain + 8.0) << "the shift is what moves this case";
}

// And the shift is NOT a substitute for prewhitening: driving the naive
// (un-prewhitened) core through the same shifted loop leaves it destabilizing
// close to useless (measured +1.6 dB here, the probe floor on other seeds).
// Decorrelation shrinks the bias term; PEM removes what is left of it.
TEST(AfcDecorrelation, FrequencyShiftDoesNotReplacePrewhitening) {
    const auto path    = cabin_path();
    const auto v_conv  = held_note(k_converge, k_seed);
    const auto v_probe = held_note(k_probe, k_seed + 10);

    auto shifted     = loop_config(path);
    shifted.mode     = forward_mode::shift;
    shifted.shift_hz = 5.0;

    tap::mu::partitioned_fdaf<double>::config naive_cfg;
    naive_cfg.block_size = k_block;
    naive_cfg.partitions = k_parts;

    const double naive = added_stable_gain(shifted, tap::mu::partitioned_fdaf<double>(naive_cfg), v_conv, v_probe);
    const double pem   = added_stable_gain(shifted, kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);

    // The gap is the assertion that matters; the absolute bound is loose
    // because the naive core's failure is seed-dependent in degree, not in
    // kind (measured +1.6 dB here, and the probe floor on seeds 22 and 42).
    EXPECT_LT(naive, 6.0) << "measured +1.6 dB: a shifted loop does not fix an un-prewhitened estimate";
    EXPECT_GT(pem - naive, 8.0) << "measured 16.8 dB apart";
}

// Delay modulation is the gentler decorrelator (no pitch artifact) and buys
// materially less: measured +7.5 dB against the frequency shift's +16.6.
// Pinned so the ranking cannot silently invert.
TEST(AfcDecorrelation, DelayModulationHelpsLessThanFrequencyShift) {
    const auto path    = cabin_path();
    const auto v_conv  = held_note(k_converge, k_seed);
    const auto v_probe = held_note(k_probe, k_seed + 10);

    auto wobbled      = loop_config(path);
    wobbled.mode      = forward_mode::delay_modulation;
    wobbled.depth     = 16.0; // +-8 samples, +-0.17 ms
    wobbled.rate_hz   = 1.3;
    const double dmod = added_stable_gain(wobbled, kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);

    auto shifted       = loop_config(path);
    shifted.mode       = forward_mode::shift;
    shifted.shift_hz   = 5.0;
    const double shift = added_stable_gain(shifted, kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);

    EXPECT_GT(dmod, 2.0) << "measured +8.1 dB";
    EXPECT_GT(shift, dmod + 4.0) << "measured 10.3 dB apart";
}

// The karaoke-specific one: a backing track summed into the loudspeaker feed
// is uncorrelated with the singer, so it is free excitation for identification
// - but only when the canceller's reference is tapped AFTER the mix, which is
// what this loop models. Measured +20.0 dB against +0.3 dB with no aux feed.
// (Broadband aux at singer level is the upper bound; narrowband program
// material only excites the bands it covers - see docs and the deck.)
TEST(AfcDecorrelation, AuxiliarySpeakerFeedExcitesIdentification) {
    const auto path    = cabin_path();
    const auto v_conv  = held_note(k_converge, k_seed);
    const auto v_probe = held_note(k_probe, k_seed + 10);
    const auto aux     = mutap_test::white_near_end<double>(120000, k_seed + 777);

    auto excited     = loop_config(path);
    excited.aux      = &aux;
    excited.aux_gain = 1.0; // 0 dB relative to the unit-RMS near end

    const double with_aux = added_stable_gain(excited, kalman_afc(afc_config<kalman_afc>()), v_conv, v_probe);
    EXPECT_GT(with_aux, 12.0) << "measured +19.7 dB";
}
