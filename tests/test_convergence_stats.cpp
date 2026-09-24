// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The canceller's two raw convergence statistics, from the phase 0
// convergence-indicator experiment (1008 closed-loop runs; the library
// headers carry what it measured):
//
//   A' = partitioned_fdkf::uncertainty_ratio(), sum P / sum P(reset) — the
//        core's own identification progress (pem_afc forwards it);
//   D  = pem_afc::shadow_residual_ratio(), the smoothed prewhitened residual
//        power of the main core over that of a 2-partition shadow adapting
//        on the same prewhitened pair — the walk detector.
//
// The loop is the experiment's: block 64, forward delay 480 at 48 kHz,
// y = v + F u at sample resolution in double (partitioned FFT convolution),
// u = clip(G e(n - 480) + aux), G = the exact-Nyquist open-loop MSG - 6 dB,
// fixed. Every path is a fixture band-limited by the loudspeaker model
// (support/loudspeaker_band.h). The canceller is pem_afc on the Kalman core,
// 16 partitions (1024 taps), default config. Material: voiced near end
// (a held 300 Hz pitch) plus a backing track 12 dB down in the speaker feed,
// or the voiced near end alone (the held note).
//
// Measured on x86-64 macOS, Release, seeds 1-3 (float / double):
//
//   A' < -20 dB at (s)            studio 0.585 0.663 0.603 / 0.596 0.661 0.603
//                                 cabin  0.584 0.569 0.403 / 0.584 0.607 0.403
//   A' minimum, held note, 3 s    studio -11.424 -11.170 -11.263 / -11.701 -11.484 -11.287
//   (dB)                          cabin  -11.725 -11.561 -11.336 / -11.588 -11.761 -11.649
//   D, median over seeds (dB):    studio -> rehearsal  max 2..4 s -1.089 / -1.104,
//                                   above -0.5 at +14 / +14 blocks after the swap
//                                 cabin -> studio      max 2..4 s -1.424 / -1.616,
//                                   above -0.5 at +11 / +12 blocks
//
// Three further seed triples (4-6, 7-9, 10-12, both precisions; measured
// once, not gated) land in the same places: A' crosses -20 dB at 0.433 to
// 0.623 s per seed and its held-note minimum runs -10.773 to -12.537 dB. D's
// window is as narrow here as the experiment found (about 1 dB): the
// converged maxima of the median ran -0.937 to -1.121 dB (studio ->
// rehearsal) and -1.458 to -1.696 dB (cabin -> studio), and the -0.5 dB
// crossing +2 to +14 blocks. The levels below are regression gates for this
// loop, not the calibrated operating points a policy layer would pick (the
// experiment calibrated A' at -23.842 dB and D at -1.235 dB).
//
// Host-only, like the other closed-loop suites: neither emulated selection
// (bare_metal_main.cpp, TEST_FILTER in CMakeLists.txt) names these suites.
// Runtime: 30.044 s for the whole suite, both precisions (Release, i9-8950HK).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "fixtures/rir_cabin.h"
#include "fixtures/rir_rehearsal.h"
#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/fdaf.h"
#include "mutap/fft.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/loudspeaker_band.h"

namespace {

    constexpr size_t k_block = 64;
    constexpr size_t k_parts = 16; ///< 1024 taps
    constexpr size_t k_n     = 2 * k_block;
    constexpr size_t k_delay = 480; ///< forward delay, 10 ms
    constexpr double k_fs    = 48000.0;

    template <typename Sample>
    using kalman_afc = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;

    // ------------------------------------------------------------- the paths

