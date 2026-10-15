# Play Current Song (2026-10-14)

**Status: the press works end to end (2026-10-15) — the 2026-10-14 stall was a `String` handed to a
`Symbol`-reading lookup, fixed and measured through `loading_screen` to `game_screen`.** The
2026-10-14 finding stands as measured — every statement of the body ran and the id answered 0 — and
its cause is now known: `SongMgr`'s native handler (`0x8244938C` → `0x823F94A0`) reads an interned
**Symbol**, and the body handed it a **String**. See "2026-10-15: the landing, measured" at the end
of this file; the title's own-list fault (A8) remains open and is not claimed fixed. The paragraphs
below are the 2026-10-14 state, kept as measured.

Owner request, normalized into requirements:

| Id | Requirement |
| --- | --- |
| R1 | A new setting beside the DLC background-music row: on by default, on Help & Options → Downloadable Content, and wired to it (the row is unavailable when the background music is off) |
| R2 | A second row in the main menu, `PLAY CURRENT SONG`, immediately below `PLAY` |
| R3 | Pressing it starts **the song the menu is playing** as the background music: no list, no manual pick |
| R4 | The landing is the game's own: the power-up screen on the Ultimate target, and on the offline base target the retail first screen the song list's own Confirm reaches |
| R5 | The original R9 toggle (`DLC Menu Songs`) must also exist as a visible in-game row — the owner believed it already shipped |

## What exists today, and what does not

- **Exists (host, tested):** the setting row and its token (`play_current_song`), the revived
  `dlc_menu_songs` row, the probe channel that tells the record which pool song is playing, the DTB
  edit that inserts the menu row and its switch case, and the payload's gating of both on a boot that
  really has a pool. `ctest` is 37/37 in the test tree, including 27,767 checks in
  `rb_blitz_settings_screen_tests`.
- **Exists (guest, boot-verified):** the row is drawn second in the main menu, a press is answered by
  the host, and the body runs to its last statement ([boot] §Evidence A1–A3).
- **Does not exist:** a proven press → song. `{song_mgr get_song_id_from_short_name <key>}` answers
  **0** for the pool's own song key at main-menu time ([boot] A4), and the title's own library has the
  same fault one step further on: starting or previewing a DLC song faults the guest ([boot] A8, A9).

## Decisions

| Id | Decision | Why |
| --- | --- | --- |
| D1 | The setting lives on the merged DLC page (`Help & Options → Downloadable Content`), group `dlc`, storage key `general.play_current_song`, `bool`, default **true** | Owner chose the page; it is the page the background-music row is on, which is what "wired to one another" needs to read as |
| D2 | The menu row is **absent** rather than disabled when there is no pool, and the settings row reads unavailable | A row that exists must work; the settings screen's own vocabulary already has `disabled[token]` for the second half |
| D3 | The row is inserted directly after `splash_start` in the option array, and its case appended to the same dispatch switch | §1 of [main-menu.md](../../engine/main-menu.md): that array is what the main menu draws |
| D4 | The host answers "which song" over the `file_exists` channel only (`rbbz-play/current/<i>`), one probe per pool entry baked into the record | A guest script cannot be handed a string from the host; this is the channel the project already owns |
| D5 | The song key is the pool entry's own folder name (`\songs\<key>\<key>.mogg`) | That is the package's own spelling, and what R9's substitution already uses |
| D6 | The body consults the loading screen's own order — `get_song_id_from_short_name` → `song_mgr data` → `loading_screen set_song` + mount, then the song list's `song_chosen` | Both sequences are shipped records; nothing in the body is invented |
| D7 | **A press whose id comes back 0 does nothing.** The body guards on the id | Measured A4: an unknown name answers 0, and `song_mgr data 0` hands the loading screen a null song, which the title dereferences a second later (A1–A3, A5, A6 all fault at the same place). A guard turns that into a no-op |

## Prompts

