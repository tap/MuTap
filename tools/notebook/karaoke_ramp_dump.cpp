// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Gain-ramp dump for the karaoke audible-limit measurement
// (docs/karaoke-afc.md, "Audible limit and runaway").
//
// The test suites measure a loop's limit by bisection with the harness's
// runaway rule (a 64-sample block at 40 dB over the unit-RMS near end).
// With a frequency shifter in the loop that is not where it becomes
// audible: program partials recirculating through the shifter ring well
// before runaway. The anti-howl PoC's protocol measures real rooms by
// ramping the bus gain 1 dB per 2 s and applying an offline howl criterion
// (tools/fixtures/howl_criterion.py) to the canceller's output c. This
// program runs the same measurement on the SIMULATED loop the tests use
// (tests/support/decorrelated_loop.h: band-limited room, forward delay,
// optional IIR SSB shifter, optional aux/backing-track feed), with the live
// PEM + FD-Kalman canceller (speech cascade, default config) adapting from
// a cold start, so the criterion can be run on it exactly as on a room
// recording:
//
//   1. warm-up: `--warmup` seconds at the start gain, `--start-below` dB
//      under the dry loop's phase-exact open-loop MSG (exact_msg_db), so
//      the criterion's calibration window is >= 15 dB under any limit;
//   2. ramp: +`--rate` dB/s (0.5 = 1 dB per 2 s, the protocol's), updated
//      every block, until the loop runs away by the harness's 40 dB rule,
//      then `--tail` seconds more, or until `--max-over` dB over the MSG.
//
// `--no-canceller` runs the dry open loop instead (channel 0 is then the
// mic): the criterion's reference ramp. `--ir-only` writes PREFIX.ir.wav and
// exits (the driver uses it at 4096 taps for the room's T30).
//
// A REVERB BEHIND THE CANCELLER (docs/reverb-afc.md; tests/support/
// reverb_rig.h, the reverb suites' forward path): `--reverb shipped|paper`
// puts a vendored Dattorro plate (FAUST, third_party/faust/) in the chain's
// reverb slot through tap::mu::reverb_mix, at `--wet`, `--decay`,
// `--damping` and `--return L|M` (L or (L + R) / 2). With a reverb, or with
// `--lib-shift`, the shift (`--shift-hz`) runs inside that stage on the
// library's frequency_shifter instead of the loop's exact-ramp oscillator:
// `--topology bus` shifts the whole bus before the reverb (the chain's slot
// order: the tail recirculates through the shifter), `--topology dry` uses
// tap::mu::shifted_dry_mix (only the dry path is shifted; the reverb is fed
// the unshifted bus). `--reverb-ir-only` writes the plate's impulse response
// (L, R) to PREFIX.reverb.wav and exits (the driver's --chain-rt60).
//
// Outputs, with `--out PREFIX`:
//   PREFIX.wav      float32, 48 kHz, 4 channels: c (the canceller output),
//                   the voice stem (the near end v as it enters the mic),
//                   the track stem (the aux feed as the loudspeaker plays it;
//                   silence when there is none) and the chain output (the
//                   forward signal after the shift and the reverb, before the
//                   gain: PROTOCOL.md 7.3's signal for a reverb-only chain);
//   PREFIX.gain.csv rows "time_s,gain_db", one per block (absolute bus gain);
//   PREFIX.ir.wav   float32 mono, the band-limited room path in the loop (for
//                   measure_rir.py's Schroeder T30, the criterion's --rt60);
//   stdout          one JSON line: the configuration, exact_msg_db and
//                   theoretical_msg_db of the dry loop, the start gain, and
//                   the ramp's own runaway gain and time.
//
// Analyse channel 0 with the criterion, the stems split into mono files:
//   howl_criterion.py analyze PREFIX.wav --channel 0 --program voice.wav
//       [--program track.wav] --gain-log PREFIX.gain.csv --rt60 T30 --dechirp
// tools/notebook/karaoke_audible.py does all of it for the configurations
// the docs report.
//
// Host-side, double precision only - never part of the emulated-target
// builds (the option defaults OFF).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "fixtures/rir_cabin.h"
#include "fixtures/rir_hall.h"
#include "fixtures/rir_rehearsal.h"
#include "fixtures/rir_studio.h"
#include "mutap/fd_kalman.h"
#include "mutap/pem_afc.h"
#include "support/closed_loop.h"
#include "support/decorrelated_loop.h"
#include "support/reverb_rig.h"
#include "support/rooms.h"

