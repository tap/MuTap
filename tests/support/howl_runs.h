/// @file howl_runs.h
/// @brief The howl detector's measurement runs: loop-born howls (dry and
///        with the PEM canceller), stable-loop residuals and open signals,
///        each observed by tap::mu::howl_detector and faust-icc's detector.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Host-only (it drives the vendored FAUST detector): shared by
// tests/test_howl_detector_host.cpp's gated rows and its MUTAP_SLOW sweep,
// so a table row means the same measurement in both.
//
// THE MEASUREMENT.
//   - Rooms: karaoke::room() (the four image-source fixtures and the
//     random_decaying_rir rooms mt5 and mt9, band-limited, 1024 taps).
//   - The 40 dB rule: the first block whose RMS reaches 100 over the unit-RMS
//     near end (closed_loop.h's loop_howls).
//   - Dry loop (no canceller): plain at exact_msg_db + over; shifted (2 / 5 Hz
//     SSB, decorrelated_loop's shifter) at its own bisected limit + over
//     (exact_msg_db does not describe a shifted loop). The detector sees the
//     mic signal (e = y).
//   - Live loop (PEM + FD-Kalman, speech cascade, karaoke::kalman_afc): ONE
//     loop, as a deployment runs it: 1500 blocks of convergence at
//     exact_msg_db - 6 dB, then the gain steps to the canceller's limit +
//     rel (the limit bisected as karaoke::measure does: converged copy, fresh
//     10 s probes), and the run continues on the probe seed's material. The
//     detector sees the residual e from block 0; the step block is `step`.
//   - The detector's loop_period_s is the forward delay (the electrical part
//     of the loop; the rooms' flight adds 0-2 ms).
//
// Every per-path time is the first block, at or after `step`, at which that
// path's condition holds, read from the detector's readouts: growth when
// rise_db() reaches rise_db, ceiling when power_db() reaches ceiling_db,
// level when level_db() reaches level_db with prominence_db() at
// level_prominence_db. faust-icc's trip is its confidence above 0.5.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "closed_loop.h"
#include "decorrelated_loop.h"
#include "faust_generated.h"
#include "howl_materials.h"
#include "karaoke_asg.h"
#include "mutap/fft.h"
#include "mutap/howl_detector.h"
#include "rooms.h"

namespace mutap_test::howl {

    namespace kk    = mutap_test::karaoke;
    using detector  = tap::mu::howl_detector<double>;
    using icc_block = mutap_faust::faust_block<mutap_faust::icc_howl_detect_f64, double>;

    inline constexpr size_t k_block = 64;
    inline constexpr size_t k_tail  = 375; ///< blocks a howl run continues past the rule (0.5 s)

    /// faust-icc's prominence is over the mean of ALL its 32 bands (the
    /// peak included, so it cannot exceed 20 log10 32 = 30.1 dB); this
    /// detector's is over the mean of the others. The same spectrum read
    /// both ways: p_others = -20 log10((32 * 10^(-p_all / 20) - 1) / 31).
    inline double icc_prominence_over_others(double p_all) {
        const double r = 32.0 * std::pow(10.0, -p_all / 20.0) - 1.0;
        return r > 0.0 ? -20.0 * std::log10(r / 31.0) : std::numeric_limits<double>::infinity();
    }

    /// The harness's ceiling: the near end sits at unit RMS (0 dB re 1), not
    /// at a calibrated dBFS, so the absolute ceiling is placed 10 dB under
    /// the 40 dB rule. A deployment sets ceiling_db from its own gain
    /// structure.
    inline constexpr double k_harness_ceiling_db = 30.0;

    /// The default detector with the loop period of a forward delay and the
    /// harness's ceiling.
    inline detector::config detector_config(size_t forward_delay) {
        detector::config c;
        c.loop_period_s = static_cast<double>(forward_delay) / k_fs;
        c.ceiling_db    = k_harness_ceiling_db;
        return c;
    }

