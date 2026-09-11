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
# Apply all patches to the current SDK checkout
Get-ChildItem patches\rexglue-sdk\*.patch | ForEach-Object {
    git -C rexglue-sdk apply --check $_.FullName   # dry run
    git -C rexglue-sdk apply         $_.FullName   # apply
}

# Check whether the patch is already applied (reverse dry-run succeeds => applied)
git -C rexglue-sdk apply --reverse --check patches\rexglue-sdk\0001-xthread-log-once-core-count-warning.patch
```

The patches are intentionally **not** applied automatically by the build. Apply
them once after a fresh submodule checkout; the working tree then shows
`m rexglue-sdk`, which is expected.

When a patch lands upstream, drop the file here and bump the submodule pin.