namespace {

    using mutap_test::decorrelated_loop;
    using mutap_test::forward_mode;

    constexpr size_t k_block = 64;
    constexpr double k_fs    = 48000.0;

    using kalman_afc = tap::mu::pem_afc<double, tap::mu::speech_predictor<double>, tap::mu::partitioned_fdkf<double>>;

    struct options {
        std::string room        = "cabin";
        size_t      taps        = 1024;
        bool        banded      = true;
        size_t      delay       = 480;
        double      shift_hz    = 0.0; ///< 0: no shifter (plain forward path)
        std::string material    = "held";
        bool        aux         = false;
        double      aux_db      = 0.0; ///< aux level re the unit-RMS near end
        unsigned    seed        = 2;
        double      warmup_s    = 30.0;
        double      start_below = 20.0;
        double      rate        = 0.5;
        double      max_over    = 40.0;
        double      tail_s      = 2.0;
        bool        canceller   = true;   ///< false: the dry open loop (the reference ramp)
        bool        ir_only     = false;  ///< write PREFIX.ir.wav and stop
        std::string reverb      = "none"; ///< none | shipped | paper
        double      wet         = 0.30;
        double      decay       = 0.5;
        double      damping     = 0.0005;
        std::string ret         = "L";   ///< L | M ((L + R) / 2)
        std::string topology    = "bus"; ///< bus | dry (with a shift and a reverb)
        bool        lib_shift   = false; ///< shift inside the stage, no reverb
        bool        reverb_ir   = false; ///< write PREFIX.reverb.wav and stop
        std::string out;
    };

    [[noreturn]] void usage(const char* msg) {
        std::fprintf(stderr,
                     "karaoke_ramp_dump: %s\n"
                     "usage: karaoke_ramp_dump --out PREFIX [--room cabin|studio|rehearsal|hall|mtN]\n"
                     "       [--taps 1024] [--raw] [--delay 480] [--shift-hz 0] [--material held|ar]\n"
                     "       [--aux-db DB] [--seed 2] [--warmup 30] [--start-below 20] [--rate 0.5]\n"
                     "       [--max-over 40] [--tail 2] [--no-canceller] [--ir-only]\n"
                     "       [--reverb none|shipped|paper] [--wet 0.3] [--decay 0.5] [--damping 0.0005]\n"
                     "       [--return L|M] [--topology bus|dry] [--lib-shift] [--reverb-ir-only]\n",
                     msg);
        std::exit(2);
    }

