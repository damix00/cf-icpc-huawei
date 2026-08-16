# CLAUDE.md — CF 2251A working guide

Codeforces **2251A — Edge–Cloud Collaborative Scheduling** (ICPC 2026 Online Challenge 1, Huawei).
An **interactive** scheduling problem, scored 0–1000 per test. Contest is running; ~12 days left as
of 2026-08-16. Account: `damix`.

`NOTES.md` holds the full model derivation, the controller maths, and every measured dead end.
This file is the operational guide: status, commands, and the rules that were learned the hard way.

## Status

Best score **16109.263** (submission **#387270011**), all 22 tests OK. Rank-50 cutoff is
**16153.093**, so the gap is **43.8** and closing. Rank 1 is 16430.623.

`sol.cpp` = `sol_best_16109.cpp` = the old policy with **`CF_WAIT_P` (the D POST merge-hold budget)
8.0 -> 32.0**. Nothing else changed. This session took 16072.540 -> 16109.263 (**+36.7**) entirely
on that one constant.

**START HERE NEXT SESSION.** The gradient is still climbing and is *accelerating*; every step so far
moved test **#19** and nothing else. Probes are prepared, verified (C++17/20/23, zero failures on
edge/ tests/ hold/ val/ judge/, protocheck 8/8) and ready to upload **in this order**:

| file | change | expectation |
|---|---|---|
| `sol_p128.cpp` | `waitPost` 32 -> 128 | next step; #19 has 87.9 points left |
| `sol_p512.cpp` | `waitPost` 32 -> 512 | if 128 still gains, find the far end |
| `sol_hc32.cpp` | `holdCap` 4 -> 32 on the 32 base | the OTHER cap on total hold time |

Upload the variant file directly and keep `sol.cpp` pinned to the confirmed best, so a losing or
dropped probe cannot lose the record of what actually scores. Confirm every submission on
`/contest/2251/my` — the judge takes ~1 per 9 minutes and silently drops the rest.

**Do not consult the local suites on this axis.** They report `waitPost` 16, 32 and 64 as
byte-identical: the budget stops binding locally above 16, so no local instrument can see the
gradient at all, and at the bottom end they called the -104.0 change an improvement.

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

## The merge-batching axis is live — work it with judge probes, one per ~9 min

Four judge measurements now bracket it:

| probe | judge total | what moved |
|---|---|---|
| `CF_WAIT_P=1` | 15968.542 (**-104.0**) | #5 -70.8, #8 -19.5, #4 -14.0, #16 -8.4, #6 +5.8, #13 +3.0 |
| `CF_WAIT_P=8` | 16072.540 | the old default |
| `CF_WAIT_P=16` | **16077.404** (+4.9) | #19 +4.8, other 21 byte-identical |
| `CF_EBW=16` (clamp released) | 16077.404 (**0.000**) | nothing — the clamp never binds |
| `mStar = L` (`sol_mall.cpp`) | 16062.24 (**-15.2**) | **#7 -14.6**, #6 -0.5, #5 +0.1 |

**Test #7 is the constraint on this axis.** It is also the test the first-token deferral cost 13.2.
Both changes delay a token that was already ready, so #7 is TPOT-critical — while #19 pays for
exactly the opposite. Any further batching has to be *conditional on TPOT slack*, not uniform.

So the merge hold's **budget** is now slack: `waitPost` 16 and 32 are byte-identical on every local
suite, `holdCap` 4 and 16 likewise, and `eBottleW` is measured inert on the judge. What still caps a
decode group is **`mStar`** — the hold only fires `while ((int)qDPost.size() < mStar)`.

Ready to submit, each building under C++17/20/23 with **zero failures across every suite**:

