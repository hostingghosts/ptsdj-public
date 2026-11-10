#include "atw/app.h"
#include "gb/bank.h"
#include "gb/kitbank.h"
#include "gb/wavconv.h"
#include "atw/chime_pcm.h"

#include <algorithm>
#include "gb/upgrade.h"
#include <cstdio>
#include <new>
#include <cstring>
#include <strings.h>

namespace atw {

namespace {
void fillThunk(int16_t* out, uint32_t frames, void* ctx) { static_cast<App*>(ctx)->fill(out, frames); }
void fillSilence(int16_t* out, uint32_t frames, void*) {
    std::memset(out, 0, size_t(frames) * ot::IAudioOut::kChannels * sizeof(int16_t));
}
}  // namespace

void App::fill(int16_t* out, uint32_t frames) {
    uint32_t tail = ringTail_.load(std::memory_order_relaxed);
    const uint32_t head = ringHead_.load(std::memory_order_acquire);
    if (flush_.exchange(false, std::memory_order_acq_rel)) {                        // a new playback or note starts: drop the silence queued before it
        const uint32_t to = flushTo_.load(std::memory_order_relaxed);
        if (int32_t(to - tail) > 0) tail = to;
    }
    uint32_t n = head - tail;
    if (n > frames) n = frames;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t k = ((tail + i) & (kRingFrames - 1)) * 2;
        out[2 * i] = ring_[k];
        out[2 * i + 1] = ring_[k + 1];
    }
    if (n < frames) {
        std::memset(out + 2 * n, 0, size_t(frames - n) * 2 * sizeof(int16_t));
        if (dual.running()) ringLate_.fetch_add(1, std::memory_order_relaxed);
    }
    ringTail_.store(tail + n, std::memory_order_release);
}

void App::pump() {
    if (exporting_ || !p_.audio) return;
    const uint32_t rate = p_.sampleRateMilliHz / 1000;
    int16_t buf[kChunk * 2];
    if (dual.live() && !liveWas_) flushRing();           // a pad starts a note: drop the queued silence so it sounds at once
    liveWas_ = dual.live();
    for (int pass = 0; pass < 16; ++pass) {
        const bool run = chimeOn_ || dual.running() || dual.live();
        const uint32_t level = ringHead_.load(std::memory_order_relaxed) - ringTail_.load(std::memory_order_acquire);
        if (level + kChunk > (run ? kRunTarget : kIdleTarget)) break;
        const uint32_t c0 = p_.cycleCounter ? p_.cycleCounter() : 0;
        if (chimeOn_) renderChime(buf, int(kChunk), rate);
        else if (run) dual.render(buf, int(kChunk), int(rate)); else std::memset(buf, 0, sizeof buf);
        if (p_.cycleCounter && run && !chimeOn_) { const uint32_t c = p_.cycleCounter() - c0; if (c > peakC_) peakC_ = c; }
        const uint32_t head = ringHead_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kChunk; ++i) {
            const uint32_t k = ((head + i) & (kRingFrames - 1)) * 2;
            ring_[k] = buf[2 * i];
            ring_[k + 1] = buf[2 * i + 1];
        }
        ringHead_.store(head + kChunk, std::memory_order_release);
    }
}

bool App::init(const Platform& p) {
    p_ = p;
    transport_.app = this;
    lsdj.setTransport(&transport_, 0);
    lsdjB.setTransport(&transport_, 1);
    synthGen.setCounter(p_.cycleCounter);
    lsdj.setSynthGen(&synthGen);
    lsdjB.setSynthGen(&synthGen);
    if (!p_.display || !p_.input || !p_.clock) return false;
    if (p_.audio) {
        p_.audio->setCallback(&fillThunk, this);
        p_.audio->start();
    }
    ui.init(*this);
    return true;
}

// Scratch for song files and decoding lives in SDRAM after the 1 MB ROM image: 64 KB of file, then one song. (A song on the stack is 32 KB
// of the ~59 KB the device has free, which is not safe.)
uint8_t* App::fileBuf() const { return p_.bigMem && p_.bigMemSize >= kScratchOffset + kScratchBytes ? p_.bigMem + kScratchOffset : nullptr; }

// A card file as a BankIO: reads and writes at byte offsets (the card may return short counts).
namespace {
struct CardIO final : gb::BankIO {
    ot::IStorage* st;
    ot::IStorage::Handle h;
    CardIO(ot::IStorage* s, const char* path, ot::IStorage::OpenMode m) : st(s), h(s ? s->open(path, m) : -1) {}
    ~CardIO() override { if (h >= 0) st->close(h); }
    bool ok() const { return h >= 0; }
    bool read(uint32_t off, void* dst, uint32_t n) override {
        if (h < 0 || !st->seek(h, off)) return false;
        uint8_t* p = static_cast<uint8_t*>(dst);
        while (n) { const int32_t r = st->read(h, p, n); if (r <= 0) return false; p += r; n -= uint32_t(r); }
        return true;
    }
    bool write(uint32_t off, const void* src, uint32_t n) override {
        if (h < 0 || !st->seek(h, off)) return false;
        const uint8_t* p = static_cast<const uint8_t*>(src);
        while (n) { const int32_t r = st->write(h, p, n); if (r <= 0) return false; p += r; n -= uint32_t(r); }
        return true;
    }
};
}  // namespace

bool App::installSong(size_t n, int console, const char* nm) {
    uint8_t* buf = fileBuf();
    dual.stop();
    gb::Song& fresh = *new (buf + kFileBytes) gb::Song;
    if (!gb::loadLsdsng(fresh, buf, n)) return false;
    if (!gb::upgradeSong(fresh)) return false;       // songs saved by older LSDJ versions
    std::memcpy((console ? songB : song).mem(), fresh.mem(), gb::kMemSize);
    char name[9] = {};
    for (int i = 0; i < 8 && nm[i] && nm[i] != '.'; ++i) name[i] = nm[i];
    (console ? lsdjB : lsdj).setName(strncasecmp(name, "AUTOLOAD", 8) == 0 ? "" : name);
    return true;
}

