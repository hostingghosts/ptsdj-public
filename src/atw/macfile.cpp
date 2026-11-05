#include "atw/macfile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "atari/json.h"
#include "atari/macsession.h"
#include "atw/export.h"

namespace atw {
namespace {

bool fail(std::string* e, const std::string& msg) {
    if (e) *e = msg;
    return false;
}

class FileSource final : public atari::ByteSource {
public:
    FileSource(ot::IStorage& fs, ot::IStorage::Handle h) : fs_(fs), h_(h) {}
    size_t read(char* buffer, size_t max) override {
        const int32_t got = fs_.read(h_, buffer, uint32_t(max));
        return got > 0 ? size_t(got) : 0;
    }
    bool rewind() override { return fs_.seek(h_, 0); }

private:
    ot::IStorage& fs_;
    ot::IStorage::Handle h_;
};

// A whole number in [lo, hi] from a value that was just read.
bool inRange(const atari::Json& v, int lo, int hi, int& out) {
    if (!v.isInt() || v.asInt() < lo || v.asInt() > hi) return false;
    out = v.asInt();
    return true;
}

// What round 1 learns about the file before anything is stored.
struct Info {
    int tempo = 0, stepsPerBeat = 0, stepsPerBar = 0;
    bool pokey = false;
    int current = 0;
    int instruments = 0, patterns = 0;
    uint8_t bars[kPatternCount] = {};
};

constexpr int kMaxFileInstruments = 128;
constexpr int kMaxEnvSteps = 32;
constexpr size_t kMaxPendingEnds = 8192;

class Loader {
public:
    Loader(atari::JsonStream& js, std::string* error) : js_(js), error_(error) {}

    bool scan(Info& info) {
        bool tempo = false, spBar = false, spBeat = false, chip = false, ins = false, pats = false;
        if (!js_.beginObject()) return bad("not a JSON object");
        std::string key;
        atari::Json v;
        while (js_.nextMember(key)) {
            if (key == "tempo") {
                if (!js_.value(v) || !inRange(v, 20, 800, info.tempo)) return bad("tempo missing or out of range");
                tempo = true;
            } else if (key == "stepsPerBar") {
                if (!js_.value(v) || !inRange(v, 1, 128, info.stepsPerBar)) return bad("stepsPerBar missing or out of range");
                spBar = true;
            } else if (key == "stepsPerBeat") {
                if (!js_.value(v) || !inRange(v, 1, 64, info.stepsPerBeat) || (info.stepsPerBeat & (info.stepsPerBeat - 1))) return bad("stepsPerBeat missing or not 1, 2, 4 .. 64");
                spBeat = true;
            } else if (key == "chip") {
                if (!js_.value(v) || v.type != atari::Json::Str) return bad("chip missing");
                info.pokey = v.s == "POKEY";
                chip = true;
            } else if (key == "current") {
                if (!js_.value(v)) return bad(nullptr);
                int c;
                info.current = inRange(v, 0, 1000, c) ? c : 0;
            } else if (key == "instruments") {
                if (!js_.beginArray()) return bad(nullptr);
                while (js_.nextElement()) {
                    if (!js_.skip()) return bad(nullptr);
                    if (++info.instruments > kMaxFileInstruments) return bad("instruments: 1 to 128 expected");
                }
                ins = true;
            } else if (key == "patterns") {
                if (!js_.beginArray()) return bad(nullptr);
                while (js_.nextElement()) {
                    if (info.patterns >= kPatternCount) return bad("patterns: 1 to 64 expected");
                    if (!js_.beginObject()) return bad(nullptr);
                    int bars = 0;
                    std::string k2;
                    while (js_.nextMember(k2)) {
                        if (k2 == "bars") {
                            if (!js_.value(v) || !inRange(v, 1, 128, bars)) return bad("bars missing or out of range");
                        } else if (!js_.skip()) {
                            return bad(nullptr);
                        }
                    }
                    if (!js_.ok()) return bad(nullptr);
                    if (!bars) return bad("pattern " + std::to_string(info.patterns + 1) + ": bars missing");
                    info.bars[info.patterns++] = uint8_t(bars);
                }
                pats = true;
            } else if (!js_.skip()) {
                return bad(nullptr);
            }
        }
        if (!js_.ok() || !js_.atEnd()) return bad(nullptr);
        if (!ins || !pats || !chip || !tempo || !spBar || !spBeat) return bad("not a Mac app session");
        if (!info.pokey) return bad("only POKEY Mac sessions are supported so far");
        if (!info.instruments) return bad("instruments: 1 to 128 expected");
        if (!info.patterns) return bad("patterns: 1 to 64 expected");
        return true;
    }

