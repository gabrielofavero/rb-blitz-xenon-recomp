// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The part of D19 that is a platform: the one HTTPS GET behind the update check, the folder
// the installer puts the updater in, and starting it.
//
// Everything here is best-effort and silent. The check is a convenience for a user who
// asked for a settings window, so a machine with no network, a proxy that eats the request
// or a release that has not been published yet all end the same way: nothing is said, and
// the launcher carries on.
//
// The fetch runs on a worker thread so the window appears immediately rather than after a
// network round trip. UpdateChecker owns that thread and joins it on destruction; every
// request is bounded by the WinHTTP timeouts below, so the join is a wait of at most those
// seconds, and only when the check has not already finished - which, on a machine whose
// network answers at all, is in the milliseconds before the first frame.

#pragma once

#include <filesystem>
#include <string>

#include "update_check.h"

namespace rb_blitz::launcher {

// The release manifest this build checks (RBBLITZ_LAUNCHER_UPDATE_URL, read from
// installer/config/pins.toml when the launcher is configured). Empty for a build with no
// release channel, which then checks nothing.
std::string UpdateManifestUrl();

// The version of the launcher that is running (RBBLITZ_LAUNCHER_VERSION, read from the same
// pins). This is what the check compares a release against, and it is deliberately the version
// of the code rather than one read out of a file: after an update the payload has replaced the
// launcher, so the launcher *is* the newer build and the check settles on its own.
std::string RunningVersion();

// The release page: where a user is sent when an update cannot be installed by the updater
// - a launcher installed by hand, a release that published no payload, or a manifest this
// build does not understand. Built from the pins' homepage URL, so it is the project's own
// page and not a URL written down twice.
std::string ReleasesUrl();

// The updater the setup executable placed under the user's local application data
// (installer/setup.iss puts it in {localappdata}\{#UpdateDirName}\update, never in the
// install folder: that one holds the game and its payload). Empty when it cannot be
// resolved, which the caller treats as "no automatic update".
std::filesystem::path UpdaterPath();

// One blocking HTTPS GET, capped at a small body: a manifest is a handful of lines, and
// anything larger is not one. `error` is for the --dump-update report; the launcher's own
// check never shows a user anything it says.
bool FetchReleaseManifest(const std::string& url, std::string* body, std::string* error);

// Runs one check off the frame loop.
class UpdateChecker {
 public:
  UpdateChecker() = default;
  ~UpdateChecker();
  UpdateChecker(const UpdateChecker&) = delete;
  UpdateChecker& operator=(const UpdateChecker&) = delete;

  // Begins the check on a worker thread. An empty `url` - a build with no release channel -
  // finishes at once with nothing to offer. Calling it twice starts nothing the second time.
  void Start(const std::string& url);

  // What the check has found so far. Cheap, never blocks, safe to call every frame.
  UpdateCheck Poll() const;

 private:
  struct Worker;
  Worker* worker_ = nullptr;
};

// Starts the updater and returns at once; the caller leaves, because the update replaces the
// launcher that is running. `error` is empty when the process was started.
bool StartUpdater(const std::filesystem::path& updater, std::string* error);

// Opens the release page in the user's browser. Best-effort: the URL is on screen either
// way, so a failure is not reported.
void OpenReleasesPage();

}  // namespace rb_blitz::launcher
