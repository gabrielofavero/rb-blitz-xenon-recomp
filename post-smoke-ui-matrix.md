# Sequential Windows UI regression matrix

Historical preparation checklist. Current execution and limitations are recorded in the
[migration plan](docs/plans/in-progress/2026-11-17-rexglue-v0.11-migration-plan.md).

Prepared 2026-11-18 by read-only inspection. **No UI action was executed for this
matrix.** Reuse already retained current-build evidence; a checklist is not a pass.
At this checkpoint the D3D One Week/pause/results/clean-close smoke was recorded,
and the Vulkan counterpart was pending. The narrow MiB logging correction is now final:
200 MiB is supported without changing the existing acceptance harnesses. Build
the latest title before these checks. SDK files were not changed by this work.

## Execution boundary

Run one game at a time and stop at the first failure. Retain console, screenshots,
complete logs, argv, settings/profile snapshots and summaries under a fresh
`out/rexglue-migration/post-smoke-ui-r1/`. Each script's `BuildDir` is relative to
the prototype, so use `../title-link-build`, not an absolute child passed to
PowerShell Join-Path. The prototype's `game/default.xex` exists; explicitly pass
the shared real game root to harnesses that expose `GameRoot`.

Before any harness that clears build/logs, copy the entire existing log directory
into that leg's evidence directory. Copy the post-leg build logs even after an
exception, before another harness clears them. Controls/recovery write explicit
private logs, reject an existing label, and retain their profile snapshots.

For each leg that does not set its own launcher/cache paths, use a fresh private
profile copied from `matched-performance-seed-r1/profile/` and a private cache.
The title already supports process environment aliases `REX_LAUNCHER_PROFILE`,
`REX_CACHE_ROOT`, `REX_GPU_BACKEND`, `REX_ULTIMATE_MODE`, and `REX_DLC_ROOT` through
its actual runtime binding table. Set them only in the child harness environment,
then restore the caller's process environment. CLI arguments outrank these aliases.
Controls/recovery have their own explicit profile and DLC roots; keep those.
Do not override their intentionally seeded input gates with a generic profile.

All deletion/corruption targets must resolve beneath the fresh evidence/user-data
root before invoking save/persistence/DLC scripts. They deliberately delete or
damage their private fixtures. Never pass a normal user-data folder to them.
No harness here is allowed to alter source game/DLC packages; package sources are
read-only mounts. Do not use installer/alpha orchestration to refresh a payload
or replace an existing installation as part of this bounded matrix.

## Short sequence

Each command below names an existing route; none adds a new UI flow or overlay.
`$P` is the absolute prototype root, `$G` the absolute shared `game` root, and `$E`
is the root-relative-to-prototype spelling `../post-smoke-ui-r1`. Invoke scripts
with `powershell -NoProfile -ExecutionPolicy Bypass -File` and retain exit codes.