    /// Open-loop MSG by the exact Nyquist criterion at loop delay d: the
    /// largest positive real part of the loop response where its imaginary
    /// part crosses zero (linear interpolation between grid points), and DC.
    double exact_msg_db(const std::vector<double>& f, size_t d) {
        constexpr size_t    k_grid = size_t{1} << 22;
        tap::mu::real_fft   fft(k_grid);
        std::vector<double> buf(k_grid, 0.0);
        for (size_t i = 0; i < f.size(); ++i) {
            buf[d + i] = f[i];
        }
        fft.forward_inplace(buf.data());
        double best = std::max(0.0, std::max(buf[0], buf[1]));
        double pre  = buf[0];
        double pim  = 0.0;
        for (size_t k = 1; k <= k_grid / 2; ++k) {
            const double re = (k < k_grid / 2) ? buf[2 * k] : buf[1];
            const double im = (k < k_grid / 2) ? buf[2 * k + 1] : 0.0;
            if ((pim < 0.0) != (im < 0.0) || im == 0.0) {
                const double den = pim - im;
                const double fr  = den != 0.0 ? pim / den : 0.0;
                best             = std::max(best, pre + fr * (re - pre));
            }
            pre = re;
            pim = im;
        }
        return -20.0 * std::log10(best);
    }

    struct room {
        std::vector<double> f;
        double              msg_db;
    };

    room make_room(const float* rir, size_t taps) {
        room r;
        r.f      = mutap_test::loudspeaker_band(rir, taps);
        r.msg_db = exact_msg_db(r.f, k_delay);
        return r;
    }

    const room& studio() {
        static const room k_room =
            make_room(mutap_test::fixtures::k_rir_studio, mutap_test::fixtures::k_rir_studio_taps);
        return k_room;
    }
    const room& cabin() {
        static const room k_room = make_room(mutap_test::fixtures::k_rir_cabin, mutap_test::fixtures::k_rir_cabin_taps);
        return k_room;
    }
    const room& rehearsal() {
        static const room k_room =
            make_room(mutap_test::fixtures::k_rir_rehearsal, mutap_test::fixtures::k_rir_rehearsal_taps);
        return k_room;
    }

    // -------------------------------------------------------------- the loop

    /// Uniformly partitioned overlap-save convolution of the speaker stream
    /// with up to two fixed double-precision paths sharing one delay line,
    /// so a path swap at a block boundary keeps the speaker history.
    class path_conv {
      public:
        path_conv(const std::vector<double>& f1, const std::vector<double>& f2)
            : m_pf((std::max(f1.size(), f2.size()) + k_block - 1) / k_block)
            , m_fdl(m_pf * k_n, 0.0)
            , m_head(m_pf - 1) {
            for (const auto* p : {&f1, &f2}) {
                std::vector<double> hs(m_pf * k_n, 0.0);
                for (size_t q = 0; q < m_pf; ++q) {
                    double* t = &hs[q * k_n];
                    for (size_t i = 0; i < k_block; ++i) {
                        const size_t j = q * k_block + i;
                        t[i]           = j < p->size() ? (*p)[j] : 0.0;
                    }
                    m_fft.forward_inplace(t);
                }
                m_h.push_back(std::move(hs));
            }
        }

        template <typename Sample>
        void push(const Sample* u) {
            for (size_t i = 0; i < k_block; ++i) {
                m_in[i]           = m_in[i + k_block];
                m_in[i + k_block] = static_cast<double>(u[i]);
            }
            m_head = (m_head + 1) % m_pf;
            std::copy(m_in.begin(), m_in.end(), m_fdl.begin() + static_cast<std::ptrdiff_t>(m_head * k_n));
            m_fft.forward_inplace(&m_fdl[m_head * k_n]);
        }

        void output(size_t path, double* y) {
            std::fill(m_acc.begin(), m_acc.end(), 0.0);
            for (size_t q = 0; q < m_pf; ++q) {
                tap::mu::detail::packed_mac(&m_fdl[((m_head + m_pf - q) % m_pf) * k_n], &m_h[path][q * k_n],
                                            m_acc.data(), k_n);
            }
            m_fft.inverse(m_acc.data(), m_tmp.data());
            for (size_t i = 0; i < k_block; ++i) {
                y[i] = m_tmp[i + k_block];
            }
        }

