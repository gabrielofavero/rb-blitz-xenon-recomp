// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <string_view>

namespace rb_blitz::messages {

struct BuiltInIcon {
  std::string_view id;
  std::string_view material;
};

// Names measured in list_inbox.milo_xbox. The challenge texture is absent from
// the main ark, and unplayed_songs has no row material, so both use the default.
inline constexpr std::array<BuiltInIcon, 13> kBuiltInIcons{{
    {"icon_challenge", ""},
    {"icon_friend_popular", "icon_friend_popular.mat"},
    {"icon_friends_play", "icon_friends_play.mat"},
    {"icon_misc", "icon_misc.mat"},
    {"icon_more_stars", "icon_more_stars.mat"},
    {"icon_new_release", "icon_new_release.mat"},
    {"icon_revisit_song", "icon_revisit_song.mat"},
    {"icon_store_plays", "icon_store_plays.mat"},
    {"icon_tournament", "icon_tournament.mat"},
    {"icons_decrease_rank", "icon_decrease_rank.mat"},
    {"icons_increase_rank", "icon_increase_rank.mat"},
    {"icons_store_rec", "icon_store_rec.mat"},
    {"icons_unplayed_songs", ""},
}};

inline bool IsBuiltInIcon(std::string_view id) {
  for (const auto& icon : kBuiltInIcons) if (icon.id == id) return true;
  return false;
}

inline std::string_view RowMaterial(std::string_view id) {
  for (const auto& icon : kBuiltInIcons)
    if (icon.id == id && !icon.material.empty()) return icon.material;
  return "icon_misc.mat";
}

}  // namespace rb_blitz::messages
