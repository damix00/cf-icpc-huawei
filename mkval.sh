#!/bin/sh
# Build a second, independent suite (different seeds) used only to check that a change
# generalises rather than fitting the 28 tests in tests/.   sh mkholdout.sh
set -e
g++ -O2 -std=gnu++17 -o gen.exe gen.cpp
g++ -O2 -std=gnu++17 -o sim.exe sim.cpp
mkdir -p val
for prof in 0 1 2 3 4 5 6 7 8; do
  for seed in 21 22 23 24; do
    f="val/v_${prof}_${seed}.txt"
    [ -f "$f" ] && continue
    ./gen.exe "$seed" "$prof" > "$f"
    ./sim.exe "$f" -calibrate || rm -f "$f"
  done
done
echo "validation tests: $(ls val/*.txt 2>/dev/null | wc -l)"
