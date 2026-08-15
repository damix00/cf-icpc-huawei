#!/bin/sh
# Score the suite once per environment setting given on stdin (one "VAR=v VAR=v" line per config).
# Usage:  printf 'CF_POOL=1e9\nCF_POOL=64\n' | sh sweep.sh
g++ -O2 -std=gnu++17 -o sim.exe sim.cpp || exit 1
while IFS= read -r cfg; do
  [ -n "$cfg" ] || continue
  out=$(for f in tests/g_*.txt tests/ex1.txt tests/ex2.txt; do
          env $cfg ./sim.exe "$f" || true
        done)
  echo "$out" | awk -v c="$cfg" '/score=/{split($0,a,"score=");split(a[2],b," ");t+=b[1];n++}
       /FAILED/{f++}
       END{printf "mean=%8.3f  fails=%d  %s\n", n?t/n:0, f+0, c}'
done
