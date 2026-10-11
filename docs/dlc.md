# DLC

Where downloadable content goes, and what the guest does with it.

Blitz does not read a DLC directory of its own: it enumerates content through the
emulated Xbox content APIs and opens what it found, and the SDK's content manager
serves both halves. A package therefore has to sit where that manager looks, and the
rules below are
[rexglue-sdk/src/system/xam/content_manager.cpp](../rexglue-sdk/src/system/xam/content_manager.cpp).
The directory described in §2 is registered by [src/hooks/dlc.cpp](../src/hooks/dlc.cpp),
and its shape is pinned by [tests/dlc_layout_tests.cpp](../tests/dlc_layout_tests.cpp).

## 1. What the guest does

- **Enumeration.** The title resolves `XamContentAggregateCreateEnumerator` at boot
  rather than importing it: the import table
  ([generated/default/rb_blitz_init.cpp](../generated/default/rb_blitz_init.cpp)) lists
  `XamContentCreateEnumerator` and not the aggregate form, which is why RB3DX has to
  force-import it so it stays linked — [rb3-references.md](rb3-references.md) §4. The
  SDK's
  [xam_content_aggregate.cpp](../rexglue-sdk/src/kernel/xam/xam_content_aggregate.cpp)
  answers with one item per package, for the running title **and for the XEX's
  alternate title ids**: optional header `0x000407FF` of this dump reads
  `45410829, 45410869, 45410914`, i.e. the earlier Rock Band titles and **RB3**. RB3
  DLC is in scope for Blitz because that list says so.
- **Opening.** `XamContentCreateEx` resolves the item by title id + content type +
  file name, mounts it, and exposes it as the guest's own root name (`songcache:`,
  `globaloptions:`, …). The device id (HDD/ODD) is not part of that identity: one of
  the SDK's own comments says it, *"device_id is a virtual storage selector, not a
  content identifier"*.
- **Marketplace content** (`00000002`) always resolves under the common xuid
  `0000000000000000`; every other type resolves under the profile's xuid. The DLC
  root has no xuid level, so one directory covers both.

## 2. Where packages go

```text
<game_data_root>/dlc/<title_id>/<content_type>/<package>
```

