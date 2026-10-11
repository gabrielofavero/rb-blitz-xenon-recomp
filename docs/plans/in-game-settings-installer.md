# In-game settings — P7: the two folders the game reads (implementation record)

Status: **P7 complete** as a host-tested and silently-run lane. `installer/src/paths.{h,cpp}`, three new
helper commands, the wizard's install-folder page, the shortcut/finish defaults and the
Controller Settings terminology are all in place; the installer suite is **406 checks green** and the
wizard compiles with ISCC. Nothing outside `installer/` was touched (P5b and P8 ran in parallel in the
same tree).

Read order: `docs/plans/in-game-settings-plan.md` §9 (what this package owes) → this file (what it
built, what was measured, what is still open) → `installer/README.md` (the user-facing half).

Evidence labels: **measured** = executed in this pass; **static** = read from source, not run;
**proposal** = not implemented.

---

## 1. What the package was

Parent plan §9.1/§9.2, on `wpSelectDir` itself:

* install, DLC and save folders asked for on the same page, prefilled from explicit parameters >
  the installation's own `paths.toml` > the legacy launcher profile > defaults;
* the two external folders committed to `paths.toml` **only** after a successful install, and
  preserved on failure, update and uninstall;
* validation sharing the game's own path policy and DLC layout policy;
* documented unattended path parameters with the omitted-versus-empty rule;
* every shortcut task unchecked, explicit task choices honoured, interactive finish default = the
  game, silent default = nothing;
* the visible launcher text becomes **Controller Settings**.

## 2. What was built

