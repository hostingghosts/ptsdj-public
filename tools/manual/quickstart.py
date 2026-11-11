import sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from lib import *

def footer(canv, doc):
    canv.saveState(); canv.setFont('Helvetica', 8.5); canv.setFillColor(colors.HexColor('#666'))
    canv.drawString(MARGIN, 0.5 * inch, 'PTSDJ Quick Start  -  version 1.0.0')
    canv.drawRightString(W - MARGIN, 0.5 * inch, 'page %d' % doc.page)
    canv.restoreState()

def cover(canv, doc):
    canv.saveState(); canv.setFillColor(colors.HexColor(GREEN)); canv.rect(0, H - 3.2 * inch, W, 3.2 * inch, fill=1, stroke=0)
    canv.setFillColor(colors.HexColor(PINK)); canv.rect(0, H - 3.2 * inch, W, 5, fill=1, stroke=0); canv.setFillColor(colors.HexColor(NAVY)); canv.rect(0, H - 3.2 * inch - 5, W, 5, fill=1, stroke=0)
    canv.restoreState()

story = []
story += [Spacer(1, 0.55 * inch), Paragraph('PTSDJ', S['cover1']), Spacer(1, 6), Paragraph('Quick Start', S['cover2']), Spacer(1, 2.1 * inch)]
story += [fig('82-first-song-page', 'A new song, ready for music.', 3.6 * inch)]
story += [Spacer(1, 10), Paragraph('A Game Boy-style music maker for the Polyend Tracker', S['cover2']), Spacer(1, 6),
          Paragraph('Version 1.0.0 &nbsp;-&nbsp; for the original Polyend Tracker. Read this first. The full Manual has all the details.', S['cover3'])]
story += [PageBreak()]

story += [Paragraph('What is PTSDJ?', S['h1']), Rule(), Spacer(1, 6)]
story += [P('PTSDJ turns your Polyend Tracker into a Game Boy music maker. It looks and works like a popular Game Boy music program called LSDJ.'),
          P('You make short pieces of music. You link the pieces into a song. You press [Play] and listen. When you are done, you can put your songs on a <b>real Game Boy</b> that runs LSDJ.'),
          H2('How it is built'), P('A song is made of small parts. Each part sits inside a bigger part, like boxes inside boxes:')]
story += table([['Part', 'What it is'],
                ['Phrase', 'The small box. It has 16 steps. Each step can hold a note.'],
                ['Chain', 'A list of phrases. They play one after another.'],
                ['Song', 'The big plan. It lists chains for 4 tracks that play at the same time.'],
                ['Instrument', 'A sound you can play: a beep, a wave, a drum kit, or noise.']], [1.3 * inch, BODY_W - 1.3 * inch])
story += [P('Each Game Boy has <b>4 tracks</b>: two beepy pulse tracks (PU1, PU2), one wave track (WAV), and one noise track (NOI). PTSDJ has <b>two</b> pretend Game Boys, so you get 8 tracks. Song A is tracks 1 to 4. Song B is tracks 5 to 8.')]
story += [H2('What you need')]
story += [bullets(['A Polyend Tracker (the original one) with PTSDJ loaded on it.', 'An SD card in the Tracker.', 'Your own copy of the LSDJ ROM file, named <b>LSDJ.GB</b>. You need it for drum kits and the synth page. PTSDJ does not come with it.', 'A real Game Boy, a flash cartridge, or an emulator, if you want to play your songs outside the Tracker.'])]
story += callout('note', 'PTSDJ is for your own use. It is not for sale and it does not include any LSDJ code or files.')

story += [PageBreak(), Paragraph('Step 1: Get ready', S['h1']), Rule(), Spacer(1, 6)]
story += [H2('The SD card'), P('Make these folders on the SD card. Use capital letters like this:')]
story += table([['Folder', 'What goes in it'],
                ['GB', 'Put <b>LSDJ.GB</b> here. PTSDJ makes the other folders by itself.'],
                ['GB/Samples', 'Your WAV sound files, for making drum kits.'],
                ['GB/Instruments', 'Saved sounds (the presets and your own).'],
                ['GB/Songs', 'Loose LSDJ song files you want to load.'],
                ['Firmware', 'Update files for the Tracker (.ptf).']], [1.6 * inch, BODY_W - 1.6 * inch])
