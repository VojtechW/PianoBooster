# Practising with falling notes (Melodics-style)

PianoBooster shows the music as **falling notes** above a keyboard. Play each note when it
reaches the line on top of the keys. Every note is judged (Perfect / Good / Early / Late / Miss),
and you get an accuracy score and stars at the end.

## Getting started (macOS)

1. Build (see [BUILD.md](../BUILD.md)) and run `build/build/pianobooster.app`.
2. Plug in your MIDI keyboard. When the keyboard chosen in *Setup → MIDI Setup* is not
   connected, PianoBooster uses the first MIDI keyboard it finds. It shows
   "MIDI keyboard connected" in the status bar, and reconnects it after you unplug and replug it.
3. Sound:
   - **Your keyboard has speakers:** in *Setup → Keyboard* set the right-note sound to *None*,
     so you don't hear every note twice. Wrong notes are still played through the computer, so you
     notice them; set the wrong-note sound to *None* as well if you don't want that.
   - **Your keyboard is a controller without sound:** PianoBooster plays the sound through
     FluidSynth with a 128-frame (about 3 ms) audio buffer (see *Sounds* below).
4. Choose a song in the side panel and press **Space**. One bar is counted in, then the music starts.

## Modes (side panel, "Skill")

| Mode | What it does |
|---|---|
| **Follow You** ("Wait for me") | The music waits until you play the right notes. Good for learning the notes. Only checks *which* notes you play, so at most 2 stars. |
| **Play Along** | The music keeps going. Each note is judged on timing: Perfect within ±40 ms, Good within ±100 ms, otherwise Early or Late. |
| **Listen** | Just listen to the piece. |

Use **Right / Both / Left** (F4 / F3 / F2; on a Mac keyboard hold Fn for the F keys) to practise one hand at a time. The other hand is then
played for you and shown in grey.

## Does the song fit your keyboard?

Above the falling notes PianoBooster says whether the notes you are about to play fit on your MIDI
keyboard. It knows the size of many models from their names (LPK25 and MPK mini: 25 keys, Launchkey
49, Keystation 61, digital pianos: 88 …). You can also set the exact range in *Setup → Keyboard*.

| Message | What to do |
|---|---|
| *Fits your LPK25: lowest key on C4* (green) | Use the octave buttons until your lowest key plays C4. The keys your keyboard reaches are marked with a cyan line under the on-screen keyboard. |
| *Fits your LPK25 if transposed by +5* (orange) | Set *Transpose* in the top bar to +5. Small keyboards shift by whole octaves, so a song that spans few enough keys can still need transposing to fit between two Cs. |
| *Needs 28 keys, your LPK25 has 25: practise one hand at a time* (orange) | Choose *Right* or *Left* in the side panel. The message tells you if only one of the hands fits. |
| *Needs 15 keys (C4–D5)* (grey) | The size of your keyboard is not known. Set its range in *Setup → Keyboard*. |

In the notation view the same advice appears in the status bar when a song does not fit.

## Practice tools

- **Loop a passage:** drag across the bars you want on the progress bar at the top. The loop is
  shown in pink. Click the progress bar once to start from that bar without a loop.
- **Auto speed-up** (*Practice → Auto Speed-up in Loops*): set a lower speed (Speed box or the
  `-` key). After every loop pass with at least 90% accuracy the speed goes up by 5%, until 100%.
- **Count-in** (*Practice → Count-in*) and **Metronome** (*Practice → Metronome*, Fn+F6).
- **Zoom:** scroll the mouse wheel over the falling notes to see more or fewer beats ahead.
- **Note names:** *Setup → Preferences → Show note names* also labels the falling notes.
- **Notation:** *View → Falling Notes* (Fn+F7) switches back to the scrolling music score.
- **Timing calibration** (*Practice → Timing Calibration…*): if notes you play on the beat
  are judged late (for example because of a slow audio interface), enter the delay here.

## Sounds

PianoBooster uses every SoundFont (`.sf2`/`.sf3`) in `~/Library/Audio/Sounds/Banks`:

- **Accompaniment:** a General MIDI bank, *GeneralUser GS*, plays the backing of the songs.
- **Your instrument:** choose it in *Practice → Your Sound*. Each bank is only loaded when you pick
  it. Suggested banks:

| Sound | Character | Good for |
|---|---|---|
| **Salamander Grand Piano** (default) | Yamaha C5 concert grand, 16 velocity layers | Classical pieces and anything you want to sound its best. 1.3 GB, takes about a second to load. |
| **YDP Grand Piano** | Bright, clear Yamaha grand | Everyday practice, pop. |
| **Upright Piano KW** | Warm, intimate Kawai upright | Folk songs and ballads (e.g. *Greensleeves*, *Scarborough Fair*). |
| **jRhodes3 Electric Piano** | 1977 Rhodes Mark I | Jazz, soul and modern keys. |
| **GeneralUser GS** keys | Electric pianos (Rhodes, DX7-style FM), Clavinet, Vibraphone, Drawbar/Percussive/Rock Organ, Harpsichord | Jazz organ and vibes, funk, baroque. |
| **The Song's Instrument** | Whatever the song gives your part | Hearing the part as the arranger intended. |

- **Other banks:** to add one, put it into the folder (*Practice → Your Sound → Open the Sound Banks
  Folder*) or use *Add Sound Bank…*.
- **Lag:** none of these add any. The delay depends only on the audio buffer, and even the biggest
  bank renders more than 100× faster than real time.
- **Reverb:** room reverb is on by default. Turn it off in *Setup → MIDI Setup*.
- **Credits and licences:** they are in `~/Library/Audio/Sounds/Banks/PianoBooster licenses`:
  - GeneralUser GS (S. Christian Collins): free for any use
  - Salamander and YDP: CC BY 3.0
  - Upright Piano KW: CC0
  - jRhodes3 (Jeff Learman): listed as public domain

## Progress

- The status bar shows today's practice time against your daily goal, and your day streak.
  Only time spent actually playing counts.
- The best stars for each song are shown in the song list.
- *Practice → Next Lesson* (Cmd+L) jumps to the first song in the book without 3 stars.
- *Practice → Your Progress…* (Cmd+Shift+P) shows:
  - your daily goal (adjustable)
  - your streak
  - the last 14 days
  - your best results per song

Progress is stored in `~/Library/Application Support/PianoBooster/Piano Booster/progress.json`.

## Stars

| Stars | Accuracy |
|---|---|
| ★ | 60% |
| ★★ | 80% |
| ★★★ | 92% (Play Along only) |

Accuracy weights Perfect 100%, Good 85%, Early/Late 60%, Missed 0. Each wrong note counts as
half a missed note.

## For developers

- `build/build/pianobooster_tests` runs the unit tests for the scoring and progress logic.
- `tools/smoke_test.sh <song.mid> <outdir> [seconds] -- [virtual_pianist options]` does an
  end-to-end run. It needs `pip install mido python-rtmidi` and "Virtual Pianist" selected as
  the MIDI input. The run:
  - starts a virtual MIDI keyboard (`tools/virtual_pianist.py`), which plays the song with
    optional timing jitter, offsets and wrong notes
  - launches the app, autoplays and takes screenshots
  - reports input latency, frame times and the score
- Environment hooks used by the tests:
  - `PB_AUTOPLAY`, `PB_SCREENSHOT`, `PB_QUIT_AFTER`
  - `PB_SPEED`, `PB_LOOP_FROM`, `PB_LOOP_BARS`
  - `PB_LATENCY_LOG`, `PB_FRAME_LOG`
