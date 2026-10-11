// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 — the native settings schema: which rows exist, in which order, in which category
// and submenu group, where each is persisted, and how a row's choices are filled from a
// capability snapshot. The descriptors themselves are the frozen model (model.h); this
// header adds only the two things the model deliberately does not carry:
//
//   * the storage location (`[general] target`, `[graphics] resolution = [w, h]`, ...), so
//     the store never guesses a key from an id, and
//   * the submenu group token, so the UI can build the tree in the parent plan's §3 order
//     without inventing a second SettingId for each container row.
//
// `choices_source` is frozen as "" | "renderers" | "resolutions" | "provider". P3 adds
// "processor_count" and "menu_rows" for the two rows whose choices are not a capability
// record: the DLC scan-thread ceiling (§3.1) and the Hide Main Menu row list (§3.3, P10's
// seam). Both are resolved in BuildDescriptors; an unknown source leaves the row's static
// choices in place.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "settings/model.h"

namespace rb_blitz::settings::schema {

// One schema row: the frozen descriptor plus the two P3-only facts above.
struct SchemaRow {
  Descriptor descriptor;
  StorageKey storage;   // empty section when the row is not a settings.toml preference
  std::string group;    // submenu container token; "" = a direct child of its category
  std::string label;    // English fallback for label_key (P5 localizes)
  std::string help;     // English fallback for help_key
};

// Every row, in the parent plan §3 presentation order. Static for the process lifetime.
const std::vector<SchemaRow>& Rows();

// The row for an id, or nullptr for kUnknown / an id this build does not know.
const SchemaRow* FindRow(SettingId id);

// P9: the §5.5 application strategy for one id, from the same table BuildDescriptors
// materializes. `kRestart` for an id this build does not know, because an unknown row is never
// applied to the running game.
ApplicationStrategy StrategyOf(SettingId id);

// A fresh descriptor list with capability/provider-dependent choices resolved and the
// availability of provider rows set from `caps.provider`. The returned descriptors are a
// by-value copy, never a reference into the static table.
std::vector<Descriptor> BuildDescriptors(const CapabilitySnapshot& caps);

// Provider rows seen with a menu-row source rather than a capability source: the caller
// (P10 for Hide Main Menu) supplies the row tokens and labels. `rows` replaces every
// "menu_rows" row's choices; pass an empty vector to leave them empty.
void SetMenuRows(std::vector<Choice> rows);
std::vector<Choice> MenuRows();

// Parses a stable token into a Value of the descriptor's kind. A kResolution token is
// "WIDTHxHEIGHT"; an enum/string token is the token itself; a kInt token is decimal. False
// when the token is unknown for that kind, so a stale token is refused instead of guessed.
bool ParseTokenToValue(const Descriptor& descriptor, std::string_view token, Value* out);

// The label a token shows by default, or the token itself when the row has no such choice.
std::string LabelForToken(const Descriptor& descriptor, std::string_view token);

}  // namespace rb_blitz::settings::schema
