# The release model: one file per release, and the bootstraps — done 2026-10-15

**Status: finished.** Decided and built on 2026-10-15, at the owner's direction, in the same
session as the Controller Settings update-check removal (the two are one decision — see below).

The successor of D19 (the release-manifest update check) in
[`../finished/2026-09-27-launcher-plan.md`](2026-09-27-launcher-plan.md). Where it disagrees with
that plan, this document is later and owns the disagreement.

## 1. What the owner asked for, and what it turned into

Verbatim, in the order it arrived:

> I want to only ship with a single exe. That should be the only file on a release.

> latest is dynamic, no build required, no config file required. Simply catches the latest commit
> from main as base and that's it.

> when its tagged by a version (0.1.0), then we pick by the last commit on that release (or a
> payload etc), but every information the installer needs should be on the exe itself … locking a
> specific release into the installer.

> controller settings shouldn't check for updates. Its a separated setting, not a launcher. For
> now, leave the updater exe as is without wiring to the controller settings and remove that
> function from it.

> any release, latest or pinned, needs only the exe. Any config files, if needed, are to be stored
> on user files if comparisons are needed etc.

> hmm maybe we can call nightly or experimental instead of latest. Latest indeed infers that its
> the latest release. maybe we can have: latest: latest release / nightly|experimental: latest
> commit on main / pinned.

> maybe we could rename the installers. (If nightly or latest, call it so instead of 0.1.0)

That settled on **three products**, each one file, and two channels that are told apart by
GitHub's own `prerelease` flag rather than by a naming convention:

| Product | File | What it is | What a release publishes |
| --- | --- | --- | --- |
| pinned | `RockBandBlitzSetup-<version>.exe` | the complete package, payload inside, built from a tagged commit | this file, alone |
| rolling | `RockBandBlitzSetup-nightly-<commit>.exe` | the complete package for a commit on main — what continuous building publishes per push | this file, alone |
| latest bootstrap | `RockBandBlitzSetup-latest.exe` | carries nothing; asks for the newest **release** and runs its one file | nothing: it never changes, so it is a workflow artifact |
| nightly bootstrap | `RockBandBlitzSetup-nightly.exe` | the same, asking for the newest **build of main** (the newest prerelease) | same |

## 2. What was built

