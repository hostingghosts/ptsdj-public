// GB tracker -- application shell over the HAL interfaces (platform/hal_api.h). Platform independent: the device glue
// (app/device_main.cpp) and the simulator (sim/src/main.cpp) fill a Platform and call run() / loop().
// Today it is an empty shell: a silent audio output and a blank screen. The Game Boy sequencer and sound engine come later.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "atw/ui/ui.h"
#include "gb/dual.h"
#include "gb/kitbank.h"
#include "gb/lsdsng.h"
#include "gb/player.h"
#include "gb/synthgen.h"
#include "gb/song.h"
#include "gb/ui/lsdj.h"
#include "hal_api.h"

namespace atw {

struct DriverStats {
    uint32_t minC = 0, avgC = 0, maxC = 0, blocks = 0;   // callback cycles (DWT) this window
    uint32_t underruns = 0;                              // cumulative late blocks
    uint32_t displayFaults = 0;                          // FT81x coprocessor resets
};

struct Platform {
    ot::IDisplay* display = nullptr;
    ot::IAudioOut* audio = nullptr;
    ot::IStorage* storage = nullptr;   // null = no card
    ot::IInput* input = nullptr;
    ot::ILeds* leds = nullptr;
    ot::IClock* clock = nullptr;
    void (*service)() = nullptr;       // HAL housekeeping, once per loop pass
    uint32_t sampleRateMilliHz = 44100000;
    ot::IMidiPort* midi = nullptr;
    uint8_t* bigMem = nullptr;         // large scratch RAM (SDRAM on the device) for the kit ROM image
    uint32_t bigMemSize = 0;
    const char* name = "host";
    uint32_t (*cycleCounter)() = nullptr;
    bool (*driverStats)(DriverStats& out) = nullptr;
    void (*setRawDetents)(bool on) = nullptr;
    bool (*isUpdateImageName)(const char* name) = nullptr;
    bool (*requestFirmwareUpdate)(ot::IStorage& fs, const char* image) = nullptr;
    void (*muteOutput)(bool on) = nullptr;   // hardware mute of the output stage (device only)
};

class App {
public:
    bool init(const Platform& p);
    void boot();                        // the card: folders and (later) the workspace
    void loop();                        // one main-loop pass
    [[noreturn]] void run();
    // Silences the output and keeps the work (the device calls it from the power-off hook). Nothing to keep yet.
    void quiesce();