bool App::loadSong(const char* path, int console) {
    if (!p_.storage || !fileBuf()) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    uint8_t* buf = fileBuf();
    const int32_t n = p_.storage->read(h, buf, kFileBytes);
    p_.storage->close(h);
    if (n <= 0) return false;
    const char* stem = std::strrchr(path, '/');
    return installSong(size_t(n), console, stem ? stem + 1 : path);
}

// The name a song is filed under in the bank: the project name, or SONGA / SONGB for an unnamed one.
static void bankName(const gb::Lsdj& l, int console, char* out) {
    const char* nm = l.name();
    if (!nm[0]) nm = console ? "SONGB" : "SONGA";
    std::snprintf(out, 9, "%.8s", nm);
}

bool App::saveToBank(int console) {
    error_ = nullptr;
    if (!p_.storage || !fileBuf()) return false;
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kSaveDir);
    const bool had = p_.storage->exists(bankPath(console));
    CardIO io(p_.storage, bankPath(console), ot::IStorage::kReadWrite);
    if (!io.ok()) return false;
    gb::Bank bank;
    if (!(had && bank.load(io))) {
        if (!gb::Bank::format(io) || !bank.load(io)) return false;     // a new bank, or a file that was not an LSDJ save
    }
    char name[9];
    bankName(console ? lsdjB : lsdj, console, name);
    int slot = bank.find(name);
    if (slot < 0) slot = bank.freeSlot();
    if (slot < 0) { error_ = "BANK FULL: 32 SONGS"; return false; }
    uint8_t* buf = fileBuf();
    const gb::Song& sg = console ? songB : song;
    const size_t n = gb::saveLsdsng(sg, name, 1, buf);
    const int old = bank.used(slot) ? bank.blocksOf(slot) : 0;
    if (!bank.store(io, slot, buf, n)) { error_ = int((n - 9) / 512) > bank.freeBlocks() + old ? "BANK FULL: NO SPACE" : nullptr; return false; }
    return gb::Bank::writeWorking(io, sg);
}

bool App::newSong(int console, const char* name) {
    dual.stop();
    (console ? songB : song).reset();
    gb::Lsdj& l = console ? lsdjB : lsdj;
    l.setName(name);
    l.newSongView();
    return true;
}

// Each console starts with the song it saved last (the bank's active slot) and empty if it never saved one.
bool App::loadLastSaved(int console) {
    if (!p_.storage || !p_.storage->exists(bankPath(console))) return false;
    CardIO io(p_.storage, bankPath(console), ot::IStorage::kRead);
    gb::Bank bank;
    if (!io.ok() || !bank.load(io) || !bank.used(bank.active)) return false;
    return loadFromBank(bank.active, console);
}

bool App::loadFromBank(int slot, int console) {
    if (!p_.storage || !fileBuf() || !p_.storage->exists(bankPath(console))) return false;
    CardIO io(p_.storage, bankPath(console), ot::IStorage::kRead);
    gb::Bank bank;
    if (!io.ok() || !bank.load(io) || !bank.used(slot)) return false;
    uint8_t* buf = fileBuf();
    const size_t n = bank.fetch(io, slot, buf, kFileBytes);
    return n && installSong(n, console, bank.name[slot]);
}

bool App::saveSong(const char* path, const char* name, int console) {
    if (!p_.storage || !fileBuf()) return false;
    uint8_t* buf = fileBuf();
    const size_t n = gb::saveLsdsng(console ? songB : song, name, 1, buf);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    const bool ok = p_.storage->write(h, buf, uint32_t(n)) == int32_t(n);
    p_.storage->close(h);
    return ok;
}

// The ROM image is read from the card in 64 KB steps (boot runs them one per main-loop pass so the screen shows progress).
bool App::romStep() {
    if (!p_.storage || !p_.bigMem || p_.bigMemSize < kScratchOffset) return true;
    if (romH_ < 0) {
        romH_ = p_.storage->open(kRomPath, ot::IStorage::kRead);
        if (romH_ < 0) return true;
        romGot_ = 0;
    }
    while (romGot_ < kRomBytes) {
        const int32_t n = p_.storage->read(romH_, p_.bigMem + romGot_, std::min<uint32_t>(32768, kRomBytes - romGot_));
        if (n <= 0) break;
        romGot_ += uint32_t(n);
        if (romGot_ % (64 * 1024) == 0 && romGot_ < kRomBytes) return false;   // more to do next pass
    }
    p_.storage->close(romH_);
    romH_ = -1;
    if (romGot_ >= (1u << 19)) {
        kits.setRom(p_.bigMem, romGot_);
        kitsB.setRom(p_.bigMem, romGot_);
        synthGen.setRom(p_.bigMem, romGot_);
        player.setKits(&kits);
        playerB.setKits(&kitsB);
    }
    return true;
}

// .kit files: <hex slot>*.kit, 16 KB each, replace that kit slot. Files in GB/Kits itself serve both consoles; GB/Kits/A and GB/Kits/B hold each console's
// own kits (the ones made on the Tracker). The listing is taken first, then one file is read per step.
struct KitScan { App* a; int console; };
int App::scanKitDir(const char* dir, int console) {
    KitScan ctx{this, console};
    char dirPath[40];
    std::snprintf(dirPath, sizeof dirPath, "%s", dir);
    struct Local {
        static bool each(const ot::IStorage::DirEntry& e, void* c) {
            KitScan* k = static_cast<KitScan*>(c);
            App* a = k->a;
            const size_t n = std::strlen(e.name);
            auto hex = [](char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1; };
            if (e.isDir || n < 6 || n > 36 || strcasecmp(e.name + n - 4, ".kit") != 0 || e.size != gb::kKitBankSize) return true;
            const int hi = hex(e.name[0]), lo = hex(e.name[1]);
            if (hi < 0 || lo < 0 || hi * 16 + lo > 63 || a->kitFiles_ >= kMaxKitFiles) return a->kitFiles_ < kMaxKitFiles;
            KitFile& f = a->kitFile_[a->kitFiles_++];
            std::snprintf(f.path, sizeof f.path, "%s/%s", a->scanDir_, e.name);
            f.idx = uint8_t(hi * 16 + lo);
            f.console = uint8_t(k->console);
            return true;
        }
    };
    scanDir_ = dirPath;
    const int before = kitFiles_;
    p_.storage->listDir(dirPath, &Local::each, &ctx);
    return kitFiles_ - before;
}

