from lib import *

def build():
    s = []
    s += H1('5. The pages, one by one')
    s += [P('This chapter walks through every page. For each one you see what it is for, what is on the screen, and which keys to press.')]

    # ---- Song
    s += [H2('The Song page'),
          P('The Song page is the big plan. Each row has four boxes, one for each track: PU1, PU2, WAV and NOI. A box holds a <b>chain number</b>. A box with -- is empty. All four tracks play down the rows at the same time.'),
          fig('20-song', 'The Song page of a finished song. The numbers are chain numbers.', 4.2 * inch)]
    s += table([['Key', 'What it does'],
                ['[Insert]', 'Puts a chain number in the box. Then hold [Shift] and press Right to open that chain.'],
                ['[Delete]', 'Clears the box.'],
                ['[Play]', 'Starts the song at the row you are on. Press it again to stop.'],
                ['[Shift] + [Insert]', 'Mutes that track (and unmutes it).'],
                ['[Shift] + [Delete]', 'Mutes all the tracks of this Game Boy.'],
                ['[Shift] + [Song]', 'Switches between song A and song B.'],
                ['[Shift] + [Insert] + arrows', 'Picks many boxes. [Copy] and [Shift] + [Copy] copy and paste them.']], [2.2 * inch, BODY_W - 2.2 * inch])
    s += [callout('tip', 'When a track runs out of chains it starts again at the top of its column. You can make a long song with short columns that repeat.')]

    # ---- Chain
    s += [H2('The Chain page'),
          P('A chain is a list of up to 16 phrases. They play one after another. The left column is the <b>phrase</b> number. The right column is <b>TSP</b>, which means transpose: it moves all the notes of that phrase higher or lower. <b>0C</b> is one octave up.'),
          fig('21-chain', 'A chain with four phrases. The 03 phrase plays four times.', 4.2 * inch)]
    s += table([['Key', 'What it does'],
                ['[Insert]', 'Puts a phrase number in the box. Hold [Shift] and press Right to open the phrase.'],
                ['[Delete] + Left or Right', 'Picks another chain.'],
                ['[Play]', 'Plays just this chain and loops it. [Shift] + [Play] plays the whole song from here.'],
                ['Left column / right column', 'Move between the PHR box and the TSP box with Left and Right.']], [2.2 * inch, BODY_W - 2.2 * inch])

    # ---- Phrase
    s += [H2('The Phrase page'),
          P('A phrase has 16 steps, numbered 0 to F. Each step has three boxes:'),
          bullets(['<b>NOTE</b> is the pitch. It looks like C-3 or F#4. C-3 is a lower C and C-4 is a higher one.', '<b>INSTR</b> is which sound plays. It is an instrument number.', '<b>CMD</b> is a special trick. It is a letter and a two-digit number. See Chapter 6.']),
          fig('22-phrase', 'A phrase. Notes come with the instrument number and a command.', 4.2 * inch)]
    s += table([['Key', 'What it does'],
                ['[Insert]', 'Puts in a note. The first note you put in uses the last instrument you picked.'],
                ['[Insert] + Left or Right', 'Changes the note by one half step.'],
                ['[Insert] + Up or Down', 'Changes the note by one octave (12 half steps).'],
                ['[Delete]', 'Clears the step.'],
                ['[Delete] + Up or Down', 'Goes to another phrase.'],
                ['[Play]', 'Plays just this phrase on its track and loops it. [Shift] + [Play] plays the whole song.'],
                ['Pads', 'With [Rec] off, a pad types its note at the cursor. With [Rec] on, the pads only make sound (see Chapter 7).']], [2.2 * inch, BODY_W - 2.2 * inch])
    s += [callout('tip', 'There is no OFF note in LSDJ, and none in PTSDJ. To end a note, put the <b>K</b> command on the step. K08 ends the note after 8 ticks (a step is 6 ticks). The notes run from C-2 up to a top note. A big step with Up or Down that would go past the end does nothing.')]
    s += [callout('note', 'For a drum kit sound, the NOTE column does not show a pitch. It shows the <b>name of a drum sample</b>. Chapter 11 explains drum kits.')]

    # ---- Instrument
    s += [H2('The Instrument page'),
          P('An instrument is a sound. PTSDJ has 64 of them, numbered 00 to 3F. The number is at the top of the page. To pick a different one, move the cursor up onto the number and use Left and Right or the jog wheel. The pads play the sound as you go.'),
          P('The first row is <b>NAME</b>. Press [Insert] to type a name of up to five letters. The second row is <b>TYPE</b>. It can be PULSE, WAVE, KIT or NOISE. The rows below change with the type.'),
          callout('note', 'A new instrument starts as a plain pulse sound you can hear right away. When you change the TYPE, the sound goes back to the plain settings of that type.'),
          fig('23-instr-pulse', 'A pulse instrument.', 4.2 * inch)]
    s += [H3('Rows with more than one value'),
          P('Some rows hold two or three values side by side, like <b>ENV.</b> (three) and <b>TABLE</b> (two). The bright box shows which one you are on. Press Left or Right to move to the next value. Then use [Insert] with Left or Right, or the jog wheel, to change it.')]
    s += [H3('Pulse instruments (tracks PU1 and PU2)')]
    s += table([['Row', 'What it does'],
                ['ENV.', 'How loud the note is and how it fades. It has three two-digit values. In the first, the first digit is how loud the note starts (0 is silent, F is loudest) and the second is how fast it fades (0 means no fade; small numbers fade fast). The second and third values add two more stages, so a note can start loud, drop to a middle level, then fade out.'],
                ['WAVE', 'The shape of the beep: 12.5, 25, 50 or 75. Thin sounds (12.5) are buzzy. 50 is round and full.'],
                ['OUTPUT', 'Which speaker: OFF, L (left), R (right) or LR (both).'],
                ['LENGTH', 'UNLIM lets the note ring until the next one. A number makes the note stop by itself. Small numbers are short.'],
                ['SWEEP', 'Makes the pitch slide at the start of each note. 7F means no sweep. Only works on PU1.'],
                ['PITCH', 'The style of pitch bend, slide and vibrato. The usual value is fine to start.'],
                ['TRANSP.', 'ON: the note follows the transposes of the chain and the project. OFF: it ignores them.'],
                ['PU2 TSP', 'Moves the pitch of PU2 up or down in half steps. 0C is one octave.'],
                ['FINETUNE', 'A tiny pitch nudge. Two pulse tracks with a tiny nudge sound thick and wide.'],
                ['CMD/RATE', 'A setting LSDJ keeps for each instrument. PTSDJ stores it so your songs stay the same on a real Game Boy, but it does not use it yet. Leave it as it is.'],
                ['TABLE', 'Two values. The first is OFF, ON or STEP. ON runs the table as time passes. STEP moves the table one row for each new note. The second is the table number (Table page). While the table is OFF you cannot change the number.']], [1.3 * inch, BODY_W - 1.3 * inch])
    s += [fig2('30-instr-wave', 'A wave instrument.', '32-instr-noise', 'A noise instrument.')]
    s += [H3('Wave instruments (track WAV)'),
          P('A wave instrument plays a little repeating shape that you design on the Synth and Wave pages.')]
    s += table([['Row', 'What it does'],
                ['VOLUME / OUTPUT', 'Two values. VOLUME is 3 (full), 2 (half), 1 (a quarter) or 0 (silent). Next to it is the speaker choice.'],
                ['PITCH, TRANSP., CMD/RATE, TABLE', 'Work like on the pulse instrument.'],
                ['FINETUNE', 'A tiny pitch nudge.'],
                ['PLAY', 'MANUAL plays one wave all the time. ONCE, LOOP and PINGPONG step through the 16 waves of a synth by themselves: once, round and round, or back and forth.'],
                ['WAVE (or SYNTH)', 'Which wave to play. With PLAY on MANUAL it is a two-digit wave number: the first digit is the synth (0 to F) and the second is which of its 16 waves. With the other PLAY settings the row is called SYNTH and you pick only the synth, 0 to F. When you turn PLAY up from MANUAL, the wave number goes back to the first wave of its synth.'],
                ['SPEED', 'How many ticks each wave stays, when PLAY steps through them. Small numbers are fast.'],
                ['LENGTH', 'How many of the 16 waves are used when PLAY steps through them. F uses all 16.'],
                ['LOOP POS', 'Where a LOOP starts again. A dash means from the first wave. It cannot go before the first wave that is used.']], [1.8 * inch, BODY_W - 1.8 * inch])
    s += [callout('tip', 'A wave note has no fade. It rings until the next note. To end it, put the <b>K</b> command on the step (Chapter 6). The same goes for the end of a song: if a song loops back to a quiet part, end the last wave note with K or it will ring all through the quiet part.')]
    s += [H3('Noise instruments (track NOI)')]
    s += table([['Row', 'What it does'],
                ['ENV.', 'Loudness and fade, like the pulse instrument.'],
                ['OUTPUT, LENGTH, TRANSP., CMD/RATE, TABLE', 'Work like on the pulse instrument.'],
                ['PITCH', 'FREE or SAFE. These are the same two choices LSDJ has for how the noise pitch is picked.'],
                ['VIBRATO', 'The style of vibrato for the noise. The usual value is fine to start.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [H3('Kit instruments (track WAV)'),
          P('A kit instrument plays short recorded sounds, such as drums. It plays on the WAV track. Chapter 11 has the full story.'),
          fig('31-instr-kit', 'A kit instrument. KIT 1 and KIT 2 name the drum kits. The kit name shows next to each number.', 4.2 * inch)]
    s += table([['Row', 'What it does'],
                ['KIT 1', 'The first drum kit (01 to 33 in hex, counting from 01 like a real Game Boy). The step\'s note picks a sample from this kit.'],
                ['KIT 2', 'A second kit. It plays at the same time as KIT 1 when a step picks a KIT 2 sample.'],
                ['VOLUME / OUTPUT', 'Two values. VOLUME is 3 (full), 2 (half), 1 (a quarter) or 0 (silent). Next to it is the speaker choice.'],
                ['PITCH, TABLE', 'Work like on the other instruments.'],
                ['FINETUNE', 'A tiny pitch nudge for every sample.'],
                ['OFFSET', 'Where in the sample the sound starts. Bigger numbers skip more of the beginning. The second value is the one PTSDJ uses.'],
                ['LEN', 'How much of the sample plays. 00 (shown ALL) plays all of it. A number cuts it short. The second value is the one PTSDJ uses.'],
                ['LOOP', 'Two ON / OFF values. The second one repeats the sample. PTSDJ uses only that one.'],
                ['SPEED', '1X is the normal speed. 0.5X plays the sample at half speed, so it sounds an octave lower and lasts twice as long.'],
                ['CLIP', 'What happens when two layered samples add up to too much: HARD, SOFT, FOLD or WRAP. PTSDJ always clips hard, like HARD. The setting is kept for a real Game Boy.']], [1.8 * inch, BODY_W - 1.8 * inch])
    s += [H3('Naming an instrument'),
          P('On the NAME row, press [Insert]. A little keyboard opens. Move with the arrows. Press [Insert] to type the letter. The &lt; key erases. Press [Play] for OK. Press [Delete] to cancel.'),
          fig('33-name-keyboard', 'The name keyboard.', 3.8 * inch)]

    # ---- Table
    s += [H2('The Table page'),
          P('A table is a tiny pattern that plays by itself while a sound plays. It has 16 rows. Each row can change the volume, change the pitch, or run a command. There are 32 tables (00 to 1F).'),
          fig('24-table', 'A table with commands in it.', 4.2 * inch)]
    s += table([['Column', 'What it does'],
                ['VOL', 'Loudness for that row.'],
                ['TSP', 'Pitch change in half steps. 0C moves the pitch up by one octave.'],
                ['CMD and value (two of them)', 'Two commands per row. Hold [Shift] and press [Instrument Parameters] to see all of them.']], [2.0 * inch, BODY_W - 2.0 * inch])
    s += [P('<b>To use a table:</b> go to the Instrument page, move to TABLE, and pick the table number. The table now plays every time that instrument plays. The table moves one row at a time and loops back to the top after row F. A hop command (H) inside a table can jump to another row instead.'),
          P('Press [Play] on the Table page to hear the sound that uses it. Press [Play] again to stop. [Shift] + [Play] plays the whole song.')]

    # ---- Groove
    s += [H2('The Groove page'),
          P('A step lasts a number of <b>ticks</b>. A tick is one small tap of the clock. The groove says how many ticks each step lasts. The default is <b>06</b>, so every step lasts the same time.'),
          fig('27-groove', 'The Groove page. Step 0 and step 1 both last 6 ticks.', 4.0 * inch)]
    s += table([['Groove', 'Sounds like'],
                ['06 06', 'An even beat.'],
                ['07 05', 'Swing: long step, short step.'],
                ['04 08', 'A stronger bounce.']], [1.5 * inch, BODY_W - 1.5 * inch])
    s += [P('The list repeats. A <b>00</b> ends the list at that step. There are 32 grooves (00 to 1F). In a phrase, the command <b>G</b> picks a groove. [Delete] clears a step to 00. [Delete] + Left or Right picks another groove.')]

    # ---- Synth + Wave
    s += [H2('The Synth page and the Wave page'),
          P('The WAV track makes its sound from a 32-dot shape called a <b>wave</b>. The Wave page lets you draw one. The Synth page makes waves for you with a few knobs.'),
          callout('care', 'The Synth page needs <b>GB/LSDJ.GB</b> on the SD card. It uses the ROM to build the waves.'),
          fig2('25-synth', 'The Synth page. Each row shapes the sound: filter, distortion, phase and volume.', '26-wave', 'The Wave page. Each dot is one of 32 points in the shape.')]
    s += table([['On the Synth page', 'What it does'],
                ['Left or Right', 'Moves to another digit.'],
                ['[Insert] + arrows', 'Changes a value.'],
                ['[Play]', 'Plays the synth so you can hear it.'],
                ['[Delete] + Left or Right', 'Picks another synth (0 to F).']], [2.2 * inch, BODY_W - 2.2 * inch])
    s += table([['On the Wave page', 'What it does'],
                ['Left or Right', 'Moves along the dots.'],
                ['Up or Down', 'Moves one dot higher or lower.'],
                ['[Play]', 'Plays the wave.'],
                ['[Delete] + Left or Right', 'Picks another wave.'],
                ['[Delete] (a quick tap)', 'Copies the wave.'],
                ['[Shift] + [Insert]', 'Pastes the copied wave.']], [2.2 * inch, BODY_W - 2.2 * inch])

    # ---- Project
    s += [H2('The Project page'),
          P('This page holds the settings for the song on the current Game Boy. Press [Config] to open it. It also has the buttons to save, load and export.'),
          fig('28-project', 'The Project page.', 4.2 * inch)]
    s += table([['Row', 'What it does'],
                ['NAME', 'The name of the song. Press [Insert] to type it.'],
                ['TEMPO', 'How fast the song plays. Bigger is faster. It is a hex number from 28 to FF. 80 is a medium speed.'],
                ['TRANSPOSE', 'Moves every note up or down in half steps.'],
                ['SYNC', 'OFF, MASTER or SLAVE. Used when two Game Boys play together (Chapter 8).'],
                ['CLONE', 'DEEP or SLIM. Decides what happens when you paste chains and phrases (Chapter 4).'],
                ['LOOK', 'The colours of the screen. Turn the jog wheel to try them.'],
                ['PRELISTEN', 'ON: the pads make sound when you press them on any page. OFF: the pads stay quiet, except while recording.'],
                ['HELP', 'Opens the help pages.'],
                ['CLEAN SONG DATA', 'Frees chains and phrases that no song uses. Press [Insert] twice to say yes.'],
                ['CLEAN INSTR DATA', 'Does the same for sounds nobody uses. Press [Insert] twice.'],
                ['SAVE SONG', 'Saves the song to this Game Boy\'s save file.'],
                ['LOAD SONG', 'Shows the songs in the save file.'],
                ['NEW SONG', 'Asks for a name and starts an empty song.'],
                ['EXPORT WAV', 'Makes an audio file of your song.'],
                ['EXPORT LSDJ', 'Pick A, B or A+B with the jog wheel, then press [Insert]. Makes files for a real Game Boy (Chapter 12).']], [1.6 * inch, BODY_W - 1.6 * inch])
    s += [callout('tip', 'Hold [Shift] on the Project page and two things change. The ? above F8 turns into FIRMWARE, and a line of tiny technical numbers appears at the bottom. The line is for fixing problems.')]

    # ================================================================= 6
    s += H1('6. Commands')
    s += [P('A command is a special trick for one step. You put it in the CMD box of a phrase or a table. A command is a <b>letter</b> and two digits, like <b>K03</b>.'),
          P('Two letters in the digits show up a lot: <b>x</b> and <b>y</b>. They are two separate digits (0 to F). <b>xx</b> means both digits together as one number (00 to FF).'),
          callout('tip', 'Forget one? Hold [Shift] and press [Note] on the Phrase page. A cheat sheet shows every command.'),
          H2('Every command')]
    cmds = [['A xx', 'Start table xx. A20 stops the table.', 'A03 starts table 03.'],
            ['B xy', 'Maybe play the note. A big number means the note plays often. A small number means seldom.', 'BFF always plays. B40 plays about one time in four.'],
            ['C xy', 'Chord: plays the note, then +x and +y half steps, one after another, very fast.', 'C47 makes a major chord sound.'],
            ['D xx', 'Delays the note by xx ticks.', 'D03 starts the note 3 ticks late.'],
            ['E xy', 'Sets volume x, then the fade speed y.', 'EA2 starts loud and fades.'],
            ['F xy', 'Fine tune for pulse sounds.', ''],
            ['G xx', 'Picks groove number xx.', 'G01 uses groove 01.'],
            ['H xx', 'Hop: jumps to step x of the next phrase. HFF stops everything.', 'HFF stops the song.'],
            ['K xx', 'Kill: stops the note after xx ticks.', 'K02 gives a very short blip.'],
            ['L xx', 'Slide to the new note in xx ticks.', 'L08 glides over 8 ticks.'],
            ['M xy', 'Master volume. x is the left side, y is the right side (0 to 7).', 'M77 is full volume.'],
            ['O xx', 'Output. 0 is off, 1 is left, 2 is right, 3 is both.', 'O01 plays only on the left.'],
            ['P xx', 'Pitch bend. Goes up for small numbers. FF bends down.', ''],
            ['R xy', 'Repeat (retrigger) the note every y ticks.', 'R02 makes a fast stutter.'],
            ['S xy', 'Sweep on pulse 1, or shape on noise.', ''],
            ['T xx', 'Sets the tempo.', 'T80 sets a medium tempo.'],
            ['V xy', 'Vibrato: x is the speed, y is the depth.', 'V44 is a gentle wobble.'],
            ['W xx', 'Pulse width (the shape of the beep).', ''],
            ['Z xy', 'Randomizes the last command.', '']]
    s += table([['Command', 'What it does', 'Example']] + cmds, [0.9 * inch, 3.9 * inch, BODY_W - 4.8 * inch])
    s += [callout('note', 'Some commands (like B and Z) use random numbers. They do not play exactly the same each time. That is part of the fun.')]
    return s