**Controller Settings lost its update check** (the owner's third paragraph). The `Update` control,
the prompt, the manifest fetch on a worker thread, the compiled-in `manifest_url`,
`--dump-update` and `--no-update-check` are gone, and so are `src/update_check.{h,cpp}`,
`src/updater.{h,cpp}` and their test target. `BarOptions()` is Close/Save/Play on both surfaces.
It links no HTTP client at all now. `[update] declined_version` stays as preservation data.
Details and the doc trail: [`../../settings/menu.md`](../../settings/menu.md) §3.3's
dated supersede, and `controller-settings/README.md`, section "Updates: this window has none".

**The helper gained one channel-aware command.** `resolve-build --channel latest|nightly`
(`installer/src/commands.cpp`) reads the release resource (`src/release_api.{h,cpp}`, a
field-by-field reader with no JSON library), downloads the release's one file, and checks the
size and the digest GitHub published for it. Nightly also asks `.../commits/main` and reports
which commit the build is and whether main has moved on. A bootstrap never installs a bootstrap:
an asset named `RockBandBlitzSetup-latest.exe` or `-nightly.exe` is refused by name.

**`installer/bootstrap.iss`** is the two bootstraps: no pages, no install folder, no uninstaller,
`PrivilegesRequired=lowest` (a file download does not need administrator), a progress page fed by
the helper's progress file, and `%APPDATA%\rb_blitz\bootstrap\<channel>.toml` recording what was
resolved and installed. It runs the downloaded setup with the user's own command line forwarded,
and exits with that setup's exit code.

**`installer/build.ps1`** builds all three: `-Latest`, `-Nightly`, `-PayloadCommit <id>` (pinned),
or nothing (a rolling package from the checkout). A rolling package is named and *titled* after
its commit: `AppVerName` is generated from the payload version, so a build of main says
"Rock Band Blitz nightly-96dce40" wherever a title is shown.

**`.github/workflows/builds.yml`** is what makes the channels real, and it is the piece that
cannot run here: a push to main publishes a **prerelease** tagged `nightly-<run>-<commit>` whose
one asset is the complete package for that commit; a tag publishes a release with the complete
package; the two bootstraps are built on demand and uploaded as artifacts. It runs on a
**self-hosted runner** because the build needs `game/default.xex`, which is deliberately not in
the repository.

## 3. Evidence (dated, with the artefact)

| Claim | Evidence | When |
| --- | --- | --- |
| The nightly bootstrap fetches, verifies, runs and records a build — and says when it is behind main | `scripts/acceptance_bootstrap.ps1 -Channel nightly` → **20/20 checks**, `out/_swap/bootstrap/nightly/` (legs: happy path, digest mismatch, wrong channel, bootstrap asset, behind main) | 2026-10-15 |
| The latest bootstrap does the same for a release, and refuses a build of main | `scripts/acceptance_bootstrap.ps1 -Channel latest` → **17/17 checks**, `out/_swap/bootstrap/latest/` | 2026-10-15 |
| Both channels fail honestly when nothing is published | helper run against the real API: `out/_swap/bootstrap/real-api/{latest,nightly}.txt` — "this repository has no release yet (… answered 404) …" and "this repository has published nothing yet …" | 2026-10-15 |
| The products name themselves as the build they are | `scripts/acceptance_product_titles.ps1`: `RockBandBlitzSetup-latest.exe` → "Setup - Rock Band Blitz latest", `RockBandBlitzSetup-nightly.exe` → "Setup - Rock Band Blitz nightly"; the two complete packages are skipped there (see the limit below) | 2026-10-15 |
| A pinned package and a rolling package are named as such | builds printed `RockBandBlitzSetup-0.1.0.exe` and `RockBandBlitzSetup-nightly-fb70896.exe`; the wizard's own question on this machine reads "Rock Band Blitz 0.1.0 is already installed…" (`out/_swap/titles/pinned-window2.png`) | 2026-10-15 |
| The helper's reader and channel rules are pinned by tests | `ctest --preset installer-release` → 1/1 suite; the cases are in `installer/tests/installer_tests.cpp` (`TestReleaseApi`: the nightly list, the commit marker, the loopback rule, the channel URLs) | 2026-10-15 |
| Controller Settings has no update check and still passes everything | `ctest --test-dir out\build\rename-tests` → 38/38 (was 39: the update suite is the one that went) | 2026-10-15 |

Two bugs were found by the local mock and are worth remembering: the commit question was being
asked of GitHub while the release came from the mock (a comparison between two servers), and
`{%TEMP%}` expanded to nothing in a silent run, so the helper wrote a *relative* download path
and the bootstrap failed with "could not be started (no reason given by Windows)". Both are
fixed, and the bootstrap now logs the folder it resolved and refuses a relative path with a
sentence that names it.

## 4. Limits and what is next (honest, dated)

* **Nothing is published yet.** The repository has no releases, so `latest` has nothing to install
  until the first release is cut, and `nightly` until the first push to main is built by
  `builds.yml`. Both say exactly that, in those words. The acceptance runs above use a mock
  release served from this machine (`/LATEST_API=`, the helper's `--api`), which is why they can
  pass today.
* **`builds.yml` has never run.** It needs a self-hosted runner on a machine with the game data;
  the workflow checks for `game\default.xex` and fails with one clear sentence if it is missing.
  Until it runs, the "one prerelease per commit" contract is a documented intention.
* **The complete packages' captions are not asserted unattended.** Both first ask "… is already
  installed. Install it again?" on any machine that has the game (including this one), and an
  answer of No ends the run, so `acceptance_product_titles.ps1` skips them with that reason
  printed. Their identity is asserted by their file names and by the pins they were built from.
* **The updater executable is dormant** and its manifest (`update.toml`) is no longer published:
  a release is one file. It is still built, still placed, and still the only thing in the tree
  that installs a *payload* rather than a package.
* **The bootstraps are not release assets.** They are workflow artifacts (and a link the project
  owns); publishing them would put a second product on a release page.

## 5. Files

| Area | Files |
| --- | --- |
| the helper | `installer/src/release_api.{h,cpp}` (new), `src/commands.cpp` (`resolve-build`, `version` grew `nightly_api`), `src/download.{h,cpp}` (`Accept`, status code) |
| the bootstraps | `installer/bootstrap.iss` (new), `installer/build.ps1` (`-Latest`/`-Nightly`, rolling naming, `AppVerName`), `installer/tools/embed_config.cpp` (`AppVerName`) |
| the wizard | `installer/setup.iss` (`AppVerName`) |
| the pins | `installer/config/pins.toml` (`[payload] commit` decides pinned vs rolling; `[update]` marked dormant) |
| the pipeline | `.github/workflows/builds.yml` (new) |
| the harnesses | `scripts/acceptance_bootstrap.ps1` (new), `scripts/acceptance_product_titles.ps1` (new) |
| the tests | `installer/tests/installer_tests.cpp` |
| the docs | `installer/README.md` ("The three products", "Cutting a release", "Updating"), `docs/README.md`, `docs/reference/known-issues.md`, and the Controller Settings removal's own doc trail |