void App::scanKitFiles() {
    kitFiles_ = 0;
    if (!p_.storage) return;
    scanKitDir(kKitDir, 2);
    scanKitDir("GB/Kits/A", 0);
    scanKitDir("GB/Kits/B", 1);
}

// A loaded kit file serves the consoles it is for.
void App::applyKit(int i) {
    uint8_t* base = p_.bigMem + kKitOffset;
    const KitFile& f = kitFile_[i];
    if (f.console == 0 || f.console == 2) kits.setOverride(f.idx, base + size_t(i) * gb::kKitBankSize);
    if (f.console == 1 || f.console == 2) kitsB.setOverride(f.idx, base + size_t(i) * gb::kKitBankSize);
    player.setKits(&kits);
    playerB.setKits(&kitsB);
}

int App::kitFileStep(int i) {
    uint8_t* base = p_.bigMem && p_.bigMemSize >= kKitOffset + kMaxKitFiles * gb::kKitBankSize ? p_.bigMem + kKitOffset : nullptr;
    if (!base || i < 0 || i >= kitFiles_) return 0;
    const ot::IStorage::Handle h = p_.storage->open(kitFile_[i].path, ot::IStorage::kRead);
    if (h < 0) return 0;
    uint8_t* dst = base + size_t(i) * gb::kKitBankSize;
    uint32_t got = 0;
    while (got < gb::kKitBankSize) {
        const int32_t n = p_.storage->read(h, dst + got, std::min<uint32_t>(4096, gb::kKitBankSize - got));
        if (n <= 0) break;
        got += uint32_t(n);
    }
    p_.storage->close(h);
    if (got != gb::kKitBankSize || dst[0] != 0x60 || dst[1] != 0x40) { kitFile_[i].console = 3; return 0; }   // not a kit bank (the offset table starts at 0x4060)
    applyKit(i);
    ++kitsLoaded_;
    return 1;
}

// The kit being edited: a copy of what the console plays for that slot (an empty kit when there is none).
// The file beside a kit file that remembers which WAV each sample came from ("<index> <file>" per line).
void App::srcPath(int console, int slot, char* out, size_t n) const {
    out[0] = 0;
    for (int i = 0; i < kitFiles_; ++i)
        if (kitFile_[i].console == console && kitFile_[i].idx == slot) {
            std::snprintf(out, n, "%s", kitFile_[i].path);
            const size_t l = std::strlen(out);
            if (l > 4) std::strcpy(out + l - 4, ".src");
        }
}

bool App::kitOpen(int console, int slot) {
    uint8_t* w = workBuf(console);
    if (!w || slot < 0 || slot > 63) return false;
    for (auto& s : kitSrc_[console ? 1 : 0]) s[0] = 0;
    {
        char sp[72];
        srcPath(console, slot, sp, sizeof sp);
        if (sp[0] && p_.storage && p_.storage->exists(sp)) {
            const ot::IStorage::Handle h = p_.storage->open(sp, ot::IStorage::kRead);
            if (h >= 0) {
                char buf[640] = {};
                const int32_t n = p_.storage->read(h, buf, sizeof buf - 1);
                p_.storage->close(h);
                for (char* line = n > 0 ? buf : nullptr; line && *line;) {
                    char* nl = std::strchr(line, '\n');
                    if (nl) *nl = 0;
                    int idx = -1;
                    char name[28] = {};
                    if (std::sscanf(line, "%d %27s", &idx, name) == 2 && idx >= 0 && idx < 15) std::snprintf(kitSrc_[console ? 1 : 0][idx], sizeof kitSrc_[0][0], "%s", name);
                    line = nl ? nl + 1 : nullptr;
                }
            }
        }
    }
    const gb::Kit k = (console ? kitsB : kits).kit(slot);
    if (k.data) { std::memcpy(w, k.data, gb::kKitBankSize); return true; }
    gb::KitBank::clear(w);
    return false;
}

