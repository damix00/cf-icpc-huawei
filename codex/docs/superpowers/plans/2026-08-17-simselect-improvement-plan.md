# SimSelect Improvement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve Claude's SimSelect selector and produce a Codeforces submission scoring strictly above 16115.479.

**Architecture:** Preserve the cloneable scheduler, exact event engine, belief model, normal selector milestones, and interactive adapter. Add a guarded early round that switches only when a candidate has positive worst-case score delta across every active arrival projection and changes the imminent response; otherwise leave the proven normal selector untouched.

**Tech Stack:** C++17/20/23, PowerShell, Python 3, the repository's in-process simulator, protocol checker, and pipe replay harness.

**Spec:** `codex/docs/superpowers/specs/2026-08-17-simselect-improvement-design.md`

## Global Constraints

- Work only under `codex/`; treat `claude/` as a read-only baseline.
- Never modify `ref.cpp` or shared root harness files for policy behavior.
- Never edit a winning source in place for an experiment; create a tagged candidate.
- An incomplete selector round must never change the live policy.
- Keep the existing low-level interactive input adapter.
- Reject local spikes and discovery-only gains; compare per-test deltas on independent suites.
- Run the complete compile, edge, protocol, and real-pipe battery before submission.
- Keep the submitted source at or below 65535 characters.
- Success requires an accepted judge score strictly above 16115.479.

---

### Task 1: Reproduce SimSelect in the Codex workspace

**Files:**
- Create: `codex/sol_simselect_base.cpp`
- Create: `codex/tmp/simselect_baseline/` score artifacts

**Interfaces:**
- Consumes: `claude/sol.cpp`, `claude/tmp/strip.py`, and the shared suites.
- Produces: an immutable, submission-sized Codex baseline and per-suite score files used by every later comparison.

- [ ] **Step 1: Generate the baseline by a literal-aware mechanical rewrite**

Run:

```powershell
py claude/tmp/strip.py claude/sol.cpp codex/sol_simselect_base.cpp
```

Expected: the script reports `OK, limit 65535` and does not modify `claude/sol.cpp`.

- [ ] **Step 2: Compile the baseline under the judge language version**

Run:

```powershell
g++ -O2 -std=c++23 -o codex/sol_simselect_base.exe codex/sol_simselect_base.cpp
```

Expected: exit code 0.

- [ ] **Step 3: Score both sources on all policy suites and preserve each result**

Run:

```powershell
New-Item -ItemType Directory -Force codex/tmp/simselect_baseline | Out-Null
$suites = @{
    judge = 'judge/*.txt'
    tests = 'tests/*.txt'
    hold  = 'hold/*.txt'
    val   = 'val/*.txt'
    edge  = 'edge/*.txt'
}
foreach ($name in $suites.Keys) {
    ./score.ps1 claude/sol.cpp $suites[$name]
    Copy-Item tmp/scores_claude_sol.txt "codex/tmp/simselect_baseline/$name-claude.txt"
    ./score.ps1 codex/sol_simselect_base.cpp $suites[$name]
    Copy-Item tmp/scores_codex_sol_simselect_base.txt "codex/tmp/simselect_baseline/$name-codex.txt"
}
```

Expected: corresponding per-test score lines are identical within the scorer's printed precision and every suite reports `failures=0`.

- [ ] **Step 4: Verify source identity at the behavior boundary**

Run:

```powershell
foreach ($name in 'judge','tests','hold','val','edge') {
    $a = Get-Content "codex/tmp/simselect_baseline/$name-claude.txt" | Select-String 'score='
    $b = Get-Content "codex/tmp/simselect_baseline/$name-codex.txt" | Select-String 'score='
    $delta = Compare-Object $a $b
    if ($delta) { throw "baseline mismatch on $name`n$delta" }
}
```

Expected: no output after each same-suite run. Preserve copies of the five baseline score files under `codex/tmp/simselect_baseline/` before the next suite overwrites them.

- [ ] **Step 5: Commit the reproduced baseline**

```powershell
git add codex/sol_simselect_base.cpp
git commit -m "test: reproduce SimSelect baseline in codex"
```

---

### Task 2: Add testable robust-selection primitives

**Files:**
- Create: `codex/sol_simselect_urgent.cpp`
- Create: `codex/test_simselect_selector.cpp`

**Interfaces:**
- Consumes: `Response`, `Assign`, and candidate score rows from SimSelect.
- Produces: `Selector::sameResponse(const Response&, const Response&)` and `Selector::robustWinner(const vector<vector<double>>&, int, double)`.

- [ ] **Step 1: Generate a tagged implementation candidate**

Run:

```powershell
py claude/tmp/strip.py claude/sol.cpp codex/sol_simselect_urgent.cpp
```

- [ ] **Step 2: Write failing unit tests for response urgency and worst-case ranking**

Create `codex/test_simselect_selector.cpp` with:

```cpp
#define LOCAL_SIM
#include "sol_simselect_urgent.cpp"
#include <cassert>
#include <cstdio>