| Example (this machine's dump) | What it is |
| --- | --- |
| `dlc/45410914/00000002/65CC8CBB…3109177445` | RB3 DLC: one 803 MB `LIVE` (STFS) container |
| `dlc/5841122D/00009000/0000000818E2A242…5841122D` | Blitz's own packages: 20 `LIVE` containers, 78–230 KB each |

- A package is mounted **where it lies**, the way a console stores it: nothing is
  extracted and nothing is copied. A dumped content directory holds containers, not
  directories, which is what the SDK patch restores (upstream Xenia picks a
  container-backed package for a file and a directory-backed one for a directory) —
  [patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch](../patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch).
- An extracted directory works too — one directory per item, the form the SDK's own
  `InstallContent()` produces. That is what the save containers under
  `Documents\Rock Band Blitz` look like.
- The lookups are exact, and the SDK builds them with `{:08X}`, so the two directory
  levels are **8 upper-case hex digits**. A lower-case spelling, or a 16-digit level
  (the content root's own xuid level, copied in by mistake), is reported at boot and
  enumerates nothing.
- A file is content only when it starts with the STFS magic `CON `, `LIVE` or `PIRS`.
  Anything else in those directories — a readme, a MOGG, an ark — is reported, not
  silently skipped.

## 2.1 A flat library

A dumped song folder is not built like §2. It is thousands of loose `CON`/`LIVE`
containers with no `title_id`/`content_type` levels at all, and it is often on a
volume that cannot hold links (an exFAT drive, typically), so it
cannot be indexed into §2's shape without copying it. `--dlc_library` reads such a
folder where it lies instead, and a `--dlc_root` that is not the §2 layout is read
the same way:

```text
--dlc_library="D:\Songs"               # one or more folders, ';'-separated
```

The launcher offers the same two shapes for its *DLC location* row: the
`dlc_layout` rule accepts a structured `<title_id>/<content_type>/<package>` folder
**and** a flat library of loose containers, because a folder that is not the layout
is what the runtime above reads as a library
([launcher/src/path_validate.cpp](../launcher/src/path_validate.cpp)).

- Each container is read for **its own** title id and content type —
  `XContentMetadata::execution_info.title_id` and
  `XContentMetadata::content_type` — and placed under them, so nothing on disk is
  renamed, moved, linked or copied. The format's own spellings of both fields are in
  [stfs_xbox.h](../rexglue-sdk/include/rex/filesystem/devices/stfs_xbox.h), and
  `src/fs/dlc_library.h` reads the same offsets. The title id is used as it is; the
  content type is adapted, which the paragraphs below are about.
- The folder is walked **recursively**: nesting is fine, and directory symlinks are
  not followed (a loop would not terminate). Files that are not containers are
  counted, not reported; a library is not a layout, and a `badsongs/` folder or a
  readme in one is not a mistake.
- The name the guest receives and hands back to open an item must fit
  `XCONTENT_DATA::file_name_raw` — 42 bytes. A file whose own name is longer (most of
  a custom-song library is) keeps a UTF-8-safe prefix plus a short hash of the full
  name, so the same package gets the same name on every boot and two packages never
  collide. The name is only an identity: the songs' own titles come from the packages.
- Both sources are live at once. `--dlc_root` keeps registering §2's packages in
  place, the library adds its own, and the boot log states each. Naming `--dlc_root`
  as a flat folder is the same as naming it in `--dlc_library`, for the case where
  there is no §2 root at all.
- A container that carries the STFS magic but cannot be mounted (shorter than the
  header), or whose header names no title id, is reported with the reason and skipped.
- The library is read-only for the same reason §2's root is: no write path resolves
  into a package's host path. Nothing is installed or modified.

**The content type is adapted, and that is what makes a dumped folder usable.** A
custom-song library is packaged as **saved-game** content (`00000001`) — that is what
C3's RB3 `CON`s are — while the title reads its downloadable songs out of the
**marketplace** enumeration (`00000002`) and does not list a saved game at all.
Measured on this machine: with the packages presented as their own type, the guest
enumerated all 1401 of them, mounted every one, and added **none** to its song list;
presented as marketplace content, the same packages list and play. So a library
package whose header names saved-game content is presented as marketplace content, and
a package of any other type keeps its own.

**A library package with no license entries of its own is treated as licensed.** An
item served from §2's root or from a library has no `.header` file beside it — those
directories hold packages and nothing else — so its license can only be what the
container carries, and a package dumped unsigned (a custom `CON` has no license
entries at all) reads as *not licensed* to a title that asks the license of what it
just opened. That is exactly the gate Blitz used to list such a song and then refuse
to start it; the extra sources now answer with the configured `license_mask` — the
same value `XamContentGetLicenseMask` answers with, `1` by default — when the
container carries nothing. A container that does carry entries keeps them, and an
explicit `--license_mask=0` (the trial path) is returned unchanged.

`ContentManager::set_extra_content_root()` resolves §2 by building a path from the
title id and content type; a library item has no such path, so it is handed to
`ContentManager::set_extra_content_library()` instead, and both the enumerator and
the open path consult the same list. The host half — the recursive walk, the header
read and the name — is [src/fs/dlc_library.h](../src/fs/dlc_library.h), pinned by
[tests/dlc_library_tests.cpp](../tests/dlc_library_tests.cpp). A cold library's
headers are read on a pool, because that read is the whole cost of a scan; the walk and
the merge stay on the calling thread in walk order, so the result does not depend on how
many threads read it (§4.1).

## 3. Configuration

| Setting | Default | Effect |
| --- | --- | --- |
| `--dlc_root`, or `dlc_root` in `rb_blitz.toml` | `<game_data_root>/dlc` | root of the layout in §2; a relative value resolves against the game data root; changing it needs a relaunch |
| `--dlc_library`, or `dlc_library` in `rb_blitz.toml` | *(empty)* | `;`-separated folders of §2.1; a relative value resolves against the game data root; changing it needs a relaunch |
| `--dlc_library_content_type` | *(empty)* | present **every** library package under this 8-hex-digit content type, instead of §2.1's saved-game → marketplace adaptation; changing it needs a relaunch |
| `--dlc_scan_threads` | `1` | threads that read a §2.1 library's package headers: `1` (the default) reads them on the calling thread, `0` uses the machine's core count (capped at 8), and any other value is that many workers (§4.1). The launcher's *DLC scan threads* row bounds its slider by the machine's processor count. The scan's *result* is the same at any count; only how long a cold scan takes changes. Changing it needs a relaunch |
| `--enhancements_dlc_cache`, or `[enhancements] dlc_cache` in `rb_blitz.toml` | off | R7: persist the library enumeration (§4.1) and reuse it while the tree it describes is unchanged, and drop the emulator's per-package mount latency for the run; changing it needs a relaunch |
| `--refresh_dlc_cache` | off | R7: ignore the persisted enumeration this boot, re-scan the libraries and rewrite it. A one-shot read at boot; only meaningful with the toggle on. The main menu's last row asks for the same thing in a running title (§4.1) |

The sources are read-only by construction. Enumeration and opening resolve into them,
and every write path in the content manager (create, thumbnail, install, delete)
resolves against the content root instead, so the title cannot damage a package folder
and removing a folder stays a complete uninstall.

The boot log states what was found. The first line is what §2's two title directories
produce; the second is the same shape for a §2.1 library, grouped by the title ids and
content types its packages name; the third is what a misplaced file reports:

```text
[info] dlc: 21 package(s) for title(s) 45410914, 5841122D in D:\…\game\dlc (read-only, mounted in place)
[info] dlc: 1401 saved-game library package(s) presented as content type 00000002 so the title lists and plays them (dlc_library_content_type overrides this)
[info] dlc: 1402 package(s), 0 not a container, in 1402 file(s) walked (45410829/00000002 x252, 45410869/00000002 x214, 45410914/00000002 x936) in D:\Songs by their own headers (read-only, mounted in place)
[warn] dlc: ignoring rhythm/00000002/song.dat (not a content package: no CON/LIVE/PIRS magic)
```

An absent `dlc/` is the normal case and logs one line saying so, with no warning.

## 4. The other place content can live

The content manager's own root is the platform user directory, `Documents\Rock Band Blitz`,
laid out as `<xuid>/<title_id>/<content_type>/<name>/` with optional
`<xuid>/<title_id>/Headers/<content_type>/<name>.header` files beside it. The title's
own saves are there (`B13EBABEBABEBABEBE\5841122D\00000001\songcache`, `\globaloptions`),
it is writable, marketplace content in it belongs under `0000000000000000`, and
`--user_data_root` moves it. The DLC root exists so that game packages share neither
that directory nor its write access.

## 4.1 The library enumeration cache (R7)

A §2.1 library is read by opening every regular file and reading the 868-byte STFS
identity prefix out of each container
(`ReadPackageIdentity`, [src/fs/dlc_library.h](../src/fs/dlc_library.h)). On the
library used here (1,402 packages, 28.4 GB, exFAT) that open-and-read
takes ~0.7 s warm, every boot, before the guest even starts; a stat-only walk of the
same tree takes ~0.18 s. R7 stores the scan's *result* so the second number, plus a
comparison, is all a boot pays.

**A cold read is 500x a warm one, and it is what a scan actually costs.** Measured
2026-10-07 on the same library: a *first* open of each package costs **~16 ms per
file**, the same scan run again costs **~0.03 ms per file**, and the walk between them
is ~2 ms for all 1,402 entries. A 500x gap on the same bytes is not the SSD, and it is
charged *per file* rather than per byte: 2,170 never-touched FFmpeg sources of 10-100 KB
each measured the same 16.7 ms cold against 0.035 ms warm. It is whatever inspects a
file the antivirus has not seen yet — so a single-threaded cold walk of
that library is **~22 s** (measured), and the only ways past it are to not open
the files (the cache, below) or to overlap the opens.

**The scan reads the headers on a pool.** Because the cost is serialized per file and
independent of file size, opening several at once divides it: the production scan
measured **5.6x faster per file cold** (16.1 ms/file on one thread against 2.9 ms/file
on eight, on two disjoint never-touched trees), which is ~4 s for the library above
instead of ~22 s. `dlc_scan_threads` names the count — `1` (the default) keeps the
reads on the calling thread, `0` is the machine's core count capped at 8 — and it
cannot change the answer: the walk and the merge stay on one thread and in walk order,
so items, names, counters and rejections are identical at any count
([tests/dlc_library_tests.cpp](../tests/dlc_library_tests.cpp) pins that, and a warm
tree is unaffected either way). The same cost disappears outright if the library folder
is excluded from the antivirus's real-time scan; that is the machine's policy, not the
host's, and the pool is what the host can do by itself.

With `enhancements_dlc_cache` on, the resolved library list, the content-type override
and the scan's items are written to `<cache_root>/dlc_library.cache` (`cache_root` is
`<user_data_root>/cache` — the save folder's own cache directory, seen in
[src/rb_blitz_app.h](../src/rb_blitz_app.h) `OnConfigurePaths`). The next boot
fingerprints the tree — every regular file's root-relative path, size and modification
time, hashed in sorted-path order so directory iteration order cannot change it — and
reuses the cache only when the fingerprint, the roots and the override all match. Any
difference, a missing or damaged file, or an unreadable root is a **miss**: the library
is scanned exactly as it is today and the cache is rewritten, so a cache can never hide
a package. `--refresh_dlc_cache` forces that miss.

The cached items are what the host hands the content manager
(`set_extra_content_library()`); the guest's own enumeration, mounting and `songcache:`
are untouched, which is D8's rule for R7 — the cache is a host-side fast path over the
same answer, never a second authority (docs/engine/toggles.md). The format is in
[src/fs/dlc_cache.h](../src/fs/dlc_cache.h) and its round trip, fingerprint movement and
refusal paths are pinned by
[tests/dlc_cache_tests.cpp](../tests/dlc_cache_tests.cpp), SDK-free like the two DLC
tests beside it.

## 4.2 The rest of the boot a large library costs (R7)

The host's walk of the library is the smaller half of what a large library costs, and
R7 answers for both halves. Measured 2026-10-06 on that library (1,402
packages), release build, nothing cached:

| Part of the boot | Cost | R7's answer |
| --- | --- | --- |
| boot to the title screen | ~18-21 s | — (unchanged; the title's own startup) |
| the host's walk of the library | 14.5 s cold, ~180 ms for a stat-only walk | the persisted enumeration: 7 ms |
| the title's own discovery — *"Discovering Downloadable Content"*, between the start press and the menu | **662 s** | the mount latency below: 379 s |
| — of which: 2,803 deferred overlapped completions, each sleeping the SDK's fixed 100 ms | **280 s** | `deferred_overlapped_delay_ms = 0` |
| — of which: 2,798 STFS package mounts (2 per package: the title opens each package once per pass) and the guest's own work between them | ~382 s | unchanged — a package is still mounted and read |

The 100 ms is `KernelState::CompleteOverlappedDeferredEx`'s emulated storage latency
(`constexpr kDeferredOverlappedDelayMillis` before patch
[0011](../patches/README.md)), and `XamContentCreate` — the call a content mount goes
through — defers *the whole mount* through it. So with `enhancements_dlc_cache` on the
host sets `--deferred_overlapped_delay_ms=0` for the run
([src/hooks/dlc.cpp](../src/hooks/dlc.cpp)): 662 s becomes 379 s. Nothing else changes
— the completion still runs on the dispatch thread, still sets the overlapped, still
queues the completion routine — and with the toggle off the value is the SDK's own 100,
so the faithful boot is byte-for-byte the boot this project had before R7.

### The title caches this itself, and R7 is what lets it finish

`XamContentAggregateCreateEnumerator` is the guest's enumeration, and the title pays
for it exactly once: after a *complete* discovery it writes its own
`B13EBABEBABEBABE\5841122D\00000001\songcache\songcache` — 18 KB for an install with a
handful of songs, **1.1 MB** for this 1,402-package library. A boot that finds that
cache mounts **no package at all** and reaches the main menu in **57.5 s** (measured;
`0` `Loading STFS header file` lines), against 390 s for the cold boot above.

That cache is only written at the *end* of a discovery, which is what makes the two
halves of R7 matter together: on a 1,402-package library the cold discovery was
11 minutes and a boot that was interrupted never got its cache written, so every boot
paid the cold price again. With the host walk cached and the mount latency gone the
same first boot is ~6.3 minutes and the second is ~1 minute; with the title's cache
warm, R7's remaining contribution is the 14.5 s of host walk per boot (and every other
second of a warm boot is the title's own — §4.3).

## 4.3 What a warm boot still pays, and the one lever that shortens it (R7)

Measured 2026-10-06 back to back, same library, release build, warm `songcache` — so no
package is mounted in any run below — each run a launch → start press → main menu
walk driven through [scripts/drive_ui.ps1](../scripts/drive_ui.ps1), the probe that also
produced §4.2's numbers (it polls the screen every 3 s, so each figure carries ±3 s):

| Boot | launch → title screen | title screen → main menu | launch → main menu |
| --- | --- | --- | --- |
| `enhancements_dlc_cache=1` — R7's boot | 22.9 s | 54.4 s | **78.8 s** |
| `enhancements_dlc_cache=0` — the same boot, walked again | 39.8 s | 63.5 s | **104.7 s** |

Of that 25.9 s difference the walk itself is 14.5 s (§4.1); the rest is the walk's
pressure on the title's own reads of its `songcache` and the ark, which is why the
title screen alone arrives 16.9 s earlier.

The 54.4 s in the middle of R7's own boot is not host I/O: it is 2,809
`XamAppEnumerateContentAggregate` calls — 2 per package, one per **presented frame**.
With V-Sync on they arrive every 16.67 ms (2,809 × 16.67 ms ≈ 47 s of the phase), and
the host's own share is small — 5.4 µs per call on the app message the item arrives on,
plus the XAM task the item schedules, ~1.6 s of the 47 s — so the wait is the title's
frame, not our walk and not our call. No cache can shorten that, because the title
decides how many items a frame is worth.

**Asking for more items per call does not help, and that is measured rather than assumed**
(2026-10-10). The aggregate enumerator is created with `items_per_enumerate = 1`
(`rexglue-sdk/src/kernel/xam/xam_content_aggregate.cpp`), and the SDK publishes that number
to the guest in `X_KENUMERATOR` — so the guest can be told to expect more. Built for **4**,
the title does read it back (the message's field 5 carries `00000004` where the baseline
boot carries `00000001`) and still hands over **512 bytes** as its reply buffer, in all
2,807 calls, exactly as before. Two items cannot fit in 512 bytes even at the smallest item
this message can carry (2 × 308 > 512), so a host that bounded its own write by the guest's
buffer — the guard a spike added to `WriteItems` — would still hand over one item per call:
the phase came in at **46.7 s and 2,807 calls**, identical to the baseline, with the
enumerator offering four and reporting the true count. One item per call is therefore part
of the protocol as this title uses it, and the host has no way to enlarge a buffer the guest
owns. [song-cache-plan.md](plans/song-cache-plan.md) §13 has the spike and its measurement.

The lever on it is therefore the frame rate — the launcher's own **Graphics ▸ Window ▸ V-Sync**
row (`vsync`, re-read live by the GPU vsync worker): with `--vsync=false` the same 2,809 calls
arrive every **7.0 ms** and the phase takes **19.7 s**, against 47 s.

**R7 can pull that lever for the player, for exactly as long as the phase lasts** (2026-10-10,
[song-cache-plan.md](plans/song-cache-plan.md) §14). `enhancements_dlc_uncap_enumeration` (off by
default) turns `vsync` down when the discovery's first item is counted and puts the player's own value
back at the first of three signs that the enumeration is over: the last item counted, the engine's own
completion, or a menu frame with no item counted for two seconds. Measured on the same 1,402-package
library, over the same 2,807-item walk: **46.7 s → 15.96 s** (5.68 ms per item), the value read as
`true` and written back as `true` **1 ms** after the last item and 104 ms before the engine's own
completion, so no menu frame is ever presented uncapped. Both the read and the write happen on a host
thread — a guest thread must not touch the cvar registry — and the call count is unchanged at 2,807,
so nothing is skipped: the *title* still enumerates everything. The V-Sync row is still the player's:
nothing sets it at boot and nothing leaves it changed, which is why this is its own option rather than
something R7 does by itself.

Three measured facts, in the order they should weigh.

- **Gameplay timing is not frame-paced.** [scripts/acceptance_song.ps1](../scripts/acceptance_song.ps1)
  with `REX_VSYNC=false` played the same song for **226 s** against 225 s with V-Sync on,
  with `Boot`, `SongList`, `Playing`, `Results` and `SongSeen` all true, no `[FATAL]` and a
  clean shutdown — so the title's song clock is its audio clock and a 2.4× faster frame
  rate does not move it. The loading loop and the song clock are two different clocks, and
  only the loop reads the frame.
- **The gain is real but host-dependent.** Repeated warm boots with `--vsync=false` came in
  at 49.3-76.6 s launch-to-menu against 78.8 s with V-Sync on: the phase is shorter every
  time, by how much is an hour-of-the-machine number
  ([known-issues.md](known-issues.md), "frame pacing" limit 4).
- **The fault that argued against recommending it is fixed** (2026-10-07). V-Sync-off runs
  died in `rex::system::XEvent::Set` with `event_ = -1`, which turned out to be an SDK
  use-after-free reachable in *any* boot but certain in a fast one: `ObDereferenceObject`
  created the object it was dereferencing and then released that creation's only reference,
  so an `XEvent` died while the guest's KEVENT still carried its handle and the slot was
  handed to a thread. [Patch 0012](../patches/rexglue-sdk/0012-stale-native-object-handles.patch)
  refuses a stashed handle that does not name an object of the type the dispatch header
  claims and no longer creates one in a dereference
  ([bringup-log.md](history/bringup-log.md) B-016). Since then 13 of 13 V-Sync-off boots are
  clean, and the song acceptance passes with it both off and on. R7 still does not set `vsync` at boot
  and does not recommend a value — the row belongs to the player — and the one place the host moves it
  is the phase-only option above, which puts the player's own value back before the menu is drawn — but
  this page no longer has a reason to warn about it.

**The refresh.** The main menu's last row
([main-menu-flow.md](engine/main-menu-flow.md) §7.1) asks the host to
re-run the scan and rewrite `<cache_root>/dlc_library.cache`, and `--refresh_dlc_cache`
does the same at boot: both make the persisted enumeration a *fresh* answer for the
boot about to use it. Neither touches the title's own `songcache`, which is the title's
business — a package the scan has just discovered is a package the title has not read
yet, so its own discovery reads it and extends its own cache.

**The refresh runs behind the frame, one at a time (measured 2026-10-10).** Reading every
container is the same per-file cost the cold boot pays, so an in-game refresh of the
1,402-package library is **30.2-31.0 s** — and it used to run on the thread that asked,
which is the frame, so the press froze the game for half a minute
([plans/song-cache-plan.md](plans/song-cache-plan.md) §9.5). It now runs on a worker, and
two rules bound it ([src/fs/refresh_state.h](../src/fs/refresh_state.h), pinned by
[tests/refresh_state_tests.cpp](../tests/refresh_state_tests.cpp)): **one at a time** — a
second press while the scan is in flight is logged and ignored, so a trigger that fires
twice cannot queue a second scan — and **published exactly once**, because two guest
threads can reach the publish point together. Measured on that boot: the press returned in
**1 ms**, the menu kept drawing and taking input for the whole scan, and the same tree
re-scanned in **77 ms** once it was warm.

**Where the refreshed list lands, and why not on the worker.** The result is handed to the
content manager from a *guest* thread, because ContentManager's
`extra_content_library_` is a plain `std::vector` with **no lock of its own**: a worker
writing it while the guest lists or resolves content would be a data race, and
`Configure()` writes it from a guest thread at boot for exactly that reason. The reliable
publish point is the main menu's own per-frame music poll
([src/hooks/shell_music.cpp](../src/hooks/shell_music.cpp), `ShellMusic::Poll` — the hook
R9 already installs), which runs every frame the menu is up **with R9 on or off**, because
it is the title's own music. Measured: the publish lands on the first frame after the scan
finishes, **~10 ms**, in both configurations. The file-call hooks publish too, but the main
menu makes **no file call at all** once the row is pressed, so they are a second point and
not the only one. A refresh that finishes while the menu is not drawing is published when
it next is; the cache file is rewritten on the worker either way, and that is the half the
next boot reads.

**The row the player presses.** The row's action becomes
`{do {file_exists "songcache:/rbbz_dlc_refresh"} "zzz…"}`: the title's own `file_exists`
on a name no file has, padded back to the action it replaces. `songcache:` is the part
that was measured: a name under the game-data mount (`d:/…`) is answered out of the ark
the title already has mapped and never becomes a guest file call at all, while a device
the title owns is asked through `NtCreateFile`/`NtQueryFullAttributesFile` — which is
where [src/hooks/dlc_refresh.cpp](../src/hooks/dlc_refresh.cpp) hears it and starts the
scan. Measured end to end on 2026-10-06 with the library
above: the press logged `refresh asked for from the main menu
("songcache:\rbbz_dlc_refresh")`, started the scan, and rewrote the
cache file (its modification time moved from the previous boot to the press); ~15 s in
that run, **30.2 s** on the same library on 2026-10-10, and no longer on the frame's
time (the paragraph above).

**What the row and the discovery screen say.** The row's action is what R7 rewrites, and
its *text* is rewritten too. Both labels R7 changes live in the game's own English locale
(`ui/locale/eng/gen/locale_keep.dtb`, 76,726 bytes), which is larger than one 64 KiB
archive read — so for a long time the row kept the label the title ships for it,
`DOWNLOAD CONTENT`, an online-store name for something that now refreshes a library.

Making that edit land took two corrections, both measured 2026-10-10
([plans/song-cache-plan.md](plans/song-cache-plan.md) §11). **(1) The continuation is an
archive offset, not a guest address.** The rule used to require the second read to land
immediately after the first *in guest memory*; the title reads its archive into a pool of
buffers, and the locale's second block arrived in a buffer **192 KiB away** from the end of
the first, so the file was refused on every boot. It is now identified by the archive offset,
assembled in a host buffer, and written back into the block the second read carries — only
that one, because the guest has already copied the first block out by then, and patching it
produced a file half old and half new (the label's four new bytes became a four-byte hole)
and a guest that dereferenced garbage. **(2) The bytes pay from the *last* `#ifdef HX_PS3`,
not the first.** The locale carries 21 of them; the first sits at byte 130, in the block that
cannot be patched, and the last at 75,628, in the block that can. Every one of them is
equally unused, so which one gives the bytes up costs nothing — only *that* it has not
already been read past.

The read that *completes* the file is the one the edit runs on
([src/hooks/menu_filter.cpp](../src/hooks/menu_filter.cpp) keeps the read that cut a `.dtb`
short and completes it when the next read resumes at the following archive offset). R7
rewrites:

| Entry | Ships as | R7 draws |
| --- | --- | --- |
| `splash_dlc` — the main menu row the refresh job is given | `Download Content` | `Refresh Song Library` |
| `server_connect_enumerating_content` — the connect panel's status line while it enumerates | `Discovering Downloadable Content` | `Loading Song Cache`, only when this boot's enumeration came out of the cache |

The second is conditional on `dlc: CacheServedThisBoot()`: when the host answered from the
file it wrote last boot there is no discovery happening, and the screen says what it is
really doing. Both edits are byte-neutral like the rest of the module (the label's growth
or shrinkage is paid for, or parked in, the name of an `#ifdef` the loader already skips)
and both retarget the title's content-database row for the file, so a patched locale is
still the file the title knows.

Both are **verified in game (2026-10-10, `out/drive-ui/p3d-*.png`)**: the main menu's last row
reads `REFRESH SONG LIBRARY` — it had read `DOWNLOAD CONTENT` for as long as R7 has existed,
and the boot log carries `relabelled the downloadable-content row "Download Content" to
"Refresh Song Library"` followed by `patched a file spanning two reads at ark offset
315592934` — and a boot whose enumeration came out of the cache draws `Loading Song Cache` on
the discovery screen instead of `Discovering Downloadable Content`.

**The progress bar.** The discovery screen also has a progress bar — on PS3 and on the
360 build alike, it is the same asset: `ui/net/gen/server_connect.milo_xbox` carries
`progress_bar.grp` and `content_percent.lbl`, and a Flow whose own comments read *"How
much content we've enumerated so far"* / *"How much content we need to enumerate, -1 if
we're not enumerating right now"*. Nothing in the DTA sets those two values, so the
engine's own code does — through one small object,
[src/hooks/content_progress.cpp](../src/hooks/content_progress.cpp) hooks
`SetTotal`/`Increment`/`Complete`. **Measured 2026-10-10
([plans/song-cache-plan.md](plans/song-cache-plan.md) §9.4, §12):** this build's engine calls
`SetTotal` exactly **once, after** the whole enumeration, with a total of **0** — which the
Flow reads as "not enumerating" and hides the bar for — and `Increment` is **never called at
all**, so the bar could not be drawn while the discovery ran no matter what total the host
substituted at that call. With R7 on the host does the counting instead: the enumeration is
2,807 XAM app messages (`XamAppEnumerateContentAggregate`, message 0x0002000E), the guest
reaches that handler through imports it really has (`XMsgStartIORequestEx`,
`XMsgInProcessCall`, `XMsgSystemProcessCall`), and the host counts them and pushes
`current_content`/`total_content` into the panel with the engine's own push — no SDK source
is touched. The bar then animates: measured, one item per presented frame, 0 → 100% in
46.7 s, and the fill grew 242 → 786 → 1,514 cyan pixels across three captures
(`out/drive-ui/p2-*.png`).

## 5. Limits

- **Verified in game: the boot R7 is for (2026-10-06).** With `enhancements_dlc_cache`
  on and the same 1,402-package library: a boot with no title `songcache` mounted all
  1,402 packages and reached the main menu in **390 s** (against **662 s** with the SDK's
  100 ms mount latency left in place, and 11 minutes before R7 existed); the title then
  wrote its own 1.1 MB `songcache`, and the next boot mounted **no** package and reached
  the menu in **57.5 s**, of which R7's host cache saves the 14.5 s the walk would have
  cost. A full acceptance run in that cold state
  (`scripts/acceptance_song.ps1 -DlcLibrary` on `155`) reported `Boot`, `SongList`,
  `Playing`, `Results` and `SongSeen` all true with a 225-second playback and no
  `[FATAL]`, and the refresh row's press re-scanned the library and rewrote the cache
  (§4.2). With the toggle off, the delay is the SDK's own default and the enumeration is
  scanned as before, so the faithful boot is the one this project had before R7.
- **Verified in game: R7's frame pacing for the discovery (2026-10-10).** The title's own discovery
  advances one item per *presented* frame, so `enhancements_dlc_uncap_enumeration` (off by default)
  turns the SDK's own `vsync` down for as long as that enumeration lasts and puts the player's value
  back at the last item. Measured on the same 1,402-package library and the same **2,807-item** walk:
  **46.7 s → 15.96 s** (5.68 ms per item), with `the frame rate is free for the discovery (vsync false,
  was true)` 7 ms after the first item and `the frame rate is the player's own again (vsync = true)`
  1 ms after the last — 104 ms before the engine's own completion, so no menu frame is presented
  uncapped. The call count is unchanged, i.e. nothing is skipped, which is the whole point: batching the
  walk instead is closed by measurement ([plans/song-cache-plan.md](plans/song-cache-plan.md) §13), and
  fast-forwarding it is closed by the absence of any proof that the song list survives
  (§14.5). `scripts\acceptance_dlc.ps1 -DlcLibrary <flat library> -ExpectedSong "LYCANTHROPE"
  -Extra "--enhancements_dlc_cache=true;--enhancements_dlc_uncap_enumeration=true"` then passed with the
  pacing armed and restored inside the leg, the library's song on the guest's own list, and that song
  absent from its control leg — the leg needs `-BootTimeoutSec 900 -ScreenTimeoutSec 900`, because a
  fresh `user_data_root` is a cold `songcache` and that boot mounts the whole library before the list
  appears. A **cold** boot is not the measurement of what the option buys: there the frame is busy
  mounting (2,856 `Loading STFS header file` mounts) rather than waiting on the display, which is why
  the warm boot above is the number to quote.
- **Verified in game with a flat library (2026-10-05).** A 1,402-container library —
  1401 of them `CON` saved-game packages (RB3 936, RB2 214, RB1 251) and one a
  `LIVE` marketplace package — joined `game/dlc` as `--dlc_library`. The hook
  reported 1402 library packages (`45410829/00000002 x252, 45410869/00000002 x214,
  45410914/00000002 x936`), the §2.1 adaptation reported 1401 of them presented as
  content type `00000002`, the guest's own enumerator returned all of them (`added 1403
  items (device=0, type=00000002)` — those 1402 plus the RB3 soundtrack), and it then
  opened every one (2806 `Loading STFS header file` mounts) with no `[FATAL]`. Read as
  their own saved-game type instead, the same folder is enumerated and mounted the same
  way and reaches **no** song list, which is what the adaptation is for. A flat
  library's songs list and *play*: `scripts/acceptance_song.ps1 -DlcLibrary … -Song 2
  -SongName "155"` reports `SongList`, `Playing`, `Results` and `SongSeen` all true
  with a 225-second playback envelope (a custom `CON` out of the folder, played to its
  results screen), and the screen-driven `scripts/acceptance_dlc.ps1 -DlcLibrary …`
  passes with the library's song on the list. Evidence: `out/m8-play5/` (playback),
  `out/m8-harness/` (the acceptance pass) and `out/m8-full/` (the whole folder's logs).
  A folder this large is near the acceptance harness's limit — at 3840x2160 the OCR of
  a frame can exhaust memory while ~2000 containers are mounted, so the whole-folder
  leg is read from the log rather than from the screen. The two host halves are pinned
  by [tests/dlc_library_tests.cpp](../tests/dlc_library_tests.cpp) (78 checks).

- **Verified in game (2026-10-03).** What is proven host-side: the layout
  ([tests/dlc_layout_tests.cpp](../tests/dlc_layout_tests.cpp), and the real `game/dlc`
  tree reports 21 packages and 0 rejected entries) and the patch set
  ([patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch](../patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch),
  which applies to the pinned checkout). The guest's own song list is the only thing
  that proves the enumeration and the mount, and
  [scripts/acceptance_dlc.ps1](../scripts/acceptance_dlc.ps1) now drives it:
  `XamContentAggregateCreateEnumerator: added N items` must be non-zero and a song
  only the DLC package carries must be on the song list, read with Windows OCR,
  with a control leg at a `--dlc_root` that does not exist where the song must be
  absent. On this dump the positive leg reports `dlc: 21 package(s)` and `added 1
  items`, and the list carries All-American Rejects' "Kids in the Street"; the
  control leg reports no packages and 0 items, and the list is the three bundled
  songs alone. Evidence: `out/m7-dlc/`. Re-run 2026-10-05 against the
  library-capable harness and hook (§2.1): both legs still pass — 21 packages, one
  aggregate item and the song on the list; the absent-root leg 0 packages, 0 items and
  the song absent. Evidence: `out/m8-default/`.
- **The container probe needs the entry's full path (fixed 2026-10-03).** The first
  cut asked `IsContentPackageFile(file_info.path)`, but
  `rex::filesystem::ListFiles` reports the entry's **parent** in `FileInfo::path`
  and the entry name in `FileInfo::name` (`GetInfo` does the same), so the check
  tested the directory and rejected every STFS package. The hook found the packages
  (21 in this dump) and mounted the root, yet the guest's aggregate enumerator added
  0 items and no song list carried DLC. `file_info.path / file_info.name` is the fix,
  carried in patch 0006 ([patches/README.md](../patches/README.md)).
- **Only the content API route.** The SDK scans `game:\Content\0000000000000000\…` for
  the enumerator, but opening those items resolves through the content manager's roots
  and not against the game tree, so that directory is not a DLC source. It was not one
  before this change either.
- **One directory, no profile split.** The extra root has no xuid level and no
  `Headers` level: packages dropped there cannot carry a display name or a license
  mask of their own. Their license comes from the container's own license entries
  instead, which is where `InstallContent()` reads the bits it writes into a `.header`.
- **The SDK's `InstallContent()` still has no caller.** Extracting a package into the
  content root is not needed while the packages can be mounted where they lie.
