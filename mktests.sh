#!/bin/sh
# Regenerate and calibrate the local test suite (slow: runs the reference schedule per test).
set -e
g++ -O2 -std=gnu++17 -o gen.exe gen.cpp
g++ -O2 -std=gnu++17 -o sim.exe sim.cpp
rm -f tests/g_*.txt
for prof in 0 1 2 3 4 5 6 7; do
  for seed in 1 2 3; do
    f="tests/g_${prof}_${seed}.txt"
    ./gen.exe "$seed" "$prof" > "$f"
    ./sim.exe "$f" -calibrate
  done
done
