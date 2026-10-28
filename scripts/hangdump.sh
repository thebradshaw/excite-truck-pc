#!/usr/bin/env bash
cd /home/claude/work
Xvfb :78 -screen 0 1280x720x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=:78 timeout -s KILL 120 ./wc-et/build-linux/WiiCompiled > run5.log 2>&1 & 
sleep ${WAIT:-40}
P=$(pgrep -f '^./wc-et/build-linux/WiiCompiled' | head -1)
gdb -q -batch -p $P -ex 'set pagination off' -ex 'thread apply all bt 25' > gdb.txt 2>&1
kill -9 $P; kill $XP; wait
