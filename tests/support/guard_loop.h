/// @file guard_loop.h
/// @brief The safety layer's live loop: afc_chain with a howl_guard attached,
///        M mics in a simulated room, the forward gain (and an optional SSB
///        shift) in the decorrelator slot, and the guard-independent
///        oracles the guard's claims are measured against.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// THE LOOP (afc_chain's timeline, as tests/test_afc_chain.cpp's chain_loop and
// tests/support/two_mic_loop.h):
//
//   mic_m[n]  = x_m[n] + sum_k F_m(t)[k] clip(speaker[n - d - k])
//   speaker   = afc_chain(mics, aux)
//             = K shift(sum_m g_m e_m) [-> reverb -> guard bus stage] + aux
//   u[n]      = speaker[n - block - reference_delay]   (the shared reference)
//
// d is the electrical delay (S1 480, S3 960 samples), reference_delay =
// d - block - 32 (two_mic_loop's aligned reference: the direct path lands at
// tap 32), K the forward gain in the decorrelator slot (a gain stage, as
// test_afc_chain.cpp: the cancellers identify the room, not the gain), g_m
// the guard's per-mic gains. F_m(t) changes at `change_block`: to paths2
// (S2a, a walk to another room) or to change_scale x F (S2b, F -> 2F).
//
// THE ORACLES (guard-independent: they read the true room, never the
// guard's statistics):
//   * frozen-estimate margin: the phase-exact MSG (closed_loop.h's
//     exact_msg_db, 2^15 grid) of the loop the snapshot would close,
//     sum_m g_m (F_m(t) - F_hat_m) at the current gains, minus K (and minus
//     the bus stage's gain when one is attached). < 0 dB: the frozen loop
//     is unstable. Undefined with a frequency shift or a reverb in the loop
//     (the tests report the 40 dB rule alone there).
//   * misalignment: 10 log10(|F_m(t) - F_hat_m|^2 / |F_m(t)|^2) over the
//     taps the filter covers.
//   * the 40 dB rule: a block of a residual e_m (before the guard's gain)
//     at RMS >= 100 over the unit-RMS near end (closed_loop.h).
//   * the burst oracle (howl_runs.h): a block within 0.5 s of a block at
//     RMS >= 10 (+20 dB).
// Double precision: the claims are the algorithm's (rooms.h).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "closed_loop.h"
#include "decorrelated_loop.h"
#include "howl_materials.h"
#include "karaoke_asg.h"
#include "mutap/afc_chain.h"
#include "mutap/fd_kalman.h"
#include "mutap/howl_guard.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"
#include "rooms.h"

namespace mutap_test::guard {

    inline constexpr size_t k_block  = 64;
    inline constexpr size_t k_taps   = 1024;
    inline constexpr size_t k_parts  = k_taps / k_block;
    inline constexpr size_t k_margin = 32; ///< the reference's jitter margin (two_mic_loop.h)
    inline constexpr double k_fs     = 48000.0;
    inline constexpr size_t k_s1     = 480; ///< 10 ms
    inline constexpr size_t k_s3     = 960; ///< 20 ms
    inline constexpr size_t k_oracle = 8;   ///< blocks between oracle evaluations

    template <typename Sample>
    using afc = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;
    template <typename Sample>
    using chain_t = tap::mu::afc_chain<Sample, afc<Sample>>;
    template <typename Sample>
    using guard_t = tap::mu::howl_guard<Sample>;

    /// The canceller of every guard test: PEM + FD-Kalman with the speech
    /// cascade, 1024 taps, the 2-partition shadow (the guard needs D).
    template <typename Sample>
    typename afc<Sample>::config afc_config() {
        typename afc<Sample>::config c;
        c.fdaf.block_size   = k_block;
        c.fdaf.partitions   = k_parts;
        c.shadow_partitions = 2;
        return c;
    }

    constexpr size_t blocks_of(double seconds) {
        return static_cast<size_t>(seconds * k_fs / static_cast<double>(k_block));
    }

    /// The guard's detector for delay d: the default detector, its loop
    /// period the electrical delay, its ceiling the harness's (+30 dB re the
    /// unit near end, 10 dB under the 40 dB rule; howl_runs.h).
    template <typename Sample>
    typename guard_t<Sample>::config guard_config(size_t mics, size_t delay) {
        typename guard_t<Sample>::config c;
        c.microphones            = mics;
        c.block_size             = k_block;
        c.sample_rate            = k_fs;
        c.detector.loop_period_s = static_cast<Sample>(static_cast<double>(delay) / k_fs);
        c.detector.ceiling_db    = Sample(30);
        return c;
    }

