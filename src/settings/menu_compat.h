// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P10 - the merged-menu compatibility rules (parent plan §3.5, §3.6): what the Hide Items →
// Main Menu row hides, what the menu-music pool is, and how far the mod-row rename reaches.
//
// Why the decisions are their own SDK-free module: each one used to be a branch inside a hook
// (`src/hooks/menu_filter.cpp`, `src/hooks/shell_music.cpp`) where only a boot could tell whether
// it was right, and each has a rule the plan states in words that a branch cannot be held to. Here
// they are functions over plain data, so `tests/menu_compat_tests.cpp` can walk the whole matrix -
// master off, no selection, empty selection, a custom selection, the protected entry, the refresh
// row - without a boot, and the hook only applies the answer.
//
// Three rules from §3.5/§3.6 are the reason the module exists at all:
//
//   * **"Off disables filtering while preserving checked choices. On with an empty selected list
//     hides nothing."** An empty selection is a deliberate act, and it must not fall back to the
//     compiled defaults the way an *absent* one does. The two are different inputs here because
//     they are different things: `selection_explicit` separates them.
//   * **"The protected entry must not be hideable."** In the merged product that entry is the
//     retail HELP & OPTIONS row (`splash_options`), the only route into the settings surface, so a
//     selection that names it is refused - and *reported*, because a silent refusal is how a user
//     ends up with no settings screen.
//   * **"Default offline rows must not hide Refresh Songs when `splash_dlc` has that
//     job."** The same row carries the DLC refresh while R7 owns it, so the default trio loses
//     that row and says so; only an explicit user choice may hide it, and then the caller is told
//     the refresh has no row left.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::settings::menu_compat {

// The merged product's one settings entry: the retail HELP & OPTIONS row, routed to the merged
// screen by P5b. It is never hidden - see the header comment.
inline constexpr char kProtectedMenuRow[] = "splash_options";

// The row that carries the DLC library refresh while R7 is on (`TapRefreshCache`).
inline constexpr char kRefreshMenuRow[] = "splash_dlc";

// §3.5: the pool is a fixed 50 songs, capped by what the library can actually supply. The old
// `enhancements_menu_dlc_song_count` row is retired: a legacy value is *ignored* (with a
// diagnostic) rather than re-enabling configurability.
inline constexpr size_t kMenuPoolMax = 50;
inline constexpr int32_t kLegacyPoolCountDefault = 3;

// The rows the compiled default hides: the three entries whose screens only ever worked online.
std::vector<std::string> DefaultHiddenRows();

struct HideRequest {
  bool master = false;                 // the hide master (`hide_main_menu`)
  bool selection_explicit = false;     // the selection is a deliberate set, possibly empty
  std::vector<std::string> selected;   // meaningful when selection_explicit
  std::vector<std::string> defaults;   // the compiled default selection
  std::string protected_row = kProtectedMenuRow;
  // R7: `splash_dlc` performs the library refresh, so hiding it takes the only in-menu refresh
  // away. The *default* selection therefore leaves it alone; an explicit user choice may hide it,
  // and the caller is told the refresh lost its row.
  bool refresh_row_owns_refresh = false;
  // The main menu's refresh entry (owner request, 2026-10-15): `general.refresh_music_library`.
  // With it off the row goes - deliberately, with the master on or off, and without the
  // "needs another row" note an accidental hiding raises.
  bool refresh_row_enabled = true;
};

struct HideDecision {
  std::vector<std::string> hidden;  // exactly what to remove from the option array
  // Rows the selection named that are never hidden. Non-empty is a diagnostic, not a failure.
  std::vector<std::string> refused;
  bool refresh_row_left_visible = false;  // kept because it owns the refresh
  bool alternate_refresh_needed = false;  // the user hid it anyway: the refresh needs another row
  bool refresh_row_disabled = false;      // the refresh entry is switched off, so its row is gone
  std::string note;  // one printable line, for the boot log
};

// The §3.5 rule, decided. Order is the selection's, de-duplicated; an empty result means the menu
// is left exactly as the title ships it.
HideDecision DecideHiddenRows(const HideRequest& request);

// §3.5's pool: `min(50, what the library can supply)`. `usable_songs` is the boot's own ceiling
// (how many songs/packages it is willing to mount), never the old cvar.
size_t PoolCap(size_t usable_songs);

// A one-line note when the retired `enhancements_menu_dlc_song_count` is not at its default, so a
// boot says out loud that the value is ignored rather than silently behaving differently from what
// a hand-edited config asks for. Empty when the value is the compiled default.
std::string LegacyPoolCountNote(int32_t configured);

// §3.6: the mod-row rename is scoped to the mod's own locale key. The merged root's label is not
// in that key's namespace, and this is the guard that keeps the retired transform from ever
// reaching it.
inline constexpr char kRenameScopeKey[] = "mod_settings";

inline bool RenameCoversRow(std::string_view key) { return key == kRenameScopeKey; }

}  // namespace rb_blitz::settings::menu_compat
