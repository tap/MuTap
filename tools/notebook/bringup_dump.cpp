// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// The SIMULATED-DEVICE measurement set for the outdoor bring-up recipe
// (tools/notebook/build_outdoor_bringup.py -> notebooks/outdoor_bringup.ipynb).
//
// The outdoor chain is device-calibrated, and the device does not exist
// yet: today's device IS the Rev 6 simulated one (tests/support/
// outdoor_scenario.h — tanh loudspeaker at the moderate drive, the
// close-range path at erl -20). This dump plays the bring-up
// measurement session against that simulated device and writes exactly
// the recordings a real bring-up session would produce with a signal
// generator and the device's own mic:
//
//   meta.json      fs / block / levels / true drive & erl (truth fields
//                  are for cross-check plots only — a real session has
//                  no truth column)
//   far_end.f64    the calibration program: shaped CSS at the operating
//                  plane (raw little-endian float64, like every file)
//   spk_out.f64    the loudspeaker's electrical/acoustic output for
//                  far_end (real device: loopback / current-sense tap)
//   mic.f64        the mic during far-end single talk (echo only)
//   sweep.f64      exponential sine sweep, played QUIET (-30 dBm0, below
//                  the knee) — the echo-path measurement signal
//   sweep_mic.f64  the mic during the sweep
//   tones_in.f64   stepped-level 1 kHz bursts (-30..-4 dBm0) — the
//   tones_mic.f64  nonlinearity measurement, and the mic during it
//   path_ir.f64    TRUTH: the simulated path (cross-check only)
//
// The notebook's analysis cells consume ONLY these files; pointing them
// at a directory of real-device recordings with the same names re-runs
// the whole recipe unchanged.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <string>
#include <vector>

#include "support/outdoor_scenario.h"

namespace {

    using namespace mutap_test;
    using namespace mutap_test::outdoor;

    constexpr double k_fs       = 48000.0;
    constexpr size_t k_block    = 256;
    constexpr size_t k_taps     = 2048;
    constexpr double k_level    = -10.0; ///< operating plane, dBm0
    constexpr double k_erl      = -20.0;
    constexpr double k_sweep_db = -30.0; ///< below the knee: the IR measurement is linear
    constexpr double k_tone_hz  = 997.0; ///< off-bin-ish, standard measurement tone

    void write_f64(const std::string& dir, const char* name, const std::vector<double>& v) {
        const std::string p = dir + "/" + name;
        FILE*             f = std::fopen(p.c_str(), "wb");
        if (f == nullptr) {
            std::perror(p.c_str());
            std::exit(1);
        }
        std::fwrite(v.data(), sizeof(double), v.size(), f);
        std::fclose(f);
    }

    /// Route a signal through the simulated device (loudspeaker
    /// nonlinearity, then the acoustic path) and return both taps.
    struct device_capture {
        std::vector<double> spk;
        std::vector<double> mic;
    };
    device_capture through_device(const std::vector<double>& x, const std::vector<double>& path,
                                  const speaker_drive& sp) {
        device_capture out;
        out.spk = sp.apply(x);
        echo_sim<double>::config sc;
        sc.echo_path  = path;
        sc.block_size = k_block;
        echo_sim<double> sim(sc);
        out.mic.reserve(x.size());
        for (size_t blk = 0; blk + 1 <= x.size() / k_block; ++blk) {
            sim.step(&x[blk * k_block], &out.spk[blk * k_block], nullptr, nullptr);
            const auto& d = sim.echo_block();
            out.mic.insert(out.mic.end(), d.begin(), d.end());
        }
        out.spk.resize(out.mic.size());
        return out;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bringup_dump <output-dir>\n");
        return 1;
    }
    const std::string dir = argv[1];

    const auto          path = make_outdoor_path(k_fs, k_taps, k_erl);
    const speaker_drive sp{k_drive_moderate};

    // Program material at the operating plane.
    auto far_end = make_far_end(k_fs, 8.4, k_level);
    auto prog    = through_device(far_end, path, sp);
    far_end.resize(prog.mic.size());
    write_f64(dir, "far_end.f64", far_end);
    write_f64(dir, "spk_out.f64", prog.spk);
    write_f64(dir, "mic.f64", prog.mic);

    // Exponential sine sweep, quiet (linear-regime IR measurement),
    // 20 Hz .. 20 kHz over 2 s plus a 0.5 s tail of silence.
    {
        const double        t_sweep = 2.0;
        const size_t        n       = static_cast<size_t>((t_sweep + 0.5) * k_fs);
        const double        f0      = 20.0;
        const double        f1      = 20000.0;
        const double        r       = std::log(f1 / f0);
        std::vector<double> sw(n, 0.0);
        for (size_t i = 0; i < static_cast<size_t>(t_sweep * k_fs); ++i) {
            const double t = static_cast<double>(i) / k_fs;
            sw[i]          = std::sin(2.0 * std::numbers::pi * f0 * t_sweep / r * (std::exp(t * r / t_sweep) - 1.0));
        }
        itu::set_level_dbm0(sw, k_sweep_db);
        auto cap = through_device(sw, path, sp);
        sw.resize(cap.mic.size());
        write_f64(dir, "sweep.f64", sw);
        write_f64(dir, "sweep_mic.f64", cap.mic);
    }

    // Stepped-level tone bursts for the nonlinearity curve: 0.5 s of
    // 997 Hz at each level, 0.25 s of silence between.
    const std::vector<double> tone_levels = {-30.0, -24.0, -18.0, -12.0, -8.0, -4.0};
    {
        const size_t        n_on  = static_cast<size_t>(0.5 * k_fs);
        const size_t        n_off = static_cast<size_t>(0.25 * k_fs);
        std::vector<double> tones;
        for (const double lvl : tone_levels) {
            std::vector<double> burst(n_on);
            for (size_t i = 0; i < n_on; ++i) {
                burst[i] = std::sin(2.0 * std::numbers::pi * k_tone_hz * static_cast<double>(i) / k_fs);
            }
            itu::set_level_dbm0(burst, lvl);
            tones.insert(tones.end(), burst.begin(), burst.end());
            tones.insert(tones.end(), n_off, 0.0);
        }
        auto cap = through_device(tones, path, sp);
        tones.resize(cap.mic.size());
        write_f64(dir, "tones_in.f64", tones);
        write_f64(dir, "tones_mic.f64", cap.mic);
    }

    // Truth (cross-check plots only).
    write_f64(dir, "path_ir.f64", path);

    {
        const std::string p = dir + "/meta.json";
        FILE*             f = std::fopen(p.c_str(), "wb");
        if (f == nullptr) {
            std::perror(p.c_str());
            return 1;
        }
        std::fprintf(f,
                     "{\n"
                     "  \"fs\": %.1f, \"block\": %zu, \"taps\": %zu,\n"
                     "  \"level_dbm0\": %.1f, \"sweep_dbm0\": %.1f, \"tone_hz\": %.1f,\n"
                     "  \"tone_levels_dbm0\": [-30, -24, -18, -12, -8, -4],\n"
                     "  \"truth\": { \"drive\": %.2f, \"erl_db\": %.1f }\n"
                     "}\n",
                     k_fs, k_block, k_taps, k_level, k_sweep_db, k_tone_hz, k_drive_moderate, k_erl);
        std::fclose(f);
    }
    std::fprintf(stderr, "[bringup_dump] wrote simulated-device measurement set to %s\n", dir.c_str());
    return 0;
}
