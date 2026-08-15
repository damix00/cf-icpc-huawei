# CLAUDE.md — CF 2251A working guide

Codeforces **2251A — Edge–Cloud Collaborative Scheduling** (ICPC 2026 Online Challenge 1, Huawei).
An **interactive** scheduling problem, scored 0–1000 per test. Contest is running; ~13 days left as
of 2026-08-15. Account: `damix`.

`NOTES.md` holds the full model derivation, the controller maths, and every measured dead end.
This file is the operational guide: status, commands, and the rules that were learned the hard way.

## Status

Best submission **#387160821 = 15995.995**, all 22 tests OK. Leader `ChefChampion` 16365.928 → gap
**369.9 (2.3 %, ~16.8/test)**. Goal of beating the standings is **not** met.

Submission history: `0` (idleness) → 15979.224 → 15900.424 (reverted) → 15981.743 → 15981.743 →
15990.629 → 15952.333 (D PRE-first ungated, reverted) → 15988.091 (gated + placement) →
**15995.995** (gated only). Codeforces keeps the **best** submission, so experimenting is safe.

The winning change moved exactly two tests and left the other twenty untouched:
`#5 461.7→465.5 (+3.8)`, `#6 383.8→385.3 (+1.5)`.

**Shipped this round:** D PRE before D POST, gated on realised throughput (`swapMin`, `sol.cpp`).
Ungated it was −38.3; the gate protects #3/#8/#13/#16 completely and keeps #5/#6. It still forgoes
#7 (+7.3 under the ungated version), but lowering `swapMin` to reach it is measurably wrong —
`judge/` drops to 668.84 at any threshold ≤0.02, and #8/#13/#16 (−5.7/−2.4/−0.6 ungated) would come
back with it. `CF_SWAP=0.20` reads 669.757 on `judge/` but 0.10 and 0.35 both read 669.59, so that
is a spike, not a plateau (rule 4).

**One change is parked, with a measured reason:**
* **`pickRemote` placement/wave-width decoupling** (`sol_place2.cpp`). Worth **+229 on `h_4_15` and
  +230 on `h_4_16`** locally — four remotes were idle for an entire 141-second run — but only +0.12
  on `judge/` and **−8.1 on judge test #6**. Its gains live entirely in the large-`R` regime the
  judge does not have. Keep it for the local suites; do not ship it to the judge without new
  evidence. See `NOTES.md` §9.

## THE bug — never reintroduce it

```c
ilen = fread(ibuf, 1, sizeof(ibuf), stdin);   // WRONG on an interactive problem
```

`fread` **never returns a short read** — it loops on the underlying `read()` until the buffer is
full or EOF. The interactor sends one frame then waits for our answer, so this blocked forever
after consuming ~300 bytes of a 64 KB request → `IDLENESS_LIMIT_EXCEEDED, 0 ms, 0 MB` on all 22
tests. Now uses a single low-level `read`/`_read` (see `SOL_READ` in `sol.cpp`).

**Why it survived:** `sim.cpp` does `#define LOCAL_SIM` + `#include SOL_SRC`, so `main()`'s
stdin/stdout adapter is compiled out and never exercised by any scoring run. A redirected file
cannot reproduce it either — files only short-read at EOF.
**Therefore: anything touching `main()` must be re-checked with `pipecheck.py` and `protocheck.py`.**

## Commands

```sh
g++ -O2 -std=gnu++17 -o sol.exe sol.cpp        # the submission
sh score.sh sol.cpp                            # score on tests/ -> per-test + MEAN + failures
sh score.sh sol_x.cpp "judge/*.txt"            # THE suite to trust -- see below
sh mkjudge.sh                                  # (re)generate it, gen.cpp profile 9
sh score.sh sol_x.cpp "hold/h_*.txt"           # any suite
sh cmp.sh sol sol_x                            # per-test delta; both tags must have been
                                               #   scored on the SAME glob (score.sh overwrites
                                               #   tmp/scores_<tag>.txt each run)
BIN=tmp/sim_sol.exe TESTS="tests/g_*.txt" sh sweep2.sh < cfgs.txt   # env sweep, one mean per line
./sim.exe tests/g_5_3.txt -stats               # utilisation + group sizes + decode-stage breakdown
./analyze.exe <test> [tp]                      # per-test ceiling (see caveat below)
py pipecheck.py tmp/pc/g_5_1                   # frame-by-frame replay over REAL pipes
py protocheck.py                               # byte-dribble stdin, CRLF, empty frames, EOF, ...
py edgegen.py edge                             # regenerate corner-case instances
```

