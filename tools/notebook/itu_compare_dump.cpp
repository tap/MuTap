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

#include "aec_backend.h"                 // mutap_compare registry + backends
#include "backends/mutap_backend.h"
#if MUTAP_COMPARE_HAVE_SPEEX
#include "backends/speex_backend.h"
#endif
#if MUTAP_COMPARE_HAVE_WEBRTC
#include "backends/webrtc_backend.h"
#endif

#include "itu_chain.h"                    // tests/support: signals, meters, paths, masks

using namespace mutap_test;
using namespace mutap_test::itu;

namespace {

    // ---- JSON helpers (same decimated-trace shape as itu_dump.cpp) ----
    std::string jnum(double v) {
        if (!std::isfinite(v)) return "null";
        char b[40];
        std::snprintf(b, sizeof(b), "%.4g", v);
        return b;
    }
    std::string jarr(const std::vector<double>& v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + jnum(v[i]);
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
            for (size_t k = 0; k < kmax; ++k) acc += h[k] * x[i - k];
            d[i] = acc;
        }
        return d;
    }

    std::vector<double> erl_path(room r, const rate_setup& rs, double erl_db) {
        auto p = compliance_path(r, rs);
        for (auto& v : p) v *= std::pow(10.0, -erl_db / 20.0);
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
            for (size_t i = 0; i + b <= n; i += b) chain.process_block(&x[i], &y[i], &e[i]);
            return e;
        }
        // Third-party: black box at its native float frame.
        std::unique_ptr<mutap_compare::aec_backend> be;
        for (auto& s : mutap_compare::registry())
            if (s.key == subj) be = s.make(rs.fs);
        if (!be) return {}; // subject not linked / rate unsupported
        const size_t       fr = be->frame();
        std::vector<float> xf(fr), yf(fr), ef(fr);
        for (size_t i = 0; i + fr <= n; i += fr) {
            for (size_t j = 0; j < fr; ++j) {
                xf[j] = static_cast<float>(x[i + j]);
                yf[j] = static_cast<float>(y[i + j]);
            }
            be->process(xf.data(), yf.data(), ef.data());
            for (size_t j = 0; j < fr; ++j) e[i + j] = static_cast<double>(ef[j]);
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
        for (size_t i = 0; i < tr.size(); i += step) o.push_back(tr[i]);
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
        const auto x    = css_m16(rs, 24);
        const auto path = erl_path(room::cabin, rs, 6.0);
        const auto echo = convolve(x, path);
        std::string out = "{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, echo);
            if (e.empty()) continue;
            erl_reader          erl(echo, e, rs.fs); // mic here is echo (near silent)
            std::vector<double> curve;
            for (double t = 0.4; t < static_cast<double>(x.size()) / rs.fs - 0.01; t += 0.025)
                curve.push_back(erl.by(t));
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jtrace(curve, 0.025, 0.4);
        }
        return out + "}";
    }

    // ---- Scenario 2: re-convergence after an abrupt path change. ----
    std::string scen_reconv(const rate_setup& rs) {
        const auto   x      = css_m16(rs, 40);
        const auto   cab    = erl_path(room::cabin, rs, 6.0);
        const auto   stu    = erl_path(room::studio, rs, 16.0);
        const size_t n      = x.size();
        const size_t swap   = n / 2;
        // Echo with the path swapped mid-run (mic independent of canceller).
        auto e_cab = convolve(x, cab);
        auto e_stu = convolve(x, stu);
        std::vector<double> mic(n);
        for (size_t i = 0; i < n; ++i) mic[i] = (i < swap ? e_cab[i] : e_stu[i]);
        std::string out   = "{\"swap_s\":" + jnum(static_cast<double>(swap) / rs.fs) + ",\"curves\":{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, mic);
            if (e.empty()) continue;
            erl_reader          erl(mic, e, rs.fs);
            std::vector<double> curve;
            for (double t = 0.4; t < static_cast<double>(n) / rs.fs - 0.01; t += 0.025) curve.push_back(erl.by(t));
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jtrace(curve, 0.025, 0.4);
        }
        return out + "}}";
    }

    // ---- Scenario 3: steady-state ERL bar (converged single talk). ----
    std::string scen_steady(const rate_setup& rs) {
        const auto  x    = css_m16(rs, 24);
        const auto  path = erl_path(room::cabin, rs, 6.0);
        const auto  echo = convolve(x, path);
        std::string out  = "{";
        bool        first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, x, echo);
            if (e.empty()) continue;
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
        const auto   far  = css_m16(rs, 24);
        css_config   nc;
        nc.kind    = css_kind::double_talk;
        nc.periods = 30; // active across the whole run so the DT half is real
        nc.seed    = 907;
        auto near = make_css_at(nc, rs.fs);
        near.resize(far.size(), 0.0);
        set_level_dbm0(near, -16.0); // leveled while active (before gating)
        const auto   path = erl_path(room::cabin, rs, 6.0);
        const auto   echo = convolve(far, path);
        const size_t n    = far.size();
        const size_t on   = n / 2;                 // near-end enters at midpoint
        std::vector<double> mic(n), nearseg(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            nearseg[i] = (i >= on ? near[i] : 0.0);
            mic[i]     = echo[i] + nearseg[i];
        }
        const size_t lo = on + static_cast<size_t>(0.1 * n);
        auto energy = [](const std::vector<double>& s, size_t a, size_t b) {
            double e = 0.0;
            for (size_t i = a; i < b; ++i) e += s[i] * s[i];
            return e;
        };
        const double ne = energy(nearseg, lo, n);
        std::string  out = "{";
        bool         first = true;
        for (const auto& subj : subjects()) {
            const auto e = run_send(subj, rs, far, mic);
            if (e.empty()) continue;
            const double keep = 10.0 * std::log10(energy(e, lo, n) / (ne + 1e-30));
            out += (first ? "" : ",");
            first = false;
            out += "\"" + subj + "\":" + jnum(keep);
        }
        return out + "}";
    }

    std::string dump_rate(const rate_setup& rs) {
        std::string o = "{";
        o += "\"convergence\":" + scen_convergence(rs) + ",";
        o += "\"reconvergence\":" + scen_reconv(rs) + ",";
        o += "\"steady_erl\":" + scen_steady(rs) + ",";
        o += "\"doubletalk_near_keep\":" + scen_doubletalk(rs);
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
    { bool f = true; for (auto& s : subjects()) { js += (f ? "" : ",") + std::string("\"") + s + "\""; f = false; } }
    js += "],";
    js += "\"48000\":" + dump_rate(r48) + ",";
    js += "\"16000\":" + dump_rate(r16);
    js += "}";
    std::printf("%s\n", js.c_str());
    return 0;
}
