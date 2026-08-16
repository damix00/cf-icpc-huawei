#!/bin/sh
# Per-test delta between two scored variants.   sh cmp.sh ref claude_sol
# Names are the TAGs used by score.sh: the source path minus .cpp, separators folded to '_'
# (claude/sol.cpp -> claude_sol).
#
# Both tags must have been scored on the SAME suite -- score.sh overwrites tmp/scores_<tag>.txt on
# every run, so scoring one variant on tests/ and the other on hold/ leaves nothing to join.
A=${1:-sol}; B=${2:-sol_v1}
for t in "$A" "$B"; do
  [ -f "tmp/scores_$t.txt" ] || { echo "no tmp/scores_$t.txt -- run: sh score.sh <path>.cpp \"<glob>\"" >&2; exit 2; }
done
join -j1 \
  <(awk '/score=/{split($0,a,"test=");split(a[2],f," ");split($0,b,"score=");split(b[2],s," ");print f[1],s[1]}' "tmp/scores_$A.txt" | sort) \
  <(awk '/score=/{split($0,a,"test=");split(a[2],f," ");split($0,b,"score=");split(b[2],s," ");print f[1],s[1]}' "tmp/scores_$B.txt" | sort) |
awk -v a="$A" -v b="$B" '
  {d=$3-$2; if (d>0.05||d<-0.05) printf "%-20s %8.1f -> %8.1f  %+8.1f\n", $1, $2, $3, d; s2+=$2; s3+=$3; n++}
  END{
    if (n==0) {
      print "no tests in common -- the two tags were scored on different suites; rescore both on the same glob" > "/dev/stderr"
      exit 2
    }
    printf "\n%-20s mean %8.3f -> %8.3f  %+8.3f   (%s -> %s, %d tests)\n", "", s2/n, s3/n, (s3-s2)/n, a, b, n
  }'
