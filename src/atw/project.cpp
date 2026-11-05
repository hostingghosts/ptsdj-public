#include "atw/project.h"

#include <array>
#include <cstring>
#include <memory>

#include "atari/insfile.h"
#include "formats/crc32.h"

namespace atw {
namespace {

constexpr char kMagic[4] = {'A', 'T', 'R', 'P'};
constexpr uint8_t kVersion = 10;   // 2: tiaTrack[2] after the track volumes; 3: the ADSR settings between the patterns and the instruments; 4: tiaTune after tiaTrack; 5: funk after tiaTune; 6: a tune flag after each order entry; 7: engineSpeed after funk; 8: sound effects (count, then pattern and track each) after engineSpeed; 9: stepsPerBeat after the sound effects; 10: four grid cells (pattern, kSame or kOut) after each order entry's tune flag

// Buffered file writer that keeps the running CRC.
class Writer final : public atari::InstrumentSink {
public:
    Writer(ot::IStorage& fs, ot::IStorage::Handle h) : fs_(fs), h_(h) {}
    void write(const uint8_t* d, size_t n) override {
        crc_ = ot::crc32Update(crc_, d, n);
        while (n && ok_) {
            size_t k = sizeof buf_ - len_;
            if (k > n) k = n;
            std::memcpy(buf_ + len_, d, k);
            len_ += k;
            d += k;
            n -= k;
            if (len_ == sizeof buf_) flush();
        }
    }
    void u8(uint8_t v) { write(&v, 1); }
    bool finish() {
        const uint32_t c = ot::crc32Final(crc_);
        const uint8_t b[4] = {uint8_t(c), uint8_t(c >> 8), uint8_t(c >> 16), uint8_t(c >> 24)};
        flush();                           // the CRC itself is not part of the CRC: straight to the file
        if (ok_ && fs_.write(h_, b, 4) != 4) ok_ = false;
        return ok_;
    }

private:
    void flush() {
        if (len_ && fs_.write(h_, buf_, uint32_t(len_)) != int32_t(len_)) ok_ = false;
        len_ = 0;
    }
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
    uint8_t buf_[512 + 8];
    size_t len_ = 0;
    uint32_t crc_ = ot::crc32Init();
    bool ok_ = true;
};

// Reads a byte range of an open file, with the position kept here (the storage handle is shared by the passes).
class Reader final : public atari::ByteSource {
public:
    Reader(ot::IStorage& fs, ot::IStorage::Handle h, uint32_t start, uint32_t end) : fs_(fs), h_(h), start_(start), end_(end), pos_(start) {}
    size_t read(char* buffer, size_t max) override {
        if (pos_ >= end_ || !fs_.seek(h_, pos_)) return 0;
        uint32_t n = end_ - pos_;
        if (n > max) n = uint32_t(max);
        const int32_t got = fs_.read(h_, buffer, n);
        if (got <= 0) return 0;
        pos_ += uint32_t(got);
        return size_t(got);
    }
    bool rewind() override {
        pos_ = start_;
        return true;
    }
    uint32_t pos() const { return pos_; }
    bool u8(uint8_t& v) { return read(reinterpret_cast<char*>(&v), 1) == 1; }
    bool bytes(void* d, size_t n) {
        char* p = static_cast<char*>(d);
        while (n) {
            const size_t got = read(p, n);
            if (!got) return false;
            p += got;
            n -= got;
        }
        return true;
    }

private:
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
    uint32_t start_, end_, pos_;
};

uint8_t cellMask(const Cell& c) {
    return uint8_t((c.note ? 1 : 0) | (c.inst ? 2 : 0) | (c.vol ? 4 : 0) | (c.fx[0].type ? 8 : 0) | (c.fx[1].type ? 16 : 0));
}

bool fail(std::string* e, const char* msg) {
    if (e) *e = msg;
    return false;
}

}  // namespace

bool saveProject(ot::IStorage& fs, const Song& s, const char* path, std::string* error) {
    std::string tmp = std::string(path) + ".tmp";
    const ot::IStorage::Handle h = fs.open(tmp.c_str(), ot::IStorage::kWrite);
    if (h < 0) return fail(error, "cannot create the file");
    Writer w(fs, h);
    w.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
    w.u8(kVersion);
    w.u8(uint8_t(s.chip));
    w.u8(uint8_t(s.region));
    w.u8(s.speed);
    w.u8(s.releaseFrames);
    w.u8(s.orderLen);
    w.u8(s.restartPos);
    for (int t = 0; t < kTracks; ++t) w.u8(s.trackVolume[t]);
    w.u8(s.tiaTrack[0]);
    w.u8(s.tiaTrack[1]);
    w.u8(s.tiaTune);
    w.u8(s.funk);
    w.u8(s.engineSpeed);
    w.u8(s.sfxCount);
    for (int i = 0; i < s.sfxCount; ++i) { w.u8(s.sfxPattern[i]); w.u8(s.sfxTrack[i]); }
    w.u8(s.stepsPerBeat);
    w.write(reinterpret_cast<const uint8_t*>(s.name), kNameLen + 1);
    w.write(reinterpret_cast<const uint8_t*>(s.author), kNameLen + 1);
    int used = 0;
    for (int p = 0; p < kPatternCount; ++p) if (!s.pattern(p).empty()) ++used;
    w.u8(uint8_t(used));
    for (int i = 0; i < s.orderLen; ++i) {
        w.u8(s.order[i].pattern);
        w.u8(s.order[i].repeat);
        w.u8(s.order[i].tune);
        for (int t = 0; t < kTracks; ++t) w.u8(s.order[i].cell[t]);
    }
    for (int p = 0; p < kPatternCount; ++p) {
        const Pattern& pat = s.pattern(p);
        if (pat.empty()) continue;
        w.u8(uint8_t(p));
        w.u8(pat.rows);
        for (int r = 0; r < pat.rows; ++r)
            for (int t = 0; t < kTracks; ++t) {
                const Cell& c = pat.cells[r][t];
                const uint8_t m = cellMask(c);
                w.u8(m);
                if (m & 1) w.u8(c.note);
                if (m & 2) w.u8(c.inst);
                if (m & 4) w.u8(c.vol);
                if (m & 8) { w.u8(c.fx[0].type); w.u8(c.fx[0].value); }
                if (m & 16) { w.u8(c.fx[1].type); w.u8(c.fx[1].value); }
            }
    }
    int nAdsr = 0;
    for (const Adsr& a : s.adsr) if (a.on) ++nAdsr;
    w.u8(uint8_t(nAdsr));
    for (int i = 0; i < kInstrumentCount; ++i) {
        const Adsr& a = s.adsr[i];
        if (!a.on) continue;
        w.u8(uint8_t(i));
        w.u8(a.attack); w.u8(a.decay); w.u8(a.sustain); w.u8(a.peak); w.u8(a.dist); w.u8(a.curve);
    }
    atari::InstrumentEditor ed;
    ed.bind(const_cast<atari::PokeyInstrument*>(s.pokey), const_cast<atari::TiaInstrument*>(s.tia));
    atari::writeInstruments(ed, w);
    const bool ok = w.finish();
    fs.close(h);
    if (!ok) {
        fs.remove(tmp.c_str());
        return fail(error, "write failed (card full?)");
    }
    fs.remove(path);
    if (!fs.rename(tmp.c_str(), path)) return fail(error, "cannot replace the old file");
    return true;
}

bool loadProject(ot::IStorage& fs, Song& s, const char* path, std::string* error) {
    const ot::IStorage::Handle h = fs.open(path, ot::IStorage::kRead);
    if (h < 0) return fail(error, "cannot open the file");
    struct Closer { ot::IStorage& f; ot::IStorage::Handle h; ~Closer() { f.close(h); } } closer{fs, h};
    const int32_t size = fs.size(h);
    if (size < 4 + 1 + 4 || size > 4 * 1024 * 1024) return fail(error, "not a project file");
    const uint32_t end = uint32_t(size) - 4;

    // 1. CRC over everything before the last four bytes.
    {
        Reader r(fs, h, 0, end);
        uint32_t crc = ot::crc32Init();
        char buf[256];
        for (size_t n; (n = r.read(buf, sizeof buf)) > 0;) crc = ot::crc32Update(crc, reinterpret_cast<const uint8_t*>(buf), n);
        uint8_t tail[4];
        if (!fs.seek(h, end) || fs.read(h, tail, 4) != 4) return fail(error, "cannot read the file");
        const uint32_t stored = uint32_t(tail[0]) | uint32_t(tail[1]) << 8 | uint32_t(tail[2]) << 16 | uint32_t(tail[3]) << 24;
        if (ot::crc32Final(crc) != stored) return fail(error, "the file is damaged");
    }

    // 2. Parse: first checking only, then (second round) applying into a scratch song, so every value is checked before `s` changes.
    Reader r(fs, h, 0, end);
    char magic[4];
    uint8_t ver, chip, region, speed, release, orderLen, restart, vol[kTracks];
    char name[kNameLen + 1], author[kNameLen + 1];
    uint8_t nPat;
    if (!r.bytes(magic, 4) || std::memcmp(magic, kMagic, 4)) return fail(error, "not an Atari project");
    if (!r.u8(ver) || ver < 1 || ver > kVersion) return fail(error, "a newer project version");
    if (!r.u8(chip) || !r.u8(region) || !r.u8(speed) || !r.u8(release) || !r.u8(orderLen) || !r.u8(restart)) return fail(error, "file too short");
    for (int t = 0; t < kTracks; ++t) if (!r.u8(vol[t]) || vol[t] > 100) return fail(error, "bad track volume");
    uint8_t tia[2] = {0, 1};
    if (ver >= 2 && (!r.u8(tia[0]) || !r.u8(tia[1]) || tia[0] > 3 || tia[1] > 3 || tia[0] == tia[1])) return fail(error, "bad TIA tracks");
    uint8_t tune = 0;
    if (ver >= 4 && (!r.u8(tune) || tune > 1)) return fail(error, "bad TIA tuning");
    uint8_t funk = 0;
    if (ver >= 5 && !r.u8(funk)) return fail(error, "file too short");
    uint8_t engine = 1;
    if (ver >= 7 && (!r.u8(engine) || (engine != 1 && engine != 2 && engine != 4))) return fail(error, "bad engine speed");
    uint8_t sfxN = 0, sfxP[16] = {}, sfxT[16] = {};
    if (ver >= 8) {
        if (!r.u8(sfxN) || sfxN > 16) return fail(error, "bad sound effects");
        for (int i = 0; i < sfxN; ++i)
            if (!r.u8(sfxP[i]) || !r.u8(sfxT[i]) || sfxP[i] >= kPatternCount || sfxT[i] >= kTracks) return fail(error, "bad sound effects");
    }
    uint8_t beat = 4;
    if (ver >= 9 && (!r.u8(beat) || beat < 1 || beat > 64 || (beat & (beat - 1)))) return fail(error, "bad steps per beat");
    if (!r.bytes(name, sizeof name) || !r.bytes(author, sizeof author) || !r.u8(nPat)) return fail(error, "file too short");
    if (chip > 1 || region > 1 || speed < 1 || release > 15 || orderLen < 1 || restart >= orderLen || nPat > kPatternCount) return fail(error, "bad header");
    name[kNameLen] = author[kNameLen] = 0;
    OrderEntry order[kOrderLen];
    for (int i = 0; i < orderLen; ++i) {
        if (!r.u8(order[i].pattern) || !r.u8(order[i].repeat)) return fail(error, "file too short");
        if (ver >= 6) { if (!r.u8(order[i].tune) || order[i].tune > 1) return fail(error, "bad order list"); }
        if (ver >= 10) {
            for (int t = 0; t < kTracks; ++t) {
                if (!r.u8(order[i].cell[t])) return fail(error, "file too short");
                if (order[i].cell[t] >= kPatternCount && order[i].cell[t] != OrderEntry::kSame && order[i].cell[t] != OrderEntry::kOut) return fail(error, "bad order list");
            }
        }
        if (order[i].pattern >= kPatternCount || order[i].repeat < 1) return fail(error, "bad order list");
    }
    std::array<Adsr, kInstrumentCount> adsrLoaded;
    // Patterns go straight into the song's pattern memory only after a dry run over the same bytes.
    const uint32_t patStart = r.pos();
    for (int pass = 0; pass < 2; ++pass) {
        Reader pr(fs, h, patStart, end);
        if (pass == 1)
            for (int p = 0; p < kPatternCount; ++p) s.pattern(p).clear();
        int prev = -1;
        for (int n = 0; n < nPat; ++n) {
            uint8_t idx, rows;
            if (!pr.u8(idx) || !pr.u8(rows)) return fail(error, "file too short");
            if (idx >= kPatternCount || int(idx) <= prev || rows < 1 || rows > kMaxRows) return fail(error, "bad pattern");
            prev = idx;
            if (pass == 1) s.pattern(idx).rows = rows;
            for (int row = 0; row < rows; ++row)
                for (int t = 0; t < kTracks; ++t) {
                    uint8_t m;
                    Cell c;
                    if (!pr.u8(m) || m > 31) return fail(error, "bad cell");
                    if (m & 1) if (!pr.u8(c.note) || c.note > note::kOff || !c.note) return fail(error, "bad note");
                    if (m & 2) if (!pr.u8(c.inst) || c.inst > kInstrumentCount || !c.inst) return fail(error, "bad instrument");
                    if (m & 4) if (!pr.u8(c.vol) || c.vol > 16 || !c.vol) return fail(error, "bad volume");
                    for (int f = 0; f < 2; ++f)
                        if (m & (8 << f)) {
                            if (!pr.u8(c.fx[f].type) || !pr.u8(c.fx[f].value) || !c.fx[f].type || !fxLetter(c.fx[f].type)) return fail(error, "bad effect");
                        }
                    if (pass == 1) s.pattern(idx).cells[row][t] = c;
                }
        }
        if (pass == 0) {
            adsrLoaded = {};
            uint8_t na = 0;
            if (ver >= 3) {
                if (!pr.u8(na) || na > kInstrumentCount) return fail(error, "bad ADSR block");
                for (int k = 0; k < na; ++k) {
                    uint8_t slot, v[6];
                    if (!pr.u8(slot) || slot >= kInstrumentCount || !pr.bytes(v, 6) || v[0] > 15 || v[1] > 15 || v[2] > 15 || v[3] < 1 || v[3] > 15 || v[4] > 14 || (v[4] & 1) || v[5] > 2)
                        return fail(error, "bad ADSR block");
                    Adsr& a = adsrLoaded[slot];
                    a.on = true; a.attack = v[0]; a.decay = v[1]; a.sustain = v[2]; a.peak = v[3]; a.dist = v[4]; a.curve = v[5];
                }
            }
            // The instruments follow; read them into scratch arrays so a bad block cannot leave half a song.
            Reader ir(fs, h, pr.pos(), end);
            std::unique_ptr<atari::PokeyInstrument[]> pk(new atari::PokeyInstrument[kInstrumentCount]);
            std::unique_ptr<atari::TiaInstrument[]> ti(new atari::TiaInstrument[kInstrumentCount]);
            atari::InstrumentEditor ed;
            ed.bind(pk.get(), ti.get());
            std::string e;
            if (!atari::readInstruments(ir, ed, &e)) return fail(error, "bad instruments");
            for (int i = 0; i < kInstrumentCount; ++i) {
                s.pokey[i] = std::move(pk[i]);
                s.tia[i] = std::move(ti[i]);
            }
        }
    }
    s.chip = chip ? atari::Chip::Tia : atari::Chip::Pokey;
    s.region = region ? atari::Region::PAL : atari::Region::NTSC;
    s.speed = speed;
    s.releaseFrames = release;
    s.orderLen = orderLen;
    s.restartPos = restart;
    std::memcpy(s.name, name, sizeof name);
    std::memcpy(s.author, author, sizeof author);
    for (int t = 0; t < kTracks; ++t) s.trackVolume[t] = vol[t];
    s.tiaTrack[0] = tia[0];
    s.tiaTrack[1] = tia[1];
    s.tiaTune = tune;
    s.funk = funk;
    s.engineSpeed = engine;
    s.stepsPerBeat = beat;
    s.sfxCount = sfxN;
    for (int i = 0; i < 16; ++i) { s.sfxPattern[i] = sfxP[i]; s.sfxTrack[i] = sfxT[i]; }
    for (int i = 0; i < orderLen; ++i) s.order[i] = order[i];
    for (int i = 0; i < kInstrumentCount; ++i) s.adsr[i] = adsrLoaded[size_t(i)];
    return true;
}

}  // namespace atw
