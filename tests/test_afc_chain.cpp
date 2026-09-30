// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// afc_chain (include/mutap/afc_chain.h), the anti-howl PoC's chain skeleton.
// HOST-ONLY (tests/CMakeLists.txt): the plumbing tests are cheap, but the
// closed-loop claims are gain bisections run on std::thread, and the
// emulated selections carry canaries, not claims (support/rooms.h).
//
//   plumbing (float and double)  one mic, no stages, no reference delay:
//                                bit-identical to pem_afc + the output delay
//                                + aux over 2000 blocks; stage order; multi-
//                                block calls; the reference lag; validation
//                                and the noexcept contract.
//   closed loop (float)          the loop's electrical delay modelled as the
//                                speaker feed's delay to the room
//                                (chain_loop, below): the reference delay
//                                puts the direct path where the flight is
//                                and does not cost ASG on speech; two mics
//                                both converge.
//   sweep (MUTAP_SLOW=1)         six rooms, both families, speech and the
//                                held note, aligned vs unaligned; wall time
//                                4:05 at delay 480 on the 12-thread Intel Mac
//                                (11 workers). AFC_CHAIN_DELAY and
//                                AFC_CHAIN_PROBE override the delay (480)
//                                and the probe (5 s).
//
// THE HELD NOTE IS NOT GATED, and alignment is not a win there. On the
// held note (voiced_near_end, 160-sample period) the largest identified
// tap is the closed-loop bias, not the room: it lands where the reference
// is in phase with the note (aligned: tap 32 at delay 480, taps 12 / 972
// at delay 500; unaligned: 96 + k 160), so the readback check is only
// meaningful after broadband material (the soundcheck's backing track,
// speech). And aligned minus unaligned ASG medians on the held note at
// delay 480 read -1.06 / +5.27 / +2.81 / -4.57 / -2.81 / -0.35 dB (cabin,
// studio, rehearsal, hall, mt5, mt9); at 500, -0.70 / 0.00 / +7.38 / -2.11
// / -3.51 / -1.40. On speech the same differences are +4.21 to +8.78 dB
// in all six rooms at both delays. Untested hypothesis: filter length is
// not free on tonal material (the karaoke branch's filter-length row,
// not re-landed: HANDOFF item 9), and the unaligned filter models only 608
// taps of the room; a 640-tap aligned filter's held-note medians at 480
// read +13.48 / +10.35 / +5.08 / +7.85 / +14.18 / +12.77, above the
// 1024-tap unaligned medians in four of six rooms (one sweep, by a
// temporary partitions override; not gated, not in this file).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/afc_chain.h"
#include "mutap/fd_kalman.h"
#include "mutap/lpc.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/karaoke_asg.h"
#include "support/rooms.h"

namespace {

    using mutap_test::median;
    using mutap_test::seed_in_set;
    namespace kk = mutap_test::karaoke;

    template <typename Sample>
    using kalman_afc = tap::mu::pem_afc<Sample, tap::mu::speech_predictor<Sample>, tap::mu::partitioned_fdkf<Sample>>;

    constexpr size_t k_block = 64;

    // ------------------------------------------------------------ helpers

