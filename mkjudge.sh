#!/bin/sh
# Build the judge-matched suite (gen.cpp profile 9).
#
# The point of this suite is that the other three do not measure the regime the judge runs:
# profiles 0-8 draw R = 50..2000 and produce up to 910k frames, whereas every judge test finishes
# in <=421 ms of our CPU and its remote prefill queue never holds more than one request.  A change
# scored only on tests/+hold/+val/ is being judged almost entirely by tests that do not resemble
# anything it will be graded on.  See NOTES.md sections 7 and 8.
#
#   sh mkjudge.sh              # generate any missing tests
#   sh score.sh sol_x.cpp "judge/*.txt"
set -e
g++ -O2 -std=gnu++17 -o gen.exe gen.cpp
g++ -O2 -std=gnu++17 -o sim.exe sim.cpp
mkdir -p judge
for seed in 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 \
            51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70; do
  f="judge/j_${seed}.txt"
  [ -f "$f" ] && continue
  ./gen.exe "$seed" 9 > "$f"
  ./sim.exe "$f" -calibrate >/dev/null || { rm -f "$f"; continue; }
  # Reject anything outside the judge's measured envelope: it must be small, and it must not
  # queue prefill on a remote (that queue provably never exceeds one request on the judge).
  fr=$(./sim.exe "$f" | sed -n 's/.*frames=\([0-9]*\).*/\1/p')
  if [ -z "$fr" ] || [ "$fr" -gt 50000 ]; then rm -f "$f"; fi
done
echo "judge-like tests: $(ls judge/*.txt 2>/dev/null | wc -l)"
