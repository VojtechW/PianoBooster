#!/usr/bin/env python3
"""Virtual pianist: plays the notes of a MIDI file into a virtual MIDI port.

Used to exercise PianoBooster without a physical keyboard. Select the port
"Virtual Pianist" as the MIDI input device in PianoBooster.

Requires: pip install mido python-rtmidi

Examples:
  virtual_pianist.py song.mid --channels 0 1 --jitter 30 --wrong 0.05
  virtual_pianist.py --port-only            # just keep the port open
"""
import argparse
import random
import time

import mido


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("midi", nargs="?", help="MIDI file whose notes to play")
    ap.add_argument("--channels", type=int, nargs="*", default=None,
                    help="0-based channels to play (default: all except drums)")
    ap.add_argument("--speed", type=float, default=1.0, help="tempo factor (1.0 = as written)")
    ap.add_argument("--jitter", type=float, default=0.0, help="max random timing error in ms")
    ap.add_argument("--offset", type=float, default=0.0, help="constant timing offset in ms (+ = late)")
    ap.add_argument("--wrong", type=float, default=0.0, help="probability of playing a wrong note")
    ap.add_argument("--skip", type=float, default=0.0, help="probability of skipping a note")
    ap.add_argument("--delay", type=float, default=3.0, help="seconds to wait before playing")
    ap.add_argument("--port-only", action="store_true", help="only open the port and wait")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--wait-log", help="start when PB_MUSIC_START appears in this log file (instead of --delay)")
    args = ap.parse_args()
    rng = random.Random(args.seed)

    out = mido.open_output("Virtual Pianist", virtual=True)
    print("Opened virtual port 'Virtual Pianist'", flush=True)
    if args.port_only or not args.midi:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            return

    mid = mido.MidiFile(args.midi)
    # Build an absolute-time list of note events.
    events = []
    t = 0.0
    for msg in mid:  # iterating a MidiFile yields messages with delta seconds
        t += msg.time / args.speed
        if msg.type not in ("note_on", "note_off"):
            continue
        if msg.channel == 9:
            continue
        if args.channels is not None and msg.channel not in args.channels:
            continue
        events.append((t, msg))

    # Perturb note-ons, keeping the matching note-off paired with the same perturbed pitch.
    played = []
    held = {}
    for t, msg in events:
        on = msg.type == "note_on" and msg.velocity > 0
        if on:
            if rng.random() < args.skip:
                held[(msg.channel, msg.note)] = None
                continue
            note = msg.note
            if rng.random() < args.wrong:
                note += rng.choice([-2, -1, 1, 2])
            dt = (args.offset + rng.uniform(-args.jitter, args.jitter)) / 1000.0
            held[(msg.channel, msg.note)] = note
            played.append((t + dt, mido.Message("note_on", channel=0, note=note, velocity=msg.velocity)))
        else:
            note = held.pop((msg.channel, msg.note), msg.note)
            if note is None:
                continue
            played.append((t, mido.Message("note_off", channel=0, note=note, velocity=0)))
    played.sort(key=lambda e: e[0])

    if args.wait_log:
        print("Waiting for PB_MUSIC_START", flush=True)
        while True:
            try:
                with open(args.wait_log, errors="ignore") as f:
                    if "PB_MUSIC_START" in f.read():
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.001)
    else:
        print(f"Playing {sum(1 for _, m in played if m.type == 'note_on')} notes in {args.delay}s", flush=True)
        time.sleep(args.delay)
    start = time.perf_counter()
    try:
        for t, msg in played:
            wait = start + t - time.perf_counter()
            if wait > 0:
                time.sleep(wait)
            out.send(msg)
    finally:
        out.reset()
    print("Done", flush=True)


if __name__ == "__main__":
    main()
