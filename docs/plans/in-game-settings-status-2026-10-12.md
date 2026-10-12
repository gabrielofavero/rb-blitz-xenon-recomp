# In-game settings — session status, 2026-10-12 (evening)

Supersedes nothing; complements [the handover](in-game-settings-handover-2026-10-12.md). That
document's ledger is still accurate except where this one says otherwise.

**Headline: the black startup is diagnosed, isolated and worked around. It is not a UI or
graphics problem — the generated Help & Options menu payload was hanging an Ultimate boot.**

## What this session changed (working tree, uncommitted)

| File | Change |
| --- | --- |
| `src/ui/settings_screen.cpp` | `RequestPaths()` now lists `rbbz-settings/cancel/-/-`, the one-shot edit cancel the definition already emits. Fixes the `settings_screen` ctest failure. |
| `tests/launcher_ultimate_state_tests.cpp` | `Scratch::Make()` returns `.lexically_normal()`; the nested fixture was spelled `bare/a/b/…` while the detector's answer comes back through `lexically_normal()`. Fixes the `launcher_general` ctest failure. |
| `src/hooks/menu_filter.cpp` | `HandleRead()` now takes the read's `length`/`buffer`/`offset_ptr` captured **before** the original `NtReadFile` call. They are caller-saved PowerPC registers, and the scan's cost is linear in `length`, so a stale value there is a multi-billion-iteration loop on a guest thread. Hardening — see "not the fix" below. |
| `src/hooks/settings_payload.cpp` | **Refuses to install the generated menu records when the game root already carries a patch archive** (the Ultimate payload case). This is the workaround for the boot hang. |

`ctest` is **35/35 green** on the current sources, and the game and launcher link.

## The black startup: what it actually was

### Reproduction (disposable, no real profile touched)

`out/_swap/settings-audit/stall-probe.ps1` boots the current build against a fresh
`--user_data_root`, an **empty pinned** `--launcher_profile`, and empty DLC roots, then samples the
game's threads once the log goes quiet. `boot-check.ps1` is the same shape with window captures.

```powershell
pwsh -File out\_swap\settings-audit\stall-probe.ps1 -Target ultimate -Resolution 1280x720 -Label x
pwsh -File out\_swap\settings-audit\stall-probe.ps1 -Target common   -Resolution 1280x720 -Label y
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

### The two leading hypotheses (not yet resolved)

* **The extended archive is the first multi-part ark this title has ever seen.** The retail
  `main_xbox.hdr` and the payload's `patch_xbox.hdr` each declare exactly one part.
  `ExtendMenuArchive()` appends a *second* part (`gen/settings_xbox_0.ark`) and rebases the new
  records' offsets onto the sum of the earlier part sizes (14171207 + n). If the guest's ark loader
  honours only `parts[0]`, or resolves record offsets against the part it happens to have open, the
  reads land in the wrong file — which is what the constant `0x110000` offset inside our own part
  file looks like. Candidate fix: write **one** part — the provider's part bytes followed by ours,
  with the provider's record offsets unchanged and `parts`/`part_sizes` keeping one entry. Open
  question in the same change: what the part `checksums` table must hold (a `0` is tolerated for a
  brand-new part today, which is what makes this worth testing rather than guessing).
* **`ultimate_patches` bit 1** re-points the content device at `D:`, which this install then
  re-points again at `\Device\BlitzSettingsMenu` (a union of the cache root over the payload
  overlay, itself a union). If the title's content walk mis-handles that nesting, the same archive
  would be walked in a loop. Testable by re-pointing only `game:` and leaving `d:` alone.

Both are cheap to test with the harness above; neither is proven.

## What is verified now

* 35/35 ctest on the current sources; `rb_blitz` and `rb_blitz_launcher` link.
* **Base mode boots and presents** with the merged menu payload installed
  (`settings_menu: mounted 14 generated records over 0 provider records`), ~54 s to the title.
* **Ultimate mode boots and presents** with the payload refused by the new guard, ~46 s to the
  title. The provider itself is unaffected: `ultimate: provider compatible: 6/6 records, 8 panels,
  10 values`, the payload overlay is mounted, and the boot identity is intact.
* The early-resolution path still runs before graphics setup (`resolution_scale`, `native_2x_msaa`,
  `swap_post_effect`, `anisotropic_override` written pre-presentation; the graphics-boot marker ends
  `pending = false` with `last_good` filled in).
* The safe-graphics route and the recovery marker still behave as the handover describes.

## What is NOT verified (and is the remaining work)

Ordered by what blocks a release:

1. **The Ultimate menu payload.** Until hypothesis A or B above is settled, an Ultimate install has
   no merged Help & Options screen. This is the single largest open item, and it is now a *guard*,
   not a silent removal — the boot log says so by name. Everything the plan describes for the
   merged screen (rows, editing, prompts, target changes) therefore has runtime coverage in base
   mode only.
2. **The interactive matrix from the handover, still unrun**: selector A/Left-Right/B/A editing,
   the editing indicator, boolean save, live VSync, restart close/accept/failure, target changes,
   direct-boot readback, provider saves, Unlock All timing, and in-song navigation through every
   child screen and back. `scripts/drive_ui.ps1` + `capture_window.ps1` are the tools; the
   automation caveats are in the handover.
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
   hidden-DLC refresh access, installer/distribution checks.
8. **Docs.** `in-game-settings-remaining-work.md` still carries claims this session contradicts
   (it says graphics recovery "is not wired"; it is). Rewrite it against the evidence above and
   fold in the guard.

## Housekeeping

* The real `%APPDATA%\rb_blitz\settings.toml` was touched by one early misconfigured arm: a saved
  manual resolution was switched to `auto` with automatic provenance (the documented
  incompatible-display policy) and the `.graphics-boot.toml`/lock siblings were created. On this
  2560x1080 display Auto resolves to 2560x1080, i.e. the same pixels; set it back by hand if a
  manual entry is wanted.
* Temporary investigation code (a `/MAP` link option, `RBBZ_SKIP_MENU_PAYLOAD`,
  `RBBZ_SKIP_PROVIDER_REROUTE`, `RBBZ_SKIP_RECORDS`, and the read/open diagnostics) was **removed**;
  `git status` shows only the four files in the table above plus this document.
* Scratch: `out/_swap/settings-audit/` (`stall-probe.ps1`, `boot-check.ps1`, `resolve_map.py`,
  one directory per arm with its log and captures). Nothing here is acceptance evidence.
