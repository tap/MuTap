// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// ITU cross-canceller dump — the same compliance scenarios the certified
// battery (tools/notebook/itu_dump.cpp) runs on MuTap, but driven through
// ALL THREE cancellers (MuTap, Speex MDF, WebRTC AEC3) so the comparison
// notebook (notebooks/itu_comparison.ipynb) can overlay them against the
// ITU masks. It reuses tests/support/itu_chain.h wholesale — the P.501
// CSS and AM-FM signals, the image-source cabin/studio paths, the
// dBm0/dBm0(A) meters, the ERL reader — so every subject is measured by
// the exact machinery the test suite gates. Only the device under test
// changes.
//
// This is NOT the compliance proof (itu_dump.cpp / itu_compliance.ipynb
// stays MuTap-only, always buildable, CI-gated). It is the comparison
// view, and needs the third-party subjects, so it lives behind
// -DMUTAP_BUILD_COMPARE with -DMUTAP_COMPARE_SPEEX / _WEBRTC.
//
// Open-loop AEC lets us decouple cleanly: the microphone y = echo + near
// does not depend on the canceller, so each subject is streamed over the
// whole (far, mic) signal at its native frame and read at native
// alignment — exactly as the ITU suite and a real deployment see a
// canceller, no per-block re-framing games.
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "aec_backend.h" // mutap_compare registry + backends
#include "backends/mutap_backend.h"
#if MUTAP_COMPARE_HAVE_SPEEX
#include "backends/speex_backend.h"
#endif
#if MUTAP_COMPARE_HAVE_WEBRTC
#include "backends/webrtc_backend.h"
#endif

#include "itu_chain.h" // tests/support: signals, meters, paths, masks

using namespace mutap_test;
using namespace mutap_test::itu;

namespace {

