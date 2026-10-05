// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The game's own settings file, read and never written (docs/plans/launcher-plan.md D3;
// prompt B4).
//
// D3 puts `rb_blitz.toml` at rank 3 and the launcher's profile at rank 4: equal-ranked, the
// game's own file applied second, so the game's file wins. A row the launcher never passes on
// the command line can therefore be decided by a value the launcher itself never sees - which
// is what B4 has to say out loud instead of silently rewriting the user's in-game change.
//
// Reading it is all the launcher does with it. The file belongs to the game and to the F4
// overlay's *Save to config* (D2: "the launcher must not fight it"), and a launcher that wrote
// it would do exactly that - the first F4 save drops anything it added.
//
// Dependency-free (no ImGui, no SDL, no SDK) so the badge's rule is testable from fixtures.

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace rb_blitz::launcher {

// The name the game writes its own cvars under, next to its own executable.
inline constexpr const char* kGameConfigFileName = "rb_blitz.toml";

// What the game's own file says. `present` is false for the usual case of a player who never
// touched the in-game display settings, and `unreadable` is for a file that is there but is
// not something this reader will interpret - the badge then says nothing rather than guessing.
struct GameConfig {
  std::filesystem::path path;
  bool present = false;
  bool unreadable = false;
  std::string error;
  std::map<std::string, std::string> values;  // cvar name -> the value as text
};

// Reads <game_root>/rb_blitz.toml. A missing file is not an error and not a warning: it is
// what an untouched install looks like.
//
// The reader is deliberately no more general than the writer it reads. `SerializeToTOML`
// (rexglue-sdk/src/core/cvar.cpp) writes a flat document - `name = value`, one per line,
// strings quoted, nothing but the cvars that differ from their compiled default - so this
// understands exactly that: `#` comments, `"quoted"` and `'literal'` strings, and the numbers
// and bools as written. A table header puts the reader inside that table, where keys are not
// root cvars and are therefore skipped; an array value is skipped for the same reason. A line
// that is none of those things is reported as unreadable instead of being guessed at: the
// whole point of reading the file is to say what the game will really use.
GameConfig ReadGameConfig(const std::filesystem::path& game_root);

}  // namespace rb_blitz::launcher
