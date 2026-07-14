/// @file fft.h
/// @brief Modern C++ wrapper around the Ooura split-radix real FFT.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <vector>

// Ooura C functions. rdft comes from third_party/ooura/fftsg.c (double);
// rdft_f is the same source instantiated for float (fftsg_float.c) so both
// precisions can link into one binary. They are built into the MuTap::fft
// static library; the MuTap::MuTap INTERFACE target links it automatically.
extern "C" {
void rdft(int n, int isgn, double* a, int* ip, double* w);
void rdft_f(int n, int isgn, float* a, int* ip, float* w);
}

namespace mutap {

    namespace detail {
        inline void ooura_rdft(int n, int isgn, double* a, int* ip, double* w) {
            rdft(n, isgn, a, ip, w);
        }
        inline void ooura_rdft(int n, int isgn, float* a, int* ip, float* w) {
            rdft_f(n, isgn, a, ip, w);
        }
    } // namespace detail

    /// Real FFT using the Ooura split-radix algorithm, parameterized over the
    /// sample type (float or double — the two Ooura instantiations).
    ///
    /// FFT size must be a power of 2 (>= 4), fixed at construction. Workspace
    /// (bit-reversal and trig tables) is allocated once in the constructor;
    /// the transforms themselves are noexcept and allocation-free, so they
    /// are safe on a real-time audio thread.
    ///
    /// Packing after a forward transform of N real samples (N/2 + 1 bins):
    ///   - bin[0].real    = data[0]   (DC;      imag is zero, not stored)
    ///   - bin[N/2].real  = data[1]   (Nyquist; imag is zero, not stored)
    ///   - bin[k].real    = data[2k], bin[k].imag = data[2k+1]  for 1 <= k < N/2
    ///
    /// Sign convention: the forward transform is A[k] = sum_j a[j]*W^(jk) with
    /// W = exp(+2*pi*i/N) — the imaginary parts are CONJUGATED relative to the
    /// engineering-convention DFT (exp(-2*pi*i/N)). Spectral products (e.g.
    /// fast convolution, FDAF regressor accumulation) are unaffected as long
    /// as every operand uses this class; conjugate if importing spectra
    /// computed elsewhere.
    ///
    /// The raw inverse is unnormalized: inverse_inplace() must be followed by
    /// a 2/N scaling for a round trip, which inverse() applies for you.
    template <typename Sample>
    class basic_real_fft {
        static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>,
                      "basic_real_fft supports the two Ooura instantiations: float and double");

      public:
        explicit basic_real_fft(size_t size)
            : m_size(static_cast<int>(size))
            , m_ip(2 + static_cast<size_t>(std::sqrt(static_cast<double>(size) / 2.0)) + 1)
            , m_w(size / 2) {
            assert(size >= 4 && (size & (size - 1)) == 0);
            m_ip[0] = 0; // triggers Ooura table init on first call
        }

        size_t size() const noexcept { return static_cast<size_t>(m_size); }
        size_t num_bins() const noexcept { return static_cast<size_t>(m_size / 2 + 1); }

        /// In-place forward FFT: time-domain Sample[size] -> packed spectrum Sample[size].
        void forward_inplace(Sample* data) noexcept { detail::ooura_rdft(m_size, 1, data, m_ip.data(), m_w.data()); }

        /// In-place inverse FFT: packed spectrum Sample[size] -> time-domain Sample[size],
        /// UNSCALED — multiply by 2/size for a normalized round trip.
        void inverse_inplace(Sample* data) noexcept { detail::ooura_rdft(m_size, -1, data, m_ip.data(), m_w.data()); }

        /// Out-of-place forward FFT. Output may alias input.
        void forward(const Sample* input, Sample* output) noexcept {
            copy(input, output);
            forward_inplace(output);
        }

        /// Out-of-place inverse FFT, scaled by 2/size so forward() -> inverse()
        /// reproduces the input. Output may alias input.
        void inverse(const Sample* input, Sample* output) noexcept {
            copy(input, output);
            inverse_inplace(output);
            const Sample scale = Sample(2) / static_cast<Sample>(m_size);
            for (int i = 0; i < m_size; ++i) {
                output[i] *= scale;
            }
        }

      private:
        void copy(const Sample* input, Sample* output) noexcept {
            if (input != output) {
                for (int i = 0; i < m_size; ++i) {
                    output[i] = input[i];
                }
            }
        }

        int                 m_size;
        std::vector<int>    m_ip;
        std::vector<Sample> m_w;
    };

    /// Double-precision real FFT — the desktop/golden-model profile.
    using real_fft = basic_real_fft<double>;

    /// Single-precision real FFT — the embedded real-time profile (Cortex-M55,
    /// Hexagon HVX), where hardware floating point is single-precision only.
    using real_fft32 = basic_real_fft<float>;

} // namespace mutap