    /// The forward path in the decorrelator slot: an optional SSB shift (the
    /// harness's exact-ramp iir_ssb_shifter, decorrelated_loop.h) and the
    /// forward gain K.
    template <typename Sample>
    struct forward_stage {
        double          gain     = 1.0;
        double          shift_hz = 0.0;
        long long       n        = 0;
        iir_ssb_shifter shifter;
        void            process_block(const Sample* in, Sample* out, size_t len) noexcept {
            for (size_t i = 0; i < len; ++i) {
                double x = static_cast<double>(in[i]);
                if (shift_hz != 0.0) {
                    x = shifter.process(x, 2.0 * std::numbers::pi * shift_hz * static_cast<double>(n) / k_fs);
                }
                out[i] = static_cast<Sample>(gain * x);
                ++n;
            }
        }
    };

    /// What one mic's singer and the speaker carry.
    struct material_spec {
        howl::material mat    = howl::material::voiced;
        bool           aux    = false; ///< white backing track into the speaker
        double         aux_db = -12.0; ///< its level re the unit-RMS singer
    };

    inline std::string material_name(const material_spec& m) {
        std::string s;
        switch (m.mat) {
        case howl::material::voiced:
            s = "voiced";
            break;
        case howl::material::music:
            s = "music";
            break;
        case howl::material::speech:
            s = "speech";
            break;
        default:
            s = std::string(howl::name(m.mat));
            break;
        }
        return m.aux ? s + "+aux" : s;
    }

    /// The live loop (file comment).
    template <typename Sample>
    class guarded_loop {
      public:
        struct config {
            std::vector<std::vector<double>> paths;  ///< F_m before the change, k_taps each
            std::vector<std::vector<double>> paths2; ///< F_m after it (empty: change_scale x paths)
            double                           change_scale  = 1.0;
            long                             change_block  = -1; ///< < 0: no change
            size_t                           delay         = k_s1;
            double                           gain_db       = 0.0; ///< K
            double                           shift_hz      = 0.0;
            const std::vector<double>*       aux           = nullptr; ///< read cyclically; null = none
            double                           aux_db        = -12.0;
            double                           speaker_limit = 1000.0;
        };

        /// `guard` (may be null: no guard) is attached with set_guard();
        /// `reverb` rides in the reverb slot; `bus_stage` attaches the
        /// guard's post-reverb stage.
        guarded_loop(config cfg, guard_t<Sample>* guard, tap::mu::afc_stage_ref<Sample> reverb = nullptr,
                     bool bus_stage = true)
            : m_cfg(std::move(cfg))
            , m_chain(chain_config(m_cfg))
            , m_guard(guard) {
            const size_t mics = m_cfg.paths.size();
            if (mics == 0 || m_cfg.delay < k_block + k_margin) {
                throw std::invalid_argument("guarded_loop: bad config");
            }
            for (const auto& p : m_cfg.paths) {
                if (p.size() != k_taps) {
                    throw std::invalid_argument("guarded_loop: paths must have k_taps taps");
                }
            }
            m_hist.assign(k_history, 0.0);
            m_y.assign(mics, std::vector<Sample>(k_block, Sample(0)));
            m_ptrs.resize(mics);
            for (size_t m = 0; m < mics; ++m) {
                m_ptrs[m] = m_y[m].data();
            }
            m_acc.assign(k_block, 0.0);
            m_speaker.assign(k_block, Sample(0));
            m_auxb.assign(k_block, Sample(0));
            m_fhat.assign(k_taps, Sample(0));
            m_r.assign(k_taps + k_margin, 0.0);
            m_t = k_history;
            set_gain_db(m_cfg.gain_db);
            m_fwd.shift_hz = m_cfg.shift_hz;
            m_chain.set_decorrelator(&m_fwd);
            m_chain.set_reverb(reverb);
            m_has_reverb = static_cast<bool>(reverb);
            if (guard != nullptr) {
                if (!m_chain.set_guard(guard)) {
                    throw std::invalid_argument("guarded_loop: set_guard refused the guard");
                }
                if (bus_stage) {
                    m_chain.set_safety(guard->bus_stage());
                    m_bus_attached = true;
                }
            }
        }

        guarded_loop(const guarded_loop&)            = delete;
        guarded_loop& operator=(const guarded_loop&) = delete;

