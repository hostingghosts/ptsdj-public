// The LSDJ screens and their editing rules, independent of any display: Project, Song, Chain, Phrase, Instrument, Table, Groove,
// Synth, Wave and Help. Keys are the Game Boy's: D-pad, A, B, Select, Start.
#pragma once

#include <cstdint>

#include "gb/player.h"
#include "gb/song.h"
#include "gb/synthgen.h"
#include "gb/ui/lcd.h"

namespace gb {

enum class Key : uint8_t { Up, Down, Left, Right, A, B, Select, Start };
enum class Scr : uint8_t { Project, Wave, Synth, Song, Chain, Phrase, Instr, Table, Groove, File, Help, Library };

// Starts and stops playback for the whole machine (one or two consoles); the UI of a console calls it instead of its own player.
struct Transport {
    virtual ~Transport() = default;
    virtual void start(int console, int row) = 0;   // the console whose Start was pressed and its row
    // Rec + Play: this console's channel loops one phrase while the song plays behind it from `row` (and the linked console from its own row).
    virtual void startLoop(int console, int ch, int phrase, int row) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    enum Action { kSavePair, kLoadPair, kExportWav, kExportFiles, kSaveSong };
    virtual bool action(Action a, int slot) = 0;      // kSaveSong: slot = the console; kExportFiles: 0 = A, 1 = B, 2 = A + B; the others use the project names
    virtual int listSongs() = 0;                      // reads GB/Songs: how many .lsdsng files
    virtual const char* songName(int i) = 0;          // file name without the extension
    virtual bool loadSongFile(int i, int console) = 0;               // session actions from the Project screen; false = failed
    // The virtual link cable: 0 off, 1 master, 2 slave. The slave follows the master's tempo.
    virtual int syncMode(int console) = 0;
    virtual void setSyncMode(int console, int mode) = 0;
    virtual void tempoChanged(int console) = 0;       // the project tempo was edited: a linked slave takes it
    // Instrument library (GB/Instruments/*.gbi): n bytes per file, see Lsdj::packInstrument.
    virtual int listInstruments() = 0;
    virtual const char* instrumentFile(int i) = 0;
    virtual bool saveInstrumentFile(const char* name, const uint8_t* data, int n) = 0;
    virtual int loadInstrumentFile(int i, uint8_t* out, int max) = 0;   // bytes read, -1 on failure
    virtual bool linked() = 0;                        // both projects have SYNC on
    virtual int volume(int console) = 0;              // 0-15, the F keys of the split view
    virtual void setVolume(int console, int v) = 0;
    virtual int look() = 0;                           // LCD colours: 0 green, 1 gray, 2 amber, 3 ice
    virtual void setLook(int i) = 0;
    virtual int audioLeadMs() { return 0; }           // how far ahead of the speakers the players run (a pad press is heard this much later than the sequencer's position)
};

class Lsdj {
public:
    Lsdj(Song& s, Player& p) : s_(s), p_(p) { for (int8_t& c : padCh_) c = -1; }
    void key(Key k, bool down);
    void encoder(int delta);                 // jog wheel: like A + right/left, with Select held A + up/down
    void padDown(int pad);                   // a pad plays the current instrument on the cursor channel (and enters/records the note)
    void padUp(int pad);                     // releasing it cuts the note
    void setPadOctave(int o) { padOct_ = o < -2 ? -2 : o > 3 ? 3 : o; }
    int padOctave() const { return padOct_; }
    void setRecord(bool on) { rec_ = on; }
    // Phrase page Rec mode: pads audition the selected instrument (jog wheel picks it) and, only while Rec + Play loops this phrase, write into it.
    bool recMode() const { return recMode_; }
    void toggleRecMode() { if (scr_ == Scr::Phrase) { recMode_ = !recMode_; if (!recMode_ && !playing()) rec_ = false; } }
    bool recording() const { return rec_; }
    bool promptOpen() const { return prompt_ || nameOpen_ || confirm_ >= 0; }
    bool playing() const;
    void jump(Scr target);                   // a direct page key: carries the cursor along like Select + arrows does between the main screens
    void openFiles();                        // the song list for this console
    void openLibrary();                      // the instrument library (Sample loader key)
    void openHelp();                         // the help pages (Help key, or Insert on the Project page's HELP row)
    // Pop-ups that stay up while a key is held: the page's help (F8), and the command cheat sheets for the Phrase (Shift + Note) and Table (Shift + Instr params) pages.
    enum Popup : uint8_t { kNoPopup, kPageHelp, kPhraseSheet, kTableSheet };
    void showPopup(Popup p) { popup_ = p; popupPage_ = 0; }
    void hidePopup() { popup_ = kNoPopup; }
    Popup popup() const { return popup_; }
    void popupTurn(int d);
    int popupPage() const { return popupPage_; }
    int popupPages() const;
    static bool docsFit();                       // every help line fits the pop-up (23 characters)
    void copyKey(bool paste);                // Copy copies the selected cells (or the one under the cursor), Shift + Copy pastes them
    void draw(Lcd& l) const;
    void setBanner(const char* s) { banner_ = s; }
    void setAudioInfo(const char* s) { audio_ = s; }   // shown on the Project screen: callback cost and late blocks
    void setSynthGen(SynthGen* g) { sg_ = g; }       // regenerates a synth's frames after an edit (needs the owner's ROM)
    void setTransport(Transport* t, int console) { tr_ = t; console_ = console; }
    // A long action (save, export) reports its progress here; the screen shows a bar over the page while label is set.
    void setProgress(const char* label, int percent) { progLabel_ = label; progPct_ = percent; }
    int startRow() const { return scr_ == Scr::Song ? songRow : 0; }
    Scr screen() const { return scr_; }
    void show(Scr s) { scr_ = s; }
    // The F keys of the split view: mute / solo a track of this console, and this console's level.
    void toggleMuteChannel(int ch) { p_.mute(ch, !p_.muted(ch)); }
    void soloChannel(int ch);
    bool channelMuted(int ch) const { return p_.muted(ch); }
    bool channelOn(int ch) const { return p_.voiceOn(ch); }
    bool syncLinked() const { return tr_ && tr_->linked(); }
    int volume() const { return tr_ ? tr_->volume(console_) : 15; }
    void adjustVolume(int d) { if (tr_) tr_->setVolume(console_, tr_->volume(console_) + d); }
    const char* name() const { return name_; }       // the project name (up to 8 characters), used for saves and exports
    void setName(const char* n);
    bool prelisten() const { return prelisten_; }
    // Absolute cursor access for tests and the simulator scripts.
    int songRow = 0, songCh = 0;
    int chain = 0, chainPos = 0, chainCol = 0;
    int phrase = 0, step = 0, phraseCol = 0;
    int instr = 0, instrRow = 0;
    int table = 0, tableRow = 0, tableCol = 0;
    int groove = 0, grooveStep = 0;
    int projRow = 0;
    int fileSel = 0, fileCount = 0;              // File screen: the song list for the console that opened it
    int session = 0;                             // export slot for tests and the old session files (S00_A / S00_B .lsdsng)
    int synth = 0, synthRow = 0, synthCol = 1;   // Synth screen: synth 0-F, row 0-9 (signal, filter, dist, phase, volume, cutoff, q, vshift, limit, phase), column
    int waveNo = 0, waveCol = 0;                 // Wave screen: wave 00-FF and the cursor column 0-31 (column c shows sample (c + 1) & 31)
    int helpPage = 0;
    int libSel = 0, libCount = 0;               // Library screen: row 0 saves the current instrument, rows 1.. are the files
    const char* status() const { return status_; }   // result of the last Project action, shown on the Project screen
    static constexpr int kHelpPages = 7;
    int exportSel = 2;                           // Project: EXPORT LSDJ writes A (0), B (1) or both (2)
    // Name entry (instrument names: 5 characters, the project name: 8), the keyboard that pops up over the page.
    bool nameEditing() const { return nameOpen_; }
    const char* nameBuffer() const { return nameBuf_; }
    // Selection (Shift + Insert + arrows) and the clipboard.
    bool selecting() const { return selOn_; }
    void clipboardInfo(int& rows, int& cols) const { rows = clipRows_; cols = clipCols_; }

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
    void drawHelp(Lcd& l) const;
    void drawNameEntry(Lcd& l) const;
    void drawPopup(Lcd& l) const;
    void clearCells();                       // Delete: empties the selected cells (or the one under the cursor)
    void adjustCells(Key k);                 // the jog wheel with a selection: every filled cell changes
    bool hasData(int row, int col) const;
    void drawProgress(Lcd& l) const;
    void synthEdit(Key k);                   // A + direction on the Synth screen
    void waveEdit(Key k);                    // plain / A + direction on the Wave screen
    void regenSynth();
    int waveSample(int i) const;
    void setWaveSample(int i, int v);
    void drawProject(Lcd& l) const;
    int instrRows() const;
    void projectAct();                       // Insert on a Project row
    void cleanSong();
    void cleanInstruments();
    // names
    void openName(int target);               // 0 instrument name, 1 project name
    void nameKey(Key k);
    void nameCommit();
    void drawLibrary(Lcd& l) const;
    int packInstrument(uint8_t* out) const;      // 'GBI1', flags, name[5], instrument[16], optional table[96]
    bool unpackInstrument(const uint8_t* in, int n);
    const char* instrName(int i, char out[6]) const;

