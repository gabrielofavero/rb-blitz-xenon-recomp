// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
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

inline DisplayMode ProfileDisplayBasis(DisplayMode native, bool force_16_by_9) {
  if (native.width <= 0 || native.height <= 0) native = {1280, 720};
  if (!force_16_by_9) return native;
  const int units = std::max(1, std::min(native.width / 16, native.height / 9));
  return {units * 16, units * 9};
}

// 1206x2622 phone capture, reduced to 201:437. Keep its landscape source
// inside the display; the portrait window is the reciprocal full-height crop.
inline DisplayMode MobilePreviewDisplayBasis(DisplayMode native) {
  if (native.width <= 0 || native.height <= 0) native = {1280, 720};
  const double height = std::min(double(native.height), double(native.width) * 201 / 437);
  return {std::max(1, int32_t(std::lround(height * 437 / 201))),
          std::max(1, int32_t(std::floor(height)))};
}

// Native remains a stable intent across monitor changes. Every lower offer
// preserves the active display basis, with a 720-pixel height floor.
inline std::vector<Choice> ProfileResolutionChoices(DisplayMode native) {
  native = ProfileDisplayBasis(native, false);
  std::vector<Choice> choices{{"native", ResolutionToken(native)}};
  for (const int height : {4320, 2880, 2160, 1800, 1440, 1080, 900, 720}) {
    if (height < native.height) {
      const auto width = static_cast<int32_t>(
          (int64_t(native.width) * height + native.height / 2) / native.height);
      if (width > 0 && width <= native.width) {
        const auto token = ResolutionToken({width, height});
        choices.push_back({token, token});
      }
    }
  }
  return choices;
}

// Retain a saved quality tier when the aspect policy or monitor changes. Width
// is derived from the active basis; stored requests remain available on disk.
inline std::string ReconcileProfileResolutionToken(std::string_view token,
                                                   const std::vector<Choice>& choices) {
  if (token == "native" || choices.empty()) return "native";
  for (const auto& choice : choices) if (choice.token == token) return std::string(token);
  const auto requested = ProfileResolution(token, {});
  if (requested.height <= 0) return "native";
  for (const auto& choice : choices) {
    const auto size = ProfileResolution(choice.label, {});
    if (size.height > 0 && size.height <= requested.height) return choice.token;
  }
  return choices.back().token;
}

inline DisplayMode ScaledProfileResolution(DisplayMode native, int tenths) {
  tenths = std::clamp(tenths, 5, 10);
  return {std::max(1, (native.width * tenths + 5) / 10),
          std::max(1, (native.height * tenths + 5) / 10)};
}

// A portrait device displays a full-height crop of the landscape source. Its
// source height must match the device's long edge to retain native pixel density.
inline DisplayMode DeviceProfileSource(DisplayMode native, bool portrait) {
  if (native.width <= 0 || native.height <= 0) return {};
  if (native.width < native.height) std::swap(native.width, native.height);
  if (!portrait) return native;
  return {int32_t((int64_t(native.width) * native.width + native.height / 2) / native.height),
          native.width};
}

inline int ResolutionProfileDrawScale(DisplayMode native, bool device,
                                      bool interface_portrait, bool gameplay_portrait) {
  if (native.width <= 0 || native.height <= 0) native = {1280, 720};
  if (native.width < native.height) std::swap(native.width, native.height);
  const auto largest = DeviceProfileSource(native, device && (interface_portrait || gameplay_portrait));
  return std::max((largest.width + 1919) / 1920, (largest.height + 719) / 720);
}

// MoltenVK cannot sparsely back the scaled resolve buffer. Keep iOS at 1x
// (2x alone adds 2 GiB), fitting both orientations before applying quality tiers.
inline DisplayMode IOSProfilePixels(DisplayMode native, bool portrait, int tenths) {
  auto source = DeviceProfileSource(native, portrait);
  if (source.width <= 0 || source.height <= 0) source = {1280, 720};
  const double fit = std::min({1.0, 1920.0 / source.width, 720.0 / source.height});
  source = {std::max(1, int32_t(std::lround(source.width * fit))),
            std::max(1, int32_t(std::lround(source.height * fit)))};
  return ScaledProfileResolution(source, tenths);
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
  desktop.text = ProfileResolutionChoices(native).back().token;
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
