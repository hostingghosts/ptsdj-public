#include "atw/ui/instrument_screen.h"

#include <cstdio>
#include <cstring>

#include "atari/instrument.h"
#include "atari/presets.h"
#include "atw/app.h"
#include "ui/buttons.h"

namespace atw {
namespace ui {

using atari::Field;

namespace {
constexpr int kName = -1;
// Pseudo fields of the ADSR shaping on the envelope page (they live in Song::adsr, not in the instrument editor's staged values).
enum : int { kAdsrMode = 100, kAdsrAttack, kAdsrDecay, kAdsrSustain, kAdsrPeak, kAdsrTone, kAdsrCurve, kAdsrRelease };
const char* adsrName(int f) {
    switch (f) {
        case kAdsrMode: return "ENVELOPE";
        case kAdsrAttack: return "ATTACK";
        case kAdsrDecay: return "DECAY";
        case kAdsrSustain: return "SUSTAIN";
        case kAdsrPeak: return "PEAK";
        case kAdsrTone: return "TONE";
        case kAdsrCurve: return "CURVE";
        default: return "RELEASE (WHOLE SONG)";
    }
}
const char* adsrHelp(int f) {
    switch (f) {
        case kAdsrMode: return "MANUAL: edit the steps.  ADSR: attack, decay, sustain and release shape the volume.";
        case kAdsrAttack: return "Frames (1/60 s) to rise from silence to the peak. 0 = at once.";
        case kAdsrDecay: return "Frames to fall from the peak to the sustain level.";
        case kAdsrSustain: return "Volume held until the note ends (note off or the next note).";
        case kAdsrPeak: return "Loudest volume, reached at the end of the attack.";
        case kAdsrTone: return "Distortion of every step (pure tone, noise, buzz ...).";
        case kAdsrCurve: return "Shape of the attack and decay: linear, fast start or slow start.";
        default: return "Frames the sound fades out after note off (one value for the whole song).";
    }
}
const char* const kTones[] = {"POLY 5+17 (0)", "POLY 5 (2)", "POLY 5+4 (4)", "(6)", "NOISE 17 (8)", "PURE (A)", "BUZZ (C)", "GRITTY (E)"};
const int kPokeyAdsr[] = {kAdsrMode, kAdsrAttack, kAdsrDecay, kAdsrSustain, kAdsrPeak, kAdsrTone, kAdsrCurve, kAdsrRelease};
const int kPokeyGeneral[] = {atari::F_INSTRUMENT, kName, atari::F_FADEOUT, atari::F_VOLMIN, atari::F_VIB_DELAY, atari::F_VIB_DEPTH, atari::F_FSHIFT, atari::F_AUTOMOD, atari::F_BUZZ_STYLE, atari::F_FILTER_SWEEP};
const int kPokeyEnvelope[] = {kAdsrMode, atari::F_ENV_STEP, atari::F_ENV_VOL, atari::F_ENV_DIST, atari::F_ENV_CMD, atari::F_ENV_X, atari::F_ENV_Y,
                              atari::F_ENV_FILTER, atari::F_ENV_PORTA, atari::F_ENV_LENGTH, atari::F_ENV_LOOP};
const int kPokeyTable[] = {atari::F_TAB_STEP, atari::F_TAB_VALUE, atari::F_TAB_LENGTH, atari::F_TAB_LOOP, atari::F_TAB_SPEED, atari::F_TAB_MODE, atari::F_TAB_OP};
const int kPokeyAudctl[] = {atari::F_AUDCTL_15K, atari::F_AUDCTL_HPF13, atari::F_AUDCTL_HPF24, atari::F_AUDCTL_JOIN34,
                            atari::F_AUDCTL_JOIN12, atari::F_AUDCTL_CH3FAST, atari::F_AUDCTL_CH1FAST, atari::F_AUDCTL_POLY9};
const int kTiaGeneral[] = {atari::F_INSTRUMENT, kName, atari::F_TIA_FRAMES, atari::F_TIA_OVERLAY, atari::F_TIA_COMBINED};
const int kTiaSteps[] = {atari::F_TIA_STEP, atari::F_TIA_WAVE, atari::F_TIA_DIVIDER, atari::F_TIA_VOL, atari::F_TIA_PITCH, atari::F_TIA_LENGTH, atari::F_TIA_LOOP};
const char* const kPokeyPages[] = {"GENERAL", "ENVELOPE", "TABLE", "AUDCTL", "SOUNDS"};
const char* const kTiaPages[] = {"GENERAL", "STEPS", "SOUNDS"};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

// --- structure ---------------------------------------------------------------------

int InstrumentScreen::pageCount() const { return chip_ == atari::Chip::Pokey ? 5 : 3; }
const char* InstrumentScreen::pageName(int p) const { return chip_ == atari::Chip::Pokey ? kPokeyPages[p] : kTiaPages[p]; }
bool InstrumentScreen::soundsPage() const { return page_ == pageCount() - 1; }
bool InstrumentScreen::audctlPage() const { return chip_ == atari::Chip::Pokey && page_ == 3; }

// ADSR counts as on only while the envelope is still the one the settings make: a preset, a paste or a manual step edit turns it off.
bool InstrumentScreen::adsrActive(int slot) const {
    const Song& s = ui().song();
    const Adsr& a = s.adsr[slot];
    if (!a.on) return false;
    uint8_t vol[32];
    const int n = adsrSteps(a, vol);
    const atari::PokeyInstrument& in = s.pokey[slot];
    if (int(in.env.size()) != n || in.envGoto != n - 1) return false;
    for (int i = 0; i < n; ++i)
        if (in.env[size_t(i)].volL != vol[i] || in.env[size_t(i)].dist != a.dist) return false;
    return true;
}

void InstrumentScreen::applyAdsr(int slot) {
    Song& s = ui().song();
    uint8_t vol[32];
    const int n = adsrSteps(s.adsr[slot], vol);
    atari::PokeyInstrument& in = s.pokey[slot];
    in.env.assign(size_t(n), atari::EnvStep{});
    for (int i = 0; i < n; ++i) {
        in.env[size_t(i)].volL = in.env[size_t(i)].volR = vol[i];
        in.env[size_t(i)].dist = s.adsr[slot].dist;
    }
    in.envGoto = n - 1;
}

void InstrumentScreen::changeAdsr(int field, int d) {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    Adsr& a = s.adsr[slot];
    AudioEngine& e = u.app().engine;
    if (field == kAdsrRelease) {
        s.releaseFrames = uint8_t(clampi(s.releaseFrames + d, 0, 15));
        u.app().applySongConfig();
        u.app().markDirty();
        u.preview(u.inst);
        return;
    }
    e.hold();
    switch (field) {
        case kAdsrMode: {
            const bool on = d > 0;
            if (on && !adsrActive(slot)) {              // take over the sound as it is: read attack, decay, sustain and peak off the current steps
                const atari::PokeyInstrument& in = s.pokey[slot];
                a = Adsr{};
                a.on = true;
                if (!in.env.empty()) {
                    int pk = 1, ip = 0;
                    for (size_t i = 0; i < in.env.size(); ++i)
                        if (in.env[i].volL > pk) { pk = in.env[i].volL; ip = int(i); }
                    const int at = ip == 0 ? 0 : ip + 1;
                    const int sus = in.env[size_t(in.envGoto)].volL;
                    a.peak = uint8_t(pk);
                    a.sustain = uint8_t(sus > pk ? pk : sus);
                    a.attack = uint8_t(clampi(at, 0, 15));
                    a.decay = uint8_t(clampi(in.envGoto - (at == 0 ? 1 : at), 0, 15));
                    a.dist = uint8_t(in.env[size_t(ip)].dist & 14);
                }
            }
            else if (!on) a.on = false;
            break;
        }
        case kAdsrAttack: a.attack = uint8_t(clampi(a.attack + d, 0, 15)); break;
        case kAdsrDecay: a.decay = uint8_t(clampi(a.decay + d, 0, 15)); break;
        case kAdsrSustain: a.sustain = uint8_t(clampi(a.sustain + d, 0, 15)); break;
        case kAdsrPeak: a.peak = uint8_t(clampi(a.peak + d, 1, 15)); break;
        case kAdsrTone: a.dist = uint8_t(clampi(a.dist + 2 * d, 0, 14)); break;
        case kAdsrCurve: a.curve = uint8_t(clampi(a.curve + d, 0, 2)); break;
        default: break;
    }
    if (a.on) applyAdsr(slot);
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    row_ = clampi(row_, 0, 0 + 9);
    u.app().markDirty();
    u.preview(u.inst);
}

int InstrumentScreen::rows(const int*& list) const {
    if (chip_ == atari::Chip::Pokey) {
        switch (page_) {
            case 0: list = kPokeyGeneral; return int(sizeof kPokeyGeneral / sizeof(int));
            case 1:
                if (adsrActive(ui().inst - 1)) { list = kPokeyAdsr; return int(sizeof kPokeyAdsr / sizeof(int)); }
                list = kPokeyEnvelope;
                return int(sizeof kPokeyEnvelope / sizeof(int));
            case 2: list = kPokeyTable; return int(sizeof kPokeyTable / sizeof(int));
            case 3: list = kPokeyAudctl; return int(sizeof kPokeyAudctl / sizeof(int));
            default: list = nullptr; return 0;
        }
    }
    switch (page_) {
        case 0: list = kTiaGeneral; return int(sizeof kTiaGeneral / sizeof(int));
        case 1: list = kTiaSteps; return int(sizeof kTiaSteps / sizeof(int));
        default: list = nullptr; return 0;
    }
}

void InstrumentScreen::sync() {
    Ui& u = ui();
    Song& s = u.song();
    if (!bound_ || chip_ != s.chip) {
        ed_.bind(s.pokey, s.tia);
        bound_ = true;
        chip_ = s.chip;
        page_ = clampi(page_, 0, pageCount() - 1);
        ed_.stage[atari::F_INSTRUMENT] = uint8_t(u.inst - 1);
        ed_.load(chip_);
    }
    if (ed_.stage[atari::F_INSTRUMENT] != u.inst - 1) {
        if (trial_) cancelSound();
        ed_.stage[atari::F_INSTRUMENT] = uint8_t(u.inst - 1);
        ed_.load(chip_);
    }
}

void InstrumentScreen::enter() {
    bound_ = false;
    sync();
    if (page_ == pageCount() - 1) scanCard();
}

void InstrumentScreen::leave() {
    if (trial_) {
        cancelSound();
        ui().status("Sound not kept");
    }
}

void InstrumentScreen::changeInstrument(int d) {
    Ui& u = ui();
    if (trial_) cancelSound();
    u.inst = uint8_t(clampi(u.inst + d, 1, kInstrumentCount));
    row_ = clampi(row_, 0, 63);
    sync();
}

// --- values ------------------------------------------------------------------------

void InstrumentScreen::valueText(int field, char* out, size_t cap) {
    if (field >= kAdsrMode) {
        const Song& s = ui().song();
        const Adsr& a = s.adsr[ui().inst - 1];
        const bool on = adsrActive(ui().inst - 1);
        switch (field) {
            case kAdsrMode: std::snprintf(out, cap, "%s", on ? "ADSR" : "MANUAL"); break;
            case kAdsrAttack: std::snprintf(out, cap, "%u frames", unsigned(a.attack)); break;
            case kAdsrDecay: std::snprintf(out, cap, "%u frames", unsigned(a.decay)); break;
            case kAdsrSustain: std::snprintf(out, cap, "%u", unsigned(a.sustain)); break;
            case kAdsrPeak: std::snprintf(out, cap, "%u", unsigned(a.peak)); break;
            case kAdsrTone: std::snprintf(out, cap, "%s", kTones[a.dist / 2]); break;
            case kAdsrCurve: std::snprintf(out, cap, "%s", a.curve == 1 ? "FAST START" : (a.curve == 2 ? "SLOW START" : "LINEAR")); break;
            default: std::snprintf(out, cap, "%u frames", unsigned(s.releaseFrames)); break;
        }
        return;
    }
    if (field == kName) {
        std::snprintf(out, cap, "%.24s", ui().instrumentName(ui().inst));
        return;
    }
    const atari::FieldInfo& info = atari::fieldInfo(Field(field));
    const int v = ed_.stage[field];
    if (field == atari::F_INSTRUMENT) std::snprintf(out, cap, "%02X  %.20s", unsigned(v + 1), ui().instrumentName(v + 1));
    else if (info.texts) std::snprintf(out, cap, "%s", info.texts[v]);
    else std::snprintf(out, cap, "%d", v + info.display);
}

void InstrumentScreen::editHeld(void (*fn)(InstrumentScreen&)) {
    AudioEngine& e = ui().app().engine;
    e.hold();
    fn(*this);
    e.release();
}

void InstrumentScreen::change(int field, int d) {
    Ui& u = ui();
    if (field >= kAdsrMode) {
        changeAdsr(field, d);
        return;
    }
    const atari::FieldInfo& info = atari::fieldInfo(Field(field));
    if (field == atari::F_INSTRUMENT) {
        changeInstrument(d);
        u.preview(u.inst);
        return;
    }
    int step = d;
    if (u.shift && (info.maxStaged > 31)) step = d * 16;
    const int v = clampi(ed_.stage[field] + step, 0, info.maxStaged);
    ed_.stage[field] = uint8_t(v);
    if (info.isSelector) {
        ed_.load(chip_);
        return;
    }
    AudioEngine& e = u.app().engine;
    e.hold();
    const bool reshaped = ed_.commit(chip_);
    e.release();
    if (reshaped) e.resetVoices();
    u.app().markDirty();
    u.preview(u.inst);   // hear the change at once (the held pads again, or a short note)
}

void InstrumentScreen::encoder(int d) {
    if (soundsPage()) {
        if (row_ == 0) {
            const int cats = chip_ == atari::Chip::Pokey ? 3 : 1;
            cat_ = clampi(cat_ + d, 0, cats - 1);
            preset_ = 0;
        } else {
            const int n = presetCount();
            if (n > 0) {
                preset_ = clampi(preset_ + d, 0, n - 1);
                applyPreset();
            }
        }
        return;
    }
    if (gridPage()) {
        static const int tiaRow[4] = {atari::F_TIA_WAVE, atari::F_TIA_DIVIDER, atari::F_TIA_VOL, atari::F_TIA_PITCH};
        change(chip_ == atari::Chip::Pokey ? int(atari::F_TAB_VALUE) : tiaRow[gridRow_], d);
        return;
    }
    const int* list;
    const int n = rows(list);
    if (n == 0) return;
    row_ = clampi(row_, 0, n - 1);
    const int f = list[row_];
    if (f == kName) return;
    change(f, d);
}

// --- structure edits ----------------------------------------------------------------

void InstrumentScreen::insertStep() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Pokey && page_ == 1) {
        atari::PokeyInstrument& in = s.pokey[slot];
        const int at = clampi(ed_.stage[atari::F_ENV_STEP], 0, int(in.env.size()) - 1);
        if (in.env.size() < 32) {
            in.env.insert(in.env.begin() + at + 1, in.env[at]);
            if (in.envGoto > at) in.envGoto++;
            ed_.stage[atari::F_ENV_STEP] = uint8_t(at + 1);
        }
    } else if (chip_ == atari::Chip::Pokey && page_ == 2) {
        atari::PokeyInstrument& in = s.pokey[slot];
        const int at = clampi(ed_.stage[atari::F_TAB_STEP], 0, int(in.table.size()) - 1);
        if (in.table.size() < 32) {
            in.table.insert(in.table.begin() + at + 1, in.table[at]);
            if (in.tableGoto > at) in.tableGoto++;
            ed_.stage[atari::F_TAB_STEP] = uint8_t(at + 1);
        }
    } else if (chip_ == atari::Chip::Tia && page_ == 1) {
        atari::TiaInstrument& in = s.tia[slot];
        const int at = clampi(ed_.stage[atari::F_TIA_STEP], 0, int(in.steps.size()) - 1);
        if (in.steps.size() < 64) {
            in.steps.insert(in.steps.begin() + at + 1, in.steps[at]);
            if (in.loopStart > at) in.loopStart++;
            ed_.stage[atari::F_TIA_STEP] = uint8_t(at + 1);
        }
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
}

void InstrumentScreen::deleteStep() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Pokey && page_ == 1) {
        atari::PokeyInstrument& in = s.pokey[slot];
        const int at = clampi(ed_.stage[atari::F_ENV_STEP], 0, int(in.env.size()) - 1);
        if (in.env.size() > 1) {
            in.env.erase(in.env.begin() + at);
            if (in.envGoto > at) in.envGoto--;
            if (in.envGoto >= int(in.env.size())) in.envGoto = int(in.env.size()) - 1;
            ed_.stage[atari::F_ENV_STEP] = uint8_t(clampi(at, 0, int(in.env.size()) - 1));
        }
    } else if (chip_ == atari::Chip::Pokey && page_ == 2) {
        atari::PokeyInstrument& in = s.pokey[slot];
        const int at = clampi(ed_.stage[atari::F_TAB_STEP], 0, int(in.table.size()) - 1);
        if (in.table.size() > 1) {
            in.table.erase(in.table.begin() + at);
            if (in.tableGoto > at) in.tableGoto--;
            if (in.tableGoto >= int(in.table.size())) in.tableGoto = int(in.table.size()) - 1;
            ed_.stage[atari::F_TAB_STEP] = uint8_t(clampi(at, 0, int(in.table.size()) - 1));
        }
    } else if (chip_ == atari::Chip::Tia && page_ == 1) {
        atari::TiaInstrument& in = s.tia[slot];
        const int at = clampi(ed_.stage[atari::F_TIA_STEP], 0, int(in.steps.size()) - 1);
        if (in.steps.size() > 1) {
            in.steps.erase(in.steps.begin() + at);
            if (in.loopStart > at) in.loopStart--;
            if (in.loopStart >= int(in.steps.size())) in.loopStart = int(in.steps.size()) - 1;
            ed_.stage[atari::F_TIA_STEP] = uint8_t(clampi(at, 0, int(in.steps.size()) - 1));
        }
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
}

bool InstrumentScreen::onStepPage() const {
    return (chip_ == atari::Chip::Pokey && (page_ == 1 || page_ == 2)) || (chip_ == atari::Chip::Tia && page_ == 1);
}

void InstrumentScreen::setLoopHere() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Pokey && page_ == 1) s.pokey[slot].envGoto = clampi(ed_.stage[atari::F_ENV_STEP], 0, int(s.pokey[slot].env.size()) - 1);
    else if (chip_ == atari::Chip::Pokey && page_ == 2) s.pokey[slot].tableGoto = clampi(ed_.stage[atari::F_TAB_STEP], 0, int(s.pokey[slot].table.size()) - 1);
    else if (chip_ == atari::Chip::Tia && page_ == 1) s.tia[slot].loopStart = clampi(ed_.stage[atari::F_TIA_STEP], 0, int(s.tia[slot].steps.size()) - 1);
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    u.status("Loop returns to this step");
}