    // Round 2 (target == nullptr) checks everything; round 3 stores it.
    bool walk(const Info& info, Song* target, const char* title) {
        if (target) {
            target->clear();
            std::snprintf(target->name, sizeof target->name, "%.24s", title);
            target->speed = uint8_t(std::max(1, std::min(255, int(900.0 / double(info.tempo) + 0.5))));   // a beat lasts 4 * speed frames
            target->stepsPerBeat = uint8_t(info.stepsPerBeat);
        }
        OrderEntry order[kOrderLen];
        int orderLen = 0;
        if (!js_.beginObject()) return bad("not a JSON object");
        std::string key;
        while (js_.nextMember(key)) {
            if (key == "instruments") {
                if (!instruments(info, target)) return false;
            } else if (key == "patterns") {
                if (!patterns(info, target)) return false;
            } else if (key == "song") {
                if (!songList(info, order, orderLen)) return false;
            } else if (!js_.skip()) {
                return bad(nullptr);
            }
        }
        if (!js_.ok()) return bad(nullptr);
        if (!orderLen) {
            order[0].pattern = uint8_t(info.current < info.patterns ? info.current : 0);
            orderLen = 1;
        }
        if (target) {
            target->orderLen = uint8_t(orderLen);
            for (int i = 0; i < orderLen; ++i) target->order[i] = order[i];
        }
        return true;
    }

private:
    bool bad(const std::string& msg) { return fail(error_, msg); }
    bool bad(const char* msg) {
        if (!msg) return fail(error_, js_.error().empty() ? "unreadable file" : js_.error());
        return fail(error_, msg);
    }

    bool instruments(const Info& info, Song* target) {
        (void)info;
        if (!js_.beginArray()) return bad(nullptr);
        for (int i = 0; js_.nextElement(); ++i) {
            const std::string w = "instrument " + std::to_string(i + 1) + ": ";
            atari::PokeyInstrument p;
            p.env.clear();
            p.table.clear();
            unsigned seen = 0;
            if (!js_.beginObject()) return bad(nullptr);
            std::string key;
            atari::Json v;
            int n = 0;
            while (js_.nextMember(key)) {
                static const struct { const char* key; int lo, hi; } kScalar[] = {
                    {"audctl", 0, 255}, {"delay", 0, 255}, {"vibrato", 0, 3}, {"fshift", 0, 255}, {"vmin", 0, 15}, {"vslide", 0, 255},
                    {"envGo", 0, 31}, {"tableGo", 0, 31}, {"tableSpeed", 0, 63}, {"tableMode", 0, 1}, {"tableType", 0, 1}};
                int idx = -1;
                for (int k = 0; k < 11; ++k) if (key == kScalar[k].key) idx = k;
                if (idx >= 0) {
                    if (!js_.value(v) || !inRange(v, kScalar[idx].lo, kScalar[idx].hi, n)) return bad(w + key + " missing or out of range");
                    seen |= 1u << idx;
                    switch (idx) {
                        case 0: p.audctl = uint8_t(n); break;
                        case 1: p.delay = uint8_t(n); break;
                        case 2: p.vibrato = uint8_t(n); break;
                        case 3: p.fshift = uint8_t(n); break;
                        case 4: p.volMin = uint8_t(n); break;
                        case 5: p.fadeout = uint8_t(n); break;
                        case 6: p.envGoto = n; break;
                        case 7: p.tableGoto = n; break;
                        case 8: p.tableSpeed = n + 1; break;     // the app counts 0-63, ours 1-64
                        case 9: p.tableAdd = n != 0; break;
                        default: p.tableFreq = n != 0; break;
                    }
                } else if (key == "name") {
                    if (!js_.value(v)) return bad(nullptr);
                    if (v.type == atari::Json::Str) p.name = v.s.substr(0, 31);
                } else if (key == "env") {
                    if (!js_.beginArray()) return bad(nullptr);
                    while (js_.nextElement()) {
                        if (!js_.value(v) || v.type != atari::Json::Obj) return bad(w + "env step expected");
                        if (p.env.size() >= size_t(kMaxEnvSteps)) return bad(w + "envelope too long");
                        atari::EnvStep x;
                        int t;
                        const atari::Json* f;
                        if (!(f = v.find("vol")) || !inRange(*f, 0, 15, t)) return bad(w + "vol missing or out of range");
                        x.volL = x.volR = uint8_t(t);
                        if (!(f = v.find("dist")) || !inRange(*f, 0, 14, t)) return bad(w + "dist missing or out of range");
                        x.dist = uint8_t(t);
                        if (!(f = v.find("cmd")) || !inRange(*f, 0, 7, t)) return bad(w + "cmd missing or out of range");
                        x.cmd = uint8_t(t);
                        if (!(f = v.find("x")) || !inRange(*f, 0, 15, t)) return bad(w + "x missing or out of range");
                        x.x = uint8_t(t);
                        if (!(f = v.find("y")) || !inRange(*f, 0, 15, t)) return bad(w + "y missing or out of range");
                        x.y = uint8_t(t);
                        f = v.find("filter");
                        x.filter = f && f->type == atari::Json::Bool && f->b;
                        f = v.find("porta");
                        x.portamento = f && f->type == atari::Json::Bool && f->b;
                        p.env.push_back(x);
                    }
                    seen |= 1u << 11;
                } else if (key == "table") {
                    if (!js_.beginArray()) return bad(nullptr);
                    while (js_.nextElement()) {
                        if (!js_.value(v) || !inRange(v, -128, 127, n)) return bad(w + "table value out of range");
                        if (p.table.size() >= size_t(kMaxEnvSteps)) return bad(w + "table too long");
                        p.table.push_back(int8_t(n));
                    }
                    seen |= 1u << 12;
                } else if (!js_.skip()) {
                    return bad(nullptr);
                }
            }
            if (!js_.ok()) return bad(nullptr);
            if (seen != 0x1FFFu) return bad(w + "fields missing");
            const std::string why = atari::validatePokeyInstrument(p);
            if (!why.empty()) return bad(w + why);
            if (target && i < kInstrumentCount) target->pokey[i] = std::move(p);
        }
        return js_.ok() || bad(nullptr);
    }

