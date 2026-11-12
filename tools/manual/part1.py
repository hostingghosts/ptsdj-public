from lib import *

def build():
    s = []
    # ---------------------------------------------------------------- 1
    s += H1('1. Welcome')
    s += [P('PTSDJ turns your Polyend Tracker into a music maker that works like the Game Boy program <b>LSDJ</b>. If you have never used LSDJ, that is fine. This manual starts at the beginning.'),
          P('You do not need to read music. You do not need to know how to code. You will press keys, watch the screen, and listen.'),
          H2('What can you do with PTSDJ?'),
          bullets(['Write songs with 8 tracks: 4 for each of two pretend Game Boys.', 'Make sounds: beeps, wobbly waves, drums and noise.', 'Make your own drum kits from WAV files or from a microphone or line cable.',
                   'Save your songs on the SD card.', 'Send your songs to a <b>real Game Boy</b> running LSDJ, to a flash cartridge, or to an emulator.']),
          H2('How music is built'),
          P('LSDJ-style music is built from small parts. The small parts fit into bigger parts. It is like building with blocks.')]
    s += table([['Part', 'What it is', 'How big'],
                ['Step', 'One beat of music. It can hold a note, a sound choice and a command.', 'The smallest part'],
                ['Phrase', 'A row of 16 steps. The notes live here.', '16 steps'],
                ['Chain', 'A list of phrases that play one after another.', 'Up to 16 phrases'],
                ['Song', 'A big plan. It lists which chain plays on each track.', '256 rows, 4 tracks'],
                ['Instrument', 'A sound you can play. You pick one for each step.', '64 instruments'],
                ['Table', 'A tiny pattern that changes a sound while it plays.', '32 tables, 16 rows each']], [1.1 * inch, 3.9 * inch, BODY_W - 5.0 * inch])
    s += [P('Here is how it works. You write a <b>phrase</b> with a few notes. You put the phrase in a <b>chain</b>. You put the chain in the <b>song</b>. Then you press [Play].'),
          P('Why is this good? You write a phrase once and use it again and again. A short drum beat can play all through a song. You only change it where you want to.'),
          H2('The four tracks'),
          P('A Game Boy has four sound makers. PTSDJ calls them <b>tracks</b>. Each track makes a different kind of sound:')]
    s += table([['Track', 'Name', 'What it makes'],
                ['1', 'PU1 (pulse 1)', 'Square-wave beeps. Good for tunes and bass.'],
                ['2', 'PU2 (pulse 2)', 'Also square-wave beeps. Good for a second tune.'],
                ['3', 'WAV (wave)', 'Wobbly wave sounds. Also plays drum kits (short recorded sounds).'],
                ['4', 'NOI (noise)', 'Hissing noise. Good for hi-hats and crashes.']], [0.7 * inch, 1.5 * inch, BODY_W - 2.2 * inch])
    s += [P('Which track plays a sound depends on the <b>instrument type</b>. A pulse instrument plays on PU1 or PU2. A wave or kit instrument plays on WAV. A noise instrument plays on NOI.'),
          H2('Two Game Boys'),
          P('PTSDJ has <b>two</b> pretend Game Boys. <b>Song A</b> is tracks 1 to 4. <b>Song B</b> is tracks 5 to 8. They can play at the same time, and you can link them so they keep the same beat. Chapter 8 tells you how.'),
          H2('Counting with letters (hex numbers)'),
          P('Many numbers on the screen use letters. This is called <b>hex</b>. Hex counts like this:'),
          P('<b>0 1 2 3 4 5 6 7 8 9 A B C D E F</b> and then <b>10 11 12 ... 19 1A 1B ... 1F 20</b>', 'body'),
          P('So <b>0A</b> means ten. <b>10</b> means sixteen. <b>FF</b> means 255. You do not need to do math. Just treat the letters like numbers that come after 9.'),
          H2('How to read this manual'),
          bullets(['Key names look like this: [Insert]. They match the names on the Tracker.', 'Hold two keys together? It says <b>Shift + Insert</b>. Hold the first and press the second.', 'Page names are written like the Song page, the Phrase page and the Instrument page.',
                   'Green boxes are tips. Red boxes are warnings. Blue boxes are good things to know.'])]

    # ---------------------------------------------------------------- 2
    s += H1('2. Your Tracker and PTSDJ')
    s += [P('This chapter shows the keys on your Polyend Tracker and what they do in PTSDJ. The picture matches the real Tracker.'), KeyMap(BODY_W), Spacer(1, 6),
          callout('note', 'The small red light on the left side of the screen frame is the <b>battery light</b>. It glows while a song plays. In the Sample Editor it flickers when a sound is loud (Chapter 11).'),
          H2('The page keys'),
          P('Each page key jumps straight to one page. These are the blue keys in the picture.')]
    s += table([['Key', 'Page it opens'],
                ['[Song]', 'Song page: the big plan.'],
                ['[Pattern]', 'Chain page.'],
                ['[Note]', 'Phrase page: where the notes go.'],
                ['[Instrument]', 'Instrument page: your sounds. Hold [Shift] and press it for the instrument library.'],
                ['[Instrument Parameters]', 'Table page.'],
                ['[FX1]', 'Synth page.'],
                ['[FX2]', 'Wave page.'],
                ['[Config]', 'Project page: settings, saving, exporting and the help pages.'],
                ['[Perform]', 'Groove page: how long each step lasts.'],
                ['[File]', 'The song list for the current Game Boy.'],
                ['[Master]', 'Shows both Game Boys side by side, when they are linked (Chapter 8).']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [H2('The sample keys'), P('These green keys work with drum kits. Chapter 11 explains them.')]
    s += table([['Key', 'Page it opens'],
                ['[Sample Loader]', 'Pick a WAV file and turn it into a drum sound.'],
                ['[Sample Editor]', 'Cut a sound, name it, and add it to a kit.'],
                ['[Sample Playback]', 'The kit page: hear and change the sounds in a kit.'],
                ['[Sample Recorder]', 'Record a sound from a line cable or a microphone.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [H2('The editing keys'),
          P('These are the keys you press most. The names in this manual match the Game Boy buttons you may know.')]
    s += table([['Key', 'Like on a Game Boy', 'What it does'],
                ['[Insert]', 'A button', 'Puts something in, or does the main thing.'],
                ['[Delete]', 'B button', 'Clears or goes back. The key says Backspace above and Delete below. This manual calls it [Delete] or BKSP.'],
                ['[Shift]', 'Select', 'Hold it to do more. [Shift] + arrows move between pages.'],
                ['[Play]', 'Start', 'Starts and stops the music.'],
                ['Arrows', 'D-pad', 'Move the bright cursor box.'],
                ['[Copy]', '', 'Copies. Hold [Shift] and press [Copy] to paste. The key shows Paste above its name.'],
                ['[Rec]', '', 'Turns on recording from the pads (Chapter 7).'],
                ['Jog wheel', '', 'Turn it to change a number. Same as holding [Insert] and pressing Left or Right.']], [1.3 * inch, 1.3 * inch, BODY_W - 2.6 * inch])
    s += [H2('The pads'), P('The 48 pads are at the bottom left. Press a pad and you hear the sound that is on the screen. On the Phrase page they can also type notes for you. Hold [Shift] and press a pad row to change the octave (how high the pads play).'),
          H2('The F keys'), P('The eight keys under the screen are F1 to F8. Hold F8 to see a help box about the page you are on. F1 and F2 turn the pages of that help box. When two songs are shown side by side, the F keys also set volume and muting (Chapter 8).'),
          H2('The page map'),
          P('The pages sit next to each other, like rooms in a house. Hold [Shift] and press an arrow to walk to the next one. You can also use the page keys to jump.'), PageMap(BODY_W), Spacer(1, 4),
          fig('20-song', 'The Song page. The panel on the right shows the track name, the tempo (the number after the note symbol), the track numbers 1 to 4, and the page map. The bright letter is the page you are on.', 4.3 * inch)]

    # ---------------------------------------------------------------- 3
    s += H1('3. Setting up')
    s += [H2('The SD card'), P('PTSDJ keeps everything on the SD card. These are the folders it uses. It makes most of them by itself the first time they are needed.')]
    s += table([['Folder or file', 'What it is for'],
                ['GB/LSDJ.GB', 'Your own copy of the LSDJ ROM. <b>You must supply this.</b> PTSDJ needs it for drum kits, the synth page, and to build ROMs for your Game Boy.'],
                ['GB/Saves/A.srm, B.srm', 'Your songs. Song A goes in A.srm and song B in B.srm. Each file holds up to 32 songs.'],
                ['GB/Songs', 'Loose LSDJ song files (.lsdsng) that you can load.'],
                ['GB/Instruments', 'Saved sounds (.gbi). The presets go here too.'],
                ['GB/Samples', 'WAV files that you turn into drum sounds.'],
                ['GB/Kits/A and GB/Kits/B', 'Your drum kits. Each Game Boy has its own kits.'],
                ['GB/Export', 'What PTSDJ writes when you export: WAV files, save files and ROMs for a real Game Boy.'],
                ['Firmware', 'Update files (.ptf) for the Tracker, and the small ot_restore.txt file.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += callout('care', 'PTSDJ does not include the LSDJ ROM. Use your own copy. Songs, kits and presets are made by you or by PTSDJ. The ROM is not.')
    s += [H2('Installing PTSDJ'), steps(['Put the <b>.ptf</b> file for PTSDJ in the <b>Firmware</b> folder on the SD card.',
                                          'Put <b>ot_restore.txt</b> in the same folder. It has one line: the exact file name of the .ptf. Nothing else. No extra lines.',
                                          'Put the card in the Tracker. Turn the Tracker off and on.', 'The Tracker installs the new firmware and restarts. PTSDJ starts with the letters PTSDJ dropping down and the Game Boy chime.'])]
    s += callout('tip', 'If PTSDJ is already installed you can also update from inside it. Open the Project page, hold [Shift], and press F8. The ? changes to FIRMWARE. Pick a file and press [Insert] twice. Chapter 13 has the details.')
    s += [H2('Going back to the normal Tracker'), P('Put the stock Polyend .ptf file in the Firmware folder and use the same steps. Your PTSDJ files stay on the SD card. Normal Tracker projects are never touched by PTSDJ.'),
          H2('The first start'),
          P('The first time, there is no saved song, so both Game Boys open with a <b>demo song</b>. The demo is a drum and bass track in two parts: DEMO_A has the drums and the lead, DEMO_B has the bass and the pads. Press [Play] to hear them together. After that, each Game Boy opens the song you saved last. The first screen is the Song page.'),
          fig('20-song', 'The Song page of DEMO_A. The numbers are chains. A -- is an empty spot.', 3.8 * inch),
          P('To start a song of your own, press the [File] key and pick <b>NEW SONG</b>. Chapter 9 shows how.')]

    # ---------------------------------------------------------------- 4
    s += H1('4. Editing basics')
    s += [P('Every page works in the same way. Learn these ideas once and you know how to use every page.'),
          H2('The cursor'), P('The cursor is the bright box. Press the arrows to move it. It shows where [Insert] and [Delete] will work.'),
          H2('Putting things in'),
          steps(['Move the cursor to a box.', 'Press [Insert]. The box fills with a value. It is usually the last value you used.', 'To change it, turn the jog wheel. Or hold [Insert] and press an arrow.'])]
    s += table([['Do this', 'What happens'],
                ['[Insert]', 'Puts in a value.'],
                ['[Insert] + Left or Right', 'Changes the value by 1.'],
                ['[Insert] + Up or Down', 'Changes the value by 16 (or by an octave on notes).'],
                ['Jog wheel', 'Same as [Insert] + Left or Right.'],
                ['[Delete]', 'Clears the box.'],
                ['[Delete] + Left or Right', 'On Chain, Phrase, Instrument, Table, Groove and Wave pages: choose another chain, phrase, and so on.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [H2('Picking many boxes at once'),
          P('Hold [Shift] and [Insert] together, then press the arrows. The boxes you cover light up. Now:'),
          bullets(['[Delete] clears all of them.', 'The jog wheel changes all the filled ones at once.', '[Copy] copies them. Move somewhere else, then hold [Shift] and press [Copy] to paste.']),
          fig('35-selection', 'Three boxes picked on the Chain page.', 3.8 * inch),
          callout('note', 'Copy and paste works on the Song, Chain, Phrase, Table and Groove pages, and only inside the same page. If you paste a chain or phrase, the Project page setting CLONE decides what happens. <b>DEEP</b> makes brand new copies of the phrases. <b>SLIM</b> reuses the same ones.'),
          H2('Moving between pages'),
          P('You can jump with a page key, or walk with [Shift] and the arrows. When you walk right from the Song page, PTSDJ follows what you were on. If the cursor was on chain 03, the Chain page opens at chain 03. If you were on a phrase, the Phrase page opens at that phrase. It is a fast way to dive into your song.'),
          H2('Getting help on the Tracker'),
          P('PTSDJ has help built in. You do not need this manual to look up a key.'),
          bullets(['<b>Hold F8.</b> A box pops up about the page you are on. Press Left or Right to turn its pages. Or use F1 (back) and F2 (forward) with your other hand. A little tab with &lt; and &gt; grows out of the frame above them.',
                   '<b>The cheat sheets.</b> On the Phrase page, hold [Shift] and press [Note] to see every command. On the Table page, hold [Shift] and press [Instrument Parameters].',
                   '<b>The help pages.</b> Open the Project page and move to HELP. Press [Insert]. Left and Right turn the pages. [Delete] leaves.']),
          fig2('36-f8-popup', 'Hold F8: a pop-up about the Phrase page.', '38-cheat-sheet', 'The command cheat sheet.'),
          fig('39-help-page', 'The help pages start with a welcome.', 3.6 * inch)]
    return s
