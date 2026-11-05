#include "atw/atsong.h"

#include <cstdio>
#include <cstring>

#include "atari/presets.h"

namespace atw {

void Pattern::clear(uint8_t length) {
    rows = length < 1 ? 1 : (length > kMaxRows ? kMaxRows : length);
    for (auto& row : cells)
        for (Cell& c : row) c = Cell{};
}

bool Pattern::empty() const {
    for (int r = 0; r < rows; r++)
        for (int t = 0; t < kTracks; t++)
            if (!cells[r][t].empty()) return false;
    return true;
}

void Song::resetInstrument(int i) {
    if (i < 0 || i >= kInstrumentCount) return;
    if (!atari::pokeyPreset((size_t)i, pokey[i])) {
        pokey[i] = atari::PokeyInstrument();
        pokey[i].name = "Empty";
        pokey[i].env[0].volL = pokey[i].env[0].volR = 12;
        pokey[i].env[0].dist = 0xA;
    }
    const auto& own = atari::builtinTiaInstruments();
    if (i < (int)own.size()) {
        tia[i] = own[(size_t)i];
    } else {
        tia[i] = atari::TiaInstrument();
        tia[i].name = "Empty";
        tia[i].steps[0].waveform = 4;
        tia[i].steps[0].vol = 12;
    }
}

uint8_t instrumentTrackMask(const atari::PokeyInstrument& in) {
    uint8_t m = 0xF;
    const uint8_t ac = in.audctl;
    if (ac & 0x18) {
        uint8_t j = 0;
        if (ac & 0x10) j |= 2;
        if (ac & 0x08) j |= 8;
        m &= j;
    } else if (ac & 0x60) {
        uint8_t f = 0;
        if (ac & 0x40) f |= 1;
        if (ac & 0x20) f |= 4;
        m &= f;
    }
    for (const atari::EnvStep& s : in.env)
        if (s.filter) { m &= 3; break; }
    return m;
}

void trackMaskText(uint8_t mask, char out[24]) {
    int n = 0, first = 0, second = 0;
    for (int t = 0; t < 4; ++t)
        if ((mask >> t) & 1) { if (!n++) first = t + 1; else if (n == 2) second = t + 1; }
    if (n == 1) std::snprintf(out, 24, "track %d only", first);
    else if (n == 2) std::snprintf(out, 24, "tracks %d and %d", first, second);
    else if (n == 0) std::snprintf(out, 24, "no track");
    else std::snprintf(out, 24, "tracks %d..%d", first, first + n - 1);
}

int adsrSteps(const Adsr& a, uint8_t vol[32]) {
    auto shape = [&](double x) { return a.curve == 1 ? 1 - (1 - x) * (1 - x) : (a.curve == 2 ? x * x : x); };
    auto level = [](double v) { const int r = int(v + 0.5); return uint8_t(r < 0 ? 0 : (r > 15 ? 15 : r)); };
    const int pk = a.peak < 1 ? 1 : (a.peak > 15 ? 15 : a.peak);
    const int su = a.sustain > pk ? pk : a.sustain;
    const int at = a.attack > 15 ? 15 : a.attack, de = a.decay > 15 ? 15 : a.decay;
    int n = 0;
    if (at == 0) vol[n++] = level(pk);
    else for (int k = 1; k <= at; k++) vol[n++] = level(pk * shape(double(k) / at));
    for (int k = 1; k <= de; k++) vol[n++] = level(pk - (pk - su) * shape(double(k) / de));
    vol[n++] = level(su);                                   // the sustain step the envelope loops on
    return n;
}

void Song::clear() {
    std::memset(name, 0, sizeof name);
    std::memset(author, 0, sizeof author);
    std::snprintf(name, sizeof name, "NEW SONG");
    chip = atari::Chip::Pokey;
    region = atari::Region::NTSC;
    speed = 6;
    releaseFrames = 6;
    engineSpeed = 1;
    sfxCount = 0;
    tiaTune = 1;                       // TIA: optimised tuning by default (Config > TIA TUNING)
    funk = 0;
    stepsPerBeat = 4;
    orderLen = 1;
    restartPos = 0;
    for (OrderEntry& e : order) e = OrderEntry{};
    for (uint8_t& v : trackVolume) v = 100;
    tiaTrack[0] = 0;
    tiaTrack[1] = 1;
    for (Adsr& a : adsr) a = Adsr{};
    for (int i = 0; i < kInstrumentCount; i++) resetInstrument(i);
    if (patterns)
        for (int p = 0; p < kPatternCount; p++) patterns[p].clear();
}

// ---- cell <-> step ---------------------------------------------------------------------------------------------------
atari::Step toStep(const Cell& c) {
    atari::Step s;
    if (c.note == note::kOff) s.note = atari::kNoteOff;
    else if (note::isNote(c.note)) s.note = c.note - 1;
    s.instrument = c.inst ? c.inst - 1 : -1;
    s.vol = c.vol ? c.vol - 1 : -1;
    for (int k = 0; k < 2; k++) {
        s.fx[k].type = c.fx[k].type;
        s.fx[k].value = c.fx[k].value;
    }
    return s;
}

Cell fromStep(const atari::Step& s) {
    Cell c;
    if (s.note == atari::kNoteOff) c.note = note::kOff;
    else if (s.note >= 0 && s.note <= 61) c.note = (uint8_t)(s.note + 1);
    c.inst = s.instrument >= 0 && s.instrument < kInstrumentCount ? (uint8_t)(s.instrument + 1) : 0;
    c.vol = s.vol >= 0 && s.vol <= 15 ? (uint8_t)(s.vol + 1) : 0;
    for (int k = 0; k < 2; k++) {
        c.fx[k].type = s.fx[k].type;
        c.fx[k].value = s.fx[k].value;
    }
    return c;
}

void planTrackSteps(const Song& s, const PlanPattern& pp, int t, atari::Step* out) {
    for (int r = 0; r < pp.rows; ++r) out[r] = atari::Step();
    if (pp.src[t] == 0xFF) {
        if (pp.rows > 0) out[0].note = atari::kNoteOff;
        return;
    }
    const Pattern& src = s.pattern(pp.src[t]);
    for (int r = 0; r < pp.rows; ++r) out[r] = toStep(src.cells[r % src.rows][t]);
}

// ---- core song -------------------------------------------------------------------------------------------------------
bool toCore(const Song& s, atari::Song& out) {
    out = atari::Song();
    out.chip = s.chip;
    out.region = s.region;
    out.speed = s.speed;
    out.releaseFrames = s.releaseFrames;
    out.tiaTune = s.tiaTune;
    out.funk = s.funk;
    out.stepsPerBeat = s.stepsPerBeat;
    out.engineSpeed = s.engineSpeed;
    out.tiaTrack[0] = s.tiaTrack[0];
    out.tiaTrack[1] = s.tiaTrack[1];
    out.pokeyIns.assign(s.pokey, s.pokey + kInstrumentCount);
    out.tiaIns.assign(s.tia, s.tia + kInstrumentCount);
    // patterns as the order list uses them (a grid row is one pattern: its tracks' patterns, looped), numbered in order of first use
    std::vector<PlanPattern> keys;
    auto indexOf = [&](const PlanPattern& pp) -> int {
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == pp) return (int)i;
        keys.push_back(pp);
        atari::Pattern cp;
        cp.length = pp.rows;
        for (int t = 0; t < kTracks; t++) planTrackSteps(s, pp, t, cp.track[t].data());
        out.patterns.push_back(cp);
        return (int)keys.size() - 1;
    };
    out.playlist.clear();
    int restart = 0;
    for (int o = 0; o < s.orderLen && o < kOrderLen; o++) {
        const OrderEntry& e = s.order[o];
        const int idx = indexOf(s.planOf(e));
        if (o == s.restartPos) restart = (int)out.playlist.size();
        if (o > 0 && e.tune && out.playlist.size() > (out.tunes.empty() ? 0u : size_t(out.tunes.back()))) out.tunes.push_back((int)out.playlist.size());
        for (int k = 0; k < (e.repeat < 1 ? 1 : e.repeat); k++) {
            if (out.playlist.size() >= 254) return false;
            out.playlist.push_back((uint8_t)idx);
        }
    }
    for (int i = 0; i < s.sfxCount; i++) {                     // sound effects: their patterns are exported too
        const int slot = s.trackSlot(s.sfxTrack[i]);
        if (slot < 0) continue;
        out.sfx.emplace_back(indexOf(s.planOf(s.sfxPattern[i])), slot);
    }
    if (out.patterns.empty() || out.playlist.empty() || out.patterns.size() > 64) return false;
    out.loopTo = restart;
    return true;
}

