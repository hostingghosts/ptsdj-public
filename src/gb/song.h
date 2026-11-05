// LSDJ 9.4.2 song: the 32 KB working memory itself (layout derived with tools/oracle, docs/LSDJ_FORMAT.md).
// Editing code reads and writes this image; .sav export is the image plus an empty file table.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gb {

namespace off {
constexpr uint16_t kNotes = 0x0000;        // 255 phrases x 16, 0 = empty
constexpr uint16_t kBookmarks = 0x0FF0;    // 16, 0xFF = none
constexpr uint16_t kGroove = 0x1090;       // 32 x 16 ticks
constexpr uint16_t kSong = 0x1290;         // 256 rows x 4 channels (row-major), chain id, 0xFF = empty
constexpr uint16_t kTblEnv = 0x1690;       // 32 x 16 (envelope column)
constexpr uint16_t kInstrAlloc = 0x2040;   // 64 flags
constexpr uint16_t kChainPhrase = 0x2080;  // 128 x 16 phrase ids, 0xFF = empty
constexpr uint16_t kChainTrans = 0x2880;   // 128 x 16
constexpr uint16_t kInstr = 0x3080;        // 64 x 16
constexpr uint16_t kTblTrans = 0x3480;     // 32 x 16
constexpr uint16_t kTblCmd1 = 0x3680;
constexpr uint16_t kTblVal1 = 0x3880;
constexpr uint16_t kTblCmd2 = 0x3A80;
constexpr uint16_t kTblVal2 = 0x3C80;
constexpr uint16_t kPhraseAlloc = 0x3E82;  // 255 bits
constexpr uint16_t kChainAlloc = 0x3EA2;   // 128 bits
constexpr uint16_t kSynth = 0x3EB2;        // 16 x 16 synth parameters
constexpr uint16_t kTempo = 0x3FB4;        // project: tempo (default 0x80)
constexpr uint16_t kTranspose = 0x3FB5;    // project: transpose
constexpr uint16_t kPhraseCmd = 0x4000;    // 255 x 16
constexpr uint16_t kPhraseVal = 0x4FF0;    // 255 x 16
constexpr uint16_t kWave = 0x6000;         // 256 frames x 16 bytes (4-bit samples, 2 per byte)
constexpr uint16_t kPhraseInstr = 0x7000;  // 255 x 16, 0xFF = none
}  // namespace off

constexpr size_t kMemSize = 0x8000;
constexpr size_t kSavSize = 0x20000;
constexpr int kPhrases = 255, kChains = 128, kInstruments = 64, kTables = 32, kSynths = 16, kGrooves = 32, kSongRows = 256;

class Song {
public:
    Song() { reset(); }
    void reset();                                   // a fresh song, as LSDJ makes after a memory reset

    uint8_t* mem() { return m_; }
    const uint8_t* mem() const { return m_; }
    uint8_t& at(uint16_t o) { return m_[o]; }
    uint8_t at(uint16_t o) const { return m_[o]; }

    uint8_t& note(int phrase, int step) { return m_[off::kNotes + phrase * 16 + step]; }
    uint8_t& phraseInstr(int phrase, int step) { return m_[off::kPhraseInstr + phrase * 16 + step]; }
    uint8_t& phraseCmd(int phrase, int step) { return m_[off::kPhraseCmd + phrase * 16 + step]; }
    uint8_t& phraseVal(int phrase, int step) { return m_[off::kPhraseVal + phrase * 16 + step]; }
    uint8_t& chainPhrase(int chain, int step) { return m_[off::kChainPhrase + chain * 16 + step]; }
    uint8_t& chainTrans(int chain, int step) { return m_[off::kChainTrans + chain * 16 + step]; }
    uint8_t& songCell(int row, int channel) { return m_[off::kSong + row * 4 + channel]; }
    uint8_t* instrument(int i) { return m_ + off::kInstr + i * 16; }
    uint8_t* groove(int g) { return m_ + off::kGroove + g * 16; }
    uint8_t* synth(int s) { return m_ + off::kSynth + s * 16; }
    uint8_t* wave(int frame) { return m_ + off::kWave + frame * 16; }

    // Allocation flags LSDJ keeps for "used" phrases / chains / instruments; call after writing one.
    void markPhrase(int p) { m_[off::kPhraseAlloc + p / 8] |= uint8_t(1u << (p & 7)); }
    void markChain(int c) { m_[off::kChainAlloc + c / 8] |= uint8_t(1u << (c & 7)); }
    void markInstrument(int i) { m_[off::kInstrAlloc + i] = 1; }

    // 128 KB .sav with this song as the working memory and an empty file table.
    void writeSav(uint8_t* out /* kSavSize */) const;
    bool readSav(const uint8_t* in, size_t n);      // working memory only; false if the 'rb' markers are missing

private:
    uint8_t m_[kMemSize];
};

}  // namespace gb
