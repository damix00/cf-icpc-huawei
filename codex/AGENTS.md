# codex/ — Codex's workspace for CF 2251A

Problem context, model, protocol, scoring, harness commands, verification battery and submission
procedure live in the **root `CLAUDE.md`** (shared, not Claude-specific). Read it first.

## Rules

* Work **only inside `codex/`**. Root files are shared — change them only for harness fixes that
  help both agents, and never edit `ref.cpp`.
* Score from the repo root with a path: `sh score.sh codex/sol.cpp "judge/*.txt"`.
* Tags fold the path (`codex/sol.cpp` → `codex_sol`), so `tmp/` never collides with `claude/`.
* The bar is **16109.263** (`ref.cpp`, the frozen reference). Anything that does not beat it is not
  shippable.
* Never edit `codex/sol.cpp` for an experiment — copy to `codex/sol_<tag>.cpp` and score that.

## Starting points

`ref.cpp` is a working, protocol-correct solution scoring 16109.263; its `main()` at the bottom is
the only tested stdin/stdout adapter in the repo, and the `fread` trap documented in the root
`CLAUDE.md` is worth re-reading before writing your own.

`claude/NOTES.md` records ~20 sections of measured dead ends for one particular architecture. It is
available if you want it, but the point of a second independent workspace is a different design —
reading it will anchor you to the same local optimum.
