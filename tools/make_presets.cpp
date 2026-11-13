// make_presets: collects the pulse and noise instruments of the .lsdsng songs in a folder, once each, as .gbi library files (see Lsdj::packInstrument).
//   make_presets <songs dir> <output dir>
// Names: P001BASS = pulse number 1 (the most used), N001 = noise number 1. The most used sounds come first.
#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "gb/lsdsng.h"
#include "gb/player.h"
#include "gb/ui/lsdj.h"
#include "gb/upgrade.h"

using namespace gb;

struct Preset {
    std::vector<uint8_t> blob;
    int uses = 0;
    int type = 0;
    std::map<int, int> notes;               // the notes the songs play it on
    std::string role;                       // what it sounds like
    char duty = '5';
};

// What a preset sounds like, found by playing it: how fast it dies away, how bright a noise is, what its table does.
void describe(Preset& p) {
    static Song t;
    t.reset();
    for (int r = 0; r < 256; ++r) for (int c = 0; c < 4; ++c) t.songCell(r, c) = 0xFF;
    for (int i = 0; i < 16; ++i) t.chainPhrase(0, i) = i ? 0xFF : 0;
    for (int i = 0; i < 16; ++i) { t.note(0, i) = 0; t.phraseInstr(0, i) = 0xFF; }
    const int ch = p.type == 3 ? 3 : 0;
    t.songCell(0, ch) = 0;
    int bestNote = 25, bestUses = 0;
    for (auto& kv : p.notes) if (kv.second > bestUses) { bestUses = kv.second; bestNote = kv.first; }
    t.note(0, 0) = uint8_t(bestNote);
    t.phraseInstr(0, 0) = 0;
    const uint8_t* in = p.blob.data() + 10;
    std::memcpy(t.instrument(0), in, 16);
    const bool tbl = (in[6] & 0x20) != 0;
    int tsp[16] = {}, cmds[32] = {};
    if (tbl) {
        t.instrument(0)[6] = 0x20;                                      // table 0
        static const uint16_t o[6] = {off::kTblEnv, off::kTblTrans, off::kTblCmd1, off::kTblVal1, off::kTblCmd2, off::kTblVal2};
        for (int c = 0; c < 6; ++c) std::memcpy(t.mem() + o[c], p.blob.data() + 26 + c * 16, 16);
        for (int r = 0; r < 16; ++r) {
            tsp[r] = t.at(uint16_t(off::kTblTrans + r));
            ++cmds[t.at(uint16_t(off::kTblCmd1 + r)) & 31];
            ++cmds[t.at(uint16_t(off::kTblCmd2 + r)) & 31];
        }
    }
    static Player pl(t);
    pl.start(0);
    const int rate = 44100, win = rate / 100;                           // 10 ms windows
    std::vector<double> rms;
    int zc = 0, zn = 0;
    int16_t buf[2 * 441];
    int16_t prev = 0;
    for (int w = 0; w < 150; ++w) {
        pl.render(buf, win, rate);
        double sum = 0;
        for (int i = 0; i < win; ++i) { const double v = buf[2 * i]; sum += v * v; if (w < 8 && ((v >= 0) != (prev >= 0))) ++zc; prev = buf[2 * i]; if (w < 8) ++zn; }
        rms.push_back(std::sqrt(sum / win));
    }
    pl.stop();
    double pk = 0;
    int pkw = 0;
    for (int w = 0; w < 40; ++w) if (rms[w] > pk) { pk = rms[w]; pkw = w; }
    double dec = 99;
    for (int w = pkw; w < 150; ++w) if (rms[w] < pk * 0.3) { dec = (w - pkw) / 100.0; break; }
    const bool hold = dec > 1.2;
    const double zcr = zn ? zc * double(rate) / zn : 0;                 // zero crossings per second
    int distinct = 0;
    { int seen[256] = {}; for (int r = 0; r < 16; ++r) if (tsp[r] && !seen[tsp[r]]++) ++distinct; }
    if (p.type == 3) {
        const bool bright = zcr > 9000, mid = zcr > 3500;
        if (pk < 1) p.role = "NOIS";
        else if (bright) p.role = dec < 0.08 ? "HAT" : dec < 0.3 ? "OHAT" : "CRSH";
        else if (mid) p.role = dec < 0.3 ? "SNR" : "CRSH";
        else p.role = dec < 0.15 ? "KICK" : dec < 0.6 ? "BOOM" : "RUMB";
        if (tbl && distinct >= 2 && dec >= 0.3) p.role = "ZAP";
    } else {
        p.duty = "1257"[(in[7] >> 6) & 3];
        if (tbl && distinct >= 2) p.role = "ARP";
        else if (tbl && cmds[3] > 0) p.role = "CHRD";                   // command C
        else if (tbl && cmds[13] > 0) p.role = "BEND";                  // command P
        else if (tbl && cmds[16] > 0) p.role = "VIB";                   // command V
        else if (tbl && cmds[11] > 0) p.role = "SLD";                   // command L
        else if (hold) p.role = "LEAD";
        else if (dec < 0.10) p.role = "PLUK";
        else if (dec < 0.25) p.role = "STAB";
        else if (dec < 0.6) p.role = "DCY";
        else p.role = "FADE";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: make_presets <songs> <out>\n"); return 1; }
    std::map<std::string, Preset> all;
    DIR* d = opendir(argv[1]);
    if (!d) return 1;
    std::vector<std::string> files;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n.size() > 7 && n.substr(n.size() - 7) == ".lsdsng") files.push_back(n);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    for (const std::string& n : files) {
        FILE* f = std::fopen((std::string(argv[1]) + "/" + n).c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> in(200000);
        const size_t len = std::fread(in.data(), 1, in.size(), f);
        std::fclose(f);
        static Song s;
        if (!loadLsdsng(s, in.data(), len) || !upgradeSong(s)) { std::fprintf(stderr, "skip %s\n", n.c_str()); continue; }
        int count[64] = {};
        for (int p = 0; p < 255; ++p)
            for (int st = 0; st < 16; ++st)
                if (s.note(p, st) && s.phraseInstr(p, st) != 0xFF) ++count[s.phraseInstr(p, st) & 63];
        static Player player(s);
        Lsdj l(s, player);
        for (int i = 0; i < 64; ++i) {
            const int type = s.instrument(i)[0] & 3;
            if (!count[i] || (type != 0 && type != 3)) continue;
            l.instr = i;
            uint8_t blob[128];
            const int bn = l.packInstrument(blob);
            std::vector<uint8_t> b(blob, blob + bn);
            std::vector<uint8_t> keyBytes(b.begin() + 10, b.end());     // the instrument and its table, not its name
            if (keyBytes[6] & 0x20) keyBytes[6] &= 0xE0;                // which table slot it used does not matter
            const std::string key(keyBytes.begin(), keyBytes.end());
            Preset& p = all[key];
            if (p.blob.empty()) { p.blob = b; p.type = type; }
            p.uses += count[i];
            for (int pp = 0; pp < 255; ++pp) for (int st = 0; st < 16; ++st) if ((s.phraseInstr(pp, st) & 63) == i && s.phraseInstr(pp, st) != 0xFF && s.note(pp, st)) ++p.notes[s.note(pp, st)];
        }
    }
    std::vector<Preset*> order;
    for (auto& kv : all) order.push_back(&kv.second);
    std::stable_sort(order.begin(), order.end(), [](const Preset* a, const Preset* b) { return a->uses > b->uses; });
    std::map<std::string, int> counter;
    int written = 0, pulses = 0, noises = 0;
    for (Preset* p : order) {
        describe(*p);
        char name[6] = {};
        for (int k = 0; k < 5; ++k) { const uint8_t c = p->blob[5 + k]; name[k] = c >= 'A' && c <= 'Z' ? char(c) : c >= '0' && c <= '9' ? char(c) : c >= 'a' && c <= 'z' ? char(c - 32) : 0; }
        std::string word;                                                // the song writer's own name for it, if it says something
        int letters = 0;
        for (int k = 0; k < 5; ++k) if (name[k]) { word += name[k]; if (name[k] >= 'A' && name[k] <= 'Z') ++letters; }
        std::string base;
        if (letters >= 3) base = word.substr(0, 5);
        else base = p->type == 3 ? p->role : p->role + p->duty;
        const int no = ++counter[base];
        char file[24];
        std::snprintf(file, sizeof file, "%s-%02d", base.c_str(), no);
        if (strlen(file) > 8) std::snprintf(file, sizeof file, "%s%02d", base.substr(0, 6).c_str(), no);
        for (int k = 0; k < 5; ++k) p->blob[5 + k] = 0;
        for (int k = 0; k < 5 && file[k]; ++k) p->blob[5 + k] = uint8_t(file[k]);         // the name inside the file is the same, as much as fits
        const std::string path = std::string(argv[2]) + "/" + file + ".gbi";
        if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(p->blob.data(), 1, p->blob.size(), f); std::fclose(f); ++written; }
        (p->type == 3 ? noises : pulses)++;
    }
    std::printf("%d presets (%d pulse, %d noise)\n", written, pulses, noises);
    return 0;
}
