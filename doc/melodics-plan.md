# PianoBooster → Melodics-style trainer: implementation plan

Goal: plug a MIDI keyboard into a Mac (M1 Pro, 120 Hz screen) and learn piano with a
Melodics-like loop: falling notes, per-note timing feedback, scored runs with stars,
practice loops with auto-tempo, count-in/metronome, daily goal and streaks, and a graded
exercise course. It must feel instant (low key-to-sound and key-to-screen latency).

## Current architecture (relevant facts)
- `CGLView` runs everything on the GUI thread: a `QBasicTimer` (4 ms) calls
  `CSong::task()` → `CConductor::realTimeEngine()`; repaint is requested every ≥12 ms.
- MIDI input is **polled** (`RtMidiIn::getMessage`) inside the engine tick, so key latency
  is tick period + any GUI stall (paint/vsync can block ~8–16 ms).
- The pianist's own notes are re-played by the conductor to FluidSynth
  (good/bad channel). FluidSynth defaults: 22050 Hz, period 128, 4 periods (~23 ms buffer),
  audio driver default "pulseaudio" even on macOS.
- Song events stream from `CMidiFile` → `CScore::midiEventInsert` (display) and
  `CConductor` queues. Display time advances via `CConductor::addDeltaTime` →
  `CScore::scrollDeltaTime`. `CScore::reset()` happens on rewind/seek.
- Judging inputs available: `m_pianistTiming` (ticks offset of key press vs. chord time,
  negative = early), chord accepted in `pianistInput`, misses in `followPlaying` /
  timeout branch. Rating is a coarse accuracy bar only.
- Loop practice exists (`CBar` looping, `EVENT_BITS_UptoBarReached`).

## Milestones

### M0 – Build & test harness
- Build with Homebrew Qt6 + FluidSynth + FTGL on macOS arm64. Verify app runs.
- `tools/virtual_pianist.py` (mido + python-rtmidi): opens a virtual MIDI output port,
  plays a MIDI file's notes (optionally with jitter/wrong notes) so the app can be
  exercised without a physical keyboard.
- Small QtTest unit-test target for pure logic added in later milestones
  (judgement classifier, progress store, MIDI writer).

### M1 – Low latency ("no lag")
1. FluidSynth macOS defaults: `coreaudio`, 44100 Hz, period 64, 2 periods (~3 ms).
   One-time settings migration if stored values equal old defaults / invalid driver.
2. MIDI input via RtMidi **callback thread** → fixed-size lock-free SPSC ring buffer;
   `checkMidiInput/readMidiInput` drain the ring (engine logic unchanged).
