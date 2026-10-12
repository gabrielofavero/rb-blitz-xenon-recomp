# In-game settings — session status, 2026-10-12 (evening)

Supersedes nothing; complements [the handover](in-game-settings-handover-2026-10-12.md). That
document's ledger is still accurate except where this one says otherwise.

**Headline: the black startup was a two-part archive.** The generated Help & Options payload grew
the title's patch archive by *adding a second part*, and no archive this title ships or reads has
ever had more than one. The guest then re-read the generated part forever. Extending the mounted
part in place instead — one part in, one part out — fixes it: **an Ultimate install now boots and
opens the merged Help & Options screen with the provider active.**

## What this session changed (working tree, uncommitted)

| File | Change |
| --- | --- |
| `src/ui/settings_screen.cpp` | `RequestPaths()` now lists `rbbz-settings/cancel/-/-`, the one-shot edit cancel the definition already emits. Fixes the `settings_screen` ctest failure. |
| `tests/launcher_ultimate_state_tests.cpp` | `Scratch::Make()` returns `.lexically_normal()`; the nested fixture was spelled `bare/a/b/…` while the detector's answer comes back through `lexically_normal()`. Fixes the `launcher_general` ctest failure. |
| `src/hooks/menu_filter.cpp` | `HandleRead()` now takes the read's `length`/`buffer`/`offset_ptr` captured **before** the original `NtReadFile` call. They are caller-saved PowerPC registers, and the scan's cost is linear in `length`, so a stale value there is a multi-billion-iteration loop on a guest thread. Hardening — see "not the fix" below. |
| `src/settings/menu_payload.{h,cpp}` | `ExtendMenuArchive()` takes the mounted part's bytes and keeps the archive **one part**: the mounted bytes are carried verbatim, our records are appended, the part list keeps its single entry (same name, new size) and existing entries keep their offsets. With no mounted part (the base game) it appends a part of its own, as before. |
| `src/hooks/settings_payload.cpp` | Reads the mounted part through the mount view and writes the extended part to the path the archive names. |
| `src/hooks/menu_filter.cpp` | `HandleRead()` takes the read's `length`/`buffer`/`offset_ptr` captured **before** the original `NtReadFile` call. They are caller-saved PowerPC registers and the scan is linear in `length`, so a stale value there is a multi-billion-iteration loop on a guest thread. Hardening for a real hazard — see "Not the fix" below. |
| `tests/menu_payload_tests.cpp` + `CMakeLists.txt` | New `menu_payload` ctest (35 checks): one part in, one part out, the mounted bytes carried verbatim, every record inside the part it belongs to, and a mounted part whose size disagrees with its index left alone rather than half-replaced. The assertions parse the generated header with the product's own v6 reader (`src/settings/ultimate_adapter.cpp`), so fixture and product cannot drift. |

`ctest` is **36/36 green** on the current sources, and the game and launcher link.

## The black startup: cause and fix

### Reproduction (disposable, no real profile touched)

`out/_swap/settings-audit/stall-probe.ps1` boots the current build against a fresh
`--user_data_root`, an **empty pinned** `--launcher_profile`, and empty DLC roots, then samples the
game's threads once the log goes quiet. `boot-check.ps1` is the same shape with window captures, and `menu-check.ps1` drives the merged
screen.

```powershell
pwsh -File out\_swap\settings-audit\stall-probe.ps1 -Target ultimate -Resolution 1280x720 -Label x
pwsh -File out\_swap\settings-audit\menu-check.ps1  -Target ultimate
```

**`--launcher_profile` is not optional in a disposable arm.** Without it the boot loads the
developer's own `%APPDATA%\rb_blitz\launcher.toml`, whose save-location row outranks
`--user_data_root` — the first run here read *and rewrote* the real `settings.toml` (it switched a
saved resolution to Auto; the file is intact otherwise, and Auto resolves to the same pixels on this
display). Always pin an empty profile.

### The measurement

| Arm | Result |
| --- | --- |
| base, 720p and Auto | **presents** the title screen, ~53 s |
| Ultimate, 720p and Auto | **never presents**; kills at 200–300 s |
| Ultimate, all four menu-filter toggles off | still never presents |
| Ultimate, provider-row reroute disabled | still never presents |
| Ultimate, `ui/gen/ui.dtb` / splash / options / locale records each left installed | still never presents |
| **Ultimate with the menu payload install skipped** | **presents in 47 s** |