### `judge/` is the suite that matters — score every candidate on it

`tests/`, `hold/` and `val/` draw `R = 50..2000` (`gen.cpp` profiles 0–8) and run up to **910 422
frames**. The judge's tests finish in ≤421 ms of our CPU and never queue more than one request on a
remote's prefill queue. Only 24 of 116 local tests have `R ≤ 50`, so the other 92 vote on a regime
the judge never runs — which is exactly why full-suite means stopped predicting judge deltas.

`judge/` (`sh mkjudge.sh`, `gen.cpp` profile 9) reproduces the judge envelope: `R` 1–59 (18 %
single-request), frames 21–18 363, `w_tp` drawn mostly from `{0, 0.25, 0.4, 0.5}`, and — critically —
instances at `tpComp ≈ 0` with `R > 1`, the class that cost submission 387157181 its −42.1 and that
**no profile 0–8 test reproduces**.

It is so far the only local instrument that gets the sign right on a real judge measurement:

| variant | judge | `judge/` | 116-suite |
|---|---|---|---|
| 387125285 baseline | 15990.629 | 669.348 | 839.580 |
| + D PRE-first, ungated | 15952.333 | **668.754** | 840.296 |
| + D PRE-first, gated | 15988.091 | **669.718** | 844.414 |

The 116-suite ranked the losing change as an improvement; `judge/` did not.

Also keep the small-`R` slice of the old suites as a second opinion:

```sh
for f in tests/g_*.txt hold/h_*.txt val/v_*.txt; do \
  r=$(awk 'NR==3{N=$1} NR==4+N{print $1;exit}' "$f"); [ "$r" -le 50 ] && echo "$f"; done > tmp/small.list
sh score.sh sol_x.cpp "$(cat tmp/small.list | tr '\n' ' ')"
```

Suites are independently seeded (`gen.cpp` profiles 0–8), 118 tests total, zero failures anywhere,
plus 23 corner cases in `edge/` and 40 judge-matched cases in `judge/`:

| suite | n | 387125285 (15990.629) | current `sol.cpp` |
|---|---|---|---|
| **`judge/`** | **40** | **669.348** | **669.596** |
| small-`R` slice | 24 | 620.103 | 623.583 |
| `tests/` | 26 | 838.971 | 838.908 |
| `hold/` | 54 | 837.852 | 839.926 |
| `val/` | 36 | 842.611 | 842.625 |

`sol_place2.cpp` (the parked placement fix) reads 669.718 / 620.103 / 838.908 / 848.257 / 842.625 —
better on every local suite, and −8.1 on judge test #6.

## Verification battery — run ALL of it before submitting

```sh
for std in c++17 c++20 c++23; do g++ -O2 -std=$std -o tmp/t.exe sol.cpp; done   # judge uses C++23
for t in edge/*.txt; do ./sim.exe "$t" | grep FAILED; done                       # must be silent
py protocheck.py                                                                 # 8/8
rm -rf tmp/pc && mkdir -p tmp/pc
for t in tests/*.txt; do ./sim.exe "$t" -dump "tmp/pc/$(basename $t .txt)" >/dev/null; done
py pipecheck.py $(for t in tests/*.txt; do echo "tmp/pc/$(basename $t .txt)"; done)   # 28/28
```

`failures=0` is non-negotiable — a protocol violation, stuck state, or TLE is a **zero** on that
test, worth ~50 mean.

## Submitting

The Submit button **ignores synthetic clicks** (by coordinate and by ref). What works:

1. `navigate` to `https://codeforces.com/contest/2251/submit`
2. `find` the file input → `file_upload` with the absolute path to `sol.cpp`
3. JS tool: `document.querySelector('input.submit[type=submit]').click()`
4. Wait ~20 s, reload `https://codeforces.com/contest/2251/my`

