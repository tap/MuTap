/// @file two_mic_loop.h
/// @brief The anti-howl PoC's multi-microphone closed loop (the two-mic
///        go/no-go experiment, POC-PLAN §2.5 / D11): M mics, one canceller
///        each on the ONE shared speaker reference, a voice bus, a mono
///        speaker feed heard by every mic through its own path.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// HOST-ONLY (tests/CMakeLists.txt): double-precision acoustic measurement,
// every number a gain bisection.
//
// THE LOOP (M mics; the same timeline as afc_chain's, tap/MuTap#68):
//
//   mic_m[n]  = x_m[n] + sum_k F_m[k] clip(speaker[n - d - k])
//   e_m       = canceller_m(u, mic_m)                   (e_m = mic_m open loop)
//   bus       = sum_m e_m
//   speaker   = K * decorrelate(bus) + aux              (the mono feed; every mic hears it)
//   u[n]      = speaker[n - block - reference_delay]    (the shared reference)
//
// x_m is mic m's near end: its own singer plus the other singers' leakage
// (near_end_at(), below), precomputed because it does not depend on the loop.
// d, the ELECTRICAL delay (>= block), is where the speaker feed written now
// reaches the room: the loop from the bus back to the bus is K z^-d sum_m
// F_m, so the dry (no-canceller, plain) loop's phase-exact limit is
// exact_msg_db(sum_m F_m, d). The reference lags the speaker feed by one
// block (the chain writes the speaker block after cancelling the mic block,
// so the block it writes now is the reference of the next one at the
// earliest) plus reference_delay; aligned as the chain aligns it,
// reference_delay = d - block - margin, and each canceller's taps model
// F_m delayed by the margin. The decorrelator is the harness's
// iir_ssb_shifter (decorrelated_loop.h) on the bus; the aux feed (a backing
// track) is summed after the gain, so the reference carries it.
//
// THE MEASUREMENT (open_limit_db() and chain_limit_db(), below;
// karaoke_asg.h's rules):
// converge the M cancellers for `converge_blocks` of one seed of the
// material at exact_msg_db(sum F, d) - 6 dB, then bisect the forward gain on
// a second seed (seed + 10), a fresh copy of the converged cancellers and a
// fresh loop per probe; howling = a bus block at RMS >= 100 (closed_loop.h's
// 40 dB rule). ASG = that limit - the dry loop's limit bisected on the SAME
// probe material and length (plain, no aux, no cancellers, every mic at
// unit gain). M = 1 is the single-mic reference of the same room, delay,
// singer and forward path.
#pragma once

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
#include "karaoke_asg.h"
#include "mutap/fd_kalman.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"
#include "rooms.h"

namespace mutap_test {

    /// M mics on one shared speaker reference (see the file comment).
    template <typename Sample>
    class multi_mic_loop {
      public:
        struct config {
            std::vector<std::vector<Sample>> paths;                   ///< F_m, one per mic, equal length
            size_t                           block_size       = 64;   ///< the cancellers'
            size_t                           electrical_delay = 480;  ///< d, samples; >= block_size
            size_t                           reference_delay  = 0;    ///< u lags the speaker by block + this
            double                           forward_gain_db  = 0.0;  ///< K
            double                           speaker_limit    = 1000; ///< hard clip on what the room hears
            double                           sample_rate      = 48000.0;
            bool                             shift            = false; ///< SSB shift on the bus
            double                           shift_hz         = 2.0;
            const std::vector<Sample>*       aux              = nullptr; ///< read cyclically; null = none
            double                           aux_gain         = 1.0;
        };

