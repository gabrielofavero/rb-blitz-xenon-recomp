# In-game settings P10 — merged-menu compatibility, hide list, refresh, retired settings

Status: **P10 executed** (2026-10-11). The hide matrix §3.5 states in words is a decision function
with tests, the effective menu is read from the record the title actually loads, the pool is fixed at
50 songs, and the retired pool size says out loud that it is ignored. Five boot arms are in
`out/_swap/p10/`.

Parent plan: [in-game-settings-plan.md](in-game-settings-plan.md) §3.5, §3.6, §10.1 (P10).
Baseline: `b940bd1` plus the P9 working tree. Nothing committed.

---

## 1. What this package is

`Hide Main Menu` used to be a CSV of hardcoded names behind a branch in `src/hooks/menu_filter.cpp`,
and the pool size used to be a cvar. Both were places where the product's rule lived in code that
only a boot could check. P10 turns each into a decision over plain data, and reads the menu it is
talking about from the file the title loads.

| Piece | File | What it owns |
| --- | --- | --- |
| The decisions | `src/settings/menu_compat.{h,cpp}` (new, SDK-free) | The hide matrix, `min(50, library)` pool, the retired count's diagnostic, the rename's scope |
| The effective menu | `src/ui/menu_options.{h,cpp}` (`MenuInventory`) | The option array of the record the title will load: every row, in draw order, with its conditionality |
| The hook that applies it | `src/hooks/menu_filter.cpp` | Reads the decision, patches the splash record, logs what it decided and why |
| The pool | `src/hooks/shell_music.cpp` | Fixed cap; the retired value is reported, never obeyed |

## 2. The hide matrix, decided rather than branched

```text
DecideHiddenRows(HideRequest{master, selection_explicit, selected, defaults, protected_row,
                             refresh_row_owns_refresh}) -> HideDecision{hidden, refused,
                             refresh_row_left_visible, alternate_refresh_needed, note}
```

The four rules, each of which was a bug or a silent behaviour before:

| Input | Result | Why it matters |
| --- | --- | --- |
| master off | nothing hidden; the selection is untouched | §3.5 "Off disables filtering while preserving checked choices" |
| master on, no selection (the setting is absent) | the compiled default trio | today's behaviour, kept |
| master on, **explicitly empty** selection | **nothing hidden** | §3.5 "On with an empty selected list hides nothing" - the old code fell back to the defaults, so clearing the field re-hid the rows the player had just removed |
| master on, custom selection | exactly those rows, de-duplicated, in the selection's order | and an entry that is not a row of the menu simply does not match, which is what the inventory (§3) makes visible to the player |
| any selection naming `splash_options` | refused, **and reported** by name | the merged product's only settings entry must never become unreachable; a silent refusal is how a user loses their settings screen |
| the default trio with R7 active | `splash_dlc` **kept**, with the reason | §3.5 "Default offline rows must not hide Refresh Song Library when `splash_dlc` has that job" |
| an explicit selection naming `splash_dlc` while R7 is active | hidden, and `alternate_refresh_needed` is set | the user's explicit choice wins; the caller is told the refresh has no row left |

`selection_explicit` is the whole point of the structure. On the cvar path it is
`configured != kDefaultRows`: an absent setting is the compiled trio, and *anything* written - 
including nothing at all - is the user's own choice. The native store expresses the same distinction
properly (`[interface] hidden_main_menu` is a list whose compiled default is `kNone`, "never chosen";
an empty list is a real empty list), which is what P11 wires.

## 3. The menu that is actually there

`menu_options::MenuInventory(file, available)` walks the option array of the splash record it is
handed and reports every row in draw order with `conditional` set when the row sits inside an
`#ifdef`/`#ifndef` block. Two deliberate limits, both stated in the header: it does not resolve the
locale (a row's *label* lives in `ui/locale/*/gen/locale_keep.dtb`, a different record - the caller
pairs token and label, because the caller is what has both), and it does not drop conditional rows,
because "this row only exists with macro X" is exactly the availability the list has to show.

That inventory is what feeds the row's choices (`schema::SetMenuRows`), so the Hide Main Menu submenu
lists the menu's real rows rather than a hardcoded copy - including any row a mod or our own payload
adds. The test in `tests/menu_options_tests.cpp` reads a synthetic record and asserts the row order
and which rows are conditional.

## 4. The pool is fixed at 50

`menu_compat::PoolCap(usable) = min(50, usable)`, and `shell_music` reads no size setting any more:
`enhancements_menu_dlc_song_count` is consulted only to report that it is ignored
(`LegacyPoolCountNote`), and the boot says both facts before it decides whether to mount anything at
all. Old profile values therefore cannot re-enable configurability, and a hand-edited config is not
silently obeyed either.

## 5. The rename, scoped

