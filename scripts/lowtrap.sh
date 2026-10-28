#!/usr/bin/env bash
# Make guest page 0 (all aliases but 0x80000000) read-only after DVDInit and report the first writer.
cd /home/claude/work
Xvfb :83 -screen 0 1280x720x24 >/dev/null 2>&1 & XP=$!; sleep 1
DISPLAY=:83 SDL_AUDIO_DRIVER=dummy XDG_RUNTIME_DIR=/tmp/xdg timeout 120 gdb -q -batch -x $(dirname "$0")/lowtrap.gdb ./wc-et/build-linux/WiiCompiled > /tmp/lowtrap.out 2>&1
kill $XP
grep -A40 "SIGSEGV" /tmp/lowtrap.out | grep -E "^#|=>|^r(ax|cx|dx|12|13|14|15|bx|si|di) " | head -30