bool fromCore(const atari::Song& s, Song& out) {
    if (!out.patterns) return false;
    out.clear();
    out.chip = s.chip;
    out.region = s.region;
    out.speed = (uint8_t)(s.speed < 1 ? 1 : (s.speed > 255 ? 255 : s.speed));
    out.tiaTune = (uint8_t)(s.tiaTune ? 1 : 0);
    out.engineSpeed = (uint8_t)((s.engineSpeed == 2 || s.engineSpeed == 4) ? s.engineSpeed : 1);
    out.funk = (uint8_t)(s.funk < 0 ? 0 : (s.funk > 255 ? 255 : s.funk));
    out.stepsPerBeat = (uint8_t)(s.stepsPerBeat == 1 || s.stepsPerBeat == 2 || s.stepsPerBeat == 8 || s.stepsPerBeat == 16 || s.stepsPerBeat == 32 || s.stepsPerBeat == 64 ? s.stepsPerBeat : 4);
    out.releaseFrames = (uint8_t)(s.releaseFrames < 0 ? 0 : (s.releaseFrames > 15 ? 15 : s.releaseFrames));
    for (int i = 0; i < kInstrumentCount; i++) {
        if (i < (int)s.pokeyIns.size()) out.pokey[i] = s.pokeyIns[(size_t)i];
        if (i < (int)s.tiaIns.size()) out.tia[i] = s.tiaIns[(size_t)i];
    }
    if (s.patterns.size() > (size_t)kPatternCount || s.playlist.empty()) return false;
    for (size_t p = 0; p < s.patterns.size(); p++) {
        const atari::Pattern& src = s.patterns[p];
        Pattern& dst = out.patterns[p];
        dst.clear((uint8_t)src.length);
        const int tracks = s.tracks();
        for (int t = 0; t < tracks; t++) {
            const int source = s.trackSource(t);       // TIA: the two tracks the song uses become tracks 0 and 1
            for (int r = 0; r < src.length; r++) dst.cells[r][t] = fromStep(src.track[source][r]);
        }
    }
    int entries = 0, restart = 0;
    for (size_t i = 0; i < s.playlist.size();) {
        if (entries >= kOrderLen) return false;
        if ((int)i == s.loopTo) restart = entries;
        size_t run = 1;
        while (i + run < s.playlist.size() && s.playlist[i + run] == s.playlist[i] && run < 255 && (int)(i + run) != s.loopTo) run++;
        if (s.playlist[i] >= s.patterns.size()) return false;
        out.order[entries].pattern = s.playlist[i];
        out.order[entries].repeat = (uint8_t)run;
        entries++;
        i += run;
    }
    out.orderLen = (uint8_t)entries;
    out.restartPos = (uint8_t)restart;
    return true;
}