    /// The burst oracle for the false-trip rows: block t lies within 0.5 s
    /// of a block whose RMS reaches 10 (20 dB over the unit near end). A trip
    /// there coincides with a feedback burst the loop itself produced, not
    /// with the singer.
    inline std::vector<char> burst_oracle(const std::vector<double>& rms) {
        const size_t      n = rms.size();
        std::vector<char> out(n, 0);
        long              last = -1000000;
        for (size_t t = 0; t < n; ++t) {
            if (rms[t] >= 10.0) {
                last = static_cast<long>(t);
            }
            out[t] = (static_cast<long>(t) - last <= static_cast<long>(k_tail)) ? 1 : 0;
        }
        long next = 1L << 40;
        for (size_t t = n; t-- > 0;) {
            if (rms[t] >= 10.0) {
                next = static_cast<long>(t);
            }
            out[t] = static_cast<char>(out[t] | ((next - static_cast<long>(t) <= static_cast<long>(k_tail)) ? 1 : 0));
        }
        return out;
    }

    /// Per-block trace of one run, both detectors.
    struct trace {
        detector::config    cfg;
        long                step = 0; ///< first block of the measured phase
        std::vector<double> rms, rise, power_db, level_db, prom, peak_hz, line_hz, harm, sub;
        std::vector<char>   tripped;
        std::vector<double> icc_conf, icc_prom;
        double              howl_hz = 0.0; ///< spectral peak of the 8192 samples ending at the first rule block

        size_t size() const { return rms.size(); }
        bool   growth_at(size_t t) const { return rise[t] >= cfg.rise_db; }
        bool   ceiling_at(size_t t) const { return cfg.level_catch && power_db[t] >= cfg.ceiling_db; }
        bool   level_at(size_t t) const {
            return cfg.level_catch && level_db[t] >= cfg.level_db && prom[t] >= cfg.level_prominence_db;
        }
        bool icc_at(size_t t) const { return icc_conf[t] > 0.5; }

        template <typename Cond>
        long first(Cond c, long from) const {
            for (auto t = static_cast<size_t>(std::max(from, 0L)); t < size(); ++t) {
                if (c(t)) {
                    return static_cast<long>(t);
                }
            }
            return -1;
        }
        long first_rule(long from) const {
            return first([&](size_t t) { return rms[t] >= 100.0; }, from);
        }
        long first_growth(long from) const {
            return first([&](size_t t) { return growth_at(t); }, from);
        }
        long first_ceiling(long from) const {
            return first([&](size_t t) { return ceiling_at(t); }, from);
        }
        long first_level(long from) const {
            return first([&](size_t t) { return level_at(t); }, from);
        }
        long first_any(long from) const {
            return first([&](size_t t) { return growth_at(t) || ceiling_at(t) || level_at(t); }, from);
        }
        long first_icc(long from) const {
            return first([&](size_t t) { return icc_at(t); }, from);
        }
        /// Rising edges of a condition over [from, to).
        template <typename Cond>
        size_t events(Cond c, size_t from, size_t to) const {
            size_t n    = 0;
            bool   prev = false;
            for (size_t t = from; t < std::min(to, size()); ++t) {
                const bool now = c(t);
                n += (now && !prev) ? 1U : 0U;
                prev = now;
            }
            return n;
        }
    };

    /// Feeds both detectors one block at a time and records the trace.
    class observer {
      public:
        explicit observer(const detector::config& cfg)
            : m_det(cfg)
            , m_icc(static_cast<int>(k_fs))
            , m_y(3, std::vector<double>(k_block)) {
            m_trace.cfg = cfg;
        }

        void feed(const double* e, double rms) {
            m_det.process_block(e, k_block);
            m_tail.insert(m_tail.end(), e, e + k_block);
            if (m_tail.size() > k_peak_window) {
                m_tail.erase(m_tail.begin(), m_tail.begin() + static_cast<long>(m_tail.size() - k_peak_window));
            }
            if (rms >= 100.0 && m_trace.howl_hz == 0.0) {
                m_trace.howl_hz = spectral_peak_hz(m_tail);
            }
            double* yp[3] = {m_y[0].data(), m_y[1].data(), m_y[2].data()};
            m_buf.assign(e, e + k_block);
            m_icc.process_mono(m_buf.data(), yp, static_cast<int>(k_block));
            double conf = 0.0;
            for (size_t i = 0; i < k_block; ++i) {
                conf = std::max(conf, m_y[0][i]);
            }
            trace& t = m_trace;
            t.rms.push_back(rms);
            t.rise.push_back(m_det.rise_db());
            t.power_db.push_back(m_det.power_db());
            t.level_db.push_back(m_det.level_db());
            t.prom.push_back(m_det.prominence_db());
            t.peak_hz.push_back(m_det.peak_hz());
            t.line_hz.push_back(m_det.line_hz());
            t.harm.push_back(m_det.harmonic_ratio_db());
            t.sub.push_back(m_det.subharmonic_ratio_db());
            t.tripped.push_back(m_det.tripped() ? 1 : 0);
            t.icc_conf.push_back(conf);
            t.icc_prom.push_back(m_y[2][k_block - 1]);
        }