        void set_gain_db(double k_db) {
            m_cfg.gain_db = k_db;
            m_fwd.gain    = std::pow(10.0, k_db / 20.0);
        }
        double           gain_db() const { return m_cfg.gain_db; }
        size_t           microphones() const { return m_cfg.paths.size(); }
        chain_t<Sample>& chain() { return m_chain; }
        guard_t<Sample>* guard() const { return m_guard; }
        long             block() const { return m_blk; }
        const config&    cfg() const { return m_cfg; }

        /// Copy the cancellers' state from `other` (a converged loop).
        void load_cancellers(guarded_loop& other) {
            for (size_t m = 0; m < microphones(); ++m) {
                m_chain.canceller(m) = other.m_chain.canceller(m);
            }
        }

        /// One block: x holds microphones() pointers to a block of near end
        /// each. Returns the largest RMS over the mics' residuals e_m (before
        /// the guard's gain; +inf once non-finite).
        double step(const double* const* x) {
            const size_t mics    = microphones();
            const bool   changed = m_cfg.change_block >= 0 && m_blk >= m_cfg.change_block;
            for (size_t m = 0; m < mics; ++m) {
                const auto&  f     = path(m, changed);
                const double scale = (changed && m_cfg.paths2.empty()) ? m_cfg.change_scale : 1.0;
                std::fill(m_acc.begin(), m_acc.end(), 0.0);
                const size_t base = m_t - m_cfg.delay;
                for (size_t k = 0; k < k_taps; ++k) {
                    const double fk = scale * f[k];
                    for (size_t i = 0; i < k_block; ++i) {
                        m_acc[i] += fk * m_hist[(base + i - k) & k_mask];
                    }
                }
                for (size_t i = 0; i < k_block; ++i) {
                    m_y[m][i] = static_cast<Sample>(x[m][i] + m_acc[i]);
                }
            }
            const Sample* aux = nullptr;
            if (m_cfg.aux != nullptr && !m_cfg.aux->empty()) {
                const double g = std::pow(10.0, m_cfg.aux_db / 20.0);
                for (size_t i = 0; i < k_block; ++i) {
                    m_auxb[i] = static_cast<Sample>(g * (*m_cfg.aux)[(m_n + i) % m_cfg.aux->size()]);
                }
                aux = m_auxb.data();
            }
            m_chain.process_block(m_ptrs.data(), aux, m_speaker.data(), k_block);
            const double lim = m_cfg.speaker_limit;
            for (size_t i = 0; i < k_block; ++i) {
                double s = static_cast<double>(m_speaker[i]);
                if (!(s >= -lim)) { // catches NaN too
                    s = -lim;
                }
                if (s > lim) {
                    s = lim;
                }
                m_hist[(m_t + i) & k_mask] = s;
            }
            m_t += k_block;
            m_n += k_block;
            ++m_blk;
            double worst = 0.0;
            for (size_t m = 0; m < mics; ++m) {
                const Sample* e   = m_chain.error_block(m);
                double        acc = 0.0;
                for (size_t i = 0; i < k_block; ++i) {
                    acc += static_cast<double>(e[i]) * static_cast<double>(e[i]);
                }
                const double r = std::sqrt(acc / static_cast<double>(k_block));
                worst          = std::max(worst, std::isfinite(r) ? r : std::numeric_limits<double>::infinity());
            }
            return worst;
        }

        /// RMS of mic m's residual in the last block.
        double error_rms(size_t m) const {
            const Sample* e   = m_chain.error_block(m);
            double        acc = 0.0;
            for (size_t i = 0; i < k_block; ++i) {
                acc += static_cast<double>(e[i]) * static_cast<double>(e[i]);
            }
            return std::sqrt(acc / static_cast<double>(k_block));
        }
        /// RMS of the speaker feed in the last block.
        double speaker_rms() const {
            double acc = 0.0;
            for (size_t i = 0; i < k_block; ++i) {
                acc += static_cast<double>(m_speaker[i]) * static_cast<double>(m_speaker[i]);
            }
            return std::sqrt(acc / static_cast<double>(k_block));
        }

        /// RMS of the chain's voice in the last speaker block (the speaker
        /// minus the backing track).
        double voice_rms() const {
            const bool has_aux = m_cfg.aux != nullptr && !m_cfg.aux->empty();
            double     acc     = 0.0;
            for (size_t i = 0; i < k_block; ++i) {
                const double v = static_cast<double>(m_speaker[i]) - (has_aux ? static_cast<double>(m_auxb[i]) : 0.0);
                acc += v * v;
            }
            return std::sqrt(acc / static_cast<double>(k_block));
        }

