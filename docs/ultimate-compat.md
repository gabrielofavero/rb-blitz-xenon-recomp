# Vanilla vs Rock Band Blitz Ultimate — content-variant policy and install

Policy decided 2026-09-19, **implemented and verified 2026-09-20**
([bringup-log.md](bringup-log.md) B-012). The drag-and-drop install works: the
payload folder is dropped next to the retail game data and the same
`rb_blitz.exe` boots either variant.

> **Name correction.** Until 2026-09-20 this file called the community mod "Rock
> Band Blitz Deluxe" and linked `solamint/rock-band-blitz-deluxe`. That was wrong on
> both counts. The Blitz mod is **Rock Band Blitz Ultimate**
> ([`ultimate-mods-rb/blitz-ultimate`](https://github.com/ultimate-mods-rb/blitz-ultimate)).
> **Rock Band 3 Deluxe (RB3DX)** is a different project for a different game — it is
> the reference we mine for *technique*, not the mod this port supports (see
> [rb3-references.md](rb3-references.md) §5–§6 and §12 below).

## 1. The decision

One executable, two supported content variants:

| Variant | Who owns it | What it is |
| --- | --- | --- |
| **Vanilla** | us, from day one | The retail Xbox 360 dump used as the compilation input. Its built-in offline mode ("Proceed in Offline Mode?") is the supported route. |
| **Rock Band Blitz Ultimate** | the community mod; we only make it *work* | A quality-of-life mod installed by copying files over a vanilla game folder. Where unblocked/restored online behaviour and QoL features come from. |

This project **does not restore, emulate or route around online services** — not
Rock Central sign-in, not leaderboards, not achievements, not challenges, not
store/DLC enumeration. Blitz already ships an offline mode, so "unblocking online
restrictions" was never a requirement for a playable port, and it is out of scope
permanently. The community mod is where that work lives, exactly as RB3DX is where
it lives for Rock Band 3.

**Our goal is compatibility, not reimplementation.** A user who installs Rock Band
Blitz Ultimate gets a working recomp by drag-and-drop: no recomp-specific second
build, no recomp-side patcher to run first, and no asking them to uninstall the mod.
A user who keeps vanilla must be unaffected — same executable, same behaviour, byte
for byte, when no payload is present.

## 2. What the mod is, and how it installs

- Repository: [`ultimate-mods-rb/blitz-ultimate`](https://github.com/ultimate-mods-rb/blitz-ultimate).
  Re-read it before changing anything here: its install section can change between
  releases, and this project only promises compatibility with the *payload shape*
  documented below.
- The Xbox 360 payload is a set of files copied *over* an existing vanilla install.
  The payload this port was built against (`game/ultimate`, 2026-09-20):

  | Path | Size | Role |
  | --- | --- | --- |
  | `default.xex` | 8,812 KB | The mod's executable. **Not used by us**, see §4. |
  | `gen/patch_xbox.hdr` | 4,381 B | Title-update-shaped content header. |
  | `gen/patch_xbox_0.ark` | 14,171,207 B | The mod's content, in the same ark format as `main_xbox_0.ark`. |
  | `screenshots/_keepme` | 3 B (`zxc`) | A directory placeholder, but a load-bearing one: the mod's `ulti_init.dta` offers its endgame screenshot button only when a `screenshots/` folder exists, which is what this file is there to create. |
  | `gen/` | — | Empty in the archive; the payload's content is the pair above. |

  `gen/patch_xbox.hdr` + `gen/patch_xbox_0.ark` are one unit: the game treats a
  header without its `_0.ark` as a damaged disc. The names are the game's own
  convention, not the mod's: the decrypted retail image carries the format string
  `%s/gen/patch_%s` (right after `gen/main_%s`, at `0x8205C827`) and `UPDATE:` as its
  only device string (at `0x8205DD74`, the byte run edit `1` rewrites, §3). The mod
  therefore rides a patch-content path that the retail game already has.
- The mod also targets **PS3** (a `.pkg`). That flavour is not consumable here and is
  out of scope.

## 3. The evidence: the mod's executable differs from retail in 12 bytes

`scripts/decrypt_xex.py` mirrors the SDK's `XexModule::ReadImage` (retail key, CBC IV
= 0 chained across BASIC blocks) and can diff two decrypted images at guest
addresses. Reproduce the whole table with:

```powershell
python scripts/decrypt_xex.py game/default.xex out/vanilla.bin
python scripts/decrypt_xex.py game/ultimate/default.xex out/ultimate.bin
python scripts/decrypt_xex.py --diff out/vanilla.bin out/ultimate.bin
```

`--diff` output for the payload this was built against (10,747,904 bytes each,
mapped at `0x82000000`):

| Guest VA | Retail | Ultimate | What it is | Patch bit |
| --- | --- | --- | --- | --- |
| `0x8205DD74` | `"UPDATE:\0"` | `"D:\0\0\0\0\0\0\0"` | The content-device string, the only `UPDATE:` in the image, feeding the retail format string `%s/gen/patch_%s` at `0x8205C827`. Points the guest's content lookups at `d:` instead of `update:`. | `1` (content device) |
| `0x821D0A7C` (instruction; changed byte `0x821D0A7F`) | `li r3,1` (`01`) | `li r3,0` (`00`) | Tail of `sub_821D0A18`, the lookup that scans the 2-entry song-blacklist table at `0x82015FC8` (`hierkommtalex`/1005106, `rockandrollstar`/1005109). Patched, that table never matches — the mod's "Removed song blacklist (Rock 'n' Roll Star and Hier kommt Alex unblocked)". | `2` (song blacklist) |
| `0x8236C108` | `mflr r12` (`7d8802a6`) | `blr` (`4e800020`) | Prologue of `sub_8236C108` (`object, state`): the whole function becomes a return with `r3` intact. | `4` (state update) |

**12 bytes in 3 runs, and that is the entire code delta.** Everything else the mod
does is content in `patch_xbox_0.ark`. This is why compatibility is a small, bounded
job rather than a port of a fork.

### The executable is frozen, so this delta covers every release

The mod's `default.xex` is a committed binary: one commit in its repository
(`66038c9f`, 2025-02-24), no patch script, no offset table, no CI step that touches
it. It is also **byte-identical in every release** — 1.0, 1.01, 2.0, 2.1 and 2.11 all
ship sha256 `390e0ae089e775a899c166c6125d0daf5f6b905ab53ce54e7638d5fb5679928c`, which
is what the payload staged here (`game/ultimate/default.xex`, 9,023,488 B) hashes to
(checked 2026-09-20). Two things follow:

- **Every feature added after 1.0 is content.** Offline score saving, the input
  viewer, the cheats, custom gems and song search shipped in the ark/DTA, not in the
  executable, so they reach the guest through the overlay without more code work
  (§6). We know of exactly three xex-side behaviours — the three edits above — and
  the mod's own release notes name two of them as features: "Removed song blacklist
  (Rock 'n' Roll Star and Hier kommt Alex unblocked)" (edit `2`) and "Dirty disk error
  when loading custom songs removed (Xbox 360)" (edit `4`).
- **A new release is normally a content change only**, which is what the overlay
  already handles. The exception is a release that ships a *different* `default.xex`:
  check its hash against `390e0ae0…` and re-derive the table above before trusting
  anything else in this file.

## 4. Why the mod's `default.xex` is not the codegen input

Our translation is generated from **one specific retail `default.xex`**:
`config/functions.toml`, the discovered function boundaries, the jump tables, the
import ordinals and every byte-guarded patch are addresses in *that* image.
Therefore:

- **The codegen input stays vanilla.** A build checkout needs the retail
  `default.xex`; the mod's own install flow is what provides it.
- **The mod's three edits are reproduced on our side** — as one host-side data patch
  and two function overrides with the guest addresses in §3 — in
  [src/hooks/ultimate.cpp](../src/hooks/ultimate.cpp). An installed payload is
  *data*, never a second executable.
- **The payload's own `default.xex` is hidden** from the guest-visible tree, so a
  stale copy can never take over the boot (§6).

## 5. Why the payload cannot just be "added to the data root"

Two facts forced the design, both established by probing:

1. **Mounting the payload as `update:` does not work.** `Runtime::SetupVfs()` only
   creates `update:` when the dump actually has a title update; pointing `update:` at
   the payload made the guest open the payload's files and then call
   `XamShowDirtyDiscErrorUI` (black screen). That is what a copied-over install looks
   like from the inside: the payload expects the *retail* `d:\gen` to still be there,
   which is why the mod re-points `%s/gen/patch_%s` at `d:` instead (§2, §3).
2. **The virtual file system cannot merge per file.** `VirtualFileSystem::OpenFile`
   resolves a file by resolving its *directory* first (`d:\gen` → an entry) and then
   calling `parent_entry->GetChild("patch_xbox.hdr")`. So a symbolic link, device
   alias or any per-file trick can never redirect a single file: the merged view has
   to exist at the directory level.

The answer is a union device:
[src/fs/payload_overlay.h](../src/fs/payload_overlay.h) /
[payload_overlay.cpp](../src/fs/payload_overlay.cpp), mounted between the guest and
both directories, with the merge rules kept in an SDK-free header
([src/fs/overlay_merge.h](../src/fs/overlay_merge.h)) so they can be tested without
booting the game.

## 6. How the install works

`RbBlitzApp::OnPostLoadXexImage()` calls
`rb_blitz::ultimate::Configure(runtime(), game_data_root())` once the VFS is up, the
XEX is in guest memory and the guest has not started. `Configure()` resolves the mode
and the payload path, applies the enabled `default.xex` edits, and — when a payload
directory exists and the payload has *not* been merged into the data root — mounts the
union device and re-points both `d:` and `game:` at it.

**Overlay (union) rules**

| Situation | Result |
| --- | --- |
| File only in the payload (`patch_xbox.hdr`, `patch_xbox_0.ark`) | Read from the payload. |
| File only in the game root (`main_xbox.hdr`, `main_xbox_0.ark`, `default.xex`, …) | Read from the game root, unchanged. |
| File in both (none in this payload) | The payload copy wins; counted in the overlay stats line. |
| Directory in both (`gen`) | Merged, listed once. |
| Payload's `default.xex` | Hidden (§4): the game root's copy is what the guest sees, the payload's is invisible. |
| Writes | Go to the game root; the payload side is read-only, and a payload-only file cannot be written or truncated. |
| Everything | Case-insensitive, guest-path shape preserved, no copying and no extracting: the payload directory is read in place. |

**Verification hooks.** Each boot with a payload logs one line, for example:

```
ultimate: payload …\game\ultimate, patches 0x7
ultimate: content device string at 0x8205DD74 patched: UPDATE: -> D:
ultimate: overlay \Device\BlitzOverlay = …\game\ultimate + …\game - 4 payload-only record(s),
          13 game-root-only, 0 payload copies preferred, 1 merged director(ies), 1 hidden
```

**Patch bits** (`--ultimate_patches`, default `7`)

| Bit | Value | What it does | Needed? |
| --- | --- | --- | --- |
| Content device | `1` | `UPDATE:` → `D:` at `0x8205DD74`, so the guest looks for the payload's content pair in `d:\gen` rather than in a title-update device that this dump does not have. The lookup is the retail format string `%s/gen/patch_%s` (§2), so this one string edit re-points it; without it the payload is mounted but never opened. | Yes, for the payload to have any effect. |
| Song blacklist | `2` | Forces `sub_821D0A18` to report "not in the table", unblocking the two blacklisted bonus tracks (the mod's "Removed song blacklist"). | No for boot, but it is a user-visible feature: leave it on. |
| State update | `4` | Forces `sub_8236C108` to return immediately. | **Load-bearing**: with the payload present and this bit off, the guest calls `XamShowDirtyDiscErrorUI` and the screen stays black (§7). |

The data patch refuses to touch `0x8205DD74` unless it still holds `"UPDATE:\0"`, so
an already-patched image and any future layout are left alone. The slot lives in a
read-only XEX section, so the page is unprotected around the write and restored
afterwards (`XexModule::Load` protects code and read-only data with
`kMemoryProtectRead`; a plain store there is an unhandled guest access violation).
The two function overrides are strong `REX_FUNC` definitions that replace the weak
`__imp__…` aliases, so the original stays reachable through `__imp__…` and the
default path is untouched when the bit is off.

**Cvars** (all `kRequiresRestart`)

| Cvar | Default | Meaning |
| --- | --- | --- |
| `ultimate_mode` | `1` | `0` = off (retail only), `1` = auto (use a payload when one is present), `2` = force (warn and continue when none is). |
| `ultimate_payload_root` | `<game_data_root>/ultimate` | Payload directory; relative paths resolve against `game_data_root`. |
| `ultimate_patches` | `7` | Bit mask from the table above. |

## 7. What was verified (2026-09-20)

Host tests, no boot needed: `ctest` in the configured build tree runs three targets —
`crypto_keytable`, `fingerprint` and the new `payload_overlay` (83 checks over the
union, backing and hide rules of
[overlay_merge.h](../src/fs/overlay_merge.h)) — 3/3 pass.

Boot probes ("luma" is the mean luma of a 1280×720 window capture, so ~19 with 7 KB is
a black screen and ~75 with 1.7 MB is a rendered frame):

| Route | Log evidence | Shot | Verdict |
| --- | --- | --- | --- |
| Payload in `game/ultimate`, auto (default) | overlay stats line; `NtCreateFile path=d:\gen\patch_xbox.hdr`, then `d:\gen\patch_xbox_0.ark`; no dirty-disc call | 1694 KB, 75.1 | **pass** |
| Merged install (payload pair copied into `game/gen`) | `patches 0x7 (merged into the game root)`, no overlay line | 1694 KB, 75.0 | **pass** |
| `--ultimate_mode=0` with the payload on disk | `ultimate: off, booting the retail game data` | 1725 KB, 74.3 | pass (vanilla) |
| Payload directory renamed away, auto | `no payload at …\game\ultimate and no merged gen/patch_xbox.hdr, booting the retail game data` | 1726 KB, 74.3 | pass (vanilla) |
| `--ultimate_patches=6` (content device off) | `update:\gen\patch_xbox.hdr -> 0xc000000f`; the payload is never opened | 1725 KB, 74.3 | boots, payload inert |
| `--ultimate_patches=5` (song blacklist off) | overlay + content device patched | 1694 KB, 75.1 | pass |
| `--ultimate_patches=3` (state update off) | `XamShowDirtyDiscErrorUI called! user_index=0` | 7 KB, 19.5 | **fail**, black screen |
| `--ultimate_patches=1` (only the content device) | same dirty-disc abort | 7 KB, 19.5 | **fail**, black screen |
| Pre-overlay attempt: payload aliased as `update:` | dirty-disc abort | 7 KB, 19.3 | rejected design (§5) |

Two consequences worth keeping:

- **The state-update edit is the one that matters.** Content alone is not enough:
  the payload's content drives the guest into the disc-error state machine that
  `sub_8236C108` guards, and the mod's `blr` patch is what keeps it out.
- **The payload's content is live, not inert.** With the payload mounted, the guest
  opens the payload's pair and then asks for `game:\ulti_settings.dta` and
  `game:\ulti_settings.ini` (Ultimate's own settings files, which neither the retail
  root nor the payload ships) plus `game:\scores`. All three fail with
  `0xc000000f`, the same way the retail title-update lookup fails, and the guest
  carries on to the menu. Nothing in either decrypted image contains those names as
  plain bytes, so they come out of the payload's script content.

## 8. Install, uninstall, toggle

```text
# install: put the payload next to the game's own data (nothing is overwritten)
game/                     # retail dump, untouched
game/ultimate/            # the mod's files
  default.xex
  gen/patch_xbox.hdr
  gen/patch_xbox_0.ark

# run either variant with the same executable
rb_blitz.exe --game_data_root=<game root>

# uninstall: delete the payload directory; vanilla behaviour returns, byte for byte
```

- Want it somewhere else? `--ultimate_payload_root=D:\mods\blitz-ultimate`.
- Want vanilla without moving files? `--ultimate_mode=0`.
- Already merged the payload into the data root (the mod's own install shape, or a
  copied `gen/`)? That is detected and the overlay is skipped; only the three edits
  are applied.

## 9. Hazards and limits

| Hazard | Why it bites | Where the answer lives |
| --- | --- | --- |
| A header without its ark | `gen/patch_xbox.hdr` without `patch_xbox_0.ark` makes the game report a damaged disc. The overlay warns at boot. | this file, §2 |
| The payload's content pair is only reachable through the content-device edit | With `--ultimate_patches=0` or `6`, the guest looks in `update:`, which this dump does not have, and the payload is inert. | §6 |
| The mod's own settings files are absent | `game:\ulti_settings.dta`, `game:\ulti_settings.ini` and `game:\scores` are requested and missing (`0xc000000f`); the guest tolerates all three. If a future release needs them, they are user-supplied content, not something we ship. | §7 |
| Fingerprinting | The gate describes `default.xex` in the data root. Because an installed payload is a *separate directory*, the root's identity is unchanged (the "game data identity" line reports the same 9,023,488-byte / `e2195d62…` image on every route). Replacing the root's `default.xex` would change it: the build gate fails closed and can be relaxed with `-DRBBLITZ_ALLOW_MODIFIED_GAME_DATA=ON`, while the boot-time check only warns. | [known-issues.md](known-issues.md), [build-and-run.md](build-and-run.md) §3 |
| Menu/input data is replaced | The payload ships its own `_ark/ui/…` and `config` data; a missing or renamed DTA block can silently kill every menu binding. | [rb3-references.md](rb3-references.md) §7.2 |
| Content-enumeration counts change | Our verified offline state (`XamContentCreateEnumerator: added 2 items`, aggregate enumerator `0 items`) is a **vanilla** baseline. | [bringup-log.md](bringup-log.md), [prompts/05](../prompts/05-complete-one-song.md) |
| A payload release with a different shape | We reproduce 12 bytes of an image and union a directory. An extra ark, a real `update:` device or a second executable would make the payload unreachable again, and this file has to be revisited. The executable itself has been frozen since 1.0 (§3), so compare the release's `default.xex` hash against `390e0ae0…` first. | this file |
| Mod churn | Its install instructions can change between releases; the only shape we promise is the one in §2. | this file |

## 10. Acceptance criteria for "Ultimate works"

1. The same `rb_blitz.exe` pointed at a game root with `ultimate/` in it boots to the
   menu and plays a song, while the vanilla route (`--ultimate_mode=0`, and a root
   with no payload) still passes unchanged.
2. Every recomp-side change needed for that is a documented hook or patch with a
   guest address and evidence: no monkey-patching, no per-variant build flags, no
   forked `default.xex`.
3. The overlay never writes to the payload: an uninstall is deleting one directory,
   with nothing cached outside it.
4. Anyone can re-derive §3 from the two images with one script.

Points 2, 3 and 4 hold as of 2026-09-20. The "plays a song" half of point 1 rests on
the Milestone 5 observation rather than a scripted run, exactly like the vanilla
route ([bringup-log.md](bringup-log.md)).

## 11. What we never do

- Reimplement or emulate Rock Central, leaderboards, achievements, challenges,
  social features, or the store. "Online works" in the Ultimate sense is the mod's
  behaviour on its patched image; our job is to not break it.
- Ship, mirror or vendor Rock Band Blitz Ultimate, its patched `.xex`, its `gen/` or
  `_ark/` data, or anything extracted from it. A payload is user-supplied game data,
  on the same footing as the retail dump
  ([prompts/06](../prompts/06-reproducible-release.md) §5).
- Commit decryption keys: `scripts/decrypt_xex.py` reads the retail key out of the
  pinned SDK source instead of carrying a copy.
- Support the PS3 `.pkg` flavour.
- Pretend the payload's `default.xex` is a second build: it is hidden, and the mod is
  reproduced as three edits plus content.

## 12. Rock Band 3 Deluxe is a different project

RB3DX mods Rock Band 3, not Rock Band Blitz, and the RB3 native port consumes it
differently (it is the RB3 decomp's reference for a shipped, widely installed content
overhaul). Two differences matter to us and neither transfers:

- **Nothing here is copied from RB3DX.** We mine
  [rb3-references.md](rb3-references.md) §5–§6 for *technique*: how a recomp-side
  port keeps a giant community content mod working. The addresses, patches and
  acceptance criteria in this file are Blitz-specific and were derived from the
  images in §3.
- **Our mod is small where RB3DX is large.** RB3DX changes a lot of code and content;
  Blitz Ultimate is 12 bytes of code plus one ark of script content. Compatibility
  here cost a union device and three edits, not a port of a fork.