// Writes the edit buffer as GB/Kits/<A|B>/<slot>_<kit name>.kit and makes it the kit the console plays. An empty kit erases the file.
bool App::kitSave(int console, int slot) {
    error_ = nullptr;
    uint8_t* w = workBuf(console);
    if (!w || !p_.storage || slot < gb::kKitFirstUserSlot || slot > gb::kKitLastUserSlot) { error_ = "KITS 1A TO 37 ONLY"; return false; }
    int at = -1;
    for (int i = 0; i < kitFiles_; ++i) if (kitFile_[i].console == console && kitFile_[i].idx == slot) at = i;
    if (!gb::KitBank::hasSamples(w)) {                                   // an empty kit: forget the file
        if (at >= 0) {
            char sp[72];
            srcPath(console, slot, sp, sizeof sp);
            p_.storage->remove(kitFile_[at].path);
            if (sp[0]) p_.storage->remove(sp);
            kitFile_[at].console = 3;
            (console ? kitsB : kits).setOverride(slot, nullptr);
        }
        for (auto& sr : kitSrc_[console ? 1 : 0]) sr[0] = 0;
        return true;
    }
    char nm[9], base[12] = {};
    gb::KitBank::kitName(w, nm);
    int n = 0;
    for (int i = 0; i < 8 && nm[i]; ++i) if (nm[i] != ' ' || n) base[n++] = nm[i] == ' ' ? '_' : nm[i];
    while (n > 0 && base[n - 1] == '_') base[--n] = 0;
    if (!n) std::snprintf(base, sizeof base, "KIT");
    const char* dir = console ? "GB/Kits/B" : "GB/Kits/A";
    char path[60];
    std::snprintf(path, sizeof path, "%s/%02X_%s.kit", dir, unsigned(slot), base);
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kKitDir);
    p_.storage->mkdir(dir);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    const bool ok = p_.storage->write(h, w, gb::kKitBankSize) == int32_t(gb::kKitBankSize);
    p_.storage->close(h);
    if (!ok) return false;
    if (at >= 0 && std::strcmp(kitFile_[at].path, path) != 0) {                                     // renamed: the old file and its note go
        char old[72];
        srcPath(console, slot, old, sizeof old);
        p_.storage->remove(kitFile_[at].path);
        if (old[0]) p_.storage->remove(old);
    }
    if (at < 0) for (int i = 0; i < kitFiles_; ++i) if (kitFile_[i].console == 3) { at = i; break; }
    if (at < 0) { if (kitFiles_ >= kMaxKitFiles) { error_ = "TOO MANY KIT FILES"; return false; } at = kitFiles_++; }
    std::snprintf(kitFile_[at].path, sizeof kitFile_[at].path, "%s", path);
    kitFile_[at].idx = uint8_t(slot);
    kitFile_[at].console = uint8_t(console);
    std::memcpy(p_.bigMem + kKitOffset + size_t(at) * gb::kKitBankSize, w, gb::kKitBankSize);
    applyKit(at);
    {                                                                  // which WAV each sample came from
        char sp[72], text[640];
        srcPath(console, slot, sp, sizeof sp);
        int len = 0;
        for (int i = 0; i < 15; ++i) if (kitSrc_[console ? 1 : 0][i][0]) len += std::snprintf(text + len, sizeof text - size_t(len), "%d %s\n", i, kitSrc_[console ? 1 : 0][i]);
        if (len > 0) {
            const ot::IStorage::Handle h = p_.storage->open(sp, ot::IStorage::kWrite);
            if (h >= 0) { p_.storage->write(h, text, uint32_t(len)); p_.storage->close(h); }
        } else if (sp[0]) p_.storage->remove(sp);
    }
    return true;
}

// ---- firmware images: Firmware/*.ptf, the files the update bootloader accepts ------------------------------------------------------

int App::LinkedTransport::listFirmware() {
    App* a = app;
    a->fwCount_ = 0;
    if (!a->platform().storage) return 0;
    a->platform().storage->listDir("Firmware", [](const ot::IStorage::DirEntry& e, void* ctx) {
        App* x = static_cast<App*>(ctx);
        const size_t n = std::strlen(e.name);
        bool ok = !e.isDir && n < sizeof x->fwNames_[0];
        if (ok) {
            if (x->p_.isUpdateImageName) ok = x->p_.isUpdateImageName(e.name);                      // the device's own rule
            else ok = n >= 15 && ((!std::strncmp(e.name, "PolyendTrac", 11) && !strcasecmp(e.name + n - 4, ".ptf")) || (!std::strncmp(e.name, "mt", 2) && !strcasecmp(e.name + n - 4, ".hex")));
        }
        if (ok && x->fwCount_ < kMaxFirmware) std::snprintf(x->fwNames_[x->fwCount_++], sizeof x->fwNames_[0], "%s", e.name);
        return x->fwCount_ < kMaxFirmware;
    }, a);
    for (int i = 1; i < a->fwCount_; ++i)
        for (int j = i; j > 0 && strcasecmp(a->fwNames_[j - 1], a->fwNames_[j]) > 0; --j) {
            char tmp[40]; std::memcpy(tmp, a->fwNames_[j], 40); std::memcpy(a->fwNames_[j], a->fwNames_[j - 1], 40); std::memcpy(a->fwNames_[j - 1], tmp, 40);
        }
    return a->fwCount_;
}

bool App::LinkedTransport::flashFirmware(int i) {
    App* a = app;
    a->error_ = nullptr;
    if (i < 0 || i >= a->fwCount_ || !a->platform().storage) return false;
    if (!a->platform().requestFirmwareUpdate) { a->error_ = "ONLY ON THE TRACKER"; return false; }
    a->progress("FLASHING FIRMWARE", 50);
    const bool ok = a->platform().requestFirmwareUpdate(*a->platform().storage, a->fwNames_[i]);   // does not return when it works
    a->progress("", 0);
    if (!ok) a->error_ = "THE UPDATE WAS REFUSED";
    return ok;
}

// ---- WAV samples: GB/Samples/*.wav --------------------------------------------------------------------------------------------

int App::LinkedTransport::listSamples() {
    App* a = app;
    a->sampleCount_ = 0;
    if (!a->platform().storage) return 0;
    a->platform().storage->mkdir("GB");
    a->platform().storage->mkdir(kSampleDir);
    a->platform().storage->listDir(kSampleDir, [](const ot::IStorage::DirEntry& e, void* ctx) {
        App* x = static_cast<App*>(ctx);
        const size_t n = std::strlen(e.name);
        if (e.isDir || n < 5 || n > 27 || strcasecmp(e.name + n - 4, ".wav") != 0 || x->sampleCount_ >= kMaxSamples) return x->sampleCount_ < kMaxSamples;
        std::snprintf(x->sampleNames_[x->sampleCount_++], sizeof x->sampleNames_[0], "%s", e.name);
        return true;
    }, a);
    for (int i = 1; i < a->sampleCount_; ++i)
        for (int j = i; j > 0 && strcasecmp(a->sampleNames_[j - 1], a->sampleNames_[j]) > 0; --j) {
            char tmp[28]; std::memcpy(tmp, a->sampleNames_[j], 28); std::memcpy(a->sampleNames_[j], a->sampleNames_[j - 1], 28); std::memcpy(a->sampleNames_[j - 1], tmp, 28);
        }
    return a->sampleCount_;
}

