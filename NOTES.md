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

## 11. The session that moved 15995.995 -> 16049.587 (rank 86 -> ~63)

### What worked: reorder the uplink FIFO during ramp-up

Both links are single-server FIFOs, so **the order of work on them is frozen the moment we release
it** -- and the release time is ours. `sol.cpp` dumped every P PRE onto the uplink as soon as E was
free, so the FIFO order was arrival order and the SJF pop in `popPPre` never had anything to sort:
E is idle on these instances, so `qPPre` almost never held more than one request.

Holding a P PRE while the uplink is still working through *another prefill* costs nothing -- the
transfer cannot start earlier either way -- and lets the shortest of a larger pool take the slot.
Mean TDR is a mean completion time on that FIFO and `L_in` spans 1..4096, so the difference between
claiming slots in arrival order and claiming them shortest-first is large.

Ungated it is a disaster (**-6.8 on `judge/`**, j_64 -504, j_47 -473): once requests are decoding,
spreading prefill releases across the run interleaves every decode round with a fresh
multi-thousand-token payload, and **TPOT explodes** -- 448 -> 5072 ms on j_47. TPOT is measured from
a request's *first* token, so a request that emits one token and then stalls scores far worse than
the same request started later and run through cleanly. TDR and TPOT genuinely pull opposite ways.

They separate by decode population: during ramp-up almost no TPOT window is open, so the reordering
is free. `activeDecode <= 1` captures it.

| gate (`CF_JITL`) | `judge/` | small-`R` | real judge |
|---|---|---|---|
| off | 677.6 | 624.3 | 15995.031 |
| `activeDecode == 0` | 689.8 | 625.8 | **16033.372** (+37.4) |
| `activeDecode <= 1` | 700.7 | 624.9 | **16049.587** (+16.2) |
| `<= 2` | 698.8 | 623.8 | (judge/ says worse; not shipped) |
| ungated | 662.9 | 604.2 | -- |

Both judge gains landed almost entirely on **test #4: 744.97 -> 782.5 -> 798.1**.

Two refinements shipped on top:
* **TPOT-slack gate** -- `excess_tpot = max(0,(tpot-SLO2)/SLO2)` is clamped at zero, so stretching a
  gap while `tpot < SLO2` costs *literally nothing*. Both quantities are known online, so the hold
  is also allowed whenever the realised gap still has room (`CF_TPOTM=0.75`). Free measurement
  rather than a fitted threshold. `judge/` +0.77, `hold/` +1.09, everything else flat.
* **`CF_JITS=3`** -- release margin ahead of the link freeing. Wins on all five suites.

### `judge/` over-predicts by ~10x and can get the sign wrong

| change | `judge/` (per test) | real judge (per test) |
|---|---|---|
| JIT prefill-down release (`jitProc`) | **+8.03** | **-0.04** (moved 1 of 22 tests) |
| P PRE reorder, `activeDecode == 0` | +20.2 | +1.70 |
| P PRE reorder, `activeDecode <= 1` | +10.9 | +0.74 |

Sign was right twice and wrong once. Treat `judge/` as a **direction finder, not a magnitude**, and
require a change to win on `judge/` *and* small-`R` *and* not lose the big suites before shipping.
`jitProc` (which lost) was the one change that failed that test -- it cost small-`R` -5.1.

### Offline hill climbing is the best idea generator here

`tmp/hillclimb.py` + `sol_hc.cpp` (`CF_HCLIST`, `CF_HCTRACE`) hill climbs over *which frames force
the local computer to idle*, each candidate being one full exact-simulator run. It found the P PRE
mechanism directly: every winning idle on j_52 was `skip=PPRE` with the uplink backed up, all of
them at `activeDecode <= 1`. Headroom found (v7 base): j_46 +41.6, j_56 +10.5, j_52 +8.0, j_34 +3.2,
j_67 +0.6, j_51 +0.3, j_38 0, j_40 0.

**Its wins are single-frame skips.** Every blanket rule built from them is worse than doing nothing.

### Also measured this round, and rejected

| idea | result |
|---|---|
| **Exact forward rollout** (`sol_v9_x.cpp`, `Roll::`) -- simulate "act now" vs "wait one event" forward under a no-hold base policy and compare | **+0.15 on `judge/`**, and every rate-based objective was worse (realised rate -2.1, time-to-N-tokens -16.2). Costs almost nothing in CPU (125 ms vs 117 ms). The one remaining idea in a different algorithmic class, and it does not pay as built. |
| D PRE hold (it has none of its own; only ever delayed via the D POST hold) | uniformly harmful: -5.8 on `judge/` at every budget that activates it, and it loses on the very test the hill climb says wants it (j_56 -22.3 vs +10.5) |
| Same, bounded to N consecutive frames instead of a time budget | -4.9 `judge/`, -5.2 small-`R` |
| Wider D PRE target (whole live population instead of `mStar`) | -4.3 to -7.1 |
| Delayed-SPT admission (`t >= arr + w*xfer`, Hoogeveen-Vestjens) | flat (+0.48 at w=0.1, negative beyond) |
| Downlink SPT swap (yield the slot when a shorter prefill can reach the link first) | +3.1 `judge/`, **-5.0 small-`R`** |
| Same, gated to ramp-up | fires never -- with the P PRE hold in place only one prefill is ever in flight, so the downlink inherits the uplink's order, already SPT |
| Anticipatory yield (idle E/a remote rather than block an imminent P POST / P PROC) | -0.4 `judge/` |
| `CF_DTOL` 0.04 -> 0.16 | +0.8 `judge/`, +2.1 small-`R`, **-5.5 `hold/`** |
| All 24 local-computer priority orders | best is `3012` (+0.62 `judge/`), but every P-PRE-first order is the family that cost **-78.8** on the real judge |
| Minimum wave width `mStar` floor | flat (+0.08 at any floor) -- `mStar` is not what limits group size, the hold budget is |
| Randomised policy perturbation, best of 250 | +2.7 to +5.1 per test, i.e. a lottery, not a policy |
| **WSPT value-density priority** -- replace the fixed local-computer order with "run the candidate whose delay costs the most score per millisecond", pricing each with the clamped derivatives of the real score (all of `w_tp`, `w_c`, SLO1, SLO2, `dist_base`, `tp_base`, `tp_UB` are known, and tdr/tpot/tp are measurable online) | **-6.7 on `judge/`**. The principled index rule loses to the hand-tuned string: the running estimates are biased early (mean TDR under-counts requests still in flight) and a myopic index cannot see the pipeline-feeding value of D PRE. |
| Forcing wider decode waves (`mStar` floor + a huge hold budget) | group sizes do not move at all (j_66 stays at 4.6). `mStar` is capped by the live decode population, and on the E-bound tests that population is what is scarce -- not the grouping. |
| Probing the judge to split each test's score into its two components (force `waiting = 0` by holding P POST past `SLO1*(1+dist_base)`) | **does not work**: on lightly loaded tests nothing else is ever in flight, so the liveness fallback must release P POST anyway. Saturates at 21 of 40 tests still non-zero while `tp` collapses too, so the components never separate. |

