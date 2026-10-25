// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "settings/ultimate_adapter.h"

namespace rb_blitz::graphics {
// Shader assets are root ARK entries, commonly spelled "./xbox_shaders".
// Keep the stored path intact for every other archive consumer.
inline const settings::ultimate_adapter::ArkEntry* FindPreparationAsset(
    const settings::ultimate_adapter::ArkIndex& index, std::string_view requested) {
  if (requested != "xbox_shaders" && requested != "xbox_preinit_shaders") return nullptr;
  for (const auto& entry : index.entries) {
    auto path = std::string_view(entry.path);
    while (path.starts_with("./") || path.starts_with(".\\")) path.remove_prefix(2);
    if (path == requested) return &entry;
  }
  return nullptr;
}
}
