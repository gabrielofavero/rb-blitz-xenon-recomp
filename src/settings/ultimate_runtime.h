// SPDX-License-Identifier: GPL-2.0-only
// Typed values and guest commands for the installed Ultimate provider.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "settings/ultimate_adapter.h"
#include "ui/dtb_author.h"

namespace rb_blitz::settings::ultimate_runtime {

inline constexpr std::string_view kValuePrefix = "rbbz-settings/ultimate/value/";
inline constexpr std::string_view kTrialQuery = "rbbz-settings/ultimate/trial-enabled";
inline constexpr std::string_view kSongBeginQuery = "rbbz-ultimate/song-begin";
inline constexpr std::string_view kSongAppliedQuery = "rbbz-ultimate/song-applied";
inline constexpr std::string_view kSongSetPrefix = "rbbz-ultimate/song-set/";
inline constexpr std::string_view kSelectionTrialQuery = "rbbz-ultimate/selection-trial";
inline constexpr std::string_view kSelectionSetQuery = "rbbz-ultimate/selection-set/show_pb";
inline constexpr std::string_view kSelectionAppliedQuery = "rbbz-ultimate/selection-applied";
inline constexpr std::string_view kConstructionSetPrefix = "rbbz-ultimate/construction-set/";

inline constexpr std::array<std::string_view, 1> kScoringTokens = {
    "gameplay.precision_mode"};
inline constexpr std::array<std::string_view, 1> kTrackTokens = {
    "gameplay.disable_random_wiggles"};
inline constexpr std::array<std::string_view, 3> kPlotTokens = {
    "gameplay.disable_plots", "gameplay.forceplot", "gameplay.which_skybox"};

inline bool IsConstructionBinding(const ultimate_adapter::RowBinding& binding) {
  const auto has = [&](std::span<const std::string_view> tokens) {
    return std::find(tokens.begin(), tokens.end(), binding.token) != tokens.end();
  };
  return has(kScoringTokens) || has(kTrackTokens) || has(kPlotTokens);
}

inline std::string ConstructionSetPath(std::string_view token) {
  return std::string(kConstructionSetPrefix) + std::string(token);
}

// These eight provider globals are read by the installed ulti_game_enter record while it builds
// the HUD, camera or gem visuals. Other provider settings have different or untraced consumers.
inline constexpr std::array<std::string_view, 8> kSongEntryTokens = {
    "interface.progressbar", "interface.show_timer", "interface.show_steak",
    "interface.input_viewer", "interface.hide_hud", "interface.letterbox",
    "gameplay.camera_angle", "gameplay.gems"};

inline std::atomic<bool> g_song_entry_route_ready{false};
inline void SetSongEntryRouteReady(bool ready) {
  g_song_entry_route_ready.store(ready, std::memory_order_release);
}
inline bool SongEntryRouteReady() {
  return g_song_entry_route_ready.load(std::memory_order_acquire);
}

inline std::atomic<bool> g_selection_route_ready{false};
inline void SetSelectionRouteReady(bool ready) {
  g_selection_route_ready.store(ready, std::memory_order_release);
}
inline bool SelectionRouteReady() {
  return g_selection_route_ready.load(std::memory_order_acquire);
}

inline bool CanRunSongEntry(const ultimate_adapter::ProviderRuntime& provider,
                            bool route_ready, bool trial_enabled,
                            bool bridge_enabled, bool forward_only) {
  (void)trial_enabled;
  return bridge_enabled && !forward_only && route_ready &&
         provider.Active() && provider.shape.ok;
}

inline bool CanRunTrial(const ultimate_adapter::ProviderRuntime& provider,
                        bool route_ready, bool trial_enabled,
                        bool bridge_enabled, bool forward_only) {
  return trial_enabled && CanRunSongEntry(provider, route_ready, trial_enabled,
                                          bridge_enabled, forward_only);
}

inline bool IsSongEntryBinding(const ultimate_adapter::RowBinding& binding) {
  return std::find(kSongEntryTokens.begin(), kSongEntryTokens.end(), binding.token) !=
         kSongEntryTokens.end();
}

inline std::string SongSetPath(std::string_view token) {
  return std::string(kSongSetPrefix) + std::string(token);
}

// A song-entry completion may acknowledge a queued row only after that row's guarded guest
// assignment ran. A failed value guard must leave the row deferred for a later song or restart.
class SongEntryReceipt {
 public:
  void Queue(const std::map<SettingId, Value>& changed, uint64_t revision,
             bool trial_enabled = true) {
    for (const auto token : kSongEntryTokens) {
      const auto* binding = ultimate_adapter::BindingForToken(token);
      if (!binding || binding->id == SettingId::kLetterbox ||
          (!trial_enabled && binding->id != SettingId::kShowTimer) ||
          changed.find(binding->id) == changed.end()) continue;
      pending_[binding->id] = revision;
      witnessed_.erase(binding->id);
    }
  }