### Contest mechanics worth knowing

* The 22 preliminary tests are **feedback only**. The final ranking is the mean of **20 frozen
  final tests** (statement, "Contest aggregation").
* The dashboard's "Your points" tracks the **best** submission, not the last -- so a probe or a
  losing experiment costs nothing but wall clock, and **any ship must beat the best to matter**.
* The judge accepts ~1 submission per 9 minutes; earlier attempts are silently dropped with no
  error, so check `/my` before assuming a submission landed.
* Test #1 is `tests/ex1.txt`. Its 500.0000027586 is exactly the maximum: the chain is 45 ms and
  `tp_base` is 1/45 truncated to 9 decimals, so `tp_component = 5.5e-9`. Tests 1/2/3/11 are capped
  near 500 and the whole contest is decided on the other 18.

### The second mechanism: placement was scored with an incomplete cost

`pickRemote` ranked remotes by `pendProc[j]` -- prefill compute owed -- and nothing else. That
ignores two things the remote is already committed to and which delay our P PROC by exactly their
duration: the task it is **running** (`rFreeAt[j]`) and the decode groups already **queued** on it.
Adding them (`CF_RBW=1`, the principled weight since everything is in ms) was worth **+19.8 on the
judge**, all of it on test #5 (465.5 -> 487.3) -- a test that had not moved under any previous
policy change. `judge/` predicted +1.3, i.e. it **under**-predicted by 15x, the opposite of its
usual 10x over-prediction. Adding the per-round decode-load term (`CF_DECW=1`) gave a further +0.63.

