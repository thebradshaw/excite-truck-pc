#!/usr/bin/env bash
# Boot the game on a virtual display and take screenshots at the given seconds.
cd /home/claude/work
mkdir -p shots; rm -f shots/*.png
Xvfb :77 -screen 0 1280x720x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=:77 timeout -s KILL ${RUNSECS:-70} ./wc-et/build-linux/WiiCompiled > run.log 2>&1 & GP=$!
t=0
for s in ${SHOTS:-15 30 45 60}; do sleep $((s-t)); t=$s; DISPLAY=:77 import -window root shots/t$s.png 2>/dev/null; done
wait $GP; kill $XP
