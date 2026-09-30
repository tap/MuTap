// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Forward-path decorrelation and auxiliary speaker excitation in the car
// cabin: the karaoke case (docs/karaoke-afc.md).
//
// The case this suite exists for is the one test_pem_afc.cpp's material
// cannot reach: a SUSTAINED, pitched near end (a held sung note) in a small
// cabin. At the branch's original 2.7 ms loop the canceller alone has
// nothing to give there, and a forward-path frequency shift turns the rig
// into a working one. At the anti-howl PoC's latencies (10 ms = S1, 20 ms =
// S3) the picture is different, and the product is CANCELLER-FIRST: the
// canceller alone holds the held note, and the shift is a studied,
// material-dependent option that a blind listening test on real singing
// decides. What is gated here:
//
//   - the shifter itself: single sideband, measured image rejection and
//     group delay (the numbers support/decorrelated_loop.h quotes);
//   - the 2.7 ms low-latency REGRESSION ROW: the shift rescues the held note;
//     PEM beats the naive core behind the shift;
//   - S1: the canceller alone clears a floor on the held note; the shift
//     raises the RUNAWAY limit.
//
// Swept, not gated (test_afc_decorrelation_sweep.cpp, MUTAP_SLOW=1), to keep
// this suite near its ~3 minute budget: the aux (backing-track) rows (S1
// median +15.94 against the canceller's +11.63 at 40 s), PEM vs the naive
// core at S1 and S3 (the sweep asserts PEM - naive > 5 dB in all five rooms
// at both delays), and every room other than the cabin.
//
// How (support/karaoke_asg.h): the cabin fixture's first 1024 taps,
// band-limited; PEM + FD-Kalman (speech cascade); a 5 Hz IIR SSB shift
// (support/decorrelated_loop.h); converge 1500 blocks at exact_msg_db - 6,
// then bisect the forward gain to 0.5 dB with the 40 dB runaway rule; ASG
// against the dry open loop bisected on the same probe;
// medians over rooms.h's five seed sets (seeds 2, 22, 42, 62, 82). Probe
// lengths are the shortest whose five-seed median sat within 0.25 dB of
// the 40 s probe's (test_afc_decorrelation_sweep.cpp, MUTAP_SLOW=1, prints
// the table; bisection there at 0.1 dB):
//
//   cabin, band-limited, medians     0.8 s    5 s    10 s    20 s    40 s   gated with
//   held, canceller, 2.7 ms          -0.25  +0.21  +0.29  +0.29  +0.29     5 s
//   held, + 5 Hz shift, 2.7 ms      +12.67 +13.13 +13.21 +13.21 +13.21     5 s
//   held, naive + 5 Hz, 2.7 ms       -1.75 -13.06 -15.00 -15.00 -15.00    10 s (floor)
//   held, canceller, S1             +10.38 +11.47 +11.55 +11.63 +11.63     5 s
//   held, + 5 Hz shift, S1          +16.88 +18.07 +17.44 +18.05 +17.52    10 s (one probe)
//   held, + aux feed, S1            +14.69 +15.78 +15.86 +15.94 +15.94    sweep only (5 s)
//   held, naive + 5 Hz, S1           +4.93  +2.69  +5.93  +6.01  -0.59    not converged: sweep only
//   held, + 5 Hz shift, S3          +15.02 +17.05 +16.93 +17.00 +16.90    sweep only (5 s)
//   held, naive + 5 Hz, S3           +5.53  +7.56  +7.79  +7.95  +8.03    sweep only (10 s)
//
// The branch's original 0.8 s probe over-reads the dry open loop by 0.55 /
// 1.25 / 2.50 dB at 2.7 ms / S1 / S3; like for like, the canceller rows'
// 0.8 s medians sit -2.50 to +2.88 dB from their 40 s values (the held note
// reads low, the speech-envelope material high), the naive core's up to
// +13.25.
//
// RUNAWAY, NOT AUDIBLE. These are runaway limits. The ear objects earlier,
// and with the shifter much earlier: tools/notebook/karaoke_audible.py
// measures the audible limit with the offline howl criterion, and
// docs/karaoke-afc.md carries both. Every held-note shift number is a WORST
// CASE for the shift: a perfectly periodic synthetic note with a 40 dB floor
// between its harmonics, where partials recirculating through the shifter
// stand out. Shift claims about singing need real sung recordings and the
// ABX test.
//
// NOT GATED, recorded (the sweep has them): the shift's direction at S3 is
// room-dependent - in the cabin it COSTS runaway gain (median per-seed
// shift - plain -1.41 dB at 40 s, -1.14 at 20 s), while at 20 s studio gains
// +1.85, rehearsal +0.09, mt5 +1.85, mt9 +6.24 - and the naive core behind
// the shift at S1 never settles with probe length.
//
// Host-only by design: tests/CMakeLists.txt builds this file only for the
// host (like the FAUST suite), and the emulated selections do not name it.

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/decorrelated_loop.h"
#include "support/karaoke_asg.h"
#include "support/rooms.h"