      private:
        size_t                           m_pf;
        tap::mu::real_fft                m_fft{k_n};
        std::vector<double>              m_in = std::vector<double>(k_n, 0.0);
        std::vector<double>              m_fdl;
        std::vector<std::vector<double>> m_h;
        std::vector<double>              m_acc = std::vector<double>(k_n, 0.0);
        std::vector<double>              m_tmp = std::vector<double>(k_n, 0.0);
        size_t                           m_head;
    };

    /// The closed loop of the phase 0 convergence experiment:
    ///   y = v + F u            (F = f1 before swap_block, f2 from it on)
    ///   u = clip(G e(n - d) + aux),  speaker limit +-1000
    /// with the canceller's output e fed back. `probe(blk, afc, e)` runs
    /// after every block.
    template <typename Sample, typename Afc, typename Probe>
    void run_loop(Afc& afc, const room& f1, const room& f2, size_t swap_block, double gain_db,
                  const std::vector<Sample>& v, const std::vector<Sample>& aux, size_t blocks, Probe&& probe) {
        path_conv           conv(f1.f, f2.f);
        const double        gain = std::pow(10.0, gain_db / 20.0);
        std::vector<Sample> e_hist(k_delay, Sample(0));
        std::vector<Sample> u(k_block);
        std::vector<Sample> y(k_block);
        std::vector<Sample> e(k_block);
        std::vector<double> yf(k_block);
        for (size_t blk = 0; blk < blocks; ++blk) {
            const size_t t0 = blk * k_block;
            for (size_t i = 0; i < k_block; ++i) {
                double uu = gain * static_cast<double>(e_hist[i]);
                if (!aux.empty()) {
                    uu += static_cast<double>(aux[t0 + i]);
                }
                uu   = std::clamp(uu, -1000.0, 1000.0);
                u[i] = static_cast<Sample>(uu);
            }
            conv.push(u.data());
            conv.output(blk < swap_block ? 0 : 1, yf.data());
            for (size_t i = 0; i < k_block; ++i) {
                y[i] = static_cast<Sample>(static_cast<double>(v[t0 + i]) + yf[i]);
            }
            afc.process_block(u.data(), y.data(), e.data());
            for (size_t i = 0; i + k_block < k_delay; ++i) {
                e_hist[i] = e_hist[i + k_block];
            }
            for (size_t i = 0; i < k_block; ++i) {
                e_hist[k_delay - k_block + i] = e[i];
            }
            probe(blk, afc, e);
        }
    }

    constexpr size_t k_no_swap = std::numeric_limits<size_t>::max();

    // -------------------------------------------------------------- material

    /// The phase 0 backing track: speech-envelope noise plus the music chord,
    /// summed and scaled to 12 dB below the unit-RMS near end, added to the
    /// speaker feed after the loop gain (a karaoke playback).
    template <typename Sample>
    std::vector<Sample> backing_track(size_t n, unsigned seed) {
        const auto          a1 = mutap_test::ar_near_end<double>(n, seed + 777);
        const auto          a2 = mutap_test::music_near_end<double>(n, seed + 778);
        std::vector<double> t(n);
        double              energy = 0.0;
        for (size_t i = 0; i < n; ++i) {
            t[i] = a1[i] + a2[i];
            energy += t[i] * t[i];
        }
        const double        scale = std::pow(10.0, -12.0 / 20.0) / std::sqrt(energy / static_cast<double>(n));
        std::vector<Sample> aux(n);
        for (size_t i = 0; i < n; ++i) {
            aux[i] = static_cast<Sample>(t[i] * scale);
        }
        return aux;
    }

    template <typename Sample>
    typename kalman_afc<Sample>::config afc_config(size_t shadow_partitions = 0) {
        typename kalman_afc<Sample>::config cfg;
        cfg.fdaf.block_size   = k_block;
        cfg.fdaf.partitions   = k_parts;
        cfg.shadow_partitions = shadow_partitions;
        return cfg;
    }

