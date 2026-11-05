#include "atw/ui/settings_screen.h"

#include <cstdio>
#include <cstring>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace {
constexpr uint8_t kActNew = 200, kActOpen = 201, kActSaveAs = 202, kActOverwrite = 203, kActMacName = 204, kActMacOverwrite = 205, kActInstall = 206, kActChip = 207, kActProjName = 210;
}

void SettingsScreen::labels(const char* out[8]) {
    if (open_) {
        out[6] = "CANCEL";
        out[7] = count_ ? (fw_ ? "INSTALL" : "OPEN") : nullptr;
        return;
    }
    out[0] = "NEW";
    out[1] = "OPEN";
    out[2] = "SAVE";
    out[3] = "SAVE AS";
    out[4] = "MAC SAVE";
    out[5] = "MAC OPEN";
    out[6] = "FIRMWARE";
}

namespace {
struct ImageList {
    char (*names)[40];
    int count, max;
    bool (*accept)(const char*);
};
bool imageFn(const ot::IStorage::DirEntry& e, void* ctx) {
    ImageList* l = static_cast<ImageList*>(ctx);
    if (e.isDir || l->count >= l->max || std::strlen(e.name) >= 40 || (l->accept && !l->accept(e.name))) return true;
    std::snprintf(l->names[l->count++], 40, "%s", e.name);
    return true;
}
}  // namespace

void SettingsScreen::listImages() {
    imageCount_ = 0;
    const Platform& p = ui().app().platform();
    if (!p.storage) return;
    ImageList l{images_, 0, kMaxImages, p.isUpdateImageName};
    p.storage->listDir("/Firmware", imageFn, &l);
    imageCount_ = l.count;
}

void SettingsScreen::save(const char* name) {
    Ui& u = ui();
    std::string err;
    if (u.app().saveProject(name, &err)) {
        char m[64];
        std::snprintf(m, sizeof m, "Saved %.24s", name);
        u.status(m);
    } else {
        u.message(err.c_str());
    }
}

void SettingsScreen::saveMac(const char* name) {
    Ui& u = ui();
    std::string err;
    if (u.app().saveMacSession(name, &err)) {
        char m[64];
        std::snprintf(m, sizeof m, "Saved Mac session %.24s", name);
        u.status(m);
    } else {
        u.message(err.c_str());
    }
}

bool SettingsScreen::openPicked() {
    Ui& u = ui();
    std::string err;
    const bool ok = mac_ ? u.app().openMacSession(names_[pick_], &err) : u.app().openProject(names_[pick_], &err);
    if (ok) {
        open_ = false;
        u.status(mac_ ? "Mac session opened (not saved yet)" : "Project opened");
        u.show(ScreenId::kPattern);
    } else {
        u.message(err.c_str());
    }
    return ok;
}

void SettingsScreen::askChip() {
    Ui& u = ui();
    char q[72];
    std::snprintf(q, sizeof q, "Switch to %s?  (same song, other sound chip)", u.song().chip == atari::Chip::Pokey ? "TIA  2 tracks" : "POKEY  4 tracks");
    u.confirm(this, kActChip, q, "SWITCH");
}