namespace {

    using mutap_test::forward_mode;
    using mutap_test::iir_ssb_shifter;
    using mutap_test::median;
    using mutap_test::seed_in_set;
    namespace kk = mutap_test::karaoke;

    constexpr unsigned k_base_seed = 2;

    /// Per-seed-set results of one configuration on the band-limited cabin.
    struct rows {
        std::vector<double> asg;
        std::vector<double> chain;
        std::vector<double> open;
    };

    rows run(const kk::setup& s, const kk::protocol& p, const char* label) {
        const auto path = kk::room("cabin");
        rows       r;
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            const auto m = kk::measure(path, s, p, seed_in_set(k_base_seed, set));
            r.asg.push_back(m.asg());
            r.chain.push_back(m.chain_db);
            r.open.push_back(m.open_db);
        }
        std::string line = std::string(label) + " ASG";
        for (const double a : r.asg) {
            char b[16];
            std::snprintf(b, sizeof b, " %+.2f", a);
            line += b;
        }
        char med[16];
        std::snprintf(med, sizeof med, "%+.2f", median(r.asg));
        std::printf("%s  median %s\n", line.c_str(), med);
        ::testing::Test::RecordProperty(std::string(label) + "_median_asg_db", med);
        return r;
    }

    /// Per-seed-set a - b, printed with its median.
    std::vector<double> differences(const std::vector<double>& a, const std::vector<double>& b, const char* label) {
        std::vector<double> d;
        std::string         line = std::string(label);
        for (size_t i = 0; i < a.size(); ++i) {
            d.push_back(a[i] - b[i]);
            char buf[16];
            std::snprintf(buf, sizeof buf, " %+.2f", d.back());
            line += buf;
        }
        std::printf("%s  median %+.2f\n", line.c_str(), median(d));
        return d;
    }

    /// The protocol at `delay` with a `probe_s` probe, bisecting the chain
    /// over [lo, hi] dB re exact_msg_db. The brackets are narrowed around
    /// each row's measured per-seed range to keep the suite inside its
    /// runtime budget (8 dB = four probes at 0.5 dB), with >= 4 dB to spare
    /// on the side a claim depends on; an edge on the other side can only
    /// make a claim harder to pass (a clamped shift row reads low).
    kk::protocol at(size_t delay, double probe_s, double lo, double hi) {
        kk::protocol p;
        p.delay        = delay;
        p.probe_blocks = kk::probe_blocks(probe_s);
        p.chain_lo     = lo;
        p.chain_hi     = hi;
        return p;
    }

    kk::setup shifted(double hz, kk::engine core = kk::engine::kalman) {
        kk::setup s;
        s.mode     = forward_mode::shift;
        s.shift_hz = hz;
        s.core     = core;
        return s;
    }

    /// DFT of x at `hz` over [from, x.size()) with a Hann window.
    std::complex<double> dft_at(const std::vector<double>& x, size_t from, double hz) {
        const size_t         n = x.size() - from;
        std::complex<double> acc(0.0, 0.0);
        for (size_t i = 0; i < n; ++i) {
            const double w =
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
            const double ph = -2.0 * std::numbers::pi * hz * static_cast<double>(from + i) / kk::k_fs;
            acc += w * x[from + i] * std::complex<double>(std::cos(ph), std::sin(ph));
        }
        return acc;
    }

    /// A tone at `hz` through the shifter, shifted by `shift_hz`.
    std::vector<double> shift_tone(double hz, double shift_hz, size_t n) {
        iir_ssb_shifter     sh;
        std::vector<double> y(n);
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i);
            y[i]           = sh.process(std::cos(2.0 * std::numbers::pi * hz * t / kk::k_fs),
                                        2.0 * std::numbers::pi * shift_hz * t / kk::k_fs);
        }
        return y;
    }

} // namespace

