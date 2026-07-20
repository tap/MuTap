// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
#include "webrtc_backend.h"

#include <memory>
#include <vector>

#include "api/scoped_refptr.h"
#include "modules/audio_processing/include/audio_processing.h"

#include "../aec_backend.h"

namespace mutap_compare {

    namespace {

        // AEC3 runs on 10 ms deinterleaved float frames. Each process()
        // call feeds the render (far) frame to ProcessReverseStream, then
        // the capture (mic) frame to ProcessStream and reads back the
        // cleaned send. AEC3 aligns render vs capture internally (its own
        // delay estimator), so the output is time-aligned to the capture
        // input — no extra latency to unwind, and the harness's metrics
        // are energy ratios regardless.
        class webrtc_backend final : public aec_backend {
          public:
            explicit webrtc_backend(double fs)
                : m_fs(fs)
                , m_frame(static_cast<std::size_t>(fs / 100.0)) // 10 ms
                , m_sc(static_cast<int>(fs), 1)
                , m_far(m_frame)
                , m_mic(m_frame)
                , m_out(m_frame)
                , m_rev(m_frame) {
                build();
            }

            std::string name() const override { return "webrtc"; }
            std::size_t frame() const override { return m_frame; }
            double      latency_ms() const override { return 0.0; }
            void        reset() override { build(); } // fresh APM = cold canceller

            void process(const float* far, const float* mic, float* out) override {
                std::copy_n(far, m_frame, m_far.begin());
                std::copy_n(mic, m_frame, m_mic.begin());
                const float* rin[1] = {m_far.data()};
                float*       rout[1] = {m_rev.data()};
                m_apm->ProcessReverseStream(rin, m_sc, m_sc, rout);
                m_apm->set_stream_delay_ms(0);
                const float* cin[1] = {m_mic.data()};
                float*       cout[1] = {m_out.data()};
                m_apm->ProcessStream(cin, m_sc, m_sc, cout);
                std::copy_n(m_out.begin(), m_frame, out);
            }

          private:
            void build() {
                webrtc::AudioProcessing::Config cfg;
                cfg.echo_canceller.enabled     = true;  // AEC3
                cfg.echo_canceller.mobile_mode = false; // full, not AECM
                // Everything else stays at its default (disabled) so only
                // echo cancellation shapes the output.
                m_apm = webrtc::AudioProcessingBuilder().SetConfig(cfg).Create();
            }

            double                                       m_fs;
            std::size_t                                  m_frame;
            webrtc::StreamConfig                         m_sc;
            std::vector<float>                           m_far, m_mic, m_out, m_rev;
            rtc::scoped_refptr<webrtc::AudioProcessing>  m_apm;
        };

    } // namespace

    void register_webrtc_backend() {
        register_subject({"webrtc", "WebRTC AEC3 (webrtc-audio-processing AudioProcessing, echo canceller only)",
                          [](double fs) -> std::unique_ptr<aec_backend> {
                              // AEC3 supports 8/16/32/48 kHz; guard anyway.
                              if (fs != 8000 && fs != 16000 && fs != 32000 && fs != 48000) return nullptr;
                              return std::make_unique<webrtc_backend>(fs);
                          }});
    }

} // namespace mutap_compare