void InstrumentScreen::shiftVolumes(int d) {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Pokey && page_ == 1) {
        for (atari::EnvStep& st : s.pokey[slot].env) {
            st.volL = uint8_t(clampi(st.volL + d, 0, 15));
            st.volR = uint8_t(clampi(st.volR + d, 0, 15));
        }
    } else if (chip_ == atari::Chip::Tia && page_ == 1) {
        for (atari::TiaStep& st : s.tia[slot].steps) st.vol = uint8_t(clampi(st.vol + d, 0, 15));
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
}

void InstrumentScreen::clearStep() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Pokey && page_ == 1) s.pokey[slot].env[size_t(clampi(ed_.stage[atari::F_ENV_STEP], 0, int(s.pokey[slot].env.size()) - 1))] = atari::EnvStep();
    else if (chip_ == atari::Chip::Pokey && page_ == 2) s.pokey[slot].table[size_t(clampi(ed_.stage[atari::F_TAB_STEP], 0, int(s.pokey[slot].table.size()) - 1))] = 0;
    else if (chip_ == atari::Chip::Tia && page_ == 1) s.tia[slot].steps[size_t(clampi(ed_.stage[atari::F_TIA_STEP], 0, int(s.tia[slot].steps.size()) - 1))] = atari::TiaStep();
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    u.status("Step cleared");
}