Everything else tried in the same class lost or was flat: opening idle remotes past `dStar`
(-4.85, entirely test #6), a co-location bonus for remotes already decoding, breaking placement
ties by load or recency rather than lowest index (concentration is genuinely better), dropping the
queued-decode term on the grounds that prefill preempts decode anyway (flat), and scaling decode
interference by the prefill's transit time (monotonically worse).

Also settled: **prefill splitting cannot work in this policy.** Prefill unconditionally preempts
decode on a remote, and a split prefill's next piece re-enters the same queue -- so decode is still
blocked between pieces and splitting only ever adds a schedule cost per piece. Implementing the
interleaving the mechanic exists for (`altDecode`) and then splitting still loses monotonically
(`judge/` 711.2 -> 709.7 -> 704.3 -> 698.9 as pieces shrink). The extra S dominates.

### The judge measurement log for this session

Sixteen submissions, all measured (the best is kept, so a losing probe costs only wall clock).

| # | change | judge |
|---|---|---|
| 1 | JIT prefill-down release (`jitProc`) | **-1.0** |
| 2 | P PRE uplink reordering, `activeDecode == 0` | **+37.4** |
| 3 | ... `activeDecode <= 1` | **+16.2** |
| 4 | TPOT-slack gate | 0.00 |
| 5 | `CF_JITS=3` | +0.17 |
| 6 | TDR-worth-it guard | 0.00 |
| 7 | `CF_JITL=2` | **+2.28** |
| 8 | bottleneck-aware hold budget | 0.00 |
| 9 | placement counts committed remote work + arrival gate | **+19.8** |
| 10 | `pickRemote` dStar decoupling | **-4.85** |
| 11 | decode-load term in placement (`CF_DECW=1`) | **+0.63** |
| 12 | TPOT half of the swap gate | 0.00 |
| 13 | remove the arrival gate | **+0.045** |
| 14 | local order D POST first, gated on the weights | **-5.86** |
| 15 | ... gated on measured waiting slack instead | **-67** |
| 16 | hold behind any uplink traffic + let decode overtake prefill once TDR is met | **-18.7** |
| 17 | skip the P PRE hold when `w_c` is small (aimed at #6, whose `w_tp >= 0.75`) | -0.07 |

Twenty-one measurements in total. Test **#6 (385.3)** does not respond to the P PRE hold being
disabled, so its loss is not admission timing; **#14 (415.3) has never moved under any of the 21**.
Between them they hold ~1200 nominal points and neither is reachable by anything tried here.

Ten of sixteen measured exactly zero. The two that paid were both "the model ignores something
real": link FIFO order is fixed at release time and we were releasing blind, and the placement cost
omitted work the remote was already committed to.

**Four separate changes won on all five local suites at once and lost on the judge**: the D POST-first
order (-5.9), its measured-slack variant (-67), the `jitMode=0` + remote-preemption bundle (-18.7),
and the JIT prefill-down release (-1.0). The local suites are not merely uninformative for gate
changes -- they are anti-correlated. Only two things ever transferred: releasing work to a FIFO in
a better order, and making a cost function count work that is genuinely committed. Both are
*structural* corrections, not tuning. Treat any purely local-suite gain on a gate or ordering knob
as evidence of nothing.

**The local-computer order is a trap.** D POST first is the best of all 24 orders on *every* local
suite simultaneously (`judge/` +1.2, `tests/` +1.4, `hold/` +2.0, `val/` +2.9, `edge/` unchanged
once `dist_base == 0` and `w_tp == 0` instances are excluded) and it loses 5.9 on the judge, all of
it on test #21. Replacing the static weight gate with the measured waiting distance -- strictly
more information -- made it far worse still (-67). Do not revisit this without a judge measurement
in hand.

### Where it ended, and what is left

`sol.cpp` = `sol_v19.cpp`. Final local numbers: `judge/` 709.33, small-`R` 624.47, `tests/` 831.09,
`hold/` 841.16, `val/` 843.52, `edge/` 290.42, zero failures anywhere, protocheck 8/8, pipecheck
clean, builds under C++17/20/23.

The remaining gap to a cash-prize place is **not** closable by tuning: every knob is at a local
optimum on every suite, the exact rollout does not pay, and the judge's 22 tests are unresponsive
to everything except local-computer priority and prefill FIFO order. The two directions that still
look open:

1. **Judge-driven search.** The judge is the only calibrated instrument, it accepts ~1 submission
   per 9 minutes, the best submission is what counts, and the contest runs for days. A systematic
   one-knob-at-a-time sweep *on the judge* is slow but is the only measurement that has ever been
   trustworthy. Every local instrument mispredicts.
2. **A different architecture for makespan.** 133 points/test are lost to throughput on the big
   suites and 233 on `judge/`, but random search, hill climbing and rollout all agree the current
   schedule is within a few points of what this policy class can reach -- and `analyze.exe` says
   several tests are already at 97-99 % of their true ceiling, with `tp_UB` simply unreachable.
   Finding out how much of that 233 is real would need a genuine offline optimum for one small
   instance (branch and bound over the exact model), which nothing here has built.

## 12. The merge look-ahead's blind spot is real, and closing it is worth nothing

`nextDownAfterProc` / `nextUpAfterPre` answer "is another group member coming soon?" for the
D POST and D PROC holds. Both were extended once before -- from "a transfer is already queued" to
"a task is running that will queue one" -- but they still stopped one step short of the truth:
work that is **ready and will be dispatched in this very frame** is invisible to them, because
both holds are decided *before* the local-computer block and the remote loop run.

The blind spot lands on exactly the frame that should matter. When a decode UP transfer completes,
`qDProc[j]` becomes non-empty on an idle remote; `rBusy[j]` is still last frame's `0`, so
`nextDownAfterProc` returns `+inf`, the D POST hold concludes nothing is coming, and E spends a
whole `S` on a one-member D POST -- moments before the D PROC it should have waited for lands.

`sol_rdy.cpp` closes it on both sides (`CF_RDY` bit 0 = ready D PROC feeds the D POST hold, bit 1 =
ready D PRE feeds the D PROC hold), pricing the delivery properly: remote free time, the queued
P PROC piece that preempts decode, `S + dproc(n)`, then the downlink.

**Result: at the true break-even it never fires at all.**

| `CF_RDYW` (wait budget, in units of one merge saving) | `judge/` (40) | small-`R` (24) |
|---|---|---|
| baseline / `CF_RDY=0` | 710.728 | 624.361 |
| 1 (the actual break-even) | **710.728** | **624.361** |
| 2 | 710.734 | 624.361 |
| 4 | 710.660 | -- |
| 8 (the existing `waitPost` budget, i.e. the first cut) | 710.678 | 623.892 |

A merge saves exactly one schedule cost plus the task's fixed term (`mergeSaving`), so a wait longer
than that is a net loss on the resource doing the waiting. **Every wait the blind spot was hiding is
beyond that break-even** -- byte-identical schedules at `rdyW <= 2` on both suites. Reuse the old
`8x`/`4x` multipliers (which were fitted when the look-ahead could only see one stage back) and it
fires and loses: `-0.05` on `judge/`, `-0.47` on small-`R`, and per-test it is a coin flip
(`j_36 +12.7`, `j_46 -7.2`, `j_49 -4.6`).

So the omission is genuine but the existing code was accidentally right, and there is no plateau
anywhere in the knob -- the sign flips between 2 and 4 and flips back at 8. Not shipped; no judge
slot spent. `sol_rdy.cpp` is kept only as the record.

**What this rules out:** "look further ahead before merging" as a class. The two look-ahead stages
already in place cover every merge that pays; the third stage only reaches waits that cost more
than they return. Any future work on group formation has to change *what a merge is worth*, not
*how far ahead we can see one*.

## 13. `CF_WAIT_R` 4 -> 14: judge-measured at exactly 0.000, promoted anyway

The D PROC merge hold on a remote is budgeted at `waitProc` multiples of one merge saving. The
default `4.0` is an early fit, made before the link predictor could see across the remote stage.
With the full look-ahead in place that budget is what limits how many decode members a remote can
gather, and `4x` cuts waves short on instances where the uplink delivers members in a slow trickle.

Plateau check (rule 4) on `judge/`:

| `CF_WAIT_R` | 3 | 4 (default) | 5 | 6 | 7 | 8 | 10 | **12** | **14** | **16** | 24 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `judge/` | 710.90 | 710.73 | 710.72 | 711.05 | 711.05 | 711.06 | 711.06 | **712.98** | **712.98** | **712.98** | 712.77 |

12-16 is a genuine plateau, not a spike. Cross-suite at 14:

| suite | default | `=14` |
|---|---|---|
| `judge/` (40) | 710.728 | **712.976** (+2.25) |
| small-`R` (24) | 624.361 | 624.361 (byte-identical -- the hold never fires there) |
| `tests/` | 830.928 | 831.059 |
| `hold/` | 841.277 | 841.154 |
| `val/` | 843.581 | 843.670 |
| `edge/` | 290.419 | 290.419 |

Zero failures on every suite. The `judge/` gain is **2 tests and no losers at all**: `j_53`
698.1 -> 774.6 (+76.5) and `j_36` 866.7 -> 879.9 (+13.2). Two winners / zero losses is a much safer
shape than the trades that lost on the judge last round, and it is the pattern CLAUDE.md says to
prefer -- wins the direction finder, holds steady everywhere else.

Candidate is `sol_wr.cpp` (only the default changed). Full battery green: builds under
C++17/20/23, `edge/` silent, protocheck 8/8, pipecheck 28/28.

**Judge result: submission #387246342, 16072.540 -- delta exactly 0.000.** Not "0.0 after
rounding": all 22 per-test values are byte-identical to #387210572 out to 10 decimals
(`500.0000027586 ... 955.1963439523`). The second branch of the prediction above was the right one
-- none of the 22 tests sit in the `j_53` regime, and the merge hold's budget never binds
differently on any of them, the same way it never binds on the small-`R` slice.

Promoted into `sol.cpp` regardless, on the TDR-worth-it-guard precedent: it is free on the live
set, it is +2.25 on the direction finder from two winners and zero losers, it sits on a 12-16
plateau, and the final test set is frozen separately -- a change that can only help on unseen
instances and provably costs nothing on the seen ones is worth carrying.

**Read this as calibration, not as a loss.** `judge/` predicted +2.25 (~+5 scaled by its usual 10x
over-prediction) and delivered a bitwise no-op. Combined with the rejected-on-the-judge table in
CLAUDE.md, that is six consecutive knob changes measuring <= 0 on the real judge. The 22 judge
tests simply do not visit the states these knobs govern; no amount of local-suite agreement will
change that. Rollout (section "Next idea") is the only untried lever that changes *which states the
policy visits at all*, and is therefore the only one with a plausible path to the ~74 points now
separating us from rank 50.

Rejected combinations: `CF_SJFO=1` on top (+0.003, noise), `CF_WAIT_P=12` on top (-0.76).
`CF_JITL` 3/4 (-1.3/-3.5), `CF_TPOTM` 0.5/1.0, `CF_JITS` 2/5 all lose -- current defaults confirmed.

## 14. The oracle bound: the local-computer decision family is exhausted

Before rebuilding rollout, measure the thing rollout is trying to reach. `sol_hc3.cpp` is the
`sol_hc2.cpp` frame-override hook rebased onto the **current** `sol.cpp` (verified byte-identical
to plain `sol.cpp` on 9 judge tests with an empty override list), driven by `tmp/hc3.py`.

The hill climb is an **oracle with perfect hindsight**: it may override the local computer's choice
at any frame to any of {idle, D POST, P POST, D PRE, P PRE}, each candidate being one full exact-
simulator run, keeping whatever scores best. It is therefore an **upper bound on any per-frame
local-computer decision policy, rollout included** -- no online method can beat a searcher that
already knows the future.

Sampled (400 frames/pass, 2 passes), against the current policy:

| test | oracle on v7 base (NOTES 11) | oracle on current `sol.cpp` |
|---|---|---|
| j_46 | **+41.6** | +1.85 |
| j_56 | +10.5 | +1.42 |
| j_52 | +8.0 | +1.28 |
| j_34 | +3.2 | +2.62 |
| j_67 | +0.6 | +0.73 |
| j_51 | +0.3 | +0.71 |
| j_38 | 0 | **+9.91** |
| j_40 | 0 | +0.75 |
| j_49 | -- | +1.10 |

**The headroom rollout was meant to capture is gone** -- the P PRE just-in-time release harvested
it (j_46 +41.6 -> +1.85). Frame sampling understates the bound, so two tests were re-run with every
frame searched: j_38 +9.91 -> **+10.44**, j_46 +1.85 -> **+5.79**. Call the true exhaustive oracle
**+4 to +7 per test on `judge/`**.

At `judge/`'s measured ~10x over-prediction that is **~+0.4 to +0.7 per test on the real judge,
i.e. +9 to +15 across the 22** -- for a *perfect hindsight oracle*. A rollout gets some fraction of
an oracle: it must guess `L_out` and future arrivals, and it is CPU-budget limited. Against the
**74.2** points now separating us from rank 50, this family cannot pay, and that is a statement
about the family, not about the quality of any particular rollout implementation.

### The residual wins have no shared mechanism

`CF_HCLIST=... CF_HCTRACE=1` over the winning frames shows four unrelated shapes:

* `P PRE -> idle` with the uplink 215-482 ms backed up (j_34: 8 of 9 wins) -- this *is* the shipped
  JIT hold, blocked at these frames because `activeDecode` is 3..41 and `jitL = 2`. Ungating it is
  the family that cost **-78.8 on the real judge**; `CF_JITL` 3/4 measure -1.3/-3.5 on `judge/`.
* `P PRE -> D PRE` (j_67, j_51, j_56) -- a priority-order swap. All 24 orders were already swept:
  best is `3012` at +0.62 `judge/`, and every P-PRE-first order is the -78.8 family.
* `P POST -> idle` / `-> P PRE` on j_38, where `tdr` is 83.0 against `SLO1` 210.5 and the waiting
  component is already **1.0000** -- deprioritising P POST is free there because TDR's clock has
  slack to burn, and it buys makespan. This is the one shape not already refuted, and it is the
  same "never spend a component that is already maxed out" principle as `swapMin`/`tdrGuard`.
  But WSPT value-density pricing -- the general form of exactly this -- measured **-6.7** on
  `judge/` (section 10).
* `idle -> D POST` with the downlink 0.17-0.27 ms from delivering (j_49, j_51, j_40) -- releasing
  the merge hold a fraction of a millisecond early. Pure micro-calibration.

Four mechanisms, 1-12 frames per test, each already swept or already refuted as a blanket rule.
This is the same verdict section 11 reached from the v7 oracle -- "its wins are single-frame skips"
-- now with the residual quantified.

### What this bound does NOT cover

The hook overrides **only the local computer's choice**. Untouched, and therefore unbounded by this
measurement: remote placement (`pickRemote`), decode wave width (`mStar`/`dStar`), the remote-side
D PROC holds, and admission. Placement is the one lever that ever transferred to the judge (+19.8,
section 9). If the search continues, point the same instrument at those families rather than
building rollout on the family just proved exhausted.

### 14a. The one unrefuted shape, tested and refuted

`sol_pd.cpp` (`CF_PDEM`, default 0 = off, byte-identical to `sol.cpp` when off) implements the j_38
shape as a rule: when the projected mean TDR is under `CF_PDEM * SLO1` **and** the realised TPOT is
under `CF_PDEM * SLO2` -- i.e. the waiting component is provably pinned at 1.0 -- move P POST to the
back of the local computer's preference order. The TDR projection is `tdrWorthIt()`'s, so it works
during ramp-up; the gate is self-limiting, because spending TDR eventually turns the gate off.

| `CF_PDEM` | 0 (off) | 0.25 | 0.4 | 0.5 | 0.6 | 0.75 | 0.9 | 1.0 |
|---|---|---|---|---|---|---|---|---|
| `judge/` | 712.976 | 712.976 | 712.969 | 712.960 | 712.985 | 713.012 | 712.925 | **713.024** |

Best is **+0.048**, non-monotonic -- noise, not a plateau. And at the best setting:

| suite | off | `=1.0` |
|---|---|---|
| `judge/` (40) | 712.976 | 713.024 (+0.05) |
| small-`R` (24) | 624.361 | **623.932** (-0.43) |
| `hold/` (54) | 841.154 | **840.964** (-0.19) |
| **j_38 alone** | 685.629 | **685.353** (-0.28) |

**The decisive number is the last row.** The gate demonstrably fires on j_38 (`tdr` moves 78.02 ->
79.45, so P POST really is being demoted) and the score goes **down**, against an oracle that scores
696.073 there. The oracle needed exactly 3 frames out of 1390; the rule fires on many and the net is
negative. This is section 11's verdict reproduced under controlled conditions: *the oracle's wins
are single-frame skips, and every blanket rule built from them is worse than doing nothing.*

Not shipped. `sol.cpp` is unchanged. Together with section 14 this closes the local-computer
decision family: the oracle bound says it is worth at most ~+9 to +15 on the real judge, and the
only residual shape that was not already refuted is now refuted too.

## 15. Where the links idle, and the deferral that was sitting switched off

### The instrument: per-link idle accounting

`sim.cpp` now records every interval a FIFO link sat with nothing queued (`-stats` prints the
totals, a split by what ended each gap, and the eight biggest gaps with their timestamps). It is
the first diagnostic here that says *where* the makespan goes rather than *how much*, and it made
the structure of the link-bound instances obvious in one pass:

```
j_57  up=0.724 down=0.724     DNp[0..1.703e6]=1.703e6   UPd[4.269e6..5.77e6]=1.501e6
j_50  up=0.857 down=0.857     DNp gaps 6.78e5 total     UPd[3.997e6..4.765e6]=7.68e5
j_44  up=0.765 down=0.765     DNp gaps 8.39e5 total     UPd[2.221e6..3.053e6]=8.32e5
```

Both links carry **identical** total work (`u*(sum Lin + sum Lout)` each way), so the shape is always
the same: the **downlink starves at the start**, waiting for the first prefill to be transferred up
*and* processed, and the **uplink starves at the end**, out of decode work. On `j_57`
`makespan = downStart + downWork` to within 0.2 %, i.e. the entire test is decided by when the
downlink gets its first byte.

That number is not reachable. `j_57`'s first arrival is at t=4977 with `L_in = 2600` and nothing
else in the system, so `downStart` is one 1.70e6 ms uplink transfer, and the only way to shorten it
is to hold that P PRE until a shorter request arrives. **Every hold must be gated on something
being in flight** or the interactor never sends another frame and the test scores 0 — and at the
first arrival nothing is in flight and `R` is never revealed, so tests with `R = 1` (four of the
judge's 22 sit at ~500 exactly) would die. Dead as a class, for a good reason.

### Two families measured and closed

* **Prefill pacing.** If the uplink FIFO is clogged with prefill payloads, decode tokens queue
  behind them -- so pace the prefill releases and let decode interleave. Measured directly by
  ungating the existing hold: `CF_JITL=1000` moves throughput by **±0.005** on every test and
  destroys waiting (`j_47` -576.8, `j_44` -204.6, `j_50` -194.0). The FIFO order of prefill against
  decode is not what limits throughput. `judge/` 712.976 -> 683.031.
* **Placement, again.** `dStar` is a decode-*wave* number being used as a hard veto on *placement*
  (`load[j] == 0 && active >= dStar`). Pricing the veto instead -- open a fresh remote when the best
  open one's committed work exceeds one decode round trip (`CF_OPEN`, `sol_op.cpp`) -- is flat:
  8..128 spans 713.10 down to 712.98 and only `j_61` moves. Making the placement cost count the
  decode load a remote is *committed* to rather than the load it carries *now* (`CF_DECL=1`,
  `sol_pc.cpp`) is +0.04; scaling it by the observed mean `L_out` (`CF_DECR`) is -0.4. The
  exhaustive placement oracle (`sol_hp.cpp` + `tmp/hp.py`, every request x every remote, hill
  climbed to convergence) finds +0 to +15 per test, but the `j_48` trace shows its wins are mostly
  *relabelling* -- `was=0 now=1` with both remotes empty -- which changes the order transfers enter
  the uplink, not the balance. Placement is at a local optimum.

### What actually paid: `deferFirst`, which was implemented and defaulted to off

TPOT is `(last token - first token)/(L_out - 1)`: **the clock starts at a request's FIRST token**,
and TDR has already stopped at P POST. The interval between them is measured by neither component.
So a request that produces one token and then stalls behind a multi-thousand-token prefill payload
is charged for the stall, while the same request started after the payload is not -- same stall,
same schedule, one of them inside the measured window.

`deferFirst` implements exactly this and had been sitting at `CF_DEFER=0` since it was written.
Turning it on is worth **+3.83 on `judge/`** (11 winners, 4 losers) -- the largest single-knob move
found this session, roughly 40x anything else swept. Three gates, each a statement about the score
function rather than a constant, turn it into a clean sweep:

| | `judge/` | small-`R` | `tests/` | `hold/` | `val/` | `edge/` |
|---|---|---|---|---|---|---|
| off (shipped 16072.540) | 712.976 | 624.361 | 831.059 | 841.154 | 843.670 | 290.419 |
| `CF_DEFER=2` raw | 716.804 | **619.251** | 832.612 | 841.175 | 845.930 | 290.416 |
| + `w_c > 0` | 716.804 | 622.946 | | | | |
| + bounded hold (`CF_DEFCAP=8`) | 715.809 | 626.490 | 830.328 | 841.237 | 844.391 | 290.419 |
| + `dist_base` gradient (`CF_DEFDB=1`) | **715.809** | **626.490** | **832.703** | **841.237** | **844.391** | 290.419 |
| | **+2.83** | **+2.13** | **+1.64** | **+0.08** | **+0.72** | **0** |

* **`w_c > 0`** -- the deferral buys waiting and pays in makespan, so it is pure loss where waiting
  has no weight. `h_5_13` (`w_c = 0`) 571.3 -> 482.6 without it.
* **A bounded hold** (`deferCap` SLO2s past P POST) -- a deferral is worth at most the TPOT damage
  it avoids, which is bounded, so the wait must be too. Uncapped, a request whose congestion never
  clears is simply postponed for the whole run: on `j_57` that made **TPOT itself worse** (4484 ->
  5107 ms) while costing 2 % of the makespan. Capping turns `j_57` -10.7 into -0.0 and `g_5_1`
  **-34.5 into +50.5**. `1..24` is one flat plateau on all five suites; `deferSlo` 0.8..2.0 likewise.
* **A gradient to buy** -- with `dist_base == 0` the waiting component is not continuous. It is
  exactly 1.0 while `tdr <= SLO1` and `tpot <= SLO2`, and 0 otherwise, so shaving TPOT below a limit
  we are already under buys nothing. `ex2`'s requests all have `L_out = 1`, which makes TPOT 0 by
  construction: 1000.0 -> 933.5 without the gate.

Per test on `judge/`: `j_47` **+44.7**, `j_44` **+25.8**, `j_69` **+22.2**, `j_31` +7.5, `j_53` +6.6,
`j_34` +2.9, `j_50` +2.6, `j_37` +1.5, `j_45` +0.1, against `j_52` -0.5 and `j_60` -0.2. Nine
winners, two losers under a point. Every winner is link-bound (`up` 0.72-0.89), which is the regime
the diagnostic above says the judge's weak tests live in.

This is the first change since the P PRE just-in-time release to win on `judge/` **and** the
small-`R` slice **and** all three big suites simultaneously, and like both of the changes that ever
transferred it is a correction to something the model ignored -- here, that the two waiting metrics
leave the interval between P POST and the first token completely unmeasured.

### The judge measurement: -23.06, and exactly which tests moved

Submitted as **#387263252**: **16049.485**, i.e. **-23.055**. Per test against 16072.540:

| test | before | after | delta |
|---|---|---|---|
| #7 | 907.5 | 894.34 | **-13.16** |
| #13 | 730.6 | 720.32 | **-10.28** |
| #17 | 888.2 | 888.69 | **+0.49** |
| the other 19 | | | **0.000** |

Reverted; `sol.cpp` is byte-identical to the 16072.540 source again (`sol_prev_16072.cpp`), and the
variant is kept as `sol_defer.cpp`.

**The informative part is the 19 zeroes.** The gate needs a prefill still unfinished *and* a decode
round trip that would exceed SLO2, and on 19 of the judge's 22 tests those never co-occur. On
`judge/` the same gate fires on 15 of 40. That is a statement about the two suites, not about the
rule -- and it is the whole reason the transfer failed.

## 16. Why six changes in a row won locally and lost on the judge

Every judge-measured change is also reproducible locally as a knob flip, so the two instruments can
be compared **per change** rather than by reputation. Seven of them, as per-test deltas:

| change | `judge/` (40) | real judge (22) |
|---|---|---|
| disable the JIT prefill-down (`CF_JITR=0`) | **-2.835** | +0.045 |
| `CF_JITL=1` | -1.112 | -0.104 |
| drop the TDR-worth-it guard (`CF_TDRG=0`) | **-13.632** | 0.000 |
| drop the decode-load placement term (`CF_DECW=0`) | -0.743 | -0.029 |
| local order D POST first | +0.289 | -0.266 |
| the deferral above | **+3.828** | **-1.048** |
| `CF_JITS=1` | -0.092 | -0.008 |

The judge's tests never move more than about a point per test under **any** of these. `judge/`'s do,
by up to 13.6 -- and the per-test matrix (`tmp/deltamat.txt`) shows why: the mean is carried by a
handful of hyper-volatile instances. `j_64` alone swings **-502.9** on `CF_TDRG=0`, `j_37` -102.6 on
`CF_JITL=1`, `j_53` -84.2 on `CF_JITR=0`, `j_47` +60.9 on the deferral. **The real 22 contain nothing
like them**: the largest single-test judge move ever recorded across 23 submissions is +21.8 (test #5,
placement) and the largest loss -13.2 (test #7, this session).

### The fix: score on the quiet subset

Split `judge/` by volatility -- a test is **quiet** if no change in the battery above moves it by more
than 3 points, **loud** otherwise. `tmp/quiet.list` (23 tests) and `tmp/loud.list` (17 tests). The
quiet subset reproduces the judge's response profile **13x more accurately**:

| instrument | JITR0 | JITL1 | TDRG0 | DECW0 | DPOST1st | DEFER | JITS1 | **L1 error** |
|---|---|---|---|---|---|---|---|---|
| real judge | +0.045 | -0.104 | 0.000 | -0.029 | -0.266 | -1.048 | -0.008 | -- |
| `judge/` all 40 | -2.835 | -1.112 | -13.632 | -0.743 | +0.289 | +3.828 | -0.092 | **23.75** |
| quiet 23 | +0.027 | 0.000 | -0.007 | -0.015 | -0.019 | +0.241 | -0.108 | **1.78** |

Six of the seven match in sign and to within 0.25. Only the deferral still comes out with the wrong
sign (+0.24 against -1.05), and even there the error is 16x smaller. The selection rule is a single
volatility threshold applied uniformly -- not a per-change fit -- and it is mechanistically motivated
rather than fitted: the judge demonstrably has no volatile tests, so an instrument containing 17 of
them measures a population the judge does not draw from.

**Use `tmp/quiet.list` as the primary local instrument from now on.** Keep `tmp/loud.list` only as a
"did this destroy something" guard.

### What the calibrated instrument then says

Every tunable in the policy, swept on the quiet subset (baseline 740.560):

```
CF_JITP=0 -19.017   CF_ORD_A=1320 -2.339   CF_ORD_A=3012 -1.554   CF_WAIT_P=16 -1.312
CF_SWAP=0.02 -1.288   CF_EBW=0.5 -0.887   CF_SWAP=0.2 +0.373   CF_WAIT_P=2 +0.230
CF_JITS=6 +0.159   CF_HOLDPRE=0 +0.128   CF_JITR=0 +0.027   ... everything else |d| < 0.02
```

Except for the shipped P PRE hold (worth -19 to remove, which is the +37.4 the judge already paid
for), **nothing moves the quiet population by more than 0.4 per test.** Prefill splitting is
monotonically worse (`CF_PIECE` 1..64: 735.2, 738.7, 739.4, 739.5, 739.8, 740.5, 740.6 = off);
decode-first on the remotes (`sol_dp.cpp`, `CF_DECF=1`) is -0.09; the priced remote-opening rule and
both placement-cost refinements are exactly **0.000** on it -- they only ever moved loud tests.

### Where the quiet set's headroom actually is

Against the (optimistic) work floor, holding the waiting component fixed:

| | headroom | per test |
|---|---|---|
| quiet 23 | +654.8 | **+28.5** |
| loud 17 | +1498.1 | +88.1 |

concentrated in `j_48` +163, `j_67` +128, `j_68` +111, `j_32` +80, `j_59` +51, `j_46` +44. The new
per-computer idle accounting (`-stats` now prints `server idle:` and `idle ended by:`) says what
those tests are doing:

```
j_67  K=1  E idle 68% [Ppost 21%, Dpost 47%]   C0 idle 37% [Dproc]
j_68  K=1  E idle 64% [Ppost 36%, Dpost 34%]   C0 idle 38% [Dproc]
      biggest gaps  UPd[1945..2487]  DNp[1909..2443]   UPd[2490..3058]  DNp[2449..3012]
```

Both links, the remote and the local computer idle **at the same time**, in repeated 400-600 ms
blocks -- the signature of a latency-bound closed loop with too few live requests, not of a
contended resource. That is why no priority, hold or placement knob touches it: there is nothing to
re-order. The work floor is simply unreachable on these instances, exactly as `analyze.cpp`'s
caveat in section 7 says.

## 17. The quiet subset is a better *explanation* than it is a *predictor*

Section 16's calibration was fit on seven judge-measured changes. The eighth was an out-of-sample
test of it, and **it failed**.

`CF_WAIT_P` (the D POST merge-hold budget, in units of one merge saving) was moved 8.0 -> 1.0.
The argument was principled -- waiting longer than the merge saves is a loss by construction, and
`eBottleW` already imposed exactly that cap whenever the local computer was the bottleneck -- and
every local instrument agreed: quiet **+0.23/test**, loud +0.72, `judge/` all-40 +0.44, `hold/`
+0.48, small-`R` -0.09, `edge/` unchanged, and 1..2 was a plateau with 8..10 falling away.

Submitted as **#387266525**: **15968.542**, i.e. **-104.0**.

| test | 16072.540 | `CF_WAIT_P=1` | delta |
|---|---|---|---|
| #5 | 487.3 | 416.5 | **-70.8** |
| #8 | 830.2 | 810.7 | -19.5 |
| #4 | 795.9 | 781.9 | -14.0 |
| #16 | 980.7 | 972.3 | -8.4 |
| #6 | 385.3 | 391.1 | +5.8 |
| #13 | 730.6 | 733.6 | +3.0 |

Reverted; `sol.cpp` is byte-identical to the 16072.540 source again. Two conclusions:

1. **The quiet subset explains the historical variance but does not predict a new change.** It is a
   real improvement -- L1 error 1.78 against 23.75 on the battery it was built from -- and it is
   still the best local statistic available, but it is a *post-hoc* fit and this is what an
   out-of-sample test of it looks like. Do not treat a ±0.5/test quiet-set movement as evidence of
   anything; that is inside its own residual error.
2. **-70.8 on test #5 is the largest single-test move ever recorded here**, and it is a judge
   measurement of a *derivative*: d(score #5)/d(waitPost) is strongly positive across 1 -> 8. Test #5
   is the same test that gained +19.8 from the placement-cost fix. It is by a wide margin the most
   policy-sensitive of the judge's 22, and the D POST merge-hold budget is the biggest lever on it
   yet found. `CF_WAIT_P` has never been probed *upward* on the judge -- every local suite calls 16
   worse (quiet 739.25, `judge/` 712.22), which given an 8-for-8 record of local suites being wrong
   or anti-correlated on this knob is close to an argument for trying it.

### 17a. Walking the gradient upward: `CF_WAIT_P` 8 -> 16 is **+4.864**

Submitted as **#387266889**: **16077.404**, a new best. Exactly one test moved:

| test | 16072.540 | `CF_WAIT_P=16` | delta |
|---|---|---|---|
| #19 | 875.4 | **880.2** | **+4.86** |
| the other 21 | | | **0.000** |

So the same constant that costs 104 points when halved to 1 pays 4.9 when doubled to 16, and every
local instrument had the sign backwards **in both directions**. `judge/` all-40 reads 712.976 (at 8),
712.221 (16), 712.212 (32); the quiet subset reads 740.560, 739.248, 739.248. On this axis the local
suites are not noisy, they are **anti-correlated**, and that is now measured twice rather than
inferred.

`sol.cpp` is #387266889; `sol_best_16077.cpp` is a copy. Above 32 the budget stops binding on every
local suite (16 and 32 and 64 are all byte-identical on `judge/`), so the axis runs out somewhere
around there. Remaining probes on the same "more waiting" axis, all verified failure-free locally
and ready to submit: `CF_WAIT_P=32`, `CF_HOLDCAP` 4 -> 8/16 (the cap on total hold time, which
scales with the same budget), `CF_EBW` 1 -> 4 or unbounded (the ceiling that clamps the budget to a
single merge saving whenever the local computer is the bottleneck), `CF_WAIT_R` 14 -> 32.

### 17b. The budget is no longer the binding constraint

`sol_ebw.cpp` releases `eBottleW` (the clamp that pins the merge-hold budget to a single merge
saving whenever the local computer is the bottleneck) from 1.0 to 16.0, i.e. to `waitPost`, making
it inert. Submitted as **#387268195**: **16077.404 — exactly the current best, unchanged.** The
clamp never binds on any of the judge's 22 tests. Not promoted (it costs `hold/` -1.0 and `val/`
-0.5 locally for nothing measurable, and the final test set is frozen, so the special case stays as
insurance).

Together with `waitPost` 16 and 32 being byte-identical on every local suite, that says the *budget*
side of the merge hold is now slack. What still caps a decode group is **`mStar`**: the hold only
fires `while ((int)qDPost.size() < mStar`. So the remaining probes on this axis are the ones that
raise `mStar` or `holdCap`, not the ones that lengthen the budget:

* `sol_mall.cpp` (`CF_MALL=1`) -- `mStar = max(mStar, L)`, i.e. always target the whole live decode
  population instead of the rate model's choice. The model's `L/C` term drives `mStar` down because
  it assumes waves pipeline perfectly; when a single remote serialises D PROC they do not.
  Locally `judge/` 711.58 vs 712.22, quiet 737.81 vs 739.25 -- both *negative*, which on this axis
  has twice now meant the opposite on the judge.
* `holdCap` 4 -> 16, the cap on total hold time.
* `sol_tb.cpp` (`CF_TPOTB`) -- budget the holds by the *measured TPOT slack* (`SLO2 - tpotNow()`)
  rather than by a multiple of the merge saving. This is the structural form of what the gradient is
  paying for: the hold's real cost is the TPOT it spends, the score only charges for the part above
  SLO2, and both terms are known online.

### 17c. `mStar = L` is -15.2, and all of it is test #7

`sol_mall.cpp` (`mStar = max(mStar, L)`, i.e. target the whole live decode population) submitted as
**#387268716**: **16062.24**, -15.16 from the best.

| test | best | `mall` | delta |
|---|---|---|---|
| #7 | 907.5 | 892.9 | **-14.6** |
| #6 | 385.3 | 384.8 | -0.5 |
| #5 | 487.3 | 487.4 | +0.1 |
| #20 | 998.2 | 998.1 | -0.1 |
| #19 | 880.2 | 880.2 | (keeps the `CF_WAIT_P=16` gain) |

**Test #7 is the constraint on this whole axis.** It is the same test that lost 13.2 to the
first-token deferral (NOTES 15). Both changes do the same thing to it -- delay a token that was
ready -- so #7 is TPOT-critical, while #19 rewards exactly the opposite. The rate model's `mStar`
already sits between them; raising it uniformly buys #19 nothing it has not already got from the
budget and hands #7 the whole loss.

That is precisely the conflict `sol_tb.cpp` is built to resolve: budget the merge holds by the
**measured TPOT slack** (`SLO2 - tpotNow()`) instead of by a multiple of the merge saving, so the
hold shrinks by itself on a test whose TPOT is tight and grows where it is loose. Both terms are
known online, and `excess_tpot` is clamped at zero, so the slack is free to spend and nothing past
it is.

### 17d. Session summary of the judge-probe campaign

| # | probe | judge | delta | where |
|---|---|---|---|---|
| 387263252 | first-token deferral, gated (`sol_defer.cpp`) | 16049.485 | **-23.06** | #7 -13.2, #13 -10.3, #17 +0.5 |
| 387266525 | `CF_WAIT_P` 8 -> 1 | 15968.542 | **-104.0** | #5 -70.8, #8 -19.5, #4 -14.0, #16 -8.4 |
| 387266889 | `CF_WAIT_P` 8 -> 16 | **16077.404** | **+4.86** | #19 +4.86, other 21 identical |
| 387268195 | `CF_EBW` 1 -> 16 | 16077.404 | 0.000 | nothing; the clamp never binds |
| 387268716 | `mStar = L` (`sol_mall.cpp`) | 16062.24 | **-15.16** | #7 -14.6 |

Five judge measurements in one session, against 21 in all previous sessions combined. What they
buy is a map of which of the 22 tests respond to what:

* **#5** -- placement (+19.8 historically) and, far more strongly, the D POST merge-hold *budget*
  (-70.8 when it is cut to the break-even). Saturated by `waitPost = 8`.
* **#7** -- punished by anything that delays a token that was already ready: -13.2 from the
  deferral, -14.6 from `mStar = L`. Indifferent to the budget itself (907.5 at `waitPost` 1, 8 and
  16 alike). TPOT-critical.
* **#19** -- the mirror image: +4.86 purely from a *longer* budget at unchanged `mStar`.
* **#13** -- -10.3 from the deferral, +3.0 from `waitPost = 1`. Sides with #7.
* **#14** -- still has never moved, now under 26 submissions.

So the axis has two opposing tests and the current setting sits between them. Uniform batching
cannot win: #19 and #7 want opposite things. The only way to have both is a rule that reads the
*per-instance* TPOT slack, which is what `sol_tb.cpp` (`CF_TPOTB`) does.

### 17e. The TPOT-slack budget is refuted before submission, by the judge data we already have

`sol_tb.cpp` replaces the fitted merge-hold budget with the measured TPOT slack
(`max(one merge saving, CF_TPOTB * (SLO2 - tpotNow()))`). It behaves exactly as designed on the
local proxies -- **+0.0 on every delay-sensitive test** (`j_34`, `j_36`, `j_40`, which `mStar = L`
cost 17.6 / 19.5 / 1.7) and **+16.5 / +4.2 / +1.1** on the budget-sensitive ones (`j_45`, `j_33`,
`j_53`). Right shape.

It still collapses `hold/` from **841.1 to 824.2**, and bounding it above (`min` with the existing
budget, so the slack can only ever tighten a hold) does not help -- 824.2 either way. So the damage
comes from the *shrinking* half: wherever the measured TPOT is high, the rule cuts the budget
towards the break-even.

**That is the same change as `CF_WAIT_P = 1`, applied selectively -- and that change is the one that
cost -104.0.** Worse, the half it was built for does not even apply: test #7 is indifferent to the
budget (907.5 at `waitPost` 1, 8 and 16 alike) and lost its 14.6 to `mStar`, not to the hold length.
The rule therefore pays the -104 mechanism to fix something it cannot reach.

**Not submitted.** The coherent reading of all five judge probes is simply: *keep the merge-hold
budget large, and leave `mStar` where the rate model puts it.* That is exactly `sol.cpp` at
16077.404. The only untried step in the direction the judge has actually rewarded is more budget:
`sol_p32.cpp` (`waitPost` 32) and beyond.
