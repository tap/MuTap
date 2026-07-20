// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Cross-algorithm AEC comparison driver. Runs every linked-in subject
// (MuTap variants always; Speex MDF and WebRTC AEC3 when their build
// options are on) through identical echo paths, signals and metrics, at
// 48 and 16 kHz, and prints a human table plus a JSON blob the notebook
// and docs/aec-comparison.md consume.
//
//   ./mutap_aec_compare                 # all subjects, both rates, text + json
//   ./mutap_aec_compare --list          # list linked-in subjects
//   ./mutap_aec_compare --json-only     # just the JSON (for the notebook)
//   ./mutap_aec_compare --rate 16000    # one rate
//   ./mutap_aec_compare --subject mutap,webrtc
//
// Fairness notes live next to the metric that needs them; the headline
// caveats (float32 vs float64, linear vs full-chain, echo within filter
// capacity, ERLE vs the double-talk-immune suppression number) are in
// the doc.
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "backends/mutap_backend.h"
#include "compare_driver.h"

#if MUTAP_COMPARE_HAVE_SPEEX
#include "backends/speex_backend.h"
#endif
#if MUTAP_COMPARE_HAVE_WEBRTC
#include "backends/webrtc_backend.h"
#endif

using namespace mutap_compare;

namespace {

    // Precomputed per (room, rate): the echo signal for the standard
    // far-end excitation, so the O(n*taps) convolution runs once and is
    // shared by every subject.
    struct scene {
        std::vector<float> far;   // far-end excitation (speech-like)
        std::vector<float> near;  // independent near-end (speech-like)
        std::vector<float> rir;   // echo path at this rate
        std::vector<float> echo;  // d = rir * far
        std::vector<float> mic;   // y = d + near  (double-talk); or d alone
    };

    double median(std::vector<double> v) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }

    // Run one subject at one rate across all rooms/scenarios.
    metrics evaluate(const subject& subj, double fs) {
        auto be = subj.make(fs);
        metrics m;
        m.fs = fs;
        if (!be) { m.subject = subj.key; m.diverged = true; return m; }
        m.subject    = be->name();
        m.latency_ms = be->latency_ms();

        const double secs = 8.0;
        const size_t n    = static_cast<size_t>(secs * fs);
        const size_t cap  = static_cast<size_t>((fs <= 24000.0 ? 64.0 : 43.0) * fs / 1000.0);

        std::vector<double> erle, converge, near_keep, dt_supp, dt_keep, reconv;

        for (const auto& r : rooms()) {
            const std::vector<float> rir = make_rir(fs, r.delay_ms, r.rt60_ms, 1000.0 * cap / fs,
                                                    r.coupling_db, 0xF17E + static_cast<uint32_t>(r.delay_ms));
            const std::vector<float> far  = speechlike(n, fs, 0xFA00);
            const std::vector<float> nearv = speechlike(n, fs, 0xBEEF);
            const std::vector<float> echo = convolve(far, rir);

            const size_t dt_on = static_cast<size_t>(0.5 * n); // near-end enters at midpoint
            const size_t dt_lo = dt_on + static_cast<size_t>(0.05 * n); // let DT settle

            // --- Far-end single talk: ERLE + convergence. This far-only
            //     pass doubles as the near-muted echo reference for the
            //     double-talk echo-depth number below (same far, same path). ---
            std::vector<float> fest_out;
            {
                be->reset();
                std::vector<float> mic = echo; // near-end silent
                fest_out = run_stream(*be, far, mic, fs);
                const size_t lo = static_cast<size_t>(0.6 * n); // steady tail
                const double e  = ratio_db(mic, fest_out, lo, n);
                erle.push_back(e);
                converge.push_back(convergence_ms(mic, fest_out, fs, 20.0));
                if (!std::isfinite(e)) m.diverged = true;
                // echo depth sustained across the DT time window (near absent):
                dt_supp.push_back(ratio_db(echo, fest_out, dt_lo, n));
            }

            // --- Near-end single talk: transparency (far silent). Ideal
            //     output equals the near-end, so out/near ~ 0 dB; a ducking
            //     suppressor drives it negative. ---
            {
                be->reset();
                std::vector<float> farq(n, 0.0f);
                std::vector<float> out = run_stream(*be, farq, nearv, fs);
                const size_t lo = static_cast<size_t>(0.2 * n);
                near_keep.push_back(ratio_db(out, nearv, lo, n));
            }

            // --- Double talk: converge far-only, then near-end enters.
            //     out/near over the DT window shows how much near-end
            //     survives (ducking check); the echo side is dt_supp above. ---
            {
                be->reset();
                std::vector<float> mic(n);
                for (size_t i = 0; i < n; ++i) mic[i] = echo[i] + (i >= dt_on ? nearv[i] : 0.0f);
                std::vector<float> out = run_stream(*be, far, mic, fs);
                std::vector<float> vseg(nearv);
                dt_keep.push_back(ratio_db(out, vseg, dt_lo, n));
            }
        }

        // --- Path swap: converge on room 0, swap to room 1 at midpoint ---
        {
            const auto& r0 = rooms()[0];
            const auto& r1 = rooms()[1];
            const std::vector<float> rir0 = make_rir(fs, r0.delay_ms, r0.rt60_ms, 1000.0 * cap / fs, r0.coupling_db, 1);
            const std::vector<float> rir1 = make_rir(fs, r1.delay_ms, r1.rt60_ms, 1000.0 * cap / fs, r1.coupling_db, 2);
            const std::vector<float> far  = speechlike(n, fs, 0xFA00);
            const std::vector<float> e0   = convolve(far, rir0);
            const std::vector<float> e1   = convolve(far, rir1);
            const size_t swap = n / 2;
            std::vector<float> mic(n);
            for (size_t i = 0; i < n; ++i) mic[i] = (i < swap ? e0[i] : e1[i]);
            be->reset();
            std::vector<float> out = run_stream(*be, far, mic, fs);
            // Re-convergence: time from swap to 20 dB, measured on the tail.
            std::vector<float> mic_tail(mic.begin() + swap, mic.end());
            std::vector<float> out_tail(out.begin() + swap, out.end());
            reconv.push_back(convergence_ms(mic_tail, out_tail, fs, 20.0));
        }

        m.erle_db          = median(erle);
        m.converge_ms      = median(converge);
        m.reconverge_ms    = median(reconv);
        m.near_keep_db     = median(near_keep);
        m.dt_echo_supp_db  = median(dt_supp);
        m.dt_near_keep_db  = median(dt_keep);

        // --- Cost: time a far-only steady run, us/frame + x-realtime ---
        {
            const std::vector<float> rir = make_rir(fs, 3.0, 45.0, 1000.0 * cap / fs, 15.0, 3);
            const std::vector<float> far = white(n, 0xC057);
            const std::vector<float> mic = convolve(far, rir);
            be->reset();
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<float> out = run_stream(*be, far, mic, fs);
            const auto t1 = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            const size_t frames = n / be->frame();
            m.us_per_frame = frames ? us / frames : 0.0;
            const double audio_us = 1e6 * n / fs;
            m.x_realtime   = us > 0.0 ? audio_us / us : 0.0;
        }
        return m;
    }

    void print_row(const metrics& m) {
        std::printf("  %-14s %6.0f  %5.1f  %7.1f  %7.0f  %8.0f  %7.1f  %7.1f  %7.1f  %6.1f  %7.0f\n",
                    m.subject.c_str(), m.fs, m.latency_ms, m.erle_db, m.converge_ms, m.reconverge_ms,
                    m.near_keep_db, m.dt_echo_supp_db, m.dt_near_keep_db, m.us_per_frame, m.x_realtime);
    }

    std::string json_row(const metrics& m) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "{\"subject\":\"%s\",\"fs\":%.0f,\"latency_ms\":%.3f,\"erle_db\":%.2f,"
                      "\"converge_ms\":%.1f,\"reconverge_ms\":%.1f,\"near_keep_db\":%.2f,"
                      "\"dt_echo_supp_db\":%.2f,\"dt_near_keep_db\":%.2f,\"us_per_frame\":%.3f,"
                      "\"x_realtime\":%.1f,\"diverged\":%s}",
                      m.subject.c_str(), m.fs, m.latency_ms, m.erle_db, m.converge_ms, m.reconverge_ms,
                      m.near_keep_db, m.dt_echo_supp_db, m.dt_near_keep_db, m.us_per_frame, m.x_realtime,
                      m.diverged ? "true" : "false");
        return buf;
    }

} // namespace