    struct End { uint8_t ch, step, len; };

    bool patterns(const Info& info, Song* target) {
        if (!js_.beginArray()) return bad(nullptr);
        for (int pi = 0; js_.nextElement(); ++pi) {
            const std::string w = "pattern " + std::to_string(pi + 1) + ": ";
            if (pi >= info.patterns) return bad(w + "unexpected");
            const int rows = std::min(info.bars[pi] * info.stepsPerBar, kMaxRows);
            Pattern* pat = target ? &target->pattern(pi) : nullptr;
            if (pat) pat->clear(uint8_t(rows));
            std::vector<End> ends;
            bool sawNotes = false;
            if (!js_.beginObject()) return bad(nullptr);
            std::string key;
            while (js_.nextMember(key)) {
                if (key != "notes") {
                    if (!js_.skip()) return bad(nullptr);
                    continue;
                }
                sawNotes = true;
                if (!js_.beginArray()) return bad(nullptr);
                atari::Json n;
                while (js_.nextElement()) {
                    if (!js_.value(n) || n.type != atari::Json::Obj) return bad(w + "note expected");
                    int ch, step, pitch, len, inst, vol;
                    const atari::Json* f;
                    if (!(f = n.find("ch")) || !inRange(*f, 0, 3, ch) || !(f = n.find("step")) || !inRange(*f, 0, 127, step) ||
                        !(f = n.find("pitch")) || !inRange(*f, 0, 127, pitch) || !(f = n.find("len")) || !inRange(*f, 1, 128, len) ||
                        !(f = n.find("inst")) || !inRange(*f, 0, 127, inst) || !(f = n.find("vol")) || !inRange(*f, 0, 15, vol))
                        return bad(w + "note field missing or out of range");
                    f = n.find("off");
                    const bool off = f && f->type == atari::Json::Bool && f->b;
                    if (step >= rows) continue;
                    if (inst >= info.instruments) return bad(w + "instrument " + std::to_string(inst + 1) + " does not exist");
                    if (!off && inst >= kInstrumentCount) return bad(w + "instrument " + std::to_string(inst + 1) + " is beyond the Tracker's 48");
                    if (!pat) continue;
                    Cell& c = pat->cells[step][ch];
                    if (off) {
                        c.note = note::kOff;
                        continue;
                    }
                    atari::Step s;
                    s.note = std::min(pitch, 61);
                    s.instrument = inst;
                    s.vol = vol == 15 ? -1 : vol;
                    c = fromStep(s);
                    if (ends.size() >= kMaxPendingEnds) return bad(w + "too many notes");
                    ends.push_back(End{uint8_t(ch), uint8_t(step), uint8_t(len)});
                }
            }
            if (!js_.ok()) return bad(nullptr);
            if (!sawNotes) return bad(w + "notes expected");
            if (pat)
                for (const End& e : ends) {                // a note that stops before the next one starts gets its "===" there
                    const int at = e.step + e.len;
                    if (at < rows && pat->cells[at][e.ch].note == note::kEmpty) pat->cells[at][e.ch].note = note::kOff;
                }
        }
        return js_.ok() || bad(nullptr);
    }

