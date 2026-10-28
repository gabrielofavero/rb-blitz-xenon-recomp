// SPDX-License-Identifier: GPL-2.0-only
#include "input/input_icons.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace rb_blitz::input::icons {
namespace {
using profile::remap::Source;
using profile::remap::SourceKind;

// Each five-bit row was drawn for this project. Rendering it into a rounded keycap makes a
// self-contained, redistributable image without relying on the player's controller package.
constexpr std::array<std::pair<char, std::array<uint8_t, 7>>, 38> kFont{{
    {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
    {'C', {15,16,16,16,16,16,15}}, {'D', {30,17,17,17,17,17,30}},
    {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
    {'G', {15,16,16,23,17,17,15}}, {'H', {17,17,17,31,17,17,17}},
    {'I', {31,4,4,4,4,4,31}}, {'J', {7,2,2,2,18,18,12}},
    {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
    {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,16,14,1,1,30}}, {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
    {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
    {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
    {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,30,1,1,17,14}},
    {'6', {6,8,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,2,12}},
    {'-', {0,0,0,31,0,0,0}}, {'?', {14,17,1,2,4,0,4}},
}};

void Pixel(Icon* icon, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || y < 0 || x >= static_cast<int>(icon->width) ||
      y >= static_cast<int>(icon->height)) return;
  const size_t p = (static_cast<size_t>(y) * icon->width + x) * 4;
  icon->rgba[p] = r; icon->rgba[p + 1] = g; icon->rgba[p + 2] = b; icon->rgba[p + 3] = 255;
}

void DrawKeycap(Icon* icon) {
  std::string legend = icon->legend;
  std::transform(legend.begin(), legend.end(), legend.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  icon->width = static_cast<uint32_t>(std::max<size_t>(48, legend.size() * 6 + 14));
  icon->height = 25;
  icon->rgba.assign(static_cast<size_t>(icon->width) * icon->height * 4, 0);
  for (int y = 0; y < 25; ++y) for (int x = 0; x < static_cast<int>(icon->width); ++x) {
    if ((x < 3 || x >= static_cast<int>(icon->width) - 3) && (y < 3 || y >= 22)) continue;
    const bool edge = x < 2 || x >= static_cast<int>(icon->width) - 2 || y < 2 || y >= 23;
    Pixel(icon, x, y, edge ? 225 : 40, edge ? 230 : 45, edge ? 239 : 56);
  }
  const int left = (static_cast<int>(icon->width) - static_cast<int>(legend.size()) * 6 + 1) / 2;
  for (size_t i = 0; i < legend.size(); ++i) {
    if (legend[i] == ' ') continue;
    const auto it = std::find_if(kFont.begin(), kFont.end(), [&](const auto& glyph) {
      return glyph.first == legend[i];
    });
    const auto& rows = (it == kFont.end() ? kFont.back() : *it).second;
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 5; ++x)
      if (rows[y] & (1 << (4 - x))) Pixel(icon, left + static_cast<int>(i) * 6 + x,
                                         9 + y, 244, 246, 250);
  }
}

bool Renderable(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char c) {
    return c == ' ' || c == '-' || (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  });
}

std::string Slot(std::string_view name) {
  if (name == "a") return "confirm_a";
  if (name == "b") return "back_b";
  if (name == "x" || name == "y" || name.starts_with("dpad_")) return std::string(name);
  if (name == "left_shoulder") return "left_bumper";
  if (name == "right_shoulder") return "right_bumper";
  if (name == "left_trigger" || name == "right_trigger") return std::string(name);
  if (name == "left_thumb") return "left_stick";
  if (name == "right_thumb") return "right_stick";
  return {};
}

const std::vector<Source>* Sources(const EffectiveInput& input,
                                   const profile::InputRowToken& row) {
  switch (row.kind) {
    case profile::InputRowKind::kGeneralBinding:
      if (auto target = profile::remap::ParseTarget(row.name)) return input.general.Effective(*target);
      break;
    case profile::InputRowKind::kControllerBinding:
      if (auto action = profile::gameplay_remap::ParseAction(row.name)) return input.controller.Effective(*action);
      break;
    case profile::InputRowKind::kKeyboardMouseBinding:
      if (auto action = profile::gameplay_remap::ParseAction(row.name)) return input.keyboard_mouse.Effective(*action);
      break;
    case profile::InputRowKind::kMusicBinding:
      return input.music.Effective(row.name);
    default: break;
  }
  return nullptr;
}

Icon Unbound() {
  Icon icon;
  icon.art = Art::kUnbound;
  icon.legend = "NONE";
  DrawKeycap(&icon);
  return icon;
}

Icon FirstOf(const View& view, const std::vector<Icon>& all) {
  const SourceKind wanted = view.active_family == Family::kController ? SourceKind::kPad : SourceKind::kKey;
  for (const Icon& icon : all) {
    if (icon.source.kind == wanted ||
        (view.active_family == Family::kKeyboardMouse && icon.source.kind == SourceKind::kMouse))
      return icon;
  }
  return Unbound();
}
}  // namespace

View Capture(std::string controller_scheme, Family family,
             const controller_schemes::Registry* registry) {
  auto snapshot = CurrentInput();
  return {snapshot, std::move(controller_scheme), family, snapshot->revision, registry};
}

Icon Resolve(const Source& source, std::string_view controller_scheme,
             const controller_schemes::Registry* registry) {
  Icon icon;
  icon.source = source;
  icon.legend = profile::SourceDisplayName(source);
  if (source.kind == SourceKind::kPad) {
    icon.scheme = controller_scheme.empty() ? "retail" : std::string(controller_scheme);
    if (!profile::remap::ParseTarget(source.name) &&
        !profile::remap::ParsePadDirection(source.name)) {
      icon.art = Art::kUnsupported;
      icon.diagnostic = "unsupported physical pad source: " + source.name;
      icon.legend = "?";
      DrawKeycap(&icon);
      return icon;
    }
    icon.slot = Slot(source.name);
    // The sheet has no Start/Back or stick-direction glyphs. Render a distinct built-in
    // pictogram until those physical sources get an authored slot in a future asset format.
    icon.art = icon.slot.empty() ? Art::kBuiltIn : Art::kSchemeSlot;
    if (icon.art == Art::kBuiltIn) DrawKeycap(&icon);
    if (icon.art == Art::kSchemeSlot && registry && icon.scheme != "retail") {
      const auto scheme = registry->schemes.find(icon.scheme);
      if (scheme == registry->schemes.end()) {
        icon.diagnostic = "controller package unavailable; retail glyph used";
      } else if (const auto image = scheme->second.buttons.find(icon.slot);
                 image != scheme->second.buttons.end()) {
        icon.art = Art::kPackageImage;
        icon.width = image->second.width;
        icon.height = image->second.height;
        icon.rgba = image->second.rgba;
      }
    }
  } else if (source.kind == SourceKind::kMouse) {
    if (!profile::remap::ParseSource("mouse:" + source.name)) {
      icon.art = Art::kUnsupported;
      icon.legend = "?";
      icon.diagnostic = "unsupported mouse source: " + source.name;
    } else if (!Renderable(icon.legend)) {
      icon.art = Art::kUnsupported;
      icon.diagnostic = "mouse legend cannot be rendered: " + source.name;
      icon.legend = "?";
    } else {
      icon.art = Art::kBuiltIn;
    }
    DrawKeycap(&icon);
  } else {
    if (!profile::remap::ParseSource("key:" + source.name)) {
      icon.art = Art::kUnsupported;
      icon.legend = "?";
      icon.diagnostic = "unsupported key source: " + source.name;
    } else if (!Renderable(icon.legend)) {
      icon.art = Art::kUnsupported;
      icon.diagnostic = "key legend cannot be rendered: " + source.name;
      icon.legend = "?";
    } else {
      icon.art = Art::kBuiltIn;
    }
    DrawKeycap(&icon);
  }
  return icon;
}

std::vector<Icon> Ordered(const View& view, const profile::InputRowToken& row) {
  std::vector<Icon> result;
  if (!view.input) return result;
  const auto* sources = Sources(*view.input, row);
  if (!sources) {
    // The two analog General Mapping targets have no compiled row: the physical trigger passes
    // through unchanged. A missing table row is therefore still a real pad binding.
    if (row.kind == profile::InputRowKind::kGeneralBinding) {
      if (auto target = profile::remap::ParseTarget(row.name);
          target && profile::remap::TargetIsTrigger(*target)) {
        result.push_back(Resolve({SourceKind::kPad, row.name}, view.controller_scheme,
                                 view.registry));
      }
    }
    return result;
  }
  result.reserve(sources->size());
  for (const Source& source : *sources)
    result.push_back(Resolve(source, view.controller_scheme, view.registry));
  return result;
}

std::vector<Icon> Ordered(const View& view, profile::gameplay_remap::Action action) {
  std::vector<Icon> result;
  if (!view.input) return result;
  const auto* sources = view.active_family == Family::kController ?
      view.input->controller.Effective(action) : view.input->keyboard_mouse.Effective(action);
  if (!sources) return result;
  result.reserve(sources->size());
  for (const Source& source : *sources)
    result.push_back(Resolve(source, view.controller_scheme, view.registry));
  return result;
}

Icon First(const View& view, const profile::InputRowToken& row) {
  return FirstOf(view, Ordered(view, row));
}

Icon First(const View& view, profile::gameplay_remap::Action action) {
  return FirstOf(view, Ordered(view, action));
}
}  // namespace rb_blitz::input::icons
