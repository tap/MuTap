// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// #31 diagnostic, step 1: is the float32 real FFT bit-deterministic?
//
// The macOS float32 flake has been established as a PER-PROCESS bifurcation —
// the same binary on the same host passes one repetition and fails the next,
// landing on one of two values that are bit-identical across machines, weeks
// and build types. Something varies per process and selects between them.
//
// This probe asks the narrowest possible version of "is it the FFT?": run one
// forward transform of a fixed input at both certified geometries (N=512 for
// the canceller, N=2048 for the residual suppressor) and print a hash of the
// exact output bits. Run the program many times and compare.
//
//   hashes vary across processes -> the float32 FFT is not deterministic, and
//       fft.h's documented contract ("vDSP agrees with Ooura to <4e-7
//       relative") is false as written. #31 is then a DspTap contract bug.
//   hashes constant -> the FFT is deterministic and the bifurcation is
//       DOWNSTREAM of it: the chain has two basins reachable from identical
//       inputs, which is a materially harder and more serious finding.
//
// Each line also reports whether two transforms WITHIN one process agree, so
// intra-process scratch reuse is covered as well as process-to-process.
//
// Deliberately standalone (compiled directly in CI, like
// branchless_parity_check.cpp) so it can be built against either float32
// backend from one source without a CMake round trip.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "tap/dsp/fft.h"

namespace {

    // Same generator as DspTap's fft_backend_parity, so the material is the
    // one the existing backend gate already uses.
    std::vector<float> broadband(int n, unsigned seed) {
        std::vector<float> x(static_cast<size_t>(n));
        unsigned           s = seed;
        for (auto& v : x) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            v = (static_cast<float>(s) / 2147483648.0f - 1.0f) * 0.1f;
        }
        return x;
    }

    std::uint64_t fnv1a(const void* p, size_t bytes) {
        const auto*   b = static_cast<const unsigned char*>(p);
        std::uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < bytes; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
        return h;
    }

} // namespace

int main() {
    for (const int n : {512, 2048}) {
        const auto x = broadband(n, 0x9E3779B9u);

        tap::dsp::basic_real_fft<float> fft(static_cast<size_t>(n));
        std::vector<float>              a = x;
        std::vector<float>              b = x;
        fft.forward_inplace(a.data());
        fft.forward_inplace(b.data()); // same object, same input, twice

        const auto ha = fnv1a(a.data(), a.size() * sizeof(float));
        const auto hb = fnv1a(b.data(), b.size() * sizeof(float));
        std::printf("N=%d %016llx %s\n", n, static_cast<unsigned long long>(ha),
                    ha == hb ? "intra-ok" : "INTRA-VARIES");
    }
    return 0;
}