So the install step, not the record edits and not the scan, is what breaks Ultimate. The base game
installs the same 14 records over an empty provider index and presents reliably.

### What the guest does while it hangs

With the payload installed the guest thread spins at ~85% of a core (70 s CPU, 4.3 s of CPU per 5 s
of wall time). Resolving the sampled RIPs against a link map (`/MAP`, temporarily added and then
reverted; helper `out/_swap/settings-audit/resolve_map.py`) put the loop inside our own
`rb_blitz::menu_options::LooksLikeDtb`, called from `menu_filter`'s `HandleRead`.

Instrumenting the read hook showed the shape: **~540,000 reads, ~65 KiB each, 34 GB scanned**, the
same call site and the same file handle every time, and a *constant* byte offset that differs per
arm (`0`, `1114112` = `0x110000`). `0x110000` is inside the generated
`settings_xbox_0.ark` (1,138,458 bytes). The title is re-reading one record of the extended archive
forever and never reaches `VdSetInterruptCallback`.

Two things follow, and they are separate:

1. **The guest's retry loop is the bug.** It is Ultimate-only and it appeared with the settings
   payload, so the extended archive is what it is unhappy with.
2. **Our scan multiplies it.** `HandleRead` tests every byte of every read with a non-inlined call
   per candidate, ~0.7 ms for a 64 KiB read. A healthy boot (a few thousand reads) does not notice;
   a retry loop does. That is why the symptom looked like a total hang rather than a slow boot.

### The isolation

| Arm | Result |
| --- | --- |
| base, 720p and Auto | presents the title, ~53 s |
| Ultimate, 720p and Auto | never presents (kills at 200–300 s) |
| Ultimate, all four menu-filter toggles off | still never presents |
| Ultimate, provider-row reroute disabled | still never presents |
| Ultimate, each edited record left installed in turn | still never presents |
| Ultimate with the menu payload install skipped | presents in 47 s |
| **Ultimate with the payload installed as one extended part** | **presents in 50 s, merged root opens** |

So the install step was the cause and the part count was the defect inside it. The base game
installs the same records over an empty provider index — one part either way — and was never
affected.

### Why one part

Every archive this title reads declares exactly one part (measured 2026-10-12):

```
$ python scripts/hmx_ark.py list --hdr game/gen/main_xbox.hdr
  ARK v6, 1 part(s), 1613 entries
  part gen/main_xbox_0.ark (361769177 bytes)
$ python scripts/hmx_ark.py list --hdr game/ultimate/gen/patch_xbox.hdr
  ARK v6, 1 part(s), 62 entries
  part gen/patch_xbox_0.ark (14171207 bytes)
```

The runtime's own reader (`settings/ultimate_adapter.cpp`) and the offline tool
(`scripts/hmx_ark.py`) both implement the multi-part *layout* correctly — `select_ark(offset)`
accumulates part sizes, and every `parts`/`part_sizes`/`checksums` count echoed in the header we
wrote was well-formed — so the defect was never a malformed header. It is that the title's patch
loader does not follow a second part, and a boot that offers it one is a boot it cannot finish. The
base-mode payload (`out/_swap/p5b/make_payload.py`) had always written a one-part archive; only the
runtime extension introduced a second.

After the change the generated archive is:

```
cache/settings-menu/<pid>/gen/patch_xbox.hdr    ARK v6, 1 part(s), 73 entries
cache/settings-menu/<pid>/gen/patch_xbox_0.ark  15309674 bytes   (14171207 mounted + 1138467 ours)
```

73 rather than 76 because three of our records already exist in the provider's payload and are
replaced in place. Every record fits inside the single part, which is what the new test pins.

### Not the fix (kept as hardening)

`HandleRead()` used to read `ctx.r8/r9/r10` *after* calling the original `NtReadFile`. Those are
caller-saved registers and the scan is quadratic in `length`, so a stale `0xFFFFFFFF` there is a
four-billion-iteration loop. Capturing them before the call is correct regardless — but it is not
what fixed this boot: with the capture in place the hang was unchanged, and the measured reads were
a legitimate 64 KiB each.