    double db10(double x) {
        return 10.0 * std::log10(x);
    }

    double median(std::vector<double> x) {
        std::sort(x.begin(), x.end());
        const size_t m = x.size() / 2;
        return (x.size() % 2 == 1) ? x[m] : 0.5 * (x[m - 1] + x[m]);
    }

    /// First block at which `x` rises above (above = true) or falls below
    /// `level`, from `from` on; x.size() when it never does.
    size_t first_crossing(const std::vector<double>& x, size_t from, double level, bool above) {
        for (size_t b = from; b < x.size(); ++b) {
            if (above ? x[b] > level : x[b] < level) {
                return b;
            }
        }
        return x.size();
    }

    constexpr unsigned k_seeds[] = {1, 2, 3};

    template <typename Sample>
    class convergence_stats_test : public ::testing::Test {};

    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(convergence_stats_test, sample_types);

    // The shadow is a pure observer: it reads the prewhitened pair and the
    // core's prewhitened residual and writes only its own state, so the
    // cancelled output is BIT-identical with it on or off — for the Kalman
    // core and for the NLMS core alike.
    template <typename Afc, typename Sample>
    void expect_shadow_invisible(typename Afc::config cfg) {
        constexpr size_t blocks = 2000;
        const size_t     n      = blocks * k_block;
        const auto       v      = mutap_test::voiced_near_end<Sample>(n, 1, 160);
        const auto       aux    = backing_track<Sample>(n, 1);
        const room&      r      = studio();

        cfg.shadow_partitions = 0;
        Afc                 off(cfg);
        std::vector<Sample> e_off(n);
        run_loop<Sample>(off, r, r, k_no_swap, r.msg_db - 6.0, v, aux, blocks,
                         [&](size_t blk, const Afc& c, const std::vector<Sample>& e) {
                             std::copy(e.begin(), e.end(), e_off.begin() + static_cast<std::ptrdiff_t>(blk * k_block));
                             // Off: the powers are not tracked and the ratio reads 1.
                             ASSERT_EQ(c.residual_power(), Sample(0));
                             ASSERT_EQ(c.shadow_residual_power(), Sample(0));
                             ASSERT_EQ(c.shadow_residual_ratio(), Sample(1));
                         });
        EXPECT_FALSE(off.shadow_enabled());

        cfg.shadow_partitions = 2;
        Afc    on(cfg);
        size_t first_diff = blocks;
        run_loop<Sample>(on, r, r, k_no_swap, r.msg_db - 6.0, v, aux, blocks,
                         [&](size_t blk, const Afc&, const std::vector<Sample>& e) {
                             const Sample* ref = &e_off[blk * k_block];
                             if (first_diff == blocks && std::memcmp(ref, e.data(), k_block * sizeof(Sample)) != 0) {
                                 first_diff = blk;
                             }
                         });
        EXPECT_TRUE(on.shadow_enabled());
        EXPECT_EQ(first_diff, blocks) << "e differs from block " << first_diff;
        EXPECT_GT(on.residual_power(), Sample(0));
        EXPECT_GT(on.shadow_residual_power(), Sample(0));
    }

    TYPED_TEST(convergence_stats_test, ShadowNeverTouchesTheMainPath) {
        using sample = TypeParam;
        expect_shadow_invisible<kalman_afc<sample>, sample>(afc_config<sample>());

        typename tap::mu::pem_afc<sample>::config nlms; // the default (NLMS) core
        nlms.fdaf.block_size = k_block;
        nlms.fdaf.partitions = k_parts;
        expect_shadow_invisible<tap::mu::pem_afc<sample>, sample>(nlms);
    }