int main(int argc, char** argv) {
    register_mutap_backends();
#if MUTAP_COMPARE_HAVE_SPEEX
    register_speex_backend();
#endif
#if MUTAP_COMPARE_HAVE_WEBRTC
    register_webrtc_backend();
#endif

    bool                     json_only = false;
    std::vector<double>      rates     = {48000.0, 16000.0};
    std::vector<std::string> want; // empty = all

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--list") {
            for (auto& s : registry()) std::printf("%-14s %s\n", s.key.c_str(), s.blurb.c_str());
            return 0;
        } else if (a == "--json-only") {
            json_only = true;
        } else if (a == "--rate" && i + 1 < argc) {
            rates = {std::stod(argv[++i])};
        } else if (a == "--subject" && i + 1 < argc) {
            std::string s = argv[++i], cur;
            for (char c : s) { if (c == ',') { want.push_back(cur); cur.clear(); } else cur += c; }
            if (!cur.empty()) want.push_back(cur);
        }
    }

    auto wanted = [&](const std::string& key) {
        if (want.empty()) return true;
        return std::find(want.begin(), want.end(), key) != want.end();
    };

    std::vector<metrics> all;
    if (!json_only) {
        std::printf("\nAEC cross-algorithm comparison (higher dB = better; lower ms = better)\n");
        std::printf("  %-14s %6s  %5s  %7s  %7s  %8s  %7s  %7s  %7s  %6s  %7s\n", "subject", "fs", "lat", "ERLE",
                    "cvg_ms", "recvg_ms", "nearKp", "DTsupp", "DTnrKp", "us/fr", "xRT");
    }
    for (auto& s : registry()) {
        if (!wanted(s.key)) continue;
        for (double fs : rates) {
            metrics m = evaluate(s, fs);
            all.push_back(m);
            if (!json_only) print_row(m);
        }
    }

    std::printf("%s{\"results\":[", json_only ? "" : "\nJSON:\n");
    for (size_t i = 0; i < all.size(); ++i) std::printf("%s%s", i ? "," : "", json_row(all[i]).c_str());
    std::printf("]}\n");
    return 0;
}
