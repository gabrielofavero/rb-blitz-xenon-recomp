// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P10's menu-compatibility decisions. See src/settings/menu_compat.h for the three rules this file
// implements and why they are here rather than in the hooks.

#include "settings/menu_compat.h"

#include <algorithm>

namespace rb_blitz::settings::menu_compat {

std::vector<std::string> DefaultHiddenRows() {
  // The same three names the compiled default carries, in the same order (the string lives in
  // src/ui/menu_options.h because that module is what writes the edit).
  return {"splash_leaderboard", "splash_achievements", "splash_dlc"};
}

namespace {

std::string Join(const std::vector<std::string>& names) {
  std::string out;
  for (const std::string& name : names) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out;
}

// Appends `name` unless it is already there, so a selection that repeats a row cannot produce a
// two-entry `hidden` list the caller would try to remove twice.
void PushUnique(std::vector<std::string>* out, const std::string& name) {
  if (std::find(out->begin(), out->end(), name) == out->end()) {
    out->push_back(name);
  }
}

}  // namespace

HideDecision DecideHiddenRows(const HideRequest& request) {
  HideDecision decision;
  // The entry toggle is not the hide list: it is the refresh feature's own row, so it applies
  // whether or not the hide master is on, and it is the player's own choice rather than a loss to
  // report. Everything below either adds the row to `hidden` or leaves it to the selection.
  const bool refresh_row_off = request.refresh_row_owns_refresh && !request.refresh_row_enabled;
  if (!request.master) {
    // "Off disables filtering while preserving checked choices": the selection is not consulted at
    // all, and the caller keeps whatever it stored for the next time the master goes on.
    if (refresh_row_off) {
      PushUnique(&decision.hidden, kRefreshMenuRow);
      decision.refresh_row_disabled = true;
      decision.note = "the hide master is off, and the Refresh Songs entry is off too, so "
                      "its row is gone";
      return decision;
    }
    decision.note = "the hide master is off; every row the title ships stays in the menu";
    return decision;
  }

  const std::vector<std::string>& source =
      request.selection_explicit ? request.selected : request.defaults;
  if (source.empty()) {
    // On with an empty selection hides nothing. This is the case the old code got wrong: an empty
    // list used to mean "the defaults", which silently re-hid rows the player had cleared.
    decision.note = "the selection is empty; the master is on and nothing is hidden";
    return decision;
  }

  for (const std::string& name : source) {
    if (name.empty()) {
      continue;
    }
    if (name == request.protected_row) {
      // Refused, and named: the protected entry is the only route into the settings surface.
      PushUnique(&decision.refused, name);
      continue;
    }
    if (request.refresh_row_owns_refresh && name == kRefreshMenuRow) {
      if (refresh_row_off) {
        // Switched off in Help & Options: the row goes, once, and this is not the "refresh lost a
        // row" case - the player asked for exactly this.
        PushUnique(&decision.hidden, name);
        decision.refresh_row_disabled = true;
      } else if (request.selection_explicit) {
        // The player asked for it by name, so it goes - and the refresh has lost its row.
        PushUnique(&decision.hidden, name);
        decision.alternate_refresh_needed = true;
      } else {
        // The compiled default must not take the refresh away from a menu that offers no other
        // way to run it.
        decision.refresh_row_left_visible = true;
      }
      continue;
    }
    PushUnique(&decision.hidden, name);
  }

  // The entry toggle applies whatever the selection says: a custom selection that does not name
  // the row must not leave the switch ineffective.
  if (refresh_row_off) {
    PushUnique(&decision.hidden, kRefreshMenuRow);
    decision.refresh_row_disabled = true;
  }

  std::string note = "hiding " + std::to_string(decision.hidden.size()) + " row(s)";
  if (!decision.hidden.empty()) {
    note += ": " + Join(decision.hidden);
  }
  if (!decision.refused.empty()) {
    note += "; refused to hide the protected entry (" + Join(decision.refused) + ")";
  }
  if (decision.refresh_row_left_visible) {
    note += "; " + std::string(kRefreshMenuRow) +
            " stays because it refreshes the DLC library";
  }
  if (decision.refresh_row_disabled) {
    note += "; the Refresh Songs entry is off, so its row is gone";
  }
  if (decision.alternate_refresh_needed) {
    note += "; the library refresh lost its row, so it needs another one";
  }
  decision.note = std::move(note);
  return decision;
}

size_t PoolCap(size_t usable_songs) { return std::min(kMenuPoolMax, usable_songs); }

std::string LegacyPoolCountNote(int32_t configured) {
  if (configured == kLegacyPoolCountDefault) {
    return {};
  }
  return "the retired menu-pool size (" + std::to_string(configured) +
         ") is ignored; the pool is fixed at " + std::to_string(kMenuPoolMax) + " songs";
}

}  // namespace rb_blitz::settings::menu_compat
