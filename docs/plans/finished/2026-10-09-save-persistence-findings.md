# Save persistence — findings, evidence, and handoff

Date: 2026-10-09 / 2026-10-10. Author: previous agent (handoff document).
Scope: "loading a save game is not working / nothing is persistent (stars, modified controller
scheme, etc.)" on `rb-blitz-xenon-recomp` (ReXGlue static recompilation of Rock Band Blitz,
title id `5841122D`).

**Status (2026-10-11): local persistence implemented, root-caused and verified in-game.**
The implementation and code evidence below supersede the earlier investigation's
ranked leads. Sections 1–9 remain the historical handoff, not current conclusions.

## Root cause of "it still does not restore" (2026-10-10, measured)

The first implementation restored the checkpoint once, right after the guest's
`SaveData` initialization. That is the wrong moment: the guest tears the player's
`SaveData` down whenever it (re)loads the save from Rock Central and then waits for
the `save_data` response that offline never arrives.

* `sub_822CB0D0` is that reload path. It releases the object at `SaveData+44`
  (`sub_821CA3D8`) and clears the `SaveData` itself (`sub_821CA358`).
* Instrumenting `sub_821CA358` gave the timeline: restore at `13:17:12`
  (`guest loader read 40 of 40`, mapping 1), teardown at `13:17:35` with
  `lr=0x821CA41C` — the exact moment the title screen was confirmed. The object was
  empty for the rest of the session, which is why the song list drew no stars and the
  controller row read as default.
* It also cost the checkpoint: the next dirty `SaveLoadManager::Poll` serialized the
  emptied object, replacing the 88-byte file that held the score and five stars with a
  72-byte one at `13:08:43` (40-byte payload → 24-byte payload).

Fix (both in this revision):

1. `src/hooks/local_save.cpp` overrides `sub_821CA3D8` and re-applies the checkpoint
   immediately after the guest's teardown — the local equivalent of the `save_data`
   response. The restore path is one shared `Restore()` used by both hooks.
2. `Checkpoint` refuses to overwrite a payload that carries scores or stars with one
   that carries neither, and says so in the log. The decision is `Summarize()` in
   `src/fs/local_save.h` and is unit-tested.

Ruled out along the way, with evidence — do not re-walk these:

