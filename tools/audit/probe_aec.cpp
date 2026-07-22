// AEC audit "wringer" probes — scenarios the certified ITU battery does NOT
// cover, measured with the same instrumentation (itu_chain.h meters/signals).
// Build: see probe build command. Each probe prints a labeled measured value.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "support/itu_chain.h"

using namespace mutap_test;
using namespace mutap_test::itu;

namespace {

    // Full-length (untruncated) cabin path at a rate: resample the whole
    // 4096-tap fixture and keep ALL of it, unit energy. The compliance
    // battery's compliance_path() truncates to the canceller's tap count.
    std::vector<double> full_path(room r, const rate_setup& rs) {
        const float*        rir  = (r == room::cabin) ? fixtures::k_rir_cabin : fixtures::k_rir_studio;
        std::vector<double> p(rir, rir + 4096);
        if (rs.fs != 48000.0) {
            p = resample(p, 48000.0, rs.fs, 0);
        }
        double e = 0.0;
        for (double v : p) {
            e += v * v;
        }
        for (auto& v : p) {
            v /= std::sqrt(e);
        }
        return p;
    }

    std::vector<double> delayed(const std::vector<double>& p, size_t delay_samps) {
        std::vector<double> out(delay_samps, 0.0);
        out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    std::vector<double> css_16(const rate_setup& rs, size_t periods, double dbm0 = -16.0) {
        css_config cc;
        cc.periods = periods;
        cc.shaped  = true;
        auto x     = make_css_at(cc, rs.fs);
        set_level_dbm0(x, dbm0);
        return x;
    }

    struct probe_out {
        double residual_dbm0a; // settled A-weighted max residual (last third)
        double erle_last_db;   // broadband ERLE over the last third
    };

    // Run the compliance chain over a scenario where the SIMULATED path may
    // be longer than the canceller and the reference may be distorted or
    // drifted relative to what physically reaches the mic.
    // xf_ref: what the canceller sees as x. xf_spk: what drives the path.
    probe_out run_probe(const rate_setup& rs, const std::vector<double>& path, const std::vector<double>& x_ref,
                        const std::vector<double>& x_spk) {
        compliance_dut<double> c(chain_config<double>(rs), rs.block);
        const size_t           b  = rs.block;
        const size_t           lf = path.size();
        std::vector<double>    hist(lf - 1 + b, 0.0);
        std::vector<double>    y(b), e(b);
        std::vector<double>    out, echo;
        const size_t           nblocks = std::min(x_ref.size(), x_spk.size()) / b;
        out.reserve(nblocks * b);
        echo.reserve(nblocks * b);
        for (size_t blk = 0; blk < nblocks; ++blk) {
            for (size_t i = 0; i < b; ++i) {
                hist[lf - 1 + i] = x_spk[blk * b + i];
            }
            for (size_t i = 0; i < b; ++i) {
                double acc = 0.0;
                for (size_t k = 0; k < lf; ++k) {
                    acc += path[k] * hist[lf - 1 + i - k];
                }
                y[i] = acc;
            }
            for (size_t i = 0; i + 1 < lf; ++i) {
                hist[i] = hist[b + i];
            }
            c.process_block(&x_ref[blk * b], y.data(), e.data());
            out.insert(out.end(), e.begin(), e.end());
            echo.insert(echo.end(), y.begin(), y.end());
        }
        probe_out r;
        r.residual_dbm0a = max_level_dbm0a(out, rs.fs, out.size() * 2 / 3, out.size());
        double se = 0.0, sy = 0.0;
        for (size_t i = out.size() * 2 / 3; i < out.size(); ++i) {
            se += out[i] * out[i];
            sy += echo[i] * echo[i];
        }
        r.erle_last_db = 10.0 * std::log10(sy / (se + 1e-30));
        return r;
    }

    // Linear-interpolation resampler with a constant ppm rate offset (clock
    // drift model): out[n] = x(n * (1 + ppm*1e-6)).
    std::vector<double> drift(const std::vector<double>& x, double ppm) {
        std::vector<double> out(x.size());
        const double        step = 1.0 + ppm * 1e-6;
        for (size_t n = 0; n < out.size(); ++n) {
            const double t  = static_cast<double>(n) * step;
            const size_t i0 = static_cast<size_t>(t);
            const double fr = t - static_cast<double>(i0);
            const double a  = i0 < x.size() ? x[i0] : 0.0;
            const double bb = i0 + 1 < x.size() ? x[i0 + 1] : 0.0;
            out[n]          = a + fr * (bb - a);
        }
        return out;
    }

    std::vector<double> softclip(const std::vector<double>& x, double drive) {
        std::vector<double> out(x.size());
        for (size_t i = 0; i < x.size(); ++i) {
            out[i] = std::tanh(drive * x[i]) / drive;
        }
        return out;
    }

} // namespace

int main(int argc, char** argv) {
    const std::string which = argc > 1 ? argv[1] : "all";
    const auto        rs    = setup_48k();
    const auto        x     = css_16(rs, 40); // 14 s

    auto want = [&](const char* name) { return which == "all" || which == name; };

    if (want("baseline")) {
        // Reproduce the battery's own conditions (truncated path) as the
        // control for every probe below.
        const auto p = compliance_path(room::cabin, rs);
        const auto r = run_probe(rs, p, x, x);
        std::printf("baseline_truncated_path      residual %7.1f dBm0(A)   ERLE %5.1f dB\n", r.residual_dbm0a,
                    r.erle_last_db);
    }
    if (want("fullpath")) {
        // P1: the untruncated 85 ms cabin RIR against the 43 ms canceller.
        const auto p = full_path(room::cabin, rs);
        const auto r = run_probe(rs, p, x, x);
        std::printf("full_untruncated_path        residual %7.1f dBm0(A)   ERLE %5.1f dB\n", r.residual_dbm0a,
                    r.erle_last_db);
        const auto ps = full_path(room::studio, rs);
        const auto rr = run_probe(rs, ps, x, x);
        std::printf("full_untruncated_studio      residual %7.1f dBm0(A)   ERLE %5.1f dB\n", rr.residual_dbm0a,
                    rr.erle_last_db);
    }
    if (want("bulkdelay")) {
        // P2: bulk delay in front of the (truncated) path — loopback
        // buffering, BT stacks, USB audio all add this in deployment.
        for (double ms : {5.0, 10.0, 20.0, 30.0, 50.0}) {
            const size_t d = static_cast<size_t>(ms * rs.fs / 1000.0);
            auto         p = compliance_path(room::cabin, rs);
            p.resize(p.size() > d ? p.size() - d : 1); // keep total length = what fits...
            const auto pd = delayed(p, d);
            const auto r  = run_probe(rs, pd, x, x);
            std::printf("bulk_delay_%4.0fms_infit      residual %7.1f dBm0(A)   ERLE %5.1f dB\n", ms, r.residual_dbm0a,
                        r.erle_last_db);
        }
        // Delay with the FULL truncated path shifted (tail exceeds span):
        for (double ms : {10.0, 20.0, 45.0}) {
            const size_t d  = static_cast<size_t>(ms * rs.fs / 1000.0);
            const auto   pd = delayed(compliance_path(room::cabin, rs), d);
            const auto   r  = run_probe(rs, pd, x, x);
            std::printf("bulk_delay_%4.0fms_overrun    residual %7.1f dBm0(A)   ERLE %5.1f dB\n", ms, r.residual_dbm0a,
                        r.erle_last_db);
        }
    }
    if (want("drift")) {
        // P3: reference/mic sample-clock skew (split render/capture clocks).
        for (double ppm : {20.0, 50.0, 100.0, 300.0}) {
            const auto xs = drift(x, ppm);
            const auto p  = compliance_path(room::cabin, rs);
            const auto r  = run_probe(rs, p, x, xs);
            std::printf("clock_drift_%5.0fppm         residual %7.1f dBm0(A)   ERLE %5.1f dB\n", ppm, r.residual_dbm0a,
                        r.erle_last_db);
        }
    }
    if (want("nonlinear")) {
        // P4: loudspeaker nonlinearity — the path sees a soft-clipped x,
        // the canceller the clean x. drive 1 ~ mild, 3 ~ hot small speaker.
        for (double drive : {0.5, 1.0, 2.0, 4.0}) {
            auto xl = x;
            // Normalize to peak ~1 so drive is meaningful, then restore level.
            double pk = 0.0;
            for (double v : xl) {
                pk = std::max(pk, std::abs(v));
            }
            for (auto& v : xl) {
                v /= pk;
            }
            auto xs = softclip(xl, drive);
            for (auto& v : xs) {
                v *= pk;
            }
            for (auto& v : xl) {
                v *= pk;
            }
            const auto p = compliance_path(room::cabin, rs);
            const auto r = run_probe(rs, p, xl, xs);
            std::printf("softclip_drive_%3.1f          residual %7.1f dBm0(A)   ERLE %5.1f dB\n", drive,
                        r.residual_dbm0a, r.erle_last_db);
        }
    }
    if (want("guard")) {
        // P5: cold start under CONTINUOUS far end (no CSS pauses): does the
        // receive-activity floor ride up and release the guard before
        // convergence? Trace the first 2 s echo level.
        const auto   p  = compliance_path(room::cabin, rs);
        const size_t n  = static_cast<size_t>(6.0 * rs.fs);
        auto         xn = make_driving_noise(n, 3, rs.fs);
        set_level_dbm0(xn, -16.0);
        compliance_dut<double> c(chain_config<double>(rs), rs.block);
        auto                   rr = run_chain(c, p, rs.block, xn);
        auto                   tr = level_trace_dbm0a(rr.out, rs.fs);
        auto                   te = level_trace_dbm0a(rr.echo, rs.fs);
        for (double t : {0.15, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0}) {
            const size_t i = static_cast<size_t>(t * rs.fs);
            std::printf("guard_cont_noise t=%4.2fs     out %7.1f dBm0(A)  echo-at-mic %7.1f  (atten %5.1f dB)\n", t,
                        tr[i], te[i], te[i] - tr[i]);
        }
    }
    if (want("level")) {
        // P7: level extremes the battery never sweeps below -30 dBm0.
        for (double lvl : {-40.0, -50.0, -60.0}) {
            const auto xq = css_16(rs, 40, lvl);
            const auto p  = compliance_path(room::cabin, rs);
            const auto r  = run_probe(rs, p, xq, xq);
            std::printf("far_end_%4.0fdBm0            residual %7.1f dBm0(A)   ERLE %5.1f dB\n", lvl, r.residual_dbm0a,
                        r.erle_last_db);
        }
    }
    if (want("rate441")) {
        // P8: 44.1 kHz, block 256 — hop 5.805 ms, just OUTSIDE the preset's
        // 6..12 ms novelty band. Is the notch boundary safe?
        rate_setup r441{44100.0, 256, 2048};
        const auto p   = compliance_path(room::cabin, r441);
        const auto x41 = css_16(r441, 40);
        const auto r   = run_probe(r441, p, x41, x41);
        std::printf("rate_44100_block256          residual %7.1f dBm0(A)   ERLE %5.1f dB\n", r.residual_dbm0a,
                    r.erle_last_db);
        // And ERL trajectory early:
        compliance_dut<double> c(chain_config<double>(r441), r441.block);
        auto                   rr = run_chain(c, p, r441.block, css_16(r441, 12));
        erl_reader             erl(rr.echo, rr.out, r441.fs);
        std::printf("rate_44100 ERL by 600ms %5.1f dB, by 1200ms %5.1f dB\n", erl.by(0.6), erl.by(1.2));
    }
    return 0;
}
