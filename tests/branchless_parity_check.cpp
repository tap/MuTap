// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Bit-identity guard for the suppressor's two pass-1 forms (see
// MUTAP_SUPPRESSOR_BRANCHLESS in include/mutap/postfilter.h). The branch-free
// form is compiled on Arm Helium and the branchy form everywhere else, so no
// single build exercises both; CI compiles THIS file twice — once per macro
// value — and diffs the fingerprint below. Identical fingerprints prove the
// two forms produce sample-exact output, which is what lets the branch-free
// form ride the compliance battery that certified the branchy one.
//
// Standalone (not part of mutap_tests): the whole point is to build it once
// per macro value. See the "branchless parity" step in .github/workflows/ci.yml.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mutap/postfilter.h"

int main() {
    auto               chain = mutap::aec_chain<float>(mutap::aec_chain_preset<float>(256, 8, 48000.0));
    constexpr int      B     = 256;
    std::vector<float> x(B), y(B), o(B);

    std::uint32_t s  = 0x12345u;
    auto          nx = [&s]() noexcept {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s) / 2147483648.0f - 1.0f;
    };

    // FNV-1a over the raw bits of every output sample: any single-ULP
    // divergence between the two forms flips the fingerprint.
    std::uint64_t fp  = 1469598103934665603ULL;
    auto          mix = [&fp](float v) noexcept {
        std::uint32_t b;
        __builtin_memcpy(&b, &v, 4);
        fp = (fp ^ b) * 1099511628211ULL;
    };

    // A corpus varied enough to drive every data-dependent branch the two
    // forms disagree on if they ever diverge: level sweeps, double-talk
    // bursts (near-end incoherent with the echo), and quiet stretches.
    for (int blk = 0; blk < 600; ++blk) {
        const float amp = 0.05f + 0.1f * std::fabs(std::sin(blk * 0.03f));
        const bool  dt  = (blk / 40) % 3 == 0;
        for (int i = 0; i < B; ++i) {
            const float far  = amp * nx();
            const float echo = 0.3f * far;
            const float near = dt ? 0.08f * std::sin((blk * B + i) * 0.05f) + 0.03f * nx() : 0.0f;
            x[i]             = far;
            y[i]             = echo + near + 0.001f * nx();
        }
        chain.process_block(x.data(), y.data(), o.data());
        for (float v : o) {
            mix(v);
        }
    }

    std::printf("MUTAP_SUPPRESSOR_PARITY_FP %016llx branchless=%d\n", static_cast<unsigned long long>(fp),
                MUTAP_SUPPRESSOR_BRANCHLESS);
    return 0;
}