* **Stream type or endianness flag.** `SaveData::Load` reads through `0x824614F8`,
  which byte-swaps only when `*(stream + 4) != 0`; a `BufStream` over the file's own
  guest-order bytes (constructor flag 0) is the correct pairing. Guest memory is
  big-endian (`REX_LOAD_U32(x) = bswap32(*(base + x))`, `base` = host `0x100000000`,
  verified by reading the decrypted image's `MZ` at guest `0x82000000`).
* **Another writer of the SaveData global (`0x82849890`).** Exactly one `stw` targets
  it, in the initializer `sub_821CA620`.
* **A wrong `kSaveData` constant.** It matches the initializer's disassembly, and the
  measured `guest loader read 40 of 40` plus `mapping=1` afterwards proves the object
  the hook loads into is the live one.
* **`songs_download\` / DLC registration.** Not this bug and not reproducible here: a
  `--dlc_root` pointed at the flat library registers 1402 packages and the
  `songs_download` probes log zero failures.

Verified after the fix (same save root across a full process restart): the log shows
`restored 40 bytes at initialization` and `restored 40 bytes after the guest's
SaveData teardown`, and the five stars for *September* (song id `1020013`) are visible
in **YOUR SONGS**.

## Implementation follow-up (2026-10-10)

The missing player save has a different destination from `songcache` and
`globaloptions`: **Rock Central**. The guest function at `0x8235BC08` constructs
`remoteconfig/updatesavedata/`, serializes `SaveData` through `0x821CA2A0`, and
places the result in a `savedata/set` request. Its response-side counterpart
`0x8235BAA8` reads `save_data` into the native loader. Offline play therefore
has no local write path for that object. A populated song cache proves DLC
metadata persistence, not player-progress persistence.

Code now added:

- [src/hooks/local_save.cpp](../../../src/hooks/local_save.cpp) restores the player
  object after its initialization (`0x821CA620`). Controller mapping changes,
  star-rating updates, high-score changes, and the two star-counter mutations
  mark it dirty. The next `SaveLoadManager::Poll` checkpoints it with the
  game's own revision-11 serializer. Reload uses its own `SaveData::Load`
  (`0x821CA740`), through a guest `BufStream`; raw pointers are never persisted.
- Saves live at `<user_data_root>/<XUID>/5841122D/local_save.bin`, normally
  `Documents/Rock Band Blitz/B13EBABEBABEBABE/5841122D/local_save.bin`.
  This uses the runtime's resolved writable root and current emulated XUID.
- [src/fs/local_save.h](../../../src/fs/local_save.h) checks the envelope version,
  length, SHA-256, and complete native count/record layout before the guest
  sees a file. Writes use a sibling temporary file and replace the destination
  without first deleting the last save. Failed writes retain dirty state and
  retry after five seconds. Invalid or unreadable existing saves are preserved,
  logged, and protected from overwrite for that session. To recover, close the
  game, back up/move that file, and relaunch; it is not silently discarded.
- Audio/video settings are **console-backed**, including overscan and bass
  boost. `ConsoleSaveData::Handle` (`0x821C99F8`) sets the same dirty byte for
  these changes as for calibration. The hook queues the native autosave while
  the save manager is idle and initial loading has finished, leaving its
  `globaloptions` serialization and content I/O in charge of the actual write.

The compatibility flag **`local_save` defaults to `true`**. To compare retail
offline behavior, launch with `--local_save=false` (or put `local_save = false`
at the top level of `rb_blitz.toml`). Disabled hooks call their original guest
implementations without touching local progress. The flag is independent of
Ultimate and the DLC-cache enhancement; existing `globaloptions` and
`songcache` files retain their formats.

Two corrections to the earlier DLC hypothesis:

1. `src/hooks/dlc.cpp` already scans a flat `--dlc_root` as a library;
   `--dlc_library` is not required for that case.
2. `cache/dlc_library.cache` is only produced with
   `enhancements_dlc_cache=true`. Its absence does not prove failed registration.
   Failed optional `songs_download` probes alone do not prove an empty song list.

**Validation so far:** the Release build completed and all 20 host tests passed,
including save-envelope round trips, corrupt/truncated data rejection, replacement,
and failed-write preservation. These checks ran before the user requested to
handle testing themselves. No in-game run of the new hooks was performed.
Guest stream calls, actual startup ordering, and native autosave completion still
need runtime validation; this is not a claimed successful game round trip.

Suggested manual checks, using the same save root before and after a full process
restart:

1. Change controller mapping, return to the menu, and restart. The mapping should
   remain selected. The log should contain `local_save: saved ...`, then
   `local_save: loaded ...` on the next launch.
2. Toggle overscan and bass boost, wait for saving to finish, restart, and inspect
   both settings. Repeat the existing calibration round trip to check regression.
3. Finish a song with a nonzero score/star rating, return to the song list, and
   restart. Check that song's score/stars and controller mapping. Also replay with
   a lower score to verify the original high score remains.
4. Repeat with the Ultimate target and a DLC song. For an isolated run, pass an
   **absolute** `--user_data_root` and use info logging with a large library.

Earlier sessions whose progress was never written cannot be recovered from the
song metadata cache. This change stores progress produced from now on.

---

---

## 1. TL;DR

* The SDK's content/write layer works: calibration `CONTINUE` rewrites `globaloptions`
  (`[NtCreateFile] disp=0x5` + a 1024-byte `[NtWriteFile]`) and it round-trips across a restart.
  `scripts/acceptance/acceptance_save.ps1` is that measurement.
* The title's save files **do** get written in the normal (full DLC library) configuration:
  `songcache` = 1,009,556 bytes written 2026-10-09 **17:49:31**, `globaloptions` = 1,024 bytes
  written **17:55:57** in the real root. I decoded the cache: version 11, count **1402**,
  artist-sorted `Artist - Title` records. Only the title's `SongMgr` produces that format.
* Every isolated reproduction I ran wrote **nothing** to those files. The reason is not the
  title: my runs never got the DLC library registered, so the title saw **zero** songs and wrote
  an **empty** (16-byte) cache. In the full-library run the title probed
  `<content>:\songs_download\` for **2804** content items and all 2804 opens returned
  `0xC000000F` (STATUS_NO_SUCH_FILE).
* Prime remaining lead: **`songs_download\` is not exposed inside the enumerated DLC content
  items** (`src/fs/*`, `src/hooks/dlc.cpp`), so the title cannot read the DLC song list. This is
  the most testable, most likely root cause of "the library/progress looks empty".
* Second, independent, already-documented issue: settings that are *profile-backed* (controller
  scheme, overscan, bass boost) change on screen but never write the save file (measured
  2026-10-03, recorded in `docs/reference/known-issues.md`). Stars (`SongProgressMgr`) have the same
  shape: no third content root is ever created, and the guest imports
  `XamUserReadProfileSettings` but never `XamUserWriteProfileSettings`.

---

## 2. Environment

* OS Windows. Repo `d:\Gabriel\Documentos\Coding\decomps\360\rb-blitz-xenon-recomp`.
* Build: `cmake --preset win-amd64-release` then `cmake --build --preset win-amd64-release`
  → `out\build\win-amd64-release\{rb_blitz.exe, rexruntime.dll, rexgpu-xenos.dll, rb_blitz_controller_settings.exe}`.
* Tests: `ctest --test-dir out\build\win-amd64-release --output-on-failure` (19 tests, ~2.5 s).
* SDK submodule state: `scripts\setup\apply_sdk_patches.ps1` → `35 patched, 0 UNEXPECTED, 0 untracked`,
  exit 0. **The SDK work tree is clean** — every diagnostic I added was reverted
  (`src/kernel/xam/xam_content.cpp`, `src/system/xnotifylistener.cpp`, `src/system/kernel_state.cpp`).
* Launcher profile `%APPDATA%\rb_blitz\launcher.toml`:
  `target = "ultimate"`, `user_data_dir = ""`, `dlc_root = "F:\\Games\\Rock Band DLCs\\360 (CON)\\Singles"`
  (1402 packages), `dlc_scan_threads = 4`, `resolution_scale = 2`.
* Real save root (`Documents` is redirected): `D:\Gabriel\Documentos\Rock Band Blitz`.
  Do not disturb it; all my experiments used isolated roots under `out\_swap\`.

---

## 3. Confirmed facts (with evidence)

### 3.1 On-disk content layout the title creates

```
<root>\B13EBABEBABEBABE\5841122D\00000001\globaloptions\globaloptions     1024 B
<root>\B13EBABEBABEBABE\5841122D\00000001\songcache\songcache               16 B (empty) or 1,009,556 B (full)
<root>\B13EBABEBABEBABE\5841122D\Headers\00000001\globaloptions.header      328 B
<root>\B13EBABEBABEBABE\5841122D\Headers\00000001\songcache.header          328 B
<root>\cache\dlc_library.cache                                         164,009 B   <- written by THIS PROJECT, not the title
<root>\cache\shaders\shareable\5841122D.{xsh,rtv.d3d12.xpso}                       <- written on shutdown
<root>\achievements\5841122D.toml                                                  <- written by THIS PROJECT
```

`B13EBABEBABEBABE` is the shimmed XUID, `5841122D` the title id, `00000001` the content type
(`kSavedGame`, `flags = 0x14`, OPEN_ALWAYS). The two content display names are in the headers and
decode cleanly from UTF-16:

* `globaloptions.header` → `Rock Band Blitz Audio/Video Settings`
* `songcache.header`     → `Rock Band Blitz Library File`

### 3.2 The 1 MB song cache is genuine title output

Decoded from the real root (hex + ASCII):

```
len = 1009556
0b 00 00 00           u32 version = 11
7a 05 00 00           u32 count   = 1402
19 00 00 00 "4 Non Blondes - What's Up"                        01 00 00 00 9f 13 00 00
2a 00 00 00 "5 Seconds of Summer - She Looks So Perfect"       01 00 00 00 ...
```

i.e. `[u32 len][ASCII "Artist - Title"][u32 1][u32 varies]` records, artist-sorted. Nothing in
this repo writes that file (`scripts\acceptance\acceptance_song.ps1` and `acceptance_persistence.ps1` do not
copy or seed it), so the title wrote it.

### 3.3 `globaloptions` is a real state carrier

* 1024 bytes: `03 00 00 00` at 0x00, `05 00 00 00` at 0x0C and 0x10.
* From 0x18 onward it contains a `(cos θ, sin θ)` unit-circle table stepping `2π/4096` per entry
  — consistent with the calibration page's stick-circle data, not per-song progress.
* Two independent fresh boots differ in **exactly 1 byte** of 1024 (deterministic).
* The user's played-in file differs from a fresh one in **1002 bytes**.

### 3.4 Sign-in / notification delivery is not the problem

The title registers a notification listener with `mask = 0x1` and receives, at startup,
`XN_SYS_SIGNINCHANGED (0x0A)` twice with `data = 1`, plus `XN_SYS_UI`, `XN_SYS_INPUTDEVICESCHANGED`,
`XN_SYS_INPUTDEVICECONFIGCHANGED`. So "the engine never learns a user signed in" is **disproved**.

### 3.5 Guest imports (what the title could ever call)

Imports: `XamContentCreateEx`, `XamContentCreateEnumerator`, `XamContentGetCreator`,
`XamContentDelete`, `XamContentFlush`, `XamShowDeviceSelectorUI`, `XamUserGetSigninState`,
`XamUserGetSigninInfo`, `XamUserGetXUID`, `XamUserReadProfileSettings`, `XamNotifyCreateListener`,
`XNotifyGetNext`, `DbgPrint`, `NtCreateFile`, `NtOpenFile`, `NtQueryFullAttributesFile`, …
**Not imported: `XamUserWriteProfileSettings`.** The title's only write path is a content file.

The title's `DbgPrint` calls produce no output (shipping build), and there is no engine debug-log
file, so the engine cannot narrate its decisions.

### 3.6 The title's DLC discovery probe fails 100% in my runs

From the full-library run's log (`out\build\win-amd64-release\logs\rb_blitz_001.log`, 1.2 MB):

```
total songs_download lines: 2804
distinct result codes:      0xc000000f x2804        (STATUS_NO_SUCH_FILE)
[warning] [krnl] [t23388] [NtCreateFile] FAILED: path='cnt00000000:\songs_download\' -> 0xc000000f
[warning] [krnl] [t23388] [NtCreateFile] FAILED: path='cnt00000001:\songs_download\' -> 0xc000000f
... through cnt00000af3 (2804 items, = 2 x 1402 packages) ...
```

`cache\dlc_library.cache` (our own scan result) was **absent** from that isolated root, and from
every isolated root I produced; it is present in the real root (164,009 B, 17:38:07). So in my
runs the DLC library was never registered and the title's per-package `songs_download\` probe
failed for every item — after which the title caches an empty song list.

### 3.7 Shutdown path

A route ending in the title's own **EXIT GAME** (confirm the dialog) terminates through the guest:
log tail shows `KernelState::TerminateTitle` then `[info] [core] Execution complete`. Exit code is
always `0xC0000374` (STATUS_HEAP_CORRUPTION) after `Execution complete` — a separate teardown
observation, present in every run, not investigated further.

---

## 4. Conclusions I retract (do not re-walk these)

1. **"The game never writes progress / it is not a format problem."** Retracted. The title wrote a
   1 MB, 1402-record cache and later rewrote `globaloptions` in the real root on 2026-10-09.
   My "nothing was written" results were produced by runs where the title saw **zero songs**
   (see 3.6). The empty 16-byte cache *is* the title writing — it has nothing to write.
2. **"A full song run wrote nothing."** That run used `--dlc_root` pointing at an empty/3-package
   library, and additionally wrote into a doubled path
   (`out\build\win-amd64-release\out\_swap\song-root\`) because `--user_data_root` was passed
   relative; the probe resolves relative paths against the game's working directory (the build
   dir), not the repo. **Always pass an absolute `--user_data_root`.**
3. **"Debug logging is harmless."** With `--log_level=debug` every file read is logged; the scan
   of 1402 packages never finishes inside a run, so the title never completes discovery
   (`scripts\acceptance\acceptance_song.ps1` with the full library sat on
   `BLITZ DISCOVERING DOWNLOADABLE CONTENT` for 18 minutes and timed out on every screen).
   Use `--log_level=info` for any run that has to reach the song list.

---

## 5. Open leads, ranked

1. **`songs_download\` (most promising).** The title enumerates content items (`cnt%08x:`) and
   opens `\songs_download\` on each to read DLC song data. 2804/2804 attempts failed with
   `0xC000000F` in my run. Either (a) the flag I used (`--dlc_root`) is not the flag that
   registers a *library* (`--dlc_library`, see `scripts\acceptance\acceptance_song.ps1 -DlcLibrary` /
   `-DlcLibraryContentType`), or (b) the overlay does not expose `songs_download\` inside a
   package. Look at `src/fs/dlc_library.h`, `src/fs/dlc_cache.h`, `src/fs/payload_overlay.cpp`,
   `src/fs/overlay_merge.h`, `src/hooks/dlc.cpp`, `src/hooks/dlc_refresh.cpp` and the
   `cache\dlc_library.cache` producer. **Verify first** by re-running with the library flag and
   checking that `cache\dlc_library.cache` appears and that `songcache` grows past 16 bytes.
2. **Why the per-package probe fails** even though enumeration succeeds (2804 = 2 x 1402 items —
   the enumeration may be double-counting; worth explaining).
3. **Per-player progress (stars) has no home.** Only two content roots are ever created, and the
   guest cannot write the Xbox profile. If stars are supposed to be persisted, the write path has
   to be found (engine side) or supplied (host hook).
4. **Profile-backed settings never save** (`docs/reference/known-issues.md`, "Some settings commit, some only
   change the screen", measured 2026-10-03): controller scheme / overscan / bass boost change on
   screen and never write, while calibration CONTINUE does write. Unresolved; my measurements
   only extend it (a settings change plus the title's own EXIT GAME also wrote nothing).

---

## 6. Where a fix would go

| surface | writes today? | fix site |
|---|---|---|
| `songcache` (song list) | yes, once the library is really registered | none — fix the library registration instead |
| `globaloptions` (calibration, some settings) | yes (`scripts\acceptance\acceptance_save.ps1`) | none |
| DLC song data (`songs_download\`) | **no — 2804/2804 fail** | `src/fs/*` overlay / `src/hooks/dlc.cpp` |
| stars / profile-backed settings | not observed anywhere | engine-side gate (find it) or a host hook in `src/hooks/` |

A host hook (what the user originally proposed) would follow the existing pattern: a new file in
`src/hooks/` next to `content_progress.cpp` / `dlc_refresh.cpp` / `ultimate.cpp`, registered the
same way, gated by an `enhancements_*` cvar declared in `src/enhancements.cpp`, and calling the
engine's own save routine. Engine entry points collected from the decrypted image and the RB3
source cross-reference (`docs/reference/rb3-references.md`):

```
vtables: SaveLoadManager 0x82014AF4   SongMgr 0x82016BC4   SongProgress 0x8201757C
         SongProgressMgr 0x82017634   LevelProgress 0x8201783C
         MemcardXbox 0x82060374       CacheMgrXbox 0x82090210
functions: SaveLoadManager::{AutoSave, IsReasonToAutosave, Poll}, ProfileMgr::GetGlobalOptionsSize
           (= TheModifierMgr->SaveSize(gRev) + 0x52), SaveGlobalOptions,
           kSongMgr_SaveMount / SaveUnmount / SaveWrite, enumerate_save_game_exports
cache names: kStrGlobalCacheName = "globaloptions", TheSongMgr.GetCachedSongInfoName() = "songcache"
default.dtb ignored_content = {BlitzUnlockerbySynchrony, rbsongcache, rb2songcache, band, band3,
                               netcache, "Song Export", globaloptions, songcache}
```

Decrypted 10,747,904-byte guest image for further string/vtable work:
`out\_swap\blitz_image.bin`.

---

## 7. Tooling and artifacts

* `out\_swap\save-probe.ps1` (untracked, gitignored) — `-Mode Run|Snapshot|Diff`, `-Root` (absolute!),
  `-Route` (comma-joined `drive_ui` actions), `-GameArgs`. Run mode clears logs, snapshots the root
  (hash/size/mtime + byte copy), launches
  `rb_blitz.exe --game_data_root=… --ultimate_mode=N --user_data_root=… …`, optionally drives the UI,
  waits for exit, re-snapshots and diffs.
  **Two known defects, both of which produced misleading output for me:**
  1. When the "before" inventory has 0 files, `$lines | Set-Content` does not truncate
     `save-before.txt`, so the *previous* run's inventory is reused and the ADDED/REWRITTEN labels
     are wrong (the "is new (…), first 96" byte dumps are still trustworthy).
  2. The `cache mounts` and `song/DLC packages opened` counters grep for SDK diagnostics
     (`content#create`, `content#enum`) that I **reverted**, so they print `0` always. Use the
     `[debug] [fs] … Registered symbolic link: <name>: => \Device\Content\N\` lines instead.
* `scripts\diagnostics\drive_ui.ps1` actions used: `wait:N`, `key:<name>` (`a`, `b`, `lstick_up`,
  `lstick_down`, `lstick_right`, `dpad_right`), `shot:<file>.png`. Measured route to the title's own
  exit: 8 x `key:lstick_up`, then 5 x `key:lstick_down`, then `key:a` (EXIT GAME), then `key:a`
  (confirm).
* Existing harnesses worth reusing: `scripts\acceptance\acceptance_save.ps1` (calibration commit → the
  `disp=0x5` + 1024-byte write), `scripts\acceptance\acceptance_persistence.ps1` (5 storage cases against an
  isolated `--user_data_root`, incl. corrupt payload/header), `scripts\acceptance\acceptance_song.ps1`
  (drives a full song to the results screen; OCR-driven, fails loudly),
  `scripts\diagnostics\ocr_image.ps1`, `scripts\diagnostics\capture_window.ps1`.
* Isolated roots left behind: `out\_swap\ctl-root` (nodlc), `out\_swap\fresh-dlc` (3 packages),
  `out\_swap\full2` (full library via junction, info logging), `out\_swap\full-lib`
  (acceptance_song, full library, debug logging), `out\_swap\dlcn` (3 packages),
  `out\_swap\dlc-link` (junction → `F:\Games\Rock Band DLCs\360 (CON)\Singles`; use a junction to
  avoid spaces, because the probe splits `-GameArgs` on whitespace).
* Logs: `out\build\win-amd64-release\logs\rb_blitz_001.log` (the probe deletes logs at the start of
  every run, so early sessions are gone).

### Uncommitted / temporary state of the tree

* `scripts\acceptance\acceptance_song.ps1` — **modified (kept, not committed)**: added
  `[string]$UserDataRoot = ""` and `if ($UserDataRoot) { $launchArgs += "--user_data_root=""$UserDataRoot""" }`.
  Rationale: without it the harness writes into the user's **real** save root (that is how the
  real root accumulated its 1 MB cache). Revert or keep as you prefer.
* `out\_swap\**` — untracked/gitignored (probe, snapshots, byte copies, screenshots, decoders).
* Everything under `rexglue-sdk/` — reverted to clean; verify with
  `powershell -File scripts\setup\apply_sdk_patches.ps1`.

---

## 8. Suggested next experiments (exact commands)

```powershell
# 1. Register the library properly (the flag my runs were missing) and see if the cache is written.
#    -DlcLibrary is a *flat library*; -DlcRoot alone was not enough in my runs.
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\acceptance\acceptance_song.ps1 `
  -Runs 1 -Song 3 -SongName "THESE DAYS" `
  -DlcRoot "F:\Games\Rock Band DLCs\360 (CON)\Singles" `
  -DlcLibrary "F:\Games\Rock Band DLCs\360 (CON)\Singles" `
  -UserDataRoot "<abs>\out\_swap\lib2" -OutDir "out\_swap\lib2-run"
#    Expect: cache\dlc_library.cache appears in the root, and the run reaches the song list.
#    Check:  Get-ChildItem out\_swap\lib2 -Recurse -File | Select Name,Length,LastWriteTime

# 2. Count the songs_download probe outcome in that run's log (should stop being 2804 x 0xc000000f).
$l = (Get-ChildItem out\build\win-amd64-release\logs\*.log | Sort LastWriteTime -Desc | Select -First 1).FullName
Select-String -Path $l -Pattern 'songs_download' |
  ForEach-Object { if ($_.Line -match '-> (0x[0-9a-fA-F]+)') { $matches[1] } else { 'ok' } } |
  Group-Object | Sort Count -Desc | Select Count,Name

# 3. Only once the library is really registered: does the engine's cache appear, and does playing a
#    song change anything (stars)? Use info logging, absolute root, and diff with save-probe.
powershell -NoProfile -ExecutionPolicy Bypass -File out\_swap\save-probe.ps1 -Mode Run `
  -Root "<abs>\out\_swap\lib3" `
  -Route "wait:780,key:a,wait:10,wait:5,shot:m.png,<8 x key:lstick_up>,<5 x key:lstick_down>,key:a,wait:4,key:a,wait:12" `
  -GameArgs "--dlc_root=<abs>\out\_swap\dlc-link --log_level=info --log_flush_interval=1 --log_max_file_size_mb=200"
```

Then, in order of value:

4. Compare a *successful* (real-root) session's log for `songs_download` outcomes against the
   failing run — that single difference decides between "wrong flag" and "overlay does not expose
   the folder".
5. If the library loads and the cache is written but stars still do not persist, instrument the
   engine's save entry points (`SaveLoadManager` vtable `0x82014AF4`, `AutoSave`,
   `SaveGlobalOptions`) or add the host hook described in §6, and record the result in
   `docs/reference/known-issues.md` (the entry "Some settings commit, some only change the screen") and
   `docs/history/bringup-log.md`, in the lab-notebook style the repo already uses.

## 9. Honest summary of what went wrong in this investigation

I concluded "the title never writes" from runs that could not possibly have produced a write,
because I pointed `--dlc_root` at an empty or 3-package library and used debug logging that made
the DLC scan slower than the run. I also read "file unchanged" as "the title does not save" without
first confirming that the title had anything to save. The measurements are still useful — the file
formats, the content layout, the notification evidence, the `songs_download` failure counts — but
the headline conclusion drawn from them was wrong and has been retracted in §4.
