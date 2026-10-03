/// @file reverb_rig.h
/// @brief The vendored Dattorro plates behind tap::mu::reverb_mix /
///        shifted_dry_mix as a forward-path stage of decorrelated_loop, and
///        the reverb's own measurements (impulse response, T30, mix level).
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Shared by test_reverb_stage.cpp (the gated rows) and
// test_reverb_stage_sweep.cpp (the MUTAP_SLOW sweep), so a number in
// docs/reverb-afc.md means the same measurement wherever it came from.
// Host-only: it includes the FAUST-generated classes (third_party/faust/).
//
// A rig is one forward path behind the canceller, in the chain's order
// (decorrelator -> reverb):
//
//   reverb       y = (1 - w) x + w r(x)                    reverb_mix
//   bus_shift    y = (1 - w) s(x) + w r(s(x))              frequency_shifter, then reverb_mix
//   dry_shift    y = (1 - w) s(x) + w r(x)                 shifted_dry_mix
//   shift_only   y = s(x)                                  frequency_shifter
//
// r the plate's mono return (L or (L + R) / 2), s the library's SSB
// shifter (tap::mu::frequency_shifter<double>). Every shift row of the
// reverb suites uses the library's shifter, shift_only included, so the
// three shift rows differ only in where the reverb sits. (The karaoke suites'
// shift rows use the loop's exact-ramp oscillator instead; the Hilbert pair
// is the same.)
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

#include "../fixtures/rir_cabin.h"
#include "../fixtures/rir_hall.h"
#include "../fixtures/rir_rehearsal.h"
#include "../fixtures/rir_studio.h"
#include "faust_generated.h"
#include "mutap/fft.h"
#include "mutap/frequency_shifter.h"
#include "mutap/reverb_stage.h"
#include "tap/dsp/math.h"

namespace mutap_test::reverb {

    inline constexpr int    k_fs    = 48000;
    inline constexpr size_t k_block = 64;

    /// Which vendored plate.
    enum class variant {
        shipped, ///< re.dattorro_rev as FAUST 2.88.0 ships it (dattorro.dsp)
        paper    ///< the paper's delay lengths, tank bug fixed (dattorro_paper.dsp)
    };

    enum class topology { reverb, bus_shift, dry_shift, shift_only };

    inline const char* name(variant v) {
        return v == variant::shipped ? "shipped" : "paper";
    }
    inline const char* name(topology t) {
        switch (t) {
        case topology::bus_shift:
            return "bus shift + reverb";
        case topology::dry_shift:
            return "dry-only shift";
        case topology::shift_only:
            return "shift only";
        case topology::reverb:
        default:
            return "reverb";
        }
    }

    /// One reverb configuration.
    struct params {
        variant                plate    = variant::shipped;
        double                 decay    = 0.5;
        double                 damping  = 0.0005;
        double                 wet      = 0.30;
        tap::mu::reverb_return ret      = tap::mu::reverb_return::left;
        topology               topo     = topology::reverb;
        double                 shift_hz = 0.0;
    };

    /// FAUST's init() writes class-static state on some classes; construct
    /// one plate at a time (faust_shim.h).
    inline std::mutex& construction_mutex() {
        static std::mutex m;
        return m;
    }

    /// A rig over one plate class.
    class rig {
      public:
        explicit rig(const params& p)
            : m_p(p) {
            tap::mu::frequency_shifter<double>::config sc;
            sc.sample_rate = k_fs;
            sc.shift_hz    = p.shift_hz;
            m_shifter      = std::make_unique<tap::mu::frequency_shifter<double>>(sc);
            m_tmp.resize(k_block);
            if (p.topo != topology::shift_only) {
                const std::lock_guard<std::mutex> lock(construction_mutex());
                if (p.plate == variant::shipped) {
                    m_impl = std::make_unique<impl<mutap_faust::dattorro_f64>>(p, *m_shifter);
                }
                else {
                    m_impl = std::make_unique<impl<mutap_faust::dattorro_paper_f64>>(p, *m_shifter);
                }
            }
        }

