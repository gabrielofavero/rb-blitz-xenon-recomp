// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "input/menu_music_nav.h"
#include "profile/music_bindings.h"

namespace rb_blitz::input::menu_music_nav {
// Raw sources are sampled even outside the gate. A held input cannot become a
// command on menu entry, focus return, enabling a device or publishing a binding.
class BindingLatch {
 public:
  template <typename KeyDown>
  std::optional<Press> Update(const profile::music::Bindings& bindings,
      const profile::remap::PadState& pad, KeyDown key_down, bool keyboard,
      bool enabled, bool pause_supported, std::vector<profile::remap::Source>* consumed) {
    using namespace profile::remap;
    bool raw[3]{}, allowed[3]{};
    Producer producer[3]{Producer::kPad, Producer::kPad, Producer::kPad};
    std::string signature;
    for (size_t i = 0; i < 3; ++i) {
      for (const auto& source : *bindings.Effective(profile::music::kActions[i])) {
        signature += FormatSource(source) + ";";
        const bool held = source.kind == SourceKind::kPad ? PadSourceHeld(pad, source.name)
                                                         : key_down(source.name);
        raw[i] |= held;
        const bool supported = i != 2 || pause_supported;
        if (held && supported && (source.kind == SourceKind::kPad || keyboard)) {
          allowed[i] = true;
          if (source.kind == SourceKind::kKey) producer[i] = Producer::kMediaKey;
          if (enabled && std::find(owned_.begin(), owned_.end(), source) == owned_.end())
            owned_.push_back(source);
        }
      }
      signature += "|";
    }
    std::optional<Press> press;
    const bool boundary = signature_ != signature || keyboard_ != keyboard;
    for (size_t i = 0; i < 3; ++i) {
      if (!boundary && enabled && allowed[i] && !previous_[i] && !press)
        press = Press{static_cast<Command>(i), producer[i]};
      previous_[i] = raw[i];
    }
    signature_ = std::move(signature);
    keyboard_ = keyboard;
    std::erase_if(owned_, [&](const Source& source) {
      return source.kind == SourceKind::kPad ? !PadSourceHeld(pad, source.name) : !key_down(source.name);
    });
    consumed->insert(consumed->end(), owned_.begin(), owned_.end());
    return press;
  }
 private:
  bool previous_[3]{};
  bool keyboard_ = true;
  std::string signature_;
  std::vector<profile::remap::Source> owned_;
};
}  // namespace rb_blitz::input::menu_music_nav