| File | Change |
| --- | --- |
| `installer/src/paths.{h,cpp}` | New. `PathChoice` (present/value), `ReadPathFile`, `ReadLegacyPaths`, `ValidatePaths`, `WritePathFile`. The file format is **not** reimplemented: `settings::LoadPathFile` / `SavePathFile` from the emulator tree are compiled in (§4.4's shared SDK-free storage module). |
| `installer/src/commands.cpp` | Three commands: `read-paths` (prefill, read-only), `validate-paths` (the check), `write-paths` (the commit). `finalize` gained `--dlc-dir` / `--save-dir`. |
| `installer/src/install.{h,cpp}` | `InstallSummary` records the two answers; the manifest gains a `[paths]` table and the report a **Locations** block. |
| `installer/CMakeLists.txt` | Compiles `src/paths.cpp` plus `src/settings/{model,store,schema,paths}.cpp` and `src/launcher/{profile,profile_path}.cpp` from the emulator tree, with two warnings silenced for those files only (clang 19/C++20 sees one unused local and a duplicate `_CRT_SECURE_NO_WARNINGS` the game's build does not). |
| `installer/setup.iss` | The install-folder page grows two rows (DLC, saved games) with their own pickers and a status line; prefill, validation on Next and in `PrepareToInstall`; `write-paths` as the last stage; `DisableDirPage=no`, `UsePreviousTasks=no`, all four `[Tasks]` unchecked; `/DLCDIR`, `/SAVEDIR`, `/CONTROLLERICON`; finish default = the game, third choice = Controller Settings; ready memo and report carry the two paths. |
| `installer/tests/installer_tests.cpp` | `TestLocations()` and `TestWizardScript()`: 100 new checks (three of them the script's shape, which has no other harness). |
| `installer/README.md` | The page, the shortcut table, the silent parameters, the helper commands, `paths.toml` ownership, and the by-hand checklist the wizard half needs. |

## 3. Measured behaviour

### 3.1 The file (the contract with the game)

`write-paths --dest <install> --dlc-dir 'D:\Rock Band DLC' --save-dir 'D:\Rock Band Saves'` writes
exactly:

```toml
schema_version = 1

[paths]
dlc_root = 'D:\Rock Band DLC'
user_data_root = 'D:\Rock Band Saves'
```

* Literal strings, so a backslash needs no escaping; read back by `settings::LoadPathFile`.
* A value the caller did not answer **keeps what the file had**; an answered-but-empty value
  **drops the key** (the game's own fallback then applies); a missing file plus no answer writes
  nothing at all.
* Unknown keys, comments and hand edits survive (the game's own line editor does the patching).
* Identical answers produce **no write**: proved with a read-only file — the unchanged write
  succeeds, a changed one fails and the old bytes stay.
* A `paths.toml` this build cannot read is **never overwritten** (`write-paths` reports
  "`<file>` was left alone: …").

### 3.2 Validation

| Answer | Result |
| --- | --- |
| `--save-dir <install>\game`, `<install>\GAME\saves`, `<install>\game\sub\..\keep`, `<install>\game\saves\` | refused, `save_inside_game=1`, nothing created |
| `--save-dir <install>\saves` (missing) | accepted, created, write-probed, probe removed |
| `--save-dir <a file>` / `--save-dir <unreachable drive>` | refused with the reason |
| `--dlc-dir <title id>/<content type>/<package>` | `dlc_layout=structured`, package count, no package opened |
| `--dlc-dir <nested folder of files>` | `dlc_layout=flat`, file count, level-1 non-content names counted as rejected |
| `--dlc-dir <empty>` / `<missing>` | accepted — `empty` / `new`, with a note, because the game starts with an empty library |
| no `--dlc-dir` | `dlc_default=1`, `<game data>\dlc`, **no note** (the page does not warn about its own default) |
| `--no-touch` | the same report with nothing created: `save_created=0`, `save_writable=0`, the folder is not made and no probe is written. The check that runs while the page is merely shown uses it, so a path on its way to being corrected leaves nothing behind; Next (and `PrepareToInstall`) run the real one. |

The containment rule is `fs::IsSameOrInside` from `src/fs/path_policy.h` — the game's own rule,
including its weakly-canonical comparison — and the layout rule is `src/fs/dlc_layout.h`'s
`HasStructuredLayout` / `IsContentRootName`. A junction that points into the game tree is therefore
caught by the same canonicalisation `tests/path_policy_tests.cpp` covers on the game side.

### 3.3 The legacy profile as a prefill source

`ReadLegacyPaths` resolves the profile exactly the way the game does (`ResolveProfilePath`: explicit
override → settings-folder pointer → portable marker beside the executable → `%APPDATA%\rb_blitz`),
then reads `[settings] user_data_root`/`dlc_root` and falls back to `[launch] user_data_dir`/`dlc_dir`.
An empty app-data folder (what an all-users install passes) reads nothing, and a malformed profile is
reported, not guessed at.

**Measured on this machine:** a silent install with no folder parameters at all wrote
`dlc_root = 'F:\Games\Rock Band DLCs\360 (CON)\Singles'` — the developer's own `launcher.toml`, found
through that chain. A machine with no legacy profile writes no file in that case.

### 3.4 The real installer, silently (measured)

`installer/out/dist/RockBandBlitzSetup-0.1.0.exe`, built by ISCC from the modified script (27 s), run
against a scratch installation in `out/_swap/p7/` (the archived evidence is
`out/_swap/p7/evidence/`: the surviving `paths.toml`, Inno's setup log and the encoding probe —
the manifest and the report were removed by the uninstall that followed, which is itself the last
row of the table):

| Run | Command | Result |
| --- | --- | --- |
| fresh | `/VERYSILENT /DIR=… /GAMEFOLDER=<repo>\game /DLCDIR="…\DLC — músicas" /SAVEDIR="…\Saves de Gabriel"` | exit 0; 353.7 MB imported and fingerprinted; `paths.toml` written **last** (after the manifest/report), with both values; the save folder created; the report's **Locations** block and the manifest's `[paths]` table carry both; **no shortcut created** |
| reinstall, no parameters | `/GAMEMETHOD=installed` | exit 0; the legacy profile was the only source, and was stored |
| reinstall, both answers | `/DLCDIR="…\DLC — músicas" /SAVEDIR="…\Saves do Gabriel 2"` | exit 0; both keys written |
| reinstall, DLC answered empty | `/DLCDIR=` | exit 0; `dlc_root` **dropped**, `user_data_root` kept |
| save folder in the game tree | `/SAVEDIR=<install>\game\saves` | **exit 7**, `paths.toml` byte-identical, no folder created inside the game data; the log carries the sentence the user would have seen |
| uninstall | `unins000.exe /VERYSILENT` | exit 0; `paths.toml` **survives**, the game data is kept, the payload and the registry entry are gone |

Non-ASCII was checked end to end: a `—` (U+2014) and `ú` (U+00FA) in a `/DLCDIR` value arrive in
Inno's command line, through `{param:}`, through the helper's UTF-8 argv and into `paths.toml`
unchanged (codepoints recorded in `out/_swap/p7/evidence/encoding-probe.txt`).

The matrix ran against a setup executable built from the script as it stands, except for the
page-time `--no-touch` change made after it: a silent run never shows the folder page, so
`CurPageChanged` (the only caller of the inspecting check) is not reached, and `PrepareToInstall`
still runs the full check that produced the exit-7 evidence. Both builds of the script compile with
ISCC (`setup.iss`, and `/DUpdaterMode=1` for the other half).

## 4. Decisions worth keeping

* **`{param:NAME|@absent@}`** is how the wizard tells "not given" from "given empty". Omitted keeps
  the file; empty means the game's own folder. Without it, a silent install could not distinguish
  "leave it alone" from "reset it".
* **Nothing is answered ⇒ nothing is written.** `PathsAnswered` is false for the updater (no folder
  page) and for a silent install that was asked nothing *and* had nothing to prefill. A silent
  install that *did* prefill from the legacy profile writes what the folder page would have shown.
* **The wizard reads its answers from globals, not from the controls.** Every touch of the two rows
  is behind `gLocationsUi`, so a run whose page never exists (silent, or any future page-less mode)
  can neither crash on a nil control nor depend on the page being built. The controls are only the
  interactive front end for `gDlcValue` / `gSaveValue`.
* **The DLC folder is never scanned beyond a stat walk.** The per-package header read in
  `src/fs/dlc_library.h` costs ~17 ms each; counting entries at the first two levels keeps the page
  instant on a library of thousands, which is exactly why the page can afford to re-check on every
  Next.
* **Showing a page changes nothing.** The check that runs when the folder page appears takes
  `--no-touch`: it reports and refuses exactly as the real one does, but creates no folder and
  writes no probe. Creating the save folder is the job of the check that runs when the user moves on.
* **The free-space line moves, and is dropped when it does not fit.** It is Inno's own sentence about
  the folder above, not about the two rows this release adds; the page's `[Messages]` replacement
  already says the game data is not counted there.
* **`DisableDirPage=no`.** Inno's default hides the folder page when an install is registered, which
  is precisely when the two external folders most need to be visible. Inno still prefills the folder
  itself (`UsePreviousAppDir`).

## 5. Open items and handoffs

1. **The wizard half still needs eyes.** The two rows' geometry is 100%-scaled arithmetic via
   `ScaleX`/`ScaleY` and was not looked at on a real DPI setting — the developer should open the page
   at 100%, 200% and 300% once (`installer/README.md`, "The DLC library and the saved games" lists the
   other three by-hand checks). A page that fits at 100% and overflows at 300% is a one-line fix in
   `LayoutLocationFields`, not a redesign.
2. **The updater leg was not run.** It needs the release manifest over the network. Static: the
   updater never calls `write-paths` (`PathsAnswered` is false when `gUpdater`), so it cannot replace
   the file — but "it preserves an existing `paths.toml` across a real update" is unproven here.
3. **`settings::ResolvePathValue` reads a stored value as a *narrow* string.** `fs::path(value)` on
   Windows decodes that as the ANSI code page, while the helper writes and reads `paths.toml` as
   UTF-8. A save folder with a non-ASCII name therefore resolves correctly in the installer and
   *differently* in the game. This is a one-line fix in P3's file (`fs::path(Widen(value))`, the same
   conversion `installer/src/util.cpp` uses) and belongs to the coordinator: P7 must not fork the
   game's own reader, and the alternative — writing ANSI — would be wrong everywhere else the helper
   writes a file.
4. **P11/P12:** the install-folder page is now a three-question page; `acceptance_launcher.ps1`'s
   assumption that settings travel as argv is unaffected by P7 (no setting is emitted on a command
   line), but the installer's output directory assumptions in the acceptance scripts should be
   re-checked when the merged Help & Options screen lands.