static Assign assignment(int server, int step, int remote,
                         std::initializer_list<int> ids) {
    Assign a;
    a.server = server;
    a.step = step;
    a.remote = remote;
    a.ids.assign(ids);
    return a;
}

int main() {
    Response base, same, changed;
    base.n = same.n = changed.n = 1;
    base.a[0] = assignment(-1, ST_PPRE, 0, {3});
    same.a[0] = assignment(-1, ST_PPRE, 0, {3});
    changed.a[0] = assignment(-1, ST_DPRE, -1, {3});
    assert(Selector::sameResponse(base, same));
    assert(!Selector::sameResponse(base, changed));

    std::vector<std::vector<double>> safe = {
        {100.0, 112.0, 109.0},
        {200.0, 207.0, 205.0},
    };
    assert(Selector::robustWinner(safe, 0, 5.0) == 1);

    std::vector<std::vector<double>> unsafe = {
        {100.0, 120.0},
        {200.0, 199.0},
    };
    assert(Selector::robustWinner(unsafe, 0, 0.0) == -1);
    std::puts("SimSelect selector primitive tests passed");
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run:

```powershell
g++ -O2 -std=c++23 -o codex/test_simselect_selector.exe codex/test_simselect_selector.cpp
```

Expected: compilation fails because `sameResponse` and `robustWinner` are not defined.

- [ ] **Step 4: Implement the minimal pure helpers inside `Selector`**

Add:

```cpp
static bool sameResponse(const Response& x, const Response& y) {
    if (x.n != y.n) return false;
    for (int i = 0; i < x.n; ++i) {
        const Assign& a = x.a[i];
        const Assign& b = y.a[i];
        if (a.server != b.server || a.step != b.step || a.remote != b.remote ||
            a.ls != b.ls || a.le != b.le || a.ids != b.ids) return false;
    }
    return true;
}

static int robustWinner(const vector<vector<double>>& score,
                        int base, double margin) {
    if (score.empty() || base < 0) return -1;
    const int nc = (int)score.front().size();
    if (base >= nc) return -1;
    int best = -1;
    double bestWorst = margin;
    for (int c = 0; c < nc; ++c) {
        if (c == base) continue;
        double worst = 1e300;
        for (const auto& row : score) {
            if ((int)row.size() != nc || !isfinite(row[c]) || !isfinite(row[base]))
                return -1;
            worst = min(worst, row[c] - row[base]);
        }
        if (worst > bestWorst + 1e-12) {
            bestWorst = worst;
            best = c;
        }
    }
    return best;
}
```

- [ ] **Step 5: Run the unit tests to verify they pass**

Run:

```powershell
g++ -O2 -std=c++23 -o codex/test_simselect_selector.exe codex/test_simselect_selector.cpp
./codex/test_simselect_selector.exe
```

Expected: `SimSelect selector primitive tests passed`.

- [ ] **Step 6: Commit the primitives and tests**

```powershell
git add codex/sol_simselect_urgent.cpp codex/test_simselect_selector.cpp
git commit -m "test: define robust SimSelect selection primitives"
```

---

### Task 3: Implement the guarded urgent round

**Files:**
- Modify: `codex/sol_simselect_urgent.cpp` in `Selector::init` and `Selector::maybeSelect`
- Modify: `codex/test_simselect_selector.cpp`

**Interfaces:**
- Consumes: the existing primary and secondary projections, complete candidate pricing, `robustWinner`, and cloned `Sched::decide` responses.
- Produces: environment controls `CF_SELURG` and `CF_SELUM`, with a default-on guarded early decision and byte-identical fallback when disabled.

- [ ] **Step 1: Add a failing configuration test**

Extend the unit test so a fresh selector reads explicit urgent controls:

```cpp
static void test_urgent_controls() {
#ifdef _WIN32
    _putenv_s("CF_SELURG", "0");
    _putenv_s("CF_SELUM", "7.5");
#else
    setenv("CF_SELURG", "0", 1);
    setenv("CF_SELUM", "7.5", 1);
#endif
    Selector s;
    Params p;
    p.K = 1;
    Theta th;
    s.init(th, p);
    assert(!s.urgentOn);
    assert(std::abs(s.urgentMargin - 7.5) < 1e-12);
}
```

Call it from `main`.

- [ ] **Step 2: Run the unit test to verify it fails**

Run the Task 2 compile command.

Expected: compilation fails because `urgentOn` and `urgentMargin` do not exist.

- [ ] **Step 3: Add urgent selector state and configuration**

Add public `Selector` fields and initialization:

```cpp
bool urgentOn = true, urgentDone = false;
double urgentMargin = 4.0;
```

```cpp
urgentOn = envD("CF_SELURG", 1.0) > 0.5;
urgentMargin = envD("CF_SELUM", 4.0);
urgentDone = false;
```

- [ ] **Step 4: Price a complete per-scenario candidate matrix in the early branch**

At the first `enough && !arrOver` transition, mark the round urgent:

```cpp
bool urgent = urgentOn && enough && !arrOver && !urgentDone;
if (urgent) { urgentDone = true; enoughSel = true; trig = true; }
```

After the existing budget calculation and `static Engine` / `static Future` declarations, add a complete-matrix pricing lambda:

```cpp
vector<vector<double>> urgentScore;
auto priceAll = [&](int scenario, vector<double>& row) {
    Belief::project(sc, scenario, fu);
    row.assign(cands.size(), numeric_limits<double>::quiet_NaN());
    for (size_t c = 0; c < cands.size(); ++c) {
        if (clock() >= hardStop) return false;
        clock_t dl = min(hardStop, clock() + runBudget);
        Sched clone = sc;
        clone.setTheta(cands[c]);
        eng.seed(sc, fu);
        Pred p = eng.run(std::move(clone), dl);
        if (!p.ok) return false;
        row[c] = p.score;
    }
    return true;
};
if (urgent) {
    vector<double> primaryRow, secondaryRow;
    bool complete = priceAll(1, primaryRow) && priceAll(0, secondaryRow);
    if (!complete) {
        ++rounds;
        spent += clock() - start;
        return;
    }
    urgentScore.push_back(std::move(primaryRow));
    urgentScore.push_back(std::move(secondaryRow));
}
```

The branch deliberately prices all candidates twice. The existing `runBudget = remain / (2 * cands.size())` allocation is already sized for this matrix. Normal rounds retain the existing primary-all / secondary-top-`keep` mean ranking.

- [ ] **Step 5: Gate the early switch on worst-case gain and imminent response change**

Before the existing normal switch block, add the urgent branch:

```cpp
if (urgent) {
    int want = robustWinner(urgentScore, 0, urgentMargin);
    if (want >= 0 && rounds < freezeAfter && nSwitch < maxSwitch) {
        Sched baseClone = sc, wantClone = sc;
        baseClone.setTheta(baseTh);
        wantClone.setTheta(cands[want]);
        Response baseOut, wantOut;
        baseClone.decide(baseOut);
        wantClone.decide(wantOut);
        if (!sameResponse(baseOut, wantOut)) {
            chosen = want;
            curTh = cands[want];
            sc.setTheta(curTh);
            ++nSwitch;
        }
    }
    ++rounds;
    spent += clock() - start;
    return;
}
```

The response comparison uses clones because `decide` mutates queue state. It must never call `decide` on the live scheduler before `schedFrame` does.

- [ ] **Step 6: Run unit tests and the disabled-gate regression**

Run:

```powershell
g++ -O2 -std=c++23 -o codex/test_simselect_selector.exe codex/test_simselect_selector.cpp
./codex/test_simselect_selector.exe
$env:CF_SELURG='0'
./score.ps1 codex/sol_simselect_urgent.cpp "judge/*.txt"
Remove-Item Env:CF_SELURG
```

Expected: unit tests pass and `CF_SELURG=0` is per-test identical to the Task 1 baseline.

- [ ] **Step 7: Compile with warnings and commit the guarded implementation**

Run:

```powershell
g++ -O2 -std=c++23 -Wall -Wextra -Wshadow -o codex/sol_simselect_urgent.exe codex/sol_simselect_urgent.cpp
```

Expected: exit code 0; inspect and resolve new warnings introduced by the urgent branch.

```powershell
git add codex/sol_simselect_urgent.cpp codex/test_simselect_selector.cpp
git commit -m "feat: guard early SimSelect switches by robust gain"
```

---

### Task 4: Sweep the robust margin and validate independently

**Files:**
- Create: `codex/sweep_simselect_urgent.ps1`
- Create: `codex/tmp/simselect_urgent/` result matrix
- Modify: `codex/NOTES.md`

**Interfaces:**
- Consumes: `CF_SELUM`, `codex/sol_simselect_urgent.cpp`, baseline score files, and the four policy suites.
- Produces: per-suite means, per-test deltas, runtime observations, and one plateau-selected default margin.

- [ ] **Step 1: Write the deterministic sweep**

Create a script that runs margins `0, 1, 2, 4, 8, 15` on:

```powershell
$margins = 0, 1, 2, 4, 8, 15
$suites = 'judge/*.txt', 'hold/*.txt', 'val/*.txt', 'tests/*.txt'
foreach ($m in $margins) {
    $env:CF_SELURG = '1'
    $env:CF_SELUM = [string]$m
    foreach ($suite in $suites) {
        ./score.ps1 codex/sol_simselect_urgent.cpp $suite
        # Copy the emitted score file to codex/tmp/simselect_urgent/<suite>-m<m>.txt.
    }
}
Remove-Item Env:CF_SELURG
Remove-Item Env:CF_SELUM
```

The actual script must use a normalized suite name (`judge`, `hold`, `val`, `tests`) for each copied result path and must fail if any scorer line contains `FAILED`.

- [ ] **Step 2: Run the sweep and compare every test to baseline**

Run:

```powershell
./codex/sweep_simselect_urgent.ps1
```

Expected: a complete 6-by-4 result matrix with no failures.

- [ ] **Step 3: Select only a plateau with independent support**

Choose a margin only if neighboring values have the same-sign aggregate delta on `judge/`, both alternating halves of `hold/`, `val/`, and `tests/`. Reject a point when one unexplained per-instance loss exceeds its total suite gain or when discovery and validation signs disagree.

Change only the `urgentMargin` default to the selected plateau center; keep `CF_SELUM` as a diagnostic override.

- [ ] **Step 4: Re-run the selected point twice**

Run all four suites twice with no selector environment variables.

Expected: identical score files across repeated runs, positive deltas on independent validation, and no concentrated regression that violates Step 3.

- [ ] **Step 5: Record evidence and commit the selected point**

Append the score table, largest gains/losses, chosen margin plateau, and runtime observations to `codex/NOTES.md`.

```powershell
git add codex/sol_simselect_urgent.cpp codex/sweep_simselect_urgent.ps1 codex/NOTES.md
git commit -m "perf: select robust SimSelect early-switch margin"
```

---

### Task 5: Diagnose remaining selector misses before expanding candidates

**Files:**
- Create: `codex/analyze_simselect_truth.ps1`
- Create: `codex/tmp/simselect_truth/` diagnostic outputs
- Modify: `codex/NOTES.md`

**Interfaces:**
- Consumes: `CF_TRUTH`, default predictions, urgent predictions, and actual per-instance scores.
- Produces: a classification of each meaningful miss as belief, timing, or candidate error.

- [ ] **Step 1: Build a truth-mode diagnostic runner**

For every discovery and validation instance whose absolute urgent delta exceeds 0.5, run the selected source with `CF_TRUTH` set to that exact test file and save its score and `[sel]` trace. Also run the same source with `CF_SELURG=0`.

- [ ] **Step 2: Classify misses using fixed rules**

Record:

- `belief` when truth mode chooses a better existing continuation than normal belief;
- `timing` when truth mode identifies the winner only after the first differing response; and
- `candidate` only when truth mode still cannot match the offline best fixed policy.

Do not add policy vectors unless the same paired-coordinate family is a candidate error on at least two discovery instances and at least one validation instance.

- [ ] **Step 3: Keep the candidate set unchanged unless the evidence threshold is met**

If the threshold is not met, document `no candidate expansion` and proceed. If it is met, add exactly that paired-coordinate family to `Selector::build`, re-run Tasks 2-4, and retain it only if complete-round rates and independent validation do not regress.

- [ ] **Step 4: Commit the diagnostic evidence**

```powershell
git add codex/analyze_simselect_truth.ps1 codex/NOTES.md
git commit -m "analysis: classify remaining SimSelect selection misses"
```

---

### Task 6: Run the complete verification battery

**Files:**
- Create: `codex/sol_submit.cpp`
- Create: `codex/tmp/pipecheck/` replay traces

**Interfaces:**
- Consumes: the selected winning candidate.
- Produces: a size-compliant submission source with fresh compile, legality, protocol, determinism, and replay evidence.

- [ ] **Step 1: Generate the submission artifact**

Run:

```powershell
py claude/tmp/strip.py codex/sol_simselect_urgent.cpp codex/sol_submit.cpp
(Get-Content -Raw codex/sol_submit.cpp).Length
```

Expected: length at most 65535.

- [ ] **Step 2: Compile all required standards**

Run:

```powershell
g++ -O2 -std=c++17 -o codex/verify_c17.exe codex/sol_submit.cpp
g++ -O2 -std=c++20 -o codex/verify_c20.exe codex/sol_submit.cpp
g++ -O2 -std=c++23 -o codex/verify_c23.exe codex/sol_submit.cpp
```

Expected: all three exit 0.

- [ ] **Step 3: Run safety and unit checks**

Run:

```powershell
./codex/test_simselect_selector.exe
./score.ps1 codex/sol_submit.cpp "edge/*.txt"
```

Expected: selector tests pass and edge reports 23 tests, zero failures.

- [ ] **Step 4: Run the real protocol checker**

Run:

```powershell
g++ -O2 -std=gnu++17 -o codex/sol_submit.exe codex/sol_submit.cpp
py protocheck.py codex/sol_submit.exe
```

Expected: `8/8`.

- [ ] **Step 5: Generate and replay all test traces**

```powershell
./score.ps1 codex/sol_submit.cpp "tests/*.txt"
New-Item -ItemType Directory -Force codex/tmp/pipecheck | Out-Null
$sim = (Resolve-Path tmp/sim_codex_sol_submit.exe).Path
foreach ($test in Get-ChildItem tests/*.txt) {
    $dump = Join-Path 'codex/tmp/pipecheck' $test.BaseName
    & $sim $test.FullName -dump $dump | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "trace dump failed: $($test.Name)" }
}
$traces = Get-ChildItem codex/tmp/pipecheck -File | Select-Object -ExpandProperty FullName
$env:SOL=(Resolve-Path codex/sol_submit.exe).Path
py pipecheck.py $traces
Remove-Item Env:SOL
```

Expected: every trace passes, including the largest `g_8_2` replay.

- [ ] **Step 6: Re-score final source and verify determinism**

Run `judge/`, `hold/`, `val/`, and `tests/` twice from `codex/sol_submit.cpp`.

Expected: scores match the selected candidate and repeated score files are identical.

- [ ] **Step 7: Commit the verified artifact**

```powershell
git add codex/sol_submit.cpp codex/NOTES.md
git commit -m "chore: verify SimSelect submission candidate"
```

---

### Task 7: Submit and record the judge fingerprint

**Files:**
- Modify: `codex/NOTES.md`

**Interfaces:**
- Consumes: verified `codex/sol_submit.cpp` and the signed-in Codeforces session.
- Produces: a Codeforces submission ID, per-test scores, and a recorded next decision.

- [ ] **Step 1: Confirm the rate-limit window and upload the exact verified file**

Navigate to `https://codeforces.com/contest/2251/submit`, upload the absolute path to `codex/sol_submit.cpp`, invoke the real submit button, and read the DOM for the 65535-character validation error before assuming the click succeeded.

- [ ] **Step 2: Wait for judging and capture the submission**

Open the account's contest submissions after approximately 20 seconds. If judging is still active, monitor without submitting a duplicate.

- [ ] **Step 3: Record the complete result**

Append the submission ID, total score, all 22 per-test scores, runtime, source hash, and delta versus 16115.479 to `codex/NOTES.md`.

- [ ] **Step 4: Decide from evidence**

If the score is above 16115.479, retain the new best and run one final local/source-hash check. If it is not, keep the existing best, use the per-test fingerprint to update the belief/timing/candidate diagnosis, and begin a new tagged experiment without altering the verified failed candidate.

- [ ] **Step 5: Commit the judge record**

```powershell
git add codex/NOTES.md
git commit -m "results: record robust SimSelect judge fingerprint"
```