        explicit multi_mic_loop(config cfg)
            : m_cfg(std::move(cfg)) {
            const size_t mics = m_cfg.paths.size();
            if (mics == 0 || m_cfg.paths.front().empty() || m_cfg.electrical_delay < m_cfg.block_size) {
                throw std::invalid_argument("multi_mic_loop: bad config");
            }
            m_lf = m_cfg.paths.front().size();
            for (const auto& p : m_cfg.paths) {
                if (p.size() != m_lf) {
                    throw std::invalid_argument("multi_mic_loop: paths must have equal length");
                }
            }
            size_t h = 1;
            while (h < m_cfg.electrical_delay + m_cfg.reference_delay + m_lf + 4 * m_cfg.block_size) {
                h *= 2;
            }
            m_speaker.assign(h, Sample(0));
            m_mask = h - 1;
            m_t    = h; // one history length in: reads never go negative
            m_work.assign(m_lf - 1 + m_cfg.block_size, 0.0);
            m_acc.assign(m_cfg.block_size, 0.0);
            m_u.assign(m_cfg.block_size, Sample(0));
            m_y.assign(mics, std::vector<Sample>(m_cfg.block_size, Sample(0)));
            m_e.assign(mics, std::vector<Sample>(m_cfg.block_size, Sample(0)));
            m_bus.assign(m_cfg.block_size, Sample(0));
            set_forward_gain_db(m_cfg.forward_gain_db);
        }

        void set_forward_gain_db(double k_db) {
            m_cfg.forward_gain_db = k_db;
            m_gain                = std::pow(10.0, k_db / 20.0);
        }
        size_t block_size() const { return m_cfg.block_size; }
        size_t microphones() const { return m_cfg.paths.size(); }

        /// One block. `x` holds microphones() pointers to one block of near
        /// end each; `cancellers` is null (open loop) or points to
        /// microphones() cancellers (any type with process_block(u, y, e)).
        /// Returns the bus RMS (+inf once non-finite).
        template <typename Canceller>
        double step(const Sample* const* x, std::vector<Canceller>* cancellers) {
            const size_t b    = m_cfg.block_size;
            const size_t lf   = m_lf;
            const size_t mics = m_cfg.paths.size();
            const double lim  = m_cfg.speaker_limit;

            // What the room hears for this block's mics: the clipped speaker
            // feed from d + lf - 1 samples back up to d back (d >= b: all
            // written already), oldest first.
            const size_t first = m_t - m_cfg.electrical_delay - (lf - 1);
            for (size_t i = 0; i < lf - 1 + b; ++i) {
                double s = static_cast<double>(m_speaker[(first + i) & m_mask]);
                if (!(s >= -lim)) { // catches NaN too
                    s = -lim;
                }
                if (s > lim) {
                    s = lim;
                }
                m_work[i] = s;
            }
            // y_m[i] = x_m[i] + sum_k F_m[k] work[lf - 1 + i - k]: the k loop
            // outermost so the i loop vectorizes; each output still sums in
            // k order.
            for (size_t m = 0; m < mics; ++m) {
                const auto& f = m_cfg.paths[m];
                for (size_t i = 0; i < b; ++i) {
                    m_acc[i] = 0.0;
                }
                for (size_t k = 0; k < lf; ++k) {
                    const double  fk = static_cast<double>(f[k]);
                    const double* w  = &m_work[lf - 1 - k];
                    for (size_t i = 0; i < b; ++i) {
                        m_acc[i] += fk * w[i];
                    }
                }
                for (size_t i = 0; i < b; ++i) {
                    m_y[m][i] = static_cast<Sample>(static_cast<double>(x[m][i]) + m_acc[i]);
                }
            }

            // The shared reference: the speaker feed block + reference_delay back.
            const size_t ref0 = m_t - b - m_cfg.reference_delay;
            for (size_t i = 0; i < b; ++i) {
                m_u[i] = m_speaker[(ref0 + i) & m_mask];
            }

            for (size_t m = 0; m < mics; ++m) {
                if (cancellers != nullptr) {
                    (*cancellers)[m].process_block(m_u.data(), m_y[m].data(), m_e[m].data());
                }
                else {
                    m_e[m] = m_y[m];
                }
            }

            double rms = 0.0;
            for (size_t i = 0; i < b; ++i) {
                Sample bus = m_e[0][i];
                for (size_t m = 1; m < mics; ++m) {
                    bus += m_e[m][i];
                }
                m_bus[i] = bus;
                rms += static_cast<double>(bus) * static_cast<double>(bus);

                double fwd = static_cast<double>(bus);
                if (m_cfg.shift) {
                    const double theta =
                        2.0 * std::numbers::pi * m_cfg.shift_hz * static_cast<double>(m_n) / m_cfg.sample_rate;
                    fwd = static_cast<double>(static_cast<Sample>(m_shifter.process(fwd, theta)));
                }
                double s = m_gain * fwd;
                if (m_cfg.aux != nullptr && !m_cfg.aux->empty()) {
                    s += m_cfg.aux_gain * static_cast<double>((*m_cfg.aux)[m_n % m_cfg.aux->size()]);
                }
                m_speaker[(m_t + i) & m_mask] = static_cast<Sample>(s);
                ++m_n;
            }
            m_t += b;
            rms = std::sqrt(rms / static_cast<double>(b));
            return std::isfinite(rms) ? rms : std::numeric_limits<double>::infinity();
        }

