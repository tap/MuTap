// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Offline driver for tap::mu::nn_suppressor — the C++ half of the parity
// test (tools/ml/test_parity.py): reads a MUNN0001 weights file plus raw
// float64 e / yhat streams, processes block-by-block, writes the cleaned
// stream. Double instantiation, so parity with the float64 numpy reference
// is tight.
//
// Usage: nn_infer <weights.munn> <e.f64> <yhat.f64> <out.f64>

#include <cstdio>
#include <string>
#include <vector>

#include "mutap/nn_suppressor.h"

namespace {

    std::vector<double> read_f64(const char* path) {
        std::FILE* f = std::fopen(path, "rb");
        if (f == nullptr) {
            std::perror(path);
            std::exit(1);
        }
        std::fseek(f, 0, SEEK_END);
        const long bytes = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<double> v(static_cast<size_t>(bytes) / sizeof(double));
        if (std::fread(v.data(), sizeof(double), v.size(), f) != v.size()) {
            std::fprintf(stderr, "short read: %s\n", path);
            std::exit(1);
        }
        std::fclose(f);
        return v;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <weights.munn> <e.f64> <yhat.f64> <out.f64>\n", argv[0]);
        return 1;
    }
    const auto e    = read_f64(argv[2]);
    const auto yhat = read_f64(argv[3]);
    const auto n    = std::min(e.size(), yhat.size());

    tap::mu::nn_suppressor<double>::config cfg;
    cfg.weights = tap::mu::load_nn_suppressor_weights(argv[1]);
    // The parity reference (tools/ml/features.py + nn.py) models the gain
    // path alone; comfort noise would add tracked-floor fill on top.
    cfg.comfort_noise = false;
    tap::mu::nn_suppressor<double> sup(std::move(cfg));
    const size_t                   b = sup.block_size();
    std::vector<double>            out(n, 0.0);
    for (size_t i = 0; i + b <= n; i += b) {
        sup.process_block(&e[i], &yhat[i], &out[i]);
    }

    std::FILE* f = std::fopen(argv[4], "wb");
    if (f == nullptr) {
        std::perror(argv[4]);
        return 1;
    }
    std::fwrite(out.data(), sizeof(double), out.size(), f);
    std::fclose(f);
    return 0;
}
