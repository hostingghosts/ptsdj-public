// The help text of the Lsdj screens: the Help pages, the F8 pop-up and the cheat sheets, and how they are laid out in pages.
#include "gb/ui/lsdj.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gb {

namespace {
// Help text. A section is a title and entries; an entry is a bold name and a few words about it, wrapped to 23 characters and left aligned,
// with a blank line between entries. Sections turn into pages by themselves (13 lines each): an entry is never split across two pages.
// The same text feeds the Help screen, the F8 pop-up and the cheat sheets.
struct Entry { const char* term; const char* text; };
struct Section { const char* title; const Entry* e; int n; };
template <size_t N> constexpr Section sec(const char* t, const Entry (&e)[N]) { return {t, e, int(N)}; }

constexpr int kDocW = 23, kDocLines = 13;
struct DLine { const char* s; int len; bool bold; };

// The next row of wrapped text from p (which starts at a word): up to kDocW characters, broken at a space. Returns where the following row starts.
const char* wrapNext(const char* p, int& len) {
    const int n = int(std::strlen(p));
    if (n <= kDocW) { len = n; return p + n; }
    int cut = kDocW;
    while (cut > 0 && p[cut] != ' ') --cut;
    if (cut == 0) cut = kDocW;                          // one word longer than a row
    len = cut;
    const char* q = p + cut;
    while (*q == ' ') ++q;
    return q;
}

// The rows of one entry: its name (bold) and its wrapped text. Returns the count (at most cap).
int entryRows(const Entry& e, DLine* out, int cap) {
    int h = 0;
    if (e.term && e.term[0] && h < cap) out[h++] = {e.term, int(std::strlen(e.term)), true};
    for (const char* p = e.text; p && *p && h < cap;) {
        int len;
        const char* nx = wrapNext(p, len);
        out[h++] = {p, len, false};
        p = nx;
    }
    return h;
}

// Lays the section out in pages. Fills `out` with the rows of page `want` (when want >= 0); returns how many pages the section has.
int paginate(const Section& s, int want, DLine* out, int& count) {
    int page = 0, used = 0;
    count = 0;
    for (int i = 0; i < s.n; ++i) {
        DLine rows[kDocLines + 1];
        const int h = std::min(entryRows(s.e[i], rows, kDocLines + 1), kDocLines);
        if (used && used + 1 + h > kDocLines) { ++page; used = 0; }
        if (used) { if (page == want) out[count++] = {"", 0, false}; ++used; }
        for (int k = 0; k < h; ++k) { if (page == want) out[count++] = rows[k]; ++used; }
    }
    return page + 1;
}

// ---- the Help screen (these chapters follow one another) ---------------------------------------------------------------------

const Entry kWelcomeTxt[] = {
    {"", "PTSDJ makes music like a Game Boy."},
    {"", "You write small pieces and link them together."},
    {"PHRASE", "16 steps with notes in them."},
    {"CHAIN", "A list of phrases."},
    {"SONG", "Chains for all 4 tracks."},
    {"TWO SONGS", "PTSDJ has two Game Boys. Song A is tracks 1 to 4. Song B is tracks 5 to 8."},
};
const Entry kMovingTxt[] = {
    {"ARROWS", "Move the cursor."},
    {"SONG KEY", "Song page."},
    {"PATTERN KEY", "Chain page."},
    {"NOTE KEY", "Phrase page."},
    {"INSTR KEY", "Instrument page."},
    {"INSTR PARAMS KEY", "Table page."},
    {"FX1 AND FX2 KEYS", "Synth page and Wave page."},
    {"CONFIG KEY", "Project page."},
    {"PERFORMANCE KEY", "Groove page."},
    {"FILE KEY", "The song list."},
    {"SHIFT + INSTR KEY", "The instrument library."},
    {"SAMPLE LOADER KEY", "Pick a WAV file to turn into a drum sample."},
    {"SAMPLE EDITOR KEY", "Cut a sample and add it to a kit."},
    {"SAMPLE PLAYBACK KEY", "The kit page: hear and edit the samples."},
    {"SAMPLE RECORDER KEY", "Record a sound from the line or mic input."},
    {"SHIFT + ARROWS", "Move between the pages."},
};
const Entry kEditingTxt[] = {
    {"INSERT", "Puts in a value."},
    {"INSERT + LEFT/RIGHT", "Changes it by 1."},
    {"INSERT + UP/DOWN", "Changes it by 16."},
    {"JOG WHEEL", "Does the same as Insert + Left/Right."},
    {"BKSP", "Clears the cell."},
    {"SHIFT+INSERT+ARROWS", "Picks many cells. Then BKSP clears them all, or the jog wheel changes them all."},
    {"COPY", "Copies the cells."},
    {"SHIFT + COPY", "Pastes them. Same page only."},
};
const Entry kPlayingTxt[] = {
    {"PLAY", "Starts and stops the song. On the Chain, Phrase and Table pages it plays just that one, on the track the cursor was in, and loops until you press Play again."},
    {"SHIFT + PLAY", "On the Chain, Phrase and Table pages: plays the whole song from there."},
    {"PADS", "Play sounds. They never start the song."},
    {"SHIFT + A PAD ROW", "Sets the octave."},
    {"REC OFF", "On the Phrase page, the pads type notes at the cursor."},
    {"REC ON", "The pads only make sound. The jog wheel picks the sound."},
    {"REC + PLAY", "The phrase loops while the song plays. The pads write the notes. Play stops it."},
};
const Entry kNamesTxt[] = {
    {"", "To name a sound, go to the Instr page, move to NAME and press Insert."},
    {"INSERT", "Types a letter."},
    {"< KEY", "Erases a letter."},
    {"PLAY", "OK."},
    {"BKSP", "No."},
    {"SHIFT + INSTR KEY", "Opens the library. It saves a sound to the card, or loads one from it."},
    {"PRESETS", "Copy the presets folder to GB/Instruments on the SD card. There are 272 pulse and noise sounds. Left / Right turn a page of 13."},
};
const Entry kTwoSongsTxt[] = {
    {"SYNC", "On the Project page. MASTER sets the tempo. SLAVE follows it. Play starts both."},
    {"MASTER KEY", "Shows both songs side by side."},
    {"SHIFT + SONG KEY", "Switches the screen between song A and song B."},
    {"F1 AND F8", "In the side by side view, pick song A or B. Hold one to see what every F key does."},
    {"F2 / F7 + JOG", "Volume of song A / song B."},
    {"F3 TO F6", "Mute a track. Add Shift to play only that track."},
};
const Entry kSavingTxt[] = {
    {"", "Each Game Boy has its own save file on the SD card, in the GB/Saves folder. Song A is in A.SRM. Song B is in B.SRM."},
    {"", "A file holds up to 32 songs, like a real LSDJ cartridge."},
    {"SAVE SONG", "Puts the song on screen into its file. The same name replaces the old song. A new name adds a song."},
    {"LOAD SONG", "Lists the songs in the file. The first entry is NEW SONG. A + means a loose file from GB/Songs. A * means the built-in demo."},
    {"NEW SONG", "Asks for a name, then starts an empty song. Your kits and the library sounds are still there. Unsaved work on that Game Boy is replaced."},
    {"AT START", "Each Game Boy opens with the song you saved last. If neither one ever saved a song, they open with the demo songs. If only one did, the other opens empty."},
    {"SHIFT + BKSP", "In the song list, erases the song under the cursor."},
    {"EXPORT WAV", "Makes an audio file."},
    {"EXPORT LSDJ", "Pick A, B or A+B with the jog wheel, then press Insert. It saves the song and copies the file to GB/Export as PTSDJ_A.SRM or PTSDJ_B.SRM."},
    {"KITS IN THE ROM", "Drum kits live in the LSDJ ROM, not in the song. EXPORT LSDJ also writes PTSDJ_A.GB and PTSDJ_B.GB: your ROM with that Game Boy's own kits in. Each one goes with the save file of the same name, PTSDJ_A.SRM."},
    {"KIT SLOTS", "Use kit 15 to 32 for your own kits: 30 slots. Kits 00 to 14 are the ROM's own."},
    {"ON A GAME BOY", "Put PTSDJ_A.GB and PTSDJ_A.SRM together on the cartridge or emulator. The save file has to have the same name as the ROM."},
};
const Entry kSamplesTxt[] = {
    {"", "Drum kits are sets of up to 15 short sounds. The WAV track plays them with a KIT instrument."},
    {"KIT NUMBERS", "Kits 00 to 14 are in the ROM. Make your own in 15 to 32. Each Game Boy has its own kits."},
    {"STEP 1", "Put WAV files in the GB/Samples folder on the SD card."},
    {"STEP 2", "Press the SAMPLE LOADER key. The kit number is at the top: the jog wheel changes it. Pick a file and press Insert. It is changed to Game Boy sound quality and added to the kit. A check mark shows the files already in it."},
    {"REMOVE IT", "On the loader, BKSP takes the file's sample out of the kit again."},
    {"CUT IT FIRST", "Shift + Insert on the loader opens the file in the SAMPLE EDITOR. Set START and END to cut the sound. Pick a NAME and a KIT number. Then choose ADD TO KIT."},
    {"CHANGE A SAMPLE", "In the editor, move to the SAMPLES row and turn the jog wheel to step through the kit. Cut or rename the sample, then choose REPLACE SAMPLE. It asks first: the change is for good and can't be undone."},
    {"COPY A SOUND", "On the kit page, move to any sample, even one in a ROM kit, and press Insert. It opens in the Sample Editor. Cut it, then add it to your own kit."},
    {"STEP 4", "On the Instr page, make a KIT sound and set KIT 1 to your kit number. In a phrase, the note column shows sample names: sample 1, sample 2, and so on."},
    {"LAYER TWO KITS", "Set KIT 2 on the Instr page too. In the phrase, Insert + Up/Down adds a KIT 2 sample to a step. Alone on an empty step, or together with the KIT 1 sample. Crackle? Lower LEVEL in the Sample Editor."},
    {"RECORD ONE", "Press the SAMPLE RECORDER key. Pick LINE or MIC, press Insert, and make the sound. When it stops, the SAMPLE EDITOR opens with it."},
    {"SAMPLE PLAYBACK KEY", "Shows the samples in a kit. Play or a pad hears one. Change the kit number at the top."},
    {"SIZE", "A kit holds about 2.8 seconds of sound in all."},
    {"SAVING", "Every change to a kit is saved to the SD card at once."},
};
const Entry kFirmwareTxt[] = {
    {"", "To load another firmware, put its .PTF file in the Firmware folder on the SD card."},
    {"SHIFT + F8", "On the Project page, hold Shift. The ? over F8 turns into FIRMWARE. Press F8."},
    {"UP / DOWN", "Pick a file."},
    {"INSERT", "Asks if you are sure. Press Insert again to flash it. Any other key says no."},
    {"BKSP", "Go back."},
    {"", "The Tracker restarts when it is done. To go back to the stock firmware, pick its file."},
    {"OT_RESTORE.TXT", "This still works. Put a file name in it and restart."},
};
const Section kHelpDocs[] = {sec("WELCOME", kWelcomeTxt), sec("MOVING AROUND", kMovingTxt), sec("EDITING", kEditingTxt), sec("PLAYING", kPlayingTxt),
                             sec("NAMES, LIBRARY", kNamesTxt), sec("TWO SONGS", kTwoSongsTxt), sec("SAVING", kSavingTxt), sec("SAMPLES AND KITS", kSamplesTxt),
                             sec("LOAD FIRMWARE", kFirmwareTxt)};

// ---- the F8 pop-ups ------------------------------------------------------------------------------------------------------

const Entry kSongTxt[] = {
    {"", "The Song page is the big plan. Each row has 4 boxes, one for each track: PU1 PU2 WAV NOI."},
    {"A BOX", "Holds a chain number. -- means none. The tracks play down the rows together."},
    {"TO MAKE MUSIC", "Press Insert in a box. Then press > to open it."},
    {"PLAY", "Starts at the row you are on."},
    {"SHIFT + INSERT", "Mutes the track."},
    {"SHIFT + BKSP", "Mutes all tracks."},
    {"SHIFT + SONG KEY", "Swaps song A and song B."},
    {"BKSP", "Clears a box."},
    {"SHIFT+INSERT+ARROWS", "Pick many boxes."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kChainTxt[] = {
    {"", "A chain is a list of up to 16 phrases. They play one after another."},
    {"LEFT COLUMN", "The phrase."},
    {"RIGHT COLUMN", "Transpose. Higher or lower notes. 0C is one octave."},
    {"PLAY", "Plays just this chain on the track you came from, and loops. Shift + Play plays the song."},
    {"INSERT", "Fills a box."},
    {"> KEY", "Opens the phrase."},
    {"< KEY", "Goes back to the song."},
    {"BKSP + LEFT/RIGHT", "Switch chains."},
    {"BKSP", "Clears a box."},
    {"SHIFT+INSERT+ARROWS", "Pick many boxes."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
    {"CLONE: DEEP", "On the Project page. Pasting makes new copies of the phrases."},
    {"CLONE: SLIM", "Pasting keeps the same phrases."},
};
const Entry kPhraseTxt[] = {
    {"", "A phrase has 16 steps. The notes live here."},
    {"NOTE", "The pitch."},
    {"INSTR", "Which sound."},
    {"CMD", "A special trick. Hold Shift + Note to see them all."},
    {"PLAY", "Plays just this phrase on its track, and loops. Shift + Play plays the song."},
    {"INSERT", "Puts in a note."},
    {"LEFT / RIGHT", "Moves the note by 1."},
    {"UP / DOWN", "Moves it by an octave."},
    {"PADS, REC OFF", "Press a pad to type the note at the cursor."},
    {"PADS, REC ON", "The pads make sound but write nothing. The jog wheel picks the sound."},
    {"REC + PLAY", "The phrase loops while the song plays. The pads write the notes. Play stops it."},
    {"KIT SOUNDS", "The note column shows a sample name. Insert + Left/Right picks the KIT 1 sample. Insert + Up/Down adds a KIT 2 sample (+NAME), alone or together with KIT 1. Crackle? Lower LEVEL in the Sample Editor."},
    {"STOP A NOTE", "There is no OFF note, as in LSDJ. Put K on the step: K08 ends the note after 8 ticks. A wave note rings on until you do."},
    {"NOTE RANGE", "Notes stop at the top note. An octave step that would leave the range does nothing."},
    {"BKSP", "Clears a step."},
    {"BKSP + UP/DOWN", "Go to another phrase."},
    {"SHIFT+INSERT+ARROWS", "Pick many steps. The jog wheel changes them all."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kInstrTxt[] = {
    {"", "An instrument is a sound you can play."},
    {"THE NUMBER", "Move up to the number at the top. Then use < > or the jog wheel to pick one. The pads play it as you go."},
    {"TYPE", "Pulse, wave, kit or noise. The rows below change with the type. Changing the type starts the sound again from that type's plain settings."},
    {"EMPTY SOUNDS", "An instrument nobody has used yet starts as a plain pulse sound you can hear at once."},
    {"NAME", "Press Insert to type a name."},
    {"LEFT / RIGHT", "Some rows hold more than one value, like ENV. Left and Right pick which one. Insert + Left/Right changes it."},
    {"ENV.", "How the volume goes up and down. Three two-digit values: the start level and speed, then a second and a third stage."},
    {"OUTPUT", "Left, right or both speakers. On wave and kit sounds it sits beside VOLUME."},
    {"LENGTH", "How long a note lasts."},
    {"PITCH, TRANSP.", "PITCH is the style of bend and vibrato. TRANSP. OFF makes notes ignore the transposes of chains and the project."},
    {"CMD/RATE", "Kept so your songs stay the same on a real Game Boy. PTSDJ does not use it yet."},
    {"TABLE", "The first value is OFF, ON or STEP. ON runs the table as time passes. STEP moves it one row for each new note. The second value is the table number."},
    {"VOLUME", "On wave and kit sounds. 3 is full, 2 is half, 1 is a quarter, 0 is silent."},
    {"PLAY", "On a wave sound. MANUAL keeps one wave. ONCE, LOOP and PINGPONG step through the waves by themselves."},
    {"WAVE / SYNTH", "Which wave to play. With MANUAL it is a wave number. With the other PLAY settings the row says SYNTH and you pick the synth."},
    {"SPEED, LENGTH", "On a wave sound that steps through waves. SPEED is how long each wave stays. LENGTH is how many waves play."},
    {"LOOP POS", "On a wave sound set to LOOP. Where the loop starts again. It cannot go before the first wave."},
    {"LEN, OFFSET, LOOP", "On a kit sound. LEN cuts the sample short (00 plays all of it). OFFSET skips the start. The two LOOP values turn looping on or off."},
    {"SPEED, CLIP", "On a kit sound. SPEED 0.5X plays at half speed. CLIP is how two layered samples that are too loud get squashed."},
    {"PITCH (NOISE)", "FREE or SAFE, the same two choices as in LSDJ."},
    {"SHIFT + INSTR KEY", "Opens the library: save this sound to the card, or load one."},
    {"KIT 1 AND KIT 2", "On a KIT sound: the two kits. Their names show beside the numbers. A phrase note picks a sample from each, and the two play together."},
};
const Entry kTableTxt[] = {
    {"", "A table is a tiny pattern that plays by itself with a sound."},
    {"VOL", "Loudness."},
    {"TSP", "Pitch change."},
    {"CMD", "A special trick. Two per row. Hold Shift + Instr Params to see them all."},
    {"TO USE IT", "Set TABLE on the Instr page."},
    {"PLAY", "Plays a note with the sound that uses this table, so you hear it. Play again stops it. Shift + Play plays the song."},
    {"BKSP + LEFT/RIGHT", "Pick another table."},
    {"BKSP", "Clears a cell."},
    {"SHIFT+INSERT+ARROWS", "Pick many cells. The jog wheel changes them all."},
    {"COPY, SHIFT + COPY", "Copy and paste."},
};
const Entry kGrooveTxt[] = {
    {"", "A groove sets how long each step lasts. The numbers are ticks."},
    {"6 AND 6", "An even beat."},
    {"7 AND 5", "Swing."},
    {"", "The list repeats."},
    {"COMMAND G", "In a phrase, picks a groove number."},
    {"BKSP + LEFT/RIGHT", "Pick another groove."},
    {"BKSP", "Alone, it clears the step to 00. A 00 ends the groove there."},
};
const Entry kProjectTxt[] = {
    {"", "Settings for the song."},
    {"NAME", "Press Insert to type it."},
    {"TEMPO", "Bigger is faster."},
    {"TRANSPOSE", "All notes up or down."},
    {"SYNC", "Off, master or slave. For two songs."},
    {"CLONE", "Deep or slim."},
    {"LOOK", "Screen colors."},
    {"PRELISTEN", "The pads make sound when on."},
    {"CLEAN SONG DATA", "Frees chains and phrases you do not use. Press Insert twice."},
    {"CLEAN INSTR DATA", "Does the same for sounds."},
    {"SAVE SONG", "Puts the song in this Game Boy's save file on the SD card."},
    {"LOAD SONG", "Lists the songs in that file."},
    {"NEW SONG", "Asks for a name, then starts an empty song."},
    {"EXPORT WAV", "Makes an audio file."},
    {"EXPORT LSDJ", "Pick A, B or A+B with the jog wheel, then press Insert. Copies the save files to GB/Export. With your own kits it also writes PTSDJ_A.GB and PTSDJ_B.GB."},
    {"SHIFT", "Hold Shift here: F8 says FIRMWARE, and the line of technical numbers at the bottom shows."},
    {"SHIFT + F8", "Press it to load another firmware."},
};
const Entry kSynthTxt[] = {
    {"", "Synths make the shapes the WAV track plays."},
    {"EACH ROW", "Shapes the sound: filter, distort, phase and volume."},
    {"LEFT / RIGHT", "Picks a digit."},
    {"INSERT + ARROWS", "Changes a value."},
    {"PLAY", "Hears the synth."},
    {"BKSP + LEFT/RIGHT", "Picks another synth."},
    {"", "Needs GB/LSDJ.GB on the card."},
};
const Entry kWaveTxt[] = {
    {"", "A wave is 32 dots that make the shape of a sound."},
    {"< >", "Moves along."},
    {"UP / DOWN", "Moves a dot."},
    {"PLAY", "Hears the wave."},
    {"BKSP + LEFT/RIGHT", "Picks another wave."},
    {"BKSP (TAP)", "Copies the wave."},
    {"SHIFT + INSERT", "Pastes it."},
};
const Entry kFileTxt[] = {
    {"", "These are the songs for this Game Boy."},
    {"UP / DOWN", "Choose a song."},
    {"NEW SONG", "The first entry. Insert asks for a name, then starts an empty song."},
    {"INSERT", "Loads it into the song on screen."},
    {"- BEFORE A NAME", "A song in the save file."},
    {"+ BEFORE A NAME", "A loose file from GB/Songs. Saving files it in the save file."},
    {"* BEFORE A NAME", "The built-in demo for this Game Boy. Load one on A and one on B, then press Play to hear them together."},
    {"SHIFT + BKSP", "Erases the song under the cursor. Press Insert to say yes."},
    {"BKSP", "Go back."},
};
const Entry kLibraryTxt[] = {
    {"", "The library keeps your sounds on the card. Open it with Shift + the Instr key."},
    {"TOP ROW", "Saves the sound on screen. Press Insert, type a name, then press Play."},
    {"OTHER ROWS", "The files. The presets are here too."},
    {"PLAY", "Previews the file. It goes into a temporary memory, so nothing in your song changes. The pads play it too."},
    {"INSERT", "Puts the file into the sound on screen. This changes your song."},
    {"LEFT / RIGHT", "Turn a page of 13."},
    {"BKSP", "Go back."},
};

// ---- cheat sheets (Shift + Note, Shift + Instr Params) -------------------------------------------------------------------

const Entry kPhraseColsTxt[] = {
    {"NOTE", "The pitch."},
    {"INSTR", "Which sound."},
    {"CMD", "A special trick. It is a letter and a 2-digit number."},
    {"x AND y", "Each is one digit, 0 to F."},
    {"xx", "Both digits as one number, 00 to FF."},
    {"", "Turn the page with < > to see every command."},
};
const Entry kTableColsTxt[] = {
    {"VOL", "Loudness."},
    {"TSP", "Pitch change."},
    {"CMD", "A special trick. It is a letter and a 2-digit number. Two per row."},
    {"x AND y", "Each is one digit, 0 to F."},
    {"xx", "Both digits as one number, 00 to FF."},
    {"", "Turn the page with < > to see every command."},
};
const Entry kCmdAFTxt[] = {
    {"A xx", "Start table xx. A20 stops it."},
    {"B xy", "Maybe play the note. A big number means often."},
    {"C xy", "Chord: the note, then +x and +y steps."},
    {"D xx", "Delay the note by xx ticks."},
    {"E xy", "Volume x, then fade speed y."},
    {"F xy", "Fine tune (pulse)."},
};
const Entry kCmdGOTxt[] = {
    {"G xx", "Groove number."},
    {"H xx", "Hop to step x of the next phrase. HFF stops everything."},
    {"K xx", "Stop the note after xx ticks."},
    {"L xx", "Slide to the new note in xx ticks."},
    {"M xy", "Master volume."},
    {"O xx", "Output. 0 is off, 1 left, 2 right, 3 both."},
};
const Entry kCmdPZTxt[] = {
    {"P xx", "Pitch bend. FF bends down."},
    {"R xy", "Repeat the note every y ticks."},
    {"S xy", "Sweep (pulse 1) or shape (noise)."},
    {"T xx", "Tempo."},
    {"V xy", "Vibrato: x is speed, y is depth."},
    {"W xx", "Pulse width."},
    {"Z xy", "Randomize the last command."},
};
const Section kPhraseSheetDocs[] = {sec("PHRASE COLUMNS", kPhraseColsTxt), sec("COMMANDS A TO F", kCmdAFTxt), sec("COMMANDS G TO O", kCmdGOTxt), sec("COMMANDS P TO Z", kCmdPZTxt)};
const Section kTableSheetDocs[] = {sec("TABLE COLUMNS", kTableColsTxt), sec("COMMANDS A TO F", kCmdAFTxt), sec("COMMANDS G TO O", kCmdGOTxt), sec("COMMANDS P TO Z", kCmdPZTxt)};
const Entry kLoaderTxt[] = {
    {"", "These are the WAV files in the GB/Samples folder on the SD card."},
    {"UP / DOWN", "Choose a file."},
    {"INSERT", "Adds the file to the kit, as a Game Boy sample. A check mark shows the files already in it. The kit number is at the top: the jog wheel changes it."},
    {"SHIFT + INSERT", "Opens the file in the Sample Editor first, so you can cut it."},
    {"BKSP", "Takes the file's sample out of the kit again."},
    {"PLAY", "Hears the file as the Game Boy would play it."},
    {"", "Long files are cut to what fits in a kit."},
};
const Entry kEditorTxt[] = {
    {"", "Cut a sample, name it and add it to a kit."},
    {"START, END", "Where the sound begins and ends. The jog wheel, or Insert + < >, moves it a little. Insert + UP/DOWN moves it a lot."},
    {"NAME", "Press Insert, then pick 3 new letters with Insert. The keyboard starts empty. Start ends it. BKSP closes it and keeps the old name."},
    {"KIT", "The kit number to add to: 15 to 32."},
    {"SAMPLES", "Turn the jog wheel to step through the kit's samples and change one."},
    {"DELETE", "Throws the sample away. A new sample is dropped at once. A sample from the kit asks first, then is erased from the kit and gives its space back."},
    {"RED LIGHT", "While a sound plays, the red light in the frame flickers when it is loud enough to crackle if layered. Lower LEVEL until it stays dark."},
    {"LEVEL", "How loud the sample is stored: 100%, 75%, 50% or 25%. Layered sounds add together, and loud ones crackle. Use 50% for sounds you will layer. It goes back to 100% after each add."},
    {"ADD TO KIT", "A new sample: press Insert. The kit is saved at once. The status line says which note plays it."},
    {"REPLACE SAMPLE", "A sample from the kit: press Insert. It asks first, because the change is for good and can't be undone."},
    {"PLAY", "Hears the cut sound."},
    {"BKSP", "Go back."},
};
const Entry kKitTxt[] = {
    {"", "This page shows the samples in a kit."},
    {"TOP NUMBER", "The kit. Left / Right or the jog wheel changes it. Kits 15 to 32 are yours."},
    {"NAME", "Press Insert to name the kit."},
    {"A SAMPLE", "Insert opens it in the Sample Editor, in any kit. Shift + Insert renames it (your kits only). BKSP erases it: press Insert to say yes."},
    {"PLAY OR A PAD", "Hears the sample. A pad plays the sample with the same number."},
    {"FREE", "Next to the kit name: how much room is left in this kit, in seconds."},
    {"ROM KITS", "You can hear them, but not change them."},
    {"", "Every change is saved at once."},
};
const Entry kFirmwareWinTxt[] = {
    {"", "The update files in the Firmware folder."},
    {"UP / DOWN", "Pick a file."},
    {"INSERT", "Asks first. Press Insert again to flash it."},
    {"BKSP", "Go back."},
};
const Entry kRecorderTxt[] = {
    {"", "Records a sound from the line or mic input and turns it into a kit sample."},
    {"INPUT", "LINE for a cable from another device. MIC LOW or MIC HIGH for the microphone. The jog wheel changes it."},
    {"TRIGGER", "When recording starts. AT ONCE starts when you press Insert. LOW, MEDIUM and HIGH wait until the sound passes that loudness."},
    {"MONITOR", "ON lets you hear the input in the Tracker's output, so you can set the level. Turn it OFF with a microphone, or the speakers will squeal."},
    {"START / STOP", "Insert or Play. It records up to 2.8 seconds, or until you stop it."},
    {"LEVEL", "How loud the input is now. Keep it out of the very top."},
    {"AFTER", "The Sample Editor opens with the sound. Cut it and add it to a kit."},
    {"BKSP", "While recording: stops and throws it away. Otherwise goes back."},
};
const Section kRecorderDocs[] = {sec("SAMPLE RECORDER", kRecorderTxt)};
const Section kLoaderDocs[] = {sec("SAMPLE LOADER", kLoaderTxt)};
const Section kEditorDocs[] = {sec("SAMPLE EDITOR", kEditorTxt)};
const Section kKitDocs[] = {sec("KIT PAGE", kKitTxt)};
const Section kFirmwareDocs[] = {sec("LOAD FIRMWARE", kFirmwareWinTxt)};
const Section kSongDocs[] = {sec("SONG PAGE", kSongTxt)};
const Section kChainDocs[] = {sec("CHAIN PAGE", kChainTxt)};
const Section kPhraseDocs[] = {sec("PHRASE PAGE", kPhraseTxt)};
const Section kInstrDocs[] = {sec("INSTRUMENT PAGE", kInstrTxt)};
const Section kTableDocs[] = {sec("TABLE PAGE", kTableTxt)};
const Section kGrooveDocs[] = {sec("GROOVE PAGE", kGrooveTxt)};
const Section kProjectDocs[] = {sec("PROJECT PAGE", kProjectTxt)};
const Section kSynthDocs[] = {sec("SYNTH PAGE", kSynthTxt)};
const Section kWaveDocs[] = {sec("WAVE PAGE", kWaveTxt)};
const Section kFileDocs[] = {sec("SONG FILES", kFileTxt)};
const Section kLibraryDocs[] = {sec("LIBRARY", kLibraryTxt)};

#define DOC_SET(a) (n = int(sizeof a / sizeof *a), a)
const Section* docsFor(Lsdj::Popup p, Scr s, int& n) {
    n = 0;
    if (p == Lsdj::kPhraseSheet) return DOC_SET(kPhraseSheetDocs);
    if (p == Lsdj::kTableSheet) return DOC_SET(kTableSheetDocs);
    switch (s) {
        case Scr::Song: return DOC_SET(kSongDocs);
        case Scr::Chain: return DOC_SET(kChainDocs);
        case Scr::Phrase: return DOC_SET(kPhraseDocs);
        case Scr::Instr: return DOC_SET(kInstrDocs);
        case Scr::Table: return DOC_SET(kTableDocs);
        case Scr::Groove: return DOC_SET(kGrooveDocs);
        case Scr::Project: return DOC_SET(kProjectDocs);
        case Scr::Synth: return DOC_SET(kSynthDocs);
        case Scr::Wave: return DOC_SET(kWaveDocs);
        case Scr::File: return DOC_SET(kFileDocs);
        case Scr::Library: return DOC_SET(kLibraryDocs);
        case Scr::Loader: return DOC_SET(kLoaderDocs);
        case Scr::Editor: return DOC_SET(kEditorDocs);
        case Scr::Kit: return DOC_SET(kKitDocs);
        case Scr::Firmware: return DOC_SET(kFirmwareDocs);
        case Scr::Recorder: return DOC_SET(kRecorderDocs);
        default: return DOC_SET(kHelpDocs);
    }
}
#undef DOC_SET

int totalPages(const Section* d, int n) {
    int pages = 0, c;
    for (int i = 0; i < n; ++i) pages += paginate(d[i], -1, nullptr, c);
    return pages;
}

// A page of text, counted across the sections: a title bar with the page counter, up to 13 left aligned rows starting one column in from the edge
// (names in bold), and a hint at the bottom.
void drawDoc(Lcd& l, const Section* d, int n, int page, int x0, int w, const char* foot) {
    int total = totalPages(d, n), first = 0;
    page = clampi(page, 0, std::max(0, total - 1));
    int s = 0, c;
    for (; s < n - 1; ++s) {
        const int pages = paginate(d[s], -1, nullptr, c);
        if (page < first + pages) break;
        first += pages;
    }
    DLine rows[kDocLines + 1];
    paginate(d[s], page - first, rows, c);
    char t[24];
    std::snprintf(t, sizeof t, "%d/%d", page + 1, total);
    l.rect(x0, 8, w, 8, 3);
    l.text(1, 1, d[s].title, true);
    l.text(23 - int(std::strlen(t)), 1, t, true);                // the page counter sits at the right end of the title bar
    for (int i = 0; i < c && i < kDocLines; ++i) {
        if (!rows[i].len) continue;
        char row[kDocW + 1];
        std::memcpy(row, rows[i].s, size_t(rows[i].len));
        row[rows[i].len] = 0;
        if (rows[i].bold) l.boldText(1, 3 + i, row); else l.text(1, 3 + i, row);
    }
    l.text(24 - int(std::strlen(foot)), 16, foot);               // right aligned: the page tab under the bezel covers the left end
}
}  // namespace

bool Lsdj::docsFit() {
    auto fits = [](const Section* d, int n) {
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < d[i].n; ++k) {
                const Entry& e = d[i].e[k];
                if (e.term && std::strlen(e.term) > size_t(kDocW)) return false;
                DLine rows[kDocLines + 2];
                if (entryRows(e, rows, kDocLines + 2) > kDocLines) return false;          // an entry has to fit on a page
                for (const char* p = e.text; p && *p;) {                                   // no word longer than a row
                    int len;
                    const char* nx = wrapNext(p, len);
                    if (len == kDocW && p[len] != ' ' && p[len] != 0) return false;
                    p = nx;
                }
            }
        return true;
    };
    int n;
    bool ok = true;
    for (Popup p : {kPhraseSheet, kTableSheet}) { const Section* d = docsFor(p, Scr::Song, n); ok = ok && fits(d, n); }
    for (Scr s : {Scr::Song, Scr::Chain, Scr::Phrase, Scr::Instr, Scr::Table, Scr::Groove, Scr::Project, Scr::Synth, Scr::Wave, Scr::File, Scr::Library, Scr::Loader, Scr::Editor, Scr::Kit, Scr::Firmware, Scr::Recorder, Scr::Help}) {
        const Section* d = docsFor(kPageHelp, s, n);
        ok = ok && fits(d, n);
    }
    return ok;
}

int Lsdj::popupPages() const { int n; const Section* d = docsFor(popup_, scr_, n); return totalPages(d, n); }
void Lsdj::popupTurn(int d) { popupPage_ = clampi(popupPage_ + d, 0, std::max(0, popupPages() - 1)); }
int Lsdj::helpPages() const { int n; const Section* d = docsFor(kPageHelp, Scr::Help, n); return totalPages(d, n); }

void Lsdj::drawPopup(Lcd& l) const {
    int n;
    const Section* d = docsFor(popup_, scr_, n);
    const int pages = totalPages(d, n);
    l.rect(0, 0, 200, 144, 3);                       // covers the page and the side panel; a border of the dark shade
    l.rect(2, 2, 196, 140, 0);
    drawDoc(l, d, n, clampi(popupPage_, 0, pages - 1), 2, 196, pages > 1 ? "PAGE: < > OR F1 F2" : "");
}

void Lsdj::drawHelp(Lcd& l) const {
    int n;
    const Section* d = docsFor(kPageHelp, Scr::Help, n);
    drawDoc(l, d, n, clampi(helpPage, 0, helpPages() - 1), 0, 200, "<> PAGE  BKSP EXIT");
}

}  // namespace gb