  bool Witness(std::string_view path) {
    if (!path.starts_with(kSongSetPrefix)) return false;
    const auto* binding = ultimate_adapter::BindingForToken(path.substr(kSongSetPrefix.size()));
    if (!binding || !IsSongEntryBinding(*binding)) return false;
    if (pending_.contains(binding->id)) witnessed_.insert(binding->id);
    return true;
  }

  bool Ready(uint64_t revision) const {
    if (pending_.empty()) return false;
    for (const auto& [id, queued_revision] : pending_)
      if (queued_revision > revision || !witnessed_.contains(id)) return false;
    return true;
  }

  void BeginSong() { witnessed_.clear(); }

  void Complete() {
    pending_.clear();
    witnessed_.clear();
  }

 private:
  std::map<SettingId, uint64_t> pending_;
  std::set<SettingId> witnessed_;
};

class SelectionReceipt {
 public:
  void Queue(const std::map<SettingId, Value>& changed, uint64_t revision) {
    if (!changed.contains(SettingId::kShowPb)) return;
    pending_revision_ = revision;
    witnessed_ = false;
  }
  void Begin() { witnessed_ = false; }
  bool Witness(std::string_view path) {
    if (path != kSelectionSetQuery) return false;
    if (pending_revision_) witnessed_ = true;
    return true;
  }
  bool Ready(uint64_t revision) const {
    return pending_revision_ && *pending_revision_ <= revision && witnessed_;
  }
  void Complete() {
    pending_revision_.reset();
    witnessed_ = false;
  }
 private:
  std::optional<uint64_t> pending_revision_;
  bool witnessed_ = false;
};

inline std::string HelpSymbolForApplicationState(std::string_view symbol,
                                                  const Snapshot& snapshot) {
  if (symbol == "settings_hud_pb") {
    for (const FieldState& field : snapshot.fields)
      if (field.id == SettingId::kShowPb && field.application_phase == ApplyPhase::kDeferred &&
          field.application_boundary == ApplyBoundary::kNextSong)
        return "settings_hud_pb_next_selection";
  }
  constexpr std::array<std::pair<std::string_view, SettingId>, 7> rows{{
      {"settings_hud_hide", SettingId::kHideHud},
      {"settings_hud_progress", SettingId::kProgressBar},
      {"settings_hud_timer", SettingId::kShowTimer},
      {"settings_hud_streak", SettingId::kShowSteak},
      {"settings_hud_input", SettingId::kInputViewer},
      {"settings_camera", SettingId::kCameraAngle},
      {"settings_gems", SettingId::kGems}}};
  const auto match = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
    return row.first == symbol;
  });
  if (match == rows.end()) {
    constexpr std::array<std::pair<std::string_view, SettingId>, 9> menu_rows{{
        {"settings_show_messages", SettingId::kShowMessages},
        {"settings_show_recommendations", SettingId::kShowRecommendations},
        {"settings_mm_leaderboard", SettingId::kHiddenMainMenu},
        {"settings_mm_achievements", SettingId::kHiddenMainMenu},
        {"settings_mm_upgrade", SettingId::kHiddenMainMenu},
        {"settings_mm_download_content", SettingId::kHideDownloadContent},
        {"settings_dlc_refresh", SettingId::kRefreshMusicLibrary},
        {"settings_play_current", SettingId::kPlayCurrentSong},
        {"settings_mm_rhythmverse", SettingId::kHiddenMainMenu}}};
    const auto menu = std::find_if(menu_rows.begin(), menu_rows.end(), [&](const auto& row) {
      return row.first == symbol;
    });
    if (menu == menu_rows.end()) return std::string(symbol);
    for (const FieldState& field : snapshot.fields) {
      if (field.id == menu->second && field.application_phase == ApplyPhase::kDeferred &&
          field.application_boundary == ApplyBoundary::kMenuReturn)
        return std::string(symbol) + "_menu_return";
    }
    return std::string(symbol);
  }
  for (const FieldState& field : snapshot.fields) {
    if (field.id == match->second && field.application_phase == ApplyPhase::kDeferred &&
        field.application_boundary == ApplyBoundary::kNextSong)
      return std::string(symbol) + "_next_song";
  }
  return std::string(symbol);
}

