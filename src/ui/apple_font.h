// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <imgui.h>
#include <span>

namespace rb_blitz::ui {
// Resolve installed faces through CoreText: iOS and macOS store fonts in
// different locations. Called only while the drawer's atlas is being configured.
ImFont* LoadAppleFont(ImFontAtlas* atlas, float size,
                      std::span<const char* const> names,
                      const ImWchar* ranges = nullptr);
}  // namespace rb_blitz::ui
