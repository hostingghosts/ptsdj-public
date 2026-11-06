// The LSDJ screens and their editing rules, independent of any display: Project, Song, Chain, Phrase, Instrument, Table, Groove
// (Synth and Wave are placeholders for now). Keys are the Game Boy's: D-pad, A, B, Select, Start.
#pragma once

#include <cstdint>

#include "gb/player.h"
#include "gb/song.h"
#include "gb/synthgen.h"
#include "gb/ui/lcd.h"

namespace gb {

enum class Key : uint8_t { Up, Down, Left, Right, A, B, Select, Start };
enum class Scr : uint8_t { Project, Wave, Synth, Song, Chain, Phrase, Instr, Table, Groove, File };

// Starts and stops playback for the whole machine (one or two consoles); the UI of a console calls it instead of its own player.
struct Transport {
    virtual ~Transport() = default;
    virtual void start(int console, int row) = 0;   // the console whose Start was pressed and its row
    virtual void stop() = 0;
    virtual bool running() const = 0;
    enum Action { kSavePair, kLoadPair, kExportWav };
    virtual bool action(Action a, int slot) = 0;
    virtual int listSongs() = 0;                      // reads GB/Songs: how many .lsdsng files
    virtual const char* songName(int i) = 0;          // file name without the extension
    virtual bool loadSongFile(int i, int console) = 0;               // session actions from the Project screen; false = failed
};

class Lsdj {
public:
    Lsdj(Song& s, Player& p) : s_(s), p_(p) {}
    void key(Key k, bool down);
    void encoder(int delta);                 // jog wheel: like A + right/left, with Select held A + up/down
    void padNote(int pad);                   // a pad enters a note in the phrase screen
    void draw(Lcd& l) const;
    void setBanner(const char* s) { banner_ = s; }
    void setAudioInfo(const char* s) { audio_ = s; }   // shown on the Project screen: callback cost and late blocks
    void setSynthGen(SynthGen* g) { sg_ = g; }       // regenerates a synth's frames after an edit (needs the owner's ROM)
    void setTransport(Transport* t, int console) { tr_ = t; console_ = console; }
    int startRow() const { return scr_ == Scr::Song ? songRow : 0; }
    Scr screen() const { return scr_; }
    void show(Scr s) { scr_ = s; }
    // Absolute cursor access for tests and the simulator scripts.
    int songRow = 0, songCh = 0;
    int chain = 0, chainPos = 0, chainCol = 0;
    int phrase = 0, step = 0, phraseCol = 0;
    int instr = 0, instrRow = 0;
    int table = 0, tableRow = 0, tableCol = 0;
    int groove = 0, grooveStep = 0;
    int projRow = 0;
    int fileSel = 0, fileCount = 0;              // File screen: the song list for the console that opened it
    int session = 0;                             // session slot 0-99 (files S00_A / S00_B .lsdsng, S00.wav)
    int synth = 0, synthRow = 0, synthCol = 1;   // Synth screen: synth 0-F, row 0-9 (signal, filter, dist, phase, volume, cutoff, q, vshift, limit, phase), column
    int waveNo = 0, waveCol = 0;                 // Wave screen: wave 00-FF and the cursor column 0-31 (column c shows sample (c + 1) & 31)
    const char* status() const { return status_; }   // result of the last Project action, shown on the Project screen

private:
    void press(Key k);
    void move(Key k);
    void editKey(Key k);                     // A held with a direction
    void cut();                              // B + A
    void changeId(int d);                    // B + left/right, or up/down in the phrase
    void navigate(Key k);                    // Select + direction
    void drawPanel(Lcd& l, bool full = true) const;
    void drawSong(Lcd& l) const;
    void drawChain(Lcd& l) const;
    void drawPhrase(Lcd& l) const;
    void drawInstr(Lcd& l) const;
    void drawTable(Lcd& l) const;
    void drawGroove(Lcd& l) const;
    void drawFile(Lcd& l) const;
    void drawWave(Lcd& l) const;
    void drawSynth(Lcd& l) const;
    void synthEdit(Key k);                   // A + direction on the Synth screen
    void waveEdit(Key k);                    // plain / A + direction on the Wave screen
    void regenSynth();
    int waveSample(int i) const;
    void setWaveSample(int i, int v);
    void drawProject(Lcd& l) const;
    int instrRows() const;

    Song& s_;
    Player& p_;
    SynthGen* sg_ = nullptr;
    const char* banner_ = "";
    const char* audio_ = "";
    uint8_t waveClip_[16] = {};
    bool hasClip_ = false, bUsed_ = false;
    Transport* tr_ = nullptr;
    int console_ = 0;                         // 0 = tracks 1-4, 1 = tracks 5-8
    Scr scr_ = Scr::Song;
    bool a_ = false, b_ = false, sel_ = false;
    const char* status_ = "";
    bool edited_ = false;                    // A was used with a direction: releasing it does not insert
    uint8_t lastChain_ = 0, lastPhrase_ = 0, lastNote_ = 25, lastInstr_ = 0, lastCmd_ = 0, lastVal_ = 0;
};

const char* noteName(int n, char out[4]);
char cmdLetter(int c);

}  // namespace gb