struct RuntimeValue {
  enum class Kind : uint8_t { kInt, kFloat, kSymbol } kind = Kind::kInt;
  uint32_t scalar = 0;  // integer value or IEEE-754 float bits
  std::string symbol;
};

inline std::string ValuePath(std::string_view token) {
  return std::string(kValuePrefix) + std::string(token);
}

inline const ultimate_adapter::RowBinding* BindingForValuePath(std::string_view path) {
  if (!path.starts_with(kValuePrefix)) return nullptr;
  return ultimate_adapter::BindingForToken(path.substr(kValuePrefix.size()));
}

// The installed provider reads most settings while constructing a song or its widgets. Assigning
// those globals from a paused song cannot rebuild already-created objects, and Game Speed can
// desynchronize transport and judgment. Gold Star Progress is read by the score callback after
// resume, so it is the sole bounded in-song assignment trial. Keep its strategy restart-scoped
// until a same-process consumer observation confirms the effect.
inline bool CanTrialDuringSong(const ultimate_adapter::RowBinding& binding) {
  return binding.id == SettingId::kGoldProgress;
}

// The provider's own globals use integer booleans and choice positions, a symbol for Gems,
// and a fractional float for Game Speed. Resolve payload-owned choices against this installation,
// never against a fixed token list. The caller must supply the value from a durable commit.
inline bool EncodeRuntimeValue(const ultimate_adapter::ProviderRuntime& provider,
                               const ultimate_adapter::RowBinding& binding, const Value& value,
                               RuntimeValue* out, std::string* reason = nullptr) {
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (!provider.Active() || !provider.shape.ok) return fail("Ultimate provider is not active and compatible");
  if (out == nullptr || ultimate_adapter::BindingForToken(binding.token) != &binding)
    return fail("unknown Ultimate binding");
  RuntimeValue result;
  if (binding.kind == ultimate_adapter::RowKind::kBool) {
    if (value.kind != ValueKind::kBool) return fail("boolean value required");
    result.scalar = value.boolean ? 1u : 0u;
  } else if (binding.id == SettingId::kGameSpeed) {
    if (value.kind != ValueKind::kInt || value.integer < 100 || value.integer > 150 ||
        value.integer % 5 != 0) return fail("unsupported Game Speed percentage");
    result.kind = RuntimeValue::Kind::kFloat;
    const float speed = static_cast<float>(value.integer) / 100.0f;
    std::memcpy(&result.scalar, &speed, sizeof(speed));
  } else if (binding.id == SettingId::kCameraAngle) {
    if (value.kind != ValueKind::kEnum) return fail("Camera Angle token required");
    const int index = value.text == "default" ? 0 : value.text == "high" ? 1
                      : value.text == "low" ? 2 : -1;
    if (index < 0) return fail("unknown Camera Angle token");
    result.scalar = static_cast<uint32_t>(index);
  } else if (binding.position_value) {
    if (value.kind != ValueKind::kEnum) return fail("provider choice token required");
    const auto* choices = provider.ChoicesFor(binding.variable);
    if (choices == nullptr) return fail("provider choice list unavailable");
    const auto found = std::find(choices->begin(), choices->end(), value.text);
    if (found == choices->end()) return fail("token missing from installed provider choice list");
    result.scalar = static_cast<uint32_t>(found - choices->begin());
  } else if (binding.id == SettingId::kGems) {
    if (value.kind != ValueKind::kEnum) return fail("Gems token required");
    const auto* choices = provider.ChoicesFor(binding.variable);
    if (choices == nullptr || std::find(choices->begin(), choices->end(), value.text) == choices->end())
      return fail("token missing from installed provider choice list");
    result.kind = RuntimeValue::Kind::kSymbol;
    result.symbol = value.text;
  } else {
    return fail("Ultimate binding has no runtime codec");
  }
  *out = std::move(result);
  if (reason) reason->clear();
  return true;
}

// Insert this command only at a guest-thread boundary where the setting's consumer may refresh.
// The file_exists guard prevents an absent, inactive, incompatible or invalid provider value from
// assigning a bogus zero. read_file returns the typed scalar selected by EncodeRuntimeValue.
inline ui::dtb::Node GuestAssignmentFor(const ultimate_adapter::RowBinding& binding) {
  using ui::dtb::Node;
  const std::string path = ValuePath(binding.token);
  return Node::Command({Node::Symbol("if"),
                        Node::Command({Node::Symbol("file_exists"), Node::String(path)}),
                        Node::Command({Node::Symbol("set"), Node::Var(binding.variable),
                                       Node::Command({Node::Symbol("read_file"), Node::String(path)})})});
}