    const Platform& platform() const { return p_; }
    // Two linked Game Boys: A (tracks 1-4, the one the older code and tests mean by song/player/lsdj) and B (tracks 5-8).
    gb::Song song, songB;
    gb::Player player{song}, playerB{songB};
    gb::Lsdj lsdj{song, player}, lsdjB{songB, playerB};
    gb::Dual dual{player, playerB};
    ui::Ui ui;
    void fill(int16_t* out, uint32_t frames);   // audio callback body: only copies what pump() rendered ahead
    void pump();                                // main loop: renders the players ahead into the ring (one pass)
    void flushRing() { flushTo_.store(ringHead_.load(std::memory_order_relaxed), std::memory_order_relaxed); flush_.store(true, std::memory_order_release); }   // drops what is queued now, not what is rendered afterwards
    // Songs live on the card as .lsdsng files in GB/Songs (the format LSDJ's own tools use).
    bool loadSong(const char* path, int console = 0);
    bool saveSong(const char* path, const char* name, int console = 0);
    // Each Game Boy has one bank file with its 32 songs: GB/Saves/A.srm (song A) and B.srm (song B), the same 128 KB layout as a cartridge save.
    bool saveToBank(int console);              // the song on screen goes into its slot (same name replaces, else the first free one) and the working song is updated
    bool loadFromBank(int slot, int console);
    bool loadLastSaved(int console);           // at start: the song each console saved last, or an empty one
    bool newSong(int console, const char* name);   // an empty song (the template LSDJ starts from) with this project name
    const char* bankPath(int console) const { return console ? "GB/Saves/B.srm" : "GB/Saves/A.srm"; }
    // A linked session is two files, BASE_A.lsdsng and BASE_B.lsdsng in GB/Songs.
    bool loadLinked(const char* base);
    bool saveLinked(const char* base);
    // Renders both songs once from their first rows until every channel has come round again (or `maxSeconds`); writes a 16-bit stereo WAV.
    bool exportWav(const char* path, int rate = 44100, int maxSeconds = 600);
    bool exportFiles(const char* name = nullptr, int which = 2);   // song A (0), B (1) or both (2): saves into the banks, then copies each bank to GB/Export/PTSDJ_A.srm / PTSDJ_B.srm (or `name`_A / _B) and writes the song as .lsdsng
    bool saveProject(int console);             // saveToBank
    void progress(const char* label, int pct); // a long action reports here: the bar is drawn at once, over whatever is on screen
    void bootChime(int stage);                 // 0 starts the power-on recording, 2 cuts it (the splash)
    int look() const { return look_; }
    // How long a sound rendered now waits before the speakers play it: what is queued in the ring, plus the output buffer.
    int audioLeadMs() const {
        const uint32_t rate = p_.sampleRateMilliHz / 1000;
        if (!rate) return 0;
        return int((ringHead_.load(std::memory_order_relaxed) - ringTail_.load(std::memory_order_relaxed)) * 1000u / rate) + 6;
    }
    static constexpr const char* kSongDir = "GB/Songs";      // loose .lsdsng files, listed after the bank songs when loading
    static constexpr const char* kSaveDir = "GB/Saves";      // the bank files
    static constexpr const char* kExportDir = "GB/Export";   // WAV exports; the session is always called SESSION until a file browser exists
    static constexpr const char* kInstDir = "GB/Instruments";   // saved instruments (.gbi)
    static constexpr const char* kKitDir = "GB/Kits";       // .kit files: the first two characters (hex) say which kit slot a file replaces, e.g. 0A_808.kit
    static constexpr const char* kRomPath = "GB/LSDJ.GB";   // the owner's own LSDJ ROM: its sample kits are read from it
    bool loadKits();                             // blocking: the whole ROM image and the .kit files (tests, tools); boot loads them step by step instead
    int kitFilesLoaded() const { return kitsLoaded_; }
    void setBanner(const char* s);               // one line at the bottom of the screen (boot progress)
    gb::RomKits kits, kitsB;                     // the kits each console plays: the ROM's, the root files of GB/Kits, then the console's own (GB/Kits/A, GB/Kits/B)
    gb::SynthGen synthGen;                       // wave synth frames, generated by the owner's ROM routine

private:
    static constexpr int kMaxSongs = 96;
    char songNames_[kMaxSongs][24] = {};
    uint8_t songSlot_[kMaxSongs] = {};         // bank slot of each listed song, 0xFF = a loose file
    const char* error_ = nullptr;
    const char* done_ = nullptr;               // success text when the export also wrote a ROM
    char doneBuf_[24] = {};
    uint8_t romSkipped_ = 0xFF;                // first kit slot exportRom left alone because the ROM keeps code there
    bool exportRom(const char* path, int console);   // the card's LSDJ ROM with that console's kits put into their slots
    const char* scanDir_ = nullptr;
    bool installSong(size_t n, int console, const char* name);   // the .lsdsng in fileBuf() becomes this console's song
    int songCount_ = 0;
    static constexpr uint32_t kRomBytes = 1u << 20, kFileBytes = 64 * 1024, kScratchOffset = kRomBytes, kScratchBytes = kFileBytes + gb::kMemSize;
    static constexpr int kMaxKitFiles = 48;
    static constexpr uint32_t kKitOffset = kScratchOffset + kScratchBytes;     // .kit banks follow the scratch area in SDRAM
    static constexpr uint32_t kWorkOffset = kKitOffset + kMaxKitFiles * gb::kKitBankSize;   // then the edit buffers: kit A, kit B, one sample
    static constexpr const char* kSampleDir = "GB/Samples";
    uint8_t* workBuf(int i) const { return p_.bigMem && p_.bigMemSize >= kWorkOffset + 3 * gb::kKitBankSize ? p_.bigMem + kWorkOffset + size_t(i) * gb::kKitBankSize : nullptr; }
    struct KitFile { char path[60]; uint8_t idx; uint8_t console; };        // console 0 = A, 1 = B, 2 = both, 3 = erased
    KitFile kitFile_[kMaxKitFiles] = {};
    bool kitOpen(int console, int slot);
    bool kitSave(int console, int slot);
    int scanKitDir(const char* dir, int console);
    void applyKit(int i);
    static constexpr int kMaxFirmware = 16;
    char fwNames_[kMaxFirmware][40] = {};
    int fwCount_ = 0;
    static constexpr int kMaxSamples = 40;
    char kitSrc_[2][15][28] = {};              // the WAV file each sample of the kit being edited came from (the loader's check marks)
    void srcPath(int console, int slot, char* out, size_t n) const;
    char sampleNames_[kMaxSamples][28] = {};
    int sampleCount_ = 0;
    uint8_t* fileBuf() const;
    bool romStep();
    void scanKitFiles();
    int kitFileStep(int i);
    int kitStep_ = 0;                      // loads the i-th listed .kit file (true = placed)
    int kitFiles_ = 0, kitsLoaded_ = 0;

