// F2 follow-up: can SampleRateTap's async_sample_rate_converter rescue the
// AEC from reference/mic clock skew? Model: the render device runs fast by
// `ppm`; the patch's reference stream r[n] = x(n/(1+eps)) lives on the render
// clock while the mic (and the AEC) live on the capture clock. The speaker
// reproduces the same continuous signal, so the capture-domain echo is F * x.
//
//   naive: feed r to the canceller as if it were capture-rate (the audit's
//          failing scenario, re-measured here through the same code path).
//   asrc:  push r at the render rate ((1+eps) frames per capture frame),
//          pull at the capture rate; feed the recovered stream to the
//          canceller. The type-2 FIFO servo nulls the constant ppm; what is
//          left is ~1.5 ms of designed latency (which MUST be hidden inside
//          the playout delay — see the measured note in docs/aec-audit.md)
//          plus sub-frame phase breathing below the servo bandwidth.
#include <cmath>
#include <cstdio>
#include <vector>

#include "srt/srt.h"
#include "support/itu_chain.h"

using namespace mutap_test;
using namespace mutap_test::itu;

namespace {

    // Render-clock view of the capture-domain signal x: r[n] = x(n/(1+eps)).
    std::vector<double> render_stream(const std::vector<double>& x, double ppm) {
        const double        step = 1.0 / (1.0 + ppm * 1e-6);
        std::vector<double> out(static_cast<size_t>(static_cast<double>(x.size()) / step));
        for (size_t n = 0; n < out.size(); ++n) {
            const double t  = static_cast<double>(n) * step;
            const size_t i0 = static_cast<size_t>(t);
            const double fr = t - static_cast<double>(i0);
            const double a  = i0 < x.size() ? x[i0] : 0.0;
            const double b  = i0 + 1 < x.size() ? x[i0 + 1] : 0.0;
            out[n]          = a + fr * (b - a);
        }
        return out;
    }

    double run_case(const rate_setup& rs, const std::vector<double>& path, const std::vector<double>& x_ref,
                    const std::vector<double>& x_path, double* erle_out) {
        compliance_dut<double> c(chain_config<double>(rs), rs.block);
        const size_t           b  = rs.block;
        const size_t           lf = path.size();
        std::vector<double>    hist(lf - 1 + b, 0.0);
        std::vector<double>    y(b), e(b), out, echo;
        const size_t           nblocks = std::min(x_ref.size(), x_path.size()) / b;
        for (size_t blk = 0; blk < nblocks; ++blk) {
            for (size_t i = 0; i < b; ++i) {
                hist[lf - 1 + i] = x_path[blk * b + i];
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
        double se = 0.0, sy = 0.0;
        for (size_t i = out.size() * 2 / 3; i < out.size(); ++i) {
            se += out[i] * out[i];
            sy += echo[i] * echo[i];
        }
        *erle_out = 10.0 * std::log10(sy / (se + 1e-30));
        return max_level_dbm0a(out, rs.fs, out.size() * 2 / 3, out.size());
    }

    // Push the render stream through the ASRC at (1+eps) frames per capture
    // frame, pulling capture-rate blocks: the recovered reference the AEC
    // would see in a real split-clock deployment.
    std::vector<double> asrc_recover(const std::vector<double>& r, double ppm, double fs, size_t total) {
        tap::samplerate::config cfg = tap::samplerate::config::for_sample_rate(fs);
        cfg.channels                = 1;
        tap::samplerate::async_sample_rate_converter asrc(cfg);

        std::vector<double> out;
        out.reserve(total);
        std::vector<float> pushf, pullf(4);
        const double       rate = 1.0 + ppm * 1e-6; // render frames per capture frame
        double             acc  = 0.0;
        size_t             rpos = 0;
        while (out.size() < total) {
            acc += 4.0 * rate; // fine-grained transfer: 4-frame chunks (QUIET-capable)
            const size_t n = static_cast<size_t>(acc);
            acc -= static_cast<double>(n);
            pushf.resize(n);
            for (size_t i = 0; i < n; ++i) {
                pushf[i] = rpos < r.size() ? static_cast<float>(r[rpos++]) : 0.0F;
            }
            asrc.push(pushf.data(), n);
            asrc.pull(pullf.data(), 4);
            for (size_t i = 0; i < 4 && out.size() < total; ++i) {
                out.push_back(static_cast<double>(pullf[i]));
            }
        }
        const auto st = asrc.status();
        std::printf("    [asrc state %d  ppm %.2f  fill %.1f  under %llu over %llu resync %llu]\n",
                    static_cast<int>(st.state), st.ppm, st.fifo_fill_frames,
                    static_cast<unsigned long long>(st.underruns), static_cast<unsigned long long>(st.overruns),
                    static_cast<unsigned long long>(st.resyncs));
        return out;
    }

} // namespace

int main() {
    const auto rs = setup_48k();
    // Realistic playout delay: 5 ms of render buffering + flight ahead of
    // the RIR (in-span per the audit's F3 probe), so the reference tap
    // leads the echo as it does in any real system.
    const size_t        bulk = static_cast<size_t>(0.005 * rs.fs);
    auto                base = compliance_path(room::cabin, rs);
    base.resize(base.size() - bulk);
    std::vector<double> path(bulk, 0.0);
    path.insert(path.end(), base.begin(), base.end());
    css_config cc;
    cc.periods = 90;
    cc.shaped  = true;
    auto x     = make_css_at(cc, rs.fs);
    set_level_dbm0(x, -16.0);

    for (double ppm : {0.0, 50.0, 100.0, 300.0}) {
        const auto r = render_stream(x, ppm);
        double     erle;
        // Naive: render-clock stream fed straight in as the reference.
        const double naive = run_case(rs, path, r, x, &erle);
        std::printf("ppm %5.0f  naive     residual %7.1f dBm0(A)  ERLE %5.1f dB\n", ppm, naive, erle);
        // ASRC-recovered reference.
        const auto   xr    = asrc_recover(r, ppm, rs.fs, x.size());
        const double fixed = run_case(rs, path, xr, x, &erle);
        std::printf("ppm %5.0f  via ASRC  residual %7.1f dBm0(A)  ERLE %5.1f dB\n", ppm, fixed, erle);
    }
    return 0;
}
