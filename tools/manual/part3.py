from lib import *

def build():
    s = []
    # ================================================================= 7
    s += H1('7. Playing and recording')
    s += [H2('Play'),
          P('[Play] starts and stops the music. What it plays depends on the page you are on:')]
    s += table([['Page', 'Press [Play] to hear...', '[Shift] + [Play]'],
                ['Song', 'The song, from the row the cursor is on.', ''],
                ['Chain', 'Just this chain, looping, on the track you came from.', 'The whole song from here.'],
                ['Phrase', 'Just this phrase, looping, on its track.', 'The whole song from here.'],
                ['Table', 'A note using the sound that uses this table. Press Play again to stop.', 'The whole song.'],
                ['Synth, Wave', 'The shape you are working on.', ''],
                ['Sample pages', 'The sample you are on (Chapter 11).', ''],
                ['Library', 'A preview of the highlighted sound (Chapter 10).', '']], [1.2 * inch, 3.3 * inch, BODY_W - 4.5 * inch])
    s += [fig('34-song-playing', 'A song playing. The small arrows next to the rows show where each track is.', 4.2 * inch),
          H2('Muting and soloing tracks'),
          P('On the Song page, hold [Shift] and press [Insert] to mute the track under the cursor. Press it again to bring it back. [Shift] + [Delete] mutes all four tracks. This is a great way to hear one part at a time.'),
          H2('Tempo and swing'),
          P('Set the tempo on the Project page, or with the T command. Swing comes from the Groove page: make the first step a little longer than the second, such as 07 and 05.'),
          H2('The pads'),
          P('The pads play the sound that is on the screen. The instrument you are on decides the track: pulse on PU1 or PU2, wave and kit on WAV, noise on NOI. The pads never start the song.'),
          bullets(['Hold [Shift] and press a pad row to change the octave. The side panel shows O+1, O-1 and so on.', 'On a kit sound, pad 1 plays sample 1, pad 2 plays sample 2, and so on.']),
          H2('Writing notes with the pads'),
          P('There are three ways to use the pads on the Phrase page. [Rec] decides which one you get.')]
    s += table([['Mode', 'What the pads do'],
                ['[Rec] off', 'Step mode. Press a pad and its note is typed at the cursor. The cursor moves down for the next one.'],
                ['[Rec] on', 'The pads only make sound. Turn the jog wheel to pick the sound. Nothing is written.'],
                ['[Rec] on, then [Play]', 'Live recording. The phrase loops on its track while the whole song plays behind it. The pads write the notes you play, placed on the nearest step. Press [Play] to stop.']], [1.7 * inch, BODY_W - 1.7 * inch])
    s += [callout('tip', 'Recording lines up with what you hear. PTSDJ takes the delay between pressing and hearing into account.')]

    # ================================================================= 8
    s += H1('8. Two Game Boys at once')
    s += [P('PTSDJ has two pretend Game Boys. Each has its own song, its own tempo, and its own drum kits. Together they give you 8 tracks.'),
          H2('Which one am I on?'),
          P('The title of the Song page tells you. <b>SONG A 1-4</b> is the first Game Boy. <b>SONG B 5-8</b> is the second. Hold [Shift] and press [Song] to swap.'),
          fig2('20-song', 'Song A: tracks 1 to 4.', '63-song-b', 'Song B: tracks 5 to 8.'),
          H2('Starting both together'),
          P('When both songs have <b>SYNC</b> set, [Play] on either one starts both. Do it like this:'),
          steps(['On song A, open the Project page. Set SYNC to <b>MASTER</b>.', 'Hold [Shift] and press [Song] to go to song B. Open the Project page. Set SYNC to <b>SLAVE</b>.', 'Press [Play]. Both play together.'])]
    s += [fig('70-sync-master', 'SYNC set to MASTER on song A.', 3.8 * inch),
          P('The <b>master</b> sets the tempo. The <b>slave</b> follows it. If you change the master\'s tempo, the slave\'s tempo changes too.'),
          H2('Side-by-side view'),
          P('Press [Master] while the songs are linked. Both Game Boys show next to each other on the screen. The one you are working on is bright. The other is dim.'),
          fig('71-dual', 'Both Game Boys side by side.', 5.8 * inch, box=(0, 52, 800, 348)),
          P('Look at the bottom corners. A letter <b>A</b> sits above F1. A letter <b>B</b> sits above F8. Hold either key and a dock rises. It names what every F key does:'),
          fig('72-dual-dock', 'The dock. Volume and mute keys for each Game Boy.', 5.2 * inch, box=(0, 0, 800, 480))]
    s += table([['Keys', 'What they do'],
                ['F1 and F8', 'Pick song A or song B. The keys now control that one.'],
                ['F2 + jog wheel', 'Volume of song A.'],
                ['F7 + jog wheel', 'Volume of song B.'],
                ['F3 to F6', 'Mute track 1 to 4 of the active Game Boy. Hold [Shift] too to play only that track.']], [1.6 * inch, BODY_W - 1.6 * inch])

    # ================================================================= 9
    s += H1('9. Saving and loading')
    s += [P('Each Game Boy keeps its songs in its own save file on the SD card. It works like a real LSDJ cartridge. One file holds up to <b>32 songs</b>.')]
    s += table([['File', 'What is in it'],
                ['GB/Saves/A.srm', 'The songs for Game Boy A.'],
                ['GB/Saves/B.srm', 'The songs for Game Boy B.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [H2('Save'),
          P('Open the Project page. Move to <b>SAVE SONG</b> and press [Insert]. The bottom line says SAVED. The song is filed under the song\'s name. If you save with the same name, the old song is replaced. A new name adds a new song. If all 32 spots are full you see BANK FULL.'),
          callout('care', 'Save before you start a NEW SONG or load another song. Loading replaces the song on the screen.'),
          H2('Load'),
          P('Press [File], or choose LOAD SONG on the Project page. The list shows the songs for this Game Boy.'),
          fig('11-load-song', 'The song list. NEW SONG is the first line. A + sign shows a loose file from GB/Songs.', 4.0 * inch)]
    s += table([['Key', 'What it does'],
                ['Up or Down', 'Pick a song.'],
                ['[Insert]', 'Load the song. It replaces the song on the screen.'],
                ['[Shift] + [Delete]', 'Erase the picked song from the save file. Press [Insert] to say yes.'],
                ['[Delete]', 'Go back.']], [1.7 * inch, BODY_W - 1.7 * inch])
    s += [P('A <b>-</b> before a name means the song is in the save file. A <b>+</b> means it is a loose file in GB/Songs. A loose song moves into the save file the next time you save. A <b>*</b> means it is the built-in demo song for this Game Boy. It is always in the list and you cannot erase it.'),
          H2('New song'),
          P('The first line of the song list is <b>NEW SONG</b>. The Project page also has a NEW SONG row. Press [Insert] and type a name. PTSDJ starts an empty song. Your kits and your saved sounds are still there.'),
          fig2('80-newsong-name-empty', 'Typing a name.', '81-newsong-name-typed', 'The name DEMO.'),
          H2('When PTSDJ starts'),
          P('Each Game Boy opens the song you saved last. If neither Game Boy ever saved a song, they open with the demo songs. If only one of them saved a song, the other one opens empty.'),
          H2('The demo songs'),
          P('DEMO_A has the melody. DEMO_B has the beat and the bass. They are made to be one piece of music: the same key (A minor), the same speed (120 beats a minute), and the same length (16 bars, about 32 seconds). SYNC is on in both, so pressing [Play] on either Game Boy starts both. Load <b>*DEMO_A</b> on Game Boy A and <b>*DEMO_B</b> on Game Boy B to bring them back after you change them. You can also use them as a starting point: change a note, save under a new name, and the original stays in the list.'),
          H2('Song files from LSDJ'),
          P('PTSDJ can read LSDJ song files (.lsdsng). Put them in <b>GB/Songs</b>. They show up in the song list with a +. When you export, PTSDJ also writes a .lsdsng of your song.'),
          H2('Audio files'),
          P('<b>EXPORT WAV</b> on the Project page makes an audio file in <b>GB/Export</b>. It plays the song once, until every track starts to repeat. You can share it, or play it on a computer.'),
          H2('Tidy up'),
          P('<b>CLEAN SONG DATA</b> and <b>CLEAN INSTR DATA</b> on the Project page free up chains, phrases and instruments that nothing uses. Press [Insert] twice to confirm. This makes the song file smaller.')]

    # ================================================================= 10
    s += H1('10. The instrument library')
    s += [P('The library keeps your sounds on the SD card so you can use them in any song. Hold [Shift] and press [Instrument] to open it.'),
          fig('41-library', 'The library. The first line saves a sound. The others are files.', 4.2 * inch),
          H2('Saving a sound'),
          steps(['Pick the sound you want on the Instrument page.', 'Hold [Shift] and press [Instrument]. The top row says SAVE (the sound\'s name) AS.', 'Press [Insert]. Type a name. Press [Play] for OK.'])]
    s += [H2('Using a sound'),
          steps(['Press Up or Down to pick a file.', 'Press [Play] to <b>preview</b> it. Nothing in your song changes. The pads play it too.', 'Press [Insert] to put it into your song, in the instrument you were on.'])]
    s += table([['Key', 'What it does'],
                ['Up or Down', 'Pick a file.'],
                ['[Play]', 'Preview the sound. It goes into temporary memory, so your song stays as it is.'],
                ['[Insert]', 'Put it in the current instrument. This does change your song.'],
                ['Left or Right', 'Turn a page of 13 files.'],
                ['[Delete]', 'Go back.']], [1.7 * inch, BODY_W - 1.7 * inch])
    s += [callout('note', 'A saved sound that uses a table brings its table along. If no empty table is free, the sound loads without it.'),
          H2('The presets'),
          P('There are <b>272 ready-made pulse and noise sounds</b> in a set of preset files. Copy them to <b>GB/Instruments</b> on the SD card. They are named for how they sound, like KICK, SNARE, HAT, LEAD and PLUK. They are ready to load into any song.')]

    # ================================================================= 11
    s += H1('11. Drum kits and samples')
    s += [P('The WAV track can play short recorded sounds, like drums. A set of these sounds is called a <b>kit</b>. A kit holds up to <b>15 sounds</b> and about <b>2.8 seconds</b> in total.'),
          P('Game Boy sound is very rough. PTSDJ changes your sounds into the Game Boy style (4-bit, about 11,000 samples a second). This gives that crunchy retro sound.'),
          H2('Kit numbers'),
          P('Each kit has a number (hex):')]
    s += table([['Kits', 'What they are'],
                ['00 to 14', 'Built into the LSDJ ROM. You can hear them, but not change them.'],
                ['15 to 32', '<b>Yours.</b> There are 30 free slots. Each Game Boy has its own set.']], [1.4 * inch, BODY_W - 1.4 * inch])
    s += [H2('Making a kit from WAV files'),
          steps(['Copy WAV files into <b>GB/Samples</b> on the SD card. Use plain WAV files (8, 16 or 24 bit). Short sounds (under 2 seconds) work best. Any sample rate is fine.',
                 'Press [Sample Loader]. The kit number is at the top. Turn the jog wheel to pick a kit between 15 and 32.',
                 'Pick a file with Up and Down. Press [Play] to hear it as a Game Boy would play it.',
                 'Press [Insert]. The file is changed and added to the kit. A check mark appears next to it.'])]
    s += [fig('50-sample-loader', 'The Sample Loader. A check mark means the file is already in the kit.', 4.2 * inch)]
    s += table([['Key', 'What it does'],
                ['Up or Down', 'Pick a file.'],
                ['[Insert]', 'Add the file to the kit.'],
                ['[Shift] + [Insert]', 'Open it in the Sample Editor first, so you can cut it.'],
                ['[Delete]', 'Take that file\'s sound out of the kit.'],
                ['[Play]', 'Hear it.'],
                ['Jog wheel', 'Pick the kit number.']], [1.9 * inch, BODY_W - 1.9 * inch])
    s += [callout('tip', 'Long files are cut to fit the kit. Every change is saved to the SD card at once.')]
    s += [H2('The Sample Editor'),
          P('Press [Sample Editor] to cut a sound, name it, and add it to a kit. You see the shape of the sound. The two lines show where it starts and ends.'),
          fig('51-sample-editor', 'The Sample Editor.', 4.2 * inch)]
    s += table([['Row', 'What it does'],
                ['START and END', 'Where the sound begins and ends. Turn the jog wheel to move a little. Hold [Insert] and press Up or Down to move a lot.'],
                ['NAME', 'Three letters. Press [Insert]. The keyboard starts empty. Pick three letters with [Insert], then press [Play] for OK. [Delete] cancels and keeps the old name.'],
                ['KIT', 'Which kit to add to (15 to 32).'],
                ['SAMPLES', 'Shows how many sounds the kit has and how much time is free. Turn the jog wheel to step through the sounds already in the kit so you can change one.'],
                ['LEVEL', 'How loud the sound is stored: 100%, 75%, 50% or 25%. Use a lower level for sounds you will layer. It goes back to 100% after each add.'],
                ['ADD TO KIT', 'Press [Insert] to add the sound as a new one. The kit is saved at once. When you are on one of the kit\'s own sounds, this row says REPLACE SAMPLE instead.'],
                ['DELETE THIS SAMPLE', 'Throws the sound away. If it is from the kit, the row says DELETE SAMPLE n FROM KIT. It asks first, then erases it and frees its space.'],
                ['[Play]', 'Hears the cut sound.']], [1.7 * inch, BODY_W - 1.7 * inch])
    s += [callout('care', 'Replacing or deleting a sound that is already in a kit cannot be undone. PTSDJ asks you first.'),
          H3('The red light'),
          P('While a sound plays on the sample pages, watch the little red light on the screen frame. It <b>flickers</b> when the sound is very loud. Loud sounds crackle when you layer them. If it flickers, lower LEVEL until the light stays dark.')]
    s += [H2('The kit page'),
          P('Press [Sample Playback] to see the sounds in a kit. Use Left and Right at the top to change the kit number.'),
          fig('52-kit-page', 'The kit page. Each line is one sound with its length. FREE shows the room left.', 4.2 * inch)]
    s += table([['Key', 'What it does'],
                ['Up or Down', 'Pick a sound.'],
                ['[Play] or a pad', 'Hear it. Pad 1 plays sound 1, and so on.'],
                ['[Insert]', 'Open the sound in the Sample Editor. It works on any kit, even the ROM kits, so you can copy a sound into your own kit.'],
                ['[Shift] + [Insert]', 'Rename the sound (your kits only). Type three letters, then press [Play].'],
                ['[Delete]', 'Erase the sound. Press [Insert] to say yes.'],
                ['NAME row', 'Press [Insert] to name the kit (up to 8 letters).']], [1.9 * inch, BODY_W - 1.9 * inch])
    s += [H2('Using a kit in a song'),
          steps(['Go to the Instrument page. Set TYPE to <b>KIT</b>.', 'Set <b>KIT 1</b> to your kit number (for example 15).', 'Go to the Phrase page. Press [Insert]. The NOTE box shows the <b>name of a sample</b>, like KIC or HAT.',
                 'Hold [Insert] and press Left or Right to pick another sound from the kit.'])]
    s += [fig('60-kit-layer-instr', 'A kit instrument with KIT 1 and KIT 2 both set to kit 15.', 4.0 * inch)]
    s += [H2('Layering two sounds'),
          P('A step can play a sound from KIT 1 and a sound from KIT 2 <b>at the same time</b>. This makes bigger drums. Do it like this:'),
          steps(['On the Instrument page, set both KIT 1 and KIT 2.', 'On the Phrase page, put in a note. Hold [Insert] and press Left or Right to pick the KIT 1 sound.', 'Hold [Insert] and press Up or Down to add a KIT 2 sound. The step shows both names with a plus, like BAS+HAT.']),
          fig('61-kit-layer-phrase', 'Layered steps show two names: the KIT 1 sound, a plus, and the KIT 2 sound.', 4.2 * inch)]
    s += [P('If you press [Insert] with Up or Down on an <b>empty</b> step, you get only the KIT 2 sound. The KIT 1 part stays empty (---).'),
          callout('note', 'Two sounds are <b>added</b> together. If both are loud, the sum is too big and it crackles (the same way real LSDJ does it). Add sounds you plan to layer with LEVEL 50%. Watch the red light in the Sample Editor.'),
          callout('care', 'Only one sound can play on the WAV track at a time. A new note on that track cuts the old one.')]
    s += [H2('Recording your own sound'),
          P('The Tracker can record from its <b>line input</b> or its <b>microphone</b> and turn the sound into a kit sample.'),
          fig('53-recorder', 'The Sample Recorder.', 4.2 * inch)]
    s += table([['Row', 'What it does'],
                ['INPUT', 'LINE for a cable from another device. MIC LOW or MIC HIGH for a microphone. Turn the jog wheel to pick.'],
                ['TRIGGER', 'When recording starts. AT ONCE starts when you press [Insert]. LOW, MEDIUM or HIGH wait until the sound is that loud.'],
                ['MONITOR', 'ON lets you hear the input through the Tracker so you can set the level. Turn it OFF with a microphone, or the speakers will squeal.'],
                ['START / STOP', 'Press [Insert] or [Play]. It records up to 2.8 seconds, or until you press it again.'],
                ['LEVEL', 'A meter showing how loud the input is. Keep it out of the very top.']], [1.5 * inch, BODY_W - 1.5 * inch])
    s += [steps(['Press [Sample Recorder]. Pick the INPUT.', 'Set TRIGGER to MEDIUM so it starts when the sound starts.', 'Move to START and press [Insert]. The screen says WAITING FOR SOUND.', 'Make the sound. When it stops, the Sample Editor opens with your sound, named REC.', 'Cut it, name it, and add it to a kit.']),
          fig2('54-recording', 'Recording in progress.', '55-after-record', 'The Sample Editor with the new sound.'),
          callout('tip', '[Delete] while recording stops and throws the recording away. [Delete] when not recording goes back.'),
          H2('Tips for good drum sounds'),
          bullets(['Pick short sounds. A kick or a hat is only a fraction of a second.', 'Trim the quiet start and the long tail with START and END.', 'Loud, simple sounds work best. Quiet, complex sounds turn to hiss.',
                   'Give each sound a short name so you remember it. KIC, SNR, HAT, CLP.', 'A kit has 2.8 seconds in all. Short sounds let you fit more of them.'])]

    # ================================================================= 12
    s += H1('12. Taking your songs to a real Game Boy')
    s += [P('When you finish a song, you can play it on a real Game Boy that runs LSDJ, on a flash cartridge such as an EverDrive, or in an emulator. This chapter shows how.'),
          H2('What you get'),
          P('On the Project page, move to <b>EXPORT LSDJ</b>. Turn the jog wheel to pick <b>A</b>, <b>B</b> or <b>A+B</b>. Press [Insert]. A progress bar runs. The files go in <b>GB/Export</b>:')]
    s += table([['File', 'What it is'],
                ['PTSDJ_A.srm', 'The save file for Game Boy A. It holds your songs.'],
                ['PTSDJ_A.gb', 'The LSDJ ROM with Game Boy A\'s drum kits inside.'],
                ['PTSDJ_B.srm and PTSDJ_B.gb', 'The same for Game Boy B.'],
                ['(song name).lsdsng', 'A single song in the usual LSDJ song format.']], [2.2 * inch, BODY_W - 2.2 * inch])
    s += [H2('Using them'),
          steps(['Take the SD card to your computer. Copy <b>PTSDJ_A.gb</b> and <b>PTSDJ_A.srm</b> from GB/Export.', 'Put both files on your flash cartridge or in your emulator\'s folder. <b>Keep the same name</b> for the two files. The save file goes with the ROM of the same name.',
                 'Start the ROM. Open your song in LSDJ. Press Start.'])]
    s += [callout('care', 'Always use the .srm with the .gb that has the same name. The .gb holds your drum kits. Without it, your kit sounds are missing.'),
          H2('Why a new ROM?'),
          P('In LSDJ the drum kits are part of the ROM. They are not part of the song. So PTSDJ builds a ROM that has <b>your kits</b> in the empty kit slots 15 to 32. It also fixes the ROM\'s check numbers so flash carts accept it. Only the kit parts and those numbers change. The rest of the ROM is exactly your LSDJ.'),
          H2('Songs that sound a little different'),
          P('PTSDJ copies LSDJ very closely. But the real Game Boy is the final word. A few small things can sound a little different, like the timing of a vibrato or a sweep. If a sound is not what you expected, trust the Game Boy and adjust the song.'),
          H2('Checklist'),
          bullets(['You have your own <b>GB/LSDJ.GB</b> on the SD card.', 'You saved the song before you exported.', 'You copied the .gb and the .srm with the same name.', 'Your kit numbers are between 15 and 32.', 'On the Game Boy, you loaded the song from the save file.'])]
    return s
