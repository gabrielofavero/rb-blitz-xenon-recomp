// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the remap vocabulary (src/launcher/remap.h).

#include "launcher/remap.h"

#include <algorithm>
#include <array>
#include <utility>

namespace rb_blitz::launcher::remap {
namespace {

// The targets, in the order the panel lists them, with everything the rest of this file needs
// to know about one. One table rather than five switches, so a target cannot be half-added:
// a name with no bit, or a label the panel cannot find.
struct TargetInfo {
  Target target;
  const char* name;
  const char* label;
  uint16_t bit;
};

constexpr std::array<TargetInfo, 17> kTargetInfo{{
    {Target::kA, "a", "A", 0x1000},
    {Target::kB, "b", "B", 0x2000},
    {Target::kX, "x", "X", 0x4000},
    {Target::kY, "y", "Y", 0x8000},
    {Target::kLeftShoulder, "left_shoulder", "Left bumper", 0x0100},
    {Target::kRightShoulder, "right_shoulder", "Right bumper", 0x0200},
    // The triggers have no bit: the guest reads them as analog values, so a binding writes the
    // byte instead. See TargetBit.
    {Target::kLeftTrigger, "left_trigger", "Left trigger", 0x0000},
    {Target::kRightTrigger, "right_trigger", "Right trigger", 0x0000},
    {Target::kBack, "back", "Back", 0x0020},
    {Target::kStart, "start", "Start", 0x0010},
    {Target::kGuide, "guide", "Guide", 0x0400},
    {Target::kLeftThumb, "left_thumb", "Left stick", 0x0040},
    {Target::kRightThumb, "right_thumb", "Right stick", 0x0080},
    {Target::kDpadUp, "dpad_up", "D-pad up", 0x0001},
    {Target::kDpadDown, "dpad_down", "D-pad down", 0x0002},
    {Target::kDpadLeft, "dpad_left", "D-pad left", 0x0004},
    {Target::kDpadRight, "dpad_right", "D-pad right", 0x0008},
}};

const TargetInfo* InfoFor(Target target) {
  for (const TargetInfo& info : kTargetInfo) {
    if (info.target == target) {
      return &info;
    }
  }
  return nullptr;
}

// TOML has no escaping to do here: every name and source this module writes is a bare word.
std::string Trim(std::string_view text) {
  const std::size_t first = text.find_first_not_of(" \t");
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(" \t");
  return std::string(text.substr(first, last - first + 1));
}

// The source prefixes, which are the whole grammar: everything after the colon belongs to the
// device that named it.
std::optional<SourceKind> KindForPrefix(std::string_view prefix) {
  if (prefix == "pad") {
    return SourceKind::kPad;
  }
  if (prefix == "key") {
    return SourceKind::kKey;
  }
  if (prefix == "mouse") {
    return SourceKind::kMouse;
  }
  return std::nullopt;
}

std::string_view PrefixForKind(SourceKind kind) {
  switch (kind) {
    case SourceKind::kPad:
      return "pad";
    case SourceKind::kKey:
      return "key";
    case SourceKind::kMouse:
      return "mouse";
  }
  return "pad";
}

bool IsKnownMouseName(std::string_view name) {
  return name == "left" || name == "right" || name == "middle" || name == "x1" || name == "x2";
}

// A key name is SDL's, which this module deliberately does not link: the launcher gets one from
// SDL when it captures a key and the game resolves it back, so all this has to check is that it
// could be a name at all. A name nothing resolves to simply never fires.
bool IsPlausibleSourceName(std::string_view name) {
  if (name.empty() || name.size() > 32) {
    return false;
  }
  return std::none_of(name.begin(), name.end(), [](char c) {
    return c == ',' || c == ' ' || c == '\t' || c == ':';
  });
}

bool IsKnownPadName(std::string_view name) {
  for (const TargetInfo& info : kTargetInfo) {
    if (info.name == name) {
      return true;
    }
  }
  return false;
}

std::optional<Source> ParseOneSource(std::string_view text) {
  // A comma-separated value is written with spaces after the commas ("pad:b, key:space"), so
  // the whitespace around a token is not part of it.
  const std::string token = Trim(text);
  const std::size_t colon = token.find(':');
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  const std::optional<SourceKind> kind = KindForPrefix(std::string_view(token).substr(0, colon));
  if (!kind) {
    return std::nullopt;
  }
  const std::string name = Trim(std::string_view(token).substr(colon + 1));
  switch (*kind) {
    case SourceKind::kPad:
      if (!IsKnownPadName(name)) {
        return std::nullopt;
      }
      break;
    case SourceKind::kMouse:
      if (!IsKnownMouseName(name)) {
        return std::nullopt;
      }
      break;
    case SourceKind::kKey:
      if (!IsPlausibleSourceName(name)) {
        return std::nullopt;
      }
      break;
  }
  return Source{*kind, name};
}

// Nothing when any token is not a source this build knows, which is what makes a value either
// wholly understood or kept verbatim - a half-applied list would silently drop the one source a
// newer launcher meant.
std::optional<std::vector<Source>> ParseSourceList(std::string_view text) {
  std::vector<Source> sources;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = text.find(',', start);
    const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
    const std::optional<Source> source = ParseOneSource(text.substr(start, end - start));
    if (!source) {
      return std::nullopt;
    }
    // The same source twice is one press, not two.
    if (std::find(sources.begin(), sources.end(), *source) == sources.end()) {
      sources.push_back(*source);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return sources;
}

std::string JoinSources(const std::vector<Source>& sources) {
  std::string text;
  for (const Source& source : sources) {
    if (!text.empty()) {
      text += ", ";
    }
    text += FormatSource(source);
  }
  return text;
}

}  // namespace

const std::vector<Target>& Targets() {
  static const std::vector<Target> targets = [] {
    std::vector<Target> all;
    all.reserve(kTargetInfo.size());
    for (const TargetInfo& info : kTargetInfo) {
      all.push_back(info.target);
    }
    return all;
  }();
  return targets;
}

std::string_view TargetName(Target target) {
  const TargetInfo* info = InfoFor(target);
  return info == nullptr ? std::string_view{} : std::string_view(info->name);
}

std::optional<Target> ParseTarget(std::string_view name) {
  for (const TargetInfo& info : kTargetInfo) {
    if (info.name == name) {
      return info.target;
    }
  }
  return std::nullopt;
}

std::string_view TargetLabel(Target target) {
  const TargetInfo* info = InfoFor(target);
  return info == nullptr ? std::string_view{} : std::string_view(info->label);
}

uint16_t TargetBit(Target target) {
  const TargetInfo* info = InfoFor(target);
  return info == nullptr ? 0 : info->bit;
}

bool TargetIsTrigger(Target target) {
  return target == Target::kLeftTrigger || target == Target::kRightTrigger;
}

bool operator==(const Source& left, const Source& right) {
  return left.kind == right.kind && left.name == right.name;
}

std::optional<Source> ParseSource(std::string_view text) { return ParseOneSource(text); }

std::string FormatSource(const Source& source) {
  return std::string(PrefixForKind(source.kind)) + ":" + source.name;
}

Table Table::FromRows(const std::vector<ProfileSetting>& rows) {
  Table table;
  for (const ProfileSetting& row : rows) {
    const std::optional<Target> target = ParseTarget(row.key);
    const std::optional<std::vector<Source>> sources =
        row.value.empty() ? std::optional<std::vector<Source>>{std::vector<Source>{}}
                          : ParseSourceList(row.value);
    if (!target || !sources) {
      table.unknown_.push_back(row);
      continue;
    }
    table.Set(*target, *sources);
  }
  return table;
}

std::vector<ProfileSetting> Table::ToRows() const {
  std::vector<ProfileSetting> rows;
  rows.reserve(bindings_.size() + unknown_.size());
  for (const Target target : Targets()) {
    const std::vector<Source>* sources = Bindings(target);
    if (sources == nullptr) {
      continue;
    }
    // A basic string, always: an empty list and a list are both strings, so the value's style
    // cannot be inferred and does not need to be.
    rows.push_back(ProfileSetting{std::string(TargetName(target)), JoinSources(*sources),
                                  ValueStyle::kBasic});
  }
  for (const ProfileSetting& row : unknown_) {
    rows.push_back(row);
  }
  return rows;
}

const std::vector<Source>* Table::Bindings(Target target) const {
  for (const Binding& binding : bindings_) {
    if (binding.target == target) {
      return &binding.sources;
    }
  }
  return nullptr;
}

bool Table::IsDisabled(Target target) const {
  const std::vector<Source>* sources = Bindings(target);
  return sources != nullptr && sources->empty();
}

void Table::Set(Target target, std::vector<Source> sources) {
  for (Binding& binding : bindings_) {
    if (binding.target == target) {
      binding.sources = std::move(sources);
      return;
    }
  }
  bindings_.push_back(Binding{target, std::move(sources)});
}

void Table::Reset(Target target) {
  bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                 [target](const Binding& binding) {
                                   return binding.target == target;
                                 }),
                  bindings_.end());
}