    options parse(int argc, char** argv) {
        options o;
        for (int i = 1; i < argc; ++i) {
            const std::string a    = argv[i];
            auto              next = [&]() -> std::string {
                if (i + 1 >= argc) {
                    usage(("missing value for " + a).c_str());
                }
                return argv[++i];
            };
            if (a == "--room") {
                o.room = next();
            }
            else if (a == "--taps") {
                o.taps = std::stoul(next());
            }
            else if (a == "--raw") {
                o.banded = false;
            }
            else if (a == "--delay") {
                o.delay = std::stoul(next());
            }
            else if (a == "--shift-hz") {
                o.shift_hz = std::stod(next());
            }
            else if (a == "--material") {
                o.material = next();
            }
            else if (a == "--aux-db") {
                o.aux    = true;
                o.aux_db = std::stod(next());
            }
            else if (a == "--seed") {
                o.seed = static_cast<unsigned>(std::stoul(next()));
            }
            else if (a == "--warmup") {
                o.warmup_s = std::stod(next());
            }
            else if (a == "--start-below") {
                o.start_below = std::stod(next());
            }
            else if (a == "--rate") {
                o.rate = std::stod(next());
            }
            else if (a == "--max-over") {
                o.max_over = std::stod(next());
            }
            else if (a == "--tail") {
                o.tail_s = std::stod(next());
            }
            else if (a == "--no-canceller") {
                o.canceller = false;
            }
            else if (a == "--ir-only") {
                o.ir_only = true;
            }
            else if (a == "--reverb") {
                o.reverb = next();
            }
            else if (a == "--wet") {
                o.wet = std::stod(next());
            }
            else if (a == "--decay") {
                o.decay = std::stod(next());
            }
            else if (a == "--damping") {
                o.damping = std::stod(next());
            }
            else if (a == "--return") {
                o.ret = next();
            }
            else if (a == "--topology") {
                o.topology = next();
            }
            else if (a == "--lib-shift") {
                o.lib_shift = true;
            }
            else if (a == "--reverb-ir-only") {
                o.reverb_ir = true;
            }
            else if (a == "--out") {
                o.out = next();
            }
            else {
                usage(("unknown option " + a).c_str());
            }
        }
        if (o.out.empty()) {
            usage("--out is required");
        }
        if (o.material != "held" && o.material != "ar") {
            usage("--material must be held or ar");
        }
        if (o.reverb != "none" && o.reverb != "shipped" && o.reverb != "paper") {
            usage("--reverb must be none, shipped or paper");
        }
        if (o.ret != "L" && o.ret != "M") {
            usage("--return must be L or M");
        }
        if (o.topology != "bus" && o.topology != "dry") {
            usage("--topology must be bus or dry");
        }
        if (o.reverb_ir && o.reverb == "none") {
            usage("--reverb-ir-only needs --reverb");
        }
        return o;
    }

    /// The suites' room convention (test_rir_fixtures.cpp): the first `taps`
    /// of a fixture, re-normalized to unit energy, then band-limited; or a
    /// random_decaying_rir room ("mtN", N its seed).
    std::vector<double> room_path(const options& o) {
        const float* rir = nullptr;
        if (o.room == "cabin") {
            rir = mutap_test::fixtures::k_rir_cabin;
        }
        else if (o.room == "studio") {
            rir = mutap_test::fixtures::k_rir_studio;
        }
        else if (o.room == "rehearsal") {
            rir = mutap_test::fixtures::k_rir_rehearsal;
        }
        else if (o.room == "hall") {
            rir = mutap_test::fixtures::k_rir_hall;
        }
        std::vector<double> f;
        if (rir != nullptr) {
            if (o.taps > 4096) {
                usage("--taps must be <= 4096 for a fixture");
            }
            f.assign(rir, rir + o.taps);
            double energy = 0.0;
            for (const double v : f) {
                energy += v * v;
            }
            for (auto& v : f) {
                v /= std::sqrt(energy);
            }
        }
        else if (o.room.rfind("mt", 0) == 0 && o.room.size() > 2) {
            f = mutap_test::random_decaying_rir<double>(o.taps, static_cast<unsigned>(std::stoul(o.room.substr(2))));
        }
        else {
            usage("unknown room");
        }
        return o.banded ? mutap_test::band_limited(f) : f;
    }

    void put_u32(std::FILE* f, std::uint32_t v) {
        const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                    static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
        std::fwrite(b, 1, 4, f);
    }
    void put_u16(std::FILE* f, std::uint16_t v) {
        const unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
        std::fwrite(b, 1, 2, f);
    }

