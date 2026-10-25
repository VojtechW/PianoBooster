#!/bin/bash
# End-to-end smoke test: start a virtual pianist, launch PianoBooster on a song, autoplay,
# take screenshots and collect the log.
#
# usage: tools/smoke_test.sh <song.mid> <outdir> [seconds] [-- virtual_pianist options...]
# env:   PYTHON (python with mido + python-rtmidi), APP (pianobooster binary), APP_ARGS,
#        SHOTS (comma separated ms, default 4000,8000,12000), PORT_NAME (virtual keyboard name)
# The app is told to use the "Virtual Pianist" port (PB_MIDI_INPUT); its saved settings are not changed.
set -u
song="$1"; out="$2"; secs="${3:-20}"
shift 3 2>/dev/null || shift $#
[ "${1:-}" = "--" ] && shift
here="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$here/../build/build/pianobooster.app/Contents/MacOS/pianobooster}"
PYTHON="${PYTHON:-python3}"
mkdir -p "$out"

# a copy of the user's settings, so test runs never change them (nor the practice progress)
profile="$out/profile"
mkdir -p "$profile/PianoBooster"
cp "$HOME/.config/PianoBooster/Piano Booster.ini" "$profile/PianoBooster/" 2>/dev/null
[ -n "${PROFILE_SETUP:-}" ] && eval "$PROFILE_SETUP"   # e.g. edit "$profile/PianoBooster/Piano Booster.ini"

rm -f "$out/app.log"
"$PYTHON" "$here/virtual_pianist.py" "$song" --port-name "${PORT_NAME:-Virtual Pianist}" --wait-log "$out/app.log" "$@" > "$out/pianist.log" 2>&1 &
pianist=$!
sleep 1

shots=""
IFS=',' read -ra ms <<< "${SHOTS:-4000,8000,12000}"
for t in "${ms[@]}"; do shots="$shots$out/shot_$t.png@$t;"; done

PB_TEST_PROFILE="$profile" PB_MIDI_INPUT="${PORT_NAME:-Virtual Pianist}" PB_AUTOPLAY=1500 PB_LATENCY_LOG=1 PB_FRAME_LOG=1 PB_SCREENSHOT="$shots" PB_QUIT_AFTER=$((secs * 1000)) \
    "$APP" ${APP_ARGS:-} "$song" > "$out/app.log" 2>&1
echo "app exit code $?"
kill $pianist 2>/dev/null
wait $pianist 2>/dev/null
grep PB_LATENCY "$out/app.log" | sed 's/.*=//' | sort -n | awk '
    { a[NR] = $1; s += $1 }
    END { if (NR) printf "input queue age (us): n=%d mean=%.0f p50=%d p95=%d max=%d\n", NR, s/NR, a[int(NR*0.5)+1], a[int(NR*0.95)], a[NR] }'
grep -E "PB_RESULT" "$out/app.log" | tail -3
grep PB_FRAME "$out/app.log" | sed 's/.*total_us=\([0-9]*\) hud_us=\([0-9]*\)/\1 \2/' | sort -n | awk '
    { a[NR] = $1; h += $2; s += $1 }
    END { if (NR) printf "frame paint (us): n=%d mean=%.0f (hud %.0f) p50=%d p95=%d max=%d\n", NR, s/NR, h/NR, a[int(NR*0.5)+1], a[int(NR*0.95)], a[NR] }'
