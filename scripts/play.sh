#!/usr/bin/env bash
# Boot on a virtual display, run a scripted sequence of key presses and screenshots.
# SCRIPT lines: "sleep N" | "key NAME [holdsec]" | "shot NAME"
cd /home/claude/work
mkdir -p shots; rm -f shots/*.png
Xvfb :82 -screen 0 1280x720x24 >/dev/null 2>&1 & XP=$!
sleep 1
WC_GAME_LOG=1 WC_TRACE_SLEEP=1 WC_TRACE_INPUT=1 SDL_AUDIO_DRIVER=dummy XDG_RUNTIME_DIR=/tmp/xdg DISPLAY=:82 timeout -s KILL ${RUNSECS:-120} ./wc-et/build-linux/WiiCompiled > run.log 2>&1 & GP=$!
sleep 3
W=$(DISPLAY=:82 xdotool search --name "." 2>/dev/null | tail -1); echo "window=$W" >&2
while read -r cmd a b; do
  case "$cmd" in
    sleep) sleep "$a" ;;
    key) DISPLAY=:82 xdotool windowfocus "$W" 2>/dev/null; DISPLAY=:82 xdotool keydown --window "$W" "$a"; sleep "${b:-0.3}"; DISPLAY=:82 xdotool keyup --window "$W" "$a" ;;
    shot) DISPLAY=:82 import -window root "shots/$a.png" ;;
  esac
done <<< "$SCRIPT"
kill -9 $GP 2>/dev/null; kill $XP; wait 2>/dev/null