bool SettingsScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    App& a = u.app();
    // results of the keyboard, the confirm dialog
    if (const char* name = u.keyboardResult(kActSaveAs)) {
        if (a.platform().storage && !u.confirming(kActOverwrite)) {
            char path[96];
            std::snprintf(path, sizeof path, "%s/%s.atr", App::kProjectDir, name);
            if (std::strcmp(name, a.projectFile()) && a.platform().storage->exists(path)) {
                std::snprintf(path, sizeof path, "%.24s exists. Replace it?", name);
                pendingName_[0] = 0;
                std::snprintf(pendingName_, sizeof pendingName_, "%s", name);
                u.confirm(this, kActOverwrite, path, "REPLACE");
                return true;
            }
        }
        save(name);
        return true;
    }
    if (const char* name = u.keyboardResult(kActProjName)) {
        a.setProjectName(name);
        u.status("Project named");
        return true;
    }
    if (const char* name = u.keyboardResult(kActMacName)) {
        char path[96];
        std::snprintf(path, sizeof path, "%s/%s.atari.json", App::kMacDir, name);
        if (a.platform().storage && a.platform().storage->exists(path)) {
            std::snprintf(pendingName_, sizeof pendingName_, "%s", name);
            std::snprintf(path, sizeof path, "%.24s exists. Replace it?", name);
            u.confirm(this, kActMacOverwrite, path, "REPLACE");
            return true;
        }
        saveMac(name);
        return true;
    }
    if (u.confirming(kActChip)) {
        a.engine.stop();
        u.song().chip = u.song().chip == atari::Chip::Pokey ? atari::Chip::Tia : atari::Chip::Pokey;
        a.applySongConfig();
        a.markDirty();
        u.status(u.song().chip == atari::Chip::Tia ? "TIA mode" : "POKEY mode");
        return true;
    }
    if (u.confirming(kActMacOverwrite)) {
        saveMac(pendingName_);
        return true;
    }
    if (u.confirming(kActOverwrite)) {
        save(pendingName_);
        return true;
    }
    if (u.confirming(kActNew)) {
        a.newProject();
        u.status("New song");
        return true;
    }
    if (u.confirming(kActInstall)) {
        const Platform& p = a.platform();
        if (a.dirty()) a.autosave();       // nothing is lost: the song comes back after the restart
        if (!p.requestFirmwareUpdate || !p.requestFirmwareUpdate(*p.storage, images_[pick_])) u.message("This image cannot be installed");
        return true;                       // on the device a successful request does not return
    }
    if (u.confirming(kActOpen)) {
        openPicked();
        return true;
    }
    if (open_) {
        switch (id) {
            case b::Up: if (pick_ > 0) --pick_; return true;
            case b::Down: if (pick_ + 1 < count_) ++pick_; return true;
            case b::F7: open_ = false; return true;
            case b::F8:
                if (!count_) return true;
                if (fw_) {
                    char q[96];
                    std::snprintf(q, sizeof q, "Install %.34s? The Tracker restarts.", images_[pick_]);
                    u.confirm(this, kActInstall, q, "INSTALL");
                    return true;
                }
                if (a.dirty()) u.confirm(this, kActOpen, "Unsaved changes will be lost. Open anyway?", "OPEN");
                else openPicked();
                return true;
            default: return true;   // keys the list does not use are ignored while it is open
        }
    }
    switch (id) {
        case b::F1:
            if (a.dirty()) u.confirm(this, kActNew, "Unsaved changes will be lost. Start a new song?", "NEW");
            else { a.newProject(); u.status("New song"); }
            return true;
        case b::F2:
            if (!a.platform().storage) { u.message("No card"); return true; }
            mac_ = false;
            fw_ = false;
            count_ = a.listProjects(names_, kMaxProjects);
            pick_ = 0;
            for (int i = 0; i < count_; ++i) if (!std::strcmp(names_[i], a.projectName())) pick_ = i;
            open_ = true;
            return true;
        case b::F7:
            if (!a.platform().storage || !a.platform().requestFirmwareUpdate) { u.message("No card or no firmware update here"); return true; }
            listImages();
            fw_ = true;
            mac_ = false;
            count_ = imageCount_;
            pick_ = 0;
            open_ = true;
            return true;
        case b::F5: u.keyboard(this, kActMacName, "NAME THE MAC SESSION", a.projectName(), true); return true;
        case b::F6:
            if (!a.platform().storage) { u.message("No card"); return true; }
            mac_ = true;
            fw_ = false;
            count_ = a.listMacSessions(names_, kMaxProjects);
            pick_ = 0;
            open_ = true;
            return true;
        case b::F3:
            if (!a.projectName()[0]) { u.keyboard(this, kActSaveAs, "NAME THE PROJECT", "", true); return true; }
            if (a.platform().storage && std::strcmp(a.projectName(), a.projectFile())) {   // a name typed here: do not silently replace another project
                char path[96];
                std::snprintf(path, sizeof path, "%s/%s.atr", App::kProjectDir, a.projectName());
                if (a.platform().storage->exists(path)) {
                    std::snprintf(pendingName_, sizeof pendingName_, "%s", a.projectName());
                    std::snprintf(path, sizeof path, "%.24s exists. Replace it?", pendingName_);
                    u.confirm(this, kActOverwrite, path, "REPLACE");
                    return true;
                }
            }
            save(a.projectName());
            return true;
        case b::F4: u.keyboard(this, kActSaveAs, "NAME THE PROJECT", a.projectName(), true); return true;
        case b::Up: if (row_ > 0) --row_; return true;
        case b::Down: if (row_ + 1 < kRows) ++row_; return true;
        case b::Delete:                              // on the name row: clear the name and type a new one
            if (row_ != kName) return false;
            a.setProjectName("");
            u.keyboard(this, kActProjName, "NAME THE PROJECT", "", true);
            return true;
        default: return false;
    }
}

void SettingsScreen::encoder(int d) {
    if (open_) {
        pick_ = pick_ + d < 0 ? 0 : (pick_ + d >= count_ ? (count_ ? count_ - 1 : 0) : pick_ + d);
        return;
    }
    Ui& u = ui();
    Song& s = u.song();
    switch (row_) {
        case kChip:
            s.chip = d > 0 ? atari::Chip::Tia : atari::Chip::Pokey;
            u.app().applySongConfig();
            u.app().markDirty();
            break;
        case kRegion:
            s.region = d > 0 ? atari::Region::PAL : atari::Region::NTSC;
            u.app().applySongConfig();
            u.app().markDirty();
            break;
        case kEngine:
            s.engineSpeed = uint8_t(d > 0 ? (s.engineSpeed < 2 ? 2 : 4) : (s.engineSpeed > 2 ? 2 : 1));
            u.app().engine.stop();
            u.app().applySongConfig();
            u.app().markDirty();
            break;
        case kTuning:
            s.tiaTune = d > 0 ? 1 : 0;
            u.app().applySongConfig();
            u.app().markDirty();
            break;
        case kVolume: {
            int v = u.masterVolume + d * 2;
            u.masterVolume = uint8_t(v < 0 ? 0 : (v > 100 ? 100 : v));
            u.app().engine.setMasterVolume(u.masterVolume);
            break;
        }
        case kMetro: u.metronome = d > 0; u.applyTransport(); u.app().saveSettings(); break;
        case kMetroVolume: {
            const int v = u.metroVolume + d * 5;
            u.metroVolume = uint8_t(v < 0 ? 0 : (v > 100 ? 100 : v));
            u.applyTransport();
            u.app().saveSettings();
            break;
        }
        case kPreroll: u.preroll = d > 0; u.app().saveSettings(); break;
        case kFunkKeep: u.funkKeepsTempo = d > 0; u.app().saveSettings(); break;
    }
}

