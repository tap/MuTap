/// @file karaoke_asg.h
/// @brief The karaoke suites' rooms, materials and like-for-like ASG
///        measurement on decorrelated_loop.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Shared by test_afc_decorrelation.cpp (the gated claims) and
// test_afc_decorrelation_sweep.cpp (the MUTAP_SLOW sweep), so a number in
// docs/karaoke-afc.md means the same measurement wherever it came from.
//
// THE MEASUREMENT (the anti-howl PoC's phase 0 rules):
//   - rooms: the first 1024 taps of a fixture re-normalized to unit energy,
//     or a random_decaying_rir room, then band_limited() (rooms.h);
//   - the canceller (PEM + FD-Kalman with the speech cascade, or the naive
//     NLMS core) converges inside the loop for `converge_blocks` of one seed
//     of the material at exact_msg_db - 6 dB, then the forward gain is
//     bisected on `probe_blocks` of a second seed (seed + 10), a fresh copy
//     of the converged canceller per probe; howling = a 64-sample block at
//     40 dB over the unit-RMS near end (closed_loop.h's rule): RUNAWAY;
//   - like-for-like reference: the dry loop (plain forward path, no
//     canceller) bisected on the SAME probe material and length. ASG = chain
//     limit - that open-loop limit. exact_msg_db (the phase-exact analytic
//     open-loop limit) and theoretical_msg_db (max|F|) ride along.
// Everything is double precision: a property of the algorithm, not of a
// target's arithmetic.
#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "../fixtures/rir_cabin.h"
#include "../fixtures/rir_hall.h"
#include "../fixtures/rir_rehearsal.h"
#include "../fixtures/rir_studio.h"
#include "closed_loop.h"
#include "decorrelated_loop.h"
#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/pem_afc.h"
#include "rooms.h"

namespace mutap_test::karaoke {

    inline constexpr size_t k_block = 64;
    inline constexpr size_t k_taps  = 1024; ///< first 21 ms of a room: direct + early reflections
    inline constexpr size_t k_parts = k_taps / k_block;
    inline constexpr double k_fs    = 48000.0;

    /// The PoC's latency cases as forward delays at 48 kHz, and the branch's
    /// original low-latency setting (kept as a regression row).
    inline constexpr size_t k_low = 128; ///< 2.7 ms
    inline constexpr size_t k_s1  = 480; ///< 10 ms
    inline constexpr size_t k_s3  = 960; ///< 20 ms

    using kalman_afc = tap::mu::pem_afc<double, tap::mu::speech_predictor<double>, tap::mu::partitioned_fdkf<double>>;
    using naive_core = tap::mu::partitioned_fdaf<double>;

    /// Probe length in blocks for a probe of `seconds`.
    constexpr size_t probe_blocks(double seconds) {
        return static_cast<size_t>(seconds * k_fs / static_cast<double>(k_block));
    }