namespace {
struct CardWav final : gb::WavReader {
    ot::IStorage* st;
    ot::IStorage::Handle h;
    CardWav(ot::IStorage* s, const char* path) : st(s), h(s->open(path, ot::IStorage::kRead)) {}
    ~CardWav() override { if (h >= 0) st->close(h); }
    int read(uint32_t off, void* dst, int n) override {
        if (h < 0 || !st->seek(h, off)) return 0;
        int got = 0;
        uint8_t* p = static_cast<uint8_t*>(dst);
        while (got < n) { const int32_t r = st->read(h, p + got, uint32_t(n - got)); if (r <= 0) break; got += r; }
        return got;
    }
};
}  // namespace

int App::LinkedTransport::sampleLoad(int i, bool* cut) {
    App* a = app;
    uint8_t* out = a->workBuf(2);
    if (!a->platform().storage || !out || !a->fileBuf() || i < 0 || i >= a->sampleCount_) return 0;
    char path[64];
    std::snprintf(path, sizeof path, "%s/%s", kSampleDir, a->sampleNames_[i]);
    CardWav f(a->platform().storage, path);
    gb::WavInfo w;
    if (f.h < 0 || !gb::wavOpen(f, w)) return 0;
    bool c = false;
    const int n = gb::wavToNibbles(f, w, reinterpret_cast<int16_t*>(a->fileBuf()), gb::kKitCapacity * 2 - 64, out, int(gb::kKitCapacity), c);
    if (cut) *cut = c;
    return n;
}

bool App::loadKits() {
    while (!romStep()) {}
    scanKitFiles();
    for (int i = 0; i < kitFiles_; ++i) kitFileStep(i);
    return synthGen.available();
}

bool App::loadLinked(const char* base) {
    char pa[96], pb[96];
    std::snprintf(pa, sizeof pa, "%s/%s_A.lsdsng", kSongDir, base);
    std::snprintf(pb, sizeof pb, "%s/%s_B.lsdsng", kSongDir, base);
    return loadSong(pa, 0) && loadSong(pb, 1);
}

bool App::saveLinked(const char* base) {
    char pa[96], pb[96], na[16], nb[16];
    std::snprintf(pa, sizeof pa, "%s/%s_A.lsdsng", kSongDir, base);
    std::snprintf(pb, sizeof pb, "%s/%s_B.lsdsng", kSongDir, base);
    std::snprintf(na, sizeof na, "%.7sA", base);
    std::snprintf(nb, sizeof nb, "%.7sB", base);
    return saveSong(pa, na, 0) && saveSong(pb, nb, 1);
}

void App::LinkedTransport::start(int console, int row) {
    // The console whose Start was pressed starts at its cursor row; the other one at its own cursor row.
    const int rowA = console == 0 ? row : app->lsdj.startRow();
    const int rowB = console == 1 ? row : app->lsdjB.startRow();
    // The virtual link cable: with SYNC on in both projects, both consoles start together and the slave follows the master's tempo
    // (the master is the one set to MASTER, whichever console Start was pressed on); otherwise Start plays only that console.
    if (app->linked()) {
        const int m = app->masterConsole();
        gb::Song& slave = m ? app->song : app->songB;
        slave.at(gb::off::kTempo) = (m ? app->songB : app->song).at(gb::off::kTempo);
        app->dual.startLinked(m, rowA, rowB);
    } else app->dual.startOne(console, console ? rowB : rowA);
    app->flushRing();
}

void App::LinkedTransport::startLoop(int console, int ch, int phrase, int row) {
    const int rowA = console == 0 ? row : app->lsdj.startRow();
    const int rowB = console == 1 ? row : app->lsdjB.startRow();
    app->dual.startOverdub(console, ch, phrase, rowA, rowB, app->linked() ? app->masterConsole() : -1);
    app->flushRing();
}

void App::LinkedTransport::setSyncMode(int console, int mode) {
    gb::Song& s = console ? app->songB : app->song;
    if (mode == 0) { s.at(gb::off::kSync) = 0; return; }
    s.at(gb::off::kSync) = 1;
    app->syncRole_[console] = mode;
    app->syncRole_[1 - console] = mode == 1 ? 2 : 1;
    if (app->linked()) tempoChanged(app->masterConsole());
}

// The master's tempo is the slave's: the slave's project takes it too, so the pair saves and exports with one tempo.
void App::LinkedTransport::tempoChanged(int console) {
    if (!app->linked() || console != app->masterConsole()) return;
    const uint8_t t = (console ? app->songB : app->song).at(gb::off::kTempo);
    (console ? app->song : app->songB).at(gb::off::kTempo) = t;
    (console ? app->player : app->playerB).setTempo(t);
}

int App::LinkedTransport::listSongs(int console) {
    app->songCount_ = 0;
    ot::IStorage* st = app->platform().storage;
    if (!st) return 0;
    if (st->exists(app->bankPath(console))) {
        CardIO io(st, app->bankPath(console), ot::IStorage::kRead);
        gb::Bank bank;
        if (io.ok() && bank.load(io))
            for (int s = 0; s < gb::kBankSlots && app->songCount_ < kMaxSongs; ++s)
                if (bank.used(s)) {
                    std::snprintf(app->songNames_[app->songCount_], sizeof app->songNames_[0], "%.8s", bank.name[s]);
                    app->songSlot_[app->songCount_++] = uint8_t(s);
                }
    }
    const int files = app->songCount_;
    st->listDir(kSongDir, [](const ot::IStorage::DirEntry& e, void* ctx) {
        App* a = static_cast<App*>(ctx);
        const size_t n = std::strlen(e.name);
        if (e.isDir || n < 8 || n > 22 + 7 || strcasecmp(e.name + n - 7, ".lsdsng") != 0 || a->songCount_ >= kMaxSongs) return a->songCount_ < kMaxSongs;
        std::snprintf(a->songNames_[a->songCount_], sizeof a->songNames_[0], "+%.*s", int(std::min<size_t>(n - 7, 22)), e.name);
        a->songSlot_[a->songCount_++] = 0xFF;
        return true;
    }, app);
    // loose files in name order (insertion sort: the list is short); the bank keeps its slot order
    for (int i = files + 1; i < app->songCount_; ++i)
        for (int j = i; j > files && strcasecmp(app->songNames_[j - 1], app->songNames_[j]) > 0; --j) {
            char tmp[24]; std::memcpy(tmp, app->songNames_[j], 24); std::memcpy(app->songNames_[j], app->songNames_[j - 1], 24); std::memcpy(app->songNames_[j - 1], tmp, 24);
        }
    return app->songCount_;
}

