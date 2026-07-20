// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// ITU compliance suite, double-talk battery (docs/itu-compliance.md,
// P1110 11.12 / P1120 11.13 / P.340 Table 5): the P.340 Category 1
// (full duplex) rows, at both required rates on the pinned compliance
// chain. Thresholds measured first; measured values sit next to the
// assertions. ITU_DtReceiveAtten and ITU_AttenRangeReceive have no test:
// the chain performs NO receive-path processing (receive attenuation is
// identically 0 dB) — recorded in the matrix, not asserted.
//
// Every double-talk protocol converges the chain on CSS first (the recs'
// pre-conditioning), then runs the P.501 7.2.4 AM-FM orthogonal pair —
// the interleaved comb plans are what let one measurement separate the
// near end from the echo it overlaps in time.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "support/itu_chain.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::itu;

    // Typed over <float, double>: float32 (/0) is the deployment target,
    // double (/1) the certified golden model. Gate tables (itu_chain.h
    // prec_gate) carry a column per precision; the double column is the
    // original certified gate. The converge_css / run_amfm_dt helpers are
    // already generic on the processor, so compliance_dut<Sample> drops in.
    using sample_types = ::testing::Types<float, double>;
    template <typename T>
    class ItuDoubleTalk : public ::testing::Test {};
    TYPED_TEST_SUITE(ItuDoubleTalk, sample_types);

    template <typename Proc>
    void converge_css(Proc& p, echo_sim<double>& sim, const rate_setup& rs, unsigned seed = 501) {
        css_config cc;
        cc.periods = 30;
        cc.shaped  = true;
        cc.seed    = seed;
        auto xc    = make_css_at(cc, rs.fs);
        set_level_dbm0(xc, -16.0);
        run_chain_on(sim, p, rs.block, xc);
    }

    struct dt_out {
        std::vector<double> x;
        std::vector<double> v;
        std::vector<double> out;
        size_t              n = 0;
    };

    template <typename Proc>
    dt_out run_amfm_dt(Proc& p, echo_sim<double>& sim, const rate_setup& rs, double send_dbpa) {
        dt_out o;
        o.n = static_cast<size_t>(8 * rs.fs);
        o.x = make_amfm(amfm_receive_plan(), o.n, rs.fs);
        set_level_dbm0(o.x, -16.0);
        o.v             = make_amfm(amfm_send_plan(), o.n, rs.fs);
        const double vg = dbpa_to_rms(send_dbpa) / rms_of(o.v.data(), o.v.size());
        for (auto& s : o.v) {
            s *= vg;
        }
        auto rr = run_chain_on(sim, p, rs.block, o.x, &o.v);
        o.out   = std::move(rr.out);
        return o;
    }

    std::vector<double> settled_half(const std::vector<double>& s, size_t n) {
        return {s.begin() + static_cast<long>(n / 2), s.begin() + static_cast<long>(n)};
    }

    // ITU_DtSendAtten (integrated) + ITU_DtSentSpeech (per send band
    // 200-6900 Hz): near end at -1.7 dBPa (loud), requirement <= 3 dB,
    // target <= 1.5. One run grades both.
    TYPED_TEST(ItuDoubleTalk, SendAttenuationDuringDoubleTalk) {
        using Sample = TypeParam;
        for (const auto& rs : required_rates()) {
            compliance_dut<Sample>            c(chain_config<Sample>(rs), rs.block);
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            converge_css(c, sim, rs);
            const auto o  = run_amfm_dt(c, sim, rs, -1.7);
            const auto vh = settled_half(o.v, o.n);
            const auto oh = settled_half(o.out, o.n);
            const auto sp = amfm_send_plan();

            const double integ = comb_band_level_db(vh, sp, 0.0, rs.fs) - comb_band_level_db(oh, sp, 0.0, rs.fs);
            measure<Sample>("DtSendAtten.integ", rs, integ);
            EXPECT_LE(integ, expected<Sample>({{1.5, 1.5}, {1.5, 1.5}}, rs)) << "fs " << rs.fs; // req <=3, target <=1.5

            double worst = -1e9;
            for (size_t b = 0; b < sp.f0.size(); ++b) {
                if (sp.f0[b] < 200.0 || sp.f0[b] > 6900.0 || sp.f0[b] > rs.fs / 2 * 0.9) {
                    continue;
                }
                amfm_plan one;
                one.f0.push_back(sp.f0[b]);
                one.df.push_back(sp.df[b]);
                worst =
                    std::max(worst, comb_band_level_db(vh, one, 0.0, rs.fs) - comb_band_level_db(oh, one, 0.0, rs.fs));
            }
            measure<Sample>("DtSentSpeech.worst_band", rs, worst);
            EXPECT_LE(worst, expected<Sample>({{2.0, 2.5}, {2.0, 2.5}}, rs)) << "fs " << rs.fs;
        }
    }

    // ITU_DtEchoLoss: near end at -25.7 dBPa (the clause's quiet
    // competing talker), echo loss >= 27 dB required / >= 33 target in
    // EACH receive band 200-6950 Hz.
    TYPED_TEST(ItuDoubleTalk, EchoLossDuringDoubleTalkPerBand) {
        using Sample = TypeParam;
        for (const auto& rs : required_rates()) {
            compliance_dut<Sample>            c(chain_config<Sample>(rs), rs.block);
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            converge_css(c, sim, rs);
            const auto o     = run_amfm_dt(c, sim, rs, -25.7);
            const auto xh    = settled_half(o.x, o.n);
            const auto oh    = settled_half(o.out, o.n);
            const auto rp    = amfm_receive_plan();
            double     worst = 1e9;
            for (size_t b = 0; b < rp.f0.size(); ++b) {
                if (rp.f0[b] < 200.0 || rp.f0[b] > 6950.0 || rp.f0[b] > rs.fs / 2 * 0.9) {
                    continue;
                }
                amfm_plan one;
                one.f0.push_back(rp.f0[b]);
                one.df.push_back(rp.df[b]);
                worst =
                    std::min(worst, comb_band_level_db(xh, one, 0.0, rs.fs) - comb_band_level_db(oh, one, 0.0, rs.fs));
            }
            measure<Sample>("DtEchoLoss.worst_band", rs, worst);
            EXPECT_GE(worst, expected<Sample>({{33.0, 33.0}, {33.0, 33.0}}, rs)) << "fs " << rs.fs;
        }
    }

    // ITU_P340_Type1Transfer (P.340 Table 5, Behaviour 1): the send
    // transfer function stays constant between double talk and near-end
    // single talk, +-3 dB required / +-1.5 target per third-octave band.
    // Near end = DT CSS; far end = single-talk CSS with a DIFFERENT PN
    // seed — with the default seed both deterministic PN segments
    // correlate and the canceller subtracts near-end content the far end
    // predicts (a simulation artifact worth 0.5 dB, recorded here).
    TYPED_TEST(ItuDoubleTalk, TransferFunctionConstancy) {
        using Sample = TypeParam;
        for (const auto& rs : required_rates()) {
            compliance_dut<Sample>            c(chain_config<Sample>(rs), rs.block);
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            converge_css(c, sim, rs, 777);

            const size_t n_half = static_cast<size_t>(4 * rs.fs);
            css_config   cd;
            cd.periods = 30;
            cd.kind    = css_kind::double_talk;
            cd.shaped  = true;
            auto v     = make_css_at(cd, rs.fs);
            v.resize(2 * n_half, 0.0);
            const double vg = dbpa_to_rms(-1.7) / rms_of(v.data(), v.size() / 2);
            for (auto& s : v) {
                s *= vg;
            }
            css_config cc;
            cc.periods = 30;
            cc.shaped  = true;
            cc.seed    = 777;
            auto x     = make_css_at(cc, rs.fs);
            x.resize(2 * n_half);
            for (size_t i = n_half; i < x.size(); ++i) {
                x[i] = 0.0;
            }
            set_level_dbm0(x, -16.0);
            auto rr = run_chain_on(sim, c, rs.block, x, &v);

            auto ph = [&](const std::vector<double>& s, size_t a, size_t b) {
                return std::vector<double>(s.begin() + static_cast<long>(a), s.begin() + static_cast<long>(b));
            };
            const double fmax = std::min(6300.0, rs.fs / 2 * 0.7);
            const auto   t_dt =
                attenuation_spectrum(ph(v, static_cast<size_t>(rs.fs), n_half),
                                     ph(rr.out, static_cast<size_t>(rs.fs), n_half), rs.fs, 8192, 200.0, fmax);
            const auto t_st  = attenuation_spectrum(ph(v, n_half + static_cast<size_t>(rs.fs), 2 * n_half),
                                                    ph(rr.out, n_half + static_cast<size_t>(rs.fs), 2 * n_half), rs.fs,
                                                    8192, 200.0, fmax);
            double     worst = 0.0;
            for (size_t i = 0; i < t_dt.f_center.size() && i < t_st.f_center.size(); ++i) {
                worst = std::max(worst, std::abs(t_dt.atten_db[i] - t_st.atten_db[i]));
            }
            measure<Sample>("TransferConstancy.worst", rs, worst);
            EXPECT_LE(worst, expected<Sample>({{3.0, 3.0}, {3.0, 3.0}}, rs)) << "fs " << rs.fs;
        }
    }

} // namespace
