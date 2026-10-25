# Practice mode – how it works (for developers)

This describes the parts added for the Melodics-style practice mode. See
[practice-mode.md](practice-mode.md) for the user's view, and [melodics-plan.md](melodics-plan.md)
for the plan and the decisions made in its reviews.

## Threads and timing

Everything except MIDI input runs on the GUI thread, as it always did in PianoBooster:

```
RtMidi callback thread ──► SPSC ring (CMidiDeviceRt, timestamped) ──┐
                                                                    ▼
QBasicTimer, 4 ms (PreciseTimer) ─► CGLView::updateMidiTask ─► CSong::task ─► CConductor::realTimeEngine
paintGL (about the screen refresh rate) ─┘                                       │
                                                       FluidSynth (CoreAudio thread, 128-frame device buffer)
```

- **Input:** the RtMidi callback only copies the message into a lock-free single-producer
  single-consumer ring with its arrival time. The engine drains the ring on every tick, and the
  event's age is subtracted from the timing judgement.
- **Engine clock:** `updateMidiTask` hands whole milliseconds to the engine and keeps the remainder
  (`m_realtimeConsumedNs`), so no time is lost when it runs from both the timer and the paint.
- **Display:** on macOS the swap interval is 0, so a vsync wait never stalls the engine.
  Repaints are throttled to about the screen refresh rate (`m_frameIntervalMs`).
- **Audio:** on macOS the delay from key to sound is set by the output device's I/O buffer, not by
  FluidSynth's period settings. `MacAudio::setOutputBufferFrames` asks CoreAudio for a 128-frame
  (2.9 ms) buffer for this process, and asks again when the default output device changes.
  FluidSynth gets large internal buffers (512 × 8): if they are smaller than the device buffer it
  renders nothing at all (the bug of the first alpha, which used 64 × 2).
- **Measuring:**
  - `PB_LATENCY_LOG` prints the input queue age of every key press.
  - `PB_FRAME_LOG` prints the paint cost of every frame.
  - `PB_SOUND_PROBE` prints the instrument each note gets, and the audio peak level and buffer
    size once a second.

  `tools/smoke_test.sh` summarises the first two.

## Song time

Song ticks are the MIDI file's pulses × `SPEED_ADJUST_FACTOR` (1000).

The conductor advances song time in `CConductor::addDeltaTime`. That call also moves
`CScore::scrollDeltaTime`, which drives both the notation scroll and the highway's `now`. Because of
this:
- Rewind, seek to bar, loops and "wait for me" (where time stops, or even steps back a little)
  behave the same in both views.
- `CScore::reset()` is called on every rewind and loop pass, and resets the highway's position and
  its judgements.

## Falling notes – `CHighway` (Highway.h/.cpp)

- **Note list:** built once per song in `CSong::midiFileInfo` (`beginSong`, `collectEvent`,
  `endSong`) from the same event stream the player uses, so the note times match the song ticks
  exactly.
- **Hand and transpose:** decided at draw time, so changing hands, parts or transposition needs no
  rebuild. The hand comes from `CNote::findHand` with the active channel.
- **Drawing:** one batched immediate-mode GL pass. The visible window is found by binary search.
  Text goes through the view's queue (`CGLView::flushText`), so a frame needs only one QPainter.
- **Notation view:** while the highway is shown, the notation display lists are not compiled
  (`CScore::setNotationShown(false)`). The notation queues are still drained every frame, because
  they feed the song reader.

## Judgements – `CPerformance` (Performance.h/.cpp)

The conductor calls:
- `judgeGoodNote` for each correct key, once per note of the wanted chord (`m_judgedNotes`).
- `judgeRemainingNotes` when a chord is finished: unplayed notes are marked missed (Play Along) or
  skipped (when one note of a chord was enough).
- `recordWrong` for wrong keys. Keys played during the count-in are neither judged nor counted.

The offset of a key press is:

```
m_tempo.ticksToMSec(m_pianistTiming) − input queue age − timing calibration
```

Judging that offset:
- **Play Along:** Perfect within ±40 ms, Good within ±100 ms, otherwise Early or Late.
- **Follow You (wait mode):** only the pitch counts, and stars are capped at 2.

`CHighway::judgeNote` colours the matching falling note and starts the flash and popup.

## Practice tools

- **Count-in:** done inside `CConductor::realTimeEngine`, after any seek to the start bar has
  finished. Song time does not move during it.
