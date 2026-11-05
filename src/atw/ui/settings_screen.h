// Atari tracker -- Settings (Config): chip, region, master volume, and the project (new, open, save, save as).
#pragma once

#include "atw/atsong.h"
#include "atw/ui/screen.h"

namespace atw {
namespace ui {

class SettingsScreen final : public Screen {
public:
    const char* title() const override { return "SETTINGS"; }
    void draw(ot::Gfx& g) override;
    void labels(const char* out[8]) override;
    void askChip();     // the Sample playback key: offer the other sound chip (F8 / jog push to switch)
    bool button(uint8_t id, bool down) override;
    void encoder(int delta) override;

    void enter() override { open_ = false; }

private:
    enum Row { kName, kChip, kRegion, kTuning, kVolume, kMetro, kMetroVolume, kPreroll, kEngine, kFunkKeep, kRows };
    int row_ = 0;
    // The project list (F2) or the Mac session list (F6): up to 32 files on the card; F8 opens the highlighted one.
    static constexpr int kMaxProjects = 32;
    bool open_ = false;
    bool fw_ = false;                  // the list shows the firmware images in /Firmware (F7)
    bool mac_ = false;                 // the list shows Mac sessions (F6) instead of projects (F2)
    int count_ = 0, pick_ = 0;
    char names_[kMaxProjects][kNameLen + 1];
    char pendingName_[kNameLen + 1] = {};
    static constexpr int kMaxImages = 12;
    int imageCount_ = 0;
    char images_[kMaxImages][40];       // file names are longer than a project name ("PolyendTracker_CE_1.0.0.ptf")
    void listImages();
    void save(const char* name);
    void saveMac(const char* name);
    bool openPicked();                 // loads the highlighted entry of the list
};

}  // namespace ui
}  // namespace atw
