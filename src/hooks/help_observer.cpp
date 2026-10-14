// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The retail-page caption observer. See src/hooks/help_observer.h for what it does and why, and
// src/ui/help_topics.h for the table it resolves against.

#include "hooks/help_observer.h"

#include <cstdint>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/settings_bridge.h"
#include "ui/help_overlay.h"
#include "ui/help_topics.h"

// UIListState::Selected - the logical selected row of a list, read exactly the way the mouse
// navigation reads it (src/input/native_menu.cpp). Declared here because this is a separate TU.
REX_EXTERN(sub_824E3008);

namespace rb_blitz::hooks {
namespace {

uint32_t U32(uint8_t* base, const uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}

// A guest object's name (offset 32 of the object header - the same field native_menu.cpp reads
// for screens and panels). Guarded on both ends: the name pointer must land in mapped memory, and
// the text must be printable ASCII, because a component that is not what we think it is must
// yield an empty name rather than a garbage caption.
std::string ObjectName(uint8_t* base, const uint32_t object) {
  if (object == 0) {
    return {};
  }
  const uint32_t name = U32(base, object + 32);
  if (name == 0) {
    return {};
  }
  const char* text = rex::memory::GuestPtr<const char*>(base, name);
  if (text == nullptr) {
    return {};
  }
  std::string out;
  for (size_t i = 0; i < 96; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c == 0) {
      break;
    }
    if (c < 0x20 || c > 0x7E) {
      return {};
    }
    out.push_back(static_cast<char>(c));
  }
  return out;
}

// `sub_824E3008(r3 = list + 176)` - the selected logical row, or -1 when the list has none.
// Isolated registers, exactly like the mouse navigation's side calls.
int SelectedIndex(PPCContext& ctx, uint8_t* base, const uint32_t list) {
  rex::CallFrame call(ctx);
  call.ctx.r3.u32 = list + 176;
  sub_824E3008(call.ctx, base);
  return call.ctx.r3.s32;
}

}  // namespace

void HelpObserverTick(PPCContext& ctx, uint8_t* base, const HelpFrameObservation& observation) {
  // The caption this observer published last, and the screen its name was read from. `owns` is
  // the ownership rule's whole state: the observer may only clear a caption it published.
  static uint32_t last_screen = 0;
  static std::string last_screen_name;
  static bool owns_caption = false;

  // No screen, or a frame where a menu surface is not current (a transition is in flight, or a
  // song is running): the observer has nothing to describe, and a caption it published is stale.
  if (observation.screen == 0 || !observation.allowed) {
    if (owns_caption) {
      ui::help::PublishText({});
      owns_caption = false;
    }
    last_screen = observation.screen;
    last_screen_name.clear();
    return;
  }

  if (observation.screen != last_screen) {
    last_screen = observation.screen;
    last_screen_name = ObjectName(base, observation.screen);
    REXLOG_DEBUG("help_observer: screen '{}'", last_screen_name.empty() ? "?" : last_screen_name);
  }

  if (ui::help::KnowsScreen(last_screen_name)) {
    std::string focus_name;
    int index = -1;
    if (observation.focus != 0) {
      focus_name = ObjectName(base, observation.focus);
      // The selection is read only from a component the previous frame's draw identified as the
      // focused *list*: it is the one shape whose UIListState at +176 is real. A slider focused on
      // the same page resolves by name alone.
      if (observation.focused_list != 0 && observation.focus == observation.focused_list) {
        index = SelectedIndex(ctx, base, observation.focus);
      }
    }
    const std::string caption = ui::help::CaptionFor({last_screen_name, focus_name, index,
                                                      settings::bridge::ProviderGateActive()});
    ui::help::PublishText(caption);
    owns_caption = !caption.empty();
    return;
  }

  // The screen is not one of the observer's own. Clearing is only correct when the destination is
  // not a screen the guest's own panels publish for: their entry event has already published the
  // destination's caption in this same frame, and a clear here would erase it.
  if (owns_caption) {
    if (!ui::help::IsGuestScreen(last_screen_name)) {
      ui::help::PublishText({});
    }
    owns_caption = false;
  }
}

}  // namespace rb_blitz::hooks
