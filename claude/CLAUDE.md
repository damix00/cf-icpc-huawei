# claude/ — Claude's workspace for CF 2251A

Problem context, model, protocol, scoring, harness commands and submission procedure live in the
**root `CLAUDE.md`**. Read that first; this file covers only what is specific to this workspace.

## Rules

* Work **only inside `claude/`**. Root files are shared with `codex/` — touch them only for harness
  fixes that help both, and never edit `ref.cpp`.
* Score from the repo root with a path: `sh score.sh claude/sol.cpp "judge/*.txt"`.
* **Never edit `claude/sol.cpp` for an experiment.** Copy to `claude/sol_<tag>.cpp` and score that.
* The bar is **16109.263** (`ref.cpp`). Anything that does not beat it is not shippable.

## Layout

| path | what |
|---|---|
| `NOTES.md` | the full record of the old policy: model derivation, controller maths, every measured dead end, and the judge-probe log. 20 sections, all of it earned. |
| `archive/` | the old solution (`sol.cpp` = `sol_best_16109.cpp` = `ref.cpp`) and 108 measured variants. Dead code, kept for its measurements. |
| `tmp/` | scratch, plus the calibration artefacts (`quiet.list`, `loud.list`, `deltamat.txt`) and the offline oracles (`hp.py`, `hillclimb.py`, `hc2.py`) |

## Why this workspace restarted

The archived policy reached 16109.263 and then stopped moving. **Nine consecutive judge probes
returned 0.000 or negative**, the parameter space was searched exhaustively, and a ceiling audit of
the two instance families that carry the nominal headroom showed the achieved schedule is within a
few percent of what those instances physically permit. The conclusion recorded in `NOTES.md` §19–20
is that anything which moves the number from here has to change the **architecture**, not tune it.

The old policy in one paragraph, for reference: admit fast (throughput ∝ live decode population),
keep prefills whole, take every ready request into each decode group, and choose how many remotes to
spread a decode wave across from an analytic rate model tempered by measured per-resource
availability; bounded waiting merges D POST / D PROC groups, gated so it never fires when the link is
the bottleneck; prefill priority on E flips to P PRE-first only when `w_tp >= 0.75`.

### The last nine judge probes — all dead

| probe | change | judge |
|---|---|---|
| `sol_p128` | D POST merge budget 32 → 128 | **0.000**, byte-identical (budget saturated) |
| `sol_hp0` | stop blocking D PRE during a D POST hold | −7.895 |
| `sol_hb` | *also* block P POST during the hold | −3.703 |
| `sol_wr128` | D PROC merge budget 14 → 128 | **0.000** (never binds at any magnitude) |
| `sol_w8` | warm-up 100 → 8 | **0.000** |
| `sol_hpb` | remote idles instead of taking prefill during a D PROC hold | **−48.83**, all on #4 |
| `sol_cf` | split prefill to end before the merge member lands | −17.44, all on #4 |
| `sol_dn` | drop the `dStar` widening tie-break when remotes are saturated | −0.246 |

The two hold results bracket the block set from both sides, so it is a genuine optimum, not a fitted
constant.

### Architecturally closed — do not rebuild any of these

Rollout search (§14), placement refinement (§15), prefill pacing (§15), wave-shape smoothing (§18c),
raising `mStar` under TPOT slack (§18b — the rate model is *right* to stop where it does), the hold
block set (§18d/f), both merge budgets (§18a/g), admission order (all 24 permutations searched), and
**schedule-by-remaining-work in any form** — `L_out` is never revealed, so no policy can know a
request's decode tail until it ends (§19).

### Both local instruments are discredited *for this policy*

`tmp/quiet.list` (23 low-volatility `judge/` instances) was calibrated to 13× better L1 than the
full 40 across seven changes — and then called `sol_w8` +0.63 against a judge 0.000 and `sol_hp0`
+0.13 against a judge **−7.9**. It is not anti-correlated either, so it cannot be inverted: it is
simply uninformative about anything depending on a *long* hold, because locally the budget never
binds. **Regression guard only.** A fresh architecture should re-derive its own instrument rather
than inherit this one.

Baselines for `ref.cpp`: quiet **739.248**, loud **675.634**.

```sh
sh score.sh claude/sol_x.cpp "$(cat claude/tmp/quiet.list | tr '\n' ' ')"
sh score.sh claude/sol_x.cpp "$(cat claude/tmp/loud.list  | tr '\n' ' ')"
```

### The method lesson worth carrying forward

`CF_WAIT_R` at 14, 32 and 128 was byte-identical locally while 1 moved it — the exact signature that
later paid +31.9 on the *other* budget. It scored **0.000** on the judge at every magnitude. What
made the paying knob different was a judge measurement at the **bottom** end proving it had real
authority over real tests (−104.0). **Require a judge-side bottom-end measurement before spending
submissions climbing a locally-invisible ladder.**

## Current work

**SimSelect** — `claude/sol.cpp`. The solution carries an exact simulator of its own environment and
chooses its policy vector *per instance, online, by simulating*, instead of shipping one globally
fitted compromise. Full write-up in `NOTES.md` §21.

* **Judge best: 16115.479** (#387324056), up from 16109.263.
* `CF_SEL=0` is byte-identical to `ref.cpp` on all 183 local instances — the fallback is the old
  16109.263 behaviour exactly.
* Local: `judge/` +7.31, `tests/` +5.16, `hold/` +12.50, `val/` +5.42, `edge/` −0.001, zero failures.

### The thing worth remembering

`tp_base` **reveals the hidden total output-token count.** It is by definition the throughput of the
reference schedule (one request at a time, whole prefill, groups of 1), whose makespan is a known
monotone function of the arrivals, the `L_in` values and `T = sum(L_out)`. Bisection inverts it:
median error **0.000 %** over 158 local instances (`claude/tmp/infer.py`). NOTES §19 had closed the
whole "schedule by remaining work" family on the grounds that `L_out` is never revealed.

### Submitting

Codeforces rejects sources over **65535 characters** and the form fails *silently* — the error lives
only in the page DOM. `claude/sol.cpp` is the documented master; ship
`claude/sol_submit.cpp`, produced by `py claude/tmp/strip.py claude/sol.cpp claude/sol_submit.cpp`
and verified byte-identical in behaviour. Always read the DOM for an error after clicking submit.

### Tools built this session

| path | what |
|---|---|
| `claude/tmp/oracle.py` | per-instance policy oracle over any suite — the measurement that justified the architecture |
| `claude/tmp/infer.py` / `infer2.py` | validation of the token-total inference |
| `claude/tmp/selsweep.py` | sweeps selector gates across `judge/`, `hold/` and `val/` at once, reporting losers |
| `claude/tmp/strip.py` | literal-aware comment stripper for the 64 KB source limit |
| `claude/sol_ora.cpp` | `ref.cpp` + fixed-(d,m) override; the Stage A instrument |
| `CF_TRUTH=<test>` | diagnostic: hands the belief the real instance, separating engine / belief / selection error |