        void process_block(const double* in, double* out, size_t n) {
            switch (m_p.topo) {
            case topology::shift_only:
                m_shifter->process_block(in, out, n);
                return;
            case topology::bus_shift:
                m_shifter->process_block(in, m_tmp.data(), n);
                m_impl->mix(m_tmp.data(), out, n);
                return;
            case topology::dry_shift:
                m_impl->dry_mix(in, out, n);
                return;
            case topology::reverb:
            default:
                m_impl->mix(in, out, n);
                return;
            }
        }

        /// Silence the plate and restart the shifter (phase 0).
        void reset() {
            if (m_impl) {
                m_impl->clear();
            }
            m_shifter->reset();
        }

        const params& parameters() const { return m_p; }

      private:
        struct base {
            base()                                                        = default;
            base(const base&)                                             = delete;
            base& operator=(const base&)                                  = delete;
            virtual ~base()                                               = default;
            virtual void mix(const double* in, double* out, size_t n)     = 0;
            virtual void dry_mix(const double* in, double* out, size_t n) = 0;
            virtual void clear()                                          = 0;
        };

        template <class Dsp>
        struct impl final : base {
            using block_t = mutap_faust::faust_block<Dsp, double>;
            using mix_t   = tap::mu::reverb_mix<double, block_t>;
            using dry_t   = tap::mu::shifted_dry_mix<double, tap::mu::frequency_shifter<double>, block_t>;

            impl(const params& p, tap::mu::frequency_shifter<double>& shifter)
                : plate(k_fs)
                , cfg(make_config(p))
                , m(plate, cfg)
                , d(shifter, plate, cfg) {
                plate.set("decay", p.decay);
                plate.set("damping", p.damping);
            }
            static typename mix_t::config make_config(const params& p) {
                typename mix_t::config c;
                c.block_size = k_block;
                c.channels   = 2;
                c.wet        = p.wet;
                c.mode       = p.ret;
                return c;
            }
            void mix(const double* in, double* out, size_t n) override { m.process_block(in, out, n); }
            void dry_mix(const double* in, double* out, size_t n) override { d.process_block(in, out, n); }
            void clear() override { plate.clear(); }

            block_t                plate;
            typename mix_t::config cfg;
            mix_t                  m;
            dry_t                  d;
        };

        params                                              m_p;
        std::unique_ptr<base>                               m_impl;
        std::unique_ptr<tap::mu::frequency_shifter<double>> m_shifter;
        std::vector<double>                                 m_tmp;
    };

    /// The plate's impulse response, L and R, at 48 kHz: run for up to
    /// `cap_s` seconds and truncate one past the last sample where max(|L|,
    /// |R|) >= peak * 10^(-90/20) (phase 0's dattorro_ir.cpp rule).
    struct impulse_response {
        std::vector<double> l;
        std::vector<double> r;
        bool                capped = false; ///< the tail had not fallen 90 dB by cap_s
    };