bool App::LinkedTransport::loadSongFile(int i, int console) {
    if (i < 0 || i >= app->songCount_) return false;
    if (app->songSlot_[i] != 0xFF) return app->loadFromBank(app->songSlot_[i], console);
    char path[64];
    std::snprintf(path, sizeof path, "%s/%s.lsdsng", kSongDir, app->songNames_[i] + 1);
    return app->loadSong(path, console);
}

bool App::LinkedTransport::deleteSongFile(int i, int console) {
    ot::IStorage* st = app->platform().storage;
    if (!st || i < 0 || i >= app->songCount_ || app->songSlot_[i] == 0xFF) return false;
    CardIO io(st, app->bankPath(console), ot::IStorage::kReadWrite);
    gb::Bank bank;
    if (!io.ok() || !bank.load(io)) return false;
    bank.remove(app->songSlot_[i]);
    return bank.save(io);
}

bool App::LinkedTransport::action(Action a, int slot) {
    app->done_ = nullptr;
    char name[8];
    std::snprintf(name, sizeof name, "S%02d", slot);
    switch (a) {
        case kSavePair: return app->saveLinked(name);
        case kLoadPair: return app->loadLinked(name);
        case kSaveSong: return app->saveProject(slot ? 1 : 0);
        case kExportWav: {
            if (app->platform().storage) app->platform().storage->mkdir(kExportDir);
            char base[16], path[64];
            app->projectFileName(0, base, sizeof base, "SONG");
            std::snprintf(path, sizeof path, "%s/%s.wav", kExportDir, base);
            return app->exportWav(path, 44100, 600);
        }
        case kExportFiles: return app->exportFiles(nullptr, slot);
    }
    return false;
}

// Instrument files: GB/Instruments/<name>.gbi
int App::LinkedTransport::listInstruments() {
    app->instCount_ = 0;
    if (!app->platform().storage || !app->instNames()) return 0;
    app->platform().storage->mkdir(kInstDir);
    app->platform().storage->listDir(kInstDir, [](const ot::IStorage::DirEntry& e, void* ctx) {
        App* a = static_cast<App*>(ctx);
        const size_t n = std::strlen(e.name);
        if (e.isDir || n < 5 || n > 23 + 4 || strcasecmp(e.name + n - 4, ".gbi") != 0 || a->instCount_ >= kMaxInst) return a->instCount_ < kMaxInst;
        std::snprintf(a->instNames()[a->instCount_], 24, "%.*s", int(std::min<size_t>(n - 4, 23)), e.name);
        ++a->instCount_;
        return true;
    }, app);
    char (*nm)[24] = app->instNames();
    for (int i = 1; i < app->instCount_; ++i)
        for (int j = i; j > 0 && strcasecmp(nm[j - 1], nm[j]) > 0; --j) {
            char tmp[24]; std::memcpy(tmp, nm[j], 24); std::memcpy(nm[j], nm[j - 1], 24); std::memcpy(nm[j - 1], tmp, 24);
        }
    return app->instCount_;
}

bool App::LinkedTransport::saveInstrumentFile(const char* name, const uint8_t* data, int n) {
    if (!app->platform().storage) return false;
    char base[12] = {};
    for (int i = 0; i < 8 && name[i]; ++i) base[i] = name[i] == ' ' ? '_' : name[i];
    app->platform().storage->mkdir("GB");
    app->platform().storage->mkdir(kInstDir);
    char path[64];
    std::snprintf(path, sizeof path, "%s/%s.gbi", kInstDir, base);
    const ot::IStorage::Handle h = app->platform().storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    const bool ok = app->platform().storage->write(h, data, uint32_t(n)) == n;
    app->platform().storage->close(h);
    return ok;
}

int App::LinkedTransport::loadInstrumentFile(int i, uint8_t* out, int max) {
    if (i < 0 || i >= app->instCount_ || !app->platform().storage) return -1;
    char path[64];
    std::snprintf(path, sizeof path, "%s/%s.gbi", kInstDir, app->instNames()[i]);
    const ot::IStorage::Handle h = app->platform().storage->open(path, ot::IStorage::kRead);
    if (h < 0) return -1;
    const int32_t n = app->platform().storage->read(h, out, uint32_t(max));
    app->platform().storage->close(h);
    return n;
}

void App::projectFileName(int console, char* out, size_t n, const char* fallback) const {
    const char* nm = (console ? lsdjB : lsdj).name();
    if (!nm[0]) nm = fallback;
    size_t i = 0;
    for (; nm[i] && i + 1 < n; ++i) out[i] = nm[i] == ' ' ? '_' : nm[i];
    out[i] = 0;
}

bool App::saveProject(int console) {
    progress("SAVING SONG", 30);
    const bool ok = saveToBank(console);
    progress("", 0);
    return ok;
}

// One bar, drawn at once (the main loop is busy): the label and percent go to both consoles' screens and a frame goes out.
void App::progress(const char* label, int pct) {
    lsdj.setProgress(label, pct);
    lsdjB.setProgress(label, pct);
    if (!p_.clock || !label[0]) return;
    const uint32_t now = p_.clock->millis();
    if (now - progressAt_ < 60 && pct < 100) return;
    progressAt_ = now;
    ui.requestRedraw();
    ui.update(now + 20);
}