    /// Interleaved float32 WAV (WAVE_FORMAT_IEEE_FLOAT), little-endian.
    bool write_wav(const std::string& path, const std::vector<std::vector<float>>& channels) {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f == nullptr) {
            return false;
        }
        const auto          nch   = static_cast<std::uint16_t>(channels.size());
        const size_t        n     = channels.empty() ? 0 : channels[0].size();
        const std::uint32_t bytes = static_cast<std::uint32_t>(n * nch * 4);
        std::fwrite("RIFF", 1, 4, f);
        put_u32(f, 36 + bytes);
        std::fwrite("WAVEfmt ", 1, 8, f);
        put_u32(f, 16);
        put_u16(f, 3); // IEEE float
        put_u16(f, nch);
        put_u32(f, static_cast<std::uint32_t>(k_fs));
        put_u32(f, static_cast<std::uint32_t>(k_fs) * nch * 4);
        put_u16(f, static_cast<std::uint16_t>(nch * 4));
        put_u16(f, 32);
        std::fwrite("data", 1, 4, f);
        put_u32(f, bytes);
        std::vector<float> frame(nch);
        for (size_t i = 0; i < n; ++i) {
            for (size_t c = 0; c < nch; ++c) {
                frame[c] = channels[c][i];
            }
            std::fwrite(frame.data(), sizeof(float), nch, f);
        }
        return std::fclose(f) == 0;
    }

    /// The reverb suites' forward path for these options (reverb_rig.h).
    mutap_test::reverb::params rig_params(const options& o) {
        namespace rv = mutap_test::reverb;
        rv::params p;
        p.plate    = (o.reverb == "paper") ? rv::variant::paper : rv::variant::shipped;
        p.decay    = o.decay;
        p.damping  = o.damping;
        p.wet      = o.wet;
        p.ret      = (o.ret == "M") ? tap::mu::reverb_return::mid : tap::mu::reverb_return::left;
        p.shift_hz = o.shift_hz;
        if (o.reverb == "none") {
            p.topo = rv::topology::shift_only;
        }
        else if (o.shift_hz != 0.0) {
            p.topo = (o.topology == "dry") ? rv::topology::dry_shift : rv::topology::bus_shift;
        }
        else {
            p.topo = rv::topology::reverb;
        }
        return p;
    }

} // namespace

