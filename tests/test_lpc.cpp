// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The LP / whitening chain is the numerically delicate block of the whole
// canceller (HANDOFF.md "hard parts" #1), so it gets dedicated conditioning
// tests: known-AR recovery, and every degenerate frame the guards exist for
// (silence, DC, pure tones, garbage, over-ordered fits). Plus the plug-in
// mechanics both predictors share: streaming apply() must equal one-shot
// filtering, and the pitch stage must engage exactly when it should.

#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "mutap/lpc.h"
#include "support/closed_loop.h"

namespace {

    template <typename Sample>
    std::vector<Sample> white_noise(size_t n, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<Sample>              x(n);
        for (auto& v : x) {
            v = static_cast<Sample>(dist(gen));
        }
        return x;
    }

    double energy(const std::vector<double>& x, size_t from = 0) {
        double e = 0.0;
        for (size_t i = from; i < x.size(); ++i) {
            e += x[i] * x[i];
        }
        return e;
    }

    TEST(Levinson, RecoversKnownAr2) {
        // x[n] = w[n] - a1 x[n-1] - a2 x[n-2] with a stable pole pair; the
        // prediction-error filter must recover (1, a1, a2).
        const double        a1 = -1.5;
        const double        a2 = 0.7;
        auto                w  = white_noise<double>(16384, 3);
        std::vector<double> x(w.size(), 0.0);
        for (size_t i = 2; i < x.size(); ++i) {
            x[i] = w[i] - a1 * x[i - 1] - a2 * x[i - 2];
        }

        std::vector<double> r(3);
        std::vector<double> a(3);
        std::vector<double> scratch(3);
        mutap::autocorrelation(x.data(), x.size(), r.data(), 2);
        const size_t achieved = mutap::levinson(r.data(), size_t{2}, a.data(), scratch.data(), 1e-9);

        EXPECT_EQ(achieved, 2u);
        EXPECT_NEAR(a[1], a1, 0.02);
        EXPECT_NEAR(a[2], a2, 0.02);
    }

    TEST(Levinson, WhiteNoiseGivesNearIdentity) {
        auto                x = white_noise<double>(16384, 4);
        std::vector<double> r(9);
        std::vector<double> a(9);
        std::vector<double> scratch(9);
        mutap::autocorrelation(x.data(), x.size(), r.data(), 8);
        mutap::levinson(r.data(), size_t{8}, a.data(), scratch.data(), 1e-9);
        EXPECT_DOUBLE_EQ(a[0], 1.0);
        for (size_t j = 1; j <= 8; ++j) {
            EXPECT_LT(std::abs(a[j]), 0.05) << "coefficient " << j;
        }
    }

    TEST(Levinson, SilenceGivesIdentity) {
        std::vector<double> r(9, 0.0);
        std::vector<double> a(9, 42.0);
        std::vector<double> scratch(9);
        EXPECT_EQ(mutap::levinson(r.data(), size_t{8}, a.data(), scratch.data(), 1e-4), 0u);
        EXPECT_DOUBLE_EQ(a[0], 1.0);
        for (size_t j = 1; j <= 8; ++j) {
            EXPECT_DOUBLE_EQ(a[j], 0.0);
        }
    }

    TEST(Levinson, GarbageAutocorrelationGivesIdentity) {
        std::vector<double> r(5, 0.0);
        r[0] = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> a(5, 42.0);
        std::vector<double> scratch(5);
        EXPECT_EQ(mutap::levinson(r.data(), size_t{4}, a.data(), scratch.data(), 1e-4), 0u);
        EXPECT_DOUBLE_EQ(a[0], 1.0);
        EXPECT_DOUBLE_EQ(a[1], 0.0);
    }

