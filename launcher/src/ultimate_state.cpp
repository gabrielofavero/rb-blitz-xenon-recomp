// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of D4/D5's detection (launcher/src/ultimate_state.h, B1).

#include "ultimate_state.h"

#include <string>
#include <system_error>

#include "hooks/ultimate_plan.h"

namespace rb_blitz::launcher {
namespace {

namespace fs = std::filesystem;

// A directory that holds the game: the entry point, or the archive folder the entry point
// needs. Both spellings the tree uses are accepted because the installer's layout is the
// first and a hand-made dump the second.
bool LooksLikeGameRoot(const fs::path& dir) {
  std::error_code ec;
  if (fs::is_regular_file(dir / rb_blitz::ultimate::kEntrypoint, ec) && !ec) {
    return true;
  }
  return fs::is_directory(dir / "gen", ec) && !ec;
}

// Just the header, or the pair: ultimate_plan.h owns both answers so this cannot drift from
// the runtime's check.
bool HasPair(const fs::path& root) {
  return rb_blitz::ultimate::HasPayload(root) && rb_blitz::ultimate::HasPayloadArchive(root);
}

}  // namespace

GameRoots DetectGameRoots(const fs::path& launcher_dir, std::string_view override_root) {
  GameRoots roots;
  std::error_code ec;

  // An override is a statement about where the game is, not a hint to be second-guessed: the
  // tab reads the root the user named and says whether it found a game there.
  if (!override_root.empty()) {
    roots.game_root = fs::path{std::string(override_root)}.lexically_normal();
    roots.game_root_found = LooksLikeGameRoot(roots.game_root);
    roots.ultimate_root = rb_blitz::ultimate::ResolvePayloadRoot("", roots.game_root);
    return roots;
  }

  const fs::path beside_game = launcher_dir / "game";
  if (LooksLikeGameRoot(beside_game)) {
    roots.game_root = beside_game;
    roots.game_root_found = true;
  } else if (LooksLikeGameRoot(launcher_dir)) {
    roots.game_root = launcher_dir;
    roots.game_root_found = true;
  } else {
    // The installer's layout, so the tab still has somewhere concrete to look and to offer
    // as a default, with game_root_found saying it was not actually there.
    roots.game_root = beside_game;
  }
  roots.ultimate_root = rb_blitz::ultimate::ResolvePayloadRoot("", roots.game_root);
  return roots;
}

UltimateState DetectUltimateState(const fs::path& game_root) {
  const fs::path ultimate_root = rb_blitz::ultimate::ResolvePayloadRoot("", game_root);
  if (rb_blitz::ultimate::HasPayload(ultimate_root)) {
    return rb_blitz::ultimate::HasPayloadArchive(ultimate_root) ? UltimateState::kReady
                                                               : UltimateState::kDamaged;
  }
  if (rb_blitz::ultimate::HasPayload(game_root)) {
    return HasPair(game_root) ? UltimateState::kAlsoPresent : UltimateState::kDamaged;
  }
  return UltimateState::kMissing;
}

bool UltimateAvailable(UltimateState state) {
  return state == UltimateState::kReady || state == UltimateState::kAlsoPresent;
}

bool UltimateSelectable(UltimateState /*state*/) {
  // R11 as asked was "disabled with a tooltip to reinstall"; D5 decided against a dead
  // option. If that is ever revisited, this is the one place to change.
  return true;
}

std::string_view UltimateStateText(UltimateState state) {
  switch (state) {
    case UltimateState::kReady:
      return {};
    case UltimateState::kAlsoPresent:
      return "The Ultimate payload is merged into the game folder (no separate ultimate\\ "
             "folder), which the runtime mounts the same way.";    case UltimateState::kMissing:
      return "Ultimate is not installed, so the launcher starts the retail game. The "
             "installer's Rock Band Blitz Ultimate step adds it.";
    case UltimateState::kDamaged:
      return "The payload has gen\\patch_xbox.hdr but no gen\\patch_xbox_0.ark - the game "
             "treats that pair as a damaged disc.";
  }
  return {};
}

LaunchTarget FallbackTarget(LaunchTarget wanted, UltimateState state) {
  if (wanted == LaunchTarget::kUltimate && !UltimateAvailable(state)) {
    return LaunchTarget::kCommon;
  }
  return wanted;
}

}  // namespace rb_blitz::launcher
