// WAV files to LSDJ kit samples: PCM 8, 16 or 24 bit, any rate, mono or stereo, resampled to the kit rate and cut to 4 bits.
#pragma once

#include <cstddef>
#include <cstdint>

namespace gb {

struct WavInfo {
    int rate = 0, channels = 0, bits = 0;
    uint32_t dataOff = 0, dataBytes = 0;
};

// Random access to the file (a card file in the firmware, memory in the tests): bytes read from `off`, fewer at the end of the file.
struct WavReader {
    virtual ~WavReader() = default;
    virtual int read(uint32_t off, void* dst, int n) = 0;
};

// Reads the RIFF header and finds the fmt and data chunks. False if it is not a PCM WAV this code can read.
bool wavOpen(WavReader& r, WavInfo& w);

// Converts the audio to packed 4-bit samples at kKitRate (high nibble first), normalised to the loudest peak. `tmp` holds one 16-bit sample per output
// sample (tmpCap of them, the most that fits a kit); the result is whole chunks of 16 bytes. Returns the byte count (0 if empty). `cut` says the file
// was longer than fits and was cut.
int wavToNibbles(WavReader& r, const WavInfo& w, int16_t* tmp, int tmpCap, uint8_t* out, int outCap, bool& cut);

// Seconds of audio in n packed bytes.
inline float nibbleSeconds(int bytes) { return float(bytes) * 2.f / 11460.f; }
// The device's printf has no float support (newlib nano), so times are shown from whole hundredths of a second.
inline int secHundredths(int bytes) { return int(int64_t(bytes) * 200 / 11460); }

}  // namespace gb