    // A DC frame is a maximally predictable (rank-one) signal: every guard
    // must hold and the fitted filter must actually kill DC.
    TEST(Levinson, DcFrameStaysFiniteAndWhitensDc) {
        std::vector<double> x(1024, 1.0);
        std::vector<double> r(9);
        std::vector<double> a(9);
        std::vector<double> scratch(9);
        mutap::autocorrelation(x.data(), x.size(), r.data(), 8);
        mutap::levinson(r.data(), size_t{8}, a.data(), scratch.data(), 1e-4);
        double dc_gain = 0.0;
        for (size_t j = 0; j <= 8; ++j) {
            ASSERT_TRUE(std::isfinite(a[j])) << "coefficient " << j;
            dc_gain += a[j];
        }
        EXPECT_LT(std::abs(dc_gain), 0.1); // A(1) ~ 0: DC is removed
    }

    // Pure tones are the ill-conditioned case PEM lives on: poles on the
    // unit circle. The ridge keeps every reflection coefficient inside it,
    // and the fitted FIR must still whiten the tone deeply.
    TEST(Levinson, PureToneIsWhitenedSafely) {
        for (size_t order : {8u, 32u}) { // 32 = heavily over-ordered fit
            std::vector<double> x(2048);
            for (size_t i = 0; i < x.size(); ++i) {
                x[i] = std::sin(0.093 * 2.0 * std::numbers::pi * static_cast<double>(i));
            }
            std::vector<double> r(order + 1);
            std::vector<double> a(order + 1);
            std::vector<double> scratch(order + 1);
            mutap::autocorrelation(x.data(), x.size(), r.data(), order);
            mutap::levinson(r.data(), order, a.data(), scratch.data(), 1e-4);

            std::vector<double> res(x.size(), 0.0);
            for (size_t i = order; i < x.size(); ++i) {
                double acc = 0.0;
                for (size_t j = 0; j <= order; ++j) {
                    ASSERT_TRUE(std::isfinite(a[j])) << "order " << order << " coefficient " << j;
                    acc += a[j] * x[i - j];
                }
                res[i] = acc;
            }
            const double gain_db = 10.0 * std::log10(energy(x, order) / energy(res, order));
            EXPECT_GT(gain_db, 25.0) << "order " << order << ": tone not whitened";
        }
    }

