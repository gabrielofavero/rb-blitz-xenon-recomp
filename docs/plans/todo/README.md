# To-do plans

Plans that are written but not started. Each carries its `YYYY-MM-DD-` creation date in the filename.

| Plan | What it covers |
| --- | --- |
| [`2026-10-10-shader-cache-findings.md`](2026-10-10-shader-cache-findings.md) | Shader-stutter implementation plan: repair the D3D12 preload barrier, prepare a game-specific pipeline catalogue before playback, validate coverage, and package recipes (not started; expanded 2026-10-17). |
| [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) | Song Load Guard: which stall/crash cases a cooperative timeout can safely handle and which it cannot (part 1), plus the five implementation prompts that follow it (part 2). |

The Load Guard document is a merge (2026-10-22) of two records, kept as `## Part N` sections so old
citations still resolve:

| Old document | Now |
| --- | --- |
| `2026-10-13-load-guard-findings.md` | [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) part 1 |
| `2026-10-13-load-guard-prompts.md` | [`2026-10-13-load-guard.md`](2026-10-13-load-guard.md) part 2 |

When work starts on one of these, `git mv` it to [`../in-progress/`](../in-progress/).

See [`../README.md`](../README.md) for the status taxonomy.
