// Sample kits on the Tracker: Sample Loader (WAV files from the card), Sample Editor (trim, name, add to a kit), Sample Playback (browse and
// edit the kit's samples). The kit being edited and the sample being prepared live in 16 KB buffers owned by the Transport.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "gb/kitbank.h"
#include "gb/ui/lsdj.h"
#include "gb/wavconv.h"

namespace gb {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

uint8_t* Lsdj::kitBuf() const { return tr_ ? tr_->kitBuf(console_) : nullptr; }
bool Lsdj::kitEditable() const { return kitSlot >= kKitFirstUserSlot && kitSlot <= kKitLastUserSlot; }

// Takes what the console plays for this kit number into the edit buffer.
void Lsdj::kitSelect(int slot) {
    kitSlot = clampi(slot, 0, kKitLastUserSlot);
    if (tr_) tr_->kitOpen(console_, kitSlot);
    kitRow = std::min(kitRow, 2 + std::max(0, kKitMaxSamples - 1));
}

void Lsdj::openLoader() {
    if (!tr_) return;
    smpCount = tr_->listSamples();
    smpSel = clampi(smpSel, 0, std::max(0, smpCount - 1));
    status_ = "";
    scr_ = Scr::Loader;
}

void Lsdj::openEditor() {
    if (!tr_) return;
    kitSelect(kitSlot);
    smpRow = std::min(smpRow, 4);
    status_ = "";
    scr_ = Scr::Editor;
}

void Lsdj::openKit() {
    if (!tr_) return;
    kitSelect(kitSlot);
    status_ = "";
    scr_ = Scr::Kit;
}

// Converts the listed WAV into the sample buffer: 4-bit, the kit's rate, trimmed to nothing yet. Its first three letters become the sample's name.
bool Lsdj::sampleLoad(bool toEditor) {
    if (!tr_ || !smpCount) { status_ = "NO WAV FILES"; return false; }
    bool cut = false;
    const int n = tr_->sampleLoad(smpSel, &cut);
    if (n <= 0) { smpLen = 0; smpLoaded_ = -1; status_ = tr_->errorText() ? tr_->errorText() : "CAN'T READ THAT WAV"; return false; }
    smpLen = n;
    smpLoaded_ = smpSel;
    smpStart = 0;
    smpEnd = n / 16;
    const char* f = tr_->sampleName(smpSel);
    int k = 0;
    for (; *f && k < 3; ++f) { const char c = *f >= 'a' && *f <= 'z' ? char(*f - 32) : *f; if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) smpName_[k++] = c; }
    for (; k < 3; ++k) smpName_[k] = '-';
    smpName_[3] = 0;
    std::snprintf(statusBuf_, sizeof statusBuf_, "LOADED %.1fS%s", double(nibbleSeconds(n)), cut ? " (CUT)" : "");
    status_ = statusBuf_;
    if (toEditor) { kitSelect(kitSlot); smpRow = 0; scr_ = Scr::Editor; }
    return true;
}

// Play on these pages: hear the listed WAV, the trimmed sample, or the kit's sample under the cursor.
void Lsdj::kitAudition() {
    if (!tr_) return;
    if (scr_ == Scr::Loader) {
        if (smpLoaded_ != smpSel && !sampleLoad(false)) return;
        p_.auditionData(tr_->sampleBuf(), smpLen);
    } else if (scr_ == Scr::Editor) {
        if (!smpLen) { status_ = "LOAD A SAMPLE FIRST"; return; }
        p_.auditionData(tr_->sampleBuf() + smpStart * 16, (smpEnd - smpStart) * 16);
    } else if (uint8_t* kb = kitBuf()) {
        const int i = kitRow >= 2 ? kitRow - 2 : 0;
        if (i < KitBank::count(kb)) p_.auditionData(KitBank::data(kb, i), KitBank::length(kb, i));
        else status_ = "NO SAMPLE HERE";
    }
}

