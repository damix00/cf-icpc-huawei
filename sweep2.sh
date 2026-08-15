#!/bin/sh
# Sweep environment settings for one already-built sim binary and print the suite mean per config.
#   BIN=tmp/sim_v1.exe TESTS="tests/*.txt" sh sweep2.sh <<'EOF'
#   CF_SJF=1
#   CF_SJF=0
#   EOF
BIN=${BIN:-tmp/sim_v1.exe}
TESTS=${TESTS:-"tests/ex1.txt tests/ex2.txt tests/g_*.txt"}
while IFS= read -r cfg; do
  [ -n "$cfg" ] || continue
  # shellcheck disable=SC2086
  out=$(for f in $TESTS; do env $cfg "./$BIN" "$f" || echo "FAILED"; done)
  echo "$out" | awk -v c="$cfg" '
    /score=/{split($0,a,"score=");split(a[2],b," ");t+=b[1];n++}
    /FAILED/{f++}
    END{printf "mean=%8.3f  fails=%d  %s\n", n?t/n:0, f+0, c}'
done