| Id | Prompt | Deliverable | Evidence |
| --- | --- | --- | --- |
| P1 | Add the setting and revive the R9 row | `src/settings/{model.h,schema.cpp,cvars.cpp,boot.cpp}`, `src/enhancements.cpp` | `rb_blitz_settings_*_tests`, [toggles.md](../../engine/toggles.md) R9 row |
| P2 | Insert the menu row and its case | `src/ui/settings_screen.cpp` (`AddPlayCurrentSongRow`) | 27,767 checks in `rb_blitz_settings_screen_tests` |
| P3 | Answer the press from the host | `src/hooks/shell_music.{h,cpp}` (`AnswerPlayQuery`), `src/hooks/settings_bridge_hooks.cpp` | A1's press line |
| P4 | Boot-verify the press | `out/_swap/play-current/run.ps1` and its arms | A1–A12 below |

## Evidence

All 2026-10-14, one build tree, one disposable library per arm; the arms and their logs live under
`out/_swap/play-current/` (gitignored). The arm is `pwsh -File out\_swap\play-current\run.ps1 -Label
<name> [-Songs …] [-Route …] [-RealDlc] [-CopyPackages]`.

| Id | What was run | What it showed | Evidence |
| --- | --- | --- | --- |
| A1 | mini library (2 real packages, symlinked), drive the menu to row 2 and press | the row is built (`settings_menu: "DLC Menu Songs" is on; 2 menu-music song(s); the Play Current Song entry is built as the menu's second row`), the press reaches the host (`shell_music: Play Current Song: the menu is playing pool 1 of 2 ("o332989534_freebird_lynyrd")`), and ~1 s later the guest faults (`Unhandled guest access violation: read of guest 0x00000004`) | `default/default.log`, `default-shots/03-later.png` (the row, second, under `PLAY`) |
| A2 | the same arm with **one** package, so the press always names the clean key `highvoltage_live` | the same press line and the same fault — the key's shape is not the trigger | `acdc-run.txt`, and the boot log of that hour |
| A3 | temporary staged traces in the body (`rbbz-play/trace/<n>` between the statements) | **every** statement of the body runs, `song_chosen` included; the fault follows the body's end | `trace/trace.log` |
| A4 | temporary value probes over the same channel | `{song_mgr get_song_id_from_short_name "highvoltage_live"}` **== 0** while `{song_mgr num_songs}` is non-zero; the on-disc name `roam` also answers 0 | `probe3/probe3.log` (traces `trace/10`, `trace/20`, `trace/23`) |
| A5 | the loading screen's own first step added before the lookup (`{content_mgr refresh_synchronously}`) | unchanged: the id is still 0 | `sync/sync.log` (trace `7` runs, trace `10` still fires) |
| A6 | entering the title's `YOUR SONGS` list first, then backing out and pressing the row | unchanged | `visit/visit.log` |
| A7 | entering `YOUR SONGS` and capturing it | the list **does** name both DLC songs (`AC/DC — High Voltage (Live)`, `LYNYRD SKYNYRD — Free Bird`), so the library is built and mounted | `library-shots/02-library-later.png` |
| A8 | **the title's own control**: `YOUR SONGS`, scroll two rows, confirm | the title loads the package's STFS header, runs its XeKeys AES path, and then faults **identically** (`read of guest 0x00000004`) — so the fault is not the new row's | `retail-lib-run.txt`, `retail-lib-shots/02-selected.png` (the selection the control made) |
| A9 | A8 with the package **copied** instead of symlinked | the same fault — presentation of the package is not the trigger | `realcopy-run.txt` |
| A10 | **the bundled control**: no library at all, the same route to `These Days` | the song starts and lands on the retail first screen (`In Rock Band Blitz, it's all about earning big points… PRESS (A) TO BEGIN`) — the route, the drive and a song start all work | `bundled/bundled.log`, `bundled-shots/04-later.png` |
| A11 | the shipped body (D7's guard in place), press | the press is answered and **no fault follows**; the row stays in the menu | `guard/guard.log` |
| A12 | the project's own DLC root (`<game>/dlc`, the 803 MB soundtrack package) | the walk finds it and R9 builds a 50-song pool, but the title enumerates **3,546** items and a key sent during that enumeration stalls it at 25 % (so a real-library arm must let the discovery finish the *new* enumeration, not the boot's own 4-item one) | `realdlc/realdlc.log`, `realdlc4/realdlc4.log` |

## Open questions

- **Does the owner see the same fault?** The title's own list faults here (A8, A9) with packages that
  `docs/guides/dlc.md` §2.1 says "list and play"; either that claim predates a regression, or this
  library's packages are the difference. Reproducing A8 on the owner's own library is the next
  measurement, and it decides whether D7's guard is a temporary safety net or the shipped behaviour.