    inline impulse_response plate_ir(variant v, double decay, double damping, double cap_s = 20.0) {
        auto run = [&](auto& plate) {
            plate.set("decay", decay);
            plate.set("damping", damping);
            const auto       n = static_cast<size_t>(cap_s * k_fs);
            impulse_response h;
            h.l.assign(n, 0.0);
            h.r.assign(n, 0.0);
            std::vector<double> in(k_block, 0.0);
            for (size_t i = 0; i < n; i += k_block) {
                const size_t b = std::min(k_block, n - i);
                std::fill(in.begin(), in.end(), 0.0);
                if (i == 0) {
                    in[0] = 1.0;
                }
                double* outs[2] = {&h.l[i], &h.r[i]};
                plate.process_mono(in.data(), outs, static_cast<int>(b));
            }
            double peak = 0.0;
            for (size_t i = 0; i < n; ++i) {
                peak = std::max({peak, std::abs(h.l[i]), std::abs(h.r[i])});
            }
            const double thr = peak * std::pow(10.0, -90.0 / 20.0);
            size_t       len = 0;
            for (size_t i = n; i-- > 0;) {
                if (std::abs(h.l[i]) >= thr || std::abs(h.r[i]) >= thr) {
                    len = i + 1;
                    break;
                }
            }
            h.capped = len == n;
            h.l.resize(len);
            h.r.resize(len);
            return h;
        };
        std::unique_lock<std::mutex> lock(construction_mutex());
        if (v == variant::shipped) {
            mutap_faust::faust_block<mutap_faust::dattorro_f64, double> plate(k_fs);
            lock.unlock();
            return run(plate);
        }
        mutap_faust::faust_block<mutap_faust::dattorro_paper_f64, double> plate(k_fs);
        lock.unlock();
        return run(plate);
    }

    /// The mono return of an impulse response: L or (L + R) / 2.
    inline std::vector<double> mono_return(const impulse_response& h, tap::mu::reverb_return ret) {
        if (ret == tap::mu::reverb_return::left) {
            return h.l;
        }
        std::vector<double> m(h.l.size());
        for (size_t i = 0; i < m.size(); ++i) {
            m[i] = 0.5 * (h.l[i] + h.r[i]);
        }
        return m;
    }

