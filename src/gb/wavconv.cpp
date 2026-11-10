#include "gb/wavconv.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gb/kitbank.h"

namespace gb {

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}  // namespace

bool wavOpen(WavReader& r, WavInfo& w) {
    uint8_t h[12];
    if (r.read(0, h, 12) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) return false;
    uint32_t pos = 12;
    bool fmt = false;
    for (int guard = 0; guard < 64; ++guard) {
        uint8_t c[8];
        if (r.read(pos, c, 8) != 8) return false;
        const uint32_t size = le32(c + 4);
        if (!std::memcmp(c, "fmt ", 4)) {
            uint8_t f[16];
            if (size < 16 || r.read(pos + 8, f, 16) != 16) return false;
            const int tag = le16(f);
            if (tag != 1 && tag != 0xFFFE) return false;                    // PCM (or extensible, which carries PCM here)
            w.channels = le16(f + 2);
            w.rate = int(le32(f + 4));
            w.bits = le16(f + 14);
            fmt = w.channels >= 1 && w.channels <= 2 && w.rate >= 1000 && (w.bits == 8 || w.bits == 16 || w.bits == 24);
            if (!fmt) return false;
        } else if (!std::memcmp(c, "data", 4)) {
            if (!fmt) return false;
            w.dataOff = pos + 8;
            w.dataBytes = size;
            return true;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

int wavToNibbles(WavReader& r, const WavInfo& w, int16_t* tmp, int tmpCap, uint8_t* out, int outCap, bool& cut) {
    cut = false;
    const int bytesPer = w.bits / 8, frame = bytesPer * w.channels;
    const uint32_t frames = w.dataBytes / uint32_t(frame);
    const double ratio = double(w.rate) / kKitRate;                          // input samples per output sample
    // area averaging: output k covers the input interval [k * ratio, (k + 1) * ratio)
    int nOut = 0;
    double acc = 0, edge = ratio;
    uint32_t i = 0;
    uint8_t buf[2048];
    const int perRead = int(sizeof buf) / frame;
    while (i < frames && nOut < tmpCap) {
        const int got = r.read(w.dataOff + i * uint32_t(frame), buf, std::min<uint32_t>(uint32_t(perRead), frames - i) * uint32_t(frame)) / frame;
        if (got <= 0) break;
        for (int k = 0; k < got && nOut < tmpCap; ++k, ++i) {
            double x = 0;
            for (int c = 0; c < w.channels; ++c) {
                const uint8_t* p = buf + k * frame + c * bytesPer;
                x += w.bits == 8 ? (int(p[0]) - 128) / 128.0 : w.bits == 16 ? int16_t(le16(p)) / 32768.0 : (int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8) / 8388608.0;
            }
            x /= w.channels;
            double from = double(i);
            while (from < double(i) + 1 && nOut < tmpCap) {                    // spread this input sample over the output cells it touches
                const double to = std::min(double(i) + 1, edge);
                acc += x * (to - from);
                from = to;
                if (to >= edge) { tmp[nOut++] = int16_t(std::lround(std::clamp(acc / ratio, -1.0, 1.0) * 32767.0)); acc = 0; edge += ratio; }
            }
        }
    }
    cut = i < frames;
    if (nOut < 32) return 0;
    // remove the average level, then scale the loudest peak to the full range (but never boost near silence by more than 16 times)
    long long sum = 0;
    for (int k = 0; k < nOut; ++k) sum += tmp[k];
    const int mean = int(sum / nOut);
    int peak = 1;
    for (int k = 0; k < nOut; ++k) peak = std::max(peak, std::abs(tmp[k] - mean));
    const double gain = std::min(32767.0 / peak, 16.0);
    const int bytes = (nOut + 31) / 32 * 16;                                    // whole chunks
    if (bytes > outCap) return 0;
    for (int b = 0; b < bytes; ++b) {
        int nib[2];
        for (int h = 0; h < 2; ++h) {
            const int k = b * 2 + h;
            const double x = k < nOut ? (tmp[k] - mean) * gain / 32768.0 : 0.0;
            nib[h] = std::clamp(int(std::floor(8.0 + x * 7.5 + 0.5)), 0, 15);
        }
        out[b] = uint8_t(nib[0] << 4 | nib[1]);
    }
    return bytes;
}

}  // namespace gb
