// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Scenario dump for the ML echo-cancellation benchmark harness (tools/ml).
//
// Emits the AEC double-talk protocol's signals (tests/test_aec.cpp header:
// converge / double-talk / recovery, extended with a near-end-only segment
// for measuring near-end preservation) as raw float64 files, using the SAME
// material generators (tests/support/closed_loop.h) and echo simulator
// (tests/support/echo_scenario.h) the test suite's published numbers come
// from. External systems (e.g. a neural AEC) are then run offline on these
// signals and measured by tools/ml/metrics.py with one shared meter, so a
// comparison never mixes two implementations of a metric.
//
// Usage: aec_scenario_dump <outdir> <material> <seed> <room>
//   material  near-end program during double-talk: ar | voiced | music
//   room      studio | synth<N> (random decaying RIR, seed N)
//
// Writes x.f64 (far end), v.f64 (near end), d.f64 (true echo), y.f64 (mic),
// path.f64 (true room), manifest.json (protocol geometry). All streams are
// elementwise-aligned, little-endian float64, one channel.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "fixtures/rir_studio.h"
#include "support/closed_loop.h"
#include "support/echo_scenario.h"

namespace {

    using mutap_test::echo_sim;

    constexpr size_t k_block = 64;
    constexpr size_t k_taps  = 1024;

    // Segment lengths in blocks, mirroring run_double_talk (test_aec.cpp)
    // plus the near-end-only preservation segment.
    constexpr size_t k_converge_blocks  = 1500;
    constexpr size_t k_dt_blocks        = 600;
    constexpr size_t k_recovery_blocks  = 600;
    constexpr size_t k_near_only_blocks = 600;

    // Same generator families as test_aec.cpp.
    std::vector<double> random_decaying_rir(size_t taps, unsigned seed) {
        std::mt19937                     gen(seed);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double>              f(taps);
        double                           energy = 0.0;
        for (size_t i = 0; i < taps; ++i) {
            const double v = dist(gen) * std::exp(-static_cast<double>(i) / (static_cast<double>(taps) / 4.0));
            f[i]           = v;
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return f;
    }

    std::vector<double> studio_path() {
        std::vector<double> f(mutap_test::fixtures::k_rir_studio, mutap_test::fixtures::k_rir_studio + k_taps);
        double              energy = 0.0;
        for (const double v : f) {
            energy += v * v;
        }
        for (auto& v : f) {
            v /= std::sqrt(energy);
        }
        return f;
    }

    std::vector<double> near_material(const std::string& name, size_t n, unsigned seed) {
        if (name == "ar") {
            return mutap_test::ar_near_end<double>(n, seed);
        }
        if (name == "voiced") {
            return mutap_test::voiced_near_end<double>(n, seed);
        }
        if (name == "music") {
            return mutap_test::music_near_end<double>(n, seed);
        }
        std::fprintf(stderr, "unknown material '%s' (ar | voiced | music)\n", name.c_str());
        std::exit(1);
    }

    void write_f64(const std::string& path, const std::vector<double>& v) {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) {
            std::perror(path.c_str());
            std::exit(1);
        }
        std::fwrite(v.data(), sizeof(double), v.size(), f);
        std::fclose(f);
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <outdir> <material> <seed> <room>\n", argv[0]);
        return 1;
    }
    const std::string outdir   = argv[1];
    const std::string material = argv[2];
    const unsigned    seed     = static_cast<unsigned>(std::stoul(argv[3]));
    const std::string room     = argv[4];

    const std::vector<double> path =
        (room == "studio") ? studio_path()
                           : random_decaying_rir(k_taps, static_cast<unsigned>(std::stoul(room.substr(5))));

    // Far end: speech-envelope material throughout (the protocol's choice),
    // per-segment seeds exactly as run_double_talk. Near end: the requested
    // material during double-talk, and again (fresh seed) near-end-only.
    const size_t n_converge  = k_converge_blocks * k_block;
    const size_t n_dt        = k_dt_blocks * k_block;
    const size_t n_recovery  = k_recovery_blocks * k_block;
    const size_t n_near_only = k_near_only_blocks * k_block;
    const size_t n_total     = n_converge + n_dt + n_recovery + n_near_only;

    std::vector<double> x(n_total, 0.0);
    std::vector<double> v(n_total, 0.0);

    const auto x1 = mutap_test::ar_near_end<double>(n_converge, seed);
    const auto x2 = mutap_test::ar_near_end<double>(n_dt, seed + 100);
    const auto v2 = near_material(material, n_dt, seed + 200);
    const auto x3 = mutap_test::ar_near_end<double>(n_recovery, seed + 300);
    const auto v4 = near_material(material, n_near_only, seed + 400);

    std::memcpy(x.data(), x1.data(), n_converge * sizeof(double));
    std::memcpy(x.data() + n_converge, x2.data(), n_dt * sizeof(double));
    std::memcpy(v.data() + n_converge, v2.data(), n_dt * sizeof(double));
    std::memcpy(x.data() + n_converge + n_dt, x3.data(), n_recovery * sizeof(double));
    std::memcpy(v.data() + n_converge + n_dt + n_recovery, v4.data(), n_near_only * sizeof(double));

    // Run the simulator open-loop (no canceller) to produce echo and mic.
    echo_sim<double>::config scfg;
    scfg.echo_path  = path;
    scfg.block_size = k_block;
    echo_sim<double> sim(scfg);

    std::vector<double> d(n_total);
    std::vector<double> y(n_total);
    for (size_t blk = 0; blk < n_total / k_block; ++blk) {
        sim.step(&x[blk * k_block], &v[blk * k_block], nullptr);
        std::memcpy(d.data() + blk * k_block, sim.echo_block().data(), k_block * sizeof(double));
        std::memcpy(y.data() + blk * k_block, sim.error_block().data(), k_block * sizeof(double));
    }

    write_f64(outdir + "/x.f64", x);
    write_f64(outdir + "/v.f64", v);
    write_f64(outdir + "/d.f64", d);
    write_f64(outdir + "/y.f64", y);
    write_f64(outdir + "/path.f64", path);

    std::FILE* mf = std::fopen((outdir + "/manifest.json").c_str(), "w");
    if (mf == nullptr) {
        std::perror("manifest.json");
        return 1;
    }
    std::fprintf(mf,
                 "{\n"
                 "  \"block_size\": %zu,\n"
                 "  \"taps\": %zu,\n"
                 "  \"material\": \"%s\",\n"
                 "  \"seed\": %u,\n"
                 "  \"room\": \"%s\",\n"
                 "  \"segments\": [\n"
                 "    {\"name\": \"converge\",  \"blocks\": %zu},\n"
                 "    {\"name\": \"double_talk\", \"blocks\": %zu},\n"
                 "    {\"name\": \"recovery\",  \"blocks\": %zu},\n"
                 "    {\"name\": \"near_only\", \"blocks\": %zu}\n"
                 "  ]\n"
                 "}\n",
                 k_block, k_taps, material.c_str(), seed, room.c_str(), k_converge_blocks, k_dt_blocks,
                 k_recovery_blocks, k_near_only_blocks);
    std::fclose(mf);
    return 0;
}
