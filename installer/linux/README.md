# Linux package bring-up (Prompt 2)

The desktop entry starts `rb_blitz_linux_launcher`, a normal-user, SDK-free
pre-game importer. It uses Zenity for the source picker, preview, progress,
cancel, and errors. The shared portable core verifies the selected folder or
STFS package against `game-fingerprints.toml`, stages it under the user's XDG
data root, and preserves the previous valid import on failure. A valid import
starts `rb_blitz --game_data_root=<XDG data>/rb_blitz/game`; later launches skip
selection. `rb_blitz_linux_launcher --repair` offers reimport directly. The
guest-authored Settings route to that repair command is still pending.

The executable tree remains read-only. Default paths are:

| Use | Location |
| --- | --- |
| Imported game, DLC, updates | `${XDG_DATA_HOME:-$HOME/.local/share}/rb_blitz/` |
| Settings and profile | `${XDG_CONFIG_HOME:-$HOME/.config}/rb_blitz/` |
| Cache | `${XDG_CACHE_HOME:-$HOME/.cache}/rb_blitz/` |
| State | `${XDG_STATE_HOME:-$HOME/.local/state}/rb_blitz/` (planned logging route) |

`installer/config/platform-packaging.json` is the exact stage-file list.
`package.py` copies only those files from a native build, rejects an incomplete
stage or non-ELF target, reads the version from `installer/config/pins.toml`,
and uses only the nFPM archive matching the recorded SHA-256. It generates both
formats from that same tree. Package generation remains blocked by
`nativeDependencyInventoryPending` and `releaseAuditPending` until a native
build reveals the real ELF dependencies and the stage/package audit is complete.
The stage must also be
expanded with validated controller assets, native license notices, and all
required shared libraries before this flag is cleared. Do not release the
current manifest as a playable package.

On the native Linux host, after configuring and building the release preset:

```sh
python3 installer/tools/check_platform_packaging.py
python3 installer/linux/package.py --build-dir out/build/linux-amd64-release --stage-only
```

After the dependency inventory, asset stage, and ELF/package audit are complete,
pass the pinned nFPM archive to `package.py --nfpm-archive <archive>` to produce
the `.deb` and `.rpm`. Installed graphical launch, import, game boot, and a
second launch still need native acceptance before either package is published.
