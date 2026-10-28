// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "input/effective_input.h"
#include "profile/input_rows.h"
#include "controller_schemes/schemes.h"

namespace rb_blitz::input::icons {

enum class Family { kController, kKeyboardMouse };
enum class Art { kSchemeSlot, kPackageImage, kBuiltIn, kUnbound, kUnsupported };

// A physical source, its image, and the controller-sheet slot (if one exists).
// The RGBA image is project-authored for host sources and controller controls without a retail
// glyph. A scheme slot is resolved against the selected package by the guest renderer in P9/P10.
struct Icon {
  profile::remap::Source source;
  Art art = Art::kUnsupported;
  std::string slot;
  std::string scheme;
  std::string legend;
  std::string diagnostic;
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};

// Captures one effective-input revision. No icon cache can go stale: edits, reset and preset
// changes publish a new EffectiveInput; a layout or family change builds a new view directly.
struct View {
  std::shared_ptr<const EffectiveInput> input;
  std::string controller_scheme;
  Family active_family = Family::kKeyboardMouse;
  uint64_t revision = 0;
  uint64_t device_revision = 0;
  // Boot owns the registry for the view's lifetime. Null means use the retail sheet.
  const controller_schemes::Registry* registry = nullptr;
};

View Capture(std::string controller_scheme, Family active_family,
             const controller_schemes::Registry* registry = nullptr);
// Uses the atomic last-device/layout snapshot for ordinary action hints.
View CaptureActive(const controller_schemes::Registry* registry = nullptr);
Icon Resolve(const profile::remap::Source& source, std::string_view controller_scheme,
             const controller_schemes::Registry* registry = nullptr);
std::vector<Icon> Ordered(const View& view, const profile::InputRowToken& row);
std::vector<Icon> Ordered(const View& view, profile::gameplay_remap::Action action);
Icon First(const View& view, const profile::InputRowToken& row);
Icon First(const View& view, profile::gameplay_remap::Action action);

}  // namespace rb_blitz::input::icons
