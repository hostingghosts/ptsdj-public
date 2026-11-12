from lib import *

def build():
    s = []
    # ================================================================= 13
    s += H1('13. Updating the firmware')
    s += [P('You can load a new firmware two ways. Use whichever is easier.'),
          H2('Way 1: from inside PTSDJ'),
          steps(['Copy the new <b>.ptf</b> file into the <b>Firmware</b> folder on the SD card.', 'Open the Project page. Hold [Shift]. The ? over F8 turns into <b>FIRMWARE</b>.', 'Press F8. A window lists the files in the Firmware folder.',
                 'Pick one with Up and Down. Press [Insert]. It asks if you are sure. Press [Insert] again to install.', 'The Tracker restarts when it is done.'])]
    s += [fig2('42-project-shift', 'Hold Shift on the Project page: F8 says FIRMWARE.', '43-firmware', 'The firmware window. The current version is at the top.'),
          callout('tip', 'Any other key at the question says no. [Delete] goes back.'),
          H2('Way 2: the restart file'),
          P('Put the .ptf file and <b>ot_restore.txt</b> in the Firmware folder. The text file has one line: the exact name of the .ptf. Restart the Tracker. This works even when PTSDJ is not installed yet.'),
          H2('Going back to the normal Tracker'),
          P('Pick the stock Polyend .ptf file the same way. Your songs, kits and sounds stay safe on the SD card.')]

    # ================================================================= 14
    s += H1('14. When something goes wrong')
    s += [P('Find your problem in the left column.')]
    s += table([['What is wrong', 'What to try'],
                ['I hear nothing when I press Play.', 'Is the track muted? On the Song page, [Shift] + [Insert] mutes and unmutes. Check the instrument: OUTPUT must not be OFF. Check the volume of the instrument and the Tracker\'s own volume. Is there a note in the phrase and a chain in the song?'],
                ['The pads make no sound.', 'PRELISTEN on the Project page may be OFF. Turn it ON. Or turn [Rec] on and use the pads on the Phrase page.'],
                ['The Synth page, the Wave page or the kits do not work.', 'They need <b>GB/LSDJ.GB</b> on the SD card. Check the name and the folder.'],
                ['My kit sounds are missing on the Game Boy.', 'Use the <b>.gb</b> that PTSDJ exported, together with the .srm of the same name. Kit numbers must be 16 to 33. A song that uses a kit your ROM does not have plays noise.'],
                ['I cannot change kits 01 to 15.', 'They are the ROM\'s own kits. Copy a sound out of one (Sample Playback, [Insert]) into your own kit 16 to 33.'],
                ['Layered drums crackle.', 'The two sounds are too loud together. Lower LEVEL in the Sample Editor to 50% when you add them. Watch the red light. Songs made with an early version of PTSDJ are repaired when you save or export.'],
                ['Sample times on the screen show only an S.', 'This was fixed in version 0.8.5. Use version 1.0 or newer.'],
                ['SAVE says BANK FULL.', 'The save file has 32 songs, or it is out of room. Erase an old song in the song list ([Shift] + [Delete]), or run CLEAN SONG DATA.'],
                ['SAVE FAILED or CAN\'T FLASH.', 'Check the SD card. It must not be locked, full or loose. Try saving again.'],
                ['The recorder hears nothing.', 'Check the cable and the INPUT row. Try TRIGGER set to AT ONCE. You see NOTHING HEARD if the sound is too quiet.'],
                ['The recorder squeals.', 'MONITOR is ON with a microphone and speakers. Turn MONITOR OFF.'],
                ['The firmware update does not start.', 'The ot_restore.txt must hold only the file name, with no extra lines or spaces. The .ptf must be in the Firmware folder.'],
                ['My Mac left strange files on the card.', 'Files that start with ._ are hidden by PTSDJ. You can ignore them.'],
                ['It sounds a little different from LSDJ on the Game Boy.', 'Some small timing details differ. The real Game Boy is the final word. Chapter 12 explains.'],
                ['I want to start over.', 'Use NEW SONG. Or erase songs in the song list. Or go back to the stock firmware (Chapter 13).']], [2.2 * inch, BODY_W - 2.2 * inch])

    # ================================================================= 15
    s += H1('15. Words to know')
    s += table([['Word', 'What it means'],
                ['BKSP', 'Backspace. The [Delete] key. Clears things or goes back.'],
                ['Chain', 'A list of up to 16 phrases.'],
                ['Command', 'A letter and two digits that does a special trick on one step.'],
                ['Cursor', 'The bright box that shows where you are.'],
                ['Envelope', 'How the loudness of a note changes over time.'],
                ['Export', 'Make files you can use somewhere else, like on a Game Boy.'],
                ['Groove', 'A list that says how many ticks each step lasts.'],
                ['Hex', 'Counting with the letters A to F after 9.'],
                ['Instrument', 'A sound you can play.'],
                ['Kit', 'A set of up to 15 short recorded sounds.'],
                ['Layer', 'Play two sounds at the same time.'],
                ['LSDJ', 'Little Sound DJ, a music program for the Game Boy.'],
                ['Octave', 'Twelve half steps. The same note, but higher or lower.'],
                ['Phrase', 'Sixteen steps. Where the notes live.'],
                ['ROM', 'The program file for a Game Boy cartridge.'],
                ['Sample', 'A short recorded sound.'],
                ['Song', 'The big plan that tells each track which chain to play.'],
                ['Step', 'One of the 16 beats in a phrase.'],
                ['SRM', 'The file type for a Game Boy save file.'],
                ['Table', 'A tiny pattern that changes a sound while it plays.'],
                ['Tick', 'One tiny tap of the music clock. A step lasts some ticks.'],
                ['Track', 'One of the four sound makers: PU1, PU2, WAV, NOI.'],
                ['Transpose', 'Move notes higher or lower.']], [1.5 * inch, BODY_W - 1.5 * inch])

    # ================================================================= A
    s += H1('Appendix A: Every key')
    s += table([['Keys', 'Do this'],
                ['[Insert]', 'Put in a value. Open the name keyboard. Press the main button.'],
                ['[Insert] + Left or Right', 'Change by 1.'],
                ['[Insert] + Up or Down', 'Change by 16, or by an octave on notes.'],
                ['Jog wheel', 'Change a number. Pick a kit. Pick a sound. Same as [Insert] + Left or Right.'],
                ['[Delete]', 'Clear a box. Go back. Say no.'],
                ['[Delete] + Left or Right', 'Pick another chain, instrument, table, groove or wave.'],
                ['[Delete] + Up or Down', 'Pick another phrase (Phrase page).'],
                ['[Shift] + Left, Right, Up, Down', 'Walk to the next page.'],
                ['[Shift] + [Insert] + arrows', 'Pick many boxes.'],
                ['[Copy]', 'Copy the picked boxes.'],
                ['[Shift] + [Copy]', 'Paste.'],
                ['[Play]', 'Start or stop. Plays just a chain, phrase or table on those pages.'],
                ['[Shift] + [Play]', 'Play the whole song from the Chain, Phrase and Table pages.'],
                ['[Rec]', 'Recording from the pads.'],
                ['[Shift] + [Insert] (Song page)', 'Mute or unmute a track.'],
                ['[Shift] + [Delete] (Song page)', 'Mute all four tracks.'],
                ['[Shift] + [Delete] (song list)', 'Erase a song from the save file.'],
                ['[Shift] + [Song]', 'Swap song A and song B.'],
                ['[Shift] + [Instrument]', 'Open the instrument library.'],
                ['[Shift] + [Note] (Phrase page)', 'The command cheat sheet.'],
                ['[Shift] + [Instrument Parameters] (Table page)', 'The command cheat sheet.'],
                ['[Shift] + a pad row', 'Change the octave of the pads.'],
                ['[Shift] + [Insert] (Sample Loader)', 'Open the file in the Sample Editor.'],
                ['[Shift] + [Insert] (kit page)', 'Rename a sound.'],
                ['[Shift] + F8 (Project page)', 'Open the firmware window.'],
                ['Hold F8', 'A pop-up about the page you are on.'],
                ['F1 and F2 (while the pop-up shows)', 'Turn its pages back and forward.'],
                ['[Master]', 'Show both Game Boys side by side.']], [2.6 * inch, BODY_W - 2.6 * inch])

    s += H1('Appendix B: Limits')
    s += table([['Thing', 'How many'],
                ['Tracks', '4 per Game Boy: PU1, PU2, WAV, NOI. 8 in all.'],
                ['Song rows', '256'],
                ['Chains', '128 (00 to 7F), each with up to 16 phrases'],
                ['Phrases', '255 (00 to FE), each with 16 steps'],
                ['Instruments', '64 (00 to 3F)'],
                ['Tables', '32 (00 to 1F), each with 16 rows'],
                ['Grooves', '32 (00 to 1F), each with 16 steps'],
                ['Synths', '16 (0 to F)'],
                ['Tempo', '28 to FF (hex)'],
                ['Songs in one save file', '32'],
                ['Your own kits', '30 (kits 16 to 33) per Game Boy'],
                ['Sounds in a kit', '15, and about 2.8 seconds in all'],
                ['Recording', 'Up to 2.8 seconds at a time'],
                ['Instruments in the library', 'Up to 400 files']], [2.2 * inch, BODY_W - 2.2 * inch])

    s += H1('Appendix C: About PTSDJ')
    s += [P('PTSDJ is a personal project that makes the Polyend Tracker work like a Game Boy music program. It is for your own use.'),
          bullets(['PTSDJ is built on the open-source Polyend Tracker firmware, which allows <b>non-commercial</b> use only.',
                   'LSDJ (Little Sound DJ) was made by Johan Kotlinski. PTSDJ does not include LSDJ. You need your own copy of the ROM.',
                   'PTSDJ is not made by or connected to Polyend or to the makers of LSDJ.',
                   'Normal Polyend Tracker projects are never touched by PTSDJ. You can always go back to the stock firmware.']),
          H2('Version'), P('This guide matches PTSDJ version 1.0.1.')]
    return s