// The shifter the loop uses is single sideband across the voice band: a tone
// at f comes out at f + shift with the image at f - shift far below it.
// Measured (4 s tone, Hann DFT at both sidebands, +5 Hz): image rejection
// 44.3 dB at 30 Hz, 55.7 at 100, 44.8 at 300, 49.0 at 1 kHz, 46.8 at 5 kHz
// (the design's worst case over 30 Hz - 20 kHz is 44.2 dB, by freqz on
// these coefficients). The 65-tap Hamming FIR it replaced managed 2.5 dB at
// 100 Hz and 7.7 dB at 300 Hz (the phase 0 review).
TEST(AfcDecorrelation, ShifterIsSingleSideband) {
    const size_t n = static_cast<size_t>(4.0 * kk::k_fs);
    for (const double hz : {30.0, 100.0, 300.0, 1000.0, 5000.0}) {
        const auto   y     = shift_tone(hz, 5.0, n);
        const double want  = std::abs(dft_at(y, n / 4, hz + 5.0));
        const double image = std::abs(dft_at(y, n / 4, hz - 5.0));
        const double rej   = 20.0 * std::log10(want / image);
        std::printf("image rejection at %g Hz: %.1f dB\n", hz, rej);
        EXPECT_GT(rej, 42.0) << hz << " Hz (measured >= 44.3 dB)";
    }
}

// It is causal and its group delay adds to the loop's forward delay (it is
// not taken out of it): the in-phase chain's group delay, from the phase
// slope of a 0 Hz "shift" at f +- 1 Hz. Measured 2.56 ms at 100 Hz, 1.39 at
// 200, 0.94 at 300, 0.29 at 1 kHz.
TEST(AfcDecorrelation, ShifterGroupDelayIsFrequencyDependent) {
    const size_t n        = static_cast<size_t>(4.0 * kk::k_fs);
    auto         phase_at = [&](double hz) {
        const auto          y = shift_tone(hz, 0.0, n);
        std::vector<double> x(n);
        for (size_t i = 0; i < n; ++i) {
            x[i] = std::cos(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kk::k_fs);
        }
        return std::arg(dft_at(y, n / 4, hz) / dft_at(x, n / 4, hz));
    };
    const double expect_ms[] = {2.56, 1.39, 0.94, 0.29};
    const double freqs[]     = {100.0, 200.0, 300.0, 1000.0};
    for (size_t k = 0; k < 4; ++k) {
        double dphi        = phase_at(freqs[k] + 1.0) - phase_at(freqs[k] - 1.0);
        dphi               = std::remainder(dphi, 2.0 * std::numbers::pi);
        const double gd_ms = -dphi / (2.0 * std::numbers::pi * 2.0) * 1000.0;
        std::printf("group delay at %g Hz: %.3f ms\n", freqs[k], gd_ms);
        EXPECT_NEAR(gd_ms, expect_ms[k], 0.05) << freqs[k] << " Hz";
    }
}