    // The ridge is a synthetic noise floor: it must CAP the prediction gain
    // (bigger ridge, shallower whitening) — that is what bounds the
    // conditioning on line spectra.
    TEST(Levinson, RidgeCapsPredictionGain) {
        std::vector<double> x(2048);
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = std::sin(0.093 * 2.0 * std::numbers::pi * static_cast<double>(i));
        }
        auto whitening_gain = [&](double ridge) {
            std::vector<double> r(9);
            std::vector<double> a(9);
            std::vector<double> scratch(9);
            mutap::autocorrelation(x.data(), x.size(), r.data(), 8);
            mutap::levinson(r.data(), size_t{8}, a.data(), scratch.data(), ridge);
            std::vector<double> res(x.size(), 0.0);
            for (size_t i = 8; i < x.size(); ++i) {
                double acc = 0.0;
                for (size_t j = 0; j <= 8; ++j) {
                    acc += a[j] * x[i - j];
                }
                res[i] = acc;
            }
            return 10.0 * std::log10(energy(x, 8) / energy(res, 8));
        };
        EXPECT_GT(whitening_gain(1e-6), whitening_gain(1e-2) + 10.0);
    }

    // Streaming apply() with carried state must match one-shot filtering —
    // the FIR history handoff across block boundaries is what the PEM
    // prefilters rely on.
    TEST(LpcPredictor, BlockwiseApplyMatchesOneShot) {
        auto x = white_noise<double>(1024, 5);
        for (size_t i = 1; i < x.size(); ++i) {
            x[i] += 0.8 * x[i - 1]; // color it so coefficients are nontrivial
        }
        mutap::lpc_predictor<double> lp({16, 1e-4});
        lp.analyze(x.data(), x.size());

        auto                one_state = lp.make_state();
        std::vector<double> one(x.size());
        lp.apply(one_state, x.data(), one.data(), x.size());

        auto                blk_state = lp.make_state();
        std::vector<double> blk(x.size());
        for (size_t pos = 0; pos < x.size();) { // ragged blocks cross the history length
            const size_t n = std::min<size_t>({7, x.size() - pos});
            lp.apply(blk_state, &x[pos], &blk[pos], n);
            pos += n;
        }
        for (size_t i = 0; i < x.size(); ++i) {
            ASSERT_DOUBLE_EQ(blk[i], one[i]) << "sample " << i;
        }
    }

    TEST(LpcPredictor, ResetStateClearsHistory) {
        auto                         x = white_noise<double>(256, 6);
        mutap::lpc_predictor<double> lp({8, 1e-4});
        lp.analyze(x.data(), x.size());

        auto                s = lp.make_state();
        std::vector<double> out(x.size());
        lp.apply(s, x.data(), out.data(), x.size());
        lp.reset_state(s);

        auto                fresh = lp.make_state();
        std::vector<double> a(x.size());
        std::vector<double> b(x.size());
        lp.apply(s, x.data(), a.data(), x.size());
        lp.apply(fresh, x.data(), b.data(), x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            ASSERT_DOUBLE_EQ(a[i], b[i]);
        }
    }

    TEST(SpeechPredictor, FindsPitchOnVoicedMaterial) {
        const auto                      v = mutap_test::voiced_near_end<double>(4096, 7, 160);
        mutap::speech_predictor<double> sp({});
        sp.analyze(&v[2048], 1024);
        EXPECT_EQ(sp.pitch_lag(), 160u);
        EXPECT_NEAR(static_cast<double>(sp.pitch_gain()), 0.9, 1e-6); // clamped at max_gain
    }

    TEST(SpeechPredictor, PitchStaysOffOnUnvoicedMaterial) {
        mutap::speech_predictor<double> sp({});

        const auto t = mutap_test::tonal_near_end<double>(4096, 7);
        sp.analyze(&t[2048], 1024); // short-term LP whitens tones; residual is noise
        EXPECT_EQ(sp.pitch_lag(), 0u);

        const auto w = white_noise<double>(4096, 8);
        sp.analyze(&w[2048], 1024);
        EXPECT_EQ(sp.pitch_lag(), 0u);
    }

    // The cascade's reason to exist: on voiced (pitch-periodic) material it
    // must whiten far deeper than short-term LP alone (measured ~49 dB vs
    // ~29 dB on this fixture).
    TEST(SpeechPredictor, CascadeBeatsShortTermAloneOnVoiced) {
        const auto v = mutap_test::voiced_near_end<double>(4096, 7, 160);

        mutap::speech_predictor<double> sp({});
        sp.analyze(&v[2048], 1024);
        auto                sp_state = sp.make_state();
        std::vector<double> cascade(1024);
        sp.apply(sp_state, &v[3072], cascade.data(), 1024);

        mutap::lpc_predictor<double> lp({16, 1e-4});
        lp.analyze(&v[2048], 1024);
        auto                lp_state = lp.make_state();
        std::vector<double> short_term(1024);
        lp.apply(lp_state, &v[3072], short_term.data(), 1024);

        std::vector<double> in(v.begin() + 3072, v.begin() + 4096);
        const double        cascade_gain_db = 10.0 * std::log10(energy(in, 512) / energy(cascade, 512));
        const double        short_gain_db   = 10.0 * std::log10(energy(in, 512) / energy(short_term, 512));
        EXPECT_GT(cascade_gain_db, 40.0);
        EXPECT_GT(cascade_gain_db, short_gain_db + 10.0);
    }

    TEST(SpeechPredictor, BlockwiseApplyMatchesOneShot) {
        const auto                      v = mutap_test::voiced_near_end<double>(2048, 9, 160);
        mutap::speech_predictor<double> sp({});
        sp.analyze(v.data(), 1024);

        auto                one_state = sp.make_state();
        std::vector<double> one(v.size());
        sp.apply(one_state, v.data(), one.data(), v.size());

        auto                blk_state = sp.make_state();
        std::vector<double> blk(v.size());
        for (size_t pos = 0; pos < v.size();) {
            const size_t n = std::min<size_t>({13, v.size() - pos});
            sp.apply(blk_state, &v[pos], &blk[pos], n);
            pos += n;
        }
        for (size_t i = 0; i < v.size(); ++i) {
            ASSERT_DOUBLE_EQ(blk[i], one[i]) << "sample " << i;
        }
    }

    // The warped predictor at lambda = 0 IS the plain predictor: the
    // allpass sections collapse to unit delays. The identity is algebraic,
    // not bitwise — the two implementations walk different arithmetic
    // paths, and compilers that contract them differently (AppleClang on
    // arm64 fuses the allpass recursion into FMAs) round differently, which
    // Levinson then amplifies to ~2e-10 relative on the smallest
    // coefficients. Asserted at 1e-9, ~3 orders looser than that and ~3
    // orders tighter than any behavioral difference could matter.
    TEST(WarpedLpcPredictor, LambdaZeroMatchesPlain) {
        const auto v = mutap_test::music_near_end<double>(4096, 7);

        mutap::lpc_predictor<double> plain({16, 1e-4});
        plain.analyze(&v[2048], 1024);

        mutap::warped_lpc_predictor<double>::config wc;
        wc.order  = 16;
        wc.lambda = 0.0;
        mutap::warped_lpc_predictor<double> warped(wc);
        warped.analyze(&v[2048], 1024);

        for (size_t j = 0; j <= 16; ++j) {
            EXPECT_NEAR(plain.coefficients()[j], warped.coefficients()[j], 1e-9) << "coefficient " << j;
        }

        auto                ps = plain.make_state();
        auto                ws = warped.make_state();
        std::vector<double> pout(512);
        std::vector<double> wout(512);
        plain.apply(ps, &v[3072], pout.data(), 512);
        warped.apply(ws, &v[3072], wout.data(), 512);
        for (size_t i = 0; i < 512; ++i) {
            ASSERT_NEAR(pout[i], wout[i], 1e-9) << "sample " << i;
        }
    }

    TEST(WarpedLpcPredictor, BlockwiseApplyMatchesOneShot) {
        const auto                          v = mutap_test::music_near_end<double>(2048, 9);
        mutap::warped_lpc_predictor<double> wp({});
        wp.analyze(v.data(), 1024);

        auto                one_state = wp.make_state();
        std::vector<double> one(v.size());
        wp.apply(one_state, v.data(), one.data(), v.size());

        auto                blk_state = wp.make_state();
        std::vector<double> blk(v.size());
        for (size_t pos = 0; pos < v.size();) {
            const size_t n = std::min<size_t>({13, v.size() - pos});
            wp.apply(blk_state, &v[pos], &blk[pos], n);
            pos += n;
        }
        for (size_t i = 0; i < v.size(); ++i) {
            ASSERT_DOUBLE_EQ(blk[i], one[i]) << "sample " << i;
        }
    }

    TEST(WarpedLpcPredictor, ResetStateClearsHistory) {
        const auto                          v = mutap_test::music_near_end<double>(1024, 6);
        mutap::warped_lpc_predictor<double> wp({});
        wp.analyze(v.data(), v.size());

        auto                s = wp.make_state();
        std::vector<double> out(v.size());
        wp.apply(s, v.data(), out.data(), v.size());
        wp.reset_state(s);

        auto                fresh = wp.make_state();
        std::vector<double> a(v.size());
        std::vector<double> b(v.size());
        wp.apply(s, v.data(), a.data(), v.size());
        wp.apply(fresh, v.data(), b.data(), v.size());
        for (size_t i = 0; i < v.size(); ++i) {
            ASSERT_DOUBLE_EQ(a[i], b[i]);
        }
    }

    // The conditioning guards are shared with the plain analysis: degenerate
    // frames must yield the identity filter, and applying the identity must
    // pass the signal through untouched (the allpass taps all weigh zero).
    TEST(WarpedLpcPredictor, DegenerateFramesGiveIdentity) {
        mutap::warped_lpc_predictor<double> wp({});

        std::vector<double> silence(1024, 0.0);
        wp.analyze(silence.data(), silence.size());
        for (size_t j = 1; j <= wp.order(); ++j) {
            EXPECT_DOUBLE_EQ(wp.coefficients()[j], 0.0);
        }

        std::vector<double> garbage(1024, std::numeric_limits<double>::quiet_NaN());
        wp.analyze(garbage.data(), garbage.size());
        for (size_t j = 1; j <= wp.order(); ++j) {
            EXPECT_DOUBLE_EQ(wp.coefficients()[j], 0.0);
        }

        const auto          x = white_noise<double>(256, 3);
        auto                s = wp.make_state();
        std::vector<double> out(x.size());
        wp.apply(s, x.data(), out.data(), x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            ASSERT_DOUBLE_EQ(out[i], x[i]) << "identity filter must pass through";
        }
    }

    // Float instantiation (the embedded profile) tracks double analysis on
    // the same frame. The tolerance is loose by design: a 24th-order fit on
    // a near-line spectrum is the ill-conditioned case the ridge exists
    // for, and float accumulation through 24 chained allpass passes
    // legitimately shifts coefficients by ~0.5% (behavioral float coverage
    // comes from the in-loop suites).
    TEST(WarpedLpcPredictor, FloatTracksDouble) {
        const auto         vd = mutap_test::music_near_end<double>(4096, 7);
        std::vector<float> vf(vd.begin(), vd.end());

        mutap::warped_lpc_predictor<double> wd({});
        mutap::warped_lpc_predictor<float>  wf({});
        wd.analyze(&vd[2048], 1024);
        wf.analyze(&vf[2048], 1024);
        for (size_t j = 0; j <= wd.order(); ++j) {
            EXPECT_NEAR(static_cast<double>(wf.coefficients()[j]), wd.coefficients()[j], 1e-2) << "coefficient " << j;
        }
    }

    TEST(PredictorConfigValidation, RejectsBadConfigs) {
        using lpc = mutap::lpc_predictor<float>;
        EXPECT_THROW(lpc({0, 1e-4F}), std::invalid_argument);
        EXPECT_THROW(lpc({8, 0.0F}), std::invalid_argument);

        using speech = mutap::speech_predictor<float>;
        speech::config c;
        c.min_lag = 0;
        EXPECT_THROW(speech{c}, std::invalid_argument);
        c         = {};
        c.min_lag = c.max_lag;
        EXPECT_THROW(speech{c}, std::invalid_argument);
        c                   = {};
        c.analysis_capacity = c.max_lag; // < 2 * max_lag
        EXPECT_THROW(speech{c}, std::invalid_argument);
        c          = {};
        c.max_gain = 1.0F;
        EXPECT_THROW(speech{c}, std::invalid_argument);

        using warped = mutap::warped_lpc_predictor<float>;
        warped::config w;
        w.order = 0;
        EXPECT_THROW(warped{w}, std::invalid_argument);
        w        = {};
        w.lambda = 1.0F;
        EXPECT_THROW(warped{w}, std::invalid_argument);
        w        = {};
        w.lambda = -1.0F;
        EXPECT_THROW(warped{w}, std::invalid_argument);
        w                   = {};
        w.analysis_capacity = 0;
        EXPECT_THROW(warped{w}, std::invalid_argument);
    }

} // namespace