        const std::vector<Sample>& mic_block(size_t m) const { return m_y[m]; }
        const std::vector<Sample>& error_block(size_t m) const { return m_e[m]; }
        const std::vector<Sample>& bus_block() const { return m_bus; }
        const std::vector<Sample>& reference_block() const { return m_u; }

      private:
        config                           m_cfg;
        double                           m_gain = 1.0;
        size_t                           m_lf   = 0;
        std::vector<Sample>              m_speaker; ///< the speaker feed as written (unclipped), a ring
        size_t                           m_mask = 0;
        size_t                           m_t    = 0; ///< ring clock
        size_t                           m_n    = 0; ///< samples played (shifter phase, aux index)
        std::vector<double>              m_work;     ///< clipped feed window for the path convolutions
        std::vector<double>              m_acc;
        std::vector<Sample>              m_u;
        std::vector<std::vector<Sample>> m_y;
        std::vector<std::vector<Sample>> m_e;
        std::vector<Sample>              m_bus;
        iir_ssb_shifter                  m_shifter;
    };

    namespace two_mic {

        inline constexpr size_t k_block  = 64;
        inline constexpr size_t k_taps   = 1024;
        inline constexpr size_t k_parts  = k_taps / k_block;
        inline constexpr size_t k_margin = 32; ///< the reference's jitter margin (afc_chain's tests)
        inline constexpr double k_fs     = 48000.0;
        inline constexpr size_t k_s1     = 480; ///< 10 ms
        inline constexpr size_t k_s3     = 960; ///< 20 ms

        /// The chain's aligned reference delay for electrical delay d.
        constexpr size_t aligned_reference_delay(size_t d) {
            return d - k_block - k_margin;
        }

        constexpr size_t probe_blocks(double seconds) {
            return static_cast<size_t>(seconds * k_fs / static_cast<double>(k_block));
        }

        template <typename Sample>
        using afc = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;

        template <typename Sample>
        typename afc<Sample>::config afc_config() {
            typename afc<Sample>::config c;
            c.fdaf.block_size = k_block;
            c.fdaf.partitions = k_parts;
            return c;
        }

        /// Where the second mic of a fixture room reads the room: taps
        /// [256, 1280) of the 4096-tap image-source response (5.3 ms on:
        /// the early reflections and on, no direct path).
        inline constexpr size_t k_second_window = 256;

        /// Taps [offset, offset + k_taps) of a 4096-tap fixture at unit
        /// energy, band-limited (offset 0 is karaoke::room(name)).
        inline std::vector<double> fixture_window(const float* rir, size_t offset) {
            std::vector<double> f(rir + offset, rir + offset + k_taps);
            double              energy = 0.0;
            for (const double v : f) {
                energy += v * v;
            }
            for (auto& v : f) {
                v /= std::sqrt(energy);
            }
            return band_limited(f);
        }

        /// A room pair: two mic positions of one room family, band-limited,
        /// 1024 taps each.
        ///   "cabin", "studio", "rehearsal", "hall": one image-source fixture
        ///       at two truncations, taps [0, 1024) and [256, 1280);
        ///   "mtA+mtB": random_decaying_rir seeds A and B.
        inline std::vector<std::vector<double>> room_pair(const std::string& name) {
            const auto plus = name.find('+');
            if (name.rfind("mt", 0) == 0 && plus != std::string::npos) {
                return {karaoke::room(name.substr(0, plus)), karaoke::room(name.substr(plus + 1))};
            }
            const float* rir = nullptr;
            if (name == "cabin") {
                rir = fixtures::k_rir_cabin;
            }
            else if (name == "studio") {
                rir = fixtures::k_rir_studio;
            }
            else if (name == "rehearsal") {
                rir = fixtures::k_rir_rehearsal;
            }
            else if (name == "hall") {
                rir = fixtures::k_rir_hall;
            }
            else {
                throw std::invalid_argument("two_mic::room_pair: unknown pair " + name);
            }
            return {fixture_window(rir, 0), fixture_window(rir, k_second_window)};
        }