    /// T30 by Schroeder backward integration: a least-squares line through
    /// the energy decay curve from its first -5 dB to its first -35 dB
    /// sample (test_faust_vendored.cpp's method).
    inline double t30_seconds(const std::vector<double>& h) {
        std::vector<double> edc(h.size());
        double              acc = 0.0;
        for (size_t i = h.size(); i-- > 0;) {
            acc += h[i] * h[i];
            edc[i] = acc;
        }
        size_t lo = 0;
        while (lo < h.size() && tap::dsp::power_db(edc[lo] / edc[0]) > -5.0) {
            ++lo;
        }
        size_t hi = lo;
        while (hi < h.size() && tap::dsp::power_db(edc[hi] / edc[0]) > -35.0) {
            ++hi;
        }
        double sx  = 0.0;
        double sy  = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (size_t i = lo; i <= hi && i < h.size(); ++i) {
            const double t = static_cast<double>(i) / k_fs;
            const double y = tap::dsp::power_db(edc[i] / edc[0]);
            sx += t;
            sy += y;
            sxx += t * t;
            sxy += t * y;
        }
        const auto   m     = static_cast<double>(hi - lo + 1);
        const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx); // dB per second
        return -60.0 / slope;
    }

    /// The mix's broadband level change for white input, dB: 10 log10 of
    /// the energy of (1 - w) delta + w r. Negative = the mix is quieter
    /// than the dry bus (the crossfade gives up more level than the tail
    /// adds back).
    inline double mix_energy_db(const std::vector<double>& r, double wet) {
        double e = 0.0;
        for (size_t i = 0; i < r.size(); ++i) {
            double h = wet * r[i];
            if (i == 0) {
                h += 1.0 - wet;
            }
            e += h * h;
        }
        return tap::dsp::power_db(e);
    }

    // ------------------------------------------------- the bare-loop analysis
    //
    // Phase 0's method (_afc_poc/phase0-data/dattorro/dattorro_msg.py,
    // reproduced): the loop L(w) = K e^{-jwd} H(w) F(w) with no canceller,
    // H = (1 - w) + w R the mix, F a room fixture. Two limits per loop:
    //   bound  = -20 log10 max |H F|           (magnitude only; any delay)
    //   exact  = -20 log10 of the largest positive real value of
    //            e^{-jwd} H F where its phase crosses 0 mod 2 pi (DC and
    //            Nyquist included when positive): the first K at which
    //            K L = 1, the Nyquist crossing (closed_loop.h's
    //            exact_msg_db, generalized to a mix)
    // and the cost of the mix = dry limit - mix limit (positive = less
    // stable gain). Spectra on an rfft grid with linear interpolation of
    // the crossings between bins.

    /// X(w_k) = sum_n x[n] e^{-j w_k n}, k = 0 .. n/2, of x zero-padded to n
    /// (a power of two).
    inline std::vector<std::complex<double>> spectrum(const std::vector<double>& x, size_t n) {
        tap::mu::real_fft   fft(n);
        std::vector<double> buf(n, 0.0);
        for (size_t i = 0; i < x.size() && i < n; ++i) {
            buf[i] = x[i];
        }
        fft.forward_inplace(buf.data());
        // Ooura packing with the exp(+i) sign (closed_loop.h): X(w_k) =
        // buf[2k] - j buf[2k+1]; DC and Nyquist are real, in buf[0], buf[1].
        std::vector<std::complex<double>> X(n / 2 + 1);
        X[0]     = {buf[0], 0.0};
        X[n / 2] = {buf[1], 0.0};
        for (size_t k = 1; k < n / 2; ++k) {
            X[k] = {buf[2 * k], -buf[2 * k + 1]};
        }
        return X;
    }

    struct loop_limits {
        double bound_db = 0.0;
        double exact_db = 0.0;
    };

    /// The two limits of e^{-jwd} H F with H = (1 - wet) + wet R (R null:
    /// H = 1, the dry loop), every spectrum on the same n-point grid.
    inline loop_limits analytic_limits(const std::vector<std::complex<double>>& F,
                                       const std::vector<std::complex<double>>* R, double wet, size_t d) {
        const size_t half = F.size() - 1;
        const size_t n    = 2 * half;
        auto         at   = [&](size_t k) {
            std::complex<double> h = F[k];
            if (R != nullptr) {
                h *= (1.0 - wet) + wet * (*R)[k];
            }
            const auto   m  = static_cast<size_t>((static_cast<std::uint64_t>(k) * d) % n);
            const double ph = -2.0 * std::numbers::pi * static_cast<double>(m) / static_cast<double>(n);
            return std::pair<std::complex<double>, double>{h * std::polar(1.0, ph), std::abs(h)};
        };
        double peak   = 0.0;
        auto [l0, a0] = at(0);
        peak          = a0;
        double best   = std::max(l0.real(), 0.0);
        for (size_t k = 0; k < half; ++k) {
            auto [l1, a1] = at(k + 1);
            peak          = std::max(peak, a1);
            if (std::signbit(l0.imag()) != std::signbit(l1.imag())) {
                const double t = l0.imag() / (l0.imag() - l1.imag());
                best           = std::max(best, l0.real() + t * (l1.real() - l0.real()));
            }
            l0 = l1;
        }
        best = std::max(best, l0.real()); // Nyquist
        return {-20.0 * std::log10(peak), -20.0 * std::log10(best)};
    }

    /// A room fixture as phase 0's bare-loop analysis loaded it: all 4096
    /// taps, as stored (not band-limited). "studio", "rehearsal", "hall";
    /// anything else is the cabin.
    inline std::vector<double> fixture_4096(const std::string& name) {
        const float* f = fixtures::k_rir_cabin;
        if (name == "studio") {
            f = fixtures::k_rir_studio;
        }
        else if (name == "rehearsal") {
            f = fixtures::k_rir_rehearsal;
        }
        else if (name == "hall") {
            f = fixtures::k_rir_hall;
        }
        return {f, f + 4096};
    }

    /// Phase 0's median of the four fixtures: numpy's, the mean of the
    /// middle two.
    inline double median_of_four(std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return 0.5 * (v[1] + v[2]);
    }

    /// The smallest power of two >= x.
    inline size_t next_pow2(size_t x) {
        size_t n = 1;
        while (n < x) {
            n <<= 1U;
        }
        return n;
    }

} // namespace mutap_test::reverb
