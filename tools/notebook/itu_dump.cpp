// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Stage 4 measurement dump for the ITU compliance proof notebook
// (tools/notebook/build_itu_compliance.py -> notebooks/itu_compliance.ipynb).
//
// This program re-runs the Stage 3/3b compliance scenarios on the SAME
// pinned chain the test suite measures (tests/support/itu_chain.h) and
// writes every measured number and level trace the notebook renders as
// one JSON document. The notebook never re-implements a meter or a
// scenario in Python: the C++ battery here is the single source of
// truth, and the gtest files (tests/test_itu_*.cpp, tests/test_g168.cpp)
// remain the assertion authority — scenario recipes here mirror those
// tests line for line, so a number in the notebook is the number the
// suite gates.
//
// Usage: itu_dump <output.json>   (runs ~6 minutes — CSS generation
// through the NOTE 2 resampler dominates, hence the memo cache below;
// all seeds fixed, so the output is deterministic for a given library
// state)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "support/closed_loop.h"
#include "support/itu_chain.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::itu;

    // ------------------------------------------------------------- JSON
    // A minimal writer: the document structure is static, so plain
    // string assembly beats a dependency.

    std::string jnum(double v) {
        if (!std::isfinite(v)) {
            return "null";
        }
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", v);
        return {b};
    }

    std::string jarr(const std::vector<double>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i != 0) {
                s += ",";
            }
            s += jnum(v[i]);
        }
        return s + "]";
    }

    std::string jbools(const std::vector<bool>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i != 0) {
                s += ",";
            }
            s += v[i] ? "true" : "false";
        }
        return s + "]";
    }

    struct jobj {
        std::string s     = "{";
        bool        first = true;
        void        add(const std::string& k, const std::string& raw) {
            if (!first) {
                s += ",";
            }
            first = false;
            s += "\"" + k + "\":" + raw;
        }
        void        add(const std::string& k, double v) { add(k, jnum(v)); }
        std::string str() const { return s + "}"; }
    };

    /// A level trace decimated for plotting: {"t0":..,"dt":..,"v":[..]}.
    std::string jtrace(const std::vector<double>& tr, double fs, double plot_rate = 200.0, double t0 = 0.0) {
        const size_t        step = std::max<size_t>(1, static_cast<size_t>(fs / plot_rate));
        std::vector<double> v;
        v.reserve(tr.size() / step + 1);
        for (size_t i = 0; i < tr.size(); i += step) {
            v.push_back(tr[i]);
        }
        jobj o;
        o.add("t0", t0);
        o.add("dt", static_cast<double>(step) / fs);
        o.add("v", jarr(v));
        return o.str();
    }

    // ------------------------------------------- shared scenario helpers
    // Mirrors of the file-local helpers in tests/test_g168.cpp and
    // tests/test_itu_doubletalk.cpp — those tests are the source of
    // truth; keep any change here in lockstep.

    struct raw_kalman {
        mutap::partitioned_fdkf<double> core;
        explicit raw_kalman(const compliance_chain::config& c)
            : core(c.canceller) {}
        void process_block(const double* x, const double* y, double* e) noexcept { core.process_block(x, y, e); }
    };

    /// Memoized make_css_at: CSS generation resamples from the 44.1 kHz
    /// native tables through the NOTE 2 windowed-sinc interpolator, which
    /// dominates this program's runtime (not the chain), and the battery
    /// requests the same (kind, periods, seed, rate) dozens of times.
    /// The cache returns a copy of the identical buffer the direct call
    /// would produce; callers scale their copy afterwards.
    std::vector<double> make_css_cached(const css_config& cc, double fs) {
        using key = std::tuple<int, size_t, bool, unsigned, double>;
        static std::map<key, std::vector<double>> cache;
        const key k{static_cast<int>(cc.kind), cc.periods, cc.shaped, cc.seed, fs};
        auto      it = cache.find(k);
        if (it == cache.end()) {
            it = cache.emplace(k, make_css_at(cc, fs)).first;
        }
        return it->second;
    }

    double fig9(double l_act) {
        return l_act <= -10.0 ? -65.0 : -65.0 + (l_act + 10.0);
    }
    double fig11(double l_act) {
        return -55.0 + (l_act + 30.0) * 25.0 / 30.0;
    }

    std::vector<double> erl_path(room r, const rate_setup& rs, double erl_db) {
        auto p = compliance_path(r, rs);
        for (auto& v : p) {
            v *= std::pow(10.0, -erl_db / 20.0);
        }
        return p;
    }

    std::vector<double> css_at_act(double l_act, size_t periods, const rate_setup& rs) {
        css_config cc;
        cc.periods = periods;
        cc.shaped  = true;
        auto x     = make_css_cached(cc, rs.fs);
        set_level_dbm0(x, l_act - 1.49); // LRin,act convention
        return x;
    }

    std::vector<double> g168_meter(const std::vector<double>& out, double fs) {
        exp_level_meter m(fs, 0.035);
        return m.trace_dbm0(out);
    }

    double max_in(const std::vector<double>& tr, double fs, double t0, double t1) {
        double v = -1e9;
        for (size_t i = static_cast<size_t>(t0 * fs); i < std::min(tr.size(), static_cast<size_t>(t1 * fs)); ++i) {
            v = std::max(v, tr[i]);
        }
        return v;
    }

    compliance_chain::config nlp_on_cfg(const rate_setup& rs) {
        auto cfg                     = chain_config(rs);
        cfg.postfilter.comfort_noise = false; // Figure 9's instruction
        return cfg;
    }

    template <typename Proc>
    void converge_css(Proc& p, echo_sim<double>& sim, const rate_setup& rs, unsigned seed = 501) {
        css_config cc;
        cc.periods = 30;
        cc.shaped  = true;
        cc.seed    = seed;
        auto xc    = make_css_cached(cc, rs.fs);
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

    /// ERL(t) curve sampled every `step_s` for the convergence figures.
    std::string erl_curve(const erl_reader& erl, double t_lo, double t_hi, double step_s = 0.05) {
        std::vector<double> t;
        std::vector<double> v;
        for (double x = t_lo; x <= t_hi; x += step_s) {
            t.push_back(x);
            v.push_back(erl.by(x));
        }
        jobj o;
        o.add("t", jarr(t));
        o.add("erl", jarr(v));
        return o.str();
    }

    // ------------------------------------------------ scenario batteries
    // Each function returns one JSON object; recipes mirror the named
    // gtest exactly (file/test in the comment).

    // ItuEcho.ConvergenceQuiet
    std::string dump_conv_quiet(const rate_setup& rs) {
        compliance_chain c(chain_config(rs));
        const auto       path = compliance_path(room::cabin, rs);
        css_config       cc;
        cc.periods = 12;
        cc.shaped  = true;
        auto x     = make_css_cached(cc, rs.fs);
        set_level_dbm0(x, -16.0);
        auto       rr = run_chain(c, path, rs.block, x);
        erl_reader erl(rr.echo, rr.out, rs.fs);
        jobj       o;
        o.add("curve", erl_curve(erl, 0.4, static_cast<double>(rr.out.size()) / rs.fs - 0.01));
        o.add("by600", erl.by(0.6));
        o.add("by1200", erl.by(1.2));
        return o.str();
    }

    // ItuEcho.ConvergenceNoise (driving noise at -30 dBm0(A))
    std::string dump_conv_noise(const rate_setup& rs) {
        compliance_chain c(chain_config(rs));
        const auto       path = compliance_path(room::cabin, rs);
        const size_t     pre  = static_cast<size_t>(2.0 * rs.fs);
        css_config       cc;
        cc.periods = 12;
        cc.shaped  = true;
        auto css   = make_css_cached(cc, rs.fs);
        set_level_dbm0(css, -16.0);
        std::vector<double> x(pre, 0.0);
        x.insert(x.end(), css.begin(), css.end());
        auto noise = make_driving_noise(x.size(), 7, rs.fs);
        {
            a_weighting  aw(rs.fs);
            auto         na  = aw.apply(noise);
            const double cur = level_dbov(na.data(), na.size()) - k_dbov_per_dbm0;
            const double g   = std::pow(10.0, (-30.0 - cur) / 20.0);
            for (auto& v : noise) {
                v *= g;
            }
        }
        auto rr = run_chain(c, path, rs.block, x, &noise);
        auto tr = level_trace_dbm0a(rr.out, rs.fs);
        jobj o;
        o.add("trace", jtrace(tr, rs.fs));
        o.add("ref", max_in(tr, rs.fs, 1.0, 2.0));
        o.add("onset", max_in(tr, rs.fs, 2.1, 2.2));
        o.add("by750", max_in(tr, rs.fs, 2.75, 3.0));
        o.add("by1500", max_in(tr, rs.fs, 3.5, 4.0));
        o.add("t_start", static_cast<double>(pre) / rs.fs);
        return o.str();
    }

    // G168Adapted.ConvergenceNlpOn (Figure 9): levels swept, trace at -16.
    std::string dump_fig9(const rate_setup& rs) {
        const auto          cab = erl_path(room::cabin, rs, 6.0);
        std::vector<double> levels{-30.0, -16.0, -6.0};
        std::vector<double> steady;
        std::vector<double> loss50;
        std::vector<double> loss1s;
        std::vector<double> mask;
        std::string         trace16;
        for (double l : levels) {
            compliance_chain c(nlp_on_cfg(rs));
            auto             x  = css_at_act(l, 20, rs);
            auto             rr = run_chain(c, cab, rs.block, x);
            auto             tr = g168_meter(rr.out, rs.fs);
            loss50.push_back(l - max_in(tr, rs.fs, 0.0, 0.05));
            loss1s.push_back(l - max_in(tr, rs.fs, 0.05, 1.0));
            steady.push_back(max_in(tr, rs.fs, 1.4, 6.9));
            mask.push_back(fig9(l));
            if (l == -16.0) {
                trace16 = jtrace(tr, rs.fs);
            }
        }
        jobj o;
        o.add("levels", jarr(levels));
        o.add("steady", jarr(steady));
        o.add("loss50", jarr(loss50));
        o.add("loss1s", jarr(loss1s));
        o.add("mask", jarr(mask));
        o.add("trace16", trace16);
        return o.str();
    }

    // G168Adapted.ConvergenceNlpOff (Figure 11): bare canceller at -16.
    std::string dump_fig11(const rate_setup& rs) {
        const auto   cab = erl_path(room::cabin, rs, 6.0);
        const double l   = -16.0;
        raw_kalman   b(chain_config(rs));
        auto         x  = css_at_act(l, 33, rs);
        auto         rr = run_chain(b, cab, rs.block, x);
        auto         tr = g168_meter(rr.out, rs.fs);
        jobj         o;
        o.add("level", l);
        o.add("trace", jtrace(tr, rs.fs));
        o.add("loss50", l - max_in(tr, rs.fs, 0.0, 0.05));
        o.add("loss_1_10", l - max_in(tr, rs.fs, 1.0, 10.0));
        o.add("steady", max_in(tr, rs.fs, 10.0, 11.2));
        o.add("mask", fig11(l));
        return o.str();
    }

    // G168Adapted.ReConvergenceAfterPathChange — the documented deviation.
    std::string dump_reconv(const rate_setup& rs) {
        const auto                        cab = erl_path(room::cabin, rs, 6.0);
        const double                      l   = -16.0;
        compliance_chain                  c(nlp_on_cfg(rs));
        typename echo_sim<double>::config sc;
        sc.echo_path  = cab;
        sc.block_size = rs.block;
        echo_sim<double> sim(sc);
        run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
        auto stu16 = erl_path(room::studio, rs, 16.0);
        sim.set_echo_path(stu16.data(), stu16.size());
        auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 30, rs));
        auto tr = g168_meter(rr.out, rs.fs);
        jobj o;
        o.add("level", l);
        o.add("trace", jtrace(tr, rs.fs));
        o.add("loss_0_1", l - max_in(tr, rs.fs, 0.0, 1.0));
        o.add("loss_1_2", l - max_in(tr, rs.fs, 1.0, 2.0));
        o.add("steady_8_105", max_in(tr, rs.fs, 8.0, 10.5));
        return o.str();
    }

    // ItuEcho.{Tcl, EchoLevel, EchoStability, EchoSpectral}
    std::string dump_echo_steady(const rate_setup& rs) {
        jobj o;
        { // Tcl
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cc;
            cc.periods = static_cast<size_t>(27.0 / 0.35);
            cc.shaped  = true;
            auto x     = make_css_cached(cc, rs.fs);
            set_level_dbm0(x, -10.0);
            auto rr = run_chain(c, path, rs.block, x);

            const size_t        from = static_cast<size_t>(17.0 * rs.fs);
            std::vector<double> in_seg(x.begin() + static_cast<long>(from),
                                       x.begin() + static_cast<long>(rr.out.size()));
            std::vector<double> out_seg(rr.out.begin() + static_cast<long>(from), rr.out.end());
            const size_t        nfft = 8192;
            const auto          pin  = welch_psd_db(in_seg, nfft);
            const auto          pout = welch_psd_db(out_seg, nfft);
            const size_t        k0   = static_cast<size_t>(100.0 * static_cast<double>(nfft) / rs.fs);
            const size_t        k1 =
                std::min(pout.size() - 1, static_cast<size_t>(8000.0 * static_cast<double>(nfft) / rs.fs));
            double sr = 0.0;
            double so = 0.0;
            for (size_t k = k0; k < k1; ++k) {
                sr += std::pow(10.0, pin[k] / 10.0);
                so += std::pow(10.0, pout[k] / 10.0);
            }
            o.add("tcl", 10.0 * std::log10(sr / so));
        }
        for (room r : {room::cabin, room::studio}) { // EchoLevel
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(r, rs);
            css_config       cc;
            cc.periods = 40;
            cc.shaped  = true;
            auto x     = make_css_cached(cc, rs.fs);
            set_level_dbm0(x, -16.0);
            auto rr = run_chain(c, path, rs.block, x);
            o.add(r == room::cabin ? "level_cabin" : "level_studio",
                  max_level_dbm0a(rr.out, rs.fs, rr.out.size() * 2 / 3, rr.out.size()));
        }
        { // EchoStability
            compliance_chain                  c(chain_config(rs));
            const auto                        path = compliance_path(room::cabin, rs);
            typename echo_sim<double>::config sc;
            sc.echo_path  = path;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto xc    = make_css_cached(cc, rs.fs);
            set_level_dbm0(xc, -16.0);
            run_chain_on(sim, c, rs.block, xc);
            std::vector<double> levels{-5.0, -16.0, -25.0};
            std::vector<double> vari;
            for (double lvl : levels) {
                cc.periods = 16;
                auto x     = make_css_cached(cc, rs.fs);
                set_level_dbm0(x, lvl);
                auto       rr = run_chain_on(sim, c, rs.block, x);
                erl_reader erl(rr.echo, rr.out, rs.fs);
                double     amax = -1e9;
                double     amin = 1e9;
                for (double t = 1.75; t <= static_cast<double>(rr.out.size()) / rs.fs; t += 0.35) {
                    const double a = erl.by(t);
                    amax           = std::max(amax, a);
                    amin           = std::min(amin, a);
                }
                vari.push_back(amax - amin);
            }
            o.add("stability_levels", jarr(levels));
            o.add("stability_var", jarr(vari));
        }
        { // EchoSpectral
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto x     = make_css_cached(cc, rs.fs);
            set_level_dbm0(x, -16.0);
            auto rr = run_chain(c, path, rs.block, x);

            const size_t        from = rr.out.size() - static_cast<size_t>(4 * 0.35 * rs.fs);
            std::vector<double> ref(rr.echo.begin() + static_cast<long>(from), rr.echo.end());
            std::vector<double> out(rr.out.begin() + static_cast<long>(from), rr.out.end());
            const auto sp = attenuation_spectrum(ref, out, rs.fs, 8192, 100.0, std::min(8000.0, rs.fs / 2 * 0.94));
            const freq_mask     mask{{100, 1300, 3450, 5200, 7500, 8000}, {41, 41, 46, 46, 37, 37}};
            std::vector<double> mv;
            for (double fc : sp.f_center) {
                mv.push_back(mask.at(fc));
            }
            jobj s;
            s.add("f", jarr(sp.f_center));
            s.add("atten", jarr(sp.atten_db));
            s.add("mask", jarr(mv));
            o.add("spectral", s.str());
        }
        return o.str();
    }

    // ItuDoubleTalk.* + ItuDynamics.{SendActivationBuildUp, HangoverRecoveryAfterDoubleTalk}
    std::string dump_doubletalk(const rate_setup& rs) {
        jobj o;
        { // SendAttenuationDuringDoubleTalk: integrated + per band
            compliance_chain                  c(chain_config(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            converge_css(c, sim, rs);
            const auto ot = run_amfm_dt(c, sim, rs, -1.7);
            const auto vh = settled_half(ot.v, ot.n);
            const auto oh = settled_half(ot.out, ot.n);
            const auto sp = amfm_send_plan();
            o.add("send_integ", comb_band_level_db(vh, sp, 0.0, rs.fs) - comb_band_level_db(oh, sp, 0.0, rs.fs));
            std::vector<double> f;
            std::vector<double> att;
            for (size_t b = 0; b < sp.f0.size(); ++b) {
                if (sp.f0[b] < 200.0 || sp.f0[b] > 6900.0 || sp.f0[b] > rs.fs / 2 * 0.9) {
                    continue;
                }
                amfm_plan one;
                one.f0.push_back(sp.f0[b]);
                one.df.push_back(sp.df[b]);
                f.push_back(sp.f0[b]);
                att.push_back(comb_band_level_db(vh, one, 0.0, rs.fs) - comb_band_level_db(oh, one, 0.0, rs.fs));
            }
            jobj bands;
            bands.add("f", jarr(f));
            bands.add("atten", jarr(att));
            o.add("send_bands", bands.str());
        }
        { // EchoLossDuringDoubleTalkPerBand
            compliance_chain                  c(chain_config(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            converge_css(c, sim, rs);
            const auto          ot = run_amfm_dt(c, sim, rs, -25.7);
            const auto          xh = settled_half(ot.x, ot.n);
            const auto          oh = settled_half(ot.out, ot.n);
            const auto          rp = amfm_receive_plan();
            std::vector<double> f;
            std::vector<double> loss;
            for (size_t b = 0; b < rp.f0.size(); ++b) {
                if (rp.f0[b] < 200.0 || rp.f0[b] > 6950.0 || rp.f0[b] > rs.fs / 2 * 0.9) {
                    continue;
                }
                amfm_plan one;
                one.f0.push_back(rp.f0[b]);
                one.df.push_back(rp.df[b]);
                f.push_back(rp.f0[b]);
                loss.push_back(comb_band_level_db(xh, one, 0.0, rs.fs) - comb_band_level_db(oh, one, 0.0, rs.fs));
            }
            jobj bands;
            bands.add("f", jarr(f));
            bands.add("loss", jarr(loss));
            o.add("echo_loss_bands", bands.str());
        }
        { // TransferFunctionConstancy (Type 1)
            compliance_chain                  c(chain_config(rs));
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
            auto v     = make_css_cached(cd, rs.fs);
            v.resize(2 * n_half, 0.0);
            const double vg = dbpa_to_rms(-1.7) / rms_of(v.data(), v.size() / 2);
            for (auto& s : v) {
                s *= vg;
            }
            css_config cc;
            cc.periods = 30;
            cc.shaped  = true;
            cc.seed    = 777;
            auto x     = make_css_cached(cc, rs.fs);
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
            jobj t1;
            t1.add("f", jarr(t_dt.f_center));
            t1.add("dt", jarr(t_dt.atten_db));
            t1.add("st", jarr(t_st.atten_db));
            t1.add("worst", worst);
            o.add("type1", t1.str());
        }
        { // HangoverRecoveryAfterDoubleTalk
            compliance_chain                  c(chain_config(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = compliance_path(room::cabin, rs);
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            css_config       cc;
            cc.periods = 30;
            cc.shaped  = true;
            auto xc    = make_css_cached(cc, rs.fs);
            set_level_dbm0(xc, -16.0);
            run_chain_on(sim, c, rs.block, xc);

            const size_t n_dt = static_cast<size_t>(4 * rs.fs);
            const size_t n_st = static_cast<size_t>(2 * rs.fs);
            auto         x    = make_amfm(amfm_receive_plan(), n_dt + n_st, rs.fs);
            set_level_dbm0(x, -16.0);
            auto         v  = make_amfm(amfm_send_plan(), n_dt + n_st, rs.fs);
            const double vg = dbpa_to_rms(-1.7) / rms_of(v.data(), v.size());
            for (auto& s : v) {
                s *= vg;
            }
            for (size_t i = n_dt; i < v.size(); ++i) {
                v[i] = 0.0;
            }
            auto         rr    = run_chain_on(sim, c, rs.block, x, &v);
            erl_reader   erl(rr.echo, rr.out, rs.fs);
            const double t_end = static_cast<double>(n_dt) / rs.fs;
            jobj         h;
            h.add("curve", erl_curve(erl, 0.4, static_cast<double>(rr.out.size()) / rs.fs - 0.01));
            h.add("t_end", t_end);
            h.add("at_05", erl.by(t_end + 0.5));
            h.add("at_10", erl.by(t_end + 1.0));
            o.add("hangover", h.str());
        }
        { // SendActivationBuildUp
            compliance_chain c(chain_config(rs));
            const auto       path = compliance_path(room::cabin, rs);
            css_config       cd;
            cd.periods      = 10;
            cd.kind         = css_kind::double_talk;
            cd.shaped       = true;
            auto         v  = make_css_cached(cd, rs.fs);
            const double vg = dbpa_to_rms(-26.0) / rms_of(v.data(), v.size());
            for (auto& s : v) {
                s *= vg;
            }
            const size_t        pre = static_cast<size_t>(1.0 * rs.fs);
            std::vector<double> vv(pre, 0.0);
            vv.insert(vv.end(), v.begin(), v.end());
            std::vector<double> x(vv.size(), 0.0);
            auto                rr = run_chain(c, path, rs.block, x, &vv);

            a_weighting     aw(rs.fs);
            auto            seg = aw.apply(rr.out);
            exp_level_meter m(rs.fs, 0.005);
            const auto      tr = m.trace_dbm0(seg);
            const long      s0 = static_cast<long>(pre) + static_cast<long>(rs.fs);
            std::vector<double> settled(tr.begin() + s0, tr.begin() + s0 + static_cast<long>(rs.fs));
            std::nth_element(settled.begin(), settled.begin() + static_cast<long>(settled.size() / 2), settled.end());
            const double target = settled[settled.size() / 2];
            size_t       t_hit  = tr.size();
            for (size_t i = pre; i < tr.size(); ++i) {
                if (tr[i] >= target - 3.0) {
                    t_hit = i - pre;
                    break;
                }
            }
            // Dump the trace around the onset only, at 2 kHz plot rate.
            const size_t        a = pre - static_cast<size_t>(0.05 * rs.fs);
            const size_t        b = pre + static_cast<size_t>(0.25 * rs.fs);
            std::vector<double> win(tr.begin() + static_cast<long>(a), tr.begin() + static_cast<long>(b));
            jobj                bu;
            bu.add("trace", jtrace(win, rs.fs, 2000.0, -0.05));
            bu.add("target", target);
            bu.add("t_hit_ms", 1000.0 * static_cast<double>(t_hit) / rs.fs);
            o.add("buildup", bu.str());
        }
        return o.str();
    }

    // ItuDynamics.{ComfortNoiseLevelAndSpectrum, TransmittedNoiseFluctuation,
    // NoisePumpingAcrossFarEndBursts} + G168Adapted.ComfortNoiseTracksBackground
    std::string dump_noise(const rate_setup& rs) {
        jobj o;
        { // ComfortNoiseLevelAndSpectrum
            compliance_chain c(chain_config(rs));
            const auto       path   = compliance_path(room::cabin, rs);
            const size_t     n_half = static_cast<size_t>(10 * rs.fs);
            css_config       cc;
            cc.periods = 28;
            cc.shaped  = true;
            auto x     = make_css_cached(cc, rs.fs);
            set_level_dbm0(x, -16.0);
            x.resize(2 * n_half, 0.0);
            auto noise = make_hoth_noise(2 * n_half, 7, rs.fs);
            set_level_dbm0(noise, -46.0);
            auto rr  = run_chain(c, path, rs.block, x, &noise);
            auto seg = [&](double t0, double t1) {
                return std::vector<double>(rr.out.begin() + static_cast<long>(t0 * rs.fs),
                                           rr.out.begin() + static_cast<long>(t1 * rs.fs));
            };
            auto talk  = seg(6.0, 9.5);
            auto quiet = seg(16.0, 19.5);

            a_weighting aw(rs.fs);
            auto        ta = aw.apply(talk);
            auto        qa = aw.apply(quiet);
            o.add("cn_delta", level_dbov(ta.data(), ta.size()) - level_dbov(qa.data(), qa.size()));

            const auto          bt   = welch_psd_db(talk, 8192);
            const auto          bq   = welch_psd_db(quiet, 8192);
            const double        fmax = std::min(8000.0, rs.fs / 2 * 0.94);
            const double        edges[] = {200, 400, 800, 1600, 3150, 6300, 8000};
            std::vector<double> be;
            std::vector<double> bd;
            for (size_t b = 0; b + 1 < 7; ++b) {
                if (edges[b] >= fmax) {
                    break;
                }
                const size_t k0 = static_cast<size_t>(edges[b] * 8192 / rs.fs);
                const size_t k1 =
                    std::min(bq.size() - 1, static_cast<size_t>(std::min(edges[b + 1], fmax) * 8192 / rs.fs));
                double st = 0.0;
                double sq = 0.0;
                for (size_t k = k0; k < k1; ++k) {
                    st += std::pow(10.0, bt[k] / 10.0);
                    sq += std::pow(10.0, bq[k] / 10.0);
                }
                be.push_back(edges[b]);
                bd.push_back(10.0 * std::log10(st / sq));
            }
            jobj bands;
            bands.add("edges", jarr(be));
            bands.add("delta", jarr(bd));
            o.add("cn_bands", bands.str());
            // The PSD overlay for the figure, decimated 8x above bin 32.
            std::vector<double> pf;
            std::vector<double> pt;
            std::vector<double> pq;
            for (size_t k = 1; k < bt.size(); k += (k < 32 ? 1 : 8)) {
                pf.push_back(static_cast<double>(k) * rs.fs / 8192.0);
                pt.push_back(bt[k]);
                pq.push_back(bq[k]);
            }
            jobj psd;
            psd.add("f", jarr(pf));
            psd.add("talk", jarr(pt));
            psd.add("quiet", jarr(pq));
            o.add("cn_psd", psd.str());
        }
        { // TransmittedNoiseFluctuation
            compliance_chain    c(chain_config(rs));
            const auto          path = compliance_path(room::cabin, rs);
            const size_t        n    = static_cast<size_t>(10 * rs.fs);
            std::vector<double> x(n, 0.0);
            auto                noise = make_hoth_noise(n, 7, rs.fs);
            set_level_dbm0(noise, -46.0);
            auto   rr   = run_chain(c, path, rs.block, x, &noise);
            auto   tr   = level_trace_dbm0a(rr.out, rs.fs);
            double vmax = -1e9;
            double vmin = 1e9;
            for (size_t i = static_cast<size_t>(1.0 * rs.fs); i < tr.size(); ++i) {
                vmax = std::max(vmax, tr[i]);
                vmin = std::min(vmin, tr[i]);
            }
            o.add("fluct_span", vmax - vmin);
        }
        { // NoisePumpingAcrossFarEndBursts
            compliance_chain c(chain_config(rs));
            const auto       path  = compliance_path(room::cabin, rs);
            const double     pre   = 15.0;
            const size_t     n     = static_cast<size_t>((pre + 16.0) * rs.fs);
            auto             noise = make_driving_noise(n, 7, rs.fs);
            {
                a_weighting  aw(rs.fs);
                auto         na  = aw.apply(noise);
                const double cur = level_dbov(na.data(), na.size()) - k_dbov_per_dbm0;
                const double g   = std::pow(10.0, (-30.0 - cur) / 20.0);
                for (auto& v : noise) {
                    v *= g;
                }
            }
            css_config cc;
            cc.periods = 90;
            cc.shaped  = true;
            auto css   = make_css_cached(cc, rs.fs);
            css.resize(n);
            set_level_dbm0(css, -16.0);
            std::vector<double> x(n, 0.0);
            for (size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / rs.fs;
                if (t < pre || std::fmod(t - pre, 4.0) < 2.0) {
                    x[i] = css[i];
                }
            }
            auto rr = run_chain(c, path, rs.block, x, &noise);

            a_weighting aw(rs.fs);
            auto        oa      = aw.apply(rr.out);
            auto        seg_lvl = [&](double t0, double t1) {
                const size_t a = static_cast<size_t>(t0 * rs.fs);
                const size_t b = static_cast<size_t>(t1 * rs.fs);
                double       p = 0.0;
                for (size_t i = a; i < b; ++i) {
                    p += oa[i] * oa[i];
                }
                return 10.0 * std::log10(p / static_cast<double>(b - a));
            };
            std::vector<double> seg_t;
            std::vector<double> seg_v;
            double              lmax = -1e9;
            double              lmin = 1e9;
            for (int k = 0; k < 4; ++k) {
                const double kk = static_cast<double>(k);
                const double lb = seg_lvl(pre + kk * 4.0 + 0.2, pre + kk * 4.0 + 2.0);
                const double ln = seg_lvl(pre + kk * 4.0 + 2.2, pre + kk * 4.0 + 3.8);
                seg_t.push_back(pre + kk * 4.0 + 1.1);
                seg_v.push_back(lb);
                seg_t.push_back(pre + kk * 4.0 + 3.0);
                seg_v.push_back(ln);
                lmax = std::max({lmax, lb, ln});
                lmin = std::min({lmin, lb, ln});
            }
            exp_level_meter m(rs.fs, 0.035);
            auto            tr = m.trace_dbm0(oa);
            jobj            p;
            p.add("trace", jtrace(tr, rs.fs, 100.0));
            p.add("seg_t", jarr(seg_t));
            p.add("seg_lvl", jarr(seg_v));
            p.add("span", lmax - lmin);
            p.add("t_pre", pre);
            o.add("pump", p.str());
        }
        { // G168Adapted.ComfortNoiseTracksBackground (step + ramp)
            const auto       cab = erl_path(room::cabin, rs, 6.0);
            const double     l   = -16.0;
            compliance_chain c(chain_config(rs));
            const size_t     n = static_cast<size_t>(34.0 * rs.fs);
            auto             x = css_at_act(l, 100, rs);
            x.resize(n);
            auto noise = make_hoth_noise(n, 7, rs.fs);
            set_level_dbm0(noise, -56.0);
            const double g10 = std::pow(10.0, 10.0 / 20.0);
            for (size_t i = static_cast<size_t>(8.0 * rs.fs); i < static_cast<size_t>(14.0 * rs.fs); ++i) {
                noise[i] *= g10;
            }
            for (size_t i = static_cast<size_t>(14.0 * rs.fs); i < n; ++i) {
                const double a = (static_cast<double>(i) / rs.fs - 14.0) / 20.0;
                noise[i] *= g10 * std::pow(10.0, -20.0 * std::min(1.0, a) / 20.0);
            }
            auto rr        = run_chain(c, cab, rs.block, x, &noise);
            auto tro       = g168_meter(rr.out, rs.fs);
            auto trn       = g168_meter(noise, rs.fs);
            auto seg_delta = [&](double t0, double t1) {
                double so = 0.0;
                double sn = 0.0;
                for (size_t i = static_cast<size_t>(t0 * rs.fs); i < static_cast<size_t>(t1 * rs.fs); ++i) {
                    so += std::pow(10.0, tro[i] / 10.0);
                    sn += std::pow(10.0, trn[i] / 10.0);
                }
                return 10.0 * std::log10(so / sn);
            };
            jobj t;
            t.add("trace_out", jtrace(tro, rs.fs, 50.0));
            t.add("trace_noise", jtrace(trn, rs.fs, 50.0));
            t.add("step_delta", seg_delta(11.0, 14.0));
            t.add("ramp_delta", seg_delta(31.0, 34.0));
            o.add("cn_track", t.str());
        }
        return o.str();
    }

    // ItuEcho.TimeVariantPath
    std::string dump_tvp(const rate_setup& rs) {
        compliance_chain                  c(chain_config(rs));
        const auto                        base = compliance_path(room::cabin, rs);
        typename echo_sim<double>::config sc;
        sc.echo_path  = base;
        sc.block_size = rs.block;
        echo_sim<double> sim(sc);
        css_config       cc;
        cc.periods = 30;
        cc.shaped  = true;
        auto xc    = make_css_cached(cc, rs.fs);
        set_level_dbm0(xc, -16.0);
        run_chain_on(sim, c, rs.block, xc);

        moving_reflector refl;
        refl.base     = base;
        refl.fs       = rs.fs;
        refl.tap_gain = 0.03;
        cc.periods    = 20;
        auto xm       = make_css_cached(cc, rs.fs);
        set_level_dbm0(xm, -16.0);
        std::vector<double> out;
        std::vector<double> p2(base);
        for (size_t blk = 0; blk + 1 <= xm.size() / rs.block; ++blk) {
            refl.fill(static_cast<double>(blk * rs.block) / rs.fs, p2);
            sim.set_echo_path(p2.data(), p2.size());
            sim.step(&xm[blk * rs.block], static_cast<const double*>(nullptr), &c);
            const auto& e = sim.error_block();
            out.insert(out.end(), e.begin(), e.end());
        }
        auto tr = level_trace_dbm0a(out, rs.fs);
        jobj o;
        o.add("trace", jtrace(tr, rs.fs));
        o.add("max_lvl", max_level_dbm0a(out, rs.fs, out.size() / 3, out.size()));
        return o.str();
    }

    // ItuDynamics.ClosedLoopStabilitySweep
    std::string dump_stability(const rate_setup& rs) {
        const auto path = compliance_path(room::cabin, rs);
        css_config cc;
        cc.periods      = 14;
        cc.shaped       = true;
        auto         v  = make_css_cached(cc, rs.fs);
        const double vg = dbpa_to_rms(-1.7) / rms_of(v.data(), v.size());
        for (auto& s : v) {
            s *= vg;
        }
        std::vector<double> erls;
        std::vector<bool>   stable;
        for (double erl = 50.0; erl >= -0.5; erl -= 5.0) {
            compliance_chain                         c(chain_config(rs));
            typename closed_loop_sim<double>::config lc;
            lc.feedback_path   = path;
            lc.block_size      = rs.block;
            lc.forward_delay   = rs.block;
            lc.forward_gain_db = -erl;
            lc.speaker_limit   = 4.0;
            closed_loop_sim<double> sim(lc);
            bool                    howl = false;
            for (size_t blk = 0; blk + 1 <= v.size() / rs.block; ++blk) {
                const double r = sim.step(&v[blk * rs.block], &c);
                if (!std::isfinite(r) || r > 1.0) {
                    howl = true;
                    break;
                }
            }
            erls.push_back(erl);
            stable.push_back(!howl);
        }
        jobj o;
        o.add("erl", jarr(erls));
        o.add("stable", jbools(stable));
        return o.str();
    }

    // The remaining G.168-adapted rows: scalar reads for the section table.
    std::string dump_g168_scalars(const rate_setup& rs) {
        const auto   cab = erl_path(room::cabin, rs, 6.0);
        const double l   = -16.0;
        jobj         o;
        { // Test 2C: convergence in noise
            compliance_chain c(chain_config(rs));
            auto             x     = css_at_act(l, 12, rs);
            auto             noise = make_hoth_noise(x.size(), 7, rs.fs);
            set_level_dbm0(noise, l - 15.0);
            exp_level_meter nm(rs.fs, 0.035);
            const auto      ntr   = nm.trace_dbm0(noise);
            const double    lsgen = *std::max_element(ntr.begin() + static_cast<long>(rs.fs), ntr.end());
            auto            rr    = run_chain(c, cab, rs.block, x, &noise);
            auto            tr    = g168_meter(rr.out, rs.fs);
            o.add("noise_conv_max", max_in(tr, rs.fs, 0.5, 1.0));
            o.add("noise_conv_lsgen", lsgen);
        }
        { // Test 3A: low near end does not block adaptation
            compliance_chain c(chain_config(rs));
            auto             x = css_at_act(l, 17, rs);
            css_config       cd;
            cd.periods = 17;
            cd.kind    = css_kind::double_talk;
            cd.shaped  = true;
            auto v     = make_css_cached(cd, rs.fs);
            v.resize(x.size(), 0.0);
            set_level_dbm0(v, l - 15.0 - 1.66);
            auto            rr = run_chain(c, cab, rs.block, x, &v);
            auto            tr = g168_meter(rr.out, rs.fs);
            exp_level_meter nm(rs.fs, 0.035);
            const auto      tv = nm.trace_dbm0(v);
            o.add("low_near_vmax", max_in(tv, rs.fs, 3.0, 5.5));
            o.add("low_near_early", max_in(tr, rs.fs, 1.25, 2.5));
            o.add("low_near_late", max_in(tr, rs.fs, 2.5, 5.0));
        }
        { // Test 3B: double-talk divergence bounded (bare canceller)
            raw_kalman                        b(chain_config(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, b, rs.block, css_at_act(l, 20, rs));
            auto       x2 = css_at_act(l, 12, rs);
            css_config cd;
            cd.periods = 12;
            cd.kind    = css_kind::double_talk;
            cd.shaped  = true;
            auto v     = make_css_cached(cd, rs.fs);
            v.resize(x2.size(), 0.0);
            set_level_dbm0(v, l - 1.66);
            run_chain_on(sim, b, rs.block, x2, &v);
            auto rr = run_chain_on(sim, b, rs.block, css_at_act(l, 9, rs));
            o.add("dt_div_lres", max_in(g168_meter(rr.out, rs.fs), rs.fs, 1.0, 2.0));
        }
        { // Test 3C: conversational alternation, no bursts
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
            double worst = -1e9;
            for (int cycle = 0; cycle < 3; ++cycle) {
                auto       xd = css_at_act(l, 6, rs);
                css_config cd;
                cd.periods = 6;
                cd.kind    = css_kind::double_talk;
                cd.shaped  = true;
                auto v     = make_css_cached(cd, rs.fs);
                v.resize(xd.size(), 0.0);
                set_level_dbm0(v, l - 1.66);
                run_chain_on(sim, c, rs.block, xd, &v);
                auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 6, rs));
                worst   = std::max(worst, max_in(g168_meter(rr.out, rs.fs), rs.fs, 0.0, 1.0));
            }
            o.add("altern_worst", worst);
        }
        { // Test 4: leak rate over 45 s of silence
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
            auto r1 = run_chain_on(sim, c, rs.block, css_at_act(l, 6, rs));
            o.add("leak_before", max_in(g168_meter(r1.out, rs.fs), rs.fs, 0.5, 2.0));
            std::vector<double> silence(static_cast<size_t>(45.0 * rs.fs), 0.0);
            run_chain_on(sim, c, rs.block, silence);
            auto r2 = run_chain_on(sim, c, rs.block, css_at_act(l, 6, rs));
            o.add("leak_after", max_in(g168_meter(r2.out, rs.fs), rs.fs, 0.5, 2.0));
        }
        { // Test 5A: echo path opened (ERL -> infinity)
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
            std::vector<double> open_path(cab.size(), 0.0);
            sim.set_echo_path(open_path.data(), open_path.size());
            auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 12, rs));
            o.add("inf_erl_loss", l - max_in(g168_meter(rr.out, rs.fs), rs.fs, 1.0, 4.0));
        }
        { // Test 5B: coupling-loss swings 6 <-> 46 dB
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
            auto   cab46 = erl_path(room::cabin, rs, 46.0);
            double worst = -1e9;
            for (int k = 0; k < 2; ++k) {
                sim.set_echo_path(cab46.data(), cab46.size());
                run_chain_on(sim, c, rs.block, css_at_act(l, 9, rs));
                sim.set_echo_path(cab.data(), cab.size());
                auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 9, rs));
                worst   = std::max(worst, max_in(g168_meter(rr.out, rs.fs), rs.fs, 1.0, 3.1));
            }
            o.add("swings_worst", worst);
        }
        { // Test 6: narrow-band (DTMF) signals do not corrupt
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double> sim(sc);
            run_chain_on(sim, c, rs.block, css_at_act(l, 20, rs));
            std::vector<double> tone(static_cast<size_t>(5.0 * rs.fs));
            for (size_t i = 0; i < tone.size(); ++i) {
                const double t = static_cast<double>(i) / rs.fs;
                tone[i] = std::sin(2.0 * std::numbers::pi * 770.0 * t) + std::sin(2.0 * std::numbers::pi * 1336.0 * t);
            }
            set_level_dbm0(tone, l);
            run_chain_on(sim, c, rs.block, tone);
            auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 12, rs));
            o.add("narrowband_after", max_in(g168_meter(rr.out, rs.fs), rs.fs, 0.0, 1.0));
        }
        { // Test 7: 30 s continuous 1 kHz tone from reset
            compliance_chain    c(nlp_on_cfg(rs));
            std::vector<double> tone(static_cast<size_t>(30.0 * rs.fs));
            for (size_t i = 0; i < tone.size(); ++i) {
                tone[i] = std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / rs.fs);
            }
            set_level_dbm0(tone, l);
            auto rr = run_chain(c, cab, rs.block, tone);
            o.add("tone_max", max_in(g168_meter(rr.out, rs.fs), rs.fs, 10.0, 30.0));
        }
        { // Test 12: acoustic three-phase A -> B -> A scenario
            compliance_chain                  c(nlp_on_cfg(rs));
            typename echo_sim<double>::config sc;
            sc.echo_path  = cab;
            sc.block_size = rs.block;
            echo_sim<double>                              sim(sc);
            auto                                          stu16  = erl_path(room::studio, rs, 16.0);
            const std::vector<const std::vector<double>*> phases = {&cab, &stu16, &cab};
            std::vector<double>                           p_loss;
            std::vector<double>                           p_steady;
            for (const auto* phase : phases) {
                sim.set_echo_path(phase->data(), phase->size());
                auto rr = run_chain_on(sim, c, rs.block, css_at_act(l, 12, rs));
                auto tr = g168_meter(rr.out, rs.fs);
                p_loss.push_back(l - max_in(tr, rs.fs, 0.0, 1.0));
                p_steady.push_back(max_in(tr, rs.fs, 1.4, 4.1));
            }
            o.add("three_phase_loss", jarr(p_loss));
            o.add("three_phase_steady", jarr(p_steady));
        }
        return o.str();
    }

    // ItuG167.HistoricalRowRunAndReported (48 kHz only, unit-energy cabin)
    std::string dump_g167() {
        const auto                        rs  = setup_48k();
        const auto                        cab = compliance_path(room::cabin, rs);
        compliance_chain                  c(chain_config(rs));
        typename echo_sim<double>::config sc;
        sc.echo_path  = cab;
        sc.block_size = rs.block;
        echo_sim<double> sim(sc);

        css_config cc;
        cc.periods = 30;
        cc.shaped  = true;
        auto x     = make_css_cached(cc, rs.fs);
        set_level_dbm0(x, -16.0);
        auto       rr = run_chain_on(sim, c, rs.block, x);
        erl_reader erl(rr.echo, rr.out, rs.fs);
        jobj       o;
        o.add("tic", erl.by(1.0));
        o.add("tclwst", erl.by(10.0));

        const size_t n  = static_cast<size_t>(6 * rs.fs);
        auto         xf = make_amfm(amfm_receive_plan(), n, rs.fs);
        set_level_dbm0(xf, -16.0);
        auto         v  = make_amfm(amfm_send_plan(), n, rs.fs);
        const double vg = dbpa_to_rms(-1.7) / rms_of(v.data(), v.size());
        for (auto& s : v) {
            s *= vg;
        }
        auto rd   = run_chain_on(sim, c, rs.block, xf, &v);
        auto half = [n](const std::vector<double>& s) {
            return std::vector<double>(s.begin() + static_cast<long>(n / 2), s.begin() + static_cast<long>(n));
        };
        auto       xh = half(xf);
        auto       vh = half(v);
        auto       oh = half(rd.out);
        const auto sp = amfm_send_plan();
        const auto rp = amfm_receive_plan();
        o.add("tclwdt", comb_band_level_db(xh, rp, 0.0, rs.fs) - comb_band_level_db(oh, rp, 0.0, rs.fs));
        o.add("asdt", comb_band_level_db(vh, sp, 0.0, rs.fs) - comb_band_level_db(oh, sp, 0.0, rs.fs));
        o.add("delay_ms", 2.0 * 1000.0 * static_cast<double>(rs.block) / rs.fs);
        return o.str();
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: itu_dump <output.json>\n");
        return 2;
    }
    jobj root;
    {
        std::string rates = "[";
        bool        first = true;
        for (const auto& rs : required_rates()) {
            std::fprintf(stderr, "[itu_dump] rate %.0f Hz\n", rs.fs);
            jobj r;
            r.add("fs", rs.fs);
            r.add("block", static_cast<double>(rs.block));
            r.add("taps", static_cast<double>(rs.taps));
            r.add("delay_ms", 2.0 * 1000.0 * static_cast<double>(rs.block) / rs.fs);
            std::fprintf(stderr, "[itu_dump]   convergence (quiet, noise)\n");
            r.add("conv_quiet", dump_conv_quiet(rs));
            r.add("conv_noise", dump_conv_noise(rs));
            std::fprintf(stderr, "[itu_dump]   G.168 figures 9/11 + re-convergence\n");
            r.add("fig9", dump_fig9(rs));
            r.add("fig11", dump_fig11(rs));
            r.add("reconv", dump_reconv(rs));
            std::fprintf(stderr, "[itu_dump]   steady-state echo battery\n");
            r.add("echo", dump_echo_steady(rs));
            std::fprintf(stderr, "[itu_dump]   double-talk battery\n");
            r.add("dt", dump_doubletalk(rs));
            std::fprintf(stderr, "[itu_dump]   noise battery\n");
            r.add("noise", dump_noise(rs));
            std::fprintf(stderr, "[itu_dump]   time-variant path\n");
            r.add("tvp", dump_tvp(rs));
            std::fprintf(stderr, "[itu_dump]   stability sweep\n");
            r.add("stability", dump_stability(rs));
            std::fprintf(stderr, "[itu_dump]   G.168 scalar rows\n");
            r.add("g168", dump_g168_scalars(rs));
            if (!first) {
                rates += ",";
            }
            first = false;
            rates += r.str();
        }
        root.add("rates", rates + "]");
    }
    std::fprintf(stderr, "[itu_dump] G.167 historical row\n");
    root.add("g167", dump_g167());

    FILE* f = std::fopen(argv[1], "w");
    if (f == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const auto doc = root.str();
    std::fwrite(doc.data(), 1, doc.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "[itu_dump] wrote %s (%zu bytes)\n", argv[1], doc.size());
    return 0;
}