        /// Whether the frozen-estimate margin is defined (no shift, no reverb).
        bool margin_defined() const { return m_cfg.shift_hz == 0.0 && !m_has_reverb; }

        /// The frozen-estimate margin (file comment), dB.
        double frozen_margin_db() {
            const bool changed = m_cfg.change_block >= 0 && m_blk >= m_cfg.change_block;
            std::fill(m_r.begin(), m_r.end(), 0.0);
            for (size_t m = 0; m < microphones(); ++m) {
                const double g = mic_gain(m);
                m_chain.canceller(m).copy_impulse_response(m_fhat.data());
                const auto&  f     = path(m, changed);
                const double scale = (changed && m_cfg.paths2.empty()) ? m_cfg.change_scale : 1.0;
                for (size_t i = 0; i < k_taps + k_margin; ++i) {
                    const double fi = (i >= k_margin) ? scale * f[i - k_margin] : 0.0;
                    const double hi = (i < k_taps) ? static_cast<double>(m_fhat[i]) : 0.0;
                    m_r[i] += g * (fi - hi);
                }
            }
            double bus = 0.0;
            if (m_bus_attached && m_guard != nullptr) {
                bus = 20.0 * std::log10(std::max(static_cast<double>(m_guard->bus_gain_block()[k_block - 1]), 1e-30));
            }
            return exact_msg_db(m_r, m_cfg.delay - k_margin, size_t{1} << 15) - m_cfg.gain_db - bus;
        }

        /// Mic m's misalignment over the taps its filter covers, dB.
        double misalignment_db(size_t m) {
            const bool changed = m_cfg.change_block >= 0 && m_blk >= m_cfg.change_block;
            m_chain.canceller(m).copy_impulse_response(m_fhat.data());
            const auto&  f     = path(m, changed);
            const double scale = (changed && m_cfg.paths2.empty()) ? m_cfg.change_scale : 1.0;
            double       num   = 0.0;
            double       den   = 0.0;
            for (size_t i = 0; i + k_margin < k_taps; ++i) {
                const double fi = scale * f[i];
                const double d  = fi - static_cast<double>(m_fhat[i + k_margin]);
                num += d * d;
                den += fi * fi;
            }
            return 10.0 * std::log10(num / den + 1e-300);
        }

      private:
        static constexpr size_t k_history = size_t{1} << 13;
        static constexpr size_t k_mask    = k_history - 1;

        static typename chain_t<Sample>::config chain_config(const config& c) {
            typename chain_t<Sample>::config cc;
            cc.microphones             = c.paths.size();
            cc.canceller               = afc_config<Sample>();
            cc.reference_delay_samples = c.delay - k_block - k_margin;
            cc.sample_rate             = k_fs;
            return cc;
        }

        const std::vector<double>& path(size_t m, bool changed) const {
            return (changed && !m_cfg.paths2.empty()) ? m_cfg.paths2[m] : m_cfg.paths[m];
        }

        double mic_gain(size_t m) const {
            return m_guard != nullptr ? static_cast<double>(m_guard->gain_block(m)[k_block - 1]) : 1.0;
        }

        config                           m_cfg;
        chain_t<Sample>                  m_chain;
        guard_t<Sample>*                 m_guard;
        forward_stage<Sample>            m_fwd;
        bool                             m_has_reverb   = false;
        bool                             m_bus_attached = false;
        std::vector<double>              m_hist; ///< clipped speaker history, a ring
        size_t                           m_t   = 0;
        size_t                           m_n   = 0;
        long                             m_blk = 0;
        std::vector<std::vector<Sample>> m_y;
        std::vector<const Sample*>       m_ptrs;
        std::vector<double>              m_acc;
        std::vector<Sample>              m_speaker;
        std::vector<Sample>              m_auxb;
        std::vector<Sample>              m_fhat;
        std::vector<double>              m_r;
    };

    /// n samples of material m's singer at seed `seed` (howl_materials.h).
    inline std::vector<double> singer(const material_spec& m, size_t n, unsigned seed) {
        return howl::make(m.mat, n, seed);
    }

    /// The backing track: white noise at unit RMS (two_mic_loop.h's), read
    /// cyclically.
    inline std::vector<double> backing_track(unsigned seed) {
        return white_near_end<double>(120000, seed + 777);
    }

} // namespace mutap_test::guard
