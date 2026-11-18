// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "settings/model.h"
#include "settings/capabilities.h"

namespace rb_blitz::settings {

inline std::string ResolutionToken(DisplayMode size) {
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

inline DisplayMode ProfileResolution(std::string_view token, DisplayMode native) {
  if (token == "native") return native;
  const auto cross = token.find('x');
  if (cross == std::string_view::npos) return {};
  DisplayMode size;
  const auto w = std::from_chars(token.data(), token.data() + cross, size.width);
  const auto h = std::from_chars(token.data() + cross + 1, token.data() + token.size(), size.height);
  if (w.ec != std::errc{} || w.ptr != token.data() + cross || h.ec != std::errc{} ||
      h.ptr != token.data() + token.size() || size.width <= 0 || size.height <= 0) return {};
  return size;
}

// Native remains a stable intent across monitor changes. Lower offers preserve
// its aspect or use 16:9, fit the current screen, and stop at 720 pixels high.
inline std::vector<Choice> ProfileResolutionChoices(DisplayMode native) {
  if (native.width <= 0 || native.height <= 0) native = {1280, 720};
  std::vector<Choice> choices{{"native", ResolutionToken(native)}};
  std::vector<DisplayMode> sizes;
  const int units = std::min(native.width / 16, native.height / 9);
  if (units >= 80) sizes.push_back({units * 16, units * 9});
  for (const auto size : {DisplayMode{7680,4320}, {5120,2880}, {3840,2160},
                          {3200,1800}, {2560,1440}, {1920,1080}, {1600,900}, {1280,720}}) {
    if (size.width <= native.width && size.height <= native.height) sizes.push_back(size);
    if (size.height < native.height) {
      const auto width = static_cast<int32_t>(
          (int64_t(native.width) * size.height + native.height / 2) / native.height);
      if (width > 0 && width <= native.width) sizes.push_back({width, size.height});
    }
  }
  std::sort(sizes.begin(), sizes.end(), [](auto a, auto b) {
    return a.height != b.height ? a.height > b.height : a.width > b.width;
  });
  for (const auto size : sizes) {
    const auto token = ResolutionToken(size);
    if ((size.width == native.width && size.height == native.height) ||
        std::any_of(choices.begin(), choices.end(), [&](const auto& c) { return c.token == token; })) continue;
    choices.push_back({token, token});
  }
  return choices;
}

inline DisplayMode ScaledProfileResolution(DisplayMode native, int tenths) {
  tenths = std::clamp(tenths, 5, 10);
  return {std::max(1, (native.width * tenths + 5) / 10),
          std::max(1, (native.height * tenths + 5) / 10)};
}

inline bool IsResolutionProfile(SettingId id) {
  return id == SettingId::kInterfaceResolution || id == SettingId::kGameplayResolution ||
         id == SettingId::kInterfaceRenderScale || id == SettingId::kGameplayRenderScale;
}

// Recovery changes the effective profiles, preserving editable saved requests.
// Both contexts start conservatively so their first draw cannot undo recovery.
inline void AddSafeResolutionProfiles(std::map<SettingId, Value>& values, DisplayMode native) {
  Value desktop;
  desktop.kind = ValueKind::kEnum;
  desktop.text = native.width >= 1280 && native.height >= 720 &&
                 (native.width != 1280 || native.height != 720) ? "1280x720" : "native";
  values[SettingId::kInterfaceResolution] = desktop;
  values[SettingId::kGameplayResolution] = desktop;
  Value mobile;
  mobile.kind = ValueKind::kInt;
  mobile.integer = 5;
  values[SettingId::kInterfaceRenderScale] = mobile;
  values[SettingId::kGameplayRenderScale] = mobile;
}

// The backend multiplier stays fixed for the life of its texture/render caches.
// Change guest allocations underneath it. Tiling boundaries are aligned separately
// from the visible width, so a native ultrawide size keeps its aspect.
inline GuestScreenPlan ProfileGuestFrame(DisplayMode pixels, int scale) {
  if (scale < 1 || scale > 7 || pixels.width <= 0 || pixels.height <= 0) return {};
  if (pixels.width < pixels.height) std::swap(pixels.width, pixels.height);
  const int width = std::max(1, (pixels.width + scale / 2) / scale);
  const int height = std::max(1, (pixels.height + scale / 2) / scale);
  if (width > 1920 || height > 720) return {};
  return {true, {width, height}, scale};
}

inline int ProfileTileExtent(int extent) { return ((extent + 63) / 64) * 32; }
}  // namespace rb_blitz::settings
