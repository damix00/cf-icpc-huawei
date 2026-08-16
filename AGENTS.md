# AGENTS.md

**Read `CLAUDE.md` in this directory first.** It is the shared problem context for CF 2251A:
the model, the interaction protocol, the score function, what the judge has revealed about its 22
tests, the harness commands, the verification battery and the submission procedure. It is not
Claude-specific despite the name.

Two agents work in this repo, each in its own folder:

* `codex/` — Codex's workspace. See `codex/AGENTS.md`.
* `claude/` — Claude's workspace. Do not read or edit it while working in `codex/`; the two attempts
  are meant to be independent.

Shared, at the root: `sim.cpp`, `gen.cpp`, `analyze.cpp`, the suites (`tests/ hold/ val/ edge/
judge/`), the scoring scripts, and `pipecheck.py` / `protocheck.py`. Change these only for harness
fixes that help both agents.

`ref.cpp` is the frozen 16109.263 reference solution — **read-only**. It is the baseline to beat and
the schedule `sim.exe -calibrate` uses.
