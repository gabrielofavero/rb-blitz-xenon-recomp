# The release model: one file per release — done 2026-10-15

**Status: finished.** Decided and built on 2026-10-15, in the same session as the Controller
Settings update-check removal (the two are one decision — see below).

The successor of D19 (the release-manifest update check) in
[`../finished/2026-09-27-launcher-plan.md`](2026-09-27-launcher-plan.md). Where it disagrees with
that plan, this document is later and owns the disagreement.

## 1. The model

**One product, one file per release.** `installer\build.ps1` builds the complete package from the
working tree it runs in: `setup.iss` with the payload inside it, so it installs with no network
access, and it publishes as the release's only asset — no manifest, no payload archive, no
checksum file to keep in step with it.

| Product | File | What it is | What a release publishes |
| --- | --- | --- | --- |
| the complete package | `RockBandBlitzSetup-<version>.exe` | the wizard, the payload and the updater inside it, built from the checkout | this file, alone |

**The version is one value.** `[installer] version` in `installer\config\pins.toml` names the
setup executable and titles the wizard (`AppVerName` becomes `Rock Band Blitz <version>`); the
file name, the window and every install manifest follow it.

**The build records the checkout it was made from.** `[payload] commit` ships as `"HEAD"`, which
`build.ps1` resolves with git before the pins are compiled in (any ref, or an id, resolves the
same way; `-PayloadCommit <40 hex digits>` records another commit deliberately; empty records
nothing).

**Continuous building is tag-only.** `.github/workflows/builds.yml` runs on a tag whose name
matches the pinned version, builds from it and publishes the setup executable as the release's one
file. It runs on a self-hosted runner because the build needs `game/default.xex`, which is
deliberately not in the repository.

## 2. What was built

**Controller Settings lost its update check.** The `Update` control,
the prompt, the manifest fetch on a worker thread, the compiled-in `manifest_url`,
`--dump-update` and `--no-update-check` are gone, and so are `src/update_check.{h,cpp}`,
`src/updater.{h,cpp}` and their test target. `BarOptions()` is Close/Save/Play on both surfaces.
It links no HTTP client at all now. `[update] declined_version` stays as preservation data.
Details and the doc trail: [`../../settings/menu.md`](../../settings/menu.md) §3.3's
dated supersede, and `controller-settings/README.md`, section "Updates: this window has none".

**The build is the checkout.** `installer/build.ps1` builds the setup executable from the working
tree it runs in — the payload snapshot, the helper, the wizard, and the updater the wizard
embeds — and names it after `[installer] version`. `.github/workflows/builds.yml` does the same
on a tag whose name matches the pinned version and publishes the file as the release's only
asset. The workflow runs on a **self-hosted runner** because the build needs `game/default.xex`,
which is deliberately not in the repository; a local build is the path for everything that is not
a release.

## 3. Evidence (dated, with the artefact)

| Claim | Evidence | When |
| --- | --- | --- |
| The one command builds everything from this checkout | `powershell -ExecutionPolicy Bypass -NoProfile -File installer\build.ps1` → green end to end; result `installer/out/dist/RockBandBlitzSetup-0.1.0.exe`, 24,869,631 bytes, sha256 `2cec316b…`, commit recorded `4e065474…`; `out/_swap/installer/build-command.log` | 2026-10-15 |
| The payload snapshot rebuilds cleanly | `make_payload.ps1 -Clean -Commit <HEAD>` → exit 0, `payload snapshot: 9 files, 62.5 MiB`; `out/_swap/installer/make-payload-clean.log` (Windows PowerShell 5.1's `Remove-Item -Recurse` had aborted one rebuild with a spurious "Could not find file"; the script deletes with `[System.IO.Directory]::Delete` now) | 2026-10-15 |
| The helper suite passes | `ctest --preset installer-release` → 1/1 test; `installer/tests/installer_tests.cpp` | 2026-10-15 |
| The built installer installs | silent install → exit 0; `install-manifest.toml` records `payload_commit = "4e065474…"`, 9 payload files / 65,519,000 bytes, `fingerprints_matched = true`; `out/_swap/installer/silent-install-manifest.toml` | 2026-10-15 |
| The setup executable names itself with the pinned version | `scripts/acceptance/acceptance_product_titles.ps1` derives `Setup - Rock Band Blitz <version>` from the file name and reads it from the window when it can open one; the wizard's own question on this machine reads "Rock Band Blitz 0.1.0 is already installed…" (`out/_swap/titles/pinned-window2.png`) | 2026-10-15 |
| Controller Settings has no update check and still passes everything | `ctest --test-dir out\build\rename-tests` → 38/38 (was 39: the update suite is the one that went) | 2026-10-15 |

## 4. Limits and what is next (honest, dated)

* **Nothing is published yet.** The repository has no releases, so the setup executable is built
  by hand (`installer\build.ps1`) until the first release is cut.
* **`builds.yml` has never run.** It needs a self-hosted runner on a machine with the game data;
  the workflow checks for `game\default.xex` and fails with one clear sentence if it is missing.
  Until it runs, the tag-publishes-a-release path is a documented intention, not a fact.
* **The setup executable's caption is not asserted unattended.** It first asks "… is already
  installed. Install it again?" on any machine that has the game (including this one), and an
  answer of No ends the run, so `acceptance_product_titles.ps1` skips it with that reason
  printed. Its identity is asserted by its file name and by the pins it was built from.
* **The updater executable is dormant** and its manifest (`update.toml`) is no longer published:
  a release is one file. It is still built, still placed, and still the only thing in the tree
  that installs a *payload* rather than a package.

## 5. Files

| Area | Files |
| --- | --- |
| the build | `installer/build.ps1` (one product, run from the checkout), `installer/config/pins.toml` (`[installer] version`; `[payload] commit = "HEAD"`), `installer/tools/make_payload.ps1` (the payload snapshot) |
| the wizard | `installer/setup.iss` (`AppVerName`), `installer/tools/embed_config.cpp` (pins -> `pins.iss` and `embedded_config.h`) |
| the pipeline | `.github/workflows/builds.yml` (tag -> release, one file) |
| the harness | `scripts/acceptance/acceptance_product_titles.ps1` |
| the tests | `installer/tests/installer_tests.cpp` |
| the docs | `installer/README.md`, `docs/README.md`, `docs/reference/known-issues.md`, and the Controller Settings removal's own doc trail |
