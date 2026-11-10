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
enum class Scr : uint8_t { Project, Wave, Synth, Song, Chain, Phrase, Instr, Table, Groove, File, Help, Library, Loader, Editor, Kit, Firmware };

// Starts and stops playback for the whole machine (one or two consoles); the UI of a console calls it instead of its own player.
struct Transport {
    virtual ~Transport() = default;
    virtual void start(int console, int row) = 0;   // the console whose Start was pressed and its row
    // Rec + Play: this console's channel loops one phrase while the song plays behind it from `row` (and the linked console from its own row).
    virtual void startLoop(int console, int ch, int phrase, int row) = 0;
    // Play on the Chain / Phrase page: just that chain (chain >= 0) or phrase on one track, looping, on this console only.
    virtual void startSolo(int console, int ch, int phrase, int chain) { (void)console; (void)ch; (void)phrase; (void)chain; }
    virtual void stop() = 0;
    virtual bool running() const = 0;
    enum Action { kSavePair, kLoadPair, kExportWav, kExportFiles, kSaveSong };
    virtual bool action(Action a, int slot) = 0;      // kSaveSong: slot = the console; kExportFiles: 0 = A, 1 = B, 2 = A + B; the others use the project names
    // The songs this console can load: first the 32 slots of its bank file (GB/Saves/A.srm or B.srm), then loose .lsdsng files in GB/Songs
    // (shown with a leading "+", they are filed into the bank by the next save).
    virtual int listSongs(int console) = 0;
    virtual const char* songName(int i) = 0;          // slot name, or "+" and the file name
    virtual bool loadSongFile(int i, int console) = 0;               // session actions from the Project screen; false = failed
    virtual bool deleteSongFile(int i, int console) = 0;
    virtual bool newSong(int console, const char* name) { (void)console; (void)name; return false; }   // an empty song named `name` replaces this console's             // removes a bank slot (not a loose file); false = failed
    virtual const char* errorText() { return nullptr; }              // why the last action failed, if it knows (e.g. "BANK FULL")
    virtual const char* doneText() { return nullptr; }               // what to say instead of the usual success text, if there is more to tell
    // The virtual link cable: 0 off, 1 master, 2 slave. The slave follows the master's tempo.
    virtual int syncMode(int console) = 0;
    virtual void setSyncMode(int console, int mode) = 0;
    virtual void tempoChanged(int console) = 0;       // the project tempo was edited: a linked slave takes it
    // Instrument library (GB/Instruments/*.gbi): n bytes per file, see Lsdj::packInstrument.
    virtual int listInstruments() = 0;
    virtual const char* instrumentFile(int i) = 0;
    virtual bool saveInstrumentFile(const char* name, const uint8_t* data, int n) = 0;
    virtual int loadInstrumentFile(int i, uint8_t* out, int max) = 0;   // bytes read, -1 on failure
    // Sample kits, one set per console. Kit numbers 00-37 hex: 00-12, 18 and 19 are the ROM's, 1A-37 can be made here.
    virtual uint8_t* kitBuf(int console) { (void)console; return nullptr; }          // the kit being edited: 16 KB
    virtual bool kitOpen(int console, int slot) { (void)console; (void)slot; return false; }   // copies that kit into the edit buffer (an empty kit if there is none)
    virtual bool kitSave(int console, int slot) { (void)console; (void)slot; return false; }   // writes the edit buffer as the kit's file and makes it live; an empty kit erases it
    virtual const char* kitSource(int console, int i) { (void)console; (void)i; return ""; }   // the WAV file sample i of the kit being edited came from ("" if none)
    virtual void setKitSource(int console, int i, const char* wav) { (void)console; (void)i; (void)wav; }
    virtual void removeKitSource(int console, int i) { (void)console; (void)i; }              // sample i left the kit: the ones after it move up
    virtual int listSamples() { return 0; }                                           // GB/Samples/*.wav
    virtual const char* sampleName(int i) { (void)i; return ""; }
    virtual uint8_t* sampleBuf() { return nullptr; }                                  // the sample being prepared: packed 4-bit, up to 16 KB
    virtual int sampleLoad(int i, bool* cut) { (void)i; (void)cut; return 0; }        // converts WAV i into sampleBuf(): bytes, 0 on failure
    // The firmware loader (Project page, Shift + F8): the update images in the card's Firmware folder.
    virtual int listFirmware() { return 0; }
    virtual const char* firmwareName(int i) { (void)i; return ""; }
    virtual bool flashFirmware(int i) { (void)i; return false; }                      // hands over to the bootloader; only returns if it could not
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
    int helpPages() const;                       // pages of the Help screen
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
    // Sample kits: Sample Loader (WAV files), Sample Editor (trim, name, add to a kit) and Sample Playback (the kit's samples). The 16 KB buffers live in the Transport.
    void newSongView();                          // cursors back to the top and the Song page, for a new or loaded song
    void openLoader();
    void openEditor();
    void openKit();
    void openFirmware();
    int fwSel = 0, fwCount = 0;                 // Firmware window: the update images in /Firmware
    int smpSel = 0, smpCount = 0;               // Loader: the WAV list
    int smpLen = 0, smpStart = 0, smpEnd = 0, smpRow = 0;   // Editor: bytes in the prepared sample, trim in chunks of 16 bytes, cursor row
    int smpEditIdx = -1;                        // Editor: -1 a new sample, else the kit's sample being changed (ADD TO KIT replaces it)
    int kitSlot = 0x1A, kitRow = 0;             // Kit: the kit number 00-37 and the cursor row (0 number, 1 name, 2.. the samples)
    const char* sampleName() const { return smpName_; }
    int packInstrument(uint8_t* out) const;      // 'GBI1', flags, name[5], instrument[16], optional table[96] (the instrument on screen)
    bool unpackInstrument(const uint8_t* in, int n);
    int libSel = 0, libCount = 0;               // Library screen: row 0 saves the current instrument, rows 1.. are the files
    const char* status() const { return status_; }   // result of the last Project action, shown on the Project screen
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
    void fixInstr(int i);                    // an all-zero instrument becomes the pulse default
    // sample kit screens (lsdj_kit.cpp)
    void drawLoader(Lcd& l) const;
    void drawEditor(Lcd& l) const;
    void drawKit(Lcd& l) const;
    void drawFirmware(Lcd& l) const;
    void kitMove(Key k);
    void kitEdit(Key k);
    void kitPress();
    void kitAudition();
    void tableAudition();
    bool tableHeld_ = false;                 // a table playback is sounding
    int tableCh_ = 0;
    void kitToEditor();
    void kitRename();
    void kitSelect(int slot);
    bool kitEditable() const;
    uint8_t* kitBuf() const;
    bool sampleLoad(bool toEditor);
    void kitAdd();
    void kitErase();
    void loaderAdd();
    void kitReplace();
    void editorSelectSample(int idx);
    void kitSampleToBuffer(int i);
    char smpLoaderSrc_[28] = {};             // the WAV last loaded from the loader
    void loaderRemove();
    bool inKit(const char* wav) const;
    char smpSrc_[28] = {};                   // the WAV the prepared sample came from
    bool fullPage() const { return scr_ == Scr::File || scr_ == Scr::Library || scr_ == Scr::Loader || scr_ == Scr::Editor || scr_ == Scr::Kit || scr_ == Scr::Firmware; }
    char smpName_[4] = "SMP";
    int smpLoaded_ = -1;
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
