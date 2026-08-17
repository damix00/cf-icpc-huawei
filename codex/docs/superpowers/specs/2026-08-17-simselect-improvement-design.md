# SimSelect Improvement Design

## Goal

Beat the current Codeforces score of 16115.479 by improving Claude's SimSelect architecture, not by replacing its proven scheduler or exact forward simulator. All implementation and experiment files remain under `codex/`; `claude/` is a read-only research baseline.

## Starting Point

Claude's `sol.cpp` contains three useful layers:

1. the 16109.263 scheduling policy, refactored into a cloneable `Sched`;
2. an exact event simulator that can run a cloned scheduler to completion for a proposed future; and
3. an online selector that estimates the hidden future, evaluates policy vectors, and switches the live scheduler to the predicted best continuation.

The selector raised the judge score to 16115.479. Local truth-mode measurements show that the remaining loss is primarily from decisions made before the selector's first useful switch, while the real-judge regressions on tests 8 and 13 show that unsafe early commitment can also erase gains. Enabling the existing earliest trigger moved only judge test 6 and lost 3.163 points, so an unconditional earlier trigger is not acceptable.

## Proposed Improvement

### 1. Reproduce the baseline in the Codex workspace

Create a comment-stripped Codex copy of Claude's documented master and prove that it matches Claude's scores and protocol behavior. The copied baseline is immutable; every experiment receives a tagged filename.

### 2. Add robust urgent-decision selection

Keep the existing selector unchanged for its normal milestones. At the first moment its belief becomes usable, run an additional early round only when waiting can change the next real scheduling response.

For every candidate and the shipped baseline:

- simulate each active arrival projection separately;
- compare score deltas to the baseline in each projection rather than averaging them first;
- clone the scheduler and compare the candidate's next response with the baseline response; and
- record whether the same candidate family remains competitive across consecutive early snapshots.

An early switch is allowed only when all of these conditions hold:

1. the candidate beats the baseline by an explicit margin in every completed projection;
2. the round prices every candidate before its deadline;
3. the candidate changes an imminent response, so delaying the choice would lose a real opportunity;
4. its lead is stable across the available projections or across consecutive snapshots; and
5. the configured switch limit and wall-clock budget remain valid.

If any condition fails, the live scheduler stays on its current policy and the existing later selector remains responsible for the decision. This makes the change a guarded extension of SimSelect rather than a replacement.

The first experiment will use worst-case improvement over the active projections. It will not average random output-length splits: that mechanism already lost 4.6 points per local `judge/` instance and produced a 66.3-point regression.

### 3. Improve the candidate set only when truth-mode evidence supports it

After the early gate is measured, use `CF_TRUTH` locally to separate three causes of missed oracle score:

- belief error: the correct continuation wins only with the true future;
- timing error: the correct continuation is found after its decisive action; and
- candidate error: no available policy vector reaches the best measured continuation.

Only the third case authorizes candidate-set expansion. Paired-coordinate candidates will be added around policies that win on multiple discovery instances, then retained only when their gains survive independent validation. This avoids spending runtime on speculative combinations and preserves complete selector rounds.

## Experiment Discipline

The suites have distinct roles:

- discovery: `judge/` and alternating `hold/` instances;
- validation: the other `hold/` instances, `val/`, and `tests/`;
- safety: `edge/`, protocol checks, and real-pipe replay.

Every experiment is compared per test against the reproduced 16115.479 baseline. A local mean alone is insufficient. A candidate is rejected when its gain comes from a narrow constant spike, when one unexplained loss dominates its total gain, or when it improves discovery data but reverses sign on independent validation.

Selector constants must be swept over neighboring values. The chosen value must lie on a plateau. Runtime and incomplete-round counts are measured alongside score so selection quality cannot be an artifact of timeouts.

## Safety and Fallbacks

- An incomplete simulation round never changes the live policy.
- A failed projection, exhausted CPU budget, or inconsistent response comparison falls back to the current scheduler.
- The existing normal selector, scheduler legality rules, and low-level interactive input adapter remain intact.
- Debug truth data and local file access remain disabled in judge builds.
- The final submission is generated below Codeforces' 65535-character source limit.
- `ref.cpp` and all files under `claude/` remain unmodified.

## Verification

Before any submission, the winning candidate must satisfy all of the following with fresh output:

1. compile under C++17, C++20, and C++23;
2. reproduce the baseline when the new gate is disabled;
3. report zero failures on `edge/`;
4. pass all eight `protocheck.py` cases;
5. pass real-pipe replay for every `tests/` trace;
6. finish within the 15-second judge limit, with a conservative internal wall ceiling;
7. remain deterministic across repeated representative runs; and
8. fit within the submission size limit.

## Submission and Success Criteria

A candidate is eligible for submission only when it improves the Claude baseline across independent local suites, has a defensible per-test delta shape, and passes the full verification battery. Submissions respect the observed rate limit and their per-test fingerprints are recorded in `codex/NOTES.md`.

The task succeeds only when an accepted Codeforces submission scores strictly above 16115.479. A locally improved candidate that does not beat the judge remains an experiment, not a completed result.