// The power-on sound is a recording (assets/boot_chime.wav, see tools/make_chime.py), played back at the output rate.
void App::bootChime(int stage) {
    if (stage == 0 && p_.audio) { chimePos_ = 0; chimeOn_ = true; flushRing(); }
    else if (stage >= 2) chimeOn_ = false;
}

void App::renderChime(int16_t* out, int n, uint32_t rate) {
    const uint32_t step = rate ? uint32_t((uint64_t(kChimeRate) << 16) / rate) : 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t idx = chimePos_ >> 16, frac = chimePos_ & 0xFFFF;
        int v = 0;
        if (chimeOn_ && idx + 1 < uint32_t(kChimeLen)) {
            v = (kChimePcm[idx] * int32_t(65536 - frac) + kChimePcm[idx + 1] * int32_t(frac)) >> 16;
            v = v * kChimeGain / 16;
            chimePos_ += step;
        } else chimeOn_ = false;
        out[2 * i] = out[2 * i + 1] = int16_t(v);
    }
}

// Copies a card file through the scratch buffer.
static bool copyFile(ot::IStorage* st, uint8_t* buf, uint32_t cap, const char* from, const char* to) {
    const ot::IStorage::Handle in = st->open(from, ot::IStorage::kRead);
    if (in < 0) return false;
    const ot::IStorage::Handle out = st->open(to, ot::IStorage::kWrite);
    if (out < 0) { st->close(in); return false; }
    bool ok = true;
    for (;;) {
        int32_t n = st->read(in, buf, cap);
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        ok = st->write(out, buf, uint32_t(n)) == n;
        if (!ok) break;
    }
    st->close(in);
    st->close(out);
    return ok;
}

// The songs for two real Game Boys: each selected song goes into its bank (GB/Saves/A.srm, B.srm), the bank is copied to GB/Export/LSDJ_A.srm / LSDJ_B.srm
// (or `name`_A / _B) for the cartridge's save slot, and the song also goes out as a single .lsdsng for LSDJ's file manager.
bool App::exportFiles(const char* name, int which) {
    if (!p_.storage || !fileBuf()) return false;
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kExportDir);
    char base[2][16], bank[2][16];
    projectFileName(0, base[0], sizeof base[0], "SONGA");
    projectFileName(1, base[1], sizeof base[1], "SONGB");
    if (name) { std::snprintf(base[0], sizeof base[0], "%s_A", name); std::snprintf(base[1], sizeof base[1], "%s_B", name); }
    else if (which == 2 && std::strcmp(base[0], base[1]) == 0) { std::strncat(base[0], "_A", 3); std::strncat(base[1], "_B", 3); }
    std::snprintf(bank[0], sizeof bank[0], "%s_A", name ? name : "LSDJ");
    std::snprintf(bank[1], sizeof bank[1], "%s_B", name ? name : "LSDJ");
    bool ok = true;
    int step = 0;
    done_ = nullptr;
    const int steps = (which == 2 ? 6 : 3) + 1;
    for (int c = 0; c < 2; ++c) {
        if (which != 2 && which != c) continue;
        char path[80], sn[16];
        progress("EXPORT LSDJ", step++ * 100 / steps);
        if (!saveToBank(c)) { ok = false; continue; }
        progress("EXPORT LSDJ", step++ * 100 / steps);
        std::snprintf(path, sizeof path, "%s/%s.srm", kExportDir, bank[c]);
        ok = copyFile(p_.storage, fileBuf(), kFileBytes, bankPath(c), path) && ok;
        progress("EXPORT LSDJ", step++ * 100 / steps);
        std::snprintf(path, sizeof path, "%s/%s.lsdsng", kExportDir, base[c]);
        std::snprintf(sn, sizeof sn, "%.8s", base[c]);
        ok = saveSong(path, sn, c) && ok;
    }
    // Kits are not in a song: they are banks of the LSDJ ROM. Each console gets a copy of the card's ROM with its kits put in (PTSDJ_A.gb, PTSDJ_B.gb), so the
    // Game Boy plays the same drums as the Tracker.
    bool anyKits = false;
    for (int i = 0; i < kitFiles_; ++i) anyKits = anyKits || kitFile_[i].console != 3;
    if (anyKits && romGot_ >= (1u << 19)) {
        for (int c = 0; c < 2; ++c) {
            if (which != 2 && which != c) continue;
            progress("EXPORT LSDJ", step * 100 / steps);
            char path[80];
            std::snprintf(path, sizeof path, "%s/%s_%c.gb", kExportDir, name ? name : "PTSDJ", 'A' + c);
            if (exportRom(path, c)) {
                if (romSkipped_ == 0xFF) { if (!done_) done_ = "EXPORT DONE + ROM"; }
                else { std::snprintf(doneBuf_, sizeof doneBuf_, "KIT %02X: USE 1A-37", romSkipped_); done_ = doneBuf_; }
            } else ok = false;
        }
    }
    progress("", 0);
    return ok;
}

