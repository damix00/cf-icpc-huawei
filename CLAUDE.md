# CF 2251A — Edge–Cloud Collaborative Scheduling

Codeforces **2251A**, ICPC 2026 Online Challenge 1 (Huawei). Statement:
<https://codeforces.com/contest/2251/problem/A>. **Interactive**, scored 0–1000 per test, 22 tests,
sum is the contest score. Account `damix`. Contest ends ~2026-08-28.

This file is the **problem context and shared harness**: the model, the protocol, the scoring
function, what the judge has revealed about its 22 tests, and how to build/score/submit. It is
solution-agnostic on purpose.

## Repo layout

| path | what |
|---|---|
| `CLAUDE.md` | this file — problem + harness, shared |
| `ref.cpp` | **frozen reference solution**, judge score **16109.263** (submission 387270011). Read-only. It is the baseline to beat and the schedule `sim.exe -calibrate` uses. |
| `sim.cpp` | local interactor + scorer; compiles a solution in-process via `-DSOL_SRC` |
| `gen.cpp`, `mk*.sh`, `edgegen.py` | test generators |
| `tests/ hold/ val/ edge/ judge/` | 183 local instances (see below) |
| `score.sh`, `score.ps1`, `cmp.sh`, `sweep.sh`, `sweep2.sh`, `run.sh` | scoring harness |
| `analyze.cpp` | per-instance ceiling estimator (**optimistic** — see caveats) |
| `pipecheck.py`, `protocheck.py` | real-pipe protocol checks (mandatory before submitting) |
| `claude/` | Claude's workspace. `claude/NOTES.md` is the full record of the 16109.263 policy and every measured dead end. |
| `codex/` | Codex's workspace |

**Each agent works only inside its own folder.** Root files are shared: change them only for
harness fixes that help both, never to encode one agent's policy. `ref.cpp` is never edited.

## The model

Resources, **each strictly serial**: the local computer **E**, **K** remotes `C0..C(K-1)`, the
**UP** link, the **DOWN** link. UP and DOWN are independent single-server **FIFOs** that run
concurrently — once work is released to a link its order is frozen, so *when you release* is a real
decision.

A request `i` arrives at `arr[i]` with `L_in[i]` input tokens and produces `L_out[i]` output tokens.
**`L_out` is never revealed** — you learn a request is done only when its final token emerges.

Six steps, in this order per request:

```
P PRE   (E)      -> UP  (L_in tokens) -> P PROC (remote, layer range) -> DOWN (L_in) -> P POST (E)
then, once per output token:
D PRE   (E)      -> UP  (1/token)     -> D PROC (remote)              -> DOWN        -> D POST (E)
```

* A task occupies its computer for `S + dur`: **every extra task costs a whole `S`**.
* `dur` comes from the per-column piecewise-linear table, keyed on `L_in` for the P steps and on
  **group size** for the D steps. Columns: `p_pre, p_proc, p_post, d_pre, d_proc, d_post`.
* `P PROC` may be **split by layer range** `[ls, le)`; pieces must be contiguous and each costs a
  fresh `S`. Prefill goes to exactly one remote (`P PRE` names it, and it is the request's KV home
  for its whole life).
* `D PRE` / `D POST` run on E and may **group any number of ready requests** (a "wave"); `D PROC`
  groups only requests homed on that remote. `D PRE` fans out one UP transfer per distinct remote.
* A transfer of `len` tokens costs `lat + u*len`, `u = 8*bytes_per_token/(bw*1e6)`.
* `P POST` finishing is what stops the request's TDR clock.

## The score

Computed in `sim.cpp:533-556`, and every constant is **given at startup**:

```
tp        = totalTokens / elapsed,   elapsed = lastTokenTime - earliestArrival
tpComp    = clamp((tp - tp_base) / (tp_UB - tp_base), 0, 1)
tdr       = mean over requests of (P POST finish - arrival)
tpot      = mean inter-token gap over all requests with L_out > 1
dist      = hypot( max(0,(tdr-SLO1)/SLO1), max(0,(tpot-SLO2)/SLO2) )
waitComp  = max(0, 1 - dist/dist_base)         (or 1 iff dist==0 when dist_base==0)
score     = 1000 * (w_tp*tpComp + w_c*waitComp)
```

`totalTokens` is fixed, so **throughput is purely `1/elapsed`**. `tp_base`, `tp_UB`, `SLO1`,
`SLO2`, `dist_base`, `w_tp`, `w_c` are all read from the startup block — the realised share of the
`(tp_base, tp_UB)` span is observable online and converges from below.