The judge accepts roughly **one submission per ~9 minutes**; extra attempts silently do nothing.
Per-test scores: fetch `/contest/2251/submission/<id>` and regex
`#(\d+):\s*(\w+)\s*\[(\d+)ms,\s*(\d+)MB\]:?\s*points\s*([\d.]+)`.

## Hard-won rules

1. **Never edit `sol.cpp` for an experiment.** Copy to `sol_<tag>.cpp`, score that (`score.sh`
   builds a per-variant sim binary, so variants can run concurrently).
2. **Local means do not predict the judge.** A change worth **+17.8 mean over 118 local tests lost
   −78.8 on the judge**, all of it in one test. Confirm on `hold/` *and* `val/`, then on the judge.
3. **`analyze.cpp`'s ceiling is optimistic** — it ignores flow-shop structure and assumes a
   saturated link through the decode tail. Two long dead ends came from trusting it. `g_6_3` scores
   181 and is at 99 % of its true ceiling; `g_5_1` is ~97 %. Check a trace before chasing a gap.
4. **Verify a winning constant sits on a plateau, not a spike** — spikes are overfitting.
5. Prefer principled scheduling over fitted constants.

## What the judge revealed about the 22 tests

- **Waiting-weighted**: gating P PRE-first on `w_tp >= 0.75` removed the losses on #9/#21/#22 and
  kept the gain on #6 → **exactly one test has `w_tp >= 0.75`**.
- **Lightly loaded**: SJF on the remote prefill queue changed the score by *exactly nothing*, so
  that queue never holds more than one request.
- **Small**: every test runs in ≤421 ms against a 15 s limit — ~35× of the budget is unused.
- Tests 1/2/3/11 sit at `tp == tp_base` with waiting = 1.0 → single-request tests, 500 is the max.
- Real gap lives in **#6 (381), #14 (415), #5 (462)**.

## Current strategy in one paragraph

Admit fast (throughput ∝ live decode population), keep prefills whole (never split), take every
ready request into each decode group, and choose how many remotes to spread a decode wave across
from an analytic rate model tempered by measured per-resource availability. Bounded waiting merges
D POST / D PROC groups, gated so it never fires when the link is the bottleneck and its time is
nearly all payload. Prefill priority on the local computer flips to P PRE-first only when
`w_tp >= 0.75`, because TDR's clock stops at P POST. Every hold is gated on something being
in flight, so a stuck state is impossible.

Tunables are `getenv`-overridable (`CF_*`) for sweeping; **defaults are what the judge runs.**

## Next idea (untried)

**Rollout / lookahead.** The model is exact and deterministic apart from future arrivals and the
hidden `Lout`, and ~35× of the time limit is unused. Evaluating candidate actions by simulating
forward with the *exact* simulator — instead of the analytic model, which is 3–5× optimistic in
absolute terms — is the only remaining approach in a different algorithmic class. Every policy
lever (splitting, wave depth, group capping, admission pacing, priority orders, remote placement)
is already at a local optimum; see the rejected-ideas table in `NOTES.md` before retrying any.

## The one rule this round added

**A change is only believable if `judge/` agrees.** The 116-test mean called the −38.3 regression an
improvement (+0.72), and so did the small-`R` slice (+2.98). `judge/` called it correctly (−0.59).
Score order: `judge/` first, then the small-`R` slice, then the full suites as a regression guard —
a change that wins on `judge/` and merely holds steady elsewhere is a better bet than the reverse.

**Read the score function before trading one component for another.** `tp = totalTokens / makespan`
with `totalTokens` fixed, and `tpBase`/`tpUB`/`SLO1`/`SLO2`/`distBase`/`w_tp`/`w_c` are all given at
startup. Judge test #3 sits at `tpComp ≈ 0.001` with waiting exactly `1.0`: throughput there is not
winnable, so anything that spends waiting to buy throughput is pure loss. The realised share of the
`(tpBase, tpUB)` span is observable online and converges from below — the safe direction.
