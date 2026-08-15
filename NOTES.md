# CF 2251A — Edge–Cloud Collaborative Scheduling (ICPC 2026 Online Challenge 1, Huawei)

Statement: https://codeforces.com/contest/2251/problem/A

## Status

| submission | what | score |
|---|---|---|
| 387089802 | before the fix | **0** — Idleness limit exceeded on every test |
| 387118787 | I/O fix + reworked scheduler | 15979.224 |
| 387121888 | + P PRE-first, ungated | 15900.424 (**worse**) |
| 387122779 | + P PRE-first gated on `w_tp` | 15981.743 |
| 387123660 | + SJF on the remote prefill queue | 15981.743 (no change) |
| 387125285 | + wider remote-spread tolerance | 15990.629 |
| 387157181 | D PRE before D POST, ungated | 15952.333 (**worse**, −38.3 — see §8) |
| 387159828 | + throughput gate on the swap, + placement fix | 15988.091 (gate works; placement costs #6 −8.1) |
| **387160821** | throughput-gated swap only | **15995.995** ← best |

Rank **58**. Leader `ChefChampion` 16365.928 → gap **375.3 points (2.3 %, ~17/test)**.

Per-test of the best run (387125285):
`500, 500, 500.6, 745, 461.7, 383.8, 907.5, 830.2, 736, 682.7, 500.2, 799.8, 731, 415.3, 714.8,
980.7, 888.2, 915.7, 875.4, 996.6, 970.3, 955.2`.

## 1. The bug that scored 0 on all 22 tests

The input refill was `fread(ibuf, 1, 65536, stdin)`. **`fread` never returns a short read** — it
loops on the underlying `read()` until the buffer is full or EOF. This is an interactive problem:
the interactor sends one frame then waits for our answer, so the solution blocked forever having
consumed ~300 bytes of a 64 KB request. `0 ms, 0 MB` on every test is exactly that signature.

A redirected file cannot reproduce it (files only short-read at EOF), which is why the whole local
suite passed. Fixed to a single low-level `read`/`_read`.

**Structural cause:** `sim.cpp` compiles the strategy in-process under `LOCAL_SIM`, so `main()` is
never exercised by scoring runs. Anything touching `main()` must be re-checked with `pipecheck.py`
(frame-by-frame replay over real pipes) and `protocheck.py` (byte-at-a-time stdin, CRLF, empty
frames, every TDN shape, EOF).

## 2. What the real judge told us (worth more than any local result)

* **The 22 tests are waiting-weighted.** Gating P PRE-first on `w_tp >= 0.75` removed the losses on
  tests #9/#21/#22 and kept the gain on #6 — so **exactly one of the 22 tests has `w_tp >= 0.75`**.
* **They are lightly loaded.** Shortest-job-first on the remote prefill queue changed the score by
  *nothing at all* (15981.743 → 15981.743), so that queue never holds more than one request.
* **They are small.** Every test runs in ≤421 ms against a 15 s limit — roughly 35× headroom unused.
* **Single tests swing enormously.** One priority-order change moved test #9 by −72 points. Local
  suites never showed that concentration.
* **Local means do not predict real deltas.** A change worth **+17.8 mean over 118 local tests**
  lost **−78.8** on the judge. Only the judge is trustworthy; it rate-limits to roughly one
  accepted submission per ~9 minutes.
* Per-test scores of the best run: `500, 500, 500.6, 745, 461.7, 381, 907.5, 830.2, 733.2, 682.7,
  500.2, 799.8, 731.3, 415.3, 714.8, 980.7, 888.2, 915.7, 875.4, 990.5, 970.3, 955.2`.
  Tests 1/2/3/11 sit at `tp == tp_base` with the waiting component at 1.0 — single-request tests
  where 500 is the maximum. The real differentiators are **#6 (381), #14 (415), #5 (462)**.

**Submitting is flaky through the UI.** Clicking the Submit button by coordinate or by ref often
does nothing. What works reliably: upload the file, then run
`document.querySelector('input.submit[type=submit]').click()` via the JS tool.

## 3. Local results (118 tests over three independently seeded suites)

`tests/` 28, `hold/` 54, `val/` 36 — baseline 818.9 → **838.1**, zero protocol failures anywhere,
plus 23 adversarial corner cases in `edge/`.

## 4. Model

Resources, each serial: local **E**, **K** remotes, the **UP** link, the **DOWN** link. UP and DOWN
are independent FIFOs running concurrently. A task occupies its computer for `S + dur`, so every
extra task costs a whole `S`. Transfers cost `lat + u*len`, `u = 8*bytes_per_token/(bw*1e6)`.

Two independent lower bounds on elapsed time (`analyze.cpp` computes both):

1. **Latency floor** — a request's steps are strictly serial, so it cannot beat one token per
   round trip `C = 3S + dpre(m) + dproc(m) + dpost(m) + 2(lat + u*m)`.
2. **Work floor** — the busiest resource's total unavoidable work.

**Both are optimistic**, and that repeatedly misled the optimisation:
* `g_6_3` scores 181 and looks like the worst test in the suite — it is at **99 % of its ceiling**
  (one request holds 412 tokens × a 96 ms round trip).
* `g_5_1` looked 278 points short. Its trace shows the last P POST landing 65 s after the last
  prefill uplink transfer: prefill is a two-machine flow shop, whose makespan is `W + p_max`
  regardless of order. It is ~97 % optimal.
* `h_5_13`'s "reachable" throughput assumes the link stays saturated through the decode tail, which
  no schedule can do once too few chains remain.

### The controller

With `L` live decoding requests as waves of `m` over `d` remotes:

```
de = min(d, m)                         # a wave of m can only touch m remotes
up = de*lat + u*m ;  mr = m/de
C  = 3S + dpre(m) + dproc(mr) + dpost(m) + 2*up
rate = min( L/C,                                   # its own round trip
            availE * m /(2S + dpre + dpost),       # local computer
            availR * d*mr/(S + dproc(mr)),         # the d remotes
            availUp* m /up, availDn * m /up )      # the two link directions
```

`avail*` is the share of each resource prefill has **not** already taken, measured online.
`mStar` is maximised over `m`; `dStar` jointly over `(m, d)` at the projected population.

Four corrections, each a bug when missing:
* `de = min(d,m)`, and remote capacity scales with `d` — otherwise a wave of 1 looks identical on
  1 or 8 remotes and the tie-break spreads a high-latency instance across everything.
* Size the population by `qPPre.size() + Σload`, not `activeDecode` — placement is decided at
  P PRE, so sizing by the live decode count answers "how many remotes?" with `L=1` during ramp-up
  and pins everything onto remote 0 (`g_1_3` −378, `g_4_3` −146).
* Warm-up guard on `avail` — work is charged when a task is *issued*, so early on the charged
  total exceeds elapsed time and every resource looks saturated.
* Tie tolerance `dTol = 0.04` — within it the model cannot distinguish the options, and extra
  remotes buy pipeline depth it does not represent. Worth **+8.9 on the judge**.

### Waiting

`D POST` waits for more DOWN arrivals, `D PROC` for more UP arrivals; `D PRE` never waits, because
only E-side tasks feed `qDPre`. Holding D POST must also hold D PRE, or E spends the wait on a
small D PRE (−26 on `g_4_3`). The look-ahead includes tasks still *running*, not just queued
transfers. `batchingHelpsBottleneck` suppresses batching when the link is the bottleneck and its
time is nearly all payload (`u*m >> lat`), where a bigger group moves identical bytes for nothing.

Every hold is gated on `inFlight` (a busy remote or queued transfer), which guarantees a future
frame — so a stuck state is impossible.

## 5. Tried and rejected (all measured)

| idea | result |
|---|---|
| Prefill piece splitting | monotonically worse at every split size; keep prefills whole |
| Splitting the ready set into more waves | −42 on the degenerate class; extra transfers each cost a full `lat` |
| Capping groups at `mStar` | worse; `mStar` is a floor for *waiting*, never a ceiling |
| Smaller groups to cut TPOT on `w_c`-heavy tests | worse — contention costs more than `C(m)` grows |
| Pacing admission against the uplink backlog | much worse; the link is fixed-work, holding prefill just starves it |
| Decode-first on remotes | −3.5 |
| Limiting the admission pool | strictly worse — admitting fast raises `L`, and rate ∝ `L` |
| Longest-job-first admission | +1.7 on `tests/`, −1.3 on `hold/` → rejected |
| P PRE-first ungated | **−78.8 on the judge**; correct only when `w_tp >= 0.75` |
| SJF on the remote prefill queue | +0.96 local, **exactly 0** on the judge |
| Disabling the availability model | −5.0 |

## 6. Where the remaining 2.3 % might be

Every *policy* lever above is at a local optimum; perturbations are neutral or negative on both
the local suites and the judge. What is left untried:

* **Rollout / lookahead.** The model is exact and deterministic apart from future arrivals and the
  hidden `Lout`, and 35× of the time limit is unused. Evaluating each candidate action by
  simulating forward with the *exact* simulator (rather than the analytic model, which is 3–5×
  optimistic in absolute terms) is the one remaining approach in a different algorithmic class.
* **Judge-driven tuning.** Local means demonstrably mispredict; every candidate should be measured
  on the judge, at ~9 minutes per measurement.
* Tests **#5, #6, #14** hold most of the visible gap and are all in the small/degenerate class.

## 7. Rules for working here

1. Never edit `sol.cpp` for an experiment — copy to `sol_<tag>.cpp` and score that.
2. `failures=0` is non-negotiable; a failure is a zero on that test.
3. Confirm on `hold/` **and** `val/` before believing a `tests/` gain — then confirm on the judge.
4. Re-run `pipecheck.py` and `protocheck.py` after anything touching `main()`.
5. Prefer principled scheduling to fitted constants, and check a winning constant sits on a
   plateau rather than a spike.

## 7. The local suite measures the wrong regime

The judge's tests are **small** (≤421 ms of our CPU against 15 s) and **lightly loaded**. `gen.cpp`
draws `R = UI(50,600)` for most profiles and up to 2000 for profiles 7/8; `tests/g_8_2.txt` alone
runs **910 422 frames**. Only **24 of 116** local tests have `R ≤ 50`.

On that small-`R` subset every existing knob is already flat within ±3, while the same knobs swing
±71 on the full suite:

| config | small-`R` (24) | all 116 |
|---|---|---|
| default | **620.103** | **839.580** |
| `CF_SJF=0` | 606.160 | 801.2 |
| `CF_POOL=4` | 620.567 | 822.4 |
| `CF_WAIT_R=0` | 619.997 | 831.7 |
| `CF_ORDW=0.5` | **−0.05** | **+2.30** |

That last row is the trap in miniature: `CF_ORDW=0.5` is worth +2.30 on the 118-mean, consistent on
all three independently-seeded suites (hold +2.47 / tests +1.60 / val +2.68), and is worth **nothing**
in the regime the judge actually runs. **Score every candidate on the small-`R` subset**
(`tmp/small.list`, regenerate with the one-liner in `CLAUDE.md`) before believing a full-suite mean.

Other properties of the small-`R` subset, all measured: no resource exceeds 0.88 utilisation and most
sit at 0.15–0.55; **18 of 24 are latency-bound rather than work-bound** by `analyze.exe`'s two floors.
So `waveRate` / `avail` / `batchingHelpsBottleneck` — all capacity reasoning — are answering a
question these tests do not ask. Since `totalTokens` is fixed, `tp = totalTokens / makespan`, i.e.
**maximising throughput is exactly minimising makespan.**

## 8. What submission 387157181 cost, and what it bought

Ranking D PRE above D POST (`"1302"→"1320"`, `"3102"→"3120"`) measured **+2.98 on the small-`R`
subset** and +0.72 on the 116. On the judge it scored **15952.333, −38.3**. The per-test diff:

```
#3  500.6 -> 458.4  -42.1      #7  907.5 -> 914.8  +7.3
#8  830.2 -> 824.5   -5.7      #5  461.7 -> 465.5  +3.8
#13 731.0 -> 728.5   -2.4      #6  383.8 -> 385.3  +1.5
#16 980.7 -> 980.0   -0.6      (18 tests unchanged)
```

**Test #3 alone is the whole regression; the other 21 net +3.9** — almost exactly what the small-`R`
subset predicted. So the subset was right about the bulk and blind to one test.

#3 sits at `500.6` = `tpComp ≈ 0.001` with the waiting component at exactly `1.0`. Throughput there
is simply **not winnable** (tp is pinned at `tp_base`), so the small TPOT slip the swap costs buys
nothing and breaks a perfect waiting score. There is **no local analogue**: the only local tests with
`tpComp < 0.05` are the `R = 1` ones, where the swap is a no-op because `qDPre` and `qDPost` can
never both be non-empty.

**The fix is to read the score function.** `tp` is tokens over elapsed and both `tpBase` and `tpUB`
are given at startup, so the realised share of the `(tpBase, tpUB)` span is observable online and
converges from below during ramp-up — the safe direction. Only trade waiting for throughput once
throughput is visibly being won. Validated causally: on `h_6_13`/`h_6_14`/`h_5_13`, multiplying
`tp_UB` by 300 and changing nothing else flips the gate off and reproduces the no-swap schedule
byte-for-byte, while the original `tp_UB` keeps the full gain (h_6_14 elapsed 19 290 → 16 520 ms).

## 9. `pickRemote` conflated KV placement with decode-wave width

`if (load[j] == 0 && active >= dStar) continue;` used `dStar` — how many remotes one decode **wave**
should span — as a hard cap on **placement**. The two are different: a wave spanning `d` remotes
emits `d` uplink transfers and so costs `d·lat` per round, which is why the rate model correctly
returns 1 on high-latency instances; but a wave whose members all live on remote `j` still emits
exactly one transfer, so refusing an idle remote buys nothing. Left coupled, every request landed on
remote 0:

```
h_4_15  K=5  rem=0.945,0,0,0,0        elapsed 141 200 ms  score 570.1   (work floor 26 600 ms)
h_4_16  K=3  rem=0.945,0,0            elapsed  85 210 ms  score 637.2
```

Four remotes idle for an entire 141-second run. Opening a remote once the best open one's committed
prefill exceeds a decode round trip gives **+229.2 on h_4_15 and +229.9 on h_4_16**.

Two things had to be right:

* **Gate it on the instance being short of remote time, not link time.** Ungated it is net −364 on
  the 116 (`h_1_13 −219`, `g_2_2 −129`, `h_8_12 −52`, all with `up ≥ 0.98` and remotes under 0.12).
* **Use arrival-time totals, not the `busy*` accumulators.** `busyR`/`busyUp` also count decode
  traffic, whose volume is itself a function of how wide we spread, so gating on them makes the rule
  chase its own tail — spread, link looks busy, stop spreading, re-concentrate — and it never fires.
  `arrPProc`/`arrPXfer` are totalled from `Lin` at arrival and are placement-independent.

`CF_OPEN` is flat from 8 to 64 (843.17–843.53); at the default 16 the change touches **zero**
small-`R` tests, so it is as close to risk-free on the judge as a change gets.

## 10. Also measured this round, and rejected

| idea | result |
|---|---|
| Dynamic "promote D PRE when a downstream resource would idle" | flat across its whole threshold range (0→0.9 all give the identical 623.102, ≥1.0 gives 619.9). The one-step gain/loss model never separates the good cases from the bad, so it is pure extra code over the static swap. |
| Decode-first on remotes (never let prefill preempt a ready D PROC) | −0.41 on the subset. The "393.7 ms of D PROC waiting behind P PROC on g_5_2" is queueing that happens anyway — deferring prefill does not remove it, and `elapsed` actually rises 2344 → 2360. |
| Window-sized prefill pieces (cut a P PROC to fit before the next D PROC) | −0.11 on the subset; with decode-first, −0.55. Does not rescue the splitting idea. |
| `CF_ORDW` 0.75 → 0.5 | +2.30 on the 116 and **−0.05 on the small-`R` subset**. Also a generator artefact: `gen.cpp:89` draws `w_tp` from `{0,0.25,0.75,1.0}` or 0.5, so no local test has `w_tp ∈ (0.25, 0.5)` and thresholds anywhere in that gap are byte-identical. Not a plateau. |
