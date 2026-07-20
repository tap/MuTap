// vdsp_derisk.cpp — Does Apple's vDSP (Accelerate) beat clang-autovectorized
// Ooura for MuTap's float32 real FFT ON THIS MAC? Measured-first gate before we
// consider a vDSP backend (mirrors the CMSIS/M55 de-risk).
//
// Build from a MuTap checkout (needs the vendored Ooura float source):
//
//   clang++ -std=c++17 -O3 vdsp_derisk.cpp third_party/ooura/fftsg_float.c \
//       -framework Accelerate -o vdsp_derisk && ./vdsp_derisk
//
// (fftsg_float.c #includes fftsg.c from the same dir, so keep it alongside.)
//
// Reports, at the two certified sizes (512 = canceller, 2048 = suppressor):
//   (a) PARITY  — vDSP reconciled to Ooura's packed contract: the empirically
//                 fit scale, the sign convention, and the max relative error.
//   (b) SPEED   — ns per forward transform, apples-to-apples: both take a real
//                 block and produce a packed spectrum out-of-place. The vDSP
//                 timing INCLUDES the split-complex conversions the wrapper
//                 would have to pay (vDSP is split-complex; our layout is
//                 interleaved) — that overhead is the whole question.
//
// Decision rule (same as CMSIS): >= ~2x -> worth a backend; < ~1.3x -> skip it,
// clang-autovec Ooura is already fine on a machine with this much headroom.

#include <Accelerate/Accelerate.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" void rdft_f(int n, int isgn, float* a, int* ip, float* w);

namespace {