    bool songList(const Info& info, OrderEntry* order, int& orderLen) {
        orderLen = 0;
        if (!js_.beginArray()) return bad(nullptr);
        while (js_.nextElement()) {
            atari::Json e;
            if (!js_.value(e)) return bad(nullptr);
            const atari::Json* pat = e.type == atari::Json::Obj ? e.find("pattern") : nullptr;
            int p;
            if (!pat || !inRange(*pat, 0, info.patterns - 1, p)) return bad("song entry names a pattern that does not exist");
            const atari::Json* rpt = e.find("repeats");
            int times = 1;
            if (rpt && rpt->isInt()) times = std::max(1, std::min(255, rpt->asInt()));
            if (orderLen >= kOrderLen) return bad("the song list is longer than 255 entries");
            order[orderLen].pattern = uint8_t(p);
            order[orderLen].repeat = uint8_t(times);
            ++orderLen;
        }
        return js_.ok() || bad(nullptr);
    }

    atari::JsonStream& js_;
    std::string* error_;
};

struct MacFile final : atari::MacSink {
    MacFile(ot::IStorage& fs, ot::IStorage::Handle h) : fs(fs), h(h) {}
    bool write(const char* text, size_t n) override { return fs.write(h, text, uint32_t(n)) == int32_t(n); }
    ot::IStorage& fs;
    ot::IStorage::Handle h;
};

}  // namespace

bool saveMacSession(ot::IStorage& fs, const Song& s, const char* path, std::string* error) {
    if (s.chip != atari::Chip::Pokey) return fail(error, "Mac sessions are POKEY only for now");
    ExportPlan plan;
    std::string why;
    if (!planExport(s, plan, &why)) return fail(error, why);
    const std::string tmp = std::string(path) + ".tmp";
    fs.remove(tmp.c_str());
    const ot::IStorage::Handle h = fs.open(tmp.c_str(), ot::IStorage::kWrite);
    if (h < 0) return fail(error, "cannot create the file");
    atari::MacSessionOut out;
    out.chip = s.chip;
    out.region = s.region;
    out.speed = s.speed;
    out.stepsPerBeat = s.stepsPerBeat;
    out.playlist = plan.playlist;
    out.playlistLength = size_t(plan.playlistLength);
    out.patternCount = size_t(plan.patternCount);
    out.pokey = s.pokey;
    out.pokeyCount = kInstrumentCount;
    SongPatterns patterns(s, plan);
    MacFile sink(fs, h);
    const char* e = atari::writeMacSession(out, patterns, sink);
    fs.close(h);
    if (e) {
        fs.remove(tmp.c_str());
        return fail(error, e);
    }
    fs.remove(path);
    if (!fs.rename(tmp.c_str(), path)) return fail(error, "cannot replace the old file");
    return true;
}

bool loadMacSession(ot::IStorage& fs, Song& s, const char* path, const char* title, std::string* error) {
    const ot::IStorage::Handle h = fs.open(path, ot::IStorage::kRead);
    if (h < 0) return fail(error, "cannot open the file");
    struct Closer { ot::IStorage& f; ot::IStorage::Handle h; ~Closer() { f.close(h); } } closer{fs, h};
    const int32_t size = fs.size(h);
    if (size < 16 || size > 8 * 1024 * 1024) return fail(error, "not a Mac session");
    FileSource src(fs, h);
    atari::JsonStream js(src);
    Info info;
    if (!Loader(js, error).scan(info)) return false;
    if (!js.rewind() || !Loader(js, error).walk(info, nullptr, title)) return error && error->empty() ? fail(error, "unreadable file") : false;
    if (!js.rewind() || !Loader(js, error).walk(info, &s, title)) return error && error->empty() ? fail(error, "unreadable file") : false;
    return true;
}

}  // namespace atw
