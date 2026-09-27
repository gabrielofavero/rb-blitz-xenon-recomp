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
  `Documents\rb_blitz` look like.
- The lookups are exact, and the SDK builds them with `{:08X}`, so the two directory
  levels are **8 upper-case hex digits**. A lower-case spelling, or a 16-digit level
  (the content root's own xuid level, copied in by mistake), is reported at boot and
  enumerates nothing.
- A file is content only when it starts with the STFS magic `CON `, `LIVE` or `PIRS`.
  Anything else in those directories — a readme, a MOGG, an ark — is reported, not
  silently skipped.

## 3. Configuration

| Setting | Default | Effect |
| --- | --- | --- |
| `--dlc_root`, or `dlc_root` in `rb_blitz.toml` | `<game_data_root>/dlc` | root of the layout in §2; a relative value resolves against the game data root; changing it needs a relaunch |

The root is read-only by construction. Enumeration and opening resolve into it, and
every write path in the content manager (create, thumbnail, install, delete) resolves
against the content root instead, so the title cannot damage a package folder and
removing the folder stays a complete uninstall.

The boot log states what was found, for the layout in §2. The first line is what §2's
two title directories produce; the second is what a misplaced file reports:

```text
[info] dlc: 21 package(s) for title(s) 45410914, 5841122D in D:\…\game\dlc (read-only, mounted in place)
[warn] dlc: ignoring rhythm/00000002/song.dat (not a content package: no CON/LIVE/PIRS magic)
```

An absent `dlc/` is the normal case and logs one line saying so, with no warning.

## 4. The other place content can live

The content manager's own root is the platform user directory, `Documents\rb_blitz`,
laid out as `<xuid>/<title_id>/<content_type>/<name>/` with optional
`<xuid>/<title_id>/Headers/<content_type>/<name>.header` files beside it. The title's
own saves are there (`B13EBABEBABEBABEBE\5841122D\00000001\songcache`, `\globaloptions`),
it is writable, marketplace content in it belongs under `0000000000000000`, and
`--user_data_root` moves it. The DLC root exists so that game packages share neither
that directory nor its write access.

## 5. Limits

- **Not verified in game.** What is proven host-side: the layout
  ([tests/dlc_layout_tests.cpp](../tests/dlc_layout_tests.cpp), and the real `game/dlc`
  tree reports 21 packages and 0 rejected entries) and the patch set
  ([patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch](../patches/rexglue-sdk/0006-content-container-packages-and-extra-content-root.patch),
  which applies to the pinned checkout). The guest's own song list is the only thing
  that proves the enumeration and the mount. What to look for:
  `XamContentAggregateCreateEnumerator: added N items` in the log, where the vanilla
  baseline was 0 with no DLC present ([history/bringup-log.md](history/bringup-log.md)),
  and then a `--log_level=trace` read of a file inside one of the packages.
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
