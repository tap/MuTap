// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors
//
// Minimal mono WAV I/O for the comparison harness's file mode (feeding a
// third-party far/mic corpus through any subject and writing the cleaned
// send for external scoring, e.g. AECMOS). 16-bit PCM and 32-bit float
// on read; 16-bit PCM on write. Enough for the AEC-Challenge clips and
// the harness's own eval set — not a general WAV library.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mutap_compare {

    struct wav_data {
        std::vector<float> samples; // mono, [-1, 1]
        int                fs = 0;
    };

    inline uint32_t rd32(const uint8_t* p) {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
    }
    inline uint16_t rd16(const uint8_t* p) {
        return uint16_t(p[0] | (p[1] << 8));
    }

    // Reads the first channel of a PCM16 or float32 WAV. Returns fs==0 on failure.
    inline wav_data wav_read(const std::string& path) {
        wav_data out;
        FILE*    f = std::fopen(path.c_str(), "rb");
        if (!f)
            return out;
        std::vector<uint8_t> buf;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 44) {
            std::fclose(f);
            return out;
        }
        buf.resize(static_cast<size_t>(n));
        if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
            std::fclose(f);
            return out;
        }
        std::fclose(f);
        if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0)
            return out;

        uint16_t fmt = 1, ch = 1, bits = 16;
        uint32_t rate = 0;
        size_t   pos = 12, data_off = 0, data_len = 0;
        while (pos + 8 <= buf.size()) {
            const uint32_t csz = rd32(&buf[pos + 4]);
            if (std::memcmp(&buf[pos], "fmt ", 4) == 0) {
                fmt  = rd16(&buf[pos + 8]);
                ch   = rd16(&buf[pos + 10]);
                rate = rd32(&buf[pos + 12]);
                bits = rd16(&buf[pos + 22]);
            }
            else if (std::memcmp(&buf[pos], "data", 4) == 0) {
                data_off = pos + 8;
                data_len = csz;
                break;
            }
            pos += 8 + csz + (csz & 1);
        }
        if (!data_off || !rate)
            return out;
        out.fs               = static_cast<int>(rate);
        const uint8_t* d     = &buf[data_off];
        const size_t   avail = (data_off + data_len <= buf.size()) ? data_len : buf.size() - data_off;
        if (fmt == 3 && bits == 32) { // float32
            const size_t frames = avail / (4 * ch);
            out.samples.resize(frames);
            for (size_t i = 0; i < frames; ++i) {
                float v;
                std::memcpy(&v, d + (i * ch) * 4, 4);
                out.samples[i] = v;
            }
        }
        else if (bits == 16) { // PCM16
            const size_t frames = avail / (2 * ch);
            out.samples.resize(frames);
            for (size_t i = 0; i < frames; ++i) {
                const int16_t s = static_cast<int16_t>(rd16(d + (i * ch) * 2));
                out.samples[i]  = static_cast<float>(s) / 32768.0f;
            }
        }
        return out;
    }

    inline void wr32(std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(v & 0xff);
        b.push_back((v >> 8) & 0xff);
        b.push_back((v >> 16) & 0xff);
        b.push_back((v >> 24) & 0xff);
    }
    inline void wr16(std::vector<uint8_t>& b, uint16_t v) {
        b.push_back(v & 0xff);
        b.push_back((v >> 8) & 0xff);
    }

    inline bool wav_write(const std::string& path, const std::vector<float>& x, int fs) {
        std::vector<uint8_t> b;
        const uint32_t       data_bytes = static_cast<uint32_t>(x.size() * 2);
        b.insert(b.end(), {'R', 'I', 'F', 'F'});
        wr32(b, 36 + data_bytes);
        b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        wr32(b, 16);
        wr16(b, 1); // PCM
        wr16(b, 1); // mono
        wr32(b, static_cast<uint32_t>(fs));
        wr32(b, static_cast<uint32_t>(fs) * 2); // byte rate
        wr16(b, 2);                             // block align
        wr16(b, 16);                            // bits
        b.insert(b.end(), {'d', 'a', 't', 'a'});
        wr32(b, data_bytes);
        for (float v : x) {
            const float   c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
            const int16_t s = static_cast<int16_t>(c * 32767.0f);
            wr16(b, static_cast<uint16_t>(s));
        }
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
        std::fclose(f);
        return ok;
    }

} // namespace mutap_compare