story += [H2('Put PTSDJ on the Tracker'),
          steps(['Copy the file <b>PolyendTracGBTrkv100.ptf</b> into the <b>Firmware</b> folder on the SD card.',
                 'Copy the small text file <b>ot_restore.txt</b> into the same folder. It holds one line: the name of the .ptf file, with nothing else.',
                 'Put the card in the Tracker and turn it off and on. The Tracker installs PTSDJ and restarts.',
                 'You will hear the Game Boy chime. You are in!'])]
story += callout('tip', 'To go back to the normal Polyend firmware, do the same steps with the stock Polyend .ptf file. Your PTSDJ songs stay on the card.')

story += [PageBreak(), Paragraph('Step 2: Meet the keys', S['h1']), Rule(), Spacer(1, 6)]
story += [P('PTSDJ uses the Tracker\'s own keys. Here are the ones you will use most.'), KeyMap(BODY_W), Spacer(1, 18)]
story += table([['Key', 'What it does'],
                ['[Insert]', 'Puts something in. It works like the A button on a Game Boy.'],
                ['[Delete]', 'Takes something out. It works like the B button. The key says Backspace on top and Delete below.'],
                ['[Shift]', 'Hold it with other keys to do more things. It works like Select.'],
                ['[Copy]', 'Copies. Hold [Shift] and press it to paste. The key says Paste above the name.'],
                ['[Play]', 'Starts and stops the music. It works like Start.'],
                ['Arrows', 'Move the cursor. The cursor is the bright box on the screen.'],
                ['Jog wheel', 'Turn it to change a number up or down. It does the same as holding [Insert] and pressing Left or Right.']], [1.3 * inch, BODY_W - 1.3 * inch])

story += [PageBreak(), Paragraph('Step 3: Your first song', S['h1']), Rule(), Spacer(1, 6)]
story += callout('tip', 'The first time you start PTSDJ, both Game Boys open with a demo song. Press [Play] to hear it: DEMO_A is a melody and DEMO_B is a beat, and they play together. Then make your own song with the steps below.')
story += [steps(['Press the [File] key. The first line is <b>NEW SONG</b>. Press [Insert].',
                 'Type a name. Use the arrows to move over a letter. Press [Insert] to type it. When you finish, press [Play] for OK.'])]
story += [fig2('80-newsong-name-empty', 'The name box. The arrows move. Insert types a letter.', '81-newsong-name-typed', 'Typing the name DEMO. Press Play when you are done.')]
story += [steps(['Press the [Song] key. You see the Song page. The bright box is the first spot for track PU1.',
                 'Press [Insert]. A chain number <b>00</b> appears. Now hold [Shift] and press Right to open the chain.',
                 'On the Chain page press [Insert] again. A phrase number <b>00</b> appears. Hold [Shift] and press Right to open the phrase.'], start=3)]
story += [fig2('83-first-chain-in-song', 'Song page: chain 00 is in the first box.', '84-first-chain', 'Chain page: phrase 00 is in the first row.')]

story += [PageBreak(), Paragraph('Step 4: Put in some notes', S['h1']), Rule(), Spacer(1, 6)]
story += [P('You are on the Phrase page now. It has 16 steps, numbered 0 to F, going down the screen.')]
story += [fig('85-first-phrase-empty', 'An empty phrase. The bright box is on step 0, in the NOTE column.', 3.7 * inch)]
story += [steps(['Press [Insert]. A note appears.', 'To change the note, hold [Insert] and press Left or Right (one step) or Up or Down (a whole octave).',
                 'Press Down to move to another step. Press [Insert] to put in another note.',
                 'Press [Play]. Your phrase plays and loops. Press [Play] again to stop.'])]
story += [fig2('86-first-phrase-notes', 'A phrase with four notes.', '87-first-phrase-playing', 'While it plays, an arrow shows the step.')]
story += callout('tip', 'Not sure what a key does? Hold the [F8] key. A help box about the page you are on pops up. Use Left and Right to turn its pages.')

story += [PageBreak(), Paragraph('Step 5: Save your song', S['h1']), Rule(), Spacer(1, 6)]
story += [steps(['Press the [Config] key. This is the Project page. It has settings for your song.', 'Move down to <b>SAVE SONG A</b>.', 'Press [Insert]. The bottom line says <b>SAVED</b>.'])]
story += [fig2('88-project-save-row', 'The Project page. The bright line is SAVE SONG A.', '89-saved', 'After you press Insert, the screen says SAVED.')]
story += [P('Your song is now in a file on the SD card. Next time, press [File] and pick it from the list to load it again.'),
          callout('care', 'If you start a NEW SONG, the song on the screen is replaced. Save first!')]