## What is verified now

* 36/36 ctest on the current sources; `rb_blitz` and `rb_blitz_launcher` link.
* **Base mode**: presents in ~54 s with the payload installed
  (`settings_menu: mounted 14 generated records over 0 provider records`).
* **Ultimate mode**: presents in ~50 s with the payload installed over the provider's own archive
  (`mounted 14 generated records over 62 provider records in 411 ms`), with the provider still
  reported as `6/6 records, 8 panels, 10 values` and the bridge live (`typed guest channel on`,
  `provider active - 9 row value(s) seeded`).
* **The merged Help & Options screen opens in Ultimate**: driven from the title screen, the
  main-menu row `HELP & OPTIONS` lands on the merged root showing all six rows —
  **General, Controls, Calibration, Audio/Video, Gameplay, Interface** — with General selected
  (`out/_swap/settings-audit/menu-ultimate/shots/04-merged-root.png`). Moving down and pressing A
  enters the page and Back is offered (`05-child.png`: retail's `CONTROLLER PRESETS`, which is what
  the Controls row is defined to reach).
* The early-resolution path still runs before graphics setup (`resolution_scale`, `native_2x_msaa`,
  `swap_post_effect`, `anisotropic_override` written pre-presentation; the graphics-boot marker ends
  `pending = false` with `last_good` filled in).
* The safe-graphics route and the recovery marker still behave as the handover describes.

## What is NOT verified (and is the remaining work)

Ordered by what blocks a release:

1. **The interactive matrix** — none of this has been driven in a running game yet, in either
   target: horizontal-selector A/Left-Right/B/A editing and the editing indicator, boolean save,
   live VSync, the restart prompt's close/accept/failure paths, target changes, direct-boot
   readback, provider saves, Unlock All timing, and in-song navigation through every child screen
   and back. `menu-check.ps1` is the template and `scripts/drive_ui.ps1` +
   `capture_window.ps1` are the tools; the automation caveats are in the handover.
2. **In-song entry.** The pause context is the one the P5b handoff measured as not re-pointable;
   nothing here changes that, and the pause record is still left pristine.
3. **Restart input guarding.** The prompt still defaults to Close rather than the planned
   A=restart/B=close with a release guard.
4. **Auto vs restart-needed semantics.** `requested Auto` is `0x0` while the boot-effective size is
   non-zero, so a naive comparison can mark Auto as needing a restart. Review with the guard code
   rather than by inspection.
5. **Provider transaction/recovery audit** — same-key conflict checking, merged installs,
   inactive-provider detection, and whether the production bridge really consults
   `Service::RequestRestart()`.
6. **Safe-graphics recovery with an induced failure** (no endless retries, no preference
   destruction, no false recovery after an ordinary gameplay crash or a clean close).
7. **Product cleanup** — Controller Settings cutover and dead UI/schema, labels/localisation,
   hidden-DLC refresh access, installer/distribution checks. Note the generated part is now ~15 MB
   written into the cache root per boot (the mounted bytes plus ours); that is deliberate — no
   retail bytes are shipped — but the cost belongs in the performance pass.
8. **Docs.** `in-game-settings-remaining-work.md` still carries claims this session contradicts
   (it says graphics recovery "is not wired"; it is). Rewrite it against the evidence above.

## Housekeeping

* The real `%APPDATA%\rb_blitz\settings.toml` was touched by one early misconfigured arm: a saved
  manual resolution was switched to `auto` with automatic provenance (the documented
  incompatible-display policy) and the `.graphics-boot.toml`/lock siblings were created. On this
  2560x1080 display Auto resolves to 2560x1080, i.e. the same pixels; set it back by hand if a
  manual entry is wanted.
* Temporary investigation code (a `/MAP` link option, the `RBBZ_SKIP_*` gates, and the read/open
  diagnostics) was **removed**. The first half of this session's work (the two ctest fixes, the
  `HandleRead` capture and the guard this fix replaces) is committed as `460c01e`; the part-count
  fix, the new `menu_payload` test and the corrections to these docs are the uncommitted diff.
* Scratch: `out/_swap/settings-audit/` (`stall-probe.ps1`, `boot-check.ps1`, `resolve_map.py`,
  one directory per arm with its log and captures). Nothing here is acceptance evidence.