void Lsdj::kitMove(Key k) {
    const int dy = k == Key::Down ? 1 : k == Key::Up ? -1 : 0;
    if (scr_ == Scr::Loader) smpSel = clampi(smpSel + dy, 0, std::max(0, smpCount - 1));
    else if (scr_ == Scr::Editor) smpRow = clampi(smpRow + dy, 0, 4);
    else kitRow = clampi(kitRow + dy, 0, 16);
}

// A + direction or the jog wheel.
void Lsdj::kitEdit(Key k) {
    const int big = k == Key::Up || k == Key::Down, sgn = (k == Key::Right || k == Key::Up) ? 1 : -1;
    if (scr_ == Scr::Editor) {
        const int chunks = smpLen / 16, d = sgn * (big ? 16 : 1);
        if (smpRow == 0) smpStart = clampi(smpStart + d, 0, std::max(0, smpEnd - 2));
        else if (smpRow == 1) smpEnd = clampi(smpEnd + d, std::min(chunks, smpStart + 2), chunks);
        else if (smpRow == 3) kitSelect(kitSlot + sgn * (big ? 8 : 1));
    } else if (scr_ == Scr::Kit && kitRow == 0) kitSelect(kitSlot + sgn * (big ? 8 : 1));
}

// Insert: load the WAV, name something, add the sample to the kit.
void Lsdj::kitPress() {
    if (scr_ == Scr::Loader) { sampleLoad(true); return; }
    if (scr_ == Scr::Editor) {
        if (smpRow == 2) openName(3);
        else if (smpRow == 4) kitAdd();
        return;
    }
    if (kitRow == 1) { if (kitEditable()) openName(4); else status_ = "ROM KIT: NO RENAME"; }
    else if (kitRow >= 2) kitToEditor();
}

// Insert on a sample of any kit (the ROM's too): it opens in the Sample Editor, ready to cut and add to one of your kits.
void Lsdj::kitToEditor() {
    uint8_t* kb = kitBuf();
    uint8_t* sb = tr_ ? tr_->sampleBuf() : nullptr;
    const int i = kitRow - 2;
    if (!kb || !sb || i < 0 || i >= KitBank::count(kb)) { status_ = "NO SAMPLE HERE"; return; }
    int n = KitBank::length(kb, i);
    if (n >= 48) n -= 16;                                            // the silent chunk that ends it comes back when it is added
    std::memcpy(sb, KitBank::data(kb, i), size_t(n));
    smpLen = n;
    smpLoaded_ = -1;
    smpStart = 0;
    smpEnd = n / 16;
    KitBank::name(kb, i, smpName_);
    if (!kitEditable()) kitSelect(kKitFirstUserSlot);                // a ROM sample goes into your first kit by default
    smpRow = 0;
    status_ = "COPIED TO THE EDITOR";
    scr_ = Scr::Editor;
}

void Lsdj::kitRename() {
    uint8_t* kb = kitBuf();
    if (kitRow < 2 || !kb || !kitEditable() || kitRow - 2 >= KitBank::count(kb)) { status_ = kitEditable() ? "NOTHING TO RENAME" : "ROM KIT: NO RENAME"; return; }
    openName(5);
}

void Lsdj::kitAdd() {
    uint8_t* kb = kitBuf();
    uint8_t* sb = tr_ ? tr_->sampleBuf() : nullptr;
    if (!kb || !sb || !smpLen) { status_ = "LOAD A SAMPLE FIRST"; return; }
    if (!kitEditable()) { status_ = "KITS 1A TO 37 ONLY"; return; }
    const int n = (smpEnd - smpStart) * 16;
    if (n < 32) { status_ = "SAMPLE TOO SHORT"; return; }
    if (KitBank::count(kb) >= kKitMaxSamples) { status_ = "KIT HAS 15 SAMPLES"; return; }
    if (!KitBank::add(kb, smpName_, sb + smpStart * 16, n)) { status_ = "NO ROOM LEFT IN KIT"; return; }
    char kn[9];
    KitBank::kitName(kb, kn);
    if (kn[0] == ' ') { std::snprintf(kn, sizeof kn, "KIT%02X", unsigned(kitSlot)); KitBank::setKitName(kb, kn); }
    if (tr_->kitSave(console_, kitSlot)) {
        std::snprintf(statusBuf_, sizeof statusBuf_, "ADDED: NOTE %X IN KIT %02X", unsigned(KitBank::count(kb)), unsigned(kitSlot));
        status_ = statusBuf_;
    } else status_ = tr_->errorText() ? tr_->errorText() : "SAVE FAILED";
}