        /// What the singers sing. NOTE: voiced_near_end puts its pulses at
        /// n % period == 0 whatever the seed (the seed moves only the -40 dB
        /// noise floor), so the unison pair is two sample-aligned copies of
        /// one note: the coherent worst case. `detuned` is a unison that
        /// beats through every relative phase (1.9 Hz).
        enum class singers {
            unison,    ///< voiced_near_end at a 160-sample period (300 Hz) for both
            separated, ///< 160 (300 Hz) and 240 (200 Hz)
            speech,    ///< ar_near_end for both
            detuned    ///< 160 and 161 (300 and 298.1 Hz): a unison that beats
        };

        inline const char* name(singers s) {
            switch (s) {
            case singers::unison:
                return "unison";
            case singers::separated:
                return "separated";
            case singers::speech:
                return "speech";
            case singers::detuned:
            default:
                return "detuned";
            }
        }

        /// Singer `j` (0 or 1): `blocks` blocks of unit-RMS material at
        /// `seed` + 1000 j.
        inline std::vector<double> singer(singers s, size_t j, size_t blocks, unsigned seed) {
            const size_t   n  = blocks * k_block;
            const unsigned sj = seed + 1000U * static_cast<unsigned>(j);
            switch (s) {
            case singers::speech:
                return ar_near_end<double>(n, sj);
            case singers::separated:
                return voiced_near_end<double>(n, sj, j == 0 ? 160 : 240);
            case singers::detuned:
                return voiced_near_end<double>(n, sj, j == 0 ? 160 : 161);
            case singers::unison:
            default:
                return voiced_near_end<double>(n, sj, 160);
            }
        }

        /// The leakage path from singer j to mic m (j != m): a direct arrival
        /// at `delay` samples and a 256-tap decaying random tail 6 dB below
        /// it, unit energy. Mic 0 hears singer 1 at 96 samples (2 ms, ~0.7 m),
        /// mic 1 hears singer 0 at 144 (3 ms, ~1 m); different tail seeds.
        inline std::vector<double> leakage_path(size_t mic) {
            const size_t        delay = (mic == 0) ? 96 : 144;
            const auto          tail  = random_decaying_rir<double>(256, 500U + static_cast<unsigned>(mic));
            std::vector<double> h(delay + 1 + tail.size(), 0.0);
            h[delay]                = 1.0;
            const double tail_scale = std::pow(10.0, -6.0 / 20.0); // tail energy 6 dB below the direct
            for (size_t k = 0; k < tail.size(); ++k) {
                h[delay + 1 + k] = tail_scale * tail[k];
            }
            double energy = 0.0;
            for (const double v : h) {
                energy += v * v;
            }
            for (auto& v : h) {
                v /= std::sqrt(energy);
            }
            return h;
        }

        /// Mic m's near end: its singer plus the other singer through
        /// leakage_path(m), scaled so the leaked signal's RMS sits
        /// `leak_db` below the singer's own (on this signal). leak_db <= -200
        /// is no leakage.
        inline std::vector<double> near_end_at(size_t mic, const std::vector<double>& own,
                                               const std::vector<double>& other, double leak_db) {
            std::vector<double> x = own;
            if (leak_db <= -200.0) {
                return x;
            }
            const auto          h = leakage_path(mic);
            std::vector<double> l(own.size(), 0.0);
            for (size_t n = 0; n < own.size(); ++n) {
                double acc = 0.0;
                for (size_t k = 0; k < h.size() && k <= n; ++k) {
                    acc += h[k] * other[n - k];
                }
                l[n] = acc;
            }
            double e_own  = 0.0;
            double e_leak = 0.0;
            for (size_t n = 0; n < own.size(); ++n) {
                e_own += own[n] * own[n];
                e_leak += l[n] * l[n];
            }
            const double g = std::pow(10.0, leak_db / 20.0) * std::sqrt(e_own / e_leak);
            for (size_t n = 0; n < own.size(); ++n) {
                x[n] += g * l[n];
            }
            return x;
        }