// Table editing shortcuts (POKEY TABLE page).
void InstrumentScreen::tableAll(int d) {
    Ui& u = ui();
    atari::PokeyInstrument& in = u.song().pokey[u.inst - 1];
    AudioEngine& e = u.app().engine;
    e.hold();
    for (int8_t& v : in.table) v = int8_t(clampi(v + d, -128, 127));
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    u.preview(u.inst);
}

namespace {
int lerpRound(int a, int z, int i, int n) {          // a at i = 0, z at i = n - 1, rounded to nearest, either direction
    const int num = (z - a) * i, den = n - 1;
    return a + (num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}
// The ramp runs from the cursor step to the last step; with the cursor on the last step it runs from the first step.
void rampSpan(int cur, int n, int& from, int& to) {
    to = n - 1;
    from = cur < to ? cur : 0;
}
int tiaGet(const atari::TiaStep& t, int row) {
    return row == 0 ? t.waveform : row == 1 ? t.fixedDivider : row == 2 ? t.vol : t.pitchOffset;
}
void tiaSet(atari::TiaStep& t, int row, int v) {
    if (row == 0) t.waveform = uint8_t(clampi(v, 0, 15));
    else if (row == 1) t.fixedDivider = int8_t(clampi(v, -1, 31));
    else if (row == 2) t.vol = uint8_t(clampi(v, 0, 15));
    else t.pitchOffset = int8_t(clampi(v, -128, 127));
}
}  // namespace

// TIA: the row the shortcuts work on (grid: the row under the cursor; STEPS page: volume).
int InstrumentScreen::tiaShortcutRow() const { return grid_ ? gridRow_ : 2; }

void InstrumentScreen::tableFill() {
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Tia) {
        auto& st = u.song().tia[u.inst - 1].steps;
        const int row = tiaShortcutRow();
        const int v = tiaGet(st[size_t(clampi(ed_.stage[atari::F_TIA_STEP], 0, int(st.size()) - 1))], row);
        for (atari::TiaStep& t : st) tiaSet(t, row, v);
    } else {
        atari::PokeyInstrument& in = u.song().pokey[u.inst - 1];
        const int8_t v = in.table[size_t(clampi(ed_.stage[atari::F_TAB_STEP], 0, int(in.table.size()) - 1))];
        for (int8_t& t : in.table) t = v;
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    u.status("Every step set to the value under the cursor");
}

void InstrumentScreen::tableRamp() {
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    e.hold();
    bool done = false;
    int from = 0, to = 0;
    if (chip_ == atari::Chip::Tia) {
        auto& st = u.song().tia[u.inst - 1].steps;
        const int n = int(st.size()), row = tiaShortcutRow();
        if (n > 1) {
            rampSpan(clampi(ed_.stage[atari::F_TIA_STEP], 0, n - 1), n, from, to);
            const int a = tiaGet(st[size_t(from)], row), z = tiaGet(st[size_t(to)], row);
            for (int i = from; i <= to; i++) tiaSet(st[size_t(i)], row, lerpRound(a, z, i - from, to - from + 1));
            done = true;
        }
    } else {
        atari::PokeyInstrument& in = u.song().pokey[u.inst - 1];
        const int n = int(in.table.size());
        if (n > 1) {
            rampSpan(clampi(ed_.stage[atari::F_TAB_STEP], 0, n - 1), n, from, to);
            const int a = in.table[size_t(from)], z = in.table[size_t(to)];
            for (int i = from; i <= to; i++) in.table[size_t(i)] = int8_t(clampi(lerpRound(a, z, i - from, to - from + 1), -128, 127));
            done = true;
        }
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    char m[64];
    std::snprintf(m, sizeof m, done ? "Ramp: steps %d to %d" : "Needs at least two steps", from + 1, to + 1);
    u.status(m);
}

void InstrumentScreen::tableClearAll() {
    Ui& u = ui();
    AudioEngine& e = u.app().engine;
    e.hold();
    if (chip_ == atari::Chip::Tia) {
        const int row = tiaShortcutRow();
        const atari::TiaStep def;
        for (atari::TiaStep& t : u.song().tia[u.inst - 1].steps) tiaSet(t, row, tiaGet(def, row));
    } else {
        for (int8_t& v : u.song().pokey[u.inst - 1].table) v = 0;
    }
    e.release();
    e.resetVoices();
    ed_.load(chip_);
    u.app().markDirty();
    u.status(chip_ == atari::Chip::Tia ? "Row reset in every step" : "Table cleared");
}

void InstrumentScreen::drawGrid(ot::Gfx& g) {
    Ui& u = ui();
    const atari::PokeyInstrument& in = u.song().pokey[u.inst - 1];
    const int n = int(in.table.size());
    const int cur = ed_.stage[atari::F_TAB_STEP];
    const int cw = 62, ch = 28, x0 = 16, y0 = kContentY + 72;
    for (int i = 0; i < 32; i++) {
        const int x = x0 + (i % 8) * cw, y = y0 + (i / 8) * ch;
        const bool used = i < n;
        g.fill(x, y, cw - 4, ch - 4, i == cur ? col::kCursor : col::kPanel);
        char b[16];
        if (used) std::snprintf(b, sizeof b, "%+d", int(in.table[size_t(i)]));
        else std::snprintf(b, sizeof b, "-");
        const uint32_t tc = i == cur ? col::kOnCursor : (!used ? col::kFaint : (i == in.tableGoto ? col::kWarn : col::kText));
        g.text(x + 6, y + 6, kMono, tc, b);
    }
    const int tx = x0 + 8 * cw + 16;
    g.text(tx, y0, kMono, col::kText, "TABLE MODE");
    g.text(tx, y0 + 26, kMono, col::kDim, "Arrows: move in the grid");
    g.text(tx, y0 + 46, kMono, col::kDim, "Jog: change the value");
    g.text(tx, y0 + 66, kMono, col::kDim, "F1/F2: add / remove a step");
    g.text(tx, y0 + 86, kMono, col::kDim, "F3: loop returns here");
    g.text(tx, y0 + 106, kMono, col::kDim, "F4/F5: all values -/+");
    g.text(tx, y0 + 126, kMono, col::kDim, "Shift F4: fill  F5: ramp");
    g.text(tx, y0 + 146, kMono, col::kDim, "(ramp: cursor to last step)");
    g.text(tx, y0 + 166, kMono, col::kDim, "Shift F6: clear all");
    g.text(tx, y0 + 186, kMono, col::kDim, "Sample Recorder or Instr: exit");
    g.text(x0, y0 + 4 * ch + 8, kMono, col::kDim, "orange = loop step; dashes = unused steps (max 32)");
}

// TIA table mode: one column per step (16 at a time), rows waveform / divider / volume / pitch.
void InstrumentScreen::drawTiaGrid(ot::Gfx& g) {
    Ui& u = ui();
    const atari::TiaInstrument& in = u.song().tia[u.inst - 1];
    const int n = int(in.steps.size());
    const int cur = ed_.stage[atari::F_TIA_STEP];
    const int first = cur / 16 * 16, cw = 44, ch = 28, x0 = 80, y0 = kContentY + 118;
    static const char* const rowName[4] = {"WAVE", "DIVIDER", "VOLUME", "PITCH"};
    for (int r = 0; r < 4; r++) g.text(16, y0 + r * ch + 6, kMono, r == gridRow_ ? col::kEdit : col::kDim, rowName[r]);
    for (int k = 0; k < 16; k++) {
        const int i = first + k, x = x0 + k * cw;
        char b[16];
        if (i >= n) {
            std::snprintf(b, sizeof b, "-");
            g.text(x + 6, y0 - 24, kMono, col::kFaint, b);
            continue;
        }
        std::snprintf(b, sizeof b, "%d", i + 1);
        g.text(x + 6, y0 - 24, kMono, i == in.loopStart ? col::kWarn : col::kDim, b);
        const atari::TiaStep& st = in.steps[size_t(i)];
        for (int r = 0; r < 4; r++) {
            const bool here = i == cur && r == gridRow_;
            g.fill(x, y0 + r * ch, cw - 4, ch - 4, here ? col::kCursor : (i == cur ? col::kPanelHi : col::kPanel));
            if (r == 0) std::snprintf(b, sizeof b, "%X", st.waveform);
            else if (r == 1) { if (st.fixedDivider < 0) std::snprintf(b, sizeof b, "note"); else std::snprintf(b, sizeof b, "%d", st.fixedDivider); }
            else if (r == 2) std::snprintf(b, sizeof b, "%d", st.vol);
            else std::snprintf(b, sizeof b, "%+d", int(st.pitchOffset));
            g.text(x + 4, y0 + r * ch + 6, kMono, here ? col::kOnCursor : col::kText, b);
        }
    }
    std::snprintf(buf_, sizeof buf_, "steps %d-%d of %d   frames per step %d   loop returns to step %d", first + 1, first + 16 < n ? first + 16 : n, n, in.stepFrames, in.loopStart + 1);
    g.text(16, kContentY + 68, kMono, col::kDim, buf_);
    const int ty = y0 + 4 * ch + 12;
    g.text(16, ty, kMono, col::kText, "TABLE MODE");
    g.text(16, ty + 22, kMono, col::kDim, "Left/Right: step   Up/Down: row   jog: change the value   orange number = loop step");
    g.text(16, ty + 42, kMono, col::kDim, "F1/F2: add / remove step   F3: loop here   F4/F5: all volumes -/+   F6: clear step");
    g.text(16, ty + 62, kMono, col::kDim, "WAVE 4/12 = pure tones, others buzz and noise.  DIVIDER: note = follows the key, 0-31 fixed");
    g.text(16, ty + 82, kMono, col::kDim, "Shift F4 fill row   Shift F5 ramp row (cursor to last step, up or down)   Shift F6 reset row");
    g.text(16, ty + 102, kMono, col::kDim, "Sample Recorder or Instr: leave table mode");
}

void InstrumentScreen::setName(const char* name) {
    Ui& u = ui();
    Song& s = u.song();
    AudioEngine& e = u.app().engine;
    e.hold();
    (chip_ == atari::Chip::Pokey ? s.pokey[u.inst - 1].name : s.tia[u.inst - 1].name) = name;
    e.release();
    u.app().markDirty();
}

void InstrumentScreen::saveRti() {
    Ui& u = ui();
    if (chip_ != atari::Chip::Pokey) {
        u.status(".rti is for POKEY instruments");
        return;
    }
    ot::IStorage* fs = u.app().platform().storage;
    if (!fs) {
        u.status("No card");
        return;
    }
    char file[40], path[96];
    int n = 0;
    for (const char* c = u.instrumentName(u.inst); *c && n < 20; ++c) {
        const char ch = (*c >= 'a' && *c <= 'z') ? char(*c - 32) : *c;
        file[n++] = ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ? ch : '_';
    }
    if (!n) file[n++] = 'I';
    file[n] = 0;
    fs->mkdir(App::kDir);
    char dir[48];
    std::snprintf(dir, sizeof dir, "%s/Instruments", App::kDir);
    fs->mkdir(dir);
    std::snprintf(path, sizeof path, "%s/%s.rti", dir, file);
    const std::vector<uint8_t> bytes = atari::saveRti(u.song().pokey[u.inst - 1]);
    const ot::IStorage::Handle h = fs->open(path, ot::IStorage::kWrite);
    if (h < 0 || fs->write(h, bytes.data(), uint32_t(bytes.size())) != int32_t(bytes.size())) {
        if (h >= 0) fs->close(h);
        u.message("Could not save the .rti file", 2000);
        return;
    }
    fs->close(h);
    char msg[64];
    std::snprintf(msg, sizeof msg, "Saved %s.rti", file);
    u.message(msg, 2000);
}

// --- sounds -------------------------------------------------------------------------

int InstrumentScreen::presetCount() const {
    if (chip_ == atari::Chip::Tia) return int(atari::builtinTiaInstruments().size());
    const int own = int(atari::builtinPokeyInstruments().size());
    if (cat_ == 0) return own;
    if (cat_ == 1) return int(atari::pokeyPresetCount()) - own;
    return cardCount_;
}

namespace {
struct ScanCtx {
    char (*names)[20];
    int count;
};
bool scanFn(const ot::IStorage::DirEntry& e, void* ctx) {
    ScanCtx* c = static_cast<ScanCtx*>(ctx);
    const size_t n = std::strlen(e.name);
    if (e.isDir || n < 5 || n > 19 || c->count >= 48) return true;
    const char* ext = e.name + n - 4;
    if ((ext[0] != '.') || ((ext[1] | 32) != 'r') || ((ext[2] | 32) != 't') || ((ext[3] | 32) != 'i')) return true;
    std::snprintf(c->names[c->count++], 20, "%s", e.name);
    return true;
}
}  // namespace

void InstrumentScreen::scanCard() {
    cardCount_ = 0;
    ot::IStorage* fs = ui().app().platform().storage;
    if (!fs) return;
    char dir[48];
    std::snprintf(dir, sizeof dir, "%s/Instruments", App::kDir);
    ScanCtx c{card_, 0};
    fs->listDir(dir, scanFn, &c);
    cardCount_ = c.count;
}

void InstrumentScreen::applyPreset() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    if (!trial_) {                       // the first change: remember what was there
        bakP_ = s.pokey[slot];
        bakT_ = s.tia[slot];
        trial_ = true;
    }
    bool ok = false;
    if (chip_ == atari::Chip::Tia) {
        const auto& own = atari::builtinTiaInstruments();
        if (preset_ < int(own.size())) {
            e.hold();
            s.tia[slot] = own[size_t(preset_)];
            s.tia[slot].steps.reserve(64);
            e.release();
            ok = true;
        }
    } else if (cat_ < 2) {
        const int own = int(atari::builtinPokeyInstruments().size());
        atari::PokeyInstrument p;
        if (atari::pokeyPreset(size_t(cat_ == 0 ? preset_ : own + preset_), p)) {
            e.hold();
            s.pokey[slot] = p;
            s.pokey[slot].env.reserve(32);
            s.pokey[slot].table.reserve(32);
            e.release();
            ok = true;
        }
    } else if (preset_ < cardCount_) {
        ot::IStorage* fs = u.app().platform().storage;
        char path[96];
        std::snprintf(path, sizeof path, "%s/Instruments/%s", App::kDir, card_[preset_]);
        const ot::IStorage::Handle h = fs ? fs->open(path, ot::IStorage::kRead) : -1;
        if (h >= 0) {
            std::vector<uint8_t> bytes(4096);
            const int32_t got = fs->read(h, bytes.data(), uint32_t(bytes.size()));
            fs->close(h);
            atari::PokeyInstrument p;
            if (got > 0) {
                bytes.resize(size_t(got));
                if (atari::loadRti(bytes, p)) {
                    e.hold();
                    s.pokey[slot] = p;
                    s.pokey[slot].env.reserve(32);
                    s.pokey[slot].table.reserve(32);
                    e.release();
                    ok = true;
                }
            }
        }
        if (!ok) u.status("Not a valid .rti file");
    }
    if (ok) {
        e.resetVoices();
        ed_.load(chip_);
        u.app().markDirty();
        u.preview(u.inst);
    }
}

void InstrumentScreen::keepSound() {
    trial_ = false;
    ui().status("Sound kept");
}

void InstrumentScreen::cancelSound() {
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    AudioEngine& e = u.app().engine;
    e.hold();
    s.pokey[slot] = bakP_;
    s.tia[slot] = bakT_;
    e.release();
    e.resetVoices();
    trial_ = false;
    ed_.load(chip_);
}

// --- input --------------------------------------------------------------------------

bool InstrumentScreen::button(uint8_t id, bool down) {
    namespace b = ot::btn;
    if (!down) return false;
    Ui& u = ui();
    Song& s = u.song();
    sync();
    // the keyboard's OK: the new name
    if (const char* name = u.keyboardResult(kActName)) {
        setName(name);
        return true;
    }
    if (u.confirming(kActInit)) {
        AudioEngine& e = u.app().engine;
        e.hold();
        s.resetInstrument(u.inst - 1);
        e.release();
        e.resetVoices();
        ed_.load(chip_);
        u.app().markDirty();
        u.status("Instrument reset");
        return true;
    }
    if (gridPage()) {                              // table mode: arrows move over the grid
        const bool pk = chip_ == atari::Chip::Pokey;
        const int n = pk ? int(s.pokey[u.inst - 1].table.size()) : int(s.tia[u.inst - 1].steps.size());
        const int field = pk ? atari::F_TAB_STEP : atari::F_TIA_STEP;
        int st = ed_.stage[field];
        bool moved = true;
        switch (id) {
            case b::Left: st -= 1; break;
            case b::Right: st += 1; break;
            case b::Up: if (pk) st -= 8; else gridRow_ = clampi(gridRow_ - 1, 0, 3); break;
            case b::Down: if (pk) st += 8; else gridRow_ = clampi(gridRow_ + 1, 0, 3); break;
            case b::InstrParams: grid_ = false; return true;
            default: moved = false; break;
        }
        if (moved) {
            ed_.stage[field] = uint8_t(clampi(st, 0, n - 1));
            ed_.load(chip_);
            return true;
        }
    }
    switch (id) {
        case b::InstrParams:
        case b::Right: {
            if (trial_) cancelSound();
            page_ = (page_ + 1) % pageCount();
            row_ = 0;
            if (soundsPage()) scanCard();
            return true;
        }
        case b::Left:
            if (trial_) cancelSound();
            page_ = (page_ + pageCount() - 1) % pageCount();
            row_ = 0;
            if (soundsPage()) scanCard();
            return true;
        case b::Up:
            if (row_ > 0) --row_;
            return true;
        case b::Down: {
            const int* list;
            const int n = soundsPage() ? 2 : rows(list);
            if (row_ + 1 < n) ++row_;
            return true;
        }
        case b::F7:
            if (soundsPage() && trial_) {
                cancelSound();
                u.status("Cancelled");
            } else {
                changeInstrument(-1);
            }
            return true;
        case b::F8:
            if (soundsPage() && trial_) keepSound();
            else changeInstrument(1);
            return true;
        default: break;
    }
    if (soundsPage()) return false;
    switch (id) {
        case b::F1:
            if (page_ == 0) {
                u.confirm(this, kActInit, "Reset this instrument to its starter sound?", "RESET");
            } else {
                insertStep();
            }
            return true;
        case b::F2:
            if (page_ == 0) {
                clipHas_ = true;
                clipP_ = s.pokey[u.inst - 1];
                clipT_ = s.tia[u.inst - 1];
                u.status("Instrument copied");
            } else {
                deleteStep();
            }
            return true;
        case b::F3:
            if (page_ != 0) { setLoopHere(); return true; }
            if (page_ == 0 && clipHas_) {
                AudioEngine& e = u.app().engine;
                e.hold();
                s.pokey[u.inst - 1] = clipP_;
                s.tia[u.inst - 1] = clipT_;
                e.release();
                e.resetVoices();
                ed_.load(chip_);
                u.app().markDirty();
                u.status("Instrument pasted");
            }
            return true;
        case b::F4:
            if (page_ == 0) u.keyboard(this, kActName, "NAME THE INSTRUMENT", u.instrumentName(u.inst));
            else if (u.shift && tableKeys()) tableFill();
            else if (chip_ == atari::Chip::Pokey && page_ == 2) tableAll(-1);
            else shiftVolumes(-1);
            return true;
        case b::F5:
            if (page_ == 0) saveRti();
            else if (u.shift && tableKeys()) tableRamp();
            else if (chip_ == atari::Chip::Pokey && page_ == 2) tableAll(1);
            else shiftVolumes(1);
            return true;
        case b::F6:
            if (u.shift && tableKeys()) tableClearAll();
            else if (page_ != 0) clearStep();
            return true;
        default: return false;
    }
}

void InstrumentScreen::labels(const char* out[8]) {
    static char hint[2][16];
    if (soundsPage()) {
        out[6] = trial_ ? "CANCEL" : "INS -";
        out[7] = trial_ ? "KEEP" : "INS +";
        return;
    }
    if (page_ == 0) {
        out[0] = "INIT";
        out[1] = "COPY";
        out[2] = clipHas_ ? "PASTE" : nullptr;
        out[3] = "NAME";
        out[4] = chip_ == atari::Chip::Pokey ? "SAVE .rti" : nullptr;
    } else if ((chip_ == atari::Chip::Pokey && (page_ == 1 || page_ == 2)) || (chip_ == atari::Chip::Tia && page_ == 1)) {
        out[0] = "INS STEP";
        out[1] = "DEL STEP";
        out[2] = "SET LOOP";
        if (page_ == 1) {
            out[3] = "VOL -";
            out[4] = "VOL +";
        }
        out[5] = "CLEAR";
        if (chip_ == atari::Chip::Pokey && page_ == 2) {
            out[3] = "ALL -";
            out[4] = "ALL +";
        }
        if (tableKeys() && ui().shift) {
            out[3] = "FILL";
            out[4] = "RAMP";
            out[5] = "CLR ALL";
        }
    }
    std::snprintf(hint[0], sizeof hint[0], "INS -");
    std::snprintf(hint[1], sizeof hint[1], "INS +");
    out[6] = hint[0];
    out[7] = hint[1];
}

// Drawing the volume envelope with the pads (Shift held): 12 columns are 12 steps (the group that holds the step being edited), the four rows are the
// volumes 15, 10, 5 and 0; the jog fine-tunes. The pads' lights show the bars.
bool InstrumentScreen::drawPads() const {
    return ui().shift && ((chip_ == atari::Chip::Pokey && page_ == 1) || (chip_ == atari::Chip::Tia && page_ == 1));
}

int InstrumentScreen::drawStepOfPad(int p) const {
    const int cur = chip_ == atari::Chip::Pokey ? ed_.stage[atari::F_ENV_STEP] : ed_.stage[atari::F_TIA_STEP];
    return (cur / ot::kPadCols) * ot::kPadCols + p % ot::kPadCols;
}

void InstrumentScreen::drawVolume(int p) {
    static const uint8_t kLevel[4] = {15, 10, 5, 0};
    Ui& u = ui();
    Song& s = u.song();
    const int slot = u.inst - 1;
    const int step = drawStepOfPad(p), vol = kLevel[(p / ot::kPadCols) & 3];
    AudioEngine& e = u.app().engine;
    e.hold();
    bool done = false;
    if (chip_ == atari::Chip::Pokey) {
        atari::PokeyInstrument& in = s.pokey[slot];
        if (step < int(in.env.size())) { in.env[size_t(step)].volL = uint8_t(vol); in.env[size_t(step)].volR = uint8_t(vol); done = true; }
    } else {
        atari::TiaInstrument& in = s.tia[slot];
        if (step < int(in.steps.size())) { in.steps[size_t(step)].vol = uint8_t(vol); done = true; }
    }
    e.release();
    if (done) {
        e.resetVoices();
        ed_.load(chip_);
        u.app().markDirty();
    }
}

bool InstrumentScreen::pad(int p, bool down, uint8_t) {
    Ui& u = ui();
    if (drawPads()) {
        if (down) drawVolume(p);
        return true;
    }
    if (!down) {
        u.auditionOff(p);
        return true;
    }
    u.auditionOn(p, u.inst);   // the pads play the instrument as a keyboard
    return true;
}

bool InstrumentScreen::padColour(int p, uint32_t& rgb) {
    if (!drawPads()) return false;
    static const uint8_t kLevel[4] = {15, 10, 5, 0};
    const Song& s = ui().song();
    const int slot = ui().inst - 1, step = drawStepOfPad(p), vol = kLevel[(p / ot::kPadCols) & 3];
    int have = -1;
    if (chip_ == atari::Chip::Pokey) { if (step < int(s.pokey[slot].env.size())) have = s.pokey[slot].env[size_t(step)].volL; }
    else if (step < int(s.tia[slot].steps.size())) have = s.tia[slot].steps[size_t(step)].vol;
    if (have < 0) rgb = 0x080808;                                  // past the end of the instrument
    else if (vol == 0) rgb = have == 0 ? 0x600000 : 0x100808;      // the bottom row: silence
    else rgb = have >= vol ? 0x00A040 : 0x041008;
    return true;
}

// --- drawing ------------------------------------------------------------------------

void InstrumentScreen::drawGraph(ot::Gfx& g) {
    Ui& u = ui();
    const Song& s = u.song();
    const int gx = 452, gy = kContentY + 78, gw = 332, gh = 150;
    g.fill(gx - 4, gy - 4, gw + 8, gh + 8, col::kPanel);
    if (chip_ == atari::Chip::Pokey && (page_ == 0 || page_ == 1)) {
        const atari::PokeyInstrument& in = s.pokey[u.inst - 1];
        const int n = int(in.env.size()), bw = gw / 32;
        for (int i = 0; i < n; i++) {
            const int h = in.env[size_t(i)].volL * gh / 15;
            const bool cur = i == ed_.stage[atari::F_ENV_STEP];
            g.fill(gx + i * bw, gy + gh - h, bw - 1, h, cur ? col::kCursor : (i == in.envGoto ? col::kWarn : col::kGood));
        }
        g.text(gx, gy + gh + 8, kMono, col::kDim, "volume per frame; orange: loop");
    } else if (chip_ == atari::Chip::Pokey && page_ == 2) {
        const atari::PokeyInstrument& in = s.pokey[u.inst - 1];
        const int n = int(in.table.size()), bw = gw / 32;
        g.hline(gx, gy + gh / 2, gw, col::kFaint);
        for (int i = 0; i < n; i++) {
            const int v = clampi(in.table[size_t(i)], -24, 24), h = v * (gh / 2) / 24;
            const bool cur = i == ed_.stage[atari::F_TAB_STEP];
            g.fill(gx + i * bw, h >= 0 ? gy + gh / 2 - h : gy + gh / 2, bw - 1, h >= 0 ? h : -h, cur ? col::kCursor : (i == in.tableGoto ? col::kWarn : col::kGood));
        }
        g.text(gx, gy + gh + 8, kMono, col::kDim, "pitch per table step; orange: loop");
    } else if (chip_ == atari::Chip::Tia && (page_ == 0 || page_ == 1)) {
        const atari::TiaInstrument& in = s.tia[u.inst - 1];
        const int n = int(in.steps.size()), bw = gw / 64 < 2 ? 2 : gw / 64;
        for (int i = 0; i < n && i < 64; i++) {
            const int h = in.steps[size_t(i)].vol * gh / 15;
            const bool cur = i == ed_.stage[atari::F_TIA_STEP];
            g.fill(gx + i * bw, gy + gh - h, bw - 1, h, cur ? col::kCursor : (i == in.loopStart ? col::kWarn : col::kGood));
        }
        g.text(gx, gy + gh + 8, kMono, col::kDim, "volume per step; orange: loop");
    } else if (audctlPage()) {
        const atari::PokeyInstrument& in = s.pokey[u.inst - 1];
        g.text(gx, gy, kMono, col::kText, "AUDCTL");
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02X", unsigned(in.audctl));
        g.text(gx + 64, gy, kMono, col::kInst, buf);
        g.text(gx, gy + 28, kMono, col::kDim, "Joins and 1.79 MHz belong to a track:");
        g.text(gx, gy + 48, kMono, col::kDim, "  join 1+2: use track 2");
        g.text(gx, gy + 66, kMono, col::kDim, "  join 3+4: use track 4");
        g.text(gx, gy + 84, kMono, col::kDim, "  1.79 MHz: track 1 / 3");
        g.text(gx, gy + 112, kMono, col::kDim, "15 kHz and poly 9 are shared:");
        g.text(gx, gy + 130, kMono, col::kDim, "  they colour every track");
    }
}

void InstrumentScreen::draw(ot::Gfx& g) {
    Ui& u = ui();
    sync();
    char buf[128];
    std::snprintf(buf, sizeof buf, "INSTRUMENT %02X  %.24s", unsigned(u.inst), u.instrumentName(u.inst));
    g.text(16, kContentY + 6, kLarge, col::kText, buf);
    // page tabs
    int tx = 16;
    for (int p = 0; p < pageCount(); p++) {
        const char* name = pageName(p);
        const int w = 8 * int(std::strlen(name)) + 16;
        g.fill(tx, kContentY + 36, w, 22, p == page_ ? col::kCursor : col::kPanel);
        g.text(tx + 8, kContentY + 40, kMono, p == page_ ? col::kOnCursor : col::kDim, name);
        tx += w + 4;
    }
    const int y0 = kContentY + 72;
    if (soundsPage()) {
        const char* cats[] = {"STARTERS", "RMT", "CARD"};
        listRow(g, 16, y0, 420, "CATEGORY", cats[cat_], row_ == 0);
        const int n = presetCount();
        char name[40] = "-";
        if (chip_ == atari::Chip::Tia) {
            if (preset_ < n) std::snprintf(name, sizeof name, "%.24s", atari::builtinTiaInstruments()[size_t(preset_)].name.c_str());
        } else if (cat_ < 2) {
            atari::PokeyInstrument p;
            const int own = int(atari::builtinPokeyInstruments().size());
            if (atari::pokeyPreset(size_t(cat_ == 0 ? preset_ : own + preset_), p)) std::snprintf(name, sizeof name, "%.24s", p.name.c_str());
        } else if (preset_ < cardCount_) {
            std::snprintf(name, sizeof name, "%.19s", card_[preset_]);
        } else {
            std::snprintf(name, sizeof name, "(no .rti files on the card)");
        }
        std::snprintf(buf, sizeof buf, "%d / %d  %s", n ? preset_ + 1 : 0, n, name);
        listRow(g, 16, y0 + 28, 420, "PRESET", buf, row_ == 1, trial_ ? col::kWarn : col::kText);
        g.text(16, y0 + 70, kMono, col::kDim, "Up/Down: row   jog: try it, the pads play it at once");
        g.text(16, y0 + 90, kMono, col::kDim, trial_ ? "KEEP to keep it, CANCEL to put the old sound back" : "leaving the page without KEEP puts the old sound back");
        return;
    }
    if (gridPage()) { if (chip_ == atari::Chip::Pokey) drawGrid(g); else drawTiaGrid(g); return; }
    const int* list;
    const int n = rows(list);
    if (row_ >= n) row_ = n - 1;
    for (int i = 0; i < n; i++) {
        const int f = list[i];
        char val[64];
        valueText(f, val, sizeof val);
        listRow(g, 16, y0 + i * 26, 420, f == kName ? "NAME" : (f >= kAdsrMode ? adsrName(f) : atari::fieldInfo(Field(f)).name), val, i == row_);
    }
    drawGraph(g);
    const int f = list[row_ < 0 ? 0 : row_];
    const char* help = f == kName ? "F4 NAME: type a name on the pads" : (f >= kAdsrMode ? adsrHelp(f) : atari::fieldInfo(Field(f)).description);
    std::snprintf(buf, sizeof buf, "%.96s", help);
    g.text(16, kBarY - 30, kMono, col::kDim, buf);
}

}  // namespace ui
}  // namespace atw