3. **Immediate echo**: in the callback, note on/off (and sustain CC64) for the pianist go
   straight to FluidSynth (its API is thread-safe) on the pianist's channel. The conductor
   then only scores the event and does not re-sound it. Disabled automatically for
   rhythm-tapping mode and when output is an external RtMidi port (not thread-safe) –
   those fall back to the existing path. Wrong-note "bad sound" is not used in echo mode
   (Melodics doesn't change the sound either). Preference toggle "Low-latency input".
4. Engine timer → `Qt::PreciseTimer`; repaint every timer tick that crosses ~8 ms so the
   120 Hz display gets fresh frames; keep paint cheap.

### M2 – Falling-notes "highway" view (default view, notation still available)
- New `CHighway` (Highway.h/.cpp) fed by the same stream as the score: hook into
  `CScore::midiEventInsert`, `scrollDeltaTime`, `reset`. Keeps a fixed-capacity ring of
  notes {pitch, hand, startTick, endTick, state} in absolute ticks
  (head counter accumulates deltaTime; "now" counter accumulates scroll ticks), so
  seek/loop/wait behave exactly like the existing scroll.
- Only notes of the active part (via `CNote::findHand`) are drawn; other hand dimmed when
  practising hands separately. Colors: left = blue, right = yellow (Melodics); recolored
  by judgement: perfect green, early orange, late purple, missed red.
- Keyboard at the bottom, range fitted to the song (pre-scan pitch range at load, padded
  to whole octaves, min 2 octaves). Keys light up while held (green correct/red wrong),
  upcoming-notes hint on keys. Bar/beat lines across the highway. Hit line.
- Judgement popup text near the hit line, current streak and live score/accuracy.
- Rendering: one batched immediate-mode quad pass per frame (few hundred quads),
  binary search for visible window, no per-frame heap allocation. Text limited to a
  handful of strings (QPainter text in GL is the expensive part).
- View menu: "Falling notes" / "Notation" toggle (persisted). Lookahead zoom (+/-).

### M3 – Timing judgement, score, stars, results
- `CPerformance` (Performance.h/.cpp), owned by conductor: per wanted note record
  {offsetMs, judgement}. Windows in **real ms** (convert ticks→ms via tempo & speed):
  Perfect ≤ 40 ms, Good ≤ 90 ms (counted as early/late but in-time), Early/Late up to
  the existing play zone, beyond → Missed. Wrong notes counted separately.
- Score: points per note (Perfect 100, Good 70, Early/Late 40) × streak multiplier
  (1 + min(streak,50)/50), accuracy % = weighted hits / total notes minus wrong-note
  penalty. Stars: ≥60% 1★, ≥80% 2★, ≥92% 3★, all Perfect & no wrong = Platinum.
- In Wait mode ("Follow you") timing judgements are relaxed: correct pitch = hit,
  stars capped (Melodics uses wait mode only for practice).
- Results overlay at song end / loop end: score, accuracy, stars, counts per category,
  mean timing tendency ("you rush by 25 ms"), best streak, personal best.

### M4 – Practice tools
- Count-in: one bar of clicks before playback (always, as in Melodics) and visual
  "4 3 2 1"; implemented inside the conductor as pre-roll ticks so it's sample-accurate
  to the engine.
- Metronome: optional click on every beat (accent on beat 1), volume; emitted from the
  engine when `CBar` crosses a beat (beat-event bit), drum channel 9 (side stick / woodblock).
- Practice loop + **Auto BPM**: in loop mode, after each pass evaluate pass accuracy;
  if ≥ 90% raise speed +5% (until 100%); show pass summary toast. Start speed option.
- Practice vs Perform mode in the top bar: Practice = loop + wait/auto-BPM allowed;
  Perform = full scored run, play-along at chosen tempo, records best score.

### M5 – Progress & habit tracking
- `CProgressStore` – JSON in `QStandardPaths::AppDataLocation`:
  per-song {bestScore, bestAccuracy, stars, plays, lastPlayed}, per-day practice seconds
  (counted only while playing and the pianist is active), daily goal (default 5 min),
  day streak (current/best). Written on song end/quit, never in the hot path.
- Progress dialog: today vs goal, streak, 14-day bar chart (QPainter), totals.
- Stars shown next to song names in the side panel; "Next lesson" action
  = first song in the current book below 3★.

### M6 – Graded course content
- `tools/make_course.py` generates a "Melodics-style Course" book of short MIDI lessons
  in grades (G1 five-finger patterns, G2 C/G/F scales hands separate, G3 hands together,
  G4 triads & I–IV–V–I, G5 arpeggios, G6 I–V–vi–IV with bass + melody, ...),
  each 4–8 bars with the right hand on ch1/left hand on ch2, plus a simple drum backing
  track. Committed as .mid files and shipped/unpacked alongside BoosterMusicBooks.

## Review process
Each milestone: implement → build → run virtual-pianist smoke test → cold review agent
reviews the diff (bugs, latency, thread safety) → fix → commit.

## Revisions after cold review #1
- Highway uses a **static note list built at song load** (full-file scan), "now" tracked
  from `CScore::scrollDeltaTime`, reset in `CScore::reset`; binary search for the visible
  window. Notation queues keep draining (`drawScrollingSymbols(false)`) when the highway is shown.
- Transpose / active hand / keyboard range applied at draw time.
- Input: timestamped SPSC ring from the RtMidi callback first; judge with event age subtracted
  (removes late bias). Echo-from-callback only if measurements show GUI stalls matter.
- Fixed existing bug: `CTempo::mSecToTicks` used 100 pulses/quarter vs 96 → songs played 4.2% fast.
- Repaint driven by vsync (`frameSwapped` → `update()`), engine on its own 4 ms timer.
- Metronome needs a new beat event bit in `CBar`; count-in is tick-quantised (4 ms), fine.
- Save per-pass stats before `playFromStartBar()` (rewind resets the rating).
- Follow-tempo disabled during scored runs / Auto-BPM.
- Calibrated timing offset preference (audio/visual latency) for judgements.
- M6 replaced: use existing BeginnerCourse book + "next lesson" logic; keyboard range from
  Keyboard settings; note names on falling bars. Cut: Platinum, streak multiplier, 14-day chart.

## Status after M1–M5 (implemented)
- Low latency: FluidSynth 64x2 @44.1k on macOS, timestamped input ring, swap interval 0 on macOS
  (input queue age mean ~2 ms, max ~4.5 ms), engine clock no longer loses sub-ms remainders
  (songs played ~10% slow with frequent repaints, ~4% fast from the tempo formula before).
- Falling-notes view (default, F7 toggles notation), scroll wheel zoom, judgement flashes/popups,
  live accuracy/streak HUD, results panel with timing tendency.
- Count-in, metronome (F6), auto speed-up after ≥90% loop passes, timing calibration.
- Progress store (JSON in app data), stars in the song list, Next Lesson (Cmd+L), Progress dialog
  (Cmd+P), status bar with daily goal and streak.
- Unit tests (`pianobooster_tests`) and an end-to-end smoke test (`tools/smoke_test.sh` with
  `tools/virtual_pianist.py`).

## M7 – Visual polish and practice UX inspired by Neothesia
- Progress bar above the highway: played part, bar ticks, click to start from a bar,
  drag to select an A–B loop (sets play-from bar + looping bars), loop region shown.
- Notes on black keys use a darker shade of the hand colour; rounded corners; glow at the hit
  line for notes that are sounding; optional note names on the falling notes (existing
  "note names" preference); C and F guide lines.

## M8 – Sounds (implemented)
- Multi-SoundFont support: a General MIDI bank for the accompaniment, plus extra instrument
  banks for the pianist, loaded lazily. The pianist's channel is locked to the chosen instrument
  (`fluid_synth_program_select`), and *Practice → Your Sound* lists the instruments.
- The song's-instrument mode, room reverb on by default (the reverb setting was never applied
  before), and automatic discovery of the installed banks with an upgrade from the Homebrew
  fallback bank.
- Installed: GeneralUser GS, Salamander Grand, YDP Grand, Upright Piano KW, jRhodes3.
  All render more than 100× faster than real time, so no lag risk.
