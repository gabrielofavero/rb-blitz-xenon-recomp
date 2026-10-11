# P6 — the Ultimate provider adapter and the provider's own settings file

Status: **executed** for the adapter, the exact-file redirect and the row wiring, with the two
open items listed in §6. Evidence is host tests plus real boots against the pinned retail image
with the owner's installed payload (`game/ultimate`); §5 names the log and the capture behind every
"measured" line.

Read with the parent plan (`docs/plans/in-game-settings-plan.md` §3 for where the rows go, §5.5 for
the application strategy, §6 for the integration boundary) and with P5b's handoff
(`docs/plans/in-game-settings-merged-menu.md` §6, which froze the row identity this package binds).
`docs/ultimate-compat.md` is the payload's own side.

## 1. What this package owns

| File | What it is |
| --- | --- |
| `src/settings/ultimate_adapter.{h,cpp}` | The whole provider model, SDK-free: the payload's ark index reader, the installation inventory, the shape check over the provider's **own** compiled definitions, the frozen row bindings, the provider's settings-file reader/writer, the readiness decision and the boot's provider state. |
| `tests/ultimate_adapter_tests.cpp` (ctest `ultimate_adapter`, `ultimate_adapter_payload`) | 786 checks. A hermetic half that authors a provider-shaped definition with the project's own DTB builder and mutates it, and a real-installation half that runs the same code against `game/ultimate`. |
| `src/fs/overlay_merge.{h}` + `src/fs/payload_overlay.{h,cpp}` | The **third side** of the payload overlay: a writable directory of the player's own, and the root-level records redirected into it. |
| `src/hooks/ultimate.{h,cpp}` | Boot wiring: inspect the payload, publish the provider state, mount the redirect. |
| `src/hooks/settings_bridge_hooks.cpp` | The channel's two provider ends: seed the provider's values into the store, and write a commit through to the provider's own file. |
| `out/_swap/p6/run.ps1` | The boot harness (takes the payload directory explicitly, unlike P5b's). |

Deliberately **not** in this package: the achievements action (§6.1), the restart flow (P9), the
hide list and the pool (P10), the merged-menu/record-shadowing integration and packaging (P11), and
the first-run graphics policy (P4/P11).

## 2. Why the payload's *shape* is the compatibility test, not its version

`docs/ultimate-compat.md` §3 measures that the mod's `default.xex` is **byte-identical in every
release** — 1.0, 1.01, 2.0, 2.1 and 2.11 all ship sha256 `390e0ae0…`. So a version comparison would
prove nothing at all: the release is entirely in the ark, and which rows a release carries is a
property of the ark's contents. The adapter therefore has two levels, as plan §6.1 asks:

* **the installation inventory** (`InspectInstallation`): the payload is a `.hdr` + `.ark` pair, and
  the six records this integration needs are looked up **by name in the ark index**. Every one of
  them is named with the reason it is needed (`RecordAt`), so a partial payload is reported as
  missing exactly what is missing rather than as "something changed".
* **the shape check** (`CheckShape`): the provider's **own** `ulti/gen/mod_settings.dtb`,
  `mod_settings_rw.dtb`, `ulti_macros.dtb` and `ulti_init.dtb` are parsed with the project's DTB
  parser and checked for the objects, panels, rows, row kinds, persisted variable list, choice
  inventories, settings-file path and compiled defaults this build draws. A row naming a kind this
  build does not have, a panel that is gone, a renamed variable, a `vars` list that lost a name, or
  a settings path that moved are all refused **by name**.

`ProviderReadiness` (P3's frozen §5.7 enum) is decided from the plan's three facts and nothing else:
payload present, active target, shape ok.

## 3. The rows, bound to the provider's own vocabulary

P5b froze the row identity (`<category>.<id>` tokens); this package binds each one to the provider's
variable, kind and **application strategy** (§5.5):

| Token | Provider variable | Kind | Strategy | Evidence for `hot` |
| --- | --- | --- | --- | --- |
| `gameplay.precision_mode` | `freq_ms` | boolean | `restart` | none measured |
| `gameplay.disable_random_wiggles` | `disable_random_wiggles` | boolean | `restart` | none measured |
| `gameplay.camera_angle` | `camera_angle` | slider (tokens `default`/`high`/`low`) | `restart` | none measured |
| `gameplay.game_speed` | `speedmod` | slider (percent 100–150) | `restart` | none measured |
| `gameplay.disable_dof` | `disable_dof` | boolean | `restart` | none measured |
| `gameplay.disable_plots` | `disable_plots` | boolean (provider `reboot`-tagged) | `restart` | none measured |
| `gameplay.forceplot` | `forceplot` | boolean (choices behind a screen) | `restart` | none measured |
| `gameplay.which_skybox` | `which_skybox` | boolean (choices behind a screen) | `restart` | none measured |
| `gameplay.gems` | `gems` | select (payload inventory) | `restart` | none measured |
| `interface.letterbox` | `letterbox` | boolean | `restart` | none measured |
| `interface.hide_hud` | `hide_hud` | boolean | `restart` | none measured |
| `interface.goldprogress` | `goldprogress` | boolean | `restart` | none measured |
| `interface.progressbar` | `progressbar` | boolean | `restart` | none measured |
| `interface.show_timer` | `show_timer` | boolean | `restart` | none measured |
| `interface.show_pb` | `show_pb` | boolean | `restart` | none measured |
| `interface.show_steak` | `show_steak` | boolean | `restart` | none measured |
| `interface.input_viewer` | `input_viewer` | boolean | `restart` | none measured |

`TestsBindings` fails if any row ever claims `hot` with an empty evidence string, so "make this row
live" is a measurement, not an edit. **Nothing provider-side is hot today**, and that is the honest
answer: a provider row's value is read out of the DTA variables the payload's loader filled *before
the first frame*, so writing the file (which is what this package does) cannot change the running
game — only the next boot. `Hide HUD`'s P1 live effect was the *stub* store's, not the provider's.

Where the choices come from (plan §3.4, "the provider's inventory is authoritative"):

| Variable | Where the adapter reads it | Measured on the owner's payload |
| --- | --- | --- |
| `gems` | the row's inline `(array …)` in `mod_visual_settings_panel` | `default, freq, amp2003, amp2016, monkey, ulti, x, bolt` (8) |
| `which_skybox` | `mod_force_skybox_panel`'s flat `(data …)`, the screen's own `SELECT_MSG` storing a *position* | `default_skybox, no_skybox, nfs_mw2012` (3) |
| `forceplot` | `mod_force_plot_panel`'s flat `(data …)` | `off` + 16 plots (17) |

## 4. The provider's own settings file, and why the redirect is the fix

`ulti/gen/ulti_macros.dtb` names the file the provider writes: `GAME:/ulti_settings.dta`. The game
root is mounted **read-only** (`allow_game_relative_writes` defaults false, and the overlay's own
union is no more writable than the root it wraps), so the mod's `save_settings` statement has
**never** been able to write it on this port: checked before this pass, `ulti_settings.dta` did not
exist anywhere on the machine, under any spelling.

Plan §6.2's rule is exact — *do not make the whole game mount writable*. The implementation is a
third side on the union device:

* `PayloadOverlayDevice::AddSettingsSide(settings_root, redirects)` mounts a **writable**
  `HostPathDevice` at `\Device\BlitzUserSettings` over `<user_data_root>/ultimate`, and records the
  root-level records it serves (the provider's file is `<game root>/ulti_settings.dta`, so the
  redirect list is root-level by construction and a nested entry is refused with a log line).
* `ReadBackingFor`/`WriteBackingFor` (SDK-free, in `src/fs/overlay_merge.h`) answer `kSettings` for
  such a record, for **both** directions: whatever the payload or the game root has under that name
  is not what the guest sees.
* The union's root offers the record only when the player actually has the file: reporting a file
  that does not exist would make the provider's own `file_exists` lie about it. The first write
  creates it, through `CreateEntryInternal`, which the union's root forwards to the settings device.
* `PayloadOverlayDevice::is_read_only()` reports writable **when there is a settings side**. That is
  what lets `Entry::CreateEntry`/`VirtualFileSystem::OpenFile` get as far as the union's root; every
  write that is *not* a redirected record is still refused one level down by the backing device's
  own read-only flag, which the overlay's rule tests and the boot's untouched record hashes both
  show.

Nothing pre-creates the file, so a player who never opens the provider's settings never grows an
empty one; and the adapter's writer preserves every pair the provider persists and this build does
not draw (`controller_mapping`, `server_url`, `server_ssl`, `p9dlc`), in the file's own order.

## 5. Evidence

Host: `ctest --test-dir out/build/win-amd64-release` — **31/31**, of which `ultimate_adapter`
(hermetic) and `ultimate_adapter_payload` (the real install) are 786 checks, and `payload_overlay`
is 95 (two new cases for the settings side).

| Proof | Where | Result |
| --- | --- | --- |
| The five provider DTBs parse and recompile byte-for-byte with the project's own grammar | `out/_swap/p6/ulti/ulti/gen/*.dtb` through `rb_blitz_dtb_author_tests.exe` | 40 checks each, exit 0 |
| The installed payload is a payload: 62 records, six of six required present, ark v6 | `ultimate_adapter_payload` | `6 records present, 0 missing, ark v6` |
| Its shape is the one this build draws: 8 panels, 21 persisted names, 16 defaults, 3 choice sets | `ultimate_adapter_payload` | `report.ok`, and the numbers above |
| A mutated payload is refused **by name**, eight ways | `ultimate_adapter` | each case names the missing record/panel/row/variable/path |
| Nothing is claimed `hot` without an observed live effect | `ultimate_adapter` | every binding's strategy is `restart`, evidence empty |
| The provider's file survives a rewrite: unknown pairs, their order and the unexposed text | `ultimate_adapter` | `(0 (controller_mapping "A B X Y") (freq_ms TRUE) … (legacy_multi 1 2 3))` |
| A file this build cannot parse is refused, not rewritten | `ultimate_adapter` | bytes unchanged, no `.tmp` left |
| The value path end to end (host side): the payload's defaults, an edit through `EncodeProviderValues`, a durable write, and a re-read | `ultimate_adapter_payload` | `hide_hud FALSE`, `gems ulti` survive a reload; `letterbox` keeps the payload's default |
| The guest's own file call reaches user storage | boot, `provider.log` | `ultimate: the guest opened …\ulti_settings.dta for 0x…` |
| Installed records are never modified | boot | the retail and payload hashes are re-checked after the boots (§5.1) |

### 5.1 Boots

All boots: `rb_blitz.exe --game_data_root=<install> --ultimate_mode=2
--ultimate_payload_root=<dir> --user_data_root=<scratch> --launcher_profile=<empty>
--log_level=debug`, driven through `out/_swap/p6/run.ps1`. Each boot has its own scratch user root,
so the redirect target of one boot cannot be another's file.

| Arm | Payload | What it shows | Evidence |
| --- | --- | --- | --- |
| `provider` | `game/ultimate` (the mod), with a provider file seeded as `(0 (controller_mapping "A B X Y") (hide_hud TRUE) (letterbox TRUE) (gems monkey) (speedmod 1.10) (unlockall TRUE))` | the adapter's real readiness, the redirect, the values seeded into the store, the payload's choice inventories, and the **guest's own open of the redirected record** | `out/_swap/p6/provider/provider.log` |
| `merged2` | `out/_swap/p5b/payload` (P5b's generated merged menu) | the gate is the adapter's answer, not the diagnostic: a *mounted* payload that carries no `ulti/gen/mod_settings.dtb` is `kUnrecognizedShape`, so nothing is seeded, `provider.active` answers false, **no settings side is mounted at all**, and the merged root draws the five rows P5b measured for a false gate | `out/_swap/p6/merged2/merged2.log`, `out/_swap/p6/merged2/shots/00-root.png` |

The provider arm's own lines, verbatim:

```
ultimate: provider compatible: 6/6 records, 8 panels, 11 values
ultimate: 1 record(s) redirected to <user_data_root>\ultimate: ulti_settings.dta
ultimate: overlay \Device\BlitzOverlay = <payload> + <game> - 4 payload-only record(s),
          39 game-root-only, 0 payload copies preferred, 1 merged director(ies), 1 hidden,
          1 redirected
settings_bridge: provider active - 10 row value(s) seeded from provider compatible: …
settings_bridge: provider choice list 'gems' = default, freq, amp2003, amp2016, monkey, ulti, x, bolt
settings_bridge: provider choice list 'which_skybox' = default_skybox, no_skybox, nfs_mw2012
settings_bridge: provider choice list 'forceplot' = off, plot_bodega_a, … plot_tokyo_c
ultimate: the guest opened \Device\BlitzOverlay\ulti_settings.dta for 0x80100081 -> 0x0
```

The last line is the one that matters: `0x0` is success, and that open can only have come from the
guest's own `file_exists`/`read_file` on `GAME:/ulti_settings.dta` - the provider's own
`load_settings` statement, which the payload runs at boot. So the read path from inside the game to
the player's own storage is **measured, not argued**. The seeded file is 114 bytes at
`<scratch>\ultimate\ulti_settings.dta`.

The `merged2` arm's line says the same thing from the other side:

```
ultimate: provider shape: payload records unreadable: ulti/gen/mod_settings.dtb is not in the payload
settings_bridge: provider not active - 0 row value(s) seeded from payload mounted but not the
                 provider's shape: … (ok)
```

…and its overlay line has **no** `redirected` count, because a payload that does not name the
settings file gets no settings side: the union stays exactly as read-only as the game root.

**Installed records unchanged.** Re-hashed after the boots, against the committed
`config/game_fingerprints.toml`:

| File | sha256 (first 16) | Manifest |
| --- | --- | --- |
| `game/default.xex` | `E2195D6241D42319` | `e2195d62…` ✓ |
| `game/gen/main_xbox.hdr` | `A12777E8DF91215D` | `a12777e8…` ✓ |
| `game/gen/main_xbox_0.ark` | `EAC4D2C2BB71D014` | `eac4d2c2…` ✓ |

The payload's own pair hashes `87B30426…` (`patch_xbox.hdr`) and `736F6F7E…`
(`patch_xbox_0.ark`) — recorded here as an observed baseline, because there is no committed payload
fingerprint to compare them with (and, per §2, a payload's identity is its contents, not a version).

### 5.2 What the boots cannot show

* **A write through the redirect from the guest.** The provider's own `save_settings` runs when its
  own settings screen is left with Cancel, and that screen is reached from the *retail* options list
  (the mod appends its `mod_settings` row). With P5b's payload mounted the retail list is not what
  the main menu opens, and with the mod's payload mounted the merged menu is not there - the two
  cannot be in one payload until P11 generates the combined record set (§6.2). The write path is
  therefore proven host-side (`SaveProviderFile`, and `EncodeProviderValues` in front of it) and by
  construction, not yet by a guest-initiated save in a boot.
* **The achievements action.** §6.1.
* **The rows' values affecting gameplay.** Every provider row is `restart`, so the honest test is a
  save, a relaunch and a gameplay observation - P12's matrix, with P9's restart flow.

## 6. Open items

### 6.1 The achievements action is not wired

`general.unlock_online_achievements` is a one-shot **guest** action: the provider's own body is
`{foreach $acc (acc_high_cred …) {acc_mgr earn_accomplishment $acc}}`, i.e. a call on the guest
thread through objects the provider owns. This package does not make it. The bridge therefore
answers the `invoke` request without calling the provider and **says so at boot**:

```
settings_bridge: the achievements action is not wired yet; invoking it answers without calling the provider
```

That warning is the honest half of the item; the call itself needs the guest-object work P1's
findings defers (the allocator/refcount contract for a host-initiated guest call), and it must run on
the guest thread with the provider's objects live. The row must not be presented as working until
then.

### 6.2 The other open items

| Item | Why it is open | Who owns it |
| --- | --- | --- |
| **The combined payload's shadowed records.** P5b's generator edits the *base* `ui/gen/ui.dtb` and `ui/splash/gen/splash.dtb`; the payload also carries its own copies of both, which the player's build has mounted. A patch record built from the base ark would therefore wipe the mod's edits to those two records. | The generator must read the record from the **mounted** payload when it has one, then apply our edits on top - plan §11's "generate the patch record that carries project definitions by extending an installed retail record". Measured here: both records are in the payload's 62 (verified with `scripts/hmx_ark.py list --hdr game/ultimate/gen/patch_xbox.hdr -v`). | P11 |
| **`forceplot` and `which_skybox` tokens.** The merged screen's placeholder choice tokens (`plot_a`, `plot_b` / `none`, `custom`) are not the payload's (`plot_bodega_a` … / `no_skybox`, `nfs_mw2012`). The adapter now supplies the real inventory, so the screen's static list should be replaced by it. | P5b froze the row identity but not the choice tokens; the adapter's inventory is the authoritative one (plan §3.4). Until it is wired, those two rows draw a token the provider never reported. | P5b/P6 integration, next pass |
| **A merged install** (the mod's own documented install method: its files copied over the game root) has no payload directory to inspect and no overlay to redirect through, so the adapter reports it as absent. | The separate-payload arrangement is the one this project mounts (`game/ultimate` + `--ultimate_payload_root`); a merged install would need the records read out of the game's own ark and a different redirect route. | P6 follow-up / P11 |
| **`unlockall`'s write path.** Its *default* is read from the payload (`ulti_init` sets it true) and the adapter exposes it, but its *value* belongs under native `[ultimate_overrides]` (plan §6.2) - i.e. in P3's store, which the bridge does not own yet. | The bridge writes only the provider's file. `EncodeProviderValues` skips the row by design, and the file's `vars` list does not carry it, so nothing is silently lost - it is simply not persisted yet. | P3/P11 wiring |
| **Provider rows with no value.** `ulti_init` sets 16 variables; 10 of them are rows this build draws. The other 7 (e.g. `hide_hud`, `freq_ms`) have no provider default and no file pair, so the adapter reports them **absent** and the runtime says which: `no provider value for … ; those rows keep their own default`. | Inventing a value would overrule the payload (`os_hide_hud` reads an unset `$hide_hud`, i.e. false) and would also hide a payload that later ships one. The screen's own state query already degrades to "off" for an absent value. | by design; P11 decides whether the compiled default is seeded through the service |
| **The provider's second writer.** `ulti/gen/ulti_funcs.dtb` also carries `ulti_settings_writer`, which writes a *different* shape (`((settings (precision …)) (hud (hide_hud …)))`) to `$ulti_settings_path` - a variable nothing in the inspected records ever sets. | Recorded, not implemented: the file this package reads and writes is the one `mod_settings_rw` uses, which the payload's own `load_settings` reads. A payload that switches to the other shape would be caught by the shape check (its `vars` list and path macro are the ones checked). | P6 follow-up, only if a payload needs it |
| **Quality of the "verbatim" claim for a nested value.** An element whose value serialized as a nested array is re-emitted with normalized whitespace. | Values the provider writes are atoms, symbols, quoted strings and floats, so a rewrite is byte-exact for every measured file; a nested array is preserved in content but not in whitespace. | documented |

## 7. Reproduce

```powershell
# The adapter and its rules (SDK-free), plus the same binary against the installed payload.
cmake --build out/build/win-amd64-release --target rb_blitz_ultimate_adapter_tests
.\out\build\win-amd64-release\rb_blitz_ultimate_adapter_tests.exe                       # hermetic
.\out\build\win-amd64-release\rb_blitz_ultimate_adapter_tests.exe game/ultimate          # real
ctest --test-dir out/build/win-amd64-release -R 'ultimate_adapter|payload_overlay'

# The boots (one at a time: the harness refuses to start while rb_blitz runs).
pwsh -File out\_swap\p6\run.ps1 -Label provider -Seed '(0 (hide_hud TRUE) (gems monkey))' -Route boot
pwsh -File out\_swap\p6\run.ps1 -Label merged2 -PayloadRoot out\_swap\p5b\payload -Route root
$env:REX_SETTINGS_BRIDGE_PROVIDER_ACTIVE='1'   # the diagnostic that forces the gate open
pwsh -File out\_swap\p6\run.ps1 -Label forced -PayloadRoot out\_swap\p5b\payload -Route root
```

The harness waits for the game's window before driving it (measured: with the mod's payload the
window arrives well after the process starts, and the driver otherwise throws `rb_blitz has no main
window` and leaves the game running). `-PayloadRoot` is resolved to an absolute path first, because
the runtime resolves a relative one against `game_data_root`.

`out/_swap/` is ignored: the decoded payload material and the boot logs in it are research, not
deliverables.

## 8. Handover

* **P9** owns the restart flow. Every provider row is `restart` with an empty evidence string;
  `SaveProvider`'s comment and the binding table are where the classification lives.
* **P10** owns the hide list and the merge's own descriptors; the provider rows it must not hide are
  the ones whose values the adapter seeds (they are visible only when `provider.active` is true).
* **P11** owns the combined payload (§6.2), the `[ultimate_overrides]` write path, and seeding the
  compiled defaults for the rows the payload has no value for.
* **P12** owns the honest matrix: a provider row's effect is only observable after a save, a
  relaunch and a gameplay check, and the achievements action must not be presented as working until
  §6.1 lands.

