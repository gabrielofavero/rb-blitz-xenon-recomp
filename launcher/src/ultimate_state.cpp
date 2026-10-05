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

// How far up the tree a `<dir>\game` is looked for after the launcher's own folder. One level
// covers the installer's layout, and three cover a build tree (`out\build\<preset>`); the rest
// is slack for a deeper output directory. The limit is what keeps this from becoming a search
// of the whole drive.
constexpr int kGameSearchDepth = 6;

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

  // The installer's layout: `<launcher dir>\game`. A development build tree is the same layout
  // seen from further down - `out\build\<preset>` has the checkout's own `game` three levels
  // above it - so the search continues up the tree, which is what makes the launcher say
  // "Ultimate is ready" when it is run from the build folder instead of an install.
  const auto find_beside = [](const fs::path& base) {
    const fs::path candidate = base / "game";
    return LooksLikeGameRoot(candidate) ? candidate : fs::path{};
  };

  if (fs::path found = find_beside(launcher_dir); !found.empty()) {
    roots.game_root = std::move(found);
    roots.game_root_found = true;
  } else if (LooksLikeGameRoot(launcher_dir)) {
    roots.game_root = launcher_dir;
    roots.game_root_found = true;
  } else {
    fs::path ancestor = launcher_dir;
    for (int level = 0; level < kGameSearchDepth; ++level) {
      const fs::path parent = ancestor.parent_path();
      if (parent.empty() || parent == ancestor) {
        break;  // the filesystem root
      }
      ancestor = parent;
      if (fs::path found = find_beside(ancestor); !found.empty()) {
        roots.game_root = std::move(found);
        roots.game_root_found = true;
        break;
      }
    }
  }
  if (!roots.game_root_found) {
    // The installer's layout, so the tab still has somewhere concrete to look and to offer as a
    // default, with game_root_found saying it was not actually there.
    roots.game_root = launcher_dir / "game";
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