**Read the score function before trading one component for another.** Judge test #3 sits at
`tpComp ≈ 0.001` with waiting exactly `1.0`: throughput there is unwinnable, so anything that
spends waiting to buy throughput is pure loss.

## The protocol

Startup block:

```
K S lat bw bytes_per_token num_layers
SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c
N
<N rows>  batch_size p_pre p_proc p_post d_pre d_proc d_post     (a value < 0 means "absent")
```

Then, repeatedly, one **frame**: a timestamp, an event count, that many event lines, and you must
answer with an assignment count and that many assignment lines. `END` terminates.

Events: `ARR <rid> <Lin>`, `TDN <E|Cj> <spec…> <dur>`, `XDN <UP|DOWN> <remote> <bytes> <PRE|DEC> <cnt> <ids…>`,
`FIN <rid>`.

Assignments (**at most one per computer per frame**, at most `K+1` total):

```
E  P PRE  <remote> <rid>
Cj P PROC <ls> <le> <remote> <rid>
E  P POST <remote> <rid>
E  D PRE  -1 <m> <ids…>
Cj D PROC <remote> <m> <ids…>
E  D POST -1 <m> <ids…>
```

Assigning to a busy computer, to a request not in the matching ready state, a duplicate id, or a
non-contiguous `P PROC` piece is an immediate **failure** (`sim.cpp:166-252` enforces all of it).
Answering nothing is always legal — but if nothing is in flight either, the run is **stuck** and
scores zero.

### THE bug — never reintroduce it

```c
ilen = fread(ibuf, 1, sizeof(ibuf), stdin);   // WRONG on an interactive problem
```

`fread` **never returns a short read** — it loops on the underlying `read()` until the buffer is
full or EOF. The interactor sends one frame then waits, so this blocked forever after ~300 bytes →
`IDLENESS_LIMIT_EXCEEDED, 0 ms, 0 MB` on all 22 tests. Use a single low-level `read`/`_read`
(`SOL_READ` in `ref.cpp:956-1000` is the working adapter; copy it).

**Why it survived local testing:** `sim.cpp` does `#define LOCAL_SIM` + `#include SOL_SRC`, so
`main()`'s stdin/stdout adapter is compiled out and never exercised by any scoring run, and a file
redirect cannot reproduce it (files only short-read at EOF). **Anything touching `main()` must be
re-checked with `pipecheck.py` and `protocheck.py`.**

## Harness

Run everything **from the repo root**; pass the solution as a path.

```sh
sh score.sh claude/sol.cpp                              # default suite (tests/)
sh score.sh codex/sol.cpp "judge/*.txt"                 # any glob
STATS=1 sh score.sh claude/sol.cpp "tests/g_5_3.txt"    # utilisation, group sizes, timestamped idle gaps
sh cmp.sh ref claude_sol                                # per-test delta; tag = path minus .cpp, '/'->'_'
BIN=tmp/sim_claude_sol.exe TESTS="tests/g_*.txt" sh sweep2.sh < cfgs.txt   # env sweep
./analyze.exe <test> [tp]                               # per-instance ceiling (optimistic!)
```

Both agents share `tmp/`, but tags include the folder (`claude/sol.cpp` → `claude_sol`), so scores
and binaries never collide.

Suites are independently seeded (`gen.cpp` profiles 0–8), zero protocol failures anywhere:

| suite | n | what it is |
|---|---|---|
| `judge/` | 40 | **profile 9, matched to the judge envelope** (`R` 1–59, 21–18 363 frames). Its *instances* are right; its *mean* is not — 17 of the 40 swing by tens or hundreds of points under changes the judge barely notices (`j_64` alone swings −502.9 on one flag). |
| `tests/` | 30 | general |
| `hold/` | 54 | independent seeds, generalisation check |
| `val/` | 36 | independent seeds, generalisation check |
| `edge/` | 23 | adversarial corner cases; the sim must never print FAILED |

`ref.cpp` baselines, all measured 2026-08-16 with `sh score.sh ref.cpp "<suite>/*.txt"`, zero
failures everywhere:

| suite | n | mean | worst |
|---|---|---|---|
| `judge/` | 40 | **712.212** | 418.2 (`j_57`) |
| `tests/` | 28 | 831.008 | 181.5 (`g_6_3`) |
| `hold/` | 54 | 841.165 | 282.9 (`h_6_14`) |
| `val/` | 36 | 843.674 | 430.7 (`v_5_23`) |