    // The denominator is the reset-time sum, prior included, computed by the
    // same summation as the numerator: exactly 1 after construction and
    // after reset(), for a flat and a decaying prior.
    TYPED_TEST(convergence_stats_test, UncertaintyRatioIsExactlyOneAtReset) {
        using sample = TypeParam;
        using kf     = tap::mu::partitioned_fdkf<sample>;
        for (const sample decay : {sample(1), sample(0.5)}) {
            typename kf::config cfg;
            cfg.block_size                = k_block;
            cfg.partitions                = k_parts;
            cfg.initial_uncertainty       = sample(3);
            cfg.initial_uncertainty_decay = decay;
            kf core(cfg);
            EXPECT_EQ(core.uncertainty_ratio(), sample(1));

            const auto          x = mutap_test::white_near_end<sample>(200 * k_block, 4);
            std::vector<sample> y(k_block);
            std::vector<sample> e(k_block);
            for (size_t blk = 0; blk < 200; ++blk) {
                for (size_t i = 0; i < k_block; ++i) {
                    y[i] = sample(0.5) * x[blk * k_block + i];
                }
                core.process_block(&x[blk * k_block], y.data(), e.data());
            }
            EXPECT_LT(core.uncertainty_ratio(), sample(1)) << "identification must shrink P";
            core.reset();
            EXPECT_EQ(core.uncertainty_ratio(), sample(1));
        }

        kalman_afc<sample> afc(afc_config<sample>(2));
        EXPECT_EQ(afc.uncertainty_ratio(), sample(1));
        EXPECT_EQ(afc.shadow_residual_ratio(), sample(1)); // both powers 0
        const auto          x = mutap_test::white_near_end<sample>(200 * k_block, 5);
        std::vector<sample> y(k_block);
        std::vector<sample> e(k_block);
        for (size_t blk = 0; blk < 200; ++blk) {
            for (size_t i = 0; i < k_block; ++i) {
                y[i] = sample(0.5) * x[blk * k_block + i];
            }
            afc.process_block(&x[blk * k_block], y.data(), e.data());
        }
        EXPECT_LT(afc.uncertainty_ratio(), sample(1));
        EXPECT_GT(afc.residual_power(), sample(0));

        // Frozen: process_block returns before prewhitening; the shadow does
        // not run and the powers hold.
        const sample pm = afc.residual_power();
        const sample ps = afc.shadow_residual_power();
        afc.set_adaptation(false);
        for (size_t blk = 0; blk < 20; ++blk) {
            afc.process_block(&x[blk * k_block], y.data(), e.data());
        }
        EXPECT_EQ(afc.residual_power(), pm);
        EXPECT_EQ(afc.shadow_residual_power(), ps);
        afc.set_adaptation(true);

        afc.reset();
        EXPECT_EQ(afc.uncertainty_ratio(), sample(1));
        EXPECT_EQ(afc.residual_power(), sample(0));
        EXPECT_EQ(afc.shadow_residual_power(), sample(0));
        EXPECT_EQ(afc.shadow_residual_ratio(), sample(1));
    }

    // A' on a cold start: voiced near end plus the backing track in the
    // speaker feed, at MSG - 6 on two band-limited fixtures. Per seed, the
    // time at which A' first falls below the level; the median over seeds
    // must beat the deadline in each room.
    //
    // Measured, seeds 1-3, float / double: crossings studio 0.585 0.663 0.603
    // / 0.596 0.661 0.603 s (median 0.603 / 0.603), cabin 0.584 0.569 0.403 /
    // 0.584 0.607 0.403 s (median 0.569 / 0.584). Held-note minima over 3 s
    // -11.170 to -11.761 dB: -20 dB sits 8.830 dB below the highest of them, and
    // the 1.0 s deadline 0.397 s past the slowest median.
    constexpr double k_a_level_db    = -20.0;
    constexpr double k_a_deadline_s  = 1.0;
    constexpr double k_held_length_s = 3.0;