// THE LOW-LATENCY REGRESSION ROW (2.7 ms, the branch's original setting;
// not a PoC scenario). The canceller alone cannot hold a held note there,
// and a 5 Hz shift rescues it; behind the same shift the naive core fails
// outright, so decorrelation does not replace PEM prewhitening.
// Measured here (5 seed sets; 5 s probes, naive 10 s; ASG per seed set):
//   canceller alone   -2.00 +3.50 -3.00 +6.00 -1.50   median  -1.50
//   + 5 Hz shift     +13.50 +14.00 +12.50 +12.50 +15.50  median +13.50
//   naive + 5 Hz     -15.00 (the probe floor) in 4 of 5, -14.61
//   shift - canceller, per seed: median +15.50 (min +6.50)
//   PEM - naive behind the shift (chain limits): median +28.50 (min +27.11)
// The sweep (0.1 dB, 40 s): +0.29 / +13.21 / -15.00. On the raw (unbanded)
// cabin the branch measured +0.6 -> +18.4 dB at seed 2 against max|F|.
TEST(AfcDecorrelation, LowLatencyRowShiftRescuesTheHeldNote) {
    const auto plain = run(kk::setup{}, at(kk::k_low, 5.0, -6.0, 10.0), "low_plain");
    const auto shift = run(shifted(5.0), at(kk::k_low, 5.0, 8.0, 16.0), "low_shift5");
    const auto naive = run(shifted(5.0, kk::engine::naive), at(kk::k_low, 10.0, -15.0, 10.0), "low_naive_shift5");

    EXPECT_LT(median(plain.asg), 4.0) << "measured -1.50: no useful gain on a held note at 2.7 ms";
    EXPECT_GT(median(shift.asg), 9.0) << "measured +13.50: the shift rescues it";
    EXPECT_GT(median(differences(shift.asg, plain.asg, "low shift - plain ASG")), 8.0) << "measured +15.50";
    EXPECT_GT(median(differences(shift.chain, naive.chain, "low PEM - naive chain")), 15.0)
        << "measured +28.50: behind the shift, PEM vs the naive core";
}

// S1 (10 ms), held note, RUNAWAY limits. The canceller alone clears a floor
// (the product default), and the 5 Hz shift raises the runaway limit
// further. (The AUDIBLE limit is a different story for the shift - see the
// file comment and docs/karaoke-afc.md.)
// The canceller row is bisected (5 s probes): measured ASG per seed set
// +7.50 +14.00 +11.50 +12.00 +10.50, median +11.50. The shift row is gated
// as a median DIRECTION, to keep the suite inside its runtime budget: one
// 10 s probe per seed set at that seed's canceller limit + 3 dB, which must
// not run away in at least three of five (measured: stable in 5 of 5). The
// sweep's bisected medians (0.1 dB, 40 s): canceller +11.63, + 5 Hz +17.52,
// per-seed shift - canceller +2.20 / +8.35 / +3.87 / +6.42 / +7.03 (seed set
// 0 is the +2.20).
TEST(AfcDecorrelation, HeldNoteAtS1RunawayLimits) {
    const auto path  = kk::room("cabin");
    const auto plain = run(kk::setup{}, at(kk::k_s1, 5.0, 4.0, 20.0), "s1_plain");
    EXPECT_GT(median(plain.asg), 8.0) << "measured +11.50: the canceller alone holds the held note at S1";

    int stable = 0;
    for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
        const bool howls = kk::howls_at(path, shifted(5.0), at(kk::k_s1, 10.0, 0.0, 0.0), seed_in_set(k_base_seed, set),
                                        plain.chain[set] + 3.0);
        std::printf("s1 + 5 Hz at the canceller limit + 3 dB, seed set %u: %s\n", set, howls ? "runs away" : "stable");
        stable += howls ? 0 : 1;
    }
    ::testing::Test::RecordProperty("s1_shift5_stable_at_plain_plus_3dB", stable);
    EXPECT_GE(stable, 3) << "measured 5 of 5: the shift raises the runaway limit by > 3 dB in the median";
}