// ---- effect letters --------------------------------------------------------------------------------------------------
namespace {
struct FxInfo {
    uint8_t type;
    char letter;
    const char* name;
    const char* help;
    bool pokeyOnly;
};
const FxInfo kFx[] = {
    {atari::FX_VOLUME, 'V', "Volume", "0-F level", false},
    {atari::FX_SLIDE_UP, 'S', "Slide up", "00-3F steps/frame", false},
    {atari::FX_SLIDE_DOWN, 's', "Slide down", "00-3F steps/frame", false},
    {atari::FX_ARP, 'A', "Arpeggio", "x y semitones", false},
    {atari::FX_VIBRATO, 'v', "Vibrato", "x depth 0-3, y delay", true},
    {atari::FX_SPEED, 'T', "Tempo", "frames per row", false},
    {atari::FX_BREAK, 'B', "Break", "end the pattern", false},
    {atari::FX_AUDCTL, 'a', "AUDCTL", "control bits", true},
    {atari::FX_CUT, 'C', "Cut", "frames, then off", false},
    {atari::FX_DELAY, 'D', "Delay", "note starts n frames late", false},
    {atari::FX_RETRIG, 'R', "Retrigger", "again every n frames", false},
    {atari::FX_VOLSLIDE, 'L', "Level slide", "x up, y down", false},
    {atari::FX_PORTA, 'P', "Portamento", "1-8 glide speed", false},
};
const FxInfo* find(uint8_t type) {
    for (const FxInfo& f : kFx)
        if (f.type == type) return &f;
    return nullptr;
}
}  // namespace

bool fxAvailable(uint8_t type, atari::Chip chip) {
    const FxInfo* f = find(type);
    return f && (!f->pokeyOnly || chip == atari::Chip::Pokey);
}
char fxLetter(uint8_t type) {
    const FxInfo* f = find(type);
    return f ? f->letter : '\0';
}
const char* fxName(uint8_t type) {
    const FxInfo* f = find(type);
    return f ? f->name : "?";
}
const char* fxParamHelp(uint8_t type) {
    const FxInfo* f = find(type);
    return f ? f->help : "";
}
uint8_t fxFromLetter(char letter, atari::Chip chip) {
    for (const FxInfo& f : kFx)
        if (f.letter == letter && (!f.pokeyOnly || chip == atari::Chip::Pokey)) return f.type;
    return 0;
}
int fxList(atari::Chip chip, uint8_t out[16]) {
    int n = 0;
    for (const FxInfo& f : kFx)
        if (!f.pokeyOnly || chip == atari::Chip::Pokey) out[n++] = f.type;
    return n;
}

void noteName(uint8_t n, char out[4]) {
    static const char* kNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (n == note::kOff) {
        std::memcpy(out, "===", 4);
    } else if (note::isNote(n)) {
        const int semi = n - 1;
        std::snprintf(out, 4, "%s%d", kNames[semi % 12], semi / 12 + 1);
    } else {
        std::memcpy(out, "---", 4);
    }
}


atari::Step withTrackVolume(atari::Step s, int percent) {
    if (percent >= 100) return s;
    if (percent < 0) percent = 0;
    if (s.note >= 0 || s.vol >= 0) s.vol = (s.vol >= 0 ? s.vol : 15) * percent / 100;
    for (atari::Fx& f : s.fx) if (f.type == atari::FX_VOLUME) f.value = uint8_t((f.value & 15) * percent / 100);
    return s;
}

}  // namespace atw