    TYPED_TEST(convergence_stats_test, UncertaintyRatioFallsOnAColdStart) {
        using sample        = TypeParam;
        const size_t blocks = static_cast<size_t>(k_a_deadline_s * k_fs) / k_block;
        const size_t n      = blocks * k_block;
        for (const room* r : {&studio(), &cabin()}) {
            std::vector<double> t_cross;
            for (const unsigned seed : k_seeds) {
                kalman_afc<sample>  afc(afc_config<sample>());
                std::vector<double> a(blocks);
                run_loop<sample>(afc, *r, *r, k_no_swap, r->msg_db - 6.0,
                                 mutap_test::voiced_near_end<sample>(n, seed, 160), backing_track<sample>(n, seed),
                                 blocks, [&](size_t blk, const kalman_afc<sample>& c, const auto&) {
                                     a[blk] = db10(static_cast<double>(c.uncertainty_ratio()));
                                 });
                const size_t b = first_crossing(a, 0, k_a_level_db, false);
                t_cross.push_back(b < blocks ? static_cast<double>(b * k_block) / k_fs
                                             : std::numeric_limits<double>::infinity());
            }
            EXPECT_LE(median(t_cross), k_a_deadline_s) << (r == &studio() ? "studio" : "cabin");
        }
    }

    // ... and on a held note with no backing track it does NOT get there
    // (every seed, both rooms, over three times the cold start's deadline):
    // a held note never identifies the path — in phase 0 the true margin
    // reached 6 dB in 0 of 72 held-note runs — and A' says so. Measured
    // minima, float: studio -11.424 -11.170 -11.263, cabin -11.725 -11.561
    // -11.336 dB; double: studio -11.701 -11.484 -11.287, cabin -11.588
    // -11.761 -11.649 dB.
    TYPED_TEST(convergence_stats_test, UncertaintyRatioRefusesAHeldNote) {
        using sample        = TypeParam;
        const size_t blocks = static_cast<size_t>(k_held_length_s * k_fs) / k_block;
        const size_t n      = blocks * k_block;
        for (const room* r : {&studio(), &cabin()}) {
            for (const unsigned seed : k_seeds) {
                kalman_afc<sample> afc(afc_config<sample>());
                double             lowest = 0.0;
                run_loop<sample>(afc, *r, *r, k_no_swap, r->msg_db - 6.0,
                                 mutap_test::voiced_near_end<sample>(n, seed, 160), std::vector<sample>(), blocks,
                                 [&](size_t, const kalman_afc<sample>& c, const auto&) {
                                     lowest = std::min(lowest, db10(static_cast<double>(c.uncertainty_ratio())));
                                 });
                EXPECT_GT(lowest, k_a_level_db) << (r == &studio() ? "studio" : "cabin") << " seed " << seed;
            }
        }
    }

    // D on a walk: converge on band-limited room A (4 s), swap the true path
    // to band-limited room B at a block boundary, gain fixed at A's MSG - 6.
    // The per-block median over seeds of the main/shadow ratio must stay
    // below the level over the converged stretch (2 s to the swap) and rise
    // above it within the deadline after the swap.
    //
    // Measured (median over seeds 1-3, float / double): converged maxima
    // studio -> rehearsal -1.089 / -1.104 dB, cabin -> studio -1.424 /
    // -1.616 dB; the median crosses -0.5 dB +14 / +14 and +11 / +12 blocks
    // after the swap. The level splits the narrow gap (0.589 dB above the
    // highest converged maximum); the deadline is 18 blocks past the latest
    // crossing.
    constexpr double k_d_level_db       = -0.5;
    constexpr size_t k_d_deadline       = 32; ///< blocks, 43 ms
    constexpr double k_d_swap_s         = 4.0;
    constexpr double k_d_converged_from = 2.0;