bool App::exportRom(const char* path, int console) {
    uint8_t* base = p_.bigMem && p_.bigMemSize >= kKitOffset + kMaxKitFiles * gb::kKitBankSize ? p_.bigMem + kKitOffset : nullptr;
    if (!p_.storage || !base || !p_.bigMem || romGot_ < (1u << 19)) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    bool ok = true;
    romSkipped_ = 0xFF;
    const uint32_t banks = romGot_ / uint32_t(gb::kKitBankSize);
    const gb::RomKits& set = console ? kitsB : kits;
    for (uint32_t b = 0; b < banks && ok; ++b) {
        const uint8_t* src = p_.bigMem + size_t(b) * gb::kKitBankSize;
        const int idx = int(b) - 8;                            // kit n is bank 8 + n
        const uint8_t* kit = idx >= 0 && idx < 64 ? set.override_(idx) : nullptr;
        if (kit && kit[0] == 0x60 && kit[1] == 0x40) {         // the console's own kit for this slot, if the bank is a kit or empty
            bool free = src[0] == 0x60 && src[1] == 0x40;
            if (!free) { free = true; for (size_t k = 0; k < gb::kKitBankSize && free; ++k) free = src[k] == 0xFF; }
            if (free) src = kit;
            else if (romSkipped_ == 0xFF) romSkipped_ = uint8_t(idx);   // the stock ROM keeps program code in the banks of kits 13-17: never overwritten
        }
        ok = p_.storage->write(h, src, uint32_t(gb::kKitBankSize)) == int32_t(gb::kKitBankSize);
    }
    p_.storage->close(h);
    return ok;
}

bool App::exportWav(const char* path, int rate, int maxSeconds) {
    if (!p_.storage) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    exporting_ = true;                                  // the audio callback stays silent and off the players while we render
    uint8_t hdr[44] = {};
    bool ok = p_.storage->write(h, hdr, 44) == 44;      // placeholder, rewritten with the real sizes at the end
    static int16_t buf[2 * 1024];
    uint32_t frames = 0;
    dual.start(0, 0);
    int total[2] = {};                                  // song rows in use: the bar is how far the slowest console has come through them
    for (int c = 0; c < 2; ++c) for (int r = 0; r < gb::kSongRows; ++r) for (int ch = 0; ch < 4; ++ch) if ((c ? songB : song).songCell(r, ch) != 0xFF) total[c] = r + 1;
    uint32_t tick = 0;
    while (ok && dual.running() && !dual.looped() && frames < uint32_t(rate) * uint32_t(maxSeconds)) {
        dual.render(buf, 1024, rate);
        ok = p_.storage->write(h, buf, sizeof buf) == int32_t(sizeof buf);
        frames += 1024;
        if ((++tick & 7) == 0) {
            int pct = 100;
            for (int c = 0; c < 2; ++c) {
                if (!dual.player(c).running() || !total[c]) continue;
                int row = 0;
                for (int ch = 0; ch < 4; ++ch) if (dual.player(c).seq().active(ch)) row = std::max(row, dual.player(c).seq().row(ch));
                pct = std::min(pct, 100 * row / total[c]);
            }
            static char lb[24]; std::snprintf(lb, sizeof lb, "EXPORT WAV %u:%02u", unsigned(frames / unsigned(rate) / 60), unsigned(frames / unsigned(rate) % 60));
            progress(lb, pct);
        }
    }
    progress("", 0);
    dual.stop();
    const uint32_t bytes = frames * 4;
    auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[o + i] = uint8_t(v >> (8 * i)); };
    auto w16 = [&](int o, uint32_t v) { hdr[o] = uint8_t(v); hdr[o + 1] = uint8_t(v >> 8); };
    std::memcpy(hdr, "RIFF", 4); w32(4, 36 + bytes); std::memcpy(hdr + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2);
    w32(24, uint32_t(rate)); w32(28, uint32_t(rate) * 4); w16(32, 4); w16(34, 16); std::memcpy(hdr + 36, "data", 4); w32(40, bytes);
    ok = ok && p_.storage->seek(h, 0) && p_.storage->write(h, hdr, 44) == 44;
    p_.storage->close(h);
    exporting_ = false;
    flushRing();
    return ok;
}

void App::setBanner(const char* s) { lsdj.setBanner(s); lsdjB.setBanner(s); }

void App::boot() {
    if (!p_.storage) return;
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kSongDir);
    p_.storage->mkdir(kKitDir);
    p_.storage->mkdir(kExportDir);
    bootStep_ = 0;
    setBanner("LOADING SONG A");
}

// Everything that reads the card at start runs here, one step per main-loop pass, announced on screen before it starts: if the
// unit ever stops, the last banner says where.
void App::bootTick() {
    switch (bootStep_) {
        case 0: loadLastSaved(0); setBanner("LOADING SONG B"); ++bootStep_; break;
        case 1: loadLastSaved(1); setBanner("LOADING ROM"); ++bootStep_; break;
        case 2: {
            const bool done = romStep();
            if (!done) { char b[24]; std::snprintf(b, sizeof b, "LOADING ROM %u%%", unsigned(romGot_ * 100 / kRomBytes)); setBanner(b); }
            else { scanKitFiles(); kitStep_ = 0; setBanner(kitFiles_ ? "LOADING KITS" : ""); bootStep_ = kitFiles_ ? 3 : 4; }
            break;
        }
        case 3: {                                               // one .kit file per pass
            kitFileStep(kitStep_);
            if (++kitStep_ >= kitFiles_) { setBanner(""); bootStep_ = 4; }
            break;
        }
        default: break;
    }
}

void App::quiesce() {
    if (p_.audio) p_.audio->setCallback(&fillSilence, nullptr);
}

void App::loop() {
    if (p_.service) p_.service();
    if (bootStep_ < 4 && p_.storage) bootTick();
    pump();
    if (p_.clock && p_.clock->millis() - statsAt_ >= 500) {      // render cost and late audio blocks for the Project screen
        statsAt_ = p_.clock->millis();
        const uint32_t late = ringLate_.load(std::memory_order_relaxed);
        if (!dual.running()) { peakC_ = 0; lateAtStart_ = late; }     // both are for the current playback
        std::snprintf(audioText_, sizeof audioText_, "P%uK L%u R%uK%d", unsigned(peakC_ / 1000), unsigned(late - lateAtStart_), unsigned(romGot_ / 1024), kitsLoaded_);
        lsdj.setAudioInfo(audioText_);
        lsdjB.setAudioInfo(audioText_);
    }
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    ui.update(p_.clock->millis());
}

void App::run() {
    for (;;) loop();
}

}  // namespace atw
