#include "atw/app.h"

#include <cstring>

#include "atw/macfile.h"
#include "atw/midi_in.h"
#include "atw/project.h"

#include <cstdio>
#include <cstring>

namespace atw {

bool App::init(const Platform& p) {
    p_ = p;
    if (!p_.display || !p_.input || !p_.clock || !p_.patterns) return false;
    song.patterns = p_.patterns;
    song.clear();
    undo_.bind(&song);
    exporter.bind(&song, p_.storage);
    engine.init(&song, p_.sampleRateMilliHz);
    engine.enableMixer();
    if (p_.audio) {
        p_.audio->setCallback(&AudioEngine::fillThunk, &engine);
        p_.audio->start();
    }
    if (p_.setRawDetents) p_.setRawDetents(true);   // jog wheel = value editor
    ui.init(*this);
    return true;
}

void App::boot() {
    if (!p_.storage) return;
    p_.storage->mkdir(kDir);
    p_.storage->mkdir(kProjectDir);
    loadSettings();
    if (restoreAutosave()) ui.message("Restored the unsaved song from the last session");
}

void App::markDirty() {
    ++editSerial_;
    touch();
}

void App::touch() {
    dirty_ = true;
    autosaved_ = false;
    editedAt_ = p_.clock ? p_.clock->millis() : 0;
}

namespace {
void projectPath(char* out, size_t cap, const char* name) { std::snprintf(out, cap, "%s/%s.atr", App::kProjectDir, name); }
}  // namespace

bool App::saveProject(const char* given, std::string* error) {
    if (!p_.storage) { if (error) *error = "no card"; return false; }
    if (!given || !*given) { if (error) *error = "no name"; return false; }
    char name[kNameLen + 1];           // `given` may be projectName() itself
    std::snprintf(name, sizeof name, "%s", given);
    char path[96];
    projectPath(path, sizeof path, name);
    if (!atw::saveProject(*p_.storage, song, path, error)) return false;
    std::snprintf(project_, sizeof project_, "%s", name);
    std::snprintf(file_, sizeof file_, "%s", name);
    dirty_ = false;
    p_.storage->remove(kAutosave);     // the project now has everything the autosave had
    autosaved_ = true;
    return true;
}

void App::setProjectName(const char* name) { std::snprintf(project_, sizeof project_, "%s", name ? name : ""); }

void App::loadSettings() {
    if (!p_.storage) return;
    const ot::IStorage::Handle h = p_.storage->open(kSettings, ot::IStorage::kRead);
    if (h < 0) return;
    char text[256] = {};
    const int32_t n = p_.storage->read(h, text, sizeof text - 1);
    p_.storage->close(h);
    if (n <= 0) return;
    int v;
    if (const char* s = std::strstr(text, "metronome=")) if (std::sscanf(s + 10, "%d", &v) == 1) ui.metronome = v != 0;
    if (const char* s = std::strstr(text, "metrovolume=")) if (std::sscanf(s + 12, "%d", &v) == 1) ui.metroVolume = uint8_t(v < 0 ? 0 : (v > 100 ? 100 : v));
    if (const char* s = std::strstr(text, "preroll=")) if (std::sscanf(s + 8, "%d", &v) == 1) ui.preroll = v != 0;
    if (const char* s = std::strstr(text, "funkkeep=")) if (std::sscanf(s + 9, "%d", &v) == 1) ui.funkKeepsTempo = v != 0;
    ui.applyTransport();
}

void App::saveSettings() {
    if (!p_.storage) return;
    char text[96];
    const int n = std::snprintf(text, sizeof text, "metronome=%d\nmetrovolume=%u\npreroll=%d\nfunkkeep=%d\n", ui.metronome ? 1 : 0, unsigned(ui.metroVolume), ui.preroll ? 1 : 0, ui.funkKeepsTempo ? 1 : 0);
    p_.storage->mkdir(kDir);
    const ot::IStorage::Handle h = p_.storage->open(kSettings, ot::IStorage::kWrite);
    if (h < 0) return;
    p_.storage->write(h, text, uint32_t(n));
    p_.storage->close(h);
}

void App::songReplaced() {
    undo_.reset();
    ++epoch_;
    engine.rebuild();
    engine.release();
    ui.songChanged();
}

bool App::openProject(const char* given, std::string* error) {
    if (!p_.storage) { if (error) *error = "no card"; return false; }
    char name[kNameLen + 1];
    std::snprintf(name, sizeof name, "%s", given);
    char path[96];
    projectPath(path, sizeof path, name);
    engine.stop();
    engine.hold();
    const bool ok = atw::loadProject(*p_.storage, song, path, error);
    songReplaced();
    if (!ok) return false;
    std::snprintf(project_, sizeof project_, "%s", name);
    std::snprintf(file_, sizeof file_, "%s", name);
    dirty_ = false;
    autosaved_ = true;
    return true;
}

namespace {
void macPath(char* out, size_t cap, const char* name) { std::snprintf(out, cap, "%s/%s.atari.json", App::kMacDir, name); }
}  // namespace

bool App::saveMacSession(const char* name, std::string* error) {
    if (!p_.storage) { if (error) *error = "no card"; return false; }
    if (!name || !*name) { if (error) *error = "no name"; return false; }
    char path[96];
    macPath(path, sizeof path, name);
    p_.storage->mkdir(kDir);
    p_.storage->mkdir(kMacDir);
    return atw::saveMacSession(*p_.storage, song, path, error);
}

bool App::openMacSession(const char* name, std::string* error) {
    if (!p_.storage) { if (error) *error = "no card"; return false; }
    char path[96];
    macPath(path, sizeof path, name);
    engine.stop();
    engine.hold();
    const bool ok = atw::loadMacSession(*p_.storage, song, path, name, error);
    songReplaced();
    if (!ok) return false;
    std::snprintf(project_, sizeof project_, "%s", name);   // named like the session, but not in a project file yet
    file_[0] = 0;
    dirty_ = false;
    autosaved_ = true;
    markDirty();
    return true;
}

void App::newProject() {
    engine.stop();
    engine.hold();
    song.clear();
    songReplaced();
    project_[0] = 0;
    file_[0] = 0;
    dirty_ = false;
    autosaved_ = true;
    if (p_.storage) p_.storage->remove(kAutosave);
}

namespace {
struct ListCtx {
    char (*names)[kNameLen + 1];
    int count, max;
    const char* suffix;               // ".atr", ".atari.json" (case does not matter)
};
bool listFn(const ot::IStorage::DirEntry& e, void* ctx) {
    ListCtx* c = static_cast<ListCtx*>(ctx);
    const size_t n = std::strlen(e.name);
    const size_t sn = std::strlen(c->suffix);
    if (e.isDir || n <= sn || n - sn > size_t(kNameLen) || c->count >= c->max) return true;
    for (size_t i = 0; i < sn; ++i)
        if ((e.name[n - sn + i] | 32) != c->suffix[i]) return true;
    std::snprintf(c->names[c->count], kNameLen + 1, "%.*s", int(n - sn), e.name);
    ++c->count;
    return true;
}
}  // namespace

int App::listProjects(char (*names)[kNameLen + 1], int max) { return listFiles(kProjectDir, ".atr", names, max); }

int App::listMacSessions(char (*names)[kNameLen + 1], int max) { return listFiles(kMacDir, ".atari.json", names, max); }

int App::listFiles(const char* dir, const char* suffix, char (*names)[kNameLen + 1], int max) {
    if (!p_.storage) return 0;
    ListCtx c{names, 0, max, suffix};
    p_.storage->listDir(dir, listFn, &c);
    for (int i = 1; i < c.count; ++i)   // insertion sort: a few dozen names at most
        for (int j = i; j > 0 && std::strcmp(names[j - 1], names[j]) > 0; --j) {
            char t[kNameLen + 1];
            std::memcpy(t, names[j], sizeof t);
            std::memcpy(names[j], names[j - 1], sizeof t);
            std::memcpy(names[j - 1], t, sizeof t);
        }
    return c.count;
}

bool App::undoStep(bool redo) {
    Undo::Target t;
    engine.hold();
    const bool ok = redo ? undo_.redo(&t) : undo_.undo(&t);
    if (ok && t.kind == Undo::Kind::kHead) engine.rebuild();   // chip / region may have come back
    engine.release();
    if (!ok) return false;
    switch (t.kind) {
        case Undo::Kind::kPattern:
            ui.pattern = t.index;
            ui.show(ui::ScreenId::kPattern);
            break;
        case Undo::Kind::kInstrument:
            ui.inst = uint8_t(t.index + 1);
            engine.resetVoices();
            ui.show(ui::ScreenId::kInstrument);
            break;
        case Undo::Kind::kHead:
            ui.show(ui::ScreenId::kSong);
            break;
        default: break;
    }
    touch();
    ui.requestRedraw();
    return true;
}
bool App::undo() { return undoStep(false); }
bool App::redo() { return undoStep(true); }

void App::autosave() {
    if (!p_.storage || autosaved_) return;
    if (atw::saveProject(*p_.storage, song, kAutosave)) autosaved_ = true;
}

bool App::restoreAutosave() {
    if (!p_.storage || !p_.storage->exists(kAutosave)) return false;
    engine.hold();
    const bool ok = atw::loadProject(*p_.storage, song, kAutosave);
    songReplaced();
    if (!ok) return false;
    dirty_ = true;                     // not in any project file yet
    autosaved_ = true;
    return true;
}

void App::pollMidi() {
    if (!p_.midi) return;
    static const ot::MidiPortId kPorts[2] = {ot::MidiPortId::kDin, ot::MidiPortId::kUsb};
    for (int port = 0; port < 2; ++port) {
        uint8_t buf[64];
        for (int round = 0; round < 4; ++round) {          // bounded per pass: the screen must keep up
            const uint32_t n = p_.midi->receive(kPorts[port], buf, sizeof buf);
            if (!n) break;
            for (uint32_t i = 0; i < n; ++i) {
                MidiIn::Msg m;
                if (!midiIn_[port].feed(buf[i], m)) continue;
                switch (m.type) {
                    case MidiIn::Type::kNoteOn: ui.midiNoteOn(uint8_t(MidiIn::cellNote(m.note))); break;
                    case MidiIn::Type::kNoteOff: ui.midiNoteOff(uint8_t(MidiIn::cellNote(m.note))); break;
                    case MidiIn::Type::kAllOff: ui.midiAllOff(); break;
                    default: break;
                }
            }
        }
    }
}

void App::loop() {
    if (p_.service) p_.service();
    // Three seconds after the last edit, and never while playing (the card write must not delay the audio).
    if (!autosaved_ && !engine.playing() && p_.clock->millis() - editedAt_ > 3000) autosave();
    pollMidi();
    if (exporter.busy()) exporter.service();
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    ui.update(p_.clock->millis());
}

void App::run() {
    for (;;) loop();
}

void App::applySongConfig() {
    engine.hold();
    engine.rebuild();
    engine.release();
    ui.requestRedraw();
}

bool App::screenshot(char* name, uint32_t cap) {
    std::snprintf(name, cap, "Screenshot: coming");
    return false;
}

}  // namespace atw