    // selection and clipboard
    bool selectable() const;
    int curRow() const;
    int curCol() const;
    void setCursor(int row, int col);
    int rowsOnPage() const;
    int colsOnPage() const;
    uint8_t* cellPtr(int row, int col) const;
    void markCell(int row, int col);
    void selMove(Key k);
    bool selected(int row, int col) const;
    void clearSel() { selOn_ = false; }
    int cloneChain(int src);
    int clonePhrase(int src);
    void doCopy();
    void doPaste();

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
    Scr helpFrom_ = Scr::Song;
    bool a_ = false, b_ = false, sel_ = false;
    bool rec_ = false;                       // recording is running: pads write into the looping phrase
    bool recMode_ = false;
    bool prompt_ = false, promptYes_ = false;   // "OVERWRITE HAND-DRAWN WAVES?" on the Synth page (measured: NO is preselected)
    bool handDrawn(int syn) const;
    void markHandDrawn(int wave);
    bool auditionHeld_ = false;              // Play on the Synth / Wave page holds a preview note
    int padOct_ = 0;
    int8_t padCh_[48];                       // channel each held pad started a note on, -1 none
    int curInstr() const;
    int previewChannel(int instrIdx) const;
    void recordNote(int ch, int note);
    const char* status_ = "";
    const char* progLabel_ = "";
    int progPct_ = 0;
    const char* msg_ = "";                   // one line under the page: COPIED / PASTED / why not
    bool edited_ = false;                    // A was used with a direction: releasing it does not insert
    Popup popup_ = kNoPopup;
    int popupPage_ = 0;
    bool prelisten_ = true;
    bool cloneDeep_ = true;
    int confirm_ = -1;                       // project row waiting for a second Insert (cleaning deletes things)
    char name_[9] = {};
    char statusBuf_[40] = {};
    // name entry
    bool nameOpen_ = false;
    int nameTarget_ = 0, nameMax_ = 5, nameCx_ = 0, nameCy_ = 0;
    char nameBuf_[9] = {};
    // selection / clipboard: Song, Chain, Phrase, Table and Groove cells, up to 64 rows x 6 columns
    bool selOn_ = false, selUsed_ = false;
    int selRow_ = 0, selCol_ = 0;
    static constexpr int kClipMaxRows = 64, kClipMaxCols = 6;
    uint8_t clip_[kClipMaxRows * kClipMaxCols] = {};
    Scr clipPage_ = Scr::Song;
    int clipRows_ = 0, clipCols_ = 0, clipCol0_ = 0;
    uint8_t lastChain_ = 0, lastPhrase_ = 0, lastNote_ = 25, lastInstr_ = 0, lastCmd_ = 0, lastVal_ = 0;
};

const char* noteName(int n, char out[4]);
char cmdLetter(int c);

}  // namespace gb
