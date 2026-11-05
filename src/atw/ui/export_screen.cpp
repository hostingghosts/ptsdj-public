#include "atw/ui/export_screen.h"

#include <cstdio>
#include <cstring>

#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

namespace {
constexpr uint8_t kActReplace = 208, kActName = 209;
constexpr int kNameRow = 0;   // the first row is the project name; the exports follow it
struct Item {
    ExportKind kind;
    bool pattern;   // just the pattern being edited, not the whole order list
    const char* name;
    const char* help;
};
const Item kItems[] = {
    {ExportKind::kWav, false, "WAV  SONG", "the whole song (every order entry and repeat) played once, 44.1 kHz mono"},
    {ExportKind::kWav, true, "WAV  THIS PATTERN", "only the pattern you are editing, as a seamless loop"},
    {ExportKind::kAsm, false, "ASSEMBLY (DASM)  SONG", "the whole song + 6502 player + README, for a game"},
    {ExportKind::kAsm, true, "ASSEMBLY (DASM)  THIS PATTERN", "only the pattern you are editing"},
    {ExportKind::kA78, false, "ATARI 7800  .a78  SONG", "a playable cartridge image (POKEY at $4000, or TIA)"},
    {ExportKind::kA78, true, "ATARI 7800  .a78  THIS PATTERN", "a cartridge that loops the pattern you are editing"},
    {ExportKind::kBin2600, false, "ATARI 2600  .bin  SONG", "a playable 4K ROM, TIA only (switch the chip in Config)"},
    {ExportKind::kBin2600, true, "ATARI 2600  .bin  THIS PATTERN", "a 4K ROM that loops the pattern you are editing"},
};
constexpr int kCount = int(sizeof kItems / sizeof kItems[0]);
}  // namespace

void ExportScreen::labels(const char* out[8]) {
    if (ui().app().exporter.busy()) {
        out[6] = "CANCEL";
        return;
    }
    out[5] = "NAME";
    out[7] = "EXPORT";
}

const char* ExportScreen::exportName() const {
    return ui().app().projectName()[0] ? ui().app().projectName() : "Atari song";
}

void ExportScreen::run() {
    Ui& u = ui();
    App& a = u.app();
    ot::IStorage* fs = a.platform().storage;
    if (fs && !u.confirming(kActReplace)) {
        char path[96], q[96];
        a.exporter.targetPath(kItems[row_ - 1].kind, exportName(), kItems[row_ - 1].pattern ? int(u.pattern) : -1, path, sizeof path);
        if (fs->exists(path)) {
            std::snprintf(q, sizeof q, "%s exists. Replace it?", path + std::strlen("AtariTracker/Export/"));
            u.confirm(this, kActReplace, q, "REPLACE");
            return;
        }
    }
    doRun();
}

void ExportScreen::doRun() {
    Ui& u = ui();
    App& a = u.app();
    a.engine.stop();
    if (!a.exporter.start(kItems[row_ - 1].kind, exportName(), kItems[row_ - 1].pattern ? int(u.pattern) : -1)) u.message(a.exporter.message(), 3000);
    else if (!a.exporter.busy()) u.message(a.exporter.message(), 3000);
}

bool ExportScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Exporter& x = ui().app().exporter;
    if (x.busy()) {
        if (id == b::F7) x.cancel();
        return true;
    }
    if (ui().confirming(kActReplace)) {
        doRun();
        return true;
    }
    if (const char* name = ui().keyboardResult(kActName)) {
        ui().app().setProjectName(name);
        return true;
    }
    switch (id) {
        case b::F6: ui().keyboard(this, kActName, "NAME THE PROJECT", ui().app().projectName(), true); return true;
        case b::Up: if (row_ > 0) --row_; return true;
        case b::Down: if (row_ < kCount) ++row_; return true;
        case b::Delete:                                      // on the name row: clear the name and type a new one
            if (row_ != kNameRow) return false;
            ui().app().setProjectName("");
            ui().keyboard(this, kActName, "NAME THE PROJECT", "", true);
            return true;
        case b::F8:
        case b::EncPush:
            if (row_ != kNameRow) run();
            return true;
        default: return false;
    }
}

void ExportScreen::encoder(int d) {
    if (ui().app().exporter.busy()) return;
    row_ = row_ + d < 0 ? 0 : (row_ + d > kCount ? kCount : row_ + d);
}

void ExportScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    const Exporter& x = u.app().exporter;
    const char* name = exportName();
    char buf[96];
    std::snprintf(buf, sizeof buf, "Export \"%.24s\"  (%s, %s)", name, u.song().chip == atari::Chip::Pokey ? "POKEY" : "TIA",
                  u.song().region == atari::Region::NTSC ? "NTSC" : "PAL");
    g.text(16, kContentY + 6, kMono, col::kDim, buf);
    char pat[40];
    std::snprintf(pat, sizeof pat, "pattern %02X is the one edited", unsigned(u.pattern));
    g.text(16, kBarY - 30, kMono, col::kDim, pat);
    listRow(g, 16, kContentY + 36, 560, "NAME", u.app().projectName()[0] ? u.app().projectName() : "(not named yet)  Delete: type one", row_ == kNameRow);
    for (int i = 0; i < kCount; ++i) listRow(g, 16, kContentY + 36 + (i + 1) * 28, 560, kItems[i].name, "", i + 1 == row_);
    g.text(16, kContentY + 36 + (kCount + 1) * 28 + 6, kMono, col::kDim, row_ == kNameRow ? "the files are named after this (same name as in Config). Delete: clear it and type a new one" : kItems[row_ - 1].help);
    if (x.busy()) {
        g.fill(16, kContentY + 340, 560, 22, col::kPanel);
        g.fill(16, kContentY + 340, 560 * x.percent() / 100, 22, col::kGood);
        std::snprintf(buf, sizeof buf, "Rendering %d %%   CANCEL stops it", x.percent());
        g.text(16, kContentY + 368, kMono, col::kText, buf);
    } else if (x.message()[0]) {
        g.text(16, kContentY + 340, kMono, x.lastPath()[0] ? col::kGood : col::kWarn, x.message());
    }
}

}  // namespace ui
}  // namespace atw
