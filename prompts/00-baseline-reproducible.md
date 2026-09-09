---
status: done
milestone: 0
last_updated: 2026-09-09
---

# Milestone 0 — Make the baseline safe and reproducible

## Status: DONE

This milestone is complete. This file is kept as the record of what was
established and as a re-verification checklist before any later milestone
changes the SDK or the game fingerprint.

## What was established

- Root `.gitignore` excludes `game/`, `out/`, `generated/*` (except
  `generated/rexglue.cmake`), local logs/captures, and `CMakeUserPresets.json`.
- ReXGlue SDK pinned as a submodule at `rexglue-sdk/`, commit
  `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` (verified by `git submodule status`).
- Game fingerprints recorded in `config/game_fingerprints.toml` (see below).
- Xenia Canary baseline captured in
  `docs/baselines/xenia-canary-80679bc.md`.
- Generated-code policy: generated C++ is not committed; only
  `generated/rexglue.cmake` (from `rexglue init`) is kept.

## Frozen game fingerprint

| File | Role | Size | SHA-256 |
| --- | --- | --- | --- |
| `default.xex` | entrypoint | 9023488 | `e2195d6241d423197df31a84666a8d658cb7999521332f40c69f99f5d36e84bb` |
| `gen/main_xbox.hdr` | archive-header | 113528 | `a12777e8df91215d53a8e3e917693df9e153381d695491786124b98c244756ed` |
| `gen/main_xbox_0.ark` | archive-data | 361769177 | `eac4d2c2bb71d0142a9a604dfe4f3773d95d4512763009b3d748b002291bc620` |

XEX metadata: Title ID `5841122D`, Media ID `78492654`, region-free
(`FFFFFFFF`), XEX version `0.0.0.2`, no title update / no delta-patch markers.

## Re-verification checklist (run before upgrading SDK or changing fingerprints)

```powershell
git submodule status           # expect c94f5ebdcb3c9d1a460ca48e04f9758448f8d518
git status --short             # expect clean (game/, out/ untracked but ignored)
```

- Confirm `game/` is ignored and absent from any commit.
- Confirm `config/game_fingerprints.toml` still matches the actual dump with
  `Get-FileHash -Algorithm SHA256`.
- If anything changed, stop and update this file, `config/game_fingerprints.toml`,
  and the baseline doc together.

## Handoff

Nothing to do here. Proceed to `01-scaffold-generate.md`.