| Order | Existing route / exact script arguments | Required evidence |
| --- | --- | --- |
| 1 | Common-flags functional pilot: `python out/rexglue-migration/run_matched_performance.py --config out/rexglue-migration/matched-performance-pilot-config.json --output out/rexglue-migration/matched-performance-pilot-r1 --execute` after correctness, latest build and idle host. | One upgraded retail D3D cold song, actual renderer PID, accepted complete flags, named results and clean exit; PresentMon/process/trace capture quality. Pilot is explicitly not five-pair performance acceptance. Exact environment-only and argv round trips already passed without UI. |
| 2 | Reuse retained retail D3D and Vulkan `acceptance_song.ps1` smoke evidence when it belongs to the current functional bytes. If a backend needs a new route: `-BuildDir ../title-link-build -OutDir $E/retail-vulkan -GameRoot $G -Runs 1 -Song 2 -SongName 'ONE WEEK' -UltimateMode 0 -UserDataRoot <leg/user> -SelectKey key_enter -Pause -Extra '--gpu_backend=vulkan;--launcher_profile=<leg/profile/launcher.toml>;--cache_root=<leg/cache>;--dlc_root=<empty>'`. | Title/list/playing/named results, pause then resume, full playback envelope, no fatal, normal exit. For D3D substitute only backend/output identity. Root's reported current D3D smoke has an 11s pause and 191s envelope; do not turn that into a performance result. |
| 3 | Ultimate D3D and Vulkan: two separate `acceptance_song.ps1` calls with the same isolated args as order2, `-UltimateMode 1`, backend `d3d12` then `vulkan`, separate user/profile/cache/output roots; one play each, no need to repeat pause coverage. | Named song/results/clean exit plus title-owned Ultimate payload/patch-mask, content-device and overlay mount decisions; actual Ultimate content lookup; no dirty-disc or MODIFIED abort. Retail mode0 control is order2. Both payload header/archive exist in current game/ultimate. Do not infer a backend pass from the other backend. |
| 4 | `acceptance_controls.ps1 -BuildDir ../title-link-build -Label migration-ui-r1-controls-d3d -Backend d3d12 -BootTimeoutSec 300 -StepSeconds 4`; then `acceptance_controls_in_song.ps1 -BuildDir ../title-link-build -Label migration-ui-r1-controls-song -BootTimeoutSec 300 -TitleTimeoutSec 240 -StepSeconds 3`. Force only private cache + retail/backend environment; preserve each script's private profile/DLC fixtures. | Both report zero failures. First covers guest settings hierarchy, bind capture, default replacement, countdown/no write, wrong-device-kind refusal, reset and row text. Second covers pause-context settings/capture timeout, back navigation and resumed game_panel. Preserve profile-final and binding/row/screen logs. These older routes use legacy MnK key:a/Space; failures must distinguish that route from the already working General Mapping Enter path. |
| 5 | `acceptance_recovery.ps1 -BuildDir ../title-link-build -Label migration-ui-r1-recovery -BootTimeoutSec 300 -TitleTimeoutSec 240 -MenuTimeoutSec 300`; retail/D3D/private cache environment only. | Existing no-input notice while labels are off; F10 hold reopens keyboard, leaves mouse gate off, persists recovered profile, confirmation caption and a real menu key. `input_backend=none` aliases hid.drivers; settings_help_overlay is a registered title bool. Both are present in current source. The existing script force-stops after its assertions, so it is not clean-exit evidence. |
| 6 | `probe_master_restart.ps1 -BuildDir ../title-link-build -OutDir $E/master-restart -GameRoot $G -UltimateMode 0 -BootTimeoutSec 300 -ScreenTimeoutSec 120 -NextBootTimeoutSec 420` (no SessionOnly/NextBootOnly on its first invocation). | Existing two-boot route: leaf changes hot with no restart prompt; master changes produce exactly one authored restart confirmation, no hot master apply and no new catalogue fetch; close uses old session value; next boot sees committed master off and no store row/request. It already owns an isolated profile/user root and empty DLC; preserve the shared cache between these two boots. |
| 7 | `acceptance_save.ps1 -BuildDir ../title-link-build -OutDir $E/save -GameRoot $G -UltimateMode 0 -Steps 2 -BootTimeoutSec 300 -ScreenTimeoutSec 90`; then `acceptance_persistence.ps1 -BuildDir ../title-link-build -OutDir $E/persistence -GameRoot $G -UserDataRoot <fresh persistence/user> -UltimateMode 0 -BootTimeoutSec 300 -CloseWaitSec 30` with all five cases. Use separate private launcher/cache/empty DLC environment for each suite. | Save: change/reload/revert calibration -10ms ->0ms, save hash changes, exact reload bytes, restored zero-offset readback and each clean close. Persistence: fresh+restart+missing+corrupt-payload+corrupt-header, expected file/header inventory and actual read/write byte accounting; no fatal and clean close. B13EBABEBABEBABE remains the current SDK XUID; do not accidentally point corruption at normal saves. |
| 8 | `acceptance_dlc.ps1 -BuildDir ../title-link-build -OutDir $E/dlc -GameRoot $G -DlcRoot <verified package root> -ExpectedSong '<known installed DLC-only title>' -UltimateMode 0 -BootTimeoutSec 300 -ScreenTimeoutSec 120 -Extra '--gpu_backend=d3d12;--launcher_profile=<leg/profile/launcher.toml>;--cache_root=<leg/cache>'`; keep its negative control (no SkipControl). | Positive leg: >=1 mounted package, >=1 aggregate item, expected DLC-only song in list, no fatal. Negative leg: nonexistent private DLC roots, zero aggregate items and DLC-only song absent. Both use fresh private user roots. Current game/dlc contains structured45410914/5841122D sources, but this inspection did not establish a song title; choose from retained package metadata/evidence, not a guess. |

Cache cold/warm correctness is additionally exercised by changing the pilot's
`pilot_cache_states` to `["cold","warm"]` with a new output directory after its
cold tooling pilot succeeds, or by the original five-pair campaign. The preparer
copies same-build validated after-cache only; all module/DLL bytes participate in
identity. Check native-cache reuse and recipe/preparation paths, not just that
cache files exist. Do not share a baseline native cache with the upgraded build.

## Inventory exclusions and limitations

- `acceptance_ultimate.ps1` wraps the song route and has useful log assertions,
  but exposes no private user/profile/cache/backend options. Its unmodified
  invocation is not part of this safe matrix. Use the isolated song legs above
  and retain the wrapper's equivalent semantic checks. Framework log spelling
  changed; review any failed string match against the current event before
  treating it as a product failure or weakening its assertion.
- `acceptance_launches.ps1` hardcodes out/m3 evidence, no private roots and old
  fatal detection. Repeated launch-only testing adds little after the stronger
  song/save/restart legs; do not invoke it merely to increase a launch count.
- `acceptance_alpha.ps1` refreshes/copies installer payloads and can delete an
  installed app folder. Installed-package construction and DLL closure were
  validated separately; skip this broad mutating wrapper here.
- Row/button preset sub-harnesses overlap the broad controls test. Add them only
  for a failing capture/controller-art case. Physical controllers, touch, focus,
  display/HDR and overlay typography still need the applicable real-device/visual
  checks; synthetic keys do not prove those routes.
- The two controls suites end by force-stopping their process; other suites have
  normal-close assertions. Do not count a force-stop as lifecycle success.
- Older I/O and filesystem assertions can depend on diagnostic text. Preserve
  failed logs and inspect current public-contract behavior; no assertion was
  silently removed in this read-only inventory.
- Linux/macOS/iOS native UI, UIKit background/foreground, Metal/MoltenVK and audio
  interruption are unavailable on this Windows host and remain explicitly
  unverified. Windows evidence does not establish those paths.