story += [H2('Make it sound different')]
story += [P('Press the [Instrument] key to see the sound you are using. Change the TYPE to wave, kit or noise to hear other kinds of sound. The Manual explains every row.'),
          fig('31-instr-kit', 'The Instrument page. TYPE can be pulse, wave, kit or noise.', 3.7 * inch)]

story += [PageBreak(), Paragraph('Step 6: Play it on a real Game Boy', S['h1']), Rule(), Spacer(1, 6)]
story += [P('This part is optional. You need a Game Boy that runs LSDJ, a flash cartridge such as an EverDrive, or an emulator.')]
story += [steps(['Go to the Project page. Move to <b>EXPORT LSDJ</b>.', 'Turn the jog wheel to pick <b>A</b>, <b>B</b> or <b>A+B</b>.', 'Press [Insert]. A progress bar runs.',
                 'Take the SD card to your computer. Open the folder <b>GB/Export</b>.',
                 'Copy <b>PTSDJ_A.gb</b> and <b>PTSDJ_A.srm</b> to your cartridge or emulator. Keep the two names the same. The .srm file holds your songs.',
                 'Start the Game Boy. Load your song and press Start.'])]
story += callout('tip', 'Song B works the same way with <b>PTSDJ_B.gb</b> and <b>PTSDJ_B.srm</b>.')
story += callout('note', 'The .gb file is your LSDJ ROM with your own drum kits inside. Always use it with the .srm of the same name. If you use a plain LSDJ ROM, your custom kits will be missing.')

story += [PageBreak(), Paragraph('Cheat sheet', S['h1']), Rule(), Spacer(1, 6)]
story += [PageMap(BODY_W), Spacer(1, 8)]
story += table([['To do this', 'Do this'],
                ['Go to a page', 'Press its key: [Song], [Pattern] (Chain), [Note] (Phrase), [Instrument], [Instrument Parameters] (Table), [Config] (Project), [Perform] (Groove), [FX1] (Synth), [FX2] (Wave), [File] (song list).'],
                ['Open the next page', 'Hold [Shift] and press Right. Shift + Left goes back.'],
                ['Put something in', '[Insert]'],
                ['Change a number', 'Turn the jog wheel. Or hold [Insert] and press Left or Right (by 1), Up or Down (by 16).'],
                ['Clear a spot', '[Delete]'],
                ['Pick many spots', 'Hold [Shift] + [Insert] and press the arrows.'],
                ['Copy and paste', '[Copy] copies. [Shift] + [Copy] pastes. Same page only.'],
                ['Play or stop', '[Play]. On the Chain, Phrase and Table pages it plays just that one. [Shift] + [Play] plays the whole song.'],
                ['Mute a track', 'On the Song page, hold [Shift] and press [Insert].'],
                ['Hear the pads', 'Press a pad. It plays the sound on the screen.'],
                ['Get help', 'Hold [F8] for a pop-up. For the help pages, open [Config] and pick HELP.'],
                ['End a note', 'There is no OFF note. Put K on the step: K08 ends it after 8 ticks.'],
                ['Name a sound', 'On the Instrument page, move to NAME and press [Insert].'],
                ['Sounds from the card', 'Hold [Shift] and press [Instrument] to open the library.'],
                ['Make a drum kit', 'Press [Sample Loader], pick a WAV file, press [Insert].']], [1.7 * inch, BODY_W - 1.7 * inch])
story += [P('<b>Where next?</b> The PTSDJ Manual explains every page, every command, drum kits, recording, two songs at once, and fixing problems.', 'small')]

doc = BaseDocTemplate(__import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', '..', 'docs', 'PTSDJ_Quick_Start.pdf'), pagesize=letter, leftMargin=MARGIN, rightMargin=MARGIN, topMargin=0.8 * inch, bottomMargin=0.85 * inch,
                      title='PTSDJ Quick Start', author='PTSDJ')
fr = Frame(MARGIN, 0.85 * inch, BODY_W, H - 1.65 * inch, id='f')
doc.addPageTemplates([PageTemplate(id='cover', frames=[fr], onPage=cover, autoNextPageTemplate='body'), PageTemplate(id='body', frames=[fr], onPage=footer)])
def flat(x):
    for i in x:
        if isinstance(i, list): yield from flat(i)
        else: yield i
doc.build(list(flat(story)))
print('built')
