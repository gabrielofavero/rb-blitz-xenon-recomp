# Local patches for the pinned ReXGlue SDK

`rexglue-sdk/` is a pinned submodule of the **upstream**
`https://github.com/rexglue/rexglue-sdk.git` (pinned at `c94f5eb`). Local edits
must **not** be committed inside the submodule: the parent gitlink would then
reference a commit that does not exist on the upstream remote, so a fresh
checkout could not initialize the SDK (breaking the Milestone 0 reproducibility
guarantee).

Instead, SDK fixes are kept here as patch files and applied to the pinned
checkout.

## Patches

| Patch | Purpose | Upstream candidate |
| --- | --- | --- |
| `rexglue-sdk/0001-xthread-log-once-core-count-warning.patch` | `XThread::SetActiveCpu` re-logged "Too few processor cores" on every thread CPU assignment (~11k lines, ~580 KB per boot). Log it once. | yes |

## Apply / verify

```powershell
# Apply every patch set to the pinned SDK checkout (idempotent; safe to re-run).
# `-ExecutionPolicy Bypass` is required on this machine, where the policy is
# Restricted and a bare `.\scripts\...ps1` fails with UnauthorizedAccess.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1

# Same, without touching the working tree
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\apply_sdk_patches.ps1 -Check
```

The script reports each patch as *already applied*, *applied now*, or *failed*,
and warns if the SDK checkout no longer matches the gitlink recorded in the
parent commit. Manual equivalent, from the repository root:

```powershell
# Use absolute paths: `git -C <dir>` resolves relative ones against <dir>.
$p = (Resolve-Path patches\rexglue-sdk\0001-xthread-log-once-core-count-warning.patch).Path

git -C rexglue-sdk apply --reverse --check $p   # exit 0 => already applied
git -C rexglue-sdk apply --check         $p     # exit 0 => applies cleanly
git -C rexglue-sdk apply                 $p     # apply it
```

The patches are intentionally **not** applied automatically by the build. Apply
them once after a fresh submodule checkout; `git status` in the parent then shows
` M rexglue-sdk` and the submodule shows ` M src/system/xthread.cpp`. Both are
expected while a patch is applied, and neither is drift to be "fixed".

When a patch lands upstream, drop the file here and bump the submodule pin.

## Making the SDK tree sane on Windows

An applied patch is not the only reason the SDK submodule can look dirty. Sixteen
tracked entries under `rexglue-sdk/thirdparty/` are symbolic links upstream:

| Repo | Entries | If flattened |
| --- | --- | --- |
| `libmspack` | 15 — `cabextract/mspack/*.c`, `*.h` and `ChangeLog`, each pointing into the real source tree of the same repo (e.g. `cabextract/mspack/lzxd.c` → `../../libmspack/mspack/lzxd.c`) | **Build-breaking.** `rexglue-sdk/thirdparty/CMakeLists.txt` compiles `cabextract/mspack/lzxd.c`, which becomes 29 bytes of link text instead of 30,796 bytes of source. |
| `o1heap` | 1 — `CLAUDE.md` → `AGENTS.md` | Documentation only. |

This machine cannot create symlinks: the shell is not elevated, Windows Developer
Mode is off, and both `New-Item -ItemType SymbolicLink` and `cmd mklink` fail. The
`rexglue-sdk`, `libmspack` and `o1heap` repos carry no local `core.symlinks`
setting, so they inherit `false` from the system gitconfig
(`C:/Program Files/Git/etc/gitconfig`) and a checkout writes the *link text* into a
plain file.

Do **not** "fix" this by setting `core.symlinks true` in a submodule. Git then
calls `symlink()`, which is unavailable here: the checkout fails with
`error: unable to create symlink cabextract/mspack/lzxd.c: Function not
implemented`, exits 255, and leaves the file **missing** — worse than the flattened
stub. (Verified 2026-09-19; the root repo's `core.symlinks=true`, left over from
B-001, only affects the root repo, which contains no symlinks.)

Expand the flattened files from their targets instead:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1 -NoIndexMarks
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repair_flat_symlinks.ps1 -ClearMarks
```

`repair_flat_symlinks.ps1` walks every submodule (and the root repo), follows each
index entry with mode `120000`, and copies the target's content over the flattened
file only when the content differs — so it is idempotent and safe to re-run. A
healthy tree prints `content already correct : 16` and
`repaired from target    : 0`. It then marks exactly those paths
`--skip-worktree`, because the real difference (a type change, `120000` →
`100644`) cannot be resolved on this machine and would otherwise show as permanent
dirt. Use `-ClearMarks` before editing libmspack sources by hand if you want to see
your edits in `git status`, and `-NoIndexMarks` if you do not want the index
touched at all.

After both scripts, `git -C rexglue-sdk status --porcelain` should show only
` M src/system/xthread.cpp` (the patch) and the parent should show only
` M rexglue-sdk`. Re-run them after `git submodule update`, a fresh clone, or any
checkout that rewrites the SDK tree. Background:
[../docs/bringup-log.md](../docs/bringup-log.md) B-001 and
[../docs/known-issues.md](../docs/known-issues.md).
