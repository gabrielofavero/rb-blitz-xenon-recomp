// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <array>
#include "profile/remap.h"

namespace rb_blitz::profile::music {
inline constexpr std::array<std::string_view, 3> kActions{"next", "previous", "pause"};
inline std::optional<size_t> ActionIndex(std::string_view name) {
  for (size_t i = 0; i < kActions.size(); ++i) if (name == kActions[i]) return i;
  return {};
}
inline std::string Label(std::string_view name) {
  if (name == "next") return "Next Track";
  if (name == "previous") return "Previous Track";
  return "Play/Pause";
}
// An independent action table. No entry is interpreted as a guest pad target.
// Unknown rows/values survive the profile's surgical writer unchanged.
class Bindings {
 public:
  Bindings() { Rebuild(); }
  explicit Bindings(std::vector<ProfileSetting> rows) : rows_(std::move(rows)) { Rebuild(); }
  const std::vector<remap::Source>* Effective(std::string_view name) const {
    const auto index = ActionIndex(name);
    return index ? &effective_[*index] : nullptr;
  }
  static bool Accepts(const remap::Source& source) {
    return source.kind == remap::SourceKind::kKey ||
           (source.kind == remap::SourceKind::kPad && remap::ParseTarget(source.name).has_value());
  }
  bool IsBound(std::string_view name) const {
    return std::any_of(rows_.begin(), rows_.end(), [&](const auto& row) { return row.key == name; });
  }
  size_t BoundCount() const { return rows_.size(); }
  void Reset(std::string_view name) { std::erase_if(rows_, [&](const auto& row) { return row.key == name; }); Rebuild(); }
  void ResetAll() { for (auto name : kActions) Reset(name); }
  void Set(std::string_view name, const std::vector<remap::Source>& sources) {
    Reset(name);
    rows_.push_back({std::string(name), remap::FormatSourceList(sources), ValueStyle::kBasic});
    Rebuild();
  }
  const std::vector<ProfileSetting>& ToRows() const { return rows_; }
 private:
  std::vector<ProfileSetting> rows_;
  void Rebuild() {
    effective_ = {{
        {{remap::SourceKind::kPad, "right_shoulder"}, {remap::SourceKind::kKey, "MediaTrackNext"}},
        {{remap::SourceKind::kPad, "left_shoulder"}, {remap::SourceKind::kKey, "MediaTrackPrevious"}},
        {{remap::SourceKind::kPad, "start"}, {remap::SourceKind::kKey, "MediaPlayPause"}}}};
    for (const auto& row : rows_) if (auto index = ActionIndex(row.key)) {
      auto parsed = remap::ParseSourceList(row.value);
      if (parsed && std::all_of(parsed->begin(), parsed->end(), Accepts))
        effective_[*index] = std::move(*parsed);
    }
  }
  std::array<std::vector<remap::Source>, 3> effective_;
};
}  // namespace rb_blitz::profile::music