inline ui::dtb::Node SongEntryAssignmentFor(const ultimate_adapter::RowBinding& binding) {
  using ui::dtb::Node;
  Node guarded = GuestAssignmentFor(binding);
  Node assignment = std::move(guarded.kids[2]);
  guarded.kids[2] = Node::Command({
      Node::Symbol("do"), std::move(assignment),
      Node::Command({Node::Symbol("file_exists"), Node::String(SongSetPath(binding.token))})});
  return guarded;
}

// This trial is installed in the original scoring, track and plot records, before their own
// expressions read the provider globals. It does not claim those records reload every song: the
// receipt only proves the assignment ran, and the row stays restart-scoped pending a game check.
inline bool PatchConstructionRecord(const std::vector<uint8_t>& source,
                                    std::span<const std::string_view> tokens,
                                    std::vector<uint8_t>* patched,
                                    std::string* reason = nullptr) {
  using ui::dtb::Node;
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (!patched || tokens.empty()) return fail("no construction output or bindings");
  std::vector<Node> roots;
  ui::dtb::RootHeader header;
  std::string parse_error;
  if (!ui::dtb::Parse(source.data(), source.size(), &roots, &header, &parse_error) || roots.empty())
    return fail("cannot parse construction record");
  const auto reads_var = [](const auto& self, const Node& node, std::string_view variable) -> bool {
    if (node.tag == ui::dtb::kVar && node.text == variable) return true;
    return std::any_of(node.kids.begin(), node.kids.end(), [&](const Node& child) {
      return self(self, child, variable);
    });
  };
  std::vector<Node> assignments{Node::Symbol("do")};
  for (const auto token : tokens) {
    const auto* binding = ultimate_adapter::BindingForToken(token);
    if (!binding || !IsConstructionBinding(*binding))
      return fail("unknown construction binding");
    if (!std::any_of(roots.begin(), roots.end(), [&](const Node& root) {
          return reads_var(reads_var, root, binding->variable);
        })) return fail("construction consumer is absent from installed record");
    Node guarded = GuestAssignmentFor(*binding);
    Node assignment = std::move(guarded.kids[2]);
    guarded.kids[2] = Node::Command({
        Node::Symbol("do"), std::move(assignment),
        Node::Command({Node::Symbol("file_exists"),
                       Node::String(ConstructionSetPath(token))})});
    assignments.push_back(std::move(guarded));
  }
  roots.insert(roots.begin(), Node::Command({
      Node::Symbol("if"),
      Node::Command({Node::Symbol("file_exists"), Node::String(kTrialQuery)}),
      Node::Command(std::move(assignments))}));
  *patched = ui::dtb::Compile(roots, header);
  if (reason) reason->clear();
  return true;
}

inline bool PatchSongSelectionRecord(const std::vector<uint8_t>& source,
                                     std::vector<uint8_t>* patched,
                                     std::string* reason = nullptr) {
  using ui::dtb::Node;
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (!patched) return fail("no output for song-selection record");
  const auto* binding = ultimate_adapter::BindingForToken("interface.show_pb");
  if (!binding || binding->variable != "show_pb") return fail("Personal Best binding changed");
  std::vector<Node> roots;
  ui::dtb::RootHeader header;
  std::string parse_error;
  if (!ui::dtb::Parse(source.data(), source.size(), &roots, &header, &parse_error))
    return fail("cannot parse song-selection record");
  Node* panel = nullptr;
  for (Node& root : roots)
    if (root.tag == ui::dtb::kCommand && root.kids.size() >= 3 &&
        root.kids[0].text == "new" && root.kids[1].text == "SongSelectPanel" &&
        root.kids[2].text == "song_select_panel") panel = &root;
  if (!panel) return fail("song-selection panel changed");
  Node* enter = nullptr;
  const Node* update = nullptr;
  const Node* select = nullptr;
  for (Node& child : panel->kids) {
    if (child.tag != ui::dtb::kArray || child.kids.empty()) continue;
    if (child.kids[0].text == "enter") enter = &child;
    if (child.kids[0].text == "update_current_song") update = &child;
    if (child.kids[0].text == "SELECT_MSG") select = &child;
  }
  const auto reads_pb = [](const auto& self, const Node& node) -> bool {
    if (node.tag == ui::dtb::kVar && node.text == "show_pb") return true;
    return std::any_of(node.kids.begin(), node.kids.end(), [&](const Node& child) {
      return self(self, child);
    });
  };
  if (!enter || !update || !select || !reads_pb(reads_pb, *update) ||
      !reads_pb(reads_pb, *select))
    return fail("Personal Best selection consumers changed");
  Node guarded = GuestAssignmentFor(*binding);
  Node assignment = std::move(guarded.kids[2]);
  guarded.kids[2] = Node::Command({Node::Symbol("do"), std::move(assignment),
      Node::Command({Node::Symbol("file_exists"), Node::String(kSelectionSetQuery)})});
  enter->kids.insert(enter->kids.begin() + 1, Node::Command({
      Node::Symbol("if"),
      Node::Command({Node::Symbol("file_exists"), Node::String(kSelectionTrialQuery)}),
      Node::Command({Node::Symbol("do"), std::move(guarded),
          Node::Command({Node::Symbol("file_exists"), Node::String(kSelectionAppliedQuery)})})}));
  *patched = ui::dtb::Compile(roots, header);
  if (reason) reason->clear();
  return true;
}