- **Where exactly does the fault come from?** The last lines before it are the guest's own AES-CBC
  call on the package's stream header and the STFS header read, both of which a *bundled* song start
  also does (A10) — so the fault is after them, in whatever the content-backed song start does next.
  Not measured: a probe trace (`probe_trace=true`) or a register dump at the faulting PC.
- **Is 0 "not found" or a legitimate first id?** A4 reads 0 as "the library does not carry that name",
  which is also what the guard assumes. If it is a legitimate id, D7 is wrong and the guard must use a
  different validity test.
- **The song key.** A5/A6 aside, the pool's keys are package folder names, and the real soundtrack
  library shows both plain ones (`blackholesun`, `hierkommtalex`) and `o<offerid>_<name>` ones
  (`o134867937_theturning_oasi`) — unproven: that a package's folder name is always the name its song
  manager entry carries.

## Reproduce

```powershell
cmake --build --preset win-amd64-release
pwsh -File out\_swap\play-current\run.ps1 -Label guard                 # the shipped body
pwsh -File out\_swap\play-current\run.ps1 -Label retail -Route retail-play   # A8's control
pwsh -File out\_swap\play-current\run.ps1 -Label bundled -Route retail-play -NoLibrary  # A10
```

The arms need a real library (the script's default points at the machine's own
`<library-root>\Singles` folder; override `$Songs` and the symlink source elsewhere) and stop
`rb_blitz` if a previous arm left it. The reason the drive has to wait: the boot lands on
`PRESS (A) TO START`, and a key sent during the content discovery stalls the enumeration (A12).

## 2026-10-15: the landing, measured

**The cause of the 2026-10-14 no-op.** The body was not wrong about *what* it asked — it handed
`{song_mgr get_song_id_from_short_name <key>}` the pool's own key and the manager answered 0. The
argument's **kind** was wrong: `SongMgr`'s native handler (`0x8244938C`, which interns its argument
through `0x823F94A0`) reads a **Symbol** payload, and the DTA baked a **String** — so no name could
ever match, on-disc names included. The keys are `Sym("…")` nodes now
([src/ui/settings_screen.cpp](../../../src/ui/settings_screen.cpp), pinned by
`tests/settings_screen_tests.cpp`). Two supporting changes came with it: the body calls
`{song_select_panel set_current_song}` with the resolved metadata so the song list's own
current-song state agrees with what plays, and a press with no published track (or a name the
manager does not carry) opens `settings_play_unavailable_screen` — *"The background song is not
available to play. Please try again when another song starts."* — instead of the old silent no-op.

**Evidence (`out/_swap/music-fix/final-press/`, 2026-10-15).** Two real packages symlinked, a
disposable profile, windowed D3D12, `rb_blitz.exe` `A0B583A6…` (its `hashes.json` records all
three binaries). The route: enter the main menu, wait until `highvoltage_live` is audibly playing,
select row 2, press.

| Line / capture | What it is |
| --- | --- |
| `shell_music: Play Current Song: the menu is playing pool 1 of 2 ("highvoltage_live")` | the probe answered the **audible** song |
| `shell_music: the game took this menu track down; the rotation stops watching ("highvoltage_live")` | the game's own stop, caught by the menu-music hooks — and **no** deck advance follows |
| `native_mouse: screen=loading_screen` (1 s later) + `Loading STFS header file: …\ACDC - High Voltage (Live)` | the loading screen mounts the song the press resolved |
| the game's first screen (*"In Rock Band Blitz, it's all about earning big points… PRESS (A) TO BEGIN"*) → `screen=game_screen` | the retail landing, reached with zero guest faults |
| `shots/{row2,loading,game2,gameplay}.png` | row 2 highlighted; the loading screen; the first screen; the note highway |

**Not claimed.** The title's **own** list fault (A8) is untouched by this fix and stays open in
[known-issues.md](../../reference/known-issues.md); the unavailable-page arm was not driven at
runtime (its screen and label are pinned by the host tests); and the Ultimate-target landing
(`powerup_select_screen`) was not re-measured on this build — the press was taken on the offline
base target.