void Lsdj::kitErase() {
    uint8_t* kb = kitBuf();
    const int i = kitRow - 2;
    if (!kb || !kitEditable() || i < 0 || !KitBank::remove(kb, i)) { status_ = "NOTHING TO ERASE"; return; }
    status_ = tr_->kitSave(console_, kitSlot) ? "ERASED" : (tr_->errorText() ? tr_->errorText() : "SAVE FAILED");
}

void Lsdj::openFirmware() {
    if (!tr_) return;
    fwCount = tr_->listFirmware();
    fwSel = 0;
    status_ = "";
    scr_ = Scr::Firmware;
}

// ---- drawing -------------------------------------------------------------------------------------------------------------------

void Lsdj::drawLoader(Lcd& l) const {
    l.boldText(0, 0, console_ ? "SAMPLE LOADER B" : "SAMPLE LOADER A");
    l.text(0, 1, status_[0] ? status_ : "INSERT=LOAD  PLAY=HEAR");
    if (!smpCount) { l.text(1, 3, "NO WAV FILES YET."); l.text(1, 4, "PUT THEM IN GB/SAMPLES"); l.text(1, 5, "ON THE SD CARD."); return; }
    const int rows = 15, top = std::max(0, std::min(smpSel - rows / 2, smpCount - rows));
    for (int r = 0; r < rows && top + r < smpCount; ++r) {
        char s[40];
        std::snprintf(s, sizeof s, " %-23.23s", tr_ ? tr_->sampleName(top + r) : "");
        l.text(0, 3 + r, s, top + r == smpSel);
    }
}

void Lsdj::drawEditor(Lcd& l) const {
    l.boldText(0, 0, "SAMPLE EDITOR");
    const uint8_t* sb = tr_ ? tr_->sampleBuf() : nullptr;
    if (!smpLen || !sb) {
        l.text(0, 2, "NO SAMPLE YET.");
        l.text(0, 3, "USE SAMPLE LOADER FIRST");
    } else {
        char b[40];
        std::snprintf(b, sizeof b, "%.2f SECONDS", double(nibbleSeconds(smpLen)));
        l.text(0, 1, b);
        const int n = smpLen * 2, x0 = 20;
        for (int x = 0; x < 160; ++x) {                                   // the wave: the lowest and highest nibble under each column
            const int a = int(int64_t(x) * n / 160), e = std::max(a + 1, int(int64_t(x + 1) * n / 160));
            int lo = 15, hi = 0;
            for (int i = a; i < e && i < n; ++i) { const int v = (i & 1) ? sb[i >> 1] & 15 : sb[i >> 1] >> 4; lo = std::min(lo, v); hi = std::max(hi, v); }
            const int chunk = a / 32;
            const bool inside = chunk >= smpStart && chunk < smpEnd;
            const int y0 = 24 + (15 - hi) * 3, y1 = 24 + (15 - lo) * 3 + 2;
            l.rect(x0 + x, y0, 1, y1 - y0 + 1, inside ? 3 : 1);
        }
        const int mx0 = x0 + int(int64_t(smpStart) * 32 * 160 / n), mx1 = std::min(x0 + 159, x0 + int(int64_t(smpEnd) * 32 * 160 / n));
        l.rect(mx0, 20, 1, 52, 3);
        l.rect(mx1, 20, 1, 52, 3);
    }
    char b[40];
    std::snprintf(b, sizeof b, "START %6.2fS", double(nibbleSeconds(smpStart * 16)));
    l.text(0, 10, b, smpRow == 0);
    std::snprintf(b, sizeof b, "END   %6.2fS", double(nibbleSeconds(smpEnd * 16)));
    l.text(0, 11, b, smpRow == 1);
    std::snprintf(b, sizeof b, "NAME  %s", smpName_);
    l.text(0, 12, b, smpRow == 2);
    const uint8_t* kb = kitBuf();
    std::snprintf(b, sizeof b, "KIT   %02X %s", unsigned(kitSlot), kitEditable() ? "" : "(ROM KIT)");
    l.text(0, 13, b, smpRow == 3);
    if (kb && kitEditable()) {
        std::snprintf(b, sizeof b, "%d/15 SAMPLES %.1fS FREE", KitBank::count(kb), double(nibbleSeconds(KitBank::freeBytes(kb))));
        l.text(0, 14, b);
    }
    l.text(0, 15, "ADD TO KIT", smpRow == 4);
    l.text(0, 17, status_[0] ? status_ : "PLAY=HEAR  JOG=CHANGE");
}

