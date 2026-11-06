#include "atw/app.h"

#include <algorithm>
#include "gb/upgrade.h"
#include <cstdio>
#include <new>
#include <cstring>

namespace atw {

namespace {
void fillThunk(int16_t* out, uint32_t frames, void* ctx) { static_cast<App*>(ctx)->fill(out, frames); }
void fillSilence(int16_t* out, uint32_t frames, void*) {
    std::memset(out, 0, size_t(frames) * ot::IAudioOut::kChannels * sizeof(int16_t));
}
}  // namespace

void App::fill(int16_t* out, uint32_t frames) {
    if (!exporting_ && dual.running()) dual.render(out, int(frames), int(p_.sampleRateMilliHz / 1000));
    else fillSilence(out, frames, nullptr);
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

bool App::loadSong(const char* path, int console) {
    if (!p_.storage || !fileBuf()) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    uint8_t* buf = fileBuf();
    const int32_t n = p_.storage->read(h, buf, kFileBytes);
    p_.storage->close(h);
    if (n <= 0) return false;
    dual.stop();
    gb::Song& fresh = *new (buf + kFileBytes) gb::Song;
    if (!gb::loadLsdsng(fresh, buf, size_t(n))) return false;
    if (!gb::upgradeSong(fresh)) return false;       // songs saved by older LSDJ versions
    std::memcpy((console ? songB : song).mem(), fresh.mem(), gb::kMemSize);
    return true;
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
        synthGen.setRom(p_.bigMem, romGot_);
        player.setKits(&kits);
        playerB.setKits(&kits);
    }
    return true;
}

bool App::loadKits() {
    while (!romStep()) {}
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
    app->dual.start(rowA, rowB);
}

bool App::LinkedTransport::action(Action a, int slot) {
    char name[8];
    std::snprintf(name, sizeof name, "S%02d", slot);
    switch (a) {
        case kSavePair: return app->saveLinked(name);
        case kLoadPair: return app->loadLinked(name);
        case kExportWav: {
            if (app->platform().storage) app->platform().storage->mkdir(kExportDir);
            char path[64];
            std::snprintf(path, sizeof path, "%s/%s.wav", kExportDir, name);
            return app->exportWav(path);
        }
    }
    return false;
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
    while (ok && dual.running() && !dual.looped() && frames < uint32_t(rate) * uint32_t(maxSeconds)) {
        dual.render(buf, 1024, rate);
        ok = p_.storage->write(h, buf, sizeof buf) == int32_t(sizeof buf);
        frames += 1024;
    }
    dual.stop();
    const uint32_t bytes = frames * 4;
    auto w32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[o + i] = uint8_t(v >> (8 * i)); };
    auto w16 = [&](int o, uint32_t v) { hdr[o] = uint8_t(v); hdr[o + 1] = uint8_t(v >> 8); };
    std::memcpy(hdr, "RIFF", 4); w32(4, 36 + bytes); std::memcpy(hdr + 8, "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 2);
    w32(24, uint32_t(rate)); w32(28, uint32_t(rate) * 4); w16(32, 4); w16(34, 16); std::memcpy(hdr + 36, "data", 4); w32(40, bytes);
    ok = ok && p_.storage->seek(h, 0) && p_.storage->write(h, hdr, 44) == 44;
    p_.storage->close(h);
    exporting_ = false;
    return ok;
}

void App::setBanner(const char* s) { lsdj.setBanner(s); lsdjB.setBanner(s); }

void App::boot() {
    if (!p_.storage) return;
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kSongDir);
    p_.storage->mkdir(kExportDir);
    bootStep_ = 0;
    setBanner("LOADING SONG A");
}

// Everything that reads the card at start runs here, one step per main-loop pass, announced on screen before it starts: if the
// unit ever stops, the last banner says where.
void App::bootTick() {
    switch (bootStep_) {
        case 0: loadSong("GB/AUTOLOAD.LSDSNG"); setBanner("LOADING SONG B"); ++bootStep_; break;
        case 1: loadSong("GB/AUTOLOAD_B.LSDSNG", 1); setBanner("LOADING ROM"); ++bootStep_; break;
        case 2: {
            const bool done = romStep();
            if (!done) { char b[24]; std::snprintf(b, sizeof b, "LOADING ROM %u%%", unsigned(romGot_ * 100 / kRomBytes)); setBanner(b); }
            else { setBanner(""); bootStep_ = 3; }
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
    if (bootStep_ < 3 && p_.storage) bootTick();
    if (p_.driverStats && p_.clock && p_.clock->millis() - statsAt_ >= 500) {      // audio load for the Project screen
        statsAt_ = p_.clock->millis();
        DriverStats st;
        if (p_.driverStats(st)) {
            std::snprintf(audioText_, sizeof audioText_, "A %uK/%uK L%u", unsigned(st.avgC / 1000), unsigned(st.maxC / 1000), unsigned(st.underruns));
            lsdj.setAudioInfo(audioText_);
            lsdjB.setAudioInfo(audioText_);
        }
    }
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    ui.update(p_.clock->millis());
}

void App::run() {
    for (;;) loop();
}

}  // namespace atw