void Table::ResetAll() { bindings_.clear(); }

std::size_t Table::BoundCount() const { return bindings_.size(); }

bool Table::AnySourceSet() const {
  return std::any_of(bindings_.begin(), bindings_.end(), [](const Binding& binding) {
    return !binding.sources.empty();
  });
}

void Apply(const Table& table, const PadState& state, DownFn down, void* context, PadState* out) {
  // Every binding reads `state` and not `out`, so a pair of bindings that swap two controls
  // both see the pad as it was and neither can feed the other.
  *out = state;
  for (const Target target : Targets()) {
    const std::vector<Source>* sources = table.Bindings(target);
    if (sources == nullptr) {
      continue;
    }
    bool pressed = false;
    for (const Source& source : *sources) {
      bool this_one = false;
      switch (source.kind) {
        case SourceKind::kPad: {
          const std::optional<Target> from = ParseTarget(source.name);
          if (!from) {
            break;
          }
          this_one = TargetIsTrigger(*from)
                         ? TriggerPressed(*from == Target::kLeftTrigger ? state.left_trigger
                                                                        : state.right_trigger)
                         : (state.buttons & TargetBit(*from)) != 0;
          break;
        }
        case SourceKind::kKey:
        case SourceKind::kMouse:
          this_one = down != nullptr && down(context, source);
          break;
      }
      if (this_one) {
        pressed = true;
        break;
      }
    }

    if (TargetIsTrigger(target)) {
      // A trigger target is driven to the end of its range, not to "on": the guest reads the
      // byte, and there is nothing in between that a button press could mean.
      uint8_t& trigger = target == Target::kLeftTrigger ? out->left_trigger : out->right_trigger;
      trigger = pressed ? 0xFF : 0x00;
    } else if (pressed) {
      out->buttons |= TargetBit(target);
    } else {
      out->buttons &= static_cast<uint16_t>(~TargetBit(target));
    }
  }
}

}  // namespace rb_blitz::launcher::remap