// The firmware loader window: the images in /Firmware that the bootloader accepts. Insert picks one, a second Insert flashes it.
void Lsdj::drawFirmware(Lcd& l) const {
    l.frame(0, 0, 200, 144, 3);
    l.frame(2, 2, 196, 140, 3);
    l.boldText(2, 1, "LOAD FIRMWARE");
#ifdef GB_VERSION
    l.text(2, 2, "NOW V" GB_VERSION);
#endif
    if (!fwCount) { l.text(2, 4, "NO UPDATE FILES IN"); l.text(2, 5, "THE FIRMWARE FOLDER."); }
    const int rows = 11, top = std::max(0, std::min(fwSel - rows / 2, fwCount - rows));
    for (int r = 0; r < rows && top + r < fwCount; ++r) {
        char s[40];
        std::snprintf(s, sizeof s, "%s", tr_ ? tr_->firmwareName(top + r) : "");
        const size_t n = std::strlen(s);
        if (n > 4 && s[n - 4] == '.') s[n - 4] = 0;                  // the .ptf adds nothing and the name has to fit inside the window
        char row[40];
        std::snprintf(row, sizeof row, "%-22.22s", s);
        l.text(2, 4 + r, row, top + r == fwSel);
    }
    l.text(2, 16, status_[0] ? status_ : "INSERT=PICK  BKSP=BACK");
}

void Lsdj::drawKit(Lcd& l) const {
    const uint8_t* kb = kitBuf();
    l.boldText(0, 0, "KIT");
    char b[40];
    std::snprintf(b, sizeof b, "%02X", unsigned(kitSlot));
    l.text(4, 0, b, kitRow == 0);
    l.text(7, 0, console_ ? "B" : "A");
    if (!kitEditable()) l.text(10, 0, "ROM KIT");
    char kn[9] = "        ";
    if (kb && KitBank::hasSamples(kb)) KitBank::kitName(kb, kn);
    std::snprintf(b, sizeof b, "NAME %s", kn);
    l.text(0, 1, b, kitRow == 1);
    const int count = kb ? KitBank::count(kb) : 0;
    for (int i = 0; i < kKitMaxSamples; ++i) {
        if (i < count) {
            char nm[4];
            KitBank::name(kb, i, nm);
            std::snprintf(b, sizeof b, "%X %s %5.2fS", i + 1, nm, double(nibbleSeconds(KitBank::length(kb, i))));
        } else std::snprintf(b, sizeof b, "%X --", i + 1);
        l.text(0, 2 + i, b, kitRow == 2 + i);
    }
    if (kb && count && kitEditable()) { std::snprintf(b, sizeof b, "FREE %.1fS", double(nibbleSeconds(KitBank::freeBytes(kb)))); l.text(15, 17, b); }
    l.text(0, 17, status_[0] ? status_ : count ? "PLAY=HEAR" : "NO KIT HERE");
}

}  // namespace gb