    // ---- JSON helpers (same decimated-trace shape as itu_dump.cpp) ----
    std::string jnum(double v) {
        if (!std::isfinite(v)) {
            return "null";
        }
        char b[40];
        std::snprintf(b, sizeof(b), "%.4g", v);
        return b;
    }
    std::string jarr(const std::vector<double>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            s += (i ? "," : "") + jnum(v[i]);
        }
        return s + "]";
    }
    // A decimated {t0,dt,v[]} level/ERL trace.
    std::string jtrace(const std::vector<double>& tr, double dt, double t0) {
        return "{\"t0\":" + jnum(t0) + ",\"dt\":" + jnum(dt) + ",\"v\":" + jarr(tr) + "}";
    }

    std::vector<double> convolve(const std::vector<double>& x, const std::vector<double>& h) {
        std::vector<double> d(x.size(), 0.0);
        const size_t        lh = h.size();
        for (size_t i = 0; i < x.size(); ++i) {
            double       acc  = 0.0;
            const size_t kmax = std::min(lh, i + 1);
            for (size_t k = 0; k < kmax; ++k) {
                acc += h[k] * x[i - k];
            }
            d[i] = acc;
        }
        return d;
    }

    std::vector<double> erl_path(room r, const rate_setup& rs, double erl_db) {
        auto p = compliance_path(r, rs);
        for (auto& v : p) {
            v *= std::pow(10.0, -erl_db / 20.0);
        }
        return p;
    }

    // Stream one subject over (x, y) -> send output e, at native alignment.
    // MuTap runs the certified chain (chain_config geometry, block 256);
    // Speex/WebRTC come from the comparison backend registry.
    std::vector<double> run_send(const std::string& subj, const rate_setup& rs, const std::vector<double>& x,
                                 const std::vector<double>& y) {
        const size_t        n = x.size();
        std::vector<double> e(n, 0.0);
        if (subj == "mutap") {
            mutap::aec_chain<double> chain(chain_config(rs));
            const size_t             b = rs.block;
            for (size_t i = 0; i + b <= n; i += b) {
                chain.process_block(&x[i], &y[i], &e[i]);
            }
            return e;
        }
        // Third-party: black box at its native float frame.
        std::unique_ptr<mutap_compare::aec_backend> be;
        for (auto& s : mutap_compare::registry()) {
            if (s.key == subj) {
                be = s.make(rs.fs);
            }
        }
        if (!be) {
            return {}; // subject not linked / rate unsupported
        }
        const size_t       fr = be->frame();
        std::vector<float> xf(fr), yf(fr), ef(fr);
        for (size_t i = 0; i + fr <= n; i += fr) {
            for (size_t j = 0; j < fr; ++j) {
                xf[j] = static_cast<float>(x[i + j]);
                yf[j] = static_cast<float>(y[i + j]);
            }
            be->process(xf.data(), yf.data(), ef.data());
            for (size_t j = 0; j < fr; ++j) {
                e[i + j] = static_cast<double>(ef[j]);
            }
        }
        return e;
    }

    // Subjects to compare, in plot order. Third-party ones self-skip if
    // not linked (run_send returns empty).
    std::vector<std::string> subjects() {
        std::vector<std::string> s{"mutap"};
#if MUTAP_COMPARE_HAVE_WEBRTC
        s.push_back("webrtc");
#endif
#if MUTAP_COMPARE_HAVE_SPEEX
        s.push_back("speex");
#endif
        return s;
    }

    // Decimate a per-sample trace to ~plot_hz for compact JSON.
    std::vector<double> decimate(const std::vector<double>& tr, double fs, double plot_hz, double& dt_out) {
        const size_t        step = std::max<size_t>(1, static_cast<size_t>(fs / plot_hz));
        std::vector<double> o;
        for (size_t i = 0; i < tr.size(); i += step) {
            o.push_back(tr[i]);
        }
        dt_out = step / fs;
        return o;
    }

    // ---------------------------------------------------------------- CSS
    std::vector<double> css_m16(const rate_setup& rs, size_t periods) {
        css_config cc;
        cc.periods = periods;
        cc.shaped  = true;
        auto x     = make_css_at(cc, rs.fs);
        set_level_dbm0(x, -16.0);
        return x;
    }

    // ---- Scenario 1: convergence, quiet (§11.11.4). ERL(t) per subject. ----
    std::string scen_convergence(const rate_setup& rs) {
        const auto  x     = css_m16(rs, 24);
        const auto  path  = erl_path(room::cabin, rs, 6.0);
        const auto  echo  = convolve(x, path);
        std::string out   = "{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, echo);
            if (e.empty()) {
                continue;
            }
            erl_reader          erl(echo, e, rs.fs); // mic here is echo (near silent)
            std::vector<double> curve;
            for (double t = 0.4; t < static_cast<double>(x.size()) / rs.fs - 0.01; t += 0.025) {
                curve.push_back(erl.by(t));
            }
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jtrace(curve, 0.025, 0.4);
        }
        return out + "}";
    }

    // ---- Scenario 2: re-convergence after an abrupt path change. ----
    std::string scen_reconv(const rate_setup& rs) {
        const auto   x    = css_m16(rs, 40);
        const auto   cab  = erl_path(room::cabin, rs, 6.0);
        const auto   stu  = erl_path(room::studio, rs, 16.0);
        const size_t n    = x.size();
        const size_t swap = n / 2;
        // Echo with the path swapped mid-run (mic independent of canceller).
        auto                e_cab = convolve(x, cab);
        auto                e_stu = convolve(x, stu);
        std::vector<double> mic(n);
        for (size_t i = 0; i < n; ++i) {
            mic[i] = (i < swap ? e_cab[i] : e_stu[i]);
        }
        std::string out   = "{\"swap_s\":" + jnum(static_cast<double>(swap) / rs.fs) + ",\"curves\":{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, mic);
            if (e.empty()) {
                continue;
            }
            erl_reader          erl(mic, e, rs.fs);
            std::vector<double> curve;
            for (double t = 0.4; t < static_cast<double>(n) / rs.fs - 0.01; t += 0.025) {
                curve.push_back(erl.by(t));
            }
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jtrace(curve, 0.025, 0.4);
        }
        return out + "}}";
    }

    // ---- Scenario 3: steady-state ERL bar (converged single talk). ----
    std::string scen_steady(const rate_setup& rs) {
        const auto  x     = css_m16(rs, 24);
        const auto  path  = erl_path(room::cabin, rs, 6.0);
        const auto  echo  = convolve(x, path);
        std::string out   = "{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, echo);
            if (e.empty()) {
                continue;
            }
            erl_reader   erl(echo, e, rs.fs);
            const double steady = erl.by(static_cast<double>(x.size()) / rs.fs - 0.05);
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jnum(steady);
        }
        return out + "}";
    }

    // ---- Scenario 4: double-talk near-end preservation. ----
    // CSS far end (converged first), then a CSS near-end talker enters at
    // the midpoint. During the double-talk half the ideal send output IS
    // the near end, so out/near level (a delay-robust energy ratio, 0 dB =
    // untouched, negative = the canceller ducks the near end) is the
    // duplex-preservation number. The orthogonal ITU echo-loss-during-DT
    // measurement needs the full comb analysis and is left to the
    // compliance suite; this is the visual companion.
    std::string scen_doubletalk(const rate_setup& rs) {
        const auto far = css_m16(rs, 24);
        css_config nc;
        nc.kind    = css_kind::double_talk;
        nc.periods = 30; // active across the whole run so the DT half is real
        nc.seed    = 907;
        auto near  = make_css_at(nc, rs.fs);
        near.resize(far.size(), 0.0);
        set_level_dbm0(near, -16.0); // leveled while active (before gating)
        const auto          path = erl_path(room::cabin, rs, 6.0);
        const auto          echo = convolve(far, path);
        const size_t        n    = far.size();
        const size_t        on   = n / 2; // near-end enters at midpoint
        std::vector<double> mic(n), nearseg(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            nearseg[i] = (i >= on ? near[i] : 0.0);
            mic[i]     = echo[i] + nearseg[i];
        }
        const size_t lo     = on + static_cast<size_t>(0.1 * n);
        auto         energy = [](const std::vector<double>& s, size_t a, size_t b) {
            double e = 0.0;
            for (size_t i = a; i < b; ++i) {
                e += s[i] * s[i];
            }
            return e;
        };
        const double ne    = energy(nearseg, lo, n);
        std::string  out   = "{";
        bool         first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, far, mic);
            if (e.empty()) {
                continue;
            }
            const double keep = 10.0 * std::log10(energy(e, lo, n) / (ne + 1e-30));
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jnum(keep);
        }
        return out + "}";
    }

    // Stateful "converge then measure": run the subject over the WHOLE
    // [convergence ; measurement] stream (so the canceller is converged
    // by the measurement phase) and return the measurement-phase output.
    std::vector<double> run_converged(const std::string& subj, const rate_setup& rs, const std::vector<double>& far,
                                      const std::vector<double>& mic, size_t meas_start) {
        const auto e = run_send(subj, rs, far, mic);
        if (e.empty()) {
            return {};
        }
        return {e.begin() + static_cast<long>(meas_start), e.end()};
    }

    std::vector<double> settled_half(const std::vector<double>& s) {
        return {s.begin() + static_cast<long>(s.size() / 2), s.end()};
    }

    // ---- Scenario 5: double-talk echo loss + send attenuation (ITU comb). ----
    // The real P.501 AM-FM method the compliance suite gates: converge on
    // CSS, then the far end on the receive comb and the near end on the
    // orthogonal send comb. Echo loss = clean far-end level MINUS residual
    // in the output, worst over receive bands (near end quiet, -25.7 dBPa;
    // >= 27 dB required). Send attenuation = clean near-end level minus
    // near-end left in the output, worst over send bands (near end loud,
    // -1.7 dBPa; <= 3 dB required). Reference is the CLEAN comb signal, not
    // the room-smeared echo — that was the fix over the near-keep proxy.
    std::string scen_dt_comb(const rate_setup& rs) {
        auto cvg = css_m16(rs, 30); // convergence preamble
        // Block-align the AM-FM phase: the certified test runs convergence
        // and measurement as two separate block-gridded passes, so the
        // AM-FM phase starts on a block boundary. Front-pad the convergence
        // so it does here too (a misaligned overlap-save grid costs ~8 dB).
        const size_t pad = (rs.block - cvg.size() % rs.block) % rs.block;
        cvg.insert(cvg.begin(), pad, 0.0);
        const auto   path = compliance_path(room::cabin, rs); // unit-energy, as the test
        const size_t na   = static_cast<size_t>(8 * rs.fs);   // AM-FM phase length
        auto         xa   = make_amfm(amfm_receive_plan(), na, rs.fs);
        set_level_dbm0(xa, -16.0);
        const auto rp = amfm_receive_plan();
        const auto sp = amfm_send_plan();

        // far = [cvg ; amfm_receive]; near differs by phase level.
        std::vector<double> far = cvg;
        far.insert(far.end(), xa.begin(), xa.end());
        const auto   echo = convolve(far, path);
        const size_t meas = cvg.size();

        auto near_at = [&](double send_dbpa) {
            auto         v  = make_amfm(amfm_send_plan(), na, rs.fs);
            const double vg = dbpa_to_rms(send_dbpa) / rms_of(v.data(), v.size());
            for (auto& s : v) {
                s *= vg;
            }
            std::vector<double> nv(cvg.size(), 0.0);
            nv.insert(nv.end(), v.begin(), v.end());
            return nv;
        };
        auto band_worst = [&](const amfm_plan& plan, const std::vector<double>& refh, const std::vector<double>& oh,
                              bool want_min) {
            double worst = want_min ? 1e9 : -1e9;
            for (size_t b = 0; b < plan.f0.size(); ++b) {
                if (plan.f0[b] < 200.0 || plan.f0[b] > 6950.0 || plan.f0[b] > rs.fs / 2 * 0.9) {
                    continue;
                }
                amfm_plan one;
                one.f0.push_back(plan.f0[b]);
                one.df.push_back(plan.df[b]);
                const double d = comb_band_level_db(refh, one, 0.0, rs.fs) - comb_band_level_db(oh, one, 0.0, rs.fs);
                worst          = want_min ? std::min(worst, d) : std::max(worst, d);
            }
            return worst;
        };

        std::string out   = "{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            // Echo loss: quiet near end.
            const auto          near_q = near_at(-25.7);
            std::vector<double> micq(echo.size());
            for (size_t i = 0; i < echo.size(); ++i) {
                micq[i] = echo[i] + near_q[i];
            }
            const auto oq = run_converged(subj, rs, far, micq, meas);
            if (oq.empty()) {
                continue;
            }
            const double echo_loss = band_worst(rp, settled_half(xa), settled_half(oq), true);

            // Send attenuation: loud near end.
            const auto          near_l = near_at(-1.7);
            std::vector<double> vl(make_amfm(amfm_send_plan(), na, rs.fs));
            const double        vg = dbpa_to_rms(-1.7) / rms_of(vl.data(), vl.size());
            for (auto& s : vl) {
                s *= vg;
            }
            std::vector<double> micl(echo.size());
            for (size_t i = 0; i < echo.size(); ++i) {
                micl[i] = echo[i] + near_l[i];
            }
            const auto   ol         = run_converged(subj, rs, far, micl, meas);
            const double send_atten = band_worst(sp, settled_half(vl), settled_half(ol), false);

            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":{\"echo_loss_db\":" + jnum(echo_loss) + ",\"send_atten_db\":" + jnum(send_atten)
                   + "}";
        }
        return out + "}";
    }

    // ---- Scenario 6: spectral echo attenuation vs the §11.11.3 WB mask. ----
    // Converged single talk; third-octave attenuation spectrum of the echo
    // vs the send output, overlaid on the WB mask.
    std::string scen_spectral(const rate_setup& rs) {
        const auto   x     = css_m16(rs, 30);
        const auto   path  = compliance_path(room::cabin, rs);
        const auto   echo  = convolve(x, path);
        const double f1    = std::min(8000.0, rs.fs / 2 * 0.94);
        std::string  out   = "{";
        bool         first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, echo);
            if (e.empty()) {
                continue;
            }
            const size_t        from = e.size() - static_cast<size_t>(4 * 0.35 * rs.fs);
            std::vector<double> ref(echo.begin() + static_cast<long>(from), echo.end());
            std::vector<double> o(e.begin() + static_cast<long>(from), e.end());
            const auto          sp = attenuation_spectrum(ref, o, rs.fs, 8192, 100.0, f1);
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":{\"f\":" + jarr(sp.f_center) + ",\"atten_db\":" + jarr(sp.atten_db) + "}";
        }
        return out + "}";
    }

    // ---- Scenario 7: send activation build-up time (§11.11.8.1). ----
    // Far end silent; a near-end CSS talker at -26 dBPa turns on after 1 s
    // of silence. Build-up time = ms from onset until the A-weighted 5 ms
    // send level reaches within 3 dB of its settled median. Requirement
    // <= 50 ms, target <= 25 ms. (For a black box: how fast the near-end
    // talker passes through — a canceller that gates the near end is slow.)
    std::string scen_activation(const rate_setup& rs) {
        css_config cd;
        cd.periods      = 10;
        cd.kind         = css_kind::double_talk;
        cd.shaped       = true;
        auto         v  = make_css_at(cd, rs.fs);
        const double vg = dbpa_to_rms(-26.0) / rms_of(v.data(), v.size());
        for (auto& s : v) {
            s *= vg;
        }
        const size_t        pre = static_cast<size_t>(1.0 * rs.fs);
        std::vector<double> near(pre, 0.0);
        near.insert(near.end(), v.begin(), v.end());
        std::vector<double> far(near.size(), 0.0); // far silent -> mic = near
        std::string         out   = "{";
        bool                first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, far, near);
            if (e.empty()) {
                continue;
            }
            a_weighting     aw(rs.fs);
            auto            seg = aw.apply(e);
            exp_level_meter m(rs.fs, 0.005);
            const auto      tr       = m.trace_dbm0(seg);
            double          build_ms = 1e9;
            if (tr.size() >= pre + static_cast<size_t>(2 * rs.fs)) {
                const long          s0 = static_cast<long>(pre) + static_cast<long>(rs.fs);
                std::vector<double> settled(tr.begin() + s0, tr.begin() + s0 + static_cast<long>(rs.fs));
                std::nth_element(settled.begin(), settled.begin() + static_cast<long>(settled.size() / 2),
                                 settled.end());
                const double target = settled[settled.size() / 2];
                size_t       t_hit  = tr.size();
                for (size_t i = pre; i < tr.size(); ++i) {
                    if (tr[i] >= target - 3.0) {
                        t_hit = i - pre;
                        break;
                    }
                }
                build_ms = 1000.0 * static_cast<double>(t_hit) / rs.fs;
            }
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jnum(build_ms);
        }
        return out + "}";
    }

    std::string dump_rate(const rate_setup& rs) {
        std::string o = "{";
        o += "\"convergence\":" + scen_convergence(rs) + ",";
        o += "\"reconvergence\":" + scen_reconv(rs) + ",";
        o += "\"steady_erl\":" + scen_steady(rs) + ",";
        o += "\"doubletalk_near_keep\":" + scen_doubletalk(rs) + ",";
        o += "\"dt_comb\":" + scen_dt_comb(rs) + ",";
        o += "\"spectral\":" + scen_spectral(rs) + ",";
        o += "\"activation\":" + scen_activation(rs);
        return o + "}";
    }

} // namespace

int main() {
    mutap_compare::register_mutap_backends();
#if MUTAP_COMPARE_HAVE_SPEEX
    mutap_compare::register_speex_backend();
#endif
#if MUTAP_COMPARE_HAVE_WEBRTC
    mutap_compare::register_webrtc_backend();
#endif

    const rate_setup r48 = setup_48k();
    const rate_setup r16 = setup_16k();

    std::string js = "{";
    js += "\"subjects\":[";
    {
        bool f = true;
        for (auto& s : subjects()) {
            js += (f ? "" : ",") + std::string("\"") + s + "\"";
            f = false;
        }
    }
    js += "],";
    js += "\"48000\":" + dump_rate(r48) + ",";
    js += "\"16000\":" + dump_rate(r16);
    js += "}";
    std::printf("%s\n", js.c_str());
    return 0;
}