    std::vector<float> broadband(int n, std::uint32_t seed) {
        std::vector<float> x(n);
        std::uint32_t      s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = (static_cast<float>(s) / 2147483648.0f - 1.0f) * 0.1f;
        }
        return x;
    }

    // Ooura forward (out-of-place, our wrapper's forward()): copy real -> buf,
    // rdft in place. buf ends up in the interleaved packed layout:
    // [DC, Nyquist, re1, im1, re2, im2, ...], sign convention exp(+i).
    struct ooura {
        int                m_n;
        std::vector<int>   m_ip;
        std::vector<float> m_w;
        explicit ooura(int n)
            : m_n(n)
            , m_ip(2 + static_cast<int>(std::sqrt(n / 2.0)) + 1)
            , m_w(n / 2) {
            m_ip[0] = 0;
        }
        void forward(const float* in, float* packed) {
            for (int i = 0; i < m_n; ++i) {
                packed[i] = in[i];
            }
            rdft_f(m_n, 1, packed, m_ip.data(), m_w.data());
        }
    };

    // vDSP forward (out-of-place): ctoz (deinterleave real -> split), zrip
    // (packed real FFT), then reinterleave/scale/conjugate into Ooura's exact
    // packed layout so the two are directly comparable.
    struct vdsp {
        int                m_n, m_log2n;
        FFTSetup           m_setup;
        std::vector<float> m_rp, m_ip;
        explicit vdsp(int n)
            : m_n(n)
            , m_log2n(static_cast<int>(std::lround(std::log2(n))))
            , m_rp(n / 2)
            , m_ip(n / 2) {
            m_setup = vDSP_create_fftsetup(m_log2n, kFFTRadix2);
        }
        ~vdsp() { vDSP_destroy_fftsetup(m_setup); }

        // scale/conj chosen to match Ooura; measured once, applied in the loop.
        float m_scale = 0.5f;
        float m_conj  = -1.0f;

        void forward(const float* in, float* packed) {
            DSPSplitComplex split{m_rp.data(), m_ip.data()};
            vDSP_ctoz(reinterpret_cast<const DSPComplex*>(in), 2, &split, 1, m_n / 2);
            vDSP_fft_zrip(m_setup, &split, 1, m_log2n, kFFTDirection_Forward);
            packed[0] = m_rp[0] * m_scale; // DC
            packed[1] = m_ip[0] * m_scale; // Nyquist
            for (int k = 1; k < m_n / 2; ++k) {
                packed[2 * k]     = m_rp[k] * m_scale;
                packed[2 * k + 1] = m_ip[k] * m_scale * m_conj;
            }
        }
    };

    double ns_per(int iters, double seconds) { return seconds * 1e9 / iters; }

    void run(int n) {
        const auto in = broadband(n, 0x9E3779B9u);
        ooura      oo(n);
        vdsp       vd(n);

        std::vector<float> ref(n), got(n);
        oo.forward(in.data(), ref.data());

        // Fit vDSP's scale from the REAL parts (convention-independent): DC,
        // Nyquist, and the even-index reals. Then pick the conjugation that
        // matches Ooura's imag parts.
        DSPSplitComplex split{vd.m_rp.data(), vd.m_ip.data()};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(in.data()), 2, &split, 1, n / 2);
        vDSP_fft_zrip(vd.m_setup, &split, 1, vd.m_log2n, kFFTDirection_Forward);
        double num = 0, den = 0;
        num += vd.m_rp[0] * ref[0];
        den += vd.m_rp[0] * vd.m_rp[0]; // DC
        num += vd.m_ip[0] * ref[1];
        den += vd.m_ip[0] * vd.m_ip[0]; // Nyquist
        for (int k = 1; k < n / 2; ++k) {
            num += vd.m_rp[k] * ref[2 * k];
            den += vd.m_rp[k] * vd.m_rp[k];
        }
        vd.m_scale = static_cast<float>(num / den);
        // Conjugation: compare imag with +scale and -scale, keep the better.
        double ep = 0, em = 0;
        for (int k = 1; k < n / 2; ++k) {
            ep = std::fmax(ep, std::fabs(vd.m_ip[k] * vd.m_scale - ref[2 * k + 1]));
            em = std::fmax(em, std::fabs(-vd.m_ip[k] * vd.m_scale - ref[2 * k + 1]));
        }
        vd.m_conj = (em <= ep) ? -1.0f : 1.0f;

        vd.forward(in.data(), got.data());
        float peak = 0, err = 0;
        for (int i = 0; i < n; ++i) {
            peak = std::fmax(peak, std::fabs(ref[i]));
            err  = std::fmax(err, std::fabs(got[i] - ref[i]));
        }

        // --- speed: warm, then time a big batch of out-of-place forwards ---
        const int          iters = n <= 512 ? 400000 : 120000;
        std::vector<float> scratch(n);
        double             sink = 0;
        for (int w = 0; w < 2000; ++w) {
            oo.forward(in.data(), scratch.data());
            vd.forward(in.data(), scratch.data());
        }
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) {
            oo.forward(in.data(), scratch.data());
            sink += scratch[0] + scratch[n / 2];
        }
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) {
            vd.forward(in.data(), scratch.data());
            sink += scratch[0] + scratch[n / 2];
        }
        auto t2 = std::chrono::steady_clock::now();

        const double oo_ns = ns_per(iters, std::chrono::duration<double>(t1 - t0).count());
        const double vd_ns = ns_per(iters, std::chrono::duration<double>(t2 - t1).count());

        std::printf("N=%-5d parity: scale=%.6f conj=%+d rel_err=%.2e | "
                    "Ooura %.1f ns  vDSP %.1f ns  -> %.2fx  (sink=%.3g)\n",
                    n, vd.m_scale, static_cast<int>(vd.m_conj), err / peak, oo_ns, vd_ns,
                    oo_ns / vd_ns, sink);
    }

} // namespace

int main() {
    std::printf("vDSP vs clang-autovec Ooura, float32 forward real FFT (out-of-place)\n");
    std::printf("timing includes vDSP's split-complex ctoz/reinterleave (the real cost)\n\n");
    run(512);
    run(2048);
    std::printf("\nRule of thumb: >=~2x favors a vDSP backend; <~1.3x, keep Ooura.\n");
    return 0;
}
