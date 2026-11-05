#include "atw/export.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

#include "atari/romdata.h"
#include "atari/songwalk.h"

namespace atw {
namespace {

constexpr uint32_t kRate = 44100;
constexpr int kTailFrames = 20;              // the last notes ring out (release tails); this much is folded back onto the start
constexpr uint32_t kTailSamples = kRate * kTailFrames / 60;
constexpr long kMaxFrames = 60L * 60 * 20;   // a runaway song stops after about 20 minutes

// A file on the card written through a small buffer.
class FileOut {
public:
    FileOut(ot::IStorage& fs, const char* path) : fs_(fs) {
        fs.remove(path);
        h_ = fs.open(path, ot::IStorage::kWrite);
    }
    ~FileOut() { close(); }
    bool ok() const { return h_ >= 0 && !bad_; }
    bool write(const void* data, size_t n) {
        const uint8_t* d = static_cast<const uint8_t*>(data);
        while (n && ok()) {
            size_t k = sizeof buf_ - len_;
            if (k > n) k = n;
            std::memcpy(buf_ + len_, d, k);
            len_ += k;
            d += k;
            n -= k;
            if (len_ == sizeof buf_) flush();
        }
        return ok();
    }
    bool seekStart() { return seekTo(0); }
    bool seekTo(uint32_t pos) { flush(); return ok() && fs_.seek(h_, pos); }
    bool close() {
        if (h_ < 0) return !bad_;
        flush();
        fs_.close(h_);
        h_ = -1;
        return !bad_;
    }

private:
    void flush() {
        if (len_ && h_ >= 0 && fs_.write(h_, buf_, uint32_t(len_)) != int32_t(len_)) bad_ = true;
        len_ = 0;
    }
    ot::IStorage& fs_;
    ot::IStorage::Handle h_ = -1;
    uint8_t buf_[1024];
    size_t len_ = 0;
    bool bad_ = false;
};

struct RomFile final : atari::RomSink {
    explicit RomFile(FileOut& f) : f(f) {}
    bool write(const uint8_t* d, size_t n) override { return f.write(d, n); }
    FileOut& f;
};
struct AsmFile final : atari::AsmSink {
    explicit AsmFile(FileOut& f) : f(f) {}
    bool write(const char* t, size_t n) override { return f.write(t, n); }
    FileOut& f;
};

atari::RomSong romSong(const Song& s, const ExportPlan& plan) {
    atari::RomSong r;
    r.chip = s.chip;
    r.region = s.region;
    r.tiaTrack[0] = s.tiaTrack[0];
    r.tiaTrack[1] = s.tiaTrack[1];
    r.speed = s.speed;
    r.releaseFrames = s.releaseFrames;
    r.tiaTune = s.tiaTune;
    r.funk = s.funk;
    r.stepsPerBeat = s.stepsPerBeat;
    r.engineSpeed = s.engineSpeed;
    r.loopTo = plan.loopTo;
    r.tunes = plan.tunes;
    r.sfx = plan.sfx;
    r.playlist = plan.playlist;
    r.playlistLength = size_t(plan.playlistLength);
    r.patternCount = size_t(plan.patternCount);
    r.pokey = s.pokey;
    r.pokeyCount = kInstrumentCount;
    r.tia = s.tia;
    r.tiaCount = kInstrumentCount;
    return r;
}

bool mkdirs(ot::IStorage& fs, const char* a, const char* b = nullptr) {
    return fs.mkdir(a) && (!b || fs.mkdir(b));
}

}  // namespace

bool planExport(const Song& s, ExportPlan& plan, std::string* error) {
    plan = ExportPlan();
    for (int o = 0; o < s.orderLen && o < kOrderLen; ++o) {
        const OrderEntry& e = s.order[o];
        const int idx = plan.patternIndex(s.planOf(e));
        if (idx < 0) {
            if (error) *error = "Too many different pattern combinations in the song grid (64 at most)";
            return false;
        }
        if (o == s.restartPos) plan.loopTo = plan.playlistLength;
        if (o > 0 && e.tune && plan.playlistLength > (plan.tunes.empty() ? 0 : plan.tunes.back())) plan.tunes.push_back(plan.playlistLength);
        for (int k = 0; k < (e.repeat < 1 ? 1 : e.repeat); ++k) {
            if (plan.playlistLength >= 254) {
                if (error) *error = "Too many repeats: the song is longer than 254 pattern plays";
                return false;
            }
            plan.playlist[plan.playlistLength++] = uint8_t(idx);
        }
    }
    for (int i = 0; i < s.sfxCount; ++i) {                  // sound effects play their track of their pattern: that pattern is exported too
        const int slot = s.trackSlot(s.sfxTrack[i]);
        if (slot < 0) continue;                              // a track the chip does not play
        const int idx = plan.patternIndex(s.planOf(s.sfxPattern[i]));
        if (idx < 0) {
            if (error) *error = "Too many patterns with the sound effects (64 at most)";
            return false;
        }
        plan.sfx.emplace_back(idx, slot);
    }
    if (plan.tunes.size() > 30) {
        if (error) *error = "At most 31 tunes";
        return false;
    }
    if (!plan.playlistLength) {
        if (error) *error = "The order list is empty";
        return false;
    }
    return true;
}

void planPattern(const Song& song, int pattern, ExportPlan& plan) {
    plan = ExportPlan();
    plan.patterns[0] = song.planOf(pattern);
    plan.patternCount = 1;
    plan.playlist[0] = 0;
    plan.playlistLength = 1;
    plan.loopTo = 0;
}

bool Exporter::makePlan(ExportPlan& plan, std::string* err) const {
    if (onlyPattern_ >= 0) {
        planPattern(*song_, onlyPattern_, plan);
        return true;
    }
    return planExport(*song_, plan, err);
}

const char* Exporter::extension(ExportKind k) {
    switch (k) {
        case ExportKind::kWav: return "wav";
        case ExportKind::kA78: return "a78";
        case ExportKind::kBin2600: return "bin";
        default: return "asm";
    }
}

bool Exporter::fail(const char* text) {
    message_ = text;
    percent_ = 0;
    path_[0] = 0;
    return false;
}

void Exporter::targetPath(ExportKind kind, const char* title, int onlyPattern, char* out, size_t n) const {
    char named[40];
    if (!title || !*title) title = "Atari song";
    if (onlyPattern >= 0) {
        std::snprintf(named, sizeof named, "%.20s_pat%02X", title, unsigned(onlyPattern));
        title = named;
    }
    if (kind == ExportKind::kAsm) std::snprintf(out, n, "AtariTracker/Export/%.24s_asm", title);
    else std::snprintf(out, n, "AtariTracker/Export/%.24s.%s", title, extension(kind));
}

bool Exporter::verify(const char* path, uint32_t* bytes) {
    const ot::IStorage::Handle h = fs_->open(path, ot::IStorage::kRead);
    if (h < 0) return false;
    const int32_t n = fs_->size(h);
    fs_->close(h);
    if (bytes) *bytes = n > 0 ? uint32_t(n) : 0;
    return n > 0;
}

bool Exporter::start(ExportKind kind, const char* title, int onlyPattern) {
    if (busy()) return fail("An export is running");
    onlyPattern_ = onlyPattern;
    char named[40];
    if (title && *title && onlyPattern >= 0) {
        std::snprintf(named, sizeof named, "%.20s_pat%02X", title, unsigned(onlyPattern));
        title = named;
    }
    if (!fs_ || !song_) return fail("No SD card");
    if (kind == ExportKind::kBin2600 && song_->chip != atari::Chip::Tia) return fail("The 2600 needs TIA: switch the chip in Config");
    if (!title || !*title) title = "Atari song";
    char dir[64];
    std::snprintf(dir, sizeof dir, "%s/Export", "AtariTracker");
    mkdirs(*fs_, "AtariTracker", dir);
    switch (kind) {
        case ExportKind::kWav: return startWav(title);
        case ExportKind::kAsm: return buildAsm(title);
        default: return buildRomFile(kind, title);
    }
}

bool Exporter::buildRomFile(ExportKind kind, const char* title) {
    ExportPlan plan;
    std::string err;
    if (!makePlan(plan, &err)) return fail(err.c_str());
    const bool bin = kind == ExportKind::kBin2600;
    std::snprintf(path_, sizeof path_, "AtariTracker/Export/%.24s.%s", title, extension(kind));
    FileOut out(*fs_, path_);
    if (!out.ok()) return fail("Cannot create the file");
    RomFile sink(out);
    SongPatterns patterns(*song_, plan);
    const char* e = atari::buildRom(bin ? atari::RomTarget::Bin2600 : atari::RomTarget::A78, romSong(*song_, plan), patterns, title, sink);
    const bool closed = out.close();
    if (e || !closed) {
        fs_->remove(path_);
        const std::string text = e ? e : "Write error (card full?)";
        path_[0] = 0;
        return fail(text.c_str());
    }
    uint32_t bytes = 0;
    if (!verify(path_, &bytes)) {
        const std::string where = path_;
        path_[0] = 0;
        return fail((std::string("The card did not keep ") + where).c_str());
    }
    message_ = std::string("Saved ") + path_ + " (" + std::to_string(bytes) + " bytes)";
    percent_ = 100;
    return true;
}

bool Exporter::buildAsm(const char* title) {
    ExportPlan plan;
    std::string err;
    if (!makePlan(plan, &err)) return fail(err.c_str());
    const atari::RomSong song = romSong(*song_, plan);
    const char* lower = song_->chip == atari::Chip::Tia ? "tia" : "pokey";
    char folder[64], path[96];
    std::snprintf(folder, sizeof folder, "AtariTracker/Export/%.24s_asm", title);
    fs_->mkdir(folder);
    std::snprintf(path, sizeof path, "%s/%s_song.asm", folder, lower);
    {
        FileOut out(*fs_, path);
        if (!out.ok()) return fail("Cannot create the file");
        AsmFile sink(out);
        SongPatterns patterns(*song_, plan);
        const char* e = atari::buildSongAsm(song, patterns, title, sink);
        if (e || !out.close()) {
            fs_->remove(path);
            return fail(e ? e : "Write error (card full?)");
        }
    }
    auto whole = [&](const char* name, const char* text, size_t n) {
        std::snprintf(path, sizeof path, "%s/%s", folder, name);
        FileOut out(*fs_, path);
        return out.ok() && out.write(text, n) && out.close();
    };
    char player[32];
    std::snprintf(player, sizeof player, "%s_player.asm", lower);
    SongPatterns forPlayer(*song_, plan);
    const unsigned features = atari::tiaFeaturesNeeded(song, forPlayer);       // a TIA song gets a player without the effects it never uses
    SongPatterns forPlayer2(*song_, plan);
    const std::string ptext = atari::playerSourceFor(song, forPlayer2);
    if (!whole(player, ptext.c_str(), ptext.size())) return fail("Write error (card full?)");
    const std::string readme = atari::asmReadme(song, title, features);
    if (!whole("README.txt", readme.c_str(), readme.size())) return fail("Write error (card full?)");
    std::snprintf(path, sizeof path, "%s/%s_song.asm", folder, lower);
    if (!verify(path)) return fail((std::string("The card did not keep ") + folder).c_str());
    std::snprintf(path_, sizeof path_, "%s/", folder);
    message_ = std::string("Saved ") + path_;
    percent_ = 100;
    return true;
}

// ---- WAV --------------------------------------------------------------------------------------------------------------------------

struct Exporter::WavJob {
    WavJob(ot::IStorage& fs, const char* path) : out(fs, path) {}
    FileOut out;
    AudioEngine engine;               // a second engine: the live one is not touched
    uint32_t samples = 0;             // written to the file
    uint32_t generated = 0;           // rendered (the tail after the end is kept in `tail`, not written)
    long frames = 0;                  // video frames rendered
    int totalFrames = 0;              // estimate for the progress bar
    bool truncated = false;
    // A seamless loop: the file ends exactly where the song does (`endSample`); what the last notes still ring after that is mixed
    // onto the start of the loop (`loopSample`, 0 unless the song restarts later), the way it sounds when the song plays round.
    long endSample = -1, loopSample = -1;
    std::unique_ptr<int16_t[]> head, tail;   // the first kTailSamples written from the loop start / the ring-out after the end
    uint32_t headN = 0, tailN = 0;
};

Exporter::Exporter() = default;
Exporter::~Exporter() = default;

bool Exporter::startWav(const char* title) {
    ExportPlan plan;
    std::string err;
    if (!makePlan(plan, &err)) return fail(err.c_str());
    std::snprintf(path_, sizeof path_, "AtariTracker/Export/%.24s.wav", title);
    std::unique_ptr<WavJob> job(new (std::nothrow) WavJob(*fs_, path_));
    if (!job) return fail("Out of memory");
    if (!job->out.ok()) return fail("Cannot create the file");
    uint8_t header[44];
    atari::makeWavHeader(kRate, 0, header);
    job->out.write(header, sizeof header);
    job->head.reset(new (std::nothrow) int16_t[kTailSamples]);
    job->tail.reset(new (std::nothrow) int16_t[kTailSamples]);
    job->engine.init(song_, kRate * 1000u);
    job->engine.enableMixer();                               // the exported WAV has the Master screen's track volumes, like the ROMs
    for (int t = 0; t < kTracks; ++t) job->engine.setTrackMix(t, song_->trackVolume[t]);
    job->engine.sequencer().setStopAtEnd(true);
    if (onlyPattern_ >= 0) job->engine.playPattern(uint8_t(onlyPattern_), 0);
    else job->engine.playSong(0, 0);
    // progress: the song's length in video frames (rows x speed; speed effects make it a little off, never more than 100 %)
    long frames = 0;
    for (int i = 0; i < plan.playlistLength; ++i) frames += long(plan.patterns[plan.playlist[i]].rows) * song_->speed;
    job->totalFrames = int(frames < 1 ? 1 : frames);
    job_ = std::move(job);
    percent_ = 0;
    message_ = "Rendering";
    return true;
}

void Exporter::finishWav(bool ok, const char* text) {
    bool cut = false;
    if (job_) {
        cut = job_->truncated;
        if (ok) {
            // Fold the ring-out after the end onto the start of the loop: the file then repeats without a gap or a click.
            WavJob& j = *job_;
            const uint32_t n = j.tailN < j.headN ? j.tailN : j.headN;
            if (j.endSample >= 0 && j.loopSample >= 0 && n > 0) {
                for (uint32_t i = 0; i < n; ++i) {
                    int v = int(j.head[i]) + int(j.tail[i]);
                    j.head[i] = int16_t(v > 32767 ? 32767 : (v < -32767 ? -32767 : v));
                }
                ok = j.out.seekTo(44 + 2 * uint32_t(j.loopSample)) && j.out.write(j.head.get(), n * 2);
            }
            uint8_t header[44];
            atari::makeWavHeader(kRate, j.samples, header);
            ok = ok && j.out.seekStart() && j.out.write(header, sizeof header) && j.out.close();
            if (!ok) text = "Write error (card full?)";
        } else {
            job_->out.close();
        }
    }
    job_.reset();
    percent_ = ok ? 100 : 0;
    if (!ok) {
        fs_->remove(path_);
        path_[0] = 0;
        message_ = text;
        return;
    }
    uint32_t bytes = 0;
    if (!verify(path_, &bytes)) {
        const std::string where = path_;
        path_[0] = 0;
        message_ = std::string("The card did not keep ") + where;
        return;
    }
    message_ = std::string("Saved ") + path_ + " (" + std::to_string(bytes) + " bytes)" + (cut ? " (cut at 20 minutes)" : "");
}

void Exporter::cancel() {
    if (!job_) return;
    finishWav(false, "Export cancelled");
}

void Exporter::service() {
    if (!job_) return;
    WavJob& j = *job_;
    const Sequencer& seq = j.engine.sequencer();
    const double fps = atari::ticksPerSecond(song_->region, song_->engineSpeed);   // sequencer frames are player ticks
    // 8192 samples per pass: well under a video frame of work even on the Tracker, and the card sees large writes
    for (int round = 0; round < 8192 / int(AudioEngine::kBlock); ++round) {
        int16_t stereo[2 * AudioEngine::kBlock];
        int16_t mono[AudioEngine::kBlock];
        j.engine.fill(stereo, AudioEngine::kBlock);
        for (uint32_t i = 0; i < AudioEngine::kBlock; ++i) mono[i] = stereo[2 * i];
        const long first = long(j.generated);
        j.generated += AudioEngine::kBlock;
        if (j.endSample < 0 && seq.frames() > 0 && !seq.playing()) {          // the song has just played once
            j.endSample = long(double(seq.frames()) * kRate / fps + 0.5);
            if (j.endSample < first) j.endSample = first;
        }
        if (j.loopSample < 0 && seq.restartFrame() >= 0) j.loopSample = long(double(seq.restartFrame()) * kRate / fps + 0.5);
        uint32_t toFile = 0;
        for (uint32_t i = 0; i < AudioEngine::kBlock; ++i) {
            const long g = first + long(i);
            if (j.head && j.loopSample >= 0 && g >= j.loopSample && j.headN < kTailSamples) j.head[j.headN++] = mono[i];
            if (j.endSample < 0 || g < j.endSample) ++toFile;
            else if (j.tail && j.tailN < kTailSamples) j.tail[j.tailN++] = mono[i];
        }
        if (toFile && !j.out.write(mono, toFile * sizeof(int16_t))) {
            finishWav(false, "Write error (card full?)");
            return;
        }
        j.samples += toFile;
        j.frames = long(double(j.generated) * (fps / double(kRate)));
        if (j.endSample >= 0 && (j.tailN >= kTailSamples || !j.tail)) {
            finishWav(true, "Saved");
            return;
        }
        if (j.frames >= kMaxFrames) {
            j.truncated = true;
            j.endSample = -1;
            finishWav(true, "Saved");
            return;
        }
    }
    percent_ = int(100L * j.frames / j.totalFrames);
    if (percent_ > 99) percent_ = 99;
}

}  // namespace atw
