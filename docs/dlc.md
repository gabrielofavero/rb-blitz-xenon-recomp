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
volume that cannot hold links (this machine's `D:\Games\YARG Songs` is exFAT), so it
cannot be indexed into §2's shape without copying it. `--dlc_library` reads such a
folder where it lies instead, and a `--dlc_root` that is not the §2 layout is read
the same way:

```text
--dlc_library="D:\Games\YARG Songs"     # one or more folders, ';'-separated
```

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
the open path consult the same list. The host half — the recursive scan, the header
read and the name — is [src/fs/dlc_library.h](../src/fs/dlc_library.h), pinned by
[tests/dlc_library_tests.cpp](../tests/dlc_library_tests.cpp).

## 3. Configuration

| Setting | Default | Effect |
| --- | --- | --- |
| `--dlc_root`, or `dlc_root` in `rb_blitz.toml` | `<game_data_root>/dlc` | root of the layout in §2; a relative value resolves against the game data root; changing it needs a relaunch |
| `--dlc_library`, or `dlc_library` in `rb_blitz.toml` | *(empty)* | `;`-separated folders of §2.1; a relative value resolves against the game data root; changing it needs a relaunch |
| `--dlc_library_content_type` | *(empty)* | present **every** library package under this 8-hex-digit content type, instead of §2.1's saved-game → marketplace adaptation; changing it needs a relaunch |

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
[info] dlc: 1402 package(s), 0 not a container, in 1402 file(s) walked (45410829/00000002 x252, 45410869/00000002 x214, 45410914/00000002 x936) in D:\Games\YARG Songs by their own headers (read-only, mounted in place)
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

## 5. Limits

- **Verified in game with a flat library (2026-10-05).** `D:\Games\YARG Songs` — 1402
  containers, of which 1401 are `CON` saved-game packages (RB3 936, RB2 214, RB1 251)
  and one a `LIVE` marketplace package — joined `game/dlc` as `--dlc_library`. The hook
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
