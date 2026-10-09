// TEMPORARY (perf/guard-linux-cost, not for merge): dumps every
// howl_detector readout, bit for bit, after every process_block call over a
// set of configs, signals and partitions. Build once against main's header
// and once against the branch's, then cmp the two dumps.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "mutap/howl_detector.h"

namespace {

    std::uint64_t g_hash    = 1469598103934665603ULL;
    std::size_t   g_count   = 0;
    std::FILE*    g_out     = nullptr;
    std::size_t   g_trig[4] = {0, 0, 0, 0};

    template <typename T>
    void put(T v) {
        unsigned char b[sizeof(T)];
        std::memcpy(b, &v, sizeof(T));
        for (unsigned char c : b) {
            g_hash = (g_hash ^ c) * 1099511628211ULL;
        }
        std::fwrite(b, 1, sizeof(T), g_out);
        ++g_count;
    }

    template <typename S>
    void dump(const tap::mu::howl_detector<S>& d) {
        put(static_cast<int>(d.tripped()));
        put(static_cast<int>(d.verdict()));
        put(static_cast<int>(d.trigger()));
        ++g_trig[static_cast<int>(d.trigger())];
        put(d.confidence());
        put(d.peak_hz());
        put(d.line_hz());
        put(d.prominence_db());
        put(d.growth_db_per_pass());
        put(d.growth_db_per_s());
        put(d.rise_db());
        put(d.harmonic_ratio_db());
        put(d.subharmonic_ratio_db());
        put(d.level_db());
        put(d.power_db());
        for (size_t b = 0; b < d.bands(); ++b) {
            put(d.band_level_db(b));
        }
    }

    template <typename S>
    std::vector<std::vector<S>> signals(double fs) {
        std::vector<std::vector<S>>      out;
        const size_t                     n = static_cast<size_t>(fs * 3.0);
        std::mt19937                     rng(7);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<S>                   x(n);
        for (auto& v : x) {
            v = static_cast<S>(nd(rng));
        }
        out.push_back(x); // white
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            x[i] =
                static_cast<S>(1e-4 * std::exp(7.0 * std::fmod(t, 1.5)) * std::sin(2.0 * 3.141592653589793 * 1234.5 * t)
                               + 0.001 * nd(rng));
        }
        out.push_back(x); // a line growing ~61 dB/s, twice (growth path)
        std::fill(x.begin(), x.end(), S(0));
        out.push_back(x); // digital zero (the floors)
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            x[i] = static_cast<S>((t < 1.5 ? 0.001 * nd(rng) : 0.1 * std::sin(2.0 * 3.141592653589793 * 1000.0 * t)));
        }
        out.push_back(x); // a quiet floor, then a pure tone under the ceiling (relative level catch)
        for (size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / fs;
            x[i]           = static_cast<S>((t < 1.0 ? 0.05 : 2.0) * std::sin(2.0 * 3.141592653589793 * 440.0 * t)
                                            + 0.3 * std::sin(2.0 * 3.141592653589793 * 7000.0 * t)
                                            + (t > 2.0 ? 0.0 : 0.02 * nd(rng)));
        }
        out.push_back(x); // tones, an entrance, then pure tones (ceiling)
        for (size_t i = 0; i < n; ++i) {
            x[i] = (i % 4800 == 0) ? S(8) : S(1e-30) * static_cast<S>(nd(rng));
        }
        out.push_back(x); // clicks over a subnormal-level floor
        return out;
    }

    template <typename S>
    void run() {
        for (int c = 0; c < 4; ++c) {
            typename tap::mu::howl_detector<S>::config cfg;
            if (c == 1) {
                cfg.level_catch = false;
            }
            if (c == 2) {
                cfg.bands       = 24;
                cfg.block_size  = 48;
                cfg.sample_rate = 44100.0;
                cfg.f_hi_hz     = S(12000);
            }
            if (c == 3) {
                cfg.bands         = 5;
                cfg.loop_period_s = S(0.020);
                cfg.hold_s        = S(0);
            }
            tap::mu::howl_detector<S> d(cfg);
            for (const auto& x : signals<S>(cfg.sample_rate)) {
                d.reset();
                const size_t parts[] = {cfg.block_size, 1, 7, 64, 129, 3 * cfg.block_size};
                size_t       i       = 0;
                size_t       k       = 0;
                while (i < x.size()) {
                    const size_t len = std::min(parts[k++ % 6], x.size() - i);
                    d.process_block(&x[i], len);
                    dump(d);
                    i += len;
                }
            }
        }
    }

} // namespace

int main(int argc, char** argv) {
    g_out = std::fopen(argc > 1 ? argv[1] : "identity.bin", "wb");
    if (g_out == nullptr) {
        return 1;
    }
    run<float>();
    run<double>();
    std::fclose(g_out);
    std::printf("identity: %zu values, fnv1a64 %016llx; triggers none/growth/ceiling/level %zu/%zu/%zu/%zu\n", g_count,
                static_cast<unsigned long long>(g_hash), g_trig[0], g_trig[1], g_trig[2], g_trig[3]);
    return 0;
}
