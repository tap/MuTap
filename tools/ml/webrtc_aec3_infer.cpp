// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Offline driver for WebRTC AEC3 (the echo canceller of current Chrome /
// WebRTC, BSD-3), so the benchmark harness and the comparison notebook can
// put it on the same meter as MuTap's engines and DTLN-aec. Requires
// webrtc-audio-processing >= 1.0 (the freedesktop extraction that carries
// AEC3; the CMake target only appears when pkg-config finds it — see
// tools/ml/README.md for the build recipe).
//
// Usage: webrtc_aec3_infer <x.f64> <y.f64> <e.f64> [scale] [rate]
//
// x = far-end reference, y = microphone, mono float64 raw at 16 kHz.
// AEC3 processes 10 ms frames of float in [-1, 1] and its internal
// estimators are level-aware, so unit-RMS benchmark material is scaled
// down on the way in and back up on the way out (default 0.05, the same
// convention the DTLN wrapper uses).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <modules/audio_processing/include/audio_processing.h>

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
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr, "usage: %s <x.f64> <y.f64> <e.f64> [scale] [rate]\n", argv[0]);
        return 1;
    }
    const auto   x     = read_f64(argv[1]);
    const auto   y     = read_f64(argv[2]);
    const double scale = (argc >= 5) ? std::atof(argv[4]) : 0.05;
    const int    rate  = (argc >= 6) ? std::atoi(argv[5]) : 16000;
    const size_t n     = std::min(x.size(), y.size());

    auto                            apm = webrtc::AudioProcessingBuilder().Create();
    webrtc::AudioProcessing::Config cfg;
    cfg.echo_canceller.enabled     = true;
    cfg.echo_canceller.mobile_mode = false; // AEC3, not AECM
    apm->ApplyConfig(cfg);

    const int                  k_rate  = rate;
    const size_t               k_frame = static_cast<size_t>(k_rate / 100); // 10 ms
    const webrtc::StreamConfig sc(k_rate, 1);

    std::vector<double> e(n, 0.0);
    std::vector<float>  far_buf(k_frame);
    std::vector<float>  near_buf(k_frame);
    for (size_t i = 0; i + k_frame <= n; i += k_frame) {
        for (size_t j = 0; j < k_frame; ++j) {
            far_buf[j]  = static_cast<float>(x[i + j] * scale);
            near_buf[j] = static_cast<float>(y[i + j] * scale);
        }
        float* far_ch[1]  = {far_buf.data()};
        float* near_ch[1] = {near_buf.data()};
        if (apm->ProcessReverseStream(far_ch, sc, sc, far_ch) != 0) {
            std::fprintf(stderr, "ProcessReverseStream failed\n");
            return 1;
        }
        apm->set_stream_delay_ms(0);
        if (apm->ProcessStream(near_ch, sc, sc, near_ch) != 0) {
            std::fprintf(stderr, "ProcessStream failed\n");
            return 1;
        }
        for (size_t j = 0; j < k_frame; ++j) {
            e[i + j] = static_cast<double>(near_ch[0][j]) / scale;
        }
    }

    std::FILE* f = std::fopen(argv[3], "wb");
    if (f == nullptr) {
        std::perror(argv[3]);
        return 1;
    }
    std::fwrite(e.data(), sizeof(double), e.size(), f);
    std::fclose(f);
    return 0;
}
