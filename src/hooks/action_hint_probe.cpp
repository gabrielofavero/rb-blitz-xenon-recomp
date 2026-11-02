// SPDX-License-Identifier: GPL-2.0-only
// Action-aware guest InlineHelp icons. The guest owns visibility and drawing.
#include <array>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "assets/texture_override.h"
#include "input/input_icons.h"
#include "profile/input_rows.h"
#include "ui/input_font_scene.h"

REX_EXTERN(__imp__sub_824D85E8);  // InlineHelp::DrawShowing
REX_EXTERN(__imp__sub_824D9330);  // InlineHelp::UpdateLabels
REX_EXTERN(__imp__sub_824D8FF0);  // format and assign one UILabel
REX_EXTERN(__imp__sub_822C45A8);  // loading screen UI message handler

namespace {
namespace font = rb_blitz::ui::input_font;
constexpr std::array<const char*, font::kHintCount> kTargets{
    "", "a", "b", "y", "start", "back", "", "", "", "", "",
    "right_shoulder", "", "", "x"};
struct Entry { uint32_t label, action; };
thread_local const std::vector<Entry>* g_entries = nullptr;

bool TraceEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("RB_BLITZ_P10_TRACE");
    return value && value[0] == '1' && value[1] == '\0';
  }();
  return enabled;
}
bool GuestRange(uint32_t address, uint32_t size) {
  return address >= 0x40000000 && uint64_t(address) + size <= 0x7F000000;
}
uint32_t Be32(const uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
std::vector<Entry> Entries(const uint8_t* base, uint32_t object) {
  if (!GuestRange(object, 100)) return {};
  const uint32_t begin = Be32(base, object + 80);
  const uint32_t end = Be32(base, object + 84);
  const uint32_t labels = Be32(base, object + 92);
  const uint32_t labels_end = Be32(base, object + 96);
  if (end < begin || end - begin > 15 * 28 || (end - begin) % 28 ||
      labels_end < labels || labels_end - labels != (end - begin) / 7 ||
      !GuestRange(begin, end - begin) || !GuestRange(labels, labels_end - labels)) return {};
  std::vector<Entry> result;
  for (uint32_t i = 0; i < (end - begin) / 28; ++i) {
    const uint32_t label = Be32(base, labels + i * 4);
    if (GuestRange(label, 4)) result.push_back({label, Be32(base, begin + i * 28)});
  }
  return result;
}
std::string Signature(const rb_blitz::input::icons::View& view) {
  return std::to_string(view.revision) + ":" + std::to_string(view.device_revision) +
         ":" + view.controller_scheme + ":" +
         std::to_string(static_cast<int>(view.active_family));
}
void Publish(const rb_blitz::input::icons::View& view, const std::string& signature) {
  static std::mutex mutex;
  static std::string published;
  std::lock_guard lock(mutex);
  if (signature == published) return;
  std::vector<rb_blitz::input::icons::Icon> icons;
  icons.reserve(kTargets.size());
  for (const char* target : kTargets)
    icons.push_back(rb_blitz::input::icons::First(view,
        {rb_blitz::profile::InputRowKind::kGeneralBinding, target}));
  rb_blitz::assets::PublishHintIcons(signature, std::move(icons));
  published = signature;
  if (TraceEnabled()) REXLOG_INFO("p10_trace: hints published {}", signature);
}
void TraceInlineHelp(const uint8_t* base, uint32_t draw) {
  const auto entries = Entries(base, draw - 224);
  if (entries.empty()) return;
  REXLOG_INFO("p10_trace: inline_help draw={:08X} count={}", draw, entries.size());
  for (size_t i = 0; i < entries.size(); ++i)
    REXLOG_INFO("p10_trace: row={} label={:08X} action={}",
                i, entries[i].label, entries[i].action);
}
}  // namespace

REX_HOOK_RAW(sub_824D9330) {
  if (!rb_blitz::assets::InputIconsAvailable()) {
    __imp__sub_824D9330(ctx, base);
    return;
  }
  const auto view = rb_blitz::input::icons::CaptureActive(
      rb_blitz::assets::ControllerSchemeRegistry());
  Publish(view, Signature(view));
  const auto entries = Entries(base, ctx.r3.u32);
  const auto* previous = g_entries;
  g_entries = &entries;
  __imp__sub_824D9330(ctx, base);
  g_entries = previous;
}

REX_HOOK_RAW(sub_824D8FF0) {
  if (g_entries) {
    for (const auto& entry : *g_entries) {
      if (entry.label != ctx.r3.u32 || entry.action >= kTargets.size() ||
          !*kTargets[entry.action]) continue;
      const uint32_t code = font::GlyphCode(entry.action);
      if (TraceEnabled()) {
        static std::atomic<unsigned> traced{0};
        if (traced.fetch_add(1) < 12)
          REXLOG_INFO("p10_trace: formatted label={:08X} action={} glyph={:02X}",
                      entry.label, entry.action, code);
      }
      rex::CallFrame frame(ctx);
      auto& call = frame.ctx;
      call.r1.u32 -= 0x100;
      const uint32_t chars = call.r1.u32 + 0x80;
      if (code < 0x80) {
        base[chars] = uint8_t(code);
        base[chars + 1] = 0;
      } else {
        base[chars] = uint8_t(0xC0 | (code >> 6));
        base[chars + 1] = uint8_t(0x80 | (code & 0x3F));
        base[chars + 2] = 0;
      }
      call.r3 = ctx.r3;
      call.r4 = ctx.r4;
      call.r5.u32 = chars;
      call.r6 = ctx.r6;
      __imp__sub_824D8FF0(call, base);
      ctx.r3 = call.r3;
      return;
    }
  }
  __imp__sub_824D8FF0(ctx, base);
}

REX_HOOK_RAW(sub_824D85E8) {
  if (TraceEnabled()) {
    static std::mutex mutex;
    static std::unordered_set<uint32_t> seen;
    bool trace = false;
    {
      std::lock_guard lock(mutex);
      if (seen.size() < 24) trace = seen.insert(ctx.r3.u32).second;
    }
    if (trace) TraceInlineHelp(base, ctx.r3.u32);
  }
  if (rb_blitz::assets::InputIconsAvailable() && ctx.r3.u32 >= 224) {
    const uint32_t object = ctx.r3.u32 - 224;
    const auto entries = Entries(base, object);
    if (!entries.empty()) {
      const auto view = rb_blitz::input::icons::CaptureActive(
          rb_blitz::assets::ControllerSchemeRegistry());
      const auto signature = Signature(view);
      Publish(view, signature);
      const auto widget_signature = signature + ":" +
          std::to_string(entries.front().label);
      static std::mutex mutex;
      static std::unordered_map<uint32_t, std::string> updated;
      bool refresh = false;
      {
        std::lock_guard lock(mutex);
        if (updated.size() > 1024) updated.clear();
        auto& prior = updated[object];
        refresh = prior != widget_signature;
        prior = widget_signature;
      }
      if (refresh) {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u32 = object;
        sub_824D9330(frame.ctx, base);
      }
    }
  }
  __imp__sub_824D85E8(ctx, base);
}

REX_HOOK_RAW(sub_822C45A8) {
  // The loading prompt is populated on entry and may be the only action hint
  // on screen. Keep its Confirm tile current without rewriting the guest label.
  if (rb_blitz::assets::InputIconsAvailable()) {
    const auto view = rb_blitz::input::icons::CaptureActive(
        rb_blitz::assets::ControllerSchemeRegistry());
    Publish(view, Signature(view));
  }
  __imp__sub_822C45A8(ctx, base);
}