        /// One measurement's loop and material. paths.size() is M; conv and
        /// probe hold M near ends each (already mixed with leakage).
        template <typename Sample>
        struct scenario {
            std::vector<std::vector<Sample>> paths;
            std::vector<std::vector<Sample>> conv;
            std::vector<std::vector<Sample>> probe;
            size_t                           delay    = k_s1;
            bool                             shift    = false;
            double                           shift_hz = 2.0;
            bool                             use_aux  = false; ///< the backing track on
            std::vector<Sample>              aux;              ///< the backing track (read cyclically)
            double                           aux_db = -12.0;   ///< its level re the unit-RMS singer
        };

        template <typename Sample>
        std::vector<Sample> as(const std::vector<double>& v) {
            std::vector<Sample> out(v.size());
            for (size_t i = 0; i < v.size(); ++i) {
                out[i] = static_cast<Sample>(v[i]);
            }
            return out;
        }

        /// One experimental condition.
        struct condition {
            size_t  delay    = k_s1;
            singers who      = singers::unison;
            double  leak_db  = -10.0; ///< <= -200: no cross-leakage
            bool    shift    = false; ///< SSB shift on the bus
            double  shift_hz = 2.0;
            bool    aux      = false; ///< white backing track at aux_db
            double  aux_db   = -12.0;
        };

        /// The scenario of `c` on `paths` at seed `seed`: both mics (mic <
        /// 0) or mic `mic` alone (its own singer, its own path, no second
        /// singer and so no leakage). Singer j's convergence material is
        /// seed + 1000 j, its probe seed + 10 + 1000 j; the backing track is
        /// white_near_end at seed + 777.
        template <typename Sample>
        scenario<Sample> make_scenario(const std::vector<std::vector<double>>& paths, const condition& c, unsigned seed,
                                       size_t converge_blocks, size_t probe_blocks, int mic = -1) {
            scenario<Sample> sc;
            sc.delay    = c.delay;
            sc.shift    = c.shift;
            sc.shift_hz = c.shift_hz;
            sc.use_aux  = c.aux;
            sc.aux_db   = c.aux_db;
            if (c.aux) {
                sc.aux = as<Sample>(white_near_end<double>(120000, seed + 777));
            }
            const auto s0c = singer(c.who, 0, converge_blocks, seed);
            const auto s1c = singer(c.who, 1, converge_blocks, seed);
            const auto s0p = singer(c.who, 0, probe_blocks, seed + 10);
            const auto s1p = singer(c.who, 1, probe_blocks, seed + 10);
            if (mic < 0) {
                sc.paths = {as<Sample>(paths[0]), as<Sample>(paths[1])};
                sc.conv  = {as<Sample>(near_end_at(0, s0c, s1c, c.leak_db)),
                            as<Sample>(near_end_at(1, s1c, s0c, c.leak_db))};
                sc.probe = {as<Sample>(near_end_at(0, s0p, s1p, c.leak_db)),
                            as<Sample>(near_end_at(1, s1p, s0p, c.leak_db))};
            }
            else {
                const auto m = static_cast<size_t>(mic);
                sc.paths     = {as<Sample>(paths[m])};
                sc.conv      = {as<Sample>(m == 0 ? s0c : s1c)};
                sc.probe     = {as<Sample>(m == 0 ? s0p : s1p)};
            }
            return sc;
        }

        struct protocol {
            size_t converge_blocks = 1500;
            double tol_db          = 0.1;
            double open_lo = -10.0, open_hi = 10.0;   ///< dB re exact_msg_db(sum F, d)
            double chain_lo = -15.0, chain_hi = 30.0; ///< dB re exact_msg_db(sum F, d)
        };

        template <typename Sample>
        std::vector<Sample> path_sum(const std::vector<std::vector<Sample>>& paths) {
            std::vector<Sample> s(paths.front().size(), Sample(0));
            for (const auto& p : paths) {
                for (size_t i = 0; i < s.size(); ++i) {
                    s[i] += p[i];
                }
            }
            return s;
        }

