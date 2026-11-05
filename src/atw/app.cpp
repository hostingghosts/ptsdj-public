#include "atw/app.h"

#include <algorithm>
#include "gb/upgrade.h"
#include <cstring>

namespace atw {

namespace {
void fillThunk(int16_t* out, uint32_t frames, void* ctx) { static_cast<App*>(ctx)->fill(out, frames); }
void fillSilence(int16_t* out, uint32_t frames, void*) {
    std::memset(out, 0, size_t(frames) * ot::IAudioOut::kChannels * sizeof(int16_t));
}
}  // namespace

void App::fill(int16_t* out, uint32_t frames) {
    if (player.running()) player.render(out, int(frames), int(p_.sampleRateMilliHz / 1000));
    else fillSilence(out, frames, nullptr);
}

bool App::init(const Platform& p) {
    p_ = p;
    if (!p_.display || !p_.input || !p_.clock) return false;
    if (p_.audio) {
        p_.audio->setCallback(&fillThunk, this);
        p_.audio->start();
    }
    ui.init(*this);
    return true;
}

bool App::loadSong(const char* path) {
    if (!p_.storage) return false;
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    static uint8_t buf[9 + 64 * 512 + 512];
    const int32_t n = p_.storage->read(h, buf, sizeof buf);
    p_.storage->close(h);
    if (n <= 0) return false;
    player.stop();
    gb::Song fresh;
    if (!gb::loadLsdsng(fresh, buf, size_t(n))) return false;
    if (!gb::upgradeSong(fresh)) return false;       // songs saved by older LSDJ versions
    std::memcpy(song.mem(), fresh.mem(), gb::kMemSize);
    return true;
}

bool App::saveSong(const char* path, const char* name) {
    if (!p_.storage) return false;
    static uint8_t buf[9 + 64 * 512];
    const size_t n = gb::saveLsdsng(song, name, 1, buf);
    const ot::IStorage::Handle h = p_.storage->open(path, ot::IStorage::kWrite);
    if (h < 0) return false;
    const bool ok = p_.storage->write(h, buf, uint32_t(n)) == int32_t(n);
    p_.storage->close(h);
    return ok;
}

bool App::loadKits() {
    if (!p_.storage || !p_.bigMem || p_.bigMemSize < (1u << 20)) return false;
    const ot::IStorage::Handle h = p_.storage->open(kRomPath, ot::IStorage::kRead);
    if (h < 0) return false;
    uint32_t got = 0;
    while (got < (1u << 20)) {
        const int32_t n = p_.storage->read(h, p_.bigMem + got, std::min<uint32_t>(32768, (1u << 20) - got));
        if (n <= 0) break;
        got += uint32_t(n);
    }
    p_.storage->close(h);
    if (got < (1u << 19)) return false;
    kits.setRom(p_.bigMem, got);
    player.setKits(&kits);
    return true;
}

void App::boot() {
    if (!p_.storage) return;
    p_.storage->mkdir("GB");
    p_.storage->mkdir(kSongDir);
    loadKits();
    loadSong("GB/AUTOLOAD.LSDSNG");   // development convenience: opens this song at start when it exists
}

void App::quiesce() {
    if (p_.audio) p_.audio->setCallback(&fillSilence, nullptr);
}

void App::loop() {
    if (p_.service) p_.service();
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    ui.update(p_.clock->millis());
}

void App::run() {
    for (;;) loop();
}

}  // namespace atw