// The generated archive substitutes this provider-owned script only when its installed shape
// matches the read sites above. Assign before any original root executes, then signal completion
// after all original roots have returned. The signal is a construction boundary acknowledgment,
// not a rendered-frame or visual-effect measurement.
inline bool PatchSongEntryRecord(const std::vector<uint8_t>& source,
                                 std::vector<uint8_t>* patched, std::string* reason = nullptr) {
  using ui::dtb::Node;
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (patched == nullptr) return fail("no output for song-entry record");
  std::vector<Node> roots;
  ui::dtb::RootHeader header;
  std::string parse_error;
  if (!ui::dtb::Parse(source.data(), source.size(), &roots, &header, &parse_error))
    return fail("cannot parse installed song-entry record");
  if (roots.size() != 3 || roots[1].tag != ui::dtb::kCommand ||
      roots[1].kids.empty() || roots[1].kids[0].text != "with")
    return fail("installed song-entry command layout changed");
  const auto contains_var = [](const auto& self, const Node& node, std::string_view variable) -> bool {
    if (node.tag == ui::dtb::kVar && node.text == variable) return true;
    return std::any_of(node.kids.begin(), node.kids.end(), [&](const Node& child) {
      return self(self, child, variable);
    });
  };
  const auto has_timer_consumer = [](const auto& self, const Node& node) -> bool {
    if (node.tag == ui::dtb::kCommand && node.kids.size() == 3 &&
        node.kids[0].text == "timer.lbl" && node.kids[1].text == "set_showing" &&
        node.kids[2].tag == ui::dtb::kVar && node.kids[2].text == "show_timer")
      return true;
    return std::any_of(node.kids.begin(), node.kids.end(), [&](const Node& child) {
      return self(self, child);
    });
  };
  if (!std::any_of(roots.begin(), roots.end(), [&](const Node& root) {
        return has_timer_consumer(has_timer_consumer, root);
      })) return fail("timer visibility consumer is absent from installed record");
  std::vector<Node> assignments{Node::Symbol("do")};
  for (const auto token : kSongEntryTokens) {
    const auto* binding = ultimate_adapter::BindingForToken(token);
    if (binding == nullptr) return fail("missing song-entry binding");
    if (!std::any_of(roots.begin(), roots.end(), [&](const Node& root) {
          return contains_var(contains_var, root, binding->variable);
        })) return fail("song-entry consumer is absent from installed record");
    if (binding->id != SettingId::kShowTimer)
      assignments.push_back(SongEntryAssignmentFor(*binding));
  }
  const auto* timer = ultimate_adapter::BindingForToken("interface.show_timer");
  if (!timer) return fail("missing Timer binding");
  roots.insert(roots.begin(), Node::Command({
      Node::Symbol("if"),
      Node::Command({Node::Symbol("file_exists"), Node::String(kTrialQuery)}),
      Node::Command(std::move(assignments))}));
  roots.insert(roots.begin(), Node::Command({
      Node::Symbol("if"),
      Node::Command({Node::Symbol("file_exists"), Node::String(kSongBeginQuery)}),
      SongEntryAssignmentFor(*timer)}));
  roots.push_back(Node::Command({
      Node::Symbol("file_exists"), Node::String(kSongAppliedQuery)}));
  *patched = ui::dtb::Compile(roots, header);
  if (reason) reason->clear();
  return true;
}

}  // namespace rb_blitz::settings::ultimate_runtime
