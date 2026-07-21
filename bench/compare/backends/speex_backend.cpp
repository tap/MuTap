// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include "speex_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "../aec_backend.h"
#include "speex/speex_echo.h"

namespace mutap_compare {

    namespace {

        // Speex MDF processes int16, even in its float build, so the
        // adapter quantizes at the boundary — a documented 16-bit fairness
        // caveat, and the interface real hardware presents to Speex anyway.
        // Filter length matches MuTap's certified tap budget at each rate
        // (2048 @ 48 kHz, 1024 @ 16 kHz) so the two see the same modeling
        // capacity; frame = 256 to share the harness block granularity.
        class speex_backend final : public aec_backend {
          public:
            explicit speex_backend(double fs)
                : m_fs(fs)
                , m_frame(256)
                , m_taps(fs <= 24000.0 ? 1024 : 2048)
                , m_st(speex_echo_state_init(static_cast<int>(m_frame), static_cast<int>(m_taps)))
                , m_rec(m_frame)
                , m_play(m_frame)
                , m_out(m_frame) {
                int rate = static_cast<int>(fs);
                speex_echo_ctl(m_st, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
            }
            ~speex_backend() override { speex_echo_state_destroy(m_st); }

            std::string name() const override { return "speex"; }
            std::size_t frame() const override { return m_frame; }
            double      latency_ms() const override { return 1000.0 * static_cast<double>(m_frame) / m_fs; }
            void        reset() override { speex_echo_state_reset(m_st); }

            void process(const float* far, const float* mic, float* out) override {
                for (std::size_t i = 0; i < m_frame; ++i) {
                    m_play[i] = to_i16(far[i]);
                    m_rec[i]  = to_i16(mic[i]);
                }
                // rec = microphone, play = loudspeaker/far, out = cleaned.
                speex_echo_cancellation(m_st, m_rec.data(), m_play.data(), m_out.data());
                for (std::size_t i = 0; i < m_frame; ++i) {
                    out[i] = static_cast<float>(m_out[i]) / 32768.0f;
                }
            }

          private:
            static std::int16_t to_i16(float v) {
                const float s = std::clamp(v, -1.0f, 1.0f) * 32767.0f;
                return static_cast<std::int16_t>(std::lround(s));
            }

            double                    m_fs;
            std::size_t               m_frame, m_taps;
            SpeexEchoState*           m_st;
            std::vector<std::int16_t> m_rec, m_play, m_out;
        };

    } // namespace

    void register_speex_backend() {
        register_subject(
            {"speex", "Speex MDF echo canceller (speexdsp, float/KISS build, int16 I/O)",
             [](double fs) -> std::unique_ptr<aec_backend> { return std::make_unique<speex_backend>(fs); }});
    }

} // namespace mutap_compare
