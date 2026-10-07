// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The launcher's update check (docs/plans/launcher-plan.md D19): the release manifest, the
// version comparison, and the two decisions the launcher makes from them - is there an
// update, and should the user be asked about it.
//
// This module is dependency-free on purpose (no ImGui, no SDL, no <windows.h>): the network
// fetch and the updater that runs are in update_launcher.h, so every rule here is decided
// from text in a test rather than from a release that happens to exist.
//
// Three properties drive the design, and they are the ones D19 asks for:
//
//   * The check says nothing when it cannot be completed. A missing network, a 404, a
//     manifest this build does not understand - all of them are the same answer (no update
//     to offer), because a launcher that reported its own failures would interrupt a user
//     who asked for a settings window.
//   * The version compared against is the version of the launcher that is running, not one
//     read out of a file: after an update the launcher *is* the newer build, so the check
//     settles on its own without bookkeeping.
//   * Nothing is offered that cannot be installed. A release that pinned no payload (an
//     installer built with its payload embedded) names no URL an updater could fetch, so
//     its manifest is no-update - and the release page is what the user is left with.

#pragma once

#include <string>
#include <string_view>

namespace rb_blitz::launcher {

// --- the release manifest -------------------------------------------------

// installer/out/generated/update.toml, published as the release asset `update.toml` and
// written by the same tool that compiles the installer's pins, so the version in it is the
// version the setup executable is named after (installer/README.md, section "Updating").
//
// Only the four keys the launcher decides with are read. The payload keys next to them are
// the updater's business (installer/src/config.h, UpdateManifest), and this parser ignores
// them rather than pretending to understand them: an unknown key is not an error here.
struct ReleaseManifest {
  int schema_version = 0;
  std::string version;
  bool requires_game_data = false;
  // Where the release's build can be fetched from. Empty for a release whose payload
  // travels inside its setup executable: there is nothing an updater could install, so the
  // launcher does not offer one.
  std::string payload_url;
};

// The manifest schema this build understands, matching
// kUpdateManifestSchemaVersion in installer/src/config.h.
inline constexpr int kReleaseManifestSchemaVersion = 1;

// Parses a manifest. Returns false, with `error`, for anything this build cannot act on -
// including a schema it does not know, which is a newer release talking to an older
// launcher and is not a reason to guess.
bool ParseReleaseManifest(std::string_view text, ReleaseManifest* out, std::string* error);

// --- versions -------------------------------------------------------------

// -1, 0 or 1: which of two dotted numeric versions is newer. A component the string does
// not have counts as 0, so "1.0" and "1.0.0" compare equal - the same rule the installer's
// wizard uses for the install it finds on a machine (installer/setup.iss, CompareVersions).
int CompareVersions(std::string_view left, std::string_view right);

// --- the outcome of one check ---------------------------------------------

// What the launcher knows after looking.
enum class UpdateStatus {
  kDisabled,   // this launcher never looks: no release channel, or --no-update-check
  kChecking,   // the fetch is still running
  kNoUpdate,   // checked, and there is nothing to offer
  kAvailable,  // a newer release exists and an updater can install it
};

// One check: where it has got to, and what it found.
struct UpdateCheck {
  // A check that has not answered yet, one that was never started (no channel, or the switch
  // that turns it off), and one that has finished are three different things: the button has
  // three different things to say, and only one of them is "checking".
  enum class State { kUnchecked, kChecking, kDone };

  State state = State::kUnchecked;
  // Whether a usable manifest was fetched, and what it said. Only meaningful once the
  // state is kDone; a manifest that could not be fetched and one that could not be parsed
  // are the same answer here, which is why there is one flag and not two.
  bool fetched = false;
  ReleaseManifest manifest;
};

struct UpdateReport {
  UpdateStatus status = UpdateStatus::kChecking;
  // The newer release, when the status is kAvailable. Empty otherwise.
  std::string version;
  bool requires_game_data = false;
  // Whether the user should be asked about it now: an update is available and this version
  // was not already declined. A decline is remembered by version, so the question is asked
  // once per release and never twice for the same one.
  bool ask = false;
};

// Decides what the launcher knows and what it should say.
//
//   `running_version` is the version of the launcher doing the asking - the release it was
//   built from (RBBLITZ_LAUNCHER_VERSION), which the payload replaces with each update.
//   `declined_version` is what the profile remembers the user saying no to, or empty.
UpdateReport EvaluateUpdate(const UpdateCheck& check, std::string_view running_version,
                            std::string_view declined_version);

// The line the Update button's own hint (and tooltip) shows: what the button would do, in the
// words of the state it is in - D19 asks the button to stay where it is either way, so what it
// says is the only thing that changes when there is nothing to install.
std::string UpdateButtonText(const UpdateReport& report);

// The sentence the prompt shows above its two buttons: which release is available and what
// accepting it will mean. The game-files question is answered here rather than discovered on
// the updater's own page, because "this one asks for your files again" is the part a user
// deciding wants to know.
std::string UpdatePromptText(const UpdateReport& report);

}  // namespace rb_blitz::launcher