### Local means do not predict the judge

This is the single most expensive lesson in the repo. A change worth **+17.8 mean over 118 local
tests lost −78.8 on the judge**, all of it in one test. The full `judge/` mean over-predicts by
~10× and has got the **sign** wrong twice. Rules that follow:

1. Never trust a local mean alone. Confirm on `judge/`, then on `hold/` *and* `val/`.
2. Check the **per-test** deltas (`cmp.sh`), not the mean — judge movement is concentrated in one
   or two tests almost every time.
3. `analyze.cpp`'s ceiling is **optimistic**: it ignores flow-shop structure and assumes a
   saturated link through the decode tail. `g_6_3` scores 181 and is at **99 %** of its true
   ceiling; `g_5_1` is ~97 %. Check a trace before chasing a reported gap.
4. Verify a winning constant sits on a **plateau, not a spike** — spikes are overfitting.

## What the judge has revealed about the 22 tests

Facts, each bought with a submission. They constrain any architecture:

* **Small and lightly loaded.** Every test runs in ≤421 ms against a 15 s limit (~35× unused), and
  SJF on the remote prefill queue changed the score by *exactly* nothing — that queue never holds
  more than one request.
* **Waiting-weighted.** Gating a P PRE-first rule on `w_tp >= 0.75` removed the losses on #9/#21/#22
  and kept the gain on #6 → **exactly one of the 22 tests has `w_tp >= 0.75`**.
* **Tests 1, 2, 3, 11** sit at `tp == tp_base` with waiting `1.0` → single-request, **500 is the
  maximum**. **#14** has never moved under 31 submissions and is almost certainly the same shape.
* **#7 is TPOT-critical**: −13.2 from deferring a first token, −14.6 from maximal merging. Never
  delay an already-ready token.
* **Single tests swing enormously** — one priority-order change moved #9 by −72.
* Where the real gap is: **#6 (385.3), #14 (415.3), #5 (487.3), #10 (683.3), #9 (736)**.

`ref.cpp` per-test (16109.263):
`500, 500, 500.6, 795.9, 487.3, 385.3, 907.5, 830.2, 736, 683.3, 500.2, 799.9, 730.6, 415.3, 716.6,
980.7, 888.2, 915.7, 912.1, 998.2, 970.6, 955.2`.

Rank 76 at 16109.263; rank-50 cutoff **16166.452**, rank 1 **16430.623**. Codeforces keeps the
**best** submission, so experiments are free — but **anything shipped must beat 16109.263 to
matter**.

## Verification battery — run ALL of it before submitting

```sh
S=claude/sol.cpp; D=$(dirname $S); TAG=$(echo $S | sed 's/\.cpp$//; s#/#_#g')

for std in c++17 c++20 c++23; do g++ -O2 -std=$std -o tmp/t.exe $S; done   # judge uses C++23
sh score.sh $S "edge/*.txt" | grep FAILED                                  # must be silent
g++ -O2 -std=gnu++17 -o $D/sol.exe $S
py protocheck.py $D/sol.exe                                                # 8/8
rm -rf tmp/pc && mkdir -p tmp/pc
for t in tests/*.txt; do ./tmp/sim_$TAG.exe "$t" -dump "tmp/pc/$(basename $t .txt)" >/dev/null; done
SOL=$D/sol.exe py pipecheck.py $(for t in tests/*.txt; do echo "tmp/pc/$(basename $t .txt)"; done)
```

`failures=0` is non-negotiable — a protocol violation, stuck state, or TLE is a **zero** on that
test, worth ~50 mean.

## Submitting

The Submit button **ignores synthetic clicks** (by coordinate and by ref). What works:

1. `navigate` to `https://codeforces.com/contest/2251/submit`
2. `find` the file input → `file_upload` with the absolute path to the `.cpp`
3. JS tool: `document.querySelector('input.submit[type=submit]').click()`
4. Wait ~20 s, reload `https://codeforces.com/contest/2251/my`

The judge accepts roughly **one submission per ~9 minutes**; extra attempts silently do nothing.
Per-test scores: fetch `/contest/2251/submission/<id>` and regex
`#(\d+):\s*(\w+)\s*\[(\d+)ms,\s*(\d+)MB\]:?\s*points\s*([\d.]+)`.

**Submissions are the scarce resource.** Have a reason to expect a gain before spending one, and
record the per-test result afterwards — the per-test fingerprint is how every fact above was
learned.
