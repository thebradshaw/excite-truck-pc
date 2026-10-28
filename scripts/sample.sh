#!/usr/bin/env bash
# Boot and take N gdb stack samples of the main thread.
cd /home/claude/work
Xvfb :81 -screen 0 1920x1080x24 >/dev/null 2>&1 & XP=$!
sleep 1
XDG_RUNTIME_DIR=/tmp/xdg DISPLAY=:81 timeout -s KILL 120 ./wc-et/build-linux/WiiCompiled > run9.log 2>&1 &
sleep ${WAIT:-35}
P=$(pgrep -f '^./wc-et/build-linux/WiiCompiled' | head -1)
: > samples.txt
for i in $(seq ${N:-5}); do
  gdb -q -batch -p $P -ex 'set pagination off' -ex 'thread 1' -ex 'bt 30' 2>/dev/null | grep -E "^#" | grep -oE "in [A-Za-z_0-9:<>]+" | head -14 | tr '\n' ' ' >> samples.txt; echo >> samples.txt
  sleep 1
done
kill -9 $P; kill $XP; wait
