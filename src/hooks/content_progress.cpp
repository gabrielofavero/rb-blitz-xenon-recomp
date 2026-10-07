// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R7's progress bar, restored from the guest's own UI rather than added to it.
//
// The connect panel the title shows between the start press and the main menu
// (`ui/net/gen/server_connect.milo_xbox`) already contains the bar: a
// `progress_bar.grp`, a `content_percent.lbl`, and a Flow (`enumerating_content1.flow`)
// whose own comments read
//
//     (How much content we've enumerated so far
//     (How much content we need to enumerate, -1 if we're not enumerating right now
//
// The Flow reads two properties of the panel object, `current_content` and
// `total_content`, and shows `enumerating_content.grp` only while the total is not -1.
// Nothing in the DTA sets them (every .dtb in the ark was decoded and searched), so the
// engine's own C++ does - and it does it through one small object, recompiled here as
// `sub_82759308`..`sub_827593C8`:
//
//   sub_82759308(this, panel)  constructor; current = total = -1, not enumerating
//   sub_827593A8(this, total)  start: total = N, current = 0, then push
//   sub_827593B8(this)         one more item: ++current, then push
//   sub_827593C8(this)         done: current = total, then push
//   sub_82759190(this)         push: panel.current_content = this+8, panel.total_content = this+12
//
// Why the bar does not appear on this build: the two halves of the object are the
// package count (this+12) and the number enumerated so far (this+8), and this layer is
// the only thing that knows how many packages there are - the guest enumerates its DLC
// one item per presented frame through the emulated XamAppEnumerateContentAggregate and
// is never told a total. So when the engine has no total of its own, this file gives it
// the DLC layer's count (rb_blitz::dlc::ContentItemCount(), the size of the flat library
// it registered): the bar then has a denominator the moment the engine increments it.
//
// Hook hygiene (docs/rb3-references.md §8): the faithful behaviour, and the deviation.
//
// Faithful behaviour: with the toggle off (its default) each hook calls the guest's own
// function unchanged and touches no guest state, so the boot is the boot this project had
// before the bar existed. That is also the behaviour with no DLC library registered:
// there is no total to give, so there is nothing to deviate with.
//
// Deviation: with R7 on and a library registered, `sub_827593A8`'s total argument is
// replaced when the engine announces a non-positive one (an unknown total), and
// `sub_827593B8` gives the object the library's count before its first increment if the
// object still has none. Both are the count of packages the content manager will hand
// the title, which is exactly what the Flow wants, and both are logged with the value the
// engine chose, so a boot says which of the two paths was taken.

#include "hooks/content_progress.h"

#include <cstddef>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/dlc.h"

#if defined(_WIN32)

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
REXCVAR_DECLARE(bool, enhancements_dlc_cache);

REX_EXTERN(__imp__sub_827593A8);
REX_EXTERN(__imp__sub_827593B8);
REX_EXTERN(__imp__sub_827593C8);

namespace {

// Read once in Configure(); guest threads only read it afterwards.
bool g_enabled = false;

// The guest is big-endian, and these fields are plain ints in its own image.
int32_t LoadBe32(const uint8_t* p) {
  return static_cast<int32_t>((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                              (uint32_t(p[2]) << 8) | uint32_t(p[3]));
}

void StoreBe32(uint8_t* p, int32_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

}  // namespace

namespace rb_blitz::content_progress {

void Configure() {
  g_enabled = REXCVAR_GET(enhancements_dlc_cache);
  if (g_enabled) {
    REXLOG_INFO("content_progress: the discovery screen's progress bar is given the DLC "
                "library's package count as its total (R7); set --enhancements_dlc_cache=false "
                "to leave the bar exactly as the title draws it");
  } else {
    REXLOG_INFO("content_progress: R7 off; the discovery screen's progress bar is the "
                "title's own");
  }
}

}  // namespace rb_blitz::content_progress

// 0x827593A8 (SetTotal): the engine announcing how many items it is about to enumerate.
// A non-positive total is "not enumerating" to the Flow that draws the bar, so with R7 on
// the host's library count stands in for it.
extern "C" REX_FUNC(sub_827593A8) {
  if (g_enabled) {
    const std::size_t known = rb_blitz::dlc::ContentItemCount();
    REXLOG_INFO("content_progress: the title announced a total of {} item(s) to enumerate "
                "({} package(s) known to this build)",
                ctx.r4.s32, known);
    if (ctx.r4.s32 <= 0 && known != 0) {
      ctx.r4.s64 = static_cast<int64_t>(known);
      REXLOG_INFO("content_progress: the total was not known to the title; the discovery "
                  "bar is given {} instead",
                  known);
    }
  }
  __imp__sub_827593A8(ctx, base);
}

// 0x827593B8 (Increment): one more item enumerated. The engine pushes the object's fields
// to the panel right after this returns, so a total supplied here reaches the bar on the
// same frame. Only ever filled in when the object still has none.
extern "C" REX_FUNC(sub_827593B8) {
  if (g_enabled) {
    const std::size_t known = rb_blitz::dlc::ContentItemCount();
    if (known != 0) {
      if (uint8_t* object = rex::memory::GuestPtr<uint8_t*>(base, ctx.r3.u32)) {
        if (LoadBe32(object + 12) <= 0) {
          StoreBe32(object + 12, static_cast<int32_t>(known));
          REXLOG_INFO("content_progress: the title had no total to enumerate; the discovery "
                      "bar is given {} package(s) on its first item",
                      known);
        }
      }
    }
  }
  __imp__sub_827593B8(ctx, base);
}

// 0x827593C8 (Complete): the enumeration finished, so the Flow draws a full bar.
extern "C" REX_FUNC(sub_827593C8) {
  if (g_enabled) {
    REXLOG_INFO("content_progress: the title's content enumeration finished; the discovery "
                "bar is drawn full");
  }
  __imp__sub_827593C8(ctx, base);
}

#endif  // _WIN32