    inline std::vector<double> fixture_room(const float* rir, bool banded) {
        std::vector<double> f(rir, rir + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return banded ? band_limited(f) : f;
    }

    /// "cabin", "studio", "rehearsal", "hall" (the image-source fixtures) or
    /// "mtN" (random_decaying_rir, seed N): both generator families.
    inline std::vector<double> room(const std::string& name, bool banded = true) {
        if (name == "cabin") {
            return fixture_room(fixtures::k_rir_cabin, banded);
        }
        if (name == "studio") {
            return fixture_room(fixtures::k_rir_studio, banded);
        }
        if (name == "rehearsal") {
            return fixture_room(fixtures::k_rir_rehearsal, banded);
        }
        if (name == "hall") {
            return fixture_room(fixtures::k_rir_hall, banded);
        }
        if (name.rfind("mt", 0) == 0 && name.size() > 2) {
            const auto f = random_decaying_rir<double>(k_taps, static_cast<unsigned>(std::stoul(name.substr(2))));
            return banded ? band_limited(f) : f;
        }
        throw std::invalid_argument("karaoke::room: unknown room " + name);
    }

    enum class material {
        held,  ///< voiced_near_end at a 160-sample period: a held 300 Hz note
        speech ///< ar_near_end: the speech-envelope material
    };

    inline std::vector<double> near_end(material m, size_t blocks, unsigned seed) {
        return (m == material::held) ? voiced_near_end<double>(blocks * k_block, seed, 160)
                                     : ar_near_end<double>(blocks * k_block, seed);
    }

    enum class engine {
        kalman, ///< PEM + FD-Kalman, speech cascade, default config
        naive   ///< the un-prewhitened NLMS core
    };

    /// One configuration of the loop and canceller.
    struct setup {
        forward_mode mode     = forward_mode::plain;
        double       shift_hz = 5.0;
        engine       core     = engine::kalman;
        material     mat      = material::held;
        bool         aux      = false; ///< white aux feed at the singer's level (0 dB)
        /// After the decorrelator, before the gain: the chain's reverb slot
        /// (decorrelated_loop.h's forward_stage; caller-owned, one per
        /// thread). Empty for none.
        forward_stage<double> stage;
    };

    /// How it is measured.
    struct protocol {
        size_t delay           = k_s1;
        size_t probe_blocks    = karaoke::probe_blocks(10.0);
        size_t converge_blocks = 1500;
        double tol_db          = 0.5;
        double open_lo = -10.0, open_hi = 10.0;   ///< open-loop bracket, dB re exact_msg_db
        double chain_lo = -15.0, chain_hi = 30.0; ///< chain bracket, dB re exact_msg_db
    };

    struct result {
        double exact_db = 0.0; ///< exact_msg_db of the dry loop
        double max_f_db = 0.0; ///< theoretical_msg_db (max|F|)
        double open_db  = 0.0; ///< bisected dry open loop, same probe
        double chain_db = 0.0; ///< bisected loop with the canceller and forward path
        double asg() const { return chain_db - open_db; }
    };

    inline decorrelated_loop<double>::config loop_config(const std::vector<double>& path, const setup& s, size_t delay,
                                                         const std::vector<double>* aux) {
        decorrelated_loop<double>::config cfg;
        cfg.feedback_path = path;
        cfg.block_size    = k_block;
        cfg.forward_delay = delay;
        cfg.mode          = s.mode;
        cfg.shift_hz      = s.shift_hz;
        cfg.stage         = s.stage;
        if (s.aux && aux != nullptr) {
            cfg.aux      = aux;
            cfg.aux_gain = 1.0;
        }
        return cfg;
    }

    /// Bisected limit of the dry open loop (plain, no canceller) on `v`.
    inline double open_loop_db(const std::vector<double>& path, size_t delay, const std::vector<double>& v,
                               const protocol& p) {
        const double exact = exact_msg_db(path, delay);
        const auto   cfg   = loop_config(path, setup{}, delay, nullptr);
        return decorrelated_msg_db(cfg, static_cast<const naive_core*>(nullptr), v, exact + p.open_lo,
                                   exact + p.open_hi, p.tol_db);
    }

    /// Converge `canceller` in the loop `cfg` at exact_msg_db - 6 dB.
    template <typename Canceller>
    void converge(Canceller& canceller, decorrelated_loop<double>::config cfg, double exact, const protocol& p,
                  const std::vector<double>& v_conv) {
        cfg.forward_gain_db = exact - 6.0;
        decorrelated_loop<double> sim(cfg);
        for (size_t blk = 0; blk < p.converge_blocks; ++blk) {
            sim.step(&v_conv[blk * k_block], &canceller);
        }
    }

    /// Run `f(canceller, loop config, exact, v_conv, v_probe)` with the
    /// canceller `s.core` configured for the suites, on seed `seed`'s material.
    template <typename F>
    auto with_chain(const std::vector<double>& path, const setup& s, const protocol& p, unsigned seed, F f) {
        const double exact   = exact_msg_db(path, p.delay);
        const auto   v_conv  = near_end(s.mat, p.converge_blocks, seed);
        const auto   v_probe = near_end(s.mat, p.probe_blocks, seed + 10);
        const auto   aux     = white_near_end<double>(120000, seed + 777);
        const auto   cfg     = loop_config(path, s, p.delay, &aux);
        if (s.core == engine::kalman) {
            kalman_afc::config c;
            c.fdaf.block_size = k_block;
            c.fdaf.partitions = k_parts;
            kalman_afc afc(c);
            return f(afc, cfg, exact, v_conv, v_probe);
        }
        naive_core::config c;
        c.block_size = k_block;
        c.partitions = k_parts;
        naive_core core(c);
        return f(core, cfg, exact, v_conv, v_probe);
    }

    /// Bisected limit of the chain `s` on `path` at seed `seed`; with
    /// `with_open`, also the like-for-like open-loop reference.
    inline result measure(const std::vector<double>& path, const setup& s, const protocol& p, unsigned seed,
                          bool with_open = true) {
        result r;
        r.exact_db = exact_msg_db(path, p.delay);
        r.max_f_db = theoretical_msg_db(path);
        if (with_open) {
            r.open_db = open_loop_db(path, p.delay, near_end(s.mat, p.probe_blocks, seed + 10), p);
        }
        r.chain_db = with_chain(
            path, s, p, seed, [&](auto& canceller, const auto& cfg, double exact, const auto& vc, const auto& vp) {
                converge(canceller, cfg, exact, p, vc);
                return decorrelated_msg_db(cfg, &canceller, vp, exact + p.chain_lo, exact + p.chain_hi, p.tol_db);
            });
        return r;
    }

    /// One probe instead of a bisection: does the converged chain `s` howl
    /// at the absolute forward gain `gain_db` within the probe? (A median
    /// direction over seed sets needs only this at the claim's threshold.)
    inline bool howls_at(const std::vector<double>& path, const setup& s, const protocol& p, unsigned seed,
                         double gain_db) {
        return with_chain(path, s, p, seed,
                          [&](auto& canceller, auto cfg, double exact, const auto& vc, const auto& vp) {
                              converge(canceller, cfg, exact, p, vc);
                              cfg.forward_gain_db = gain_db;
                              decorrelated_loop<double> sim(cfg);
                              return decorrelated_loop_howls(sim, &canceller, vp);
                          });
    }

} // namespace mutap_test::karaoke