    /// A forward gain as a chain stage (out = gain * in). Riding in a stage
    /// slot puts it before the speaker, so the reference is gain * bus +
    /// aux, as closed_loop_sim's u = K e: the cancellers identify the room,
    /// not the room times the gain.
    template <typename Sample>
    struct gain_stage {
        Sample gain = Sample(1);
        void   process_block(const Sample* in, Sample* out, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) {
                out[i] = gain * in[i];
            }
        }
    };

    /// out = scale * in + offset: non-commuting stages for the order test.
    template <typename Sample>
    struct affine_stage {
        Sample scale  = Sample(1);
        Sample offset = Sample(0);
        void   process_block(const Sample* in, Sample* out, size_t n) noexcept {
            for (size_t i = 0; i < n; ++i) {
                out[i] = scale * in[i] + offset;
            }
        }
    };

    /// The chain's closed loop: the speaker feed reaches the room after an
    /// ELECTRICAL delay (converters, buffers, the loudspeaker's DSP: what the
    /// reference delay exists to remove), then each mic's path:
    ///
    ///   y_m[n] = v_m[n] + sum_k F_m[k] clip(speaker[n - electrical_delay - k])
    ///
    /// electrical_delay >= block_size, so a mic block depends on speaker
    /// blocks already written. The loop from the bus back to the bus is
    /// K z^-electrical_delay F (K in a gain stage, the chain's delays at 0),
    /// so closed_loop_sim at forward_delay = electrical_delay is its dry
    /// open-loop twin.
    template <typename Sample>
    class chain_loop {
      public:
        struct config {
            std::vector<std::vector<Sample>> paths;                  ///< F_m, one per mic
            size_t                           block_size       = 64;  ///< the chain's
            size_t                           electrical_delay = 480; ///< samples, >= block_size
            double                           speaker_limit    = 1000;
        };

        explicit chain_loop(config cfg)
            : m_cfg(std::move(cfg))
            , m_y(m_cfg.paths.size(), std::vector<Sample>(m_cfg.block_size))
            , m_ptrs(m_cfg.paths.size())
            , m_speaker(m_cfg.block_size) {
            size_t longest = 0;
            for (const auto& p : m_cfg.paths) {
                longest = std::max(longest, p.size());
            }
            if (m_cfg.paths.empty() || longest == 0 || m_cfg.electrical_delay < m_cfg.block_size) {
                throw std::invalid_argument("chain_loop: bad config");
            }
            size_t h = 1;
            while (h < m_cfg.electrical_delay + longest + 2 * m_cfg.block_size) {
                h *= 2;
            }
            m_hist.assign(h, 0.0);
            m_mask = h - 1;
            m_t    = h; // start one history length in: reads never go negative
            for (size_t m = 0; m < m_y.size(); ++m) {
                m_ptrs[m] = m_y[m].data();
            }
        }

        /// One block: the mics from the near ends `v` (one block each), the
        /// chain, the speaker into the history. Returns the RMS of the bus
        /// (the sum of the mics' error blocks; +inf once non-finite).
        template <typename Chain>
        double step(const Sample* const* v, const Sample* aux, Chain& chain) {
            const size_t b = m_cfg.block_size;
            for (size_t m = 0; m < m_y.size(); ++m) {
                const auto& f = m_cfg.paths[m];
                for (size_t i = 0; i < b; ++i) {
                    const size_t n   = m_t + i - m_cfg.electrical_delay;
                    double       acc = 0.0;
                    for (size_t k = 0; k < f.size(); ++k) {
                        acc += static_cast<double>(f[k]) * m_hist[(n - k) & m_mask];
                    }
                    m_y[m][i] = static_cast<Sample>(static_cast<double>(v[m][i]) + acc);
                }
            }
            chain.process_block(m_ptrs.data(), aux, m_speaker.data(), b);
            const double lim = m_cfg.speaker_limit;
            for (size_t i = 0; i < b; ++i) {
                double s = static_cast<double>(m_speaker[i]);
                if (!(s >= -lim)) { // catches NaN too
                    s = -lim;
                }
                if (s > lim) {
                    s = lim;
                }
                m_hist[(m_t + i) & m_mask] = s;
            }
            m_t += b;
            double rms = 0.0;
            for (size_t i = 0; i < b; ++i) {
                double bus = 0.0;
                for (size_t m = 0; m < m_y.size(); ++m) {
                    bus += static_cast<double>(chain.error_block(m)[i]);
                }
                rms += bus * bus;
            }
            rms = std::sqrt(rms / static_cast<double>(b));
            return std::isfinite(rms) ? rms : std::numeric_limits<double>::infinity();
        }

        const std::vector<Sample>& mic_block(size_t m) const { return m_y[m]; }
        const std::vector<Sample>& speaker_block() const { return m_speaker; }

      private:
        config                           m_cfg;
        std::vector<double>              m_hist; ///< the played (clipped) speaker feed
        size_t                           m_mask = 0;
        size_t                           m_t    = 0;
        std::vector<std::vector<Sample>> m_y;
        std::vector<const Sample*>       m_ptrs;
        std::vector<Sample>              m_speaker;
    };

    template <typename Sample>
    std::vector<Sample> as(const std::vector<double>& v) {
        std::vector<Sample> out(v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            out[i] = static_cast<Sample>(v[i]);
        }
        return out;
    }

    /// Misalignment (dB) of an identified impulse response `ir` against the
    /// true path `f` delayed by `lag` taps, over the filter's length.
    template <typename Sample>
    double misalignment_db(const std::vector<Sample>& f, size_t lag, const std::vector<Sample>& ir) {
        double num = 0.0;
        double den = 0.0;
        for (size_t i = 0; i < ir.size(); ++i) {
            const double t = (i >= lag && i - lag < f.size()) ? static_cast<double>(f[i - lag]) : 0.0;
            const double d = static_cast<double>(ir[i]) - t;
            num += d * d;
            den += t * t;
        }
        return 10.0 * std::log10(num / den);
    }

    template <typename Sample>
    size_t argmax_abs(const std::vector<Sample>& v) {
        size_t best = 0;
        for (size_t i = 1; i < v.size(); ++i) {
            if (std::abs(v[i]) > std::abs(v[best])) {
                best = i;
            }
        }
        return best;
    }

    // ------------------------------------------------ plumbing (typed)

    template <typename Sample>
    class afc_chain_test : public ::testing::Test {};
    using sample_types = ::testing::Types<float, double>;
    TYPED_TEST_SUITE(afc_chain_test, sample_types);

    /// The small chain the plumbing tests use: 256 taps.
    template <typename Sample>
    typename tap::mu::afc_chain<Sample>::config small_chain(size_t mics, size_t ref_delay, size_t out_delay) {
        typename tap::mu::afc_chain<Sample>::config cfg;
        cfg.microphones               = mics;
        cfg.canceller.fdaf.block_size = k_block;
        cfg.canceller.fdaf.partitions = 4;
        cfg.reference_delay_samples   = ref_delay;
        cfg.output_delay_samples      = out_delay;
        return cfg;
    }

    /// A quiet band-limited 256-tap room (-12 dB), so the plumbing loops
    /// stay stable at unit gain.
    template <typename Sample>
    std::vector<Sample> quiet_room(unsigned seed) {
        auto f = mutap_test::band_limited(mutap_test::random_decaying_rir<Sample>(256, seed));
        for (auto& x : f) {
            x = static_cast<Sample>(static_cast<double>(x) * 0.25);
        }
        return f;
    }

    // One mic, no stages, no reference delay: the chain's speaker output is
    // pem_afc driven by the same mic stream with the previous block's
    // speaker as its reference, through an output delay line, plus aux —
    // bit for bit, over 2000 blocks of a closed loop. Pins the plumbing: the
    // reference ring, the bus, the output ring and the aux sum. The output
    // delay (100) is not a multiple of the block, so the ring wraps
    // mid-block.
    TYPED_TEST(afc_chain_test, OneMicMatchesPemAfcBitExactly) {
        using sample                           = TypeParam;
        constexpr size_t           k_out_delay = 100;
        constexpr size_t           k_blocks    = 2000;
        const auto                 cfg         = small_chain<sample>(1, 0, k_out_delay);
        tap::mu::afc_chain<sample> chain(cfg);

        typename chain_loop<sample>::config lc;
        lc.paths            = {quiet_room<sample>(5)};
        lc.electrical_delay = 2 * k_block;
        chain_loop<sample> loop(lc);

        const auto v   = mutap_test::ar_near_end<sample>(k_blocks * k_block, 3);
        const auto aux = mutap_test::white_near_end<sample>(k_blocks * k_block, 4);

        kalman_afc<sample>  afc(cfg.canceller);
        std::vector<sample> u(k_block, sample(0)); // the previous speaker block
        std::vector<sample> e(k_block);
        std::vector<sample> e_hist(k_blocks * k_block + k_out_delay, sample(0));
        std::vector<sample> expect(k_block);

        size_t mismatches = 0;
        for (size_t blk = 0; blk < k_blocks; ++blk) {
            const sample* vp[] = {&v[blk * k_block]};
            loop.step(vp, &aux[blk * k_block], chain);

            afc.process_block(u.data(), loop.mic_block(0).data(), e.data());
            for (size_t i = 0; i < k_block; ++i) {
                const size_t n          = blk * k_block + i;
                e_hist[n + k_out_delay] = e[i];
                expect[i]               = e_hist[n] + aux[n];
            }
            if (std::memcmp(expect.data(), loop.speaker_block().data(), k_block * sizeof(sample)) != 0) {
                ++mismatches;
            }
            u = expect;
        }
        EXPECT_EQ(mismatches, 0U);
    }

    // The stages run decorrelator -> reverb -> safety on the bus, before the
    // output delay line: speaker = 3 (2 e + 1) + aux, bit for bit.
    TYPED_TEST(afc_chain_test, StagesRunInOrderOnTheBus) {
        using sample = TypeParam;
        tap::mu::afc_chain<sample> chain(small_chain<sample>(1, 0, 0));
        affine_stage<sample>       dec{sample(2), sample(0)};
        affine_stage<sample>       rev{sample(1), sample(1)};
        affine_stage<sample>       saf{sample(3), sample(0)};
        chain.set_decorrelator(&dec);
        chain.set_reverb(&rev);
        chain.set_safety(&saf);

        const auto          y   = mutap_test::white_near_end<sample>(50 * k_block, 7);
        const auto          aux = mutap_test::white_near_end<sample>(50 * k_block, 8);
        std::vector<sample> out(k_block);
        size_t              mismatches = 0;
        for (size_t blk = 0; blk < 50; ++blk) {
            const sample* mics[] = {&y[blk * k_block]};
            chain.process_block(mics, &aux[blk * k_block], out.data(), k_block);
            for (size_t i = 0; i < k_block; ++i) {
                const sample e      = chain.error_block(0)[i];
                const sample expect = sample(3) * (sample(1) * (sample(2) * e + sample(0)) + sample(1)) + sample(0)
                                      + aux[blk * k_block + i];
                if (expect != out[i]) {
                    ++mismatches;
                }
            }
        }
        EXPECT_EQ(mismatches, 0U);

        // Detached again: bypass.
        chain.set_decorrelator(nullptr);
        chain.set_reverb(nullptr);
        chain.set_safety(nullptr);
        const sample* mics[] = {&y[0]};
        chain.process_block(mics, nullptr, out.data(), k_block);
        EXPECT_EQ(std::memcmp(out.data(), chain.error_block(0), k_block * sizeof(sample)), 0);
    }

    // A call of n = 4 blocks is four calls of one block, including the
    // reference and output rings (both delays are off the block grid), and
    // with two mics the bus is their sum.
    TYPED_TEST(afc_chain_test, MultiBlockCallsMatchSingleBlocks) {
        using sample                   = TypeParam;
        constexpr size_t           k_n = 4 * k_block;
        const auto                 cfg = small_chain<sample>(2, 37, 91);
        tap::mu::afc_chain<sample> one(cfg);
        tap::mu::afc_chain<sample> four(cfg);

        const auto          y0  = mutap_test::white_near_end<sample>(100 * k_n, 11);
        const auto          y1  = mutap_test::white_near_end<sample>(100 * k_n, 12);
        const auto          aux = mutap_test::white_near_end<sample>(100 * k_n, 13);
        std::vector<sample> a(k_n);
        std::vector<sample> b(k_n);
        size_t              mismatches = 0;
        for (size_t c = 0; c < 100; ++c) {
            const size_t  off    = c * k_n;
            const sample* mics[] = {&y0[off], &y1[off]};
            four.process_block(mics, &aux[off], a.data(), k_n);
            for (size_t k = 0; k < 4; ++k) {
                const sample* m1[] = {&y0[off + k * k_block], &y1[off + k * k_block]};
                one.process_block(m1, &aux[off + k * k_block], &b[k * k_block], k_block);
            }
            if (std::memcmp(a.data(), b.data(), k_n * sizeof(sample)) != 0) {
                ++mismatches;
            }
        }
        EXPECT_EQ(mismatches, 0U);
    }

    // The canceller's reference is the speaker feed block_size +
    // reference_delay_samples ago: feed a known "speaker" through aux with
    // silent mics (the bus is then exactly zero until the filter moves, and
    // the Kalman core does not move on zero error).
    TYPED_TEST(afc_chain_test, ReferenceLagsTheSpeakerByBlockPlusDelay) {
        using sample                     = TypeParam;
        constexpr size_t           k_ref = 150;
        tap::mu::afc_chain<sample> chain(small_chain<sample>(1, k_ref, 0));
        EXPECT_EQ(chain.reference_lag(), k_block + k_ref);
        EXPECT_EQ(chain.latency(), k_block);

        const auto                aux = mutap_test::white_near_end<sample>(40 * k_block, 21);
        const std::vector<sample> silent(k_block, sample(0));
        std::vector<sample>       out(k_block);
        size_t                    mismatches = 0;
        for (size_t blk = 0; blk < 40; ++blk) {
            const sample* mics[] = {silent.data()};
            chain.process_block(mics, &aux[blk * k_block], out.data(), k_block);
            for (size_t i = 0; i < k_block; ++i) {
                const size_t n      = blk * k_block + i;
                const sample expect = (n >= k_block + k_ref) ? aux[n - k_block - k_ref] : sample(0);
                if (chain.reference_block()[i] != expect) {
                    ++mismatches;
                }
            }
        }
        EXPECT_EQ(mismatches, 0U);
    }

    TEST(AfcChainConfigValidation, RejectsBadConfigs) {
        using chain = tap::mu::afc_chain<float>;
        auto cfg    = small_chain<float>(1, 0, 0);

        cfg.microphones = 0;
        EXPECT_THROW(chain{cfg}, std::invalid_argument);

        cfg             = small_chain<float>(1, 0, 0);
        cfg.sample_rate = 0.0;
        EXPECT_THROW(chain{cfg}, std::invalid_argument);
        cfg.sample_rate = std::numeric_limits<double>::quiet_NaN();
        EXPECT_THROW(chain{cfg}, std::invalid_argument);
        cfg.sample_rate = std::numeric_limits<double>::infinity();
        EXPECT_THROW(chain{cfg}, std::invalid_argument);

        cfg                           = small_chain<float>(1, 0, 0);
        cfg.canceller.fdaf.block_size = 100; // the canceller's own validation still applies
        EXPECT_THROW(chain{cfg}, std::invalid_argument);

        cfg                           = small_chain<float>(2, 0, 0);
        cfg.canceller.analysis_window = k_block; // pem_afc: < 2 * block_size
        EXPECT_THROW(chain{cfg}, std::invalid_argument);

        EXPECT_NO_THROW(chain{small_chain<float>(3, 1000, 1000)});
    }

    template <typename Chain>
    constexpr bool k_has_uncertainty = requires(const Chain& c) { c.uncertainty_ratio(0); };

    // The real-time contract: everything after construction is noexcept.
    TEST(AfcChainRtContract, PostConstructionEntryPointsAreNoexcept) {
        using chain = tap::mu::afc_chain<float>;
        static_assert(noexcept(std::declval<chain&>().process_block(nullptr, nullptr, nullptr, 0)));
        static_assert(noexcept(std::declval<chain&>().reset()));
        static_assert(noexcept(std::declval<chain&>().set_adaptation(false)));
        static_assert(noexcept(std::declval<chain&>().set_decorrelator(nullptr)));
        static_assert(noexcept(std::declval<chain&>().set_reverb(nullptr)));
        static_assert(noexcept(std::declval<chain&>().set_safety(nullptr)));
        static_assert(noexcept(std::declval<const chain&>().latency()));
        static_assert(noexcept(std::declval<const chain&>().reference_lag()));
        static_assert(noexcept(std::declval<const chain&>().uncertainty_ratio(0)));
        static_assert(noexcept(std::declval<const chain&>().shadow_residual_ratio(0)));
        static_assert(noexcept(std::declval<chain&>().direct_path_tap(0, nullptr)));
        static_assert(noexcept(std::declval<chain&>().reference_aligned(0, 0, 0, nullptr)));
        static_assert(noexcept(std::declval<const chain&>().error_block(0)));
        static_assert(noexcept(std::declval<const chain&>().reference_block()));
        // A stage must be noexcept to attach at all.
        static_assert(tap::mu::afc_stage<gain_stage<float>, float>);
        struct throwing_stage {
            void process_block(const float*, float*, size_t) {}
        };
        static_assert(!tap::mu::afc_stage<throwing_stage, float>);
        static_assert(!std::is_convertible_v<throwing_stage*, tap::mu::afc_stage_ref<float>>);
        // The raw NLMS core has no uncertainty statistic: the chain compiles
        // without the forwarders.
        using raw_chain = tap::mu::afc_chain<float, tap::mu::partitioned_fdaf<float>>;
        static_assert(k_has_uncertainty<chain>);
        static_assert(!k_has_uncertainty<raw_chain>);
        SUCCEED();
    }

    // ------------------------------------------------ closed loop (host)

    constexpr size_t k_parts  = 16;  // 1024 taps, the karaoke geometry
    constexpr size_t k_s1     = 480; ///< the modelled electrical delay: 10 ms at 48 kHz
    constexpr size_t k_margin = 32;  ///< the jitter margin
    /// reference_aligned()'s tolerance in the claims: in the band-limited
    /// image-source rooms the identified peak sat 0 or 1 tap from the
    /// expected one (studio 88 / 89 against 89) in every measured run.
    constexpr size_t k_tolerance = 4;

    using chain_f = tap::mu::afc_chain<float>;

    chain_f::config loop_chain(size_t mics, size_t ref_delay) {
        chain_f::config cfg;
        cfg.microphones               = mics;
        cfg.canceller.fdaf.block_size = k_block;
        cfg.canceller.fdaf.partitions = k_parts;
        cfg.reference_delay_samples   = ref_delay;
        return cfg;
    }

    /// The aligned reference delay: the modelled round trip minus the
    /// chain's inherent block minus the margin.
    constexpr size_t k_aligned = k_s1 - k_block - k_margin;

    struct alignment_run {
        size_t true_tap    = 0;     ///< argmax |F|
        size_t tap         = 0;     ///< identified direct-path tap
        bool   aligned     = false; ///< reference_aligned(0, true_tap + k_margin, k_tolerance)
        float  uncertainty = 0.0F;
        double mis_db      = 0.0; ///< misalignment against the delayed true path
        double chain_db    = 0.0;
        double open_db     = 0.0;
        double asg() const { return chain_db - open_db; }
    };

    std::vector<float> near_end_f(kk::material m, size_t blocks, unsigned seed) {
        return as<float>(kk::near_end(m, blocks, seed));
    }

    /// Does the converged chain howl at `gain_db` on `probe`? A fresh copy of
    /// the chain and the loop per probe.
    bool chain_howls(const chain_f& converged, const chain_loop<float>::config& lc, const std::vector<float>& probe,
                     double gain_db) {
        chain_f           chain = converged;
        gain_stage<float> gain{static_cast<float>(std::pow(10.0, gain_db / 20.0))};
        chain.set_safety(&gain);
        chain_loop<float> loop(lc);
        for (size_t blk = 0; blk < probe.size() / k_block; ++blk) {
            const float* v[] = {&probe[blk * k_block]};
            if (loop.step(v, nullptr, chain) >= 100.0) {
                return true;
            }
        }
        return false;
    }

    /// One room, one seed, one reference delay: converge 1500 blocks at
    /// exact_msg_db - 6, read the direct-path tap, bisect the gain.
    alignment_run run_alignment(const std::vector<float>& path, kk::material mat, unsigned seed, size_t ref_delay,
                                double probe_s, bool bisect, size_t delay = k_s1) {
        alignment_run r;
        r.true_tap          = argmax_abs(path);
        const double exact  = mutap_test::exact_msg_db(path, delay);
        const auto   v_conv = near_end_f(mat, 1500, seed);
        const auto   probe  = near_end_f(mat, kk::probe_blocks(probe_s), seed + 10);

        chain_loop<float>::config lc;
        lc.paths            = {path};
        lc.block_size       = k_block;
        lc.electrical_delay = delay;

        chain_f           chain(loop_chain(1, ref_delay));
        gain_stage<float> gain{static_cast<float>(std::pow(10.0, (exact - 6.0) / 20.0))};
        chain.set_safety(&gain);
        {
            chain_loop<float> loop(lc);
            for (size_t blk = 0; blk < 1500; ++blk) {
                const float* v[] = {&v_conv[blk * k_block]};
                loop.step(v, nullptr, chain);
            }
        }
        chain.set_safety(nullptr);
        std::vector<float> ir(chain.filter_length());
        r.tap = chain.direct_path_tap(0, ir.data());
        // The rig's check: the direct path is expected at the flight time
        // (the true peak) plus the margin, whatever delay was configured.
        r.aligned     = chain.reference_aligned(0, r.true_tap + k_margin, k_tolerance, ir.data());
        r.uncertainty = chain.uncertainty_ratio(0);
        r.mis_db      = misalignment_db(path, delay - k_block - ref_delay, ir);
        if (!bisect) {
            return r;
        }

        // Like for like: the dry loop K z^-d F on the same probe material.
        mutap_test::closed_loop_sim<float>::config dry;
        dry.feedback_path = path;
        dry.block_size    = k_block;
        dry.forward_delay = delay;
        r.open_db         = mutap_test::measured_msg_db<float>(dry, nullptr, probe, exact - 10.0, exact + 10.0, 0.5);

        double lo = exact - 15.0;
        double hi = exact + 30.0;
        if (chain_howls(chain, lc, probe, lo)) {
            r.chain_db = lo;
            return r;
        }
        while (hi - lo > 0.5) {
            const double mid = 0.5 * (lo + hi);
            if (chain_howls(chain, lc, probe, mid)) {
                hi = mid;
            }
            else {
                lo = mid;
            }
        }
        r.chain_db = lo;
        return r;
    }

    struct two_mic_run {
        double unc_db[2]   = {0.0, 0.0}; ///< uncertainty_ratio per mic at the end, dB
        size_t tap[2]      = {0, 0};
        size_t true_tap[2] = {0, 0};
        double mis_db[2]   = {0.0, 0.0}; ///< misalignment against the true path, dB
        double peak_rms    = 0.0;        ///< largest bus block RMS
        bool   finite      = true;
    };

    /// Two mics on two band-limited rooms (cabin and mt5: one from each
    /// family), one shared reference (aligned), independent speech near
    /// ends at each mic, the bus loop at exact_msg_db(F_0 + F_1) - 6 dB.
    two_mic_run run_two_mics(unsigned seed, size_t blocks) {
        const std::vector<std::vector<float>> paths = {as<float>(kk::room("cabin")), as<float>(kk::room("mt5"))};
        std::vector<float>                    sum(paths[0].size());
        for (size_t i = 0; i < sum.size(); ++i) {
            sum[i] = paths[0][i] + paths[1][i];
        }
        const double exact = mutap_test::exact_msg_db(sum, k_s1);
        const auto   v0    = near_end_f(kk::material::speech, blocks, seed);
        const auto   v1    = near_end_f(kk::material::speech, blocks, seed + 1000);

        chain_loop<float>::config lc;
        lc.paths            = paths;
        lc.block_size       = k_block;
        lc.electrical_delay = k_s1;
        chain_loop<float> loop(lc);

        chain_f           chain(loop_chain(2, k_aligned));
        gain_stage<float> gain{static_cast<float>(std::pow(10.0, (exact - 6.0) / 20.0))};
        chain.set_safety(&gain);
        two_mic_run r;
        for (size_t blk = 0; blk < blocks; ++blk) {
            const float* v[] = {&v0[blk * k_block], &v1[blk * k_block]};
            const double rms = loop.step(v, nullptr, chain);
            r.finite         = r.finite && std::isfinite(rms);
            r.peak_rms       = std::max(r.peak_rms, rms);
        }
        std::vector<float> ir(chain.filter_length());
        for (size_t m = 0; m < 2; ++m) {
            r.unc_db[m]   = 10.0 * std::log10(static_cast<double>(chain.uncertainty_ratio(m)));
            r.tap[m]      = chain.direct_path_tap(m, ir.data());
            r.true_tap[m] = argmax_abs(paths[m]);
            r.mis_db[m]   = misalignment_db(paths[m], k_margin, ir);
        }
        return r;
    }

    bool slow_enabled() {
        const char* v = std::getenv("MUTAP_SLOW");
        return v != nullptr && std::string(v) == "1";
    }

    unsigned worker_count() {
        if (const char* v = std::getenv("MUTAP_SLOW_THREADS")) {
            return std::max(1U, static_cast<unsigned>(std::strtoul(v, nullptr, 10)));
        }
        const unsigned hw = std::thread::hardware_concurrency();
        return hw > 1 ? hw - 1 : 1;
    }

    /// Run `jobs` on worker threads.
    template <typename Job>
    void run_parallel(std::vector<Job>& jobs) {
        std::atomic<size_t>      next{0};
        std::vector<std::thread> pool;
        for (unsigned w = 0; w < worker_count(); ++w) {
            pool.emplace_back([&] {
                for (size_t j = next++; j < jobs.size(); j = next++) {
                    jobs[j]();
                }
            });
        }
        for (auto& t : pool) {
            t.join();
        }
    }

    // SWEEP (MUTAP_SLOW=1): both room families, both materials, aligned
    // (reference_delay_samples = 480 - 64 - 32 = 384) vs unaligned (0),
    // five seeds; prints taps, uncertainty and ASG.
    TEST(AfcChainSweep, ReferenceAlignment) {
        if (!slow_enabled()) {
            GTEST_SKIP() << "set MUTAP_SLOW=1 for the afc_chain alignment sweep";
        }
        const std::vector<std::string> rooms  = {"cabin", "studio", "rehearsal", "hall", "mt5", "mt9"};
        const kk::material             mats[] = {kk::material::speech, kk::material::held};
        const double                   probe_s =
            std::getenv("AFC_CHAIN_PROBE") != nullptr ? std::strtod(std::getenv("AFC_CHAIN_PROBE"), nullptr) : 5.0;
        const size_t delay   = std::getenv("AFC_CHAIN_DELAY") != nullptr
                                   ? std::strtoul(std::getenv("AFC_CHAIN_DELAY"), nullptr, 10)
                                   : k_s1;
        const size_t aligned = delay - k_block - k_margin;
        std::printf("[ sweep ] electrical delay %zu, aligned reference delay %zu, probe %.1f s\n", delay, aligned,
                    probe_s);
        struct row {
            std::string   room;
            int           mat = 0;
            size_t        ref = 0;
            unsigned      set = 0;
            alignment_run r;
        };
        std::vector<row> rows;
        for (const auto& room : rooms) {
            for (int m = 0; m < 2; ++m) {
                for (const size_t ref : {aligned, size_t{0}}) {
                    for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                        rows.push_back({room, m, ref, set, {}});
                    }
                }
            }
        }
        std::vector<std::function<void()>> jobs;
        for (auto& rw : rows) {
            jobs.emplace_back([&rw, &mats, probe_s, delay] {
                const auto path = as<float>(kk::room(rw.room));
                rw.r = run_alignment(path, mats[rw.mat], seed_in_set(2, rw.set), rw.ref, probe_s, true, delay);
            });
        }
        run_parallel(jobs);
        for (const auto& rw : rows) {
            std::printf("[ sweep ] %-9s %-6s ref %3zu set %u: true %4zu tap %4zu aligned %d unc %.3e chain %+7.2f "
                        "open %+7.2f asg %+7.2f mis %+6.2f\n",
                        rw.room.c_str(), rw.mat == 0 ? "speech" : "held", rw.ref, rw.set, rw.r.true_tap, rw.r.tap,
                        rw.r.aligned ? 1 : 0, static_cast<double>(rw.r.uncertainty), rw.r.chain_db, rw.r.open_db,
                        rw.r.asg(), rw.r.mis_db);
        }
        for (const auto& room : rooms) {
            for (int m = 0; m < 2; ++m) {
                for (const size_t ref : {aligned, size_t{0}}) {
                    std::vector<double> asg;
                    int                 hits = 0;
                    for (const auto& rw : rows) {
                        if (rw.room == room && rw.mat == m && rw.ref == ref) {
                            asg.push_back(rw.r.asg());
                            hits += rw.r.aligned ? 1 : 0;
                        }
                    }
                    std::printf("[ median ] %-9s %-6s ref %3zu: asg %+7.2f, aligned %d of 5\n", room.c_str(),
                                m == 0 ? "speech" : "held", ref, median(asg), hits);
                }
            }
        }
    }

    // Two mics on two band-limited paths (cabin, mt5) with the shared,
    // aligned reference, independent speech at each mic, the bus loop at
    // exact_msg_db(F_0 + F_1, 480) - 6 dB, 1500 blocks (2 s): both
    // cancellers converge and the bus stays finite. NO two-mic ASG claim:
    // that is the two-mic go/no-go experiment's question (PoC plan §2.5).
    // Measured (seed sets 0..4, float):
    //   uncertainty_ratio   cabin -18.14 .. -18.31 dB, mt5 -17.80 .. -18.12 dB
    //   misalignment        cabin -5.05 .. -5.68 dB,   mt5 -4.64 .. -5.48 dB
    //   peak bus block RMS  2.634 .. 3.199 (the howl rule is 100)
    // For context (not like for like: each room alone, at its own
    // exact_msg_db - 6): one mic reads cabin -18.25 .. -18.47 / -5.57 ..
    // -6.17 dB and mt5 -17.93 .. -18.09 / -4.97 .. -5.65 dB. From reset the
    // ratio is 0 dB.
    TEST(AfcChainClosedLoop, TwoMicsBothConverge) {
        std::vector<two_mic_run>           runs(mutap_test::k_claim_seed_sets);
        std::vector<std::function<void()>> jobs;
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            jobs.emplace_back([&runs, set] { runs[set] = run_two_mics(seed_in_set(2, set), 1500); });
        }
        run_parallel(jobs);
        for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
            const auto& r = runs[set];
            std::printf("[ measured ] set %u: unc %+.2f / %+.2f dB, mis %+.2f / %+.2f dB, taps %zu (true %zu) / %zu "
                        "(true %zu), peak rms %.3f finite %d\n",
                        set, r.unc_db[0], r.unc_db[1], r.mis_db[0], r.mis_db[1], r.tap[0], r.true_tap[0], r.tap[1],
                        r.true_tap[1], r.peak_rms, r.finite ? 1 : 0);
            EXPECT_TRUE(r.finite) << "set " << set;
            EXPECT_LT(r.peak_rms, 100.0) << "set " << set << ": measured <= 3.199";
            for (size_t m = 0; m < 2; ++m) {
                EXPECT_LT(r.unc_db[m], -15.0) << "set " << set << " mic " << m << ": measured <= -17.80 dB";
                EXPECT_LT(r.mis_db[m], -3.0) << "set " << set << " mic " << m << ": measured <= -4.64 dB";
            }
        }
    }

    // THE REFERENCE DELAY, in the loop: speech, S1 (electrical delay 480),
    // converge 1500 blocks at exact_msg_db - 6, then bisect the gain (5 s
    // probes, 0.5 dB) against the dry loop on the same probe. Aligned =
    // reference_delay_samples 384 (480 - the block - a 32-sample margin),
    // unaligned = 0. One room per generator family: cabin (image-source)
    // and mt9 (random_decaying_rir). Measured (seed sets 0..4, float):
    //
    //                     direct tap (true)   ASG median (per seed)
    //   cabin aligned     83 in 5 of 5 (51)   +19.80 (+19.10 .. +20.16)
    //   cabin unaligned   467 in 5 of 5       +15.59 (+15.23 .. +15.94)
    //   mt9   aligned     43 in 5 of 5 (11)   +22.97 (+21.21 .. +23.32)
    //   mt9   unaligned   427 in 5 of 5       +16.99 (+15.23 .. +17.34)
    //
    // Aligned, the direct path sits at the true tap + the margin; unaligned,
    // 416 taps later (the electrical delay minus the chain's block), where
    // 416 of the 1024 taps model dead delay and the room's last 416 fall off
    // the end. The tap claim is asserted on the image-source room only: a
    // random_decaying_rir room has no physical direct path, its largest
    // taps near-tie, and the readback reads the other one in some runs
    // (mt5: 3 of 5 aligned at delay 480, 0 of 5 at 500; mt9 3 of 5 at 500).
    TEST(AfcChainClosedLoop, ReferenceDelayAlignsTheDirectPath) {
        const char* const                  rooms[] = {"cabin", "mt9"};
        alignment_run                      runs[2][2][mutap_test::k_claim_seed_sets];
        std::vector<std::function<void()>> jobs;
        for (size_t room = 0; room < 2; ++room) {
            for (size_t a = 0; a < 2; ++a) {
                for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                    jobs.emplace_back([&runs, &rooms, room, a, set] {
                        const auto path    = as<float>(kk::room(rooms[room]));
                        runs[room][a][set] = run_alignment(path, kk::material::speech, seed_in_set(2, set),
                                                           a == 0 ? k_aligned : 0, 5.0, true);
                    });
                }
            }
        }
        run_parallel(jobs);
        for (size_t room = 0; room < 2; ++room) {
            std::vector<double> asg[2];
            for (size_t a = 0; a < 2; ++a) {
                for (unsigned set = 0; set < mutap_test::k_claim_seed_sets; ++set) {
                    const auto& r = runs[room][a][set];
                    std::printf("[ measured ] %s %s set %u: true tap %zu, direct tap %zu, chain %+.2f, open %+.2f, "
                                "ASG %+.2f\n",
                                rooms[room], a == 0 ? "aligned" : "unaligned", set, r.true_tap, r.tap, r.chain_db,
                                r.open_db, r.asg());
                    asg[a].push_back(r.asg());
                    if (room == 0) {
                        const size_t late = k_s1 - k_block;
                        EXPECT_EQ(r.aligned, a == 0) << rooms[room] << " set " << set << ": direct tap " << r.tap;
                        if (a == 1) {
                            EXPECT_NEAR(static_cast<double>(r.tap), static_cast<double>(r.true_tap + late),
                                        static_cast<double>(k_tolerance))
                                << "measured " << r.true_tap + late;
                        }
                    }
                }
            }
            std::printf("[ measured ] %s ASG median: aligned %+.2f, unaligned %+.2f\n", rooms[room], median(asg[0]),
                        median(asg[1]));
            EXPECT_GE(median(asg[0]), median(asg[1]))
                << rooms[room] << ": measured cabin +19.80 vs +15.59, mt9 +22.97 vs +16.99";
        }
    }

} // namespace