- **Metronome:** clicks when `CBar`'s beat changes.
- **Loops:** the existing `CBar` play-from and looping bars. The progress bar sets them through
  `GuiTopBar::setLoop`. A finished loop pass emits `CGLView::runFinished(result, true)`, before the
  rewind clears the statistics. Auto speed-up reacts to it in `QtWindow::onRunFinished`.
- **Seeking:** `CBar::goToBarNumer` moves at most one beat per call and stops just short of the
  start bar, so the skipped music stays silent and unjudged.

## Progress – `CProgress` (Progress.h/.cpp)

- **Storage:** a JSON file in the application data folder, written atomically with `QSaveFile`.
  It holds best accuracy and stars per song (key `book/song`), practice seconds per day, and the
  daily goal.
- **Practice time:** `QtWindow::practiceTick` counts it only while the music plays and a key was
  pressed within the last 15 s.

## Sounds

`CMidiDeviceFluidSynth` loads:
- **The main SoundFont:** a General MIDI bank for the accompaniment. Every program change of the
  song is resolved explicitly in this bank (`setMainFontProgram` with the channel's CC0 bank, and
  bank 128 on channel 10). FluidSynth's own lookup searches the newest SoundFont first, so a piano
  bank loaded later would otherwise replace the song's pianos.
- **The pianist's instrument:** an extra SoundFont, loaded lazily and only when chosen. When
  another bank is chosen, the banks no longer in use are unloaded.

`selectChannelSound` puts the chosen instrument on the pianist's channel with
`fluid_synth_program_select` and locks the channel against the song's program and bank changes.

- **Finding banks:** `CSettings::updateSoundBanks` scans the sound bank folders, upgrades from the
  Homebrew fallback bank to a General MIDI bank, and lists every other bank as an extra instrument.
- **Choosing:** *Practice → Your Sound* offers the instruments.
- **Song's instrument mode:** uses the General MIDI program the song gives the part
  (`CTrackList::getChannelProgram`).

Latency depends only on the audio buffer, not on the SoundFont. The banks below were measured with
`fluidsynth -F` rendering *Greensleeves* (107 s of music):

| Bank | Size | Load | Render speed |
|---|---|---|---|
| GeneralUser GS | 32 MB | 0.6 s | 319× real time |
| Upright Piano KW | 57 MB | 0.2 s | 109× real time |
| YDP Grand | 118 MB | 0.3 s | 172× real time |
| jRhodes3 | 79 MB | 0.3 s | 233× real time |
| Salamander Grand | 1.27 GB | 1.2 s | 100× real time |

The Salamander grand is kept in RAM once loaded.

## Keyboard fit – `KeyboardFit` (KeyboardFit.h/.cpp)

- **Keyboard size:** `identify` takes it from the keyboard setup range, or else from the MIDI port
  name (a table of models, then a key count in the name).
- **Fit:** `check` works out whether the part's sounding notes (`CHighway::partRange`, for the
  active hand) fit.
  - **Keyboards that start on a C** (controllers with octave buttons): the reachable window starts
    on any C, so a narrow part may still need a small transposition.
  - **Fixed ranges** (88 keys from A0, or the keyboard setup): the part must lie inside the range.
- **Display:** `QtWindow::updateKeyboardFit` runs once a second and on song load. It puts the
  advice in the HUD (`CGLView::setKeyboardFit`) and marks the reachable keys
  (`CHighway::setReachableRange`).

## Tests

- `pianobooster_tests` (QtTest, `src/tests/test_core.cpp`) covers the judgement classes, accuracy,
  stars and streak counts, the wait-mode star cap, and the progress store round trip with its
  streak logic.
- End to end:
  - `tools/virtual_pianist.py` creates the "Virtual Pianist" MIDI port. It plays a song's notes,
    optionally with jitter, a constant offset, wrong or skipped notes, and starts at the app's
    `PB_MUSIC_START`.
  - `tools/smoke_test.sh` runs the app on a copy of the user's settings (`PB_TEST_PROFILE`; progress
    goes to `~/.qttest`), so tests never change the user's settings or practice history. It drives
    the app with the hooks `PB_MIDI_INPUT`, `PB_AUTOPLAY`, `PB_SCREENSHOT`,
    `PB_QUIT_AFTER`, `PB_SPEED`, `PB_LOOP_FROM` and `PB_LOOP_BARS`, and reports latency, frame times
    and the score.
