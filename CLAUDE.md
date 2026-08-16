# CLAUDE.md — CF 2251A working guide

Codeforces **2251A — Edge–Cloud Collaborative Scheduling** (ICPC 2026 Online Challenge 1, Huawei).
An **interactive** scheduling problem, scored 0–1000 per test. Contest is running; ~12 days left as
of 2026-08-16. Account: `damix`.

`NOTES.md` holds the full model derivation, the controller maths, and every measured dead end.
This file is the operational guide: status, commands, and the rules that were learned the hard way.

## Status

Best score **16109.263** (submission **#387270011**), all 22 tests OK. **Rank 76.** The rank-50
cutoff has risen to **16166.452** (`pavement`), so the gap is **57.2 and widening** as others
improve. Rank 1 is 16430.623.

`sol.cpp` = `sol_best_16109.cpp` = the old policy with **`CF_WAIT_P` (the D POST merge-hold budget)
8.0 -> 32.0**. Nothing else changed; that one constant is the whole of the last +36.7.

**START HERE NEXT SESSION.** Do **not** resume knob work. Nine consecutive judge probes have now
returned 0.000 or negative, the parameter space has been searched exhaustively (NOTES §18h, §19),
and a ceiling audit of the two instance families that carry all the nominal headroom says the
achieved schedule is within a few percent of what those instances physically permit (NOTES §19).
Anything that moves the score from here has to change the *architecture*, not tune it.

### Everything measured on the judge since 16109.263 — all dead

| probe | change | judge | reading |
|---|---|---|---|
| `sol_p128` #387281899 | `waitPost` 32 -> 128 | **0.000**, byte-identical | budget saturated; `holdCap` also proven non-binding, since both hold tests scale with it |
| `sol_hp0` #387282957 | stop blocking D PRE during a D POST hold | **-7.895** | #4 -4.3, #8 -2.7, #6 -0.8, #13, #10 |
| `sol_hb` #387284805 | *also* block P POST during the hold | **-3.703** | #4 -1.1, #8 -2.0, #6 -0.4 |
| `sol_wr128` #387286019 | `waitProc` 14 -> 128 | **0.000** | the D PROC budget never binds at any magnitude |
| `sol_w8` #387287360 | `warmUp` 100 -> 8 | **0.000** | the only knob positive on BOTH calibrated instruments (quiet +0.63, `judge/` +0.31) — and still nothing |
| `sol_hpb` #387289015 | remote idles instead of taking prefill during a D PROC hold | **-48.83** | **all of it on #4** — 2nd biggest single-test move ever seen here |
| `sol_cf` #387290226 | split the prefill to end before the merge member lands | **-17.44** | again **all on #4**: the extra S per piece beats the alignment it buys |
| `sol_dn` #387290914 | drop the `dStar` widening tie-break when remotes are saturated | **-0.246** | #4 never moved — the gate does not fire there |

**Test #4 is the steepest gradient on the board and sits in a local optimum.** It has 204 points of
headroom and moves further than any other test under remote-side changes, but all three directions
tried are downhill (NOTES §20). Whole prefill pieces, no idling, is already right. The single
untested direction left is `sol_rd.cpp` (`CF_RPRE=0`, let a remote take decode ahead of prefill —
`prefillUrgent` is always true so it currently never does); built and verified, quiet -0.09,
loud -4.65.

The two hold results bracket `holdPreToo = 1` from both sides, so the block set is a genuine
optimum, not a fitted constant. `sol_p512.cpp` and `sol_hc32.cpp` are **dead — do not submit them**;
the `p128` identity proves neither constant can move a point.

### The two local instruments are now both discredited on this policy

`tmp/quiet.list` was calibrated to 13x better L1 than the full `judge/` suite, and it still called
`sol_w8` +0.63 against a judge 0.000 and `sol_hp0` +0.13 against a judge **-7.9**. It is not
*anti*-correlated either (it got `sol_hb`'s sign right), so it cannot be inverted — it is simply
uninformative about anything that depends on a *long* hold, because locally the budget never binds
and holds are always short. Keep using it as a **regression guard only**.

### What is architecturally closed (do not rebuild any of these)

Rollout (§14), placement (§15), prefill pacing (§15), wave-shape smoothing (§18c), raising `mStar`
under TPOT slack (§18b — the rate model is *right* to stop where it does), the hold block set
(§18d/f), both merge budgets (§18a/g), admission order (all 24 permutations searched), and now
**schedule-by-remaining-work in any form: `L_out` is never revealed to the solution** (§19), so no
policy can know a request's decode tail until it ends.

## Where the +36.7 came from

| `waitPost` | judge total | test #19 | step |
|---|---|---|---|
| 1 | 15968.542 | 875.4 | (also #5 -70.8, #8 -19.5, #4 -14.0, #16 -8.4) |
| 8 | 16072.540 | 875.4 | the old default |
| 16 | 16077.404 | 880.2 | +4.86 |
| **32** | **16109.263** | **912.1** | **+31.86** |

Two other tests bound the axis and must be watched on every probe:

* **#5** owns the *budget* at the bottom end: cutting it to the break-even costs **-70.8**, the
  largest single-test move ever recorded here. Saturated by `waitPost = 8`.
* **#7** punishes anything that delays an already-ready token: -13.2 from the first-token deferral,
  -14.6 from `mStar = L`. It is *indifferent to the budget* (907.5 at `waitPost` 1, 8, 16 and 32
  alike), so raising `waitPost` is free for it. Raising `mStar` is not — do not.
* **#14** has still never moved, now under 27 submissions. Almost certainly single-request and
  forced, like #1/#2/#3/#11.

Its 16072.540 predecessor came from two mechanisms (15995.995 -> 16072.540, +76.5):

1. **P PRE just-in-time release** (+56) -- hold a P PRE while the uplink is still carrying another
   *prefill*, so the shortest of a larger pool takes the FIFO slot. Both links are single-server
   FIFOs, so their order is frozen when work is released, and the release time is ours. Ungated it
   wrecks TPOT (j_47 448 -> 5072 ms, because TPOT is measured from a request's FIRST token), so it
   is gated to `activeDecode <= 2` plus a measured TPOT-slack test and a TDR guard.
2. **Placement cost that counts committed remote work** (+19.8, all of it on test #5,
   465.5 -> 487.3) -- `pickRemote` scored a remote by `pendProc` alone, ignoring the task it is
   running now and the decode groups already queued on it.

Codeforces keeps the **best** submission, so experiments are free and **anything shipped must beat
16109.263 to matter**.

**Per-test now:** `500, 500, 500.6, 795.9, 487.3, 385.3, 907.5, 830.2, 736, 683.3, 500.2, 799.9,
730.6, 415.3, 716.6, 980.7, 888.2, 915.7, 912.1, 998.2, 970.6, 955.2`.

## The merge-batching axis is CLOSED

Six judge measurements bracket the D POST hold's budget, and it is saturated:

| probe | judge total | what moved |
|---|---|---|
| `CF_WAIT_P=1` | 15968.542 (**-104.0**) | #5 -70.8, #8 -19.5, #4 -14.0, #16 -8.4, #6 +5.8, #13 +3.0 |
| `CF_WAIT_P=8` | 16072.540 | the old default |
| `CF_WAIT_P=16` | 16077.404 (+4.9) | #19 +4.8 |
| **`CF_WAIT_P=32`** | **16109.263** (+31.9) | #19 880.2 -> 912.1 |
| `CF_WAIT_P=128` | 16109.263 (**0.000**) | nothing — byte-identical, the budget no longer binds |
| `CF_EBW=16` | 16077.404 (0.000) | the E-bottleneck clamp never binds |
| `mStar = L` | 16062.24 (-15.2) | **#7 -14.6** |

The remaining cap is `mStar`, and raising it is refuted twice over: `mStar = L` costs 14.6 on #7, and
a TPOT-slack-gated version reads -1.5 on quiet with j_36 -20 and j_34 -16. The reason is structural,
not tuning — `waveRate`'s own-round-trip term is `L/C`, and with `L` requests live each emits one
token per cycle **regardless of m**, so merging past the model's optimum lengthens `C` without
raising `L`. The rate model is right to stop where it does. See NOTES §18b.

**Test fingerprint from these probes** (useful for reading any future result):

* **#19** responds to the *budget* and nothing else.
* **#4, #6, #8, #10, #13, #16** respond to the hold's *block set* and not to the budget.
* **#5** owns the budget only at the bottom end (-70.8 if cut to break-even); saturated by 8.
* **#7** is indifferent to the budget but TPOT-critical: -13.2 from the first-token deferral,
  -14.6 from `mStar = L`. Never delay an already-ready token for it.
* **#14** has still never moved, now under 31 submissions.

## Score on `tmp/quiet.list`, not on all of `judge/` — but see the caveat at the top

Seven judge-measured changes are all reproducible locally as knob flips, so the two instruments can
be compared **per change**. The judge's 22 tests never move by more than ~1 point per test under any
of them; `judge/`'s 40 move by up to **13.6**, because its mean is carried by a handful of
hyper-volatile instances (`j_64` swings **-502.9** on `CF_TDRG=0` alone). **The real 22 contain
nothing like them** — the largest single-test judge move in 23 submissions is +21.8.

Splitting `judge/` by volatility (quiet = no change in that battery moves it >3 points) gives
`tmp/quiet.list` (23 tests) and `tmp/loud.list` (17). The quiet subset reproduces the judge's
response profile with **L1 error 1.78 against 23.75 for the full suite — 13x better**, matching sign
and magnitude on six of seven changes. Full table and method in NOTES.md section 16.

```sh
sh score.sh sol_x.cpp "$(cat tmp/quiet.list | tr '\n' ' ')"     # THE instrument
sh score.sh sol_x.cpp "$(cat tmp/loud.list  | tr '\n' ' ')"     # only a "did I break it" guard
```

Baseline for the CURRENT `sol.cpp` (waitPost 32): quiet **739.248**, loud **675.634**.
(The 740.560/675.657 pair was the waitPost-8 baseline.)

**Caveat added later:** this instrument was calibrated against seven changes that all bind locally.
It is uninformative about anything that depends on a *long* hold, and it has since called
`sol_w8` +0.63 (judge 0.000) and `sol_hp0` +0.13 (judge **-7.9**). Regression guard, not oracle. Regenerate both from `tmp/deltamat.txt` if the
battery is ever re-run.

**On the quiet subset every tunable in the policy is flat within ±0.4 per test** — the only knob
worth more is `CF_JITP=0` at -19.0, which is just the already-paid-for P PRE hold. Prefill splitting
is monotonically worse, decode-first on remotes is -0.09, and the placement refinements are exactly
0.000. Treat the parameter space as exhausted; only structural changes are worth a submission.

## `CF_WAIT_R`: closed at every magnitude

`waitProc` 4 -> 14 scored **exactly 0.000** (#387246342) and 14 -> 128 scored **exactly 0.000** again
(#387286019, `sol_wr128.cpp`). The remote-side merge budget never binds anywhere on the judge.

This matters as a *method* lesson. quiet showed `CF_WAIT_R` 14, 32 and 128 byte-identical while 1 did
move it — the exact signature that paid off on `waitPost`. **That signature alone is not evidence a
knob is live.** What made `waitPost` different was a judge measurement at the bottom end
(`CF_WAIT_P=1`, -104.0, #387266525) proving the knob had real authority over real tests. Require a
judge-side bottom-end measurement before spending slots climbing a locally-invisible ladder.

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
sh score.sh sol_x.cpp "$(cat tmp/quiet.list|tr '\n' ' ')"   # THE instrument (23 tests, base 740.560)
sh score.sh sol_x.cpp "judge/*.txt"            # the full 40 -- its mean is NOT trustworthy, see below
sh mkjudge.sh                                  # (re)generate it, gen.cpp profile 9
sh score.sh sol_x.cpp "hold/h_*.txt"           # any suite
sh cmp.sh sol sol_x                            # per-test delta; both tags must have been
                                               #   scored on the SAME glob (score.sh overwrites
                                               #   tmp/scores_<tag>.txt each run)
BIN=tmp/sim_sol.exe TESTS="tests/g_*.txt" sh sweep2.sh < cfgs.txt   # env sweep, one mean per line
./sim.exe tests/g_5_3.txt -stats               # utilisation, group sizes, decode-stage breakdown,
                                               #   per-link and per-computer IDLE with the biggest
                                               #   gaps timestamped -- says WHERE the makespan goes
py tmp/hp.py judge/j_48.txt                    # exhaustive placement oracle (request x remote)
./analyze.exe <test> [tp]                      # per-test ceiling (see caveat below)
py pipecheck.py tmp/pc/g_5_1                   # frame-by-frame replay over REAL pipes
py protocheck.py                               # byte-dribble stdin, CRLF, empty frames, EOF, ...
py edgegen.py edge                             # regenerate corner-case instances
py tmp/hillclimb.py judge/j_52.txt              # what is a perfect per-frame decision worth?
py tmp/hc2.py judge/j_46.txt                    # ... and which action should it have been?
CF_HCLIST=tmp/hc2_best.list CF_HCTRACE=1 ./tmp/sim_hc2.exe judge/j_46.txt   # print their context
```

### `judge/`'s full mean is the wrong statistic — the volatile tests carry it

`judge/` (`sh mkjudge.sh`, `gen.cpp` profile 9) reproduces the judge envelope: `R` 1-59, frames
21-18 363, `w_tp` mostly from `{0, 0.25, 0.4, 0.5}`, and instances at `tpComp ~= 0` with `R > 1`.
Its *instances* are right; its *mean* is not, because 17 of the 40 swing by tens or hundreds of
points under changes the judge barely notices:

| change | `judge/` all 40 | quiet 23 | real judge |
|---|---|---|---|
| JIT prefill-down release | **+8.03** | -0.027 | **-0.04** (full suite gets the sign wrong) |
| TDR-worth-it guard | +13.63 | +0.007 | 0.00 |
| local order D POST first | -0.289 | +0.019 | -0.266 |
| first-token deferral | **+3.83** | -0.241 | **-1.048** |
| P PRE reorder, `activeDecode == 0` | +20.2 | -- | +1.70 |
| `CF_JITS=3` | +0.24 | +0.108 | +0.008 |

**Score `tmp/quiet.list` first.** The full-40 mean over-predicts by ~10x and has got the sign wrong
twice; the quiet subset has an L1 error of 1.78 across the whole battery against 23.75. Then check
small-`R`, then the big suites as a regression guard, then `tmp/loud.list` only to confirm nothing
was destroyed. Full derivation in NOTES.md section 16.

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

## Next idea — there is no parametric one left. See NOTES §18h and §19.

Every knob has been swept on the calibrated instrument and every survivor has been submitted; the
last nine judge probes returned 0.000 or negative. Before touching anything, read the rejected-ideas
tables in `NOTES.md` §5, §10, §14, §15, §16, §18 and §19 — the list of closed families is long and
several were closed twice.

**The ceiling audit (NOTES §19) is the important part.** `analyze.exe` reports 4141 points of
nominal headroom across the 40 `judge/` instances, 103.5 per test, and it is almost entirely an
artefact:

* **j_66** (the closest analogue of judge #6): ceiling 476.4, achieved **453.6 = 95.2 %**. Its own
  latency floor caps `tp` at 0.182 against a `tp_UB` of 0.354, so `tpComp` can never exceed 0.476.
  E runs at **95.5 %** utilisation, `Cdec = 37.27` of which `3S = 28.4`, and `retarget()` sits exactly
  on the crossing point of the two binding terms. **#6's 385.3 is not 615 points of headroom.**
* **j_57** (ceiling 1000, achieved 418.2): `u = 652.8 ms per token`, and request 0 arrives *alone* at
  t=4977 with `L_in = 2600` — a **1,697,386 ms** uplink transfer that must be released (holding with
  nothing in flight is a stuck state) and behind which the downlink can deliver nothing. Achieved
  elapsed exceeds the work floor by **1.713e6, which is that transfer to three digits.**

So the remaining gap is head-of-line delay no online policy can avoid. If a future session wants to
move the number, it needs a different architecture, not a better-tuned one — and it should first
reproduce the two audits above to confirm that conclusion rather than take it on trust.

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
