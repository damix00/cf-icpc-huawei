#!/bin/sh
# Score a strategy source across a test suite and print the mean.
#
#   ./score.sh                       # score sol.cpp on tests/
#   ./score.sh sol_v2.cpp            # score a variant
#   ./score.sh sol_v2.cpp "tests/g_5_*.txt tests/g_6_*.txt"
#   STATS=1 ./score.sh sol.cpp       # also print per-test utilisation
#
# Each variant builds its own sim binary, so several may be scored concurrently.
set -e
SRC=${1:-sol.cpp}
GLOB=${2:-"tests/ex1.txt tests/ex2.txt tests/g_*.txt"}
TAG=$(basename "$SRC" .cpp)
BIN="tmp/sim_$TAG.exe"
mkdir -p tmp
g++ -O2 -std=gnu++17 -DSOL_SRC="\"$SRC\"" -o "$BIN" sim.cpp

FLAG=""
[ -n "$STATS" ] && FLAG="-stats"

# shellcheck disable=SC2086
for f in $GLOB; do
  [ -f "$f" ] || continue
  "./$BIN" "$f" $FLAG || true
done | tee "tmp/scores_$TAG.txt"

echo "-----"
awk -v src="$SRC" '
  /score=/{split($0,a,"score=");split(a[2],b," ");s=b[1];t+=s;n++;
           split($0,c,"test=");split(c[2],d," ");name=d[1];
           if(n==1||s<worst){worst=s;wname=name}}
  /FAILED/{f++; print "  !! " $0}
  END{printf "%s: tests=%d failures=%d MEAN=%.3f  worst=%.1f (%s)\n", src, n, f+0, n?t/n:0, worst, wname}
' "tmp/scores_$TAG.txt"
