# The launcher → Controller Settings rename

Status: **executed** (2026-10-14). Request: "rename the launcher for something like controller-settings
— it is no longer a launcher, as most settings were externalized to the in-game settings". Scope
chosen by the owner: **the full identity rename** (source directories, targets, executable, module and
test names), **keeping the two compatibility names** (`launcher.toml`, `--launcher_profile`).

Supersedes the naming decision in
[`../../settings/plan.md`](../../settings/plan.md) §11 (P8) and
[`../../settings/menu.md`](../../settings/menu.md) (part 3, §3.4): both told an implementation to *keep*
`rb_blitz_launcher.exe` "to avoid breaking existing shortcuts". Nothing has shipped (README.md: "This
is not released yet"), so no released shortcut existed to break; the rename landed instead. The
finished record [`2026-09-27-launcher-plan.md`](2026-09-27-launcher-plan.md) is left
as it was written — it is the plan of work done while the application had its old name — and only its
file references were re-pointed, as its links name files that moved.

## 1. What the word named

Four different things shared the name, which is why the rename needed decisions rather than a
search-and-replace:

| # | Name | Kept or renamed |
| --- | --- | --- |
| 1 | `launcher/` — the source directory of the second executable | renamed → `controller-settings/` |
| 2 | `rb_blitz_launcher` — CMake target, `rb_blitz_launcher.exe`, `.rc`, the schema target | renamed |
| 3 | "Rock Band Blitz Launcher" — the window title | renamed |
| 4 | `launcher.toml`, `--launcher_profile`, the profile's `[launcher]` table, the cvar `launcher_profile` | **kept** (see D1) |

## 2. Decisions

| Id | Decision | Why |
| --- | --- | --- |
| D1 | `launcher.toml`, `--launcher_profile` and the `[launcher]` table keep their spelling, and so does the cvar `launcher_profile` that the flag maps to | they are a *file on disk* and a *command line* contract, not product names: the game's own boot (`src/settings/boot.cpp`), the installer's path prefill (`installer/src/paths.cpp`) and every harness read them, and a user's existing profile must keep working |
| D2 | The source names follow one scheme: `controller-settings/`, target and exe `rb_blitz_controller_settings`, schema target `rb_blitz_controller_settings_schema`, ctest names `controller_settings_*`, test files `tests/controller_settings_*_tests.cpp` | one word for one thing; the old target/test names (`rb_blitz_launcher_*_tests`) were the only remaining place the product called itself a launcher |
| D3 | The shared profile/remap module moves `src/launcher/` → `src/profile/`, namespace `rb_blitz::launcher` → `rb_blitz::profile` | the module is the profile format and the remap vocabulary, compiled into *both* executables; it was never the launcher's own code |
| D4 | `cvar`, `IsLauncherOnlyCvar` / `kLauncherOnlyCvarAllowlist` → `IsHostOnlyCvar` / `kHostOnlyCvarAllowlist`, and "launcher-only row" → "host-only row" | the concept is "a row only the host application owns", which is what it now says |
| D5 | `update_launcher.{h,cpp}` → `updater.{h,cpp}` | the module is the fetch and the updater run; "launcher" there meant "the thing that runs the updater" |
| D6 | The process-spawner identifiers keep the word: `ChildLauncher`, `CreateChildLauncher()`, `FakeLauncher` (`src/platform/game_restart.*`, `tests/game_restart_tests.cpp`) | they launch the *child process* of the restart handshake. That is a different noun, and a reader looking for "what launches a child" should find it |
| D7 | `WindowTitle()` takes no argument: both surfaces are titled "Rock Band Blitz Controller Settings (vX)" | the reduced and full surfaces are one application and the surface is a development gate; the harnesses already asserted one title string for both legs |
| D8 | `--controller-only` keeps its name | it is a behaviour switch, and the cutover that would retire the full surface is still P11 (`../../settings/contracts.md` §9.3) |
| D9 | The environment override is `RBBLITZ_CONTROLLER_SETTINGS_PROFILE` (was `RBBLITZ_LAUNCHER_PROFILE`) | read by `getenv` in `src/profile/profile_path.cpp` only; no script or document set the old name |
| D10 | Evidence directories: `out/controller-settings-capture`, `out/controller-settings-acceptance` | both are gitignored scratch, so no committed evidence path changed |
| D11 | The installer still accepts the legacy spellings `/LAUNCHERICON=1` and `RUNATEND=launcher`, and `[UninstallDelete]` names `rb_blitz_launcher.exe` | an install made from a pre-rename build leaves the old executable behind; the parameter and task spellings were already documented as the older spelling of the new ones |
| D12 | Historical documents (`docs/plans/**`, `docs/history/**`) keep the words they were written with; only their file references were re-pointed | a record is a record; the successor is this document |

## 3. What changed, mechanically

`[tree]` Before the rename: 164 tracked files mentioned "launcher" and 30 named the executable.

| Step | Files |
| --- | --- |
| `git mv` of two directories, two scripts, nine test files, the `.rc` and two `src/`-module files | 16 moves |
| Identifiers, paths, CMake targets, macros, test names (`out/_swap/rename-cs/rename.py`) | 129 + 37 + 7 |
| Prose in live documents and code comments (`out/_swap/rename-cs/rename_prose.py`) | 121 + 3 |
| Code-position repairs the prose pass could not see (`out/_swap/rename-cs/fixups.py`) | 42 |

The scripts and their dry runs live in `out/_swap/rename-cs/` (gitignored): `inventory.py` (the token
histogram the rule table was written from), `rename.py`, `rename_prose.py`, `fixups.py`,
`find_code_hits.py` (the check that found the three code positions a word-level rewrite cannot
distinguish from prose), and the build logs `build.log`, `build2.log`, `tests-build.log`.

**Three collisions the pass created and the run caught**, kept here because they are the failure modes
of this kind of change:

1. Two Pascal functions merged into one name — `ParamLauncherIcon` and `ParamControllerIcon` both
   became `ParamControllerIcon` in `installer/setup.iss` (ISCC would have refused the second
   definition). Fixed as `ParamLegacyIcon` / `ParamControllerIcon`, which is also the honest split:
   one reads the legacy parameter, one the current one.
2. A parameter and a test local named exactly `launcher` became `Controller Settings`
   (`src/platform/game_restart.h:196`, `tests/game_restart_tests.cpp`), which does not compile. Fixed
   as `child` / `child_`.
3. The profile's **on-disk table name** — `FindKey(layout, "launcher", …)`,
   `Schedule(layout, …, "launcher", …)` in `src/profile/profile.cpp` — was rewritten to
   `"Controller Settings"`, so the reader no longer found the table and the writer appended a second
   one (four assertions in ctest `controller_settings_profile` failed with a byte-identical-save
   comparison). Fixed, and the string is now named in the prose script's protected list.

## 4. Verification

| Check | Result |
| --- | --- |
| `[tree]` configure + build, release preset | `rb_blitz.exe`, `rb_blitz_controller_settings.exe`, all tests link; no errors (`out/_swap/rename-cs/build2.log`) |
| `[measured]` host tests | **37/37 pass** (`ctest --test-dir out/build/rename-tests`, 8.0 s). That tree is a Vulkan-off scratch tree (`out/build/rename-tests`) because no Windows *preset* can build a test today — the documented open item in `../../settings/status.md` |
| `[measured]` installer project | `installer` preset: builds, **1/1 test passes** (406 checks), `setup.iss` still compiles its shape checks |
| `[measured]` the compatibility names | `--launcher_profile`: 22 files before, 22 after. `launcher.toml`: 32 before, 32 after. `[launcher]`: 8 files held it before the prose pass and 8 after |

## 5. Not verified (the honest half)

- **No interactive run.** Neither the game nor the renamed application was booted after the rename:
  the exe names, the window title and `--controller-only` are asserted by tests and by text, not seen
  on a screen.
- **The harnesses were re-pointed, not re-run**: `scripts/capture_controller_settings.ps1` (the window
  title regex, the process name, the evidence directory) and `scripts/acceptance_controller_settings.ps1`
  (the payload's exe name). One pre-existing stale assertion was found while re-pointing: the remap
  check expects `remap: 1 pad control(s) rebound by the launcher profile`, while the game has logged
  `remap: <n> user rebind(s) from the profile` since the P8 rework. It is left as found and recorded
  here rather than silently retargeted.
- **The old-name cleanup is pinned but not exercised**: `installer/tests/installer_tests.cpp` reads the
  script's shape; no real uninstall of a pre-rename install was run.
- **`installer/build.ps1`'s renamed switches** (`-SkipControllerSettings`, `-ControllerSettingsTarget`)
  are not exercised by any test, and `-LauncherDir` on `tools/make_payload.ps1` became
  `-ControllerSettingsDir`.
- **`docs/settings/status.md`'s item 29** ("the full launcher is still the default surface") still
  describes the *behaviour*, which did not change; only the wording moved. Flipping that default
  remains P11's job.

## 6. Reproducing the measurements

```powershell
# configure + build the release tree
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release

# the tests need a tree the SDK's glslang guard leaves BUILD_TESTING=ON in: Vulkan off
cmake -S . -B out/build/rename-tests -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DREXGLUE_USE_VULKAN=OFF -DBUILD_TESTING=ON
cmake --build out/build/rename-tests
ctest --test-dir out/build/rename-tests --output-on-failure     # 37/37

# the installer's own tree
Push-Location installer; cmake --preset installer-release
cmake --build --preset installer-release; ctest --preset installer-release; Pop-Location

# the reference check
python .github/skills/documentation/scripts/check_docs.py
```