The mod-row rename (`R10`) targets the mod's own locale key, `mod_settings`, and nothing else:
`menu_compat::RenameCoversRow(key)` is the guard, tested against the protected entry, our panel name
and the empty key. It is the mechanical form of "the retired transform must not overwrite the merged
root's label". Retiring the *user-facing* toggle (the launcher row, and the mod row's routing) belongs
to the cutover, and it is listed in §7.

## 6. Evidence

Harness: `out\_swap\p10\run.ps1 -Label <name> [-HideRowValue <csv>] [-DlcCache] [-SongCount <n>]`.
Each arm boots `rb_blitz.exe` against a disposable `--user_data_root`, waits for the decision lines in
the log and stops the game. The hide selection travels in the **environment**
(`REX_ENHANCEMENTS_HIDDEN_MENU_OPTIONS`), because a command line cannot express "present and empty" -
an empty `--flag=` makes the SDK's parser take the *next* argument as the value (measured, and that is
how the first attempt ended up asking to hide `--enhancements_menu_dlc_song_count=64`).

| Arm | Log | Observation |
| --- | --- | --- |
| `empty` (master on, nothing selected) | `out/_swap/p10/empty/empty.log` | `menu_filter: R5 on: hiding 3 row(s): splash_leaderboard, splash_achievements, splash_dlc` and the byte-neutral edit applied (71 bytes into the array's `#ifdef` name) - the *absent* case, i.e. the compiled default |
| `custom` (master on, two rows) | `custom/custom.log` | `hiding 2 row(s): splash_start, splash_leaderboard`, edit applied (46 bytes) |
| `protected` (master on, `splash_options` + one row) | `protected/protected.log` | `hiding 1 row(s): splash_start; refused to hide the protected entry (splash_options)` - the edit hides exactly one row |
| `refresh` (master on + the default trio + R7) | `refresh/refresh.log` | `hiding 2 row(s): splash_leaderboard, splash_achievements; splash_dlc stays because it refreshes the DLC library`, plus `the downloadable-content row refreshes the DLC cache and is drawn as "Refresh Song Library" (R7)` and `... now refreshes the DLC cache (323 bytes of its action paid for the call)` - **the refresh path is unbroken by the new decision** |
| `poollgcy` (`--enhancements_menu_dlc_song_count=64`) | `poollgcy/poollgcy.log` | `shell_music: the menu's DLC pool holds up to 50 song(s) (fixed, plan 3.5)` and `the retired menu-pool size (64) is ignored; the pool is fixed at 50 songs` |

**Not measured in a boot: the explicitly-empty selection.** Both remaining spellings were tried and
both are blocked outside the product: an empty environment variable reads as *unset*, and a trailing
`--enhancements_hidden_menu_options=` produced a boot that never saw `--game_data_root` (the harness's
own argument passing). The rule itself is unit-tested (`empty selection hides nothing`) and has a
product spelling the store already models; §9.3 of the plan's acceptance matrix can exercise it once
P11 wires the store.

Tests: **34/34** `ctest` green. New target `menu_compat` (17 checks in 11 cases: the whole hide
matrix, the pool cap and its retired value, the rename's scope, and the hide-list seam where an empty
selection differs from no selection), plus the inventory case appended to `menu_options`.

## 7. What P10 did not do

* **The launcher-side row removal.** The retired rename toggle and the pool-size row are still rows in
  `launcher/config/settings.toml`. They are now inert in the game (P10 reads neither), but removing
  them from the launcher surface is the remaining P8 work and the P11 cutover, not this package.
* **The alternate in-menu refresh action.** When a user explicitly hides `splash_dlc` while it carries
  the refresh, the refresh has no main-menu row; the boot warns, and the alternate action (a row under
  Downloadable Content that calls the same sentinel) is §3.5's own follow-up. No second refresh
  implementation was added, which is what the plan forbids.
* **Reviewing the whole menu model against the merged tree.** The plan's P10 prompt also mentions
  "both menu variants" and the protected root in the *descriptor* list; the seam exists and is tested,
  but the boot wiring that fills it from the live menu (and therefore a menu screenshot with the
  Hide Items list in it) belongs to P11, which owns the store and the provider routing.
* **Package-handle lifetime for a 50-song pool.** The cap grew from 3 to 50, so a library of
  single-song packages now mounts up to 50 containers at once. `shell_music`'s walk already bounds the
  mounts by the same number and skips what it cannot mount, but a real 50-package library has not been
  booted since the change; that is the acceptance item, and it is called out rather than assumed.

## 8. Reproduce

```powershell
cmake --preset win-amd64-release
cmake --build out\build\win-amd64-release
ctest --test-dir out\build\win-amd64-release --output-on-failure            # 34/34

pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label custom -HideRowValue 'splash_start,splash_leaderboard'
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label protected -HideRowValue 'splash_options,splash_start'
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label refresh -DlcCache
pwsh -NoProfile -File out\_swap\p10\run.ps1 -Label poollgcy -SongCount 64
```