    void bootTick();
    uint32_t statsAt_ = 0;
    char audioText_[48] = {};
    // Audio is rendered in the main loop into a ring that the audio interrupt only copies from, so a costly stretch (a kick's very high
    // first pitches) delays the render instead of making a whole block late.
    static constexpr uint32_t kRingFrames = 4096, kRunTarget = 3072, kIdleTarget = 512, kChunk = 128;
    int16_t ring_[kRingFrames * 2] = {};
    std::atomic<uint32_t> ringHead_{0}, ringTail_{0};       // frames written / read, only ever increasing
    std::atomic<bool> flush_{false};
    std::atomic<uint32_t> flushTo_{0};
    bool liveWas_ = false;
    std::atomic<uint32_t> ringLate_{0};                     // audio blocks that found the ring short
    uint32_t peakC_ = 0, lateAtStart_ = 0;                  // worst 128-frame render (CPU cycles) and late blocks since this playback started
    int bootStep_ = 4;                           // 4 = nothing to do (boot() starts it)
    ot::IStorage::Handle romH_ = -1;
    uint32_t romGot_ = 0;
    bool exporting_ = false;
    bool chimeOn_ = false;                     // the power-on recording is playing instead of the engines
    uint32_t chimePos_ = 0;                    // 16.16 index into the recording
    static constexpr int kChimeGain = 24;      // sixteenths: 1.5 times the recording's level
    void renderChime(int16_t* out, int n, uint32_t rate);
    int syncRole_[2] = {1, 2};                 // 1 master, 2 slave; only matters while both projects have SYNC on
    int look_ = 0;
    uint32_t progressAt_ = 0;
    static constexpr int kMaxInst = 400;                                   // the library lists the presets too
    static constexpr uint32_t kInstOffset = kWorkOffset + 3 * gb::kKitBankSize;   // the names live in SDRAM after the edit buffers
    char (*instNames() const)[24] { return p_.bigMem && p_.bigMemSize >= kInstOffset + kMaxInst * 24 ? reinterpret_cast<char (*)[24]>(p_.bigMem + kInstOffset) : nullptr; }
    int instCount_ = 0;
    bool linked() const { return song.at(gb::off::kSync) == 1 && songB.at(gb::off::kSync) == 1; }
    int masterConsole() const { return syncRole_[0] == 1 || syncRole_[1] != 1 ? 0 : 1; }
    void projectFileName(int console, char* out, size_t n, const char* fallback) const;
    struct LinkedTransport final : gb::Transport {
        App* app = nullptr;
        void start(int console, int row) override;
        void startLoop(int console, int ch, int phrase, int row) override;
        void startSolo(int console, int ch, int phrase, int chain) override { app->dual.startSolo(console, ch, phrase, chain); app->flushRing(); }
        void stop() override { app->dual.stop(); }
        bool running() const override { return app->dual.running(); }
        bool action(Action a, int slot) override;
        int listSongs(int console) override;
        const char* songName(int i) override { return app->songNames_[i]; }
        bool loadSongFile(int i, int console) override;
        bool deleteSongFile(int i, int console) override;
        bool newSong(int console, const char* name) override { return app->newSong(console, name); }
        uint8_t* kitBuf(int console) override { return app->workBuf(console); }
        bool kitOpen(int console, int slot) override { return app->kitOpen(console, slot); }
        bool kitSave(int console, int slot) override { return app->kitSave(console, slot); }
        int listFirmware() override;
        const char* firmwareName(int i) override { return i >= 0 && i < app->fwCount_ ? app->fwNames_[i] : ""; }
        bool flashFirmware(int i) override;
        bool kitSampleName(int console, int kit, int sample, char out[4]) override {
            const gb::Kit k = (console ? app->kitsB : app->kits).kit(kit);
            if (!k.data || sample < 0 || sample >= gb::KitBank::count(k.data)) return false;
            gb::KitBank::name(k.data, sample, out);
            return true;
        }
        bool kitName(int console, int kit, char out[9]) override {
            const gb::Kit k = (console ? app->kitsB : app->kits).kit(kit);
            if (!k.data || !gb::KitBank::hasSamples(k.data)) return false;
            gb::KitBank::kitName(k.data, out);
            return true;
        }
        const char* kitSource(int console, int i) override { return i >= 0 && i < 15 ? app->kitSrc_[console ? 1 : 0][i] : ""; }
        void setKitSource(int console, int i, const char* wav) override { if (i >= 0 && i < 15) std::snprintf(app->kitSrc_[console ? 1 : 0][i], sizeof app->kitSrc_[0][0], "%s", wav); }
        void removeKitSource(int console, int i) override {
            char (*k)[28] = app->kitSrc_[console ? 1 : 0];
            for (int j = i; j >= 0 && j < 14; ++j) std::memcpy(k[j], k[j + 1], 28);
            if (i >= 0 && i < 15) k[14][0] = 0;
        }
        int listSamples() override;
        const char* sampleName(int i) override { return i >= 0 && i < app->sampleCount_ ? app->sampleNames_[i] : ""; }
        uint8_t* sampleBuf() override { return app->workBuf(2); }
        int sampleLoad(int i, bool* cut) override;
        const char* errorText() override { return app->error_; }
        const char* doneText() override { return app->done_; }
        int syncMode(int console) override { const gb::Song& s = console ? app->songB : app->song; return s.at(gb::off::kSync) == 1 ? app->syncRole_[console] : 0; }
        void setSyncMode(int console, int mode) override;
        void tempoChanged(int console) override;
        int listInstruments() override;
        const char* instrumentFile(int i) override { return app->instNames() && i >= 0 && i < app->instCount_ ? app->instNames()[i] : ""; }
        bool saveInstrumentFile(const char* name, const uint8_t* data, int n) override;
        int loadInstrumentFile(int i, uint8_t* out, int max) override;
        bool linked() override { return app->linked(); }
        int volume(int console) override { return app->dual.volume(console); }
        void setVolume(int console, int v) override { app->dual.setVolume(console, v); }
        int look() override { return app->look_; }
        void setLook(int i) override { app->look_ = i & 3; }
        int audioLeadMs() override { return app->audioLeadMs(); }
    } transport_;
    Platform p_;
};

}  // namespace atw