        trace& result() { return m_trace; }

        /// The Hann-windowed spectral peak of x (zero-padded to 2^16), Hz.
        static double spectral_peak_hz(const std::vector<double>& x) {
            constexpr size_t    k_n = size_t{1} << 16;
            tap::mu::real_fft   fft(k_n);
            std::vector<double> buf(k_n, 0.0);
            const size_t        m = x.size();
            for (size_t i = 0; i < m; ++i) {
                const double w =
                    0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(m));
                buf[i] = w * x[i];
            }
            fft.forward_inplace(buf.data());
            size_t best = 1;
            double peak = 0.0;
            for (size_t k = 1; k < k_n / 2; ++k) {
                const double mag = buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1];
                if (mag > peak) {
                    peak = mag;
                    best = k;
                }
            }
            return static_cast<double>(best) * k_fs / static_cast<double>(k_n);
        }

      private:
        static constexpr size_t          k_peak_window = 8192;
        std::vector<double>              m_tail;
        detector                         m_det;
        icc_block                        m_icc;
        std::vector<std::vector<double>> m_y;
        std::vector<double>              m_buf;
        trace                            m_trace;
    };

    inline double block_rms(const double* x) {
        double s = 0.0;
        for (size_t i = 0; i < k_block; ++i) {
            s += x[i] * x[i];
        }
        return std::sqrt(s / static_cast<double>(k_block));
    }

    inline decorrelated_loop<double>::config loop_config(const std::vector<double>& path, size_t delay,
                                                         double shift_hz) {
        decorrelated_loop<double>::config c;
        c.feedback_path = path;
        c.block_size    = k_block;
        c.forward_delay = delay;
        if (shift_hz > 0.0) {
            c.mode     = forward_mode::shift;
            c.shift_hz = shift_hz;
        }
        return c;
    }

    /// One dry-loop howl: `over` dB above the loop's limit (exact_msg_db
    /// plain; the bisected limit, 0.25 dB tolerance on the run's own first
    /// 10 s, shifted). Runs to the rule + k_tail or `max_s`.
    inline trace dry_howl(const std::string& room, size_t delay, double shift_hz, double over, material mat,
                          unsigned seed, double max_s, double* limit_out = nullptr) {
        const auto   path  = kk::room(room);
        const double exact = exact_msg_db(path, delay);
        const auto   n_blk = static_cast<size_t>(max_s * k_fs / static_cast<double>(k_block));
        const auto   v     = make(mat, n_blk * k_block, seed);
        auto         cfg   = loop_config(path, delay, shift_hz);
        double       limit = exact;
        if (shift_hz > 0.0) {
            const auto                probe_len = std::min(v.size(), kk::probe_blocks(10.0) * k_block);
            const std::vector<double> probe(v.begin(), v.begin() + static_cast<long>(probe_len));
            limit = decorrelated_msg_db(cfg, static_cast<const kk::naive_core*>(nullptr), probe, exact - 10.0,
                                        exact + 30.0, 0.25);
        }
        if (limit_out != nullptr) {
            *limit_out = limit;
        }
        cfg.forward_gain_db = limit + over;
        decorrelated_loop<double> sim(cfg);
        observer                  obs(detector_config(delay));
        long                      rule = -1;
        for (size_t blk = 0; blk < n_blk; ++blk) {
            const double r = sim.step(&v[blk * k_block], static_cast<kk::naive_core*>(nullptr));
            obs.feed(sim.error_block().data(), r);
            if (rule < 0 && r >= 100.0) {
                rule = static_cast<long>(blk);
            }
            if (rule >= 0 && blk >= static_cast<size_t>(rule) + k_tail) {
                break;
            }
        }
        return std::move(obs.result());
    }

    /// What the live loop's near end and speaker carry.
    struct live_material {
        material mat      = material::voiced;
        bool     aux_only = false;        ///< near end a -60 dB floor (the singer silent)
        double   aux_db   = std::nan(""); ///< white aux into the speaker at this level; NaN = none
    };

    /// One live loop (file comment): converge, step to limit + rel, run
    /// `seconds` (a howl run stops at the rule + k_tail). The limit goes to
    /// *limit_out when given.
    inline trace live_run(const std::string& room, size_t delay, double shift_hz, double rel, const live_material& lm,
                          unsigned seed, double seconds, double* limit_out = nullptr) {
        const auto   path  = kk::room(room);
        const double exact = exact_msg_db(path, delay);
        kk::protocol pr;
        pr.delay           = delay;
        const auto run_blk = static_cast<size_t>(seconds * k_fs / static_cast<double>(k_block));
        auto       gen     = [&](size_t n, unsigned s) {
            if (lm.aux_only) {
                auto w = white_near_end<double>(n, s + 999);
                for (auto& x : w) {
                    x *= 1e-3;
                }
                return w;
            }
            return make(lm.mat, n, s);
        };
        const auto v_conv  = gen(pr.converge_blocks * k_block, seed);
        const auto v_probe = gen(pr.probe_blocks * k_block, seed + 10);
        const auto v_run   = gen(run_blk * k_block, seed + 10);
        const bool has_aux = !std::isnan(lm.aux_db);
        const auto aux     = white_near_end<double>((pr.converge_blocks + run_blk) * k_block, seed + 777);
        auto       cfg     = loop_config(path, delay, shift_hz);
        if (has_aux) {
            cfg.aux      = &aux;
            cfg.aux_gain = std::pow(10.0, lm.aux_db / 20.0);
        }
        kk::kalman_afc::config ac;
        ac.fdaf.block_size = k_block;
        ac.fdaf.partitions = kk::k_parts;
        kk::kalman_afc conv(ac);
        kk::converge(conv, cfg, exact, pr, v_conv);
        const double limit =
            decorrelated_msg_db(cfg, &conv, v_probe, exact + pr.chain_lo, exact + pr.chain_hi, pr.tol_db);
        if (limit_out != nullptr) {
            *limit_out = limit;
        }
        kk::kalman_afc afc(ac);
        auto           live_cfg  = cfg;
        live_cfg.forward_gain_db = exact - 6.0;
        decorrelated_loop<double> sim(live_cfg);
        observer                  obs(detector_config(delay));
        for (size_t blk = 0; blk < pr.converge_blocks; ++blk) {
            const double r = sim.step(&v_conv[blk * k_block], &afc);
            obs.feed(sim.error_block().data(), r);
        }
        sim.set_forward_gain_db(limit + rel);
        long rule = -1;
        for (size_t blk = 0; blk < run_blk; ++blk) {
            const double r = sim.step(&v_run[blk * k_block], &afc);
            obs.feed(sim.error_block().data(), r);
            if (rule < 0 && r >= 100.0) {
                rule = static_cast<long>(blk);
            }
            if (rel > 0.0 && rule >= 0 && blk >= static_cast<size_t>(rule) + k_tail) {
                break;
            }
        }
        trace t = std::move(obs.result());
        t.step  = static_cast<long>(pr.converge_blocks);
        return t;
    }

    /// An open signal: `seconds` of material `mat`, loop period `delay`.
    inline trace open_run(material mat, unsigned seed, double seconds, size_t delay) {
        const auto n_blk = static_cast<size_t>(seconds * k_fs / static_cast<double>(k_block));
        const auto v     = make(mat, n_blk * k_block, seed);
        observer   obs(detector_config(delay));
        for (size_t blk = 0; blk < n_blk; ++blk) {
            obs.feed(&v[blk * k_block], block_rms(&v[blk * k_block]));
        }
        return std::move(obs.result());
    }

} // namespace mutap_test::howl