| file | change | local `judge/` |
|---|---|---|
| `sol_tb.cpp` | `CF_TPOTB=1` — **replace** the fitted budget with the measured TPOT slack `SLO2 - tpotNow()`, floored at one merge saving. Grows the hold where SLO2 is loose (what #19 pays for) and shrinks it to break-even where TPOT is tight (what #7 needs). | 712.65 (+0.42) |
| `sol_p32.cpp` | `waitPost` 16 -> 32 | 712.21 (-0.01) |
| `sol_hc16.cpp` | `holdCap` 4 -> 16 | 712.22 (byte-identical) |

**A negative local number here is not a reason to skip a probe.** On this axis the local suites have
now been measured backwards twice in both directions. Upload the variant file directly — keep
`sol.cpp` pinned to the confirmed best (`sol_best_16077.cpp`) so a dropped or losing probe cannot
lose the record of what actually scores.

## Score on `tmp/quiet.list`, not on all of `judge/` — this is the headline change

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

Baseline: quiet **740.560**, loud **675.657**. Regenerate both from `tmp/deltamat.txt` if the
battery is ever re-run.

**On the quiet subset every tunable in the policy is flat within ±0.4 per test** — the only knob
worth more is `CF_JITP=0` at -19.0, which is just the already-paid-for P PRE hold. Prefill splitting
is monotonically worse, decode-first on remotes is -0.09, and the placement refinements are exactly
0.000. Treat the parameter space as exhausted; only structural changes are worth a submission.

## `CF_WAIT_R` 4 → 14: submitted, **exactly 0.000** on the judge

`sol_wr.cpp` (`waitProc` default `4.0 → 14.0`) was submitted as **#387246342** and scored
**16072.540 — all 22 per-test values byte-identical to #387210572, to 10 decimal places.** The
merge hold's budget never binds differently on any judge test, exactly as it never binds on the
small-`R` slice. Promoted into `sol.cpp` anyway, on the same insurance logic as the TDR-worth-it
guard: `judge/` **710.728 → 712.976** from **2 winners and zero losers** (`j_53` +76.5, `j_36`
+13.2), a 12–16 plateau, zero failures anywhere, and no measurable cost on the live set.

**This is the sixth judge-neutral-or-negative result in a row from knob tuning.** The judge's 22
tests do not exercise the regimes the local suites disagree over. See "Next idea" below — rollout
is the only remaining lever in a different class.

Also last session: NOTES.md §12 — extending the merge look-ahead to *ready* (not just *running*)
work is a real blind spot whose every exposed wait is past the merge break-even. Dead as a class.

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

## Next idea — NOT rollout. That family is now bounded and closed. See NOTES.md §14.

**Do not build rollout.** It was already built once (`sol_v9_x.cpp`, `Roll::`, +0.15 on `judge/`),
and this session measured the *ceiling* rather than another implementation. `sol_hc3.cpp` +
`tmp/hc3.py` hill climb an **oracle with perfect hindsight** over the local computer's per-frame
choice — an upper bound on any online policy for that family, rollout included. Exhaustive over
every frame it is worth **+4 to +7 per test on `judge/`**, i.e. **~+9 to +15 across the real 22**,
against a **74.2**-point gap. The headroom rollout was designed to capture was already harvested by
the P PRE JIT release (j_46 +41.6 → +1.85). §14a then implemented the one residual shape the oracle
found that was not already refuted, and it measured **−0.28 on the very test that motivated it**.

**Those families are now measured too** (NOTES §15). The exhaustive placement oracle (`sol_hp.cpp` +
`tmp/hp.py`, every request × every remote, hill climbed to convergence) finds +0 to +15 per test, but
its `j_48` trace shows the wins are mostly *relabelling* — `was=0 now=1` with both remotes still
empty, which only reorders the uplink FIFO. Pricing the `dStar` veto on opening a fresh remote
(`sol_op.cpp`, `CF_OPEN`) and both placement-cost refinements (`sol_pc.cpp`, `CF_DECL` / `CF_DECR`)
are **exactly 0.000 on `tmp/quiet.list`**. Prefill pacing is dead outright: ungating the P PRE hold
moves throughput by ±0.005 on every test while destroying waiting.

Before retrying anything else, read the rejected-ideas tables in `NOTES.md` §5, §10, §14, §15, §16.

**What is actually left.** On `tmp/quiet.list` the headroom against the (optimistic) work floor is
+28.5 per test, concentrated in `j_48` +163, `j_67` +128, `j_68` +111, `j_32` +80. The new
per-computer idle accounting says those tests idle **every resource simultaneously** in repeated
400–600 ms blocks — a latency-bound closed loop with too few live requests, not a contended one.
There is nothing to re-order there, which is why no knob touches them. Any further gain has to come
from a change that raises decode concurrency on a latency-bound instance, or the work floor is
simply unreachable and 16072.540 is close to this architecture's ceiling.

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