int main(int argc, char** argv) {
    const options o = parse(argc, argv);

    const auto path = room_path(o);
    {
        std::vector<std::vector<float>> ir(1);
        for (const double x : path) {
            ir[0].push_back(static_cast<float>(x));
        }
        if (!write_wav(o.out + ".ir.wav", ir)) {
            std::fprintf(stderr, "karaoke_ramp_dump: cannot write %s.ir.wav\n", o.out.c_str());
            return 1;
        }
        if (o.ir_only) {
            return 0;
        }
    }

    if (o.reverb_ir) {
        const auto                      h = mutap_test::reverb::plate_ir(rig_params(o).plate, o.decay, o.damping);
        std::vector<std::vector<float>> ch(2);
        for (size_t i = 0; i < h.l.size(); ++i) {
            ch[0].push_back(static_cast<float>(h.l[i]));
            ch[1].push_back(static_cast<float>(h.r[i]));
        }
        if (!write_wav(o.out + ".reverb.wav", ch)) {
            std::fprintf(stderr, "karaoke_ramp_dump: cannot write %s.reverb.wav\n", o.out.c_str());
            return 1;
        }
        return 0;
    }

    const double exact    = mutap_test::exact_msg_db(path, o.delay);
    const double max_f    = mutap_test::theoretical_msg_db(path);
    const double start_db = exact - o.start_below;

    const size_t warm_blocks = static_cast<size_t>(o.warmup_s * k_fs / static_cast<double>(k_block));
    const double ramp_s      = (o.start_below + o.max_over) / o.rate;
    const size_t max_blocks  = warm_blocks + static_cast<size_t>(ramp_s * k_fs / static_cast<double>(k_block)) + 1;
    const size_t tail_blocks = static_cast<size_t>(o.tail_s * k_fs / static_cast<double>(k_block));

    // Material: the suites' generators (support/closed_loop.h), one seed for
    // the whole run; the aux feed as test_afc_decorrelation.cpp builds it.
    const size_t              n   = max_blocks * k_block;
    const std::vector<double> v   = (o.material == "held") ? mutap_test::voiced_near_end<double>(n, o.seed, 160)
                                                           : mutap_test::ar_near_end<double>(n, o.seed);
    const std::vector<double> aux = mutap_test::white_near_end<double>(120000, o.seed + 777);

    decorrelated_loop<double>::config lc;
    lc.feedback_path                                = path;
    lc.block_size                                   = k_block;
    lc.forward_delay                                = o.delay;
    lc.forward_gain_db                              = start_db;
    const bool                               staged = o.reverb != "none" || o.lib_shift;
    std::unique_ptr<mutap_test::reverb::rig> rig;
    if (staged) {
        rig      = std::make_unique<mutap_test::reverb::rig>(rig_params(o));
        lc.stage = mutap_test::forward_stage<double>::of(rig.get());
    }
    else if (o.shift_hz != 0.0) {
        lc.mode     = forward_mode::shift;
        lc.shift_hz = o.shift_hz;
    }
    if (o.aux) {
        lc.aux      = &aux;
        lc.aux_gain = std::pow(10.0, o.aux_db / 20.0);
    }
    decorrelated_loop<double> sim(lc);

    kalman_afc::config pc;
    pc.fdaf.block_size = k_block;
    pc.fdaf.partitions = o.taps / k_block;
    kalman_afc afc(pc);

    std::vector<std::vector<float>> wav(4);
    for (auto& ch : wav) {
        ch.reserve(n);
    }
    std::string csv = "time_s,gain_db\n";

    double runaway_db = NAN;
    double runaway_t  = NAN;
    size_t stop_at    = max_blocks;
    size_t blk        = 0;
    for (; blk < max_blocks && blk < stop_at; ++blk) {
        const double t = static_cast<double>(blk * k_block) / k_fs;
        const double g = (blk < warm_blocks)
                             ? start_db
                             : start_db + o.rate * static_cast<double>((blk - warm_blocks) * k_block) / k_fs;
        sim.set_forward_gain_db(g);
        char row[64];
        std::snprintf(row, sizeof row, "%.6f,%.6f\n", t, g);
        csv += row;

        const double rms = sim.step(&v[blk * k_block], o.canceller ? &afc : nullptr);
        const auto&  e   = sim.error_block();
        const auto&  a   = sim.aux_block();
        const auto&  fw  = sim.forward_block();
        for (size_t i = 0; i < k_block; ++i) {
            wav[0].push_back(static_cast<float>(e[i]));
            wav[1].push_back(static_cast<float>(v[blk * k_block + i]));
            wav[2].push_back(static_cast<float>(a[i]));
            wav[3].push_back(static_cast<float>(fw[i]));
        }
        if (std::isnan(runaway_db) && rms >= 100.0) {
            runaway_db = g;
            runaway_t  = t;
            stop_at    = blk + 1 + tail_blocks;
        }
    }

    if (!write_wav(o.out + ".wav", wav)) {
        std::fprintf(stderr, "karaoke_ramp_dump: cannot write %s.wav\n", o.out.c_str());
        return 1;
    }
    std::FILE* f = std::fopen((o.out + ".gain.csv").c_str(), "w");
    if (f == nullptr || std::fputs(csv.c_str(), f) < 0 || std::fclose(f) != 0) {
        std::fprintf(stderr, "karaoke_ramp_dump: cannot write %s.gain.csv\n", o.out.c_str());
        return 1;
    }

    std::printf("{\"room\": \"%s\", \"taps\": %zu, \"banded\": %s, \"delay\": %zu, \"shift_hz\": %g, "
                "\"material\": \"%s\", \"aux_db\": %s, \"seed\": %u, \"canceller\": %s, \"exact_msg_db\": %.4f, "
                "\"theoretical_msg_db\": %.4f, \"start_db\": %.4f, \"warmup_s\": %g, \"rate_db_per_s\": %g, "
                "\"runaway_db\": %s, \"runaway_t\": %s, \"seconds\": %.3f, \"reverb\": \"%s\", \"wet\": %g, "
                "\"decay\": %g, \"damping\": %g, \"return\": \"%s\", \"topology\": \"%s\", \"staged\": %s}\n",
                o.room.c_str(), o.taps, o.banded ? "true" : "false", o.delay, o.shift_hz, o.material.c_str(),
                o.aux ? std::to_string(o.aux_db).c_str() : "null", o.seed, o.canceller ? "true" : "false", exact, max_f,
                start_db, o.warmup_s, o.rate, std::isnan(runaway_db) ? "null" : std::to_string(runaway_db).c_str(),
                std::isnan(runaway_t) ? "null" : std::to_string(runaway_t).c_str(),
                static_cast<double>(blk * k_block) / k_fs, o.reverb.c_str(), o.wet, o.decay, o.damping, o.ret.c_str(),
                o.topology.c_str(), staged ? "true" : "false");
    return 0;
}