    TYPED_TEST(convergence_stats_test, ShadowRatioRisesOnAWalk) {
        using sample                                      = TypeParam;
        const size_t                              swap    = static_cast<size_t>(k_d_swap_s * k_fs) / k_block;
        const size_t                              from    = static_cast<size_t>(k_d_converged_from * k_fs) / k_block;
        const size_t                              blocks  = swap + k_d_deadline + 1;
        const size_t                              n       = blocks * k_block;
        const std::pair<const room*, const room*> walks[] = {{&studio(), &rehearsal()}, {&cabin(), &studio()}};
        for (const auto& [a, b] : walks) {
            const char*                      name = a == &studio() ? "studio -> rehearsal" : "cabin -> studio";
            std::vector<std::vector<double>> d;
            for (const unsigned seed : k_seeds) {
                kalman_afc<sample>  afc(afc_config<sample>(2));
                std::vector<double> ratio(blocks);
                run_loop<sample>(afc, *a, *b, swap, a->msg_db - 6.0, mutap_test::voiced_near_end<sample>(n, seed, 160),
                                 backing_track<sample>(n, seed), blocks,
                                 [&](size_t blk, const kalman_afc<sample>& c, const auto&) {
                                     ratio[blk] = db10(static_cast<double>(c.shadow_residual_ratio()));
                                 });
                d.push_back(std::move(ratio));
            }
            std::vector<double> med(blocks);
            for (size_t blk = 0; blk < blocks; ++blk) {
                med[blk] = median({d[0][blk], d[1][blk], d[2][blk]});
            }
            const double pre_max = *std::max_element(med.begin() + static_cast<std::ptrdiff_t>(from),
                                                     med.begin() + static_cast<std::ptrdiff_t>(swap));
            const size_t detect  = first_crossing(med, swap, k_d_level_db, true);
            EXPECT_LT(pre_max, k_d_level_db) << name << ": false alarm while converged";
            EXPECT_LE(detect - swap, k_d_deadline) << name << ": walk not detected in time";
        }
    }

    // ------------------------------------------------ config and RT contract

    TEST(ConvergenceStatsConfigValidation, RejectsBadShadowConfigs) {
        using afc = kalman_afc<float>;

        afc::config cfg       = afc_config<float>();
        cfg.shadow_partitions = k_parts + 1; // longer than the core
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                   = afc_config<float>();
        cfg.shadow_partitions = k_parts; // as long as the core is allowed
        EXPECT_NO_THROW(afc{cfg});

        cfg                   = afc_config<float>();
        cfg.shadow_transition = 0.0f;
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                   = afc_config<float>();
        cfg.shadow_transition = 1.5f;
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                  = afc_config<float>();
        cfg.shadow_smoothing = 1.0f;
        EXPECT_THROW(afc{cfg}, std::invalid_argument);

        cfg                  = afc_config<float>();
        cfg.shadow_smoothing = -0.1f;
        EXPECT_THROW(afc{cfg}, std::invalid_argument);
    }

    template <typename T>
    concept has_uncertainty_ratio = requires(const T& a) { a.uncertainty_ratio(); };

    TEST(ConvergenceStatsRtContract, NewGettersAreNoexcept) {
        using kf = tap::mu::partitioned_fdkf<float>;
        static_assert(noexcept(std::declval<const kf&>().uncertainty_ratio()));

        using kafc = kalman_afc<float>;
        static_assert(noexcept(std::declval<const kafc&>().uncertainty_ratio()));
        static_assert(noexcept(std::declval<const kafc&>().shadow_enabled()));
        static_assert(noexcept(std::declval<const kafc&>().residual_power()));
        static_assert(noexcept(std::declval<const kafc&>().shadow_residual_power()));
        static_assert(noexcept(std::declval<const kafc&>().shadow_residual_ratio()));

        // The NLMS core keeps no state uncertainty: the forwarder does not
        // exist there, while the shadow getters do.
        using nafc = tap::mu::pem_afc<double>;
        static_assert(!has_uncertainty_ratio<nafc>);
        static_assert(has_uncertainty_ratio<kafc>);
        static_assert(noexcept(std::declval<const nafc&>().shadow_residual_ratio()));
        SUCCEED();
    }

} // namespace
