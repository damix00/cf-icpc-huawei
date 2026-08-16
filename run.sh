#!/bin/sh
# Build and score a strategy across tests/.   sh run.sh claude/sol.cpp
# (score.sh is the general tool; this only exists for a quick default run.)
set -e
SRC=${1:-ref.cpp}
g++ -O2 -std=gnu++17 -DSOL_SRC="\"$SRC\"" -o sim.exe sim.cpp
for f in tests/ex1.txt tests/ex2.txt tests/g_*.txt; do
  [ -f "$f" ] || continue
  ./sim.exe "$f" || true
done | tee /tmp/cf2251.scores
echo "-----"
awk '/score=/{split($0,a,"score=");split(a[2],b," ");t+=b[1];n++}
     /FAILED/{f++}
     END{printf "tests=%d failures=%d mean=%.3f\n", n, f+0, n?t/n:0}' /tmp/cf2251.scores