        /// The dry loop's phase-exact limit: exact_msg_db(sum F, d).
        template <typename Sample>
        double exact_db(const scenario<Sample>& sc) {
            return exact_msg_db(path_sum(sc.paths), sc.delay);
        }

        template <typename Sample>
        typename multi_mic_loop<Sample>::config loop_config(const scenario<Sample>& sc, bool dry) {
            typename multi_mic_loop<Sample>::config c;
            c.paths            = sc.paths;
            c.block_size       = k_block;
            c.electrical_delay = sc.delay;
            c.reference_delay  = aligned_reference_delay(sc.delay);
            c.shift            = !dry && sc.shift;
            c.shift_hz         = sc.shift_hz;
            if (!dry && sc.use_aux) {
                c.aux      = &sc.aux;
                c.aux_gain = std::pow(10.0, sc.aux_db / 20.0);
            }
            return c;
        }

        /// Does the loop howl at `gain_db` on `x` (M near ends)? Fresh copies
        /// of `cancellers` (null: open loop) and a fresh loop.
        template <typename Sample, typename Canceller>
        bool howls(typename multi_mic_loop<Sample>::config cfg, const std::vector<Canceller>* cancellers,
                   const std::vector<std::vector<Sample>>& x, double gain_db) {
            cfg.forward_gain_db = gain_db;
            multi_mic_loop<Sample> loop(std::move(cfg));
            std::vector<Canceller> c;
            if (cancellers != nullptr) {
                c = *cancellers;
            }
            std::vector<const Sample*> p(x.size());
            const size_t               blocks = x.front().size() / k_block;
            for (size_t blk = 0; blk < blocks; ++blk) {
                for (size_t m = 0; m < x.size(); ++m) {
                    p[m] = &x[m][blk * k_block];
                }
                if (loop.step(p.data(), cancellers != nullptr ? &c : nullptr) >= 100.0) {
                    return true;
                }
            }
            return false;
        }

        template <typename F>
        double bisect(F howls_at, double lo, double hi, double tol) {
            if (howls_at(lo)) {
                return lo; // the bracket's floor
            }
            while (hi - lo > tol) {
                const double mid = 0.5 * (lo + hi);
                if (howls_at(mid)) {
                    hi = mid;
                }
                else {
                    lo = mid;
                }
            }
            return lo;
        }

        /// The dry loop (no cancellers, plain, no aux) bisected on sc.probe.
        template <typename Sample>
        double open_limit_db(const scenario<Sample>& sc, const protocol& p) {
            const double exact = exact_db(sc);
            const auto   cfg   = loop_config(sc, true);
            return bisect([&](double g) { return howls<Sample, afc<Sample>>(cfg, nullptr, sc.probe, g); },
                          exact + p.open_lo, exact + p.open_hi, p.tol_db);
        }

        /// Converge M cancellers in the loop at exact - 6 dB on sc.conv.
        template <typename Sample>
        std::vector<afc<Sample>> converged(const scenario<Sample>& sc, const protocol& p) {
            std::vector<afc<Sample>> c(sc.paths.size(), afc<Sample>(afc_config<Sample>()));
            auto                     cfg = loop_config(sc, false);
            cfg.forward_gain_db          = exact_db(sc) - 6.0;
            multi_mic_loop<Sample>     loop(cfg);
            std::vector<const Sample*> x(sc.paths.size());
            for (size_t blk = 0; blk < p.converge_blocks; ++blk) {
                for (size_t m = 0; m < x.size(); ++m) {
                    x[m] = &sc.conv[m][blk * k_block];
                }
                loop.step(x.data(), &c);
            }
            return c;
        }

        /// The converged chain's limit, bisected on sc.probe.
        template <typename Sample>
        double chain_limit_db(const scenario<Sample>& sc, const protocol& p) {
            const auto   c     = converged(sc, p);
            const double exact = exact_db(sc);
            const auto   cfg   = loop_config(sc, false);
            return bisect([&](double g) { return howls<Sample>(cfg, &c, sc.probe, g); }, exact + p.chain_lo,
                          exact + p.chain_hi, p.tol_db);
        }

    } // namespace two_mic

} // namespace mutap_test