void SettingsScreen::draw(ot::Gfx& g) {
    const Song& s = ui().song();
    char buf[48];
    if (open_) {
        if (fw_) {
            g.text(16, kContentY + 6, kLarge, col::kText, "INSTALL A FIRMWARE IMAGE");
            if (!count_) g.text(16, kContentY + 50, kMono, col::kDim, "No .ptf images in /Firmware on the card.");
            const int first = pick_ < 8 ? 0 : pick_ - 7;
            for (int i = 0; i < 9 && first + i < count_; ++i) {
                const bool atari = !std::strncmp(images_[first + i], "PolyendTracAtari", 16);
                listRow(g, 16, kContentY + 40 + i * 28, 640, images_[first + i], atari ? "Atari tracker" : "stock / other", first + i == pick_);
            }
            g.text(16, kBarY - 30, kMono, col::kDim, "Up / Down or jog: choose.  Keep the stock image here: it is the way back.");
            return;
        }
        g.text(16, kContentY + 6, kLarge, col::kText, mac_ ? "OPEN A MAC SESSION" : "OPEN A PROJECT");
        if (!count_) g.text(16, kContentY + 50, kMono, col::kDim, mac_ ? "No sessions in AtariTracker/Mac yet." : "No projects on the card yet. SAVE one first.");
        const int first = pick_ < 8 ? 0 : pick_ - 7;
        for (int i = 0; i < 9 && first + i < count_; ++i) listRow(g, 16, kContentY + 40 + i * 28, 560, names_[first + i], "", first + i == pick_);
        g.text(16, kBarY - 30, kMono, col::kDim, "Up / Down or jog: choose a file");
        return;
    }
    int y = kContentY + 60;
    g.text(16, kContentY + 8, kLarge, col::kText, "SETTINGS");
    g.text(16, kContentY + 36, kMono, ui().app().dirty() ? col::kWarn : col::kDim, ui().app().dirty() ? "unsaved changes (autosaved to the card)" : "saved");
    listRow(g, 16, y - 28, 560, "PROJECT NAME", ui().app().projectName()[0] ? ui().app().projectName() : "(not named yet)  Delete: type one", row_ == kName);
    listRow(g, 16, y, 560, "CHIP", s.chip == atari::Chip::Pokey ? "POKEY  (4 tracks, Atari 7800 cartridge)" : "TIA  (2 tracks, Atari 2600 / 7800)", row_ == kChip);
    listRow(g, 16, y + 28, 560, "REGION", s.region == atari::Region::NTSC ? "NTSC  60 Hz" : "PAL  50 Hz", row_ == kRegion);
    listRow(g, 16, y + 56, 560, "TIA TUNING", s.tiaTune ? "OPTIMISED  (+-50 cents)" : "CONCERT  A = 440", row_ == kTuning);
    std::snprintf(buf, sizeof buf, "%u %%", unsigned(ui().masterVolume));
    listRow(g, 16, y + 84, 560, "MASTER VOLUME", buf, row_ == kVolume);
    listRow(g, 16, y + 112, 560, "METRONOME", ui().metronome ? "ON" : "OFF", row_ == kMetro);
    std::snprintf(buf, sizeof buf, "%u %%", unsigned(ui().metroVolume));
    listRow(g, 16, y + 140, 560, "METRONOME VOLUME", buf, row_ == kMetroVolume);
    listRow(g, 16, y + 168, 560, "PRE-ROLL (Rec + Play)", ui().preroll ? "1 BAR OF CLICKS" : "OFF", row_ == kPreroll);
    listRow(g, 16, y + 224, 560, "FUNK KEEPS TEMPO", ui().funkKeepsTempo ? "ON  (FUNK -/+ trade speed for funk)" : "OFF", row_ == kFunkKeep);
    {
        const char* v = s.engineSpeed == 4 ? "4 TICKS PER FRAME  (the playable ROMs need 1)" : (s.engineSpeed == 2 ? "2 TICKS PER FRAME  (the playable ROMs need 1)" : "1 TICK PER FRAME");
        listRow(g, 16, y + 196, 560, "ENGINE SPEED", v, row_ == kEngine);
    }
    g.text(16, kBarY - 30, kMono, col::kDim, "Up / Down: row   jog: value   on the name row, Delete clears it and asks for a new one");
}

}  // namespace ui
}  // namespace atw
