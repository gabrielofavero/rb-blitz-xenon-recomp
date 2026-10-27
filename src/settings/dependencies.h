// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <algorithm>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::settings::dependencies {

struct Feature {
  const char* token;
  const char* label;
  bool enabled_value = true;
};
// A visibility preference whose stored value means hidden has an inverted enabled value.
inline constexpr Feature kFeatures[] = {
    {"general.dlc_menu_songs", "Background Music: Library"},
    {"interface.music_player", "Music Player"},
    {"general.play_current_song", "Play Current Song"},
    {"interface.background_music_navigation", "Menu Music Navigation"},
    {"general.cache_dlc_library", "Cache Song Library"},
    {"general.refresh_music_library", "Refresh Songs"},
    {"general.rhythmverse", "Store Access"},
    {"interface.hidden_main_menu.rbbz_rhythmverse", "RhythmVerse shortcut", false},
    {"interface.hide_hud", "All HUD Elements", false},
    {"interface.goldprogress", "Gold Star Progress"},
    {"interface.progressbar", "Song Progress Bar"},
    {"interface.show_timer", "Timer"},
    {"interface.show_pb", "Personal Best"},
    {"interface.show_steak", "Note Streak"},
    {"interface.input_viewer", "Input Viewer"},
};
struct Link { size_t child, parent; };
inline constexpr Link kLinks[] = {
    {1, 0}, {2, 0}, {3, 0}, {5, 4}, {7, 6},
    {9, 8}, {10, 8}, {11, 8}, {12, 8}, {13, 8}, {14, 8},
};
inline size_t Index(std::string_view token) {
  for (size_t i = 0; i < std::size(kFeatures); ++i)
    if (token == kFeatures[i].token) return i;
  return std::size(kFeatures);
}
inline bool Contains(std::string_view token) { return Index(token) < std::size(kFeatures); }
inline std::string PromptKey(size_t index, bool enabled) {
  return std::to_string(index) + (enabled ? ".on" : ".off");
}
struct Change { std::string token; bool value; };
struct Plan {
  std::vector<Change> changes;
  std::string prompt;
  bool confirmation = false;
};
// Pure planning: asking for a confirmation never stages or saves a preference. Enabling only
// walks toward prerequisites; disabling only walks toward dependents. Nothing restores children.
inline Plan Prepare(std::string_view token, bool value,
                    const std::function<bool(std::string_view)>& read) {
  Plan plan;
  const auto primary = Index(token);
  if (primary == std::size(kFeatures)) return plan;
  std::array<bool, std::size(kFeatures)> before{}, after{};
  for (size_t i = 0; i < before.size(); ++i)
    before[i] = after[i] = read(kFeatures[i].token) == kFeatures[i].enabled_value;
  const bool enabled = value == kFeatures[primary].enabled_value;
  const auto change = [&](const auto& self, size_t index, bool on) -> void {
    after[index] = on;
    for (const auto& link : kLinks) {
      if (on && link.child == index && !after[link.parent]) self(self, link.parent, true);
      if (!on && link.parent == index && after[link.child]) self(self, link.child, false);
    }
  };
  change(change, primary, enabled);
  plan.changes.push_back({std::string(token), value});
  for (size_t i = 0; i < before.size(); ++i) {
    if (i == primary || before[i] == after[i]) continue;
    plan.changes.push_back({kFeatures[i].token, after[i] == kFeatures[i].enabled_value});
  }
  plan.confirmation = plan.changes.size() > 1;
  // Prerequisites precede the enabled feature. This also lets its legacy normalization read
  // the transaction's new parent value rather than the old boot's value.
  if (enabled && plan.confirmation)
    std::rotate(plan.changes.begin(), plan.changes.begin() + 1, plan.changes.end());
  plan.prompt = PromptKey(primary, enabled);
  return plan;
}

struct Prompt { std::string key, text; };
inline std::vector<Prompt> Prompts() {
  std::vector<Prompt> out;
  for (size_t i = 0; i < std::size(kFeatures); ++i) {
    std::string parents, children;
    for (const auto& link : kLinks) {
      if (link.child == i) {
        if (!parents.empty()) parents += ", ";
        parents += kFeatures[link.parent].label;
      }
      if (link.parent == i) {
        if (!children.empty()) children += ", ";
        children += kFeatures[link.child].label;
      }
    }
    if (!parents.empty())
      out.push_back({PromptKey(i, true), std::string(kFeatures[i].label) +
          " is linked to " + parents + ". Turn it on as well?"});
    if (!children.empty()) {
      std::string text = std::string("Turning off ") + kFeatures[i].label +
          " will also turn off " + children + ". Continue? Enable them separately later.";
      if (i == 0)
        text = "Use Default background music? This also disables Music Player, Play Current Song and Menu Music Navigation. Enable them separately later.";
      if (i == 8)
        text = "Hide all HUD elements and disable their individual settings? They will stay disabled when you show the HUD again.";
      out.push_back({PromptKey(i, false), std::move(text)});
    }
  }
  return out;
}
}  // namespace rb_blitz::settings::dependencies
