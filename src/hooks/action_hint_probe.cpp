// SPDX-License-Identifier: GPL-2.0-only
// Action-aware guest InlineHelp icons. The guest owns visibility and drawing.
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/ppc/hook.h>
#include <rex/logging/log_category.h>
#include <rex/memory/endian.h>
#include <rex/memory/mapped_ptr.h>

#include "hooks/action_hint_probe.h"
#include "assets/texture_override.h"
#include "input/input_icons.h"
#include "input/inline_help.h"
#include "input/last_device.h"
#include "input/native_menu.h"
#include "profile/input_rows.h"
#include "ui/input_font_scene.h"
#include "ui/input_icon_rows.h"
#include "ui/settings_screen.h"
#include "ui/portrait_menu.h"

REX_EXTERN(__imp__sub_824D85E8);  // InlineHelp::DrawShowing
REX_EXTERN(__imp__sub_824D9330);  // InlineHelp::UpdateLabels
REX_EXTERN(__imp__sub_824D8FF0);  // format and assign one UILabel
REX_EXTERN(sub_824D8FF0);
REX_EXTERN(sub_8245C878);        // Symbol(out_slot, chars)
REX_EXTERN(sub_82462FA0);        // Localize(Symbol, found, Locale*)
REX_EXTERN(__imp__sub_822C45A8);  // loading screen UI message handler
REX_EXTERN(sub_821F13D0);        // RndTransformable::WorldXfm

namespace {
namespace font = rb_blitz::ui::input_font;
constexpr std::array<const char*, font::kHintCount> kTargets{
    "", "a", "b", "y", "start", "back", "", "", "", "", "",
    "right_shoulder", "", "", "x", ""};
static_assert(font::kSplashStartHint != rb_blitz::assets::kHeaderPreviousIcon);
static_assert(font::kSplashStartHint != rb_blitz::assets::kHeaderNextIcon);
using Entry = rb_blitz::input::InlineHelpEntry;
thread_local const std::vector<Entry>* g_entries = nullptr;

bool TraceEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("RB_BLITZ_P10_TRACE");
    return value && value[0] == '1' && value[1] == '\0';
  }();
  return enabled;
}
uint32_t Be32(const uint8_t* base, uint32_t address) {
  return rex::memory::load_and_swap<uint32_t>(base + address);
}
std::vector<Entry> Entries(const uint8_t* base, uint32_t object) {
  return rb_blitz::input::ReadInlineHelpEntries(object,
      [base](uint32_t address) { return Be32(base, address); });
}
std::string Signature(const rb_blitz::input::icons::View& view) {
  return std::to_string(view.revision) + ":" + std::to_string(view.device_revision) +
         ":" + view.controller_scheme + ":" +
         std::to_string(static_cast<int>(view.active_family));
}
void Publish(const rb_blitz::input::icons::View& view, const std::string& signature) {
  static std::mutex mutex;
  static std::string published;
  static std::string published_content;
  std::lock_guard lock(mutex);
  if (signature == published) return;
  rb_blitz::hooks::EnsureInputCatalog(view);
  const std::string content = std::to_string(view.revision) + ":" + view.controller_scheme;
  if (content == published_content) {
    rb_blitz::assets::PublishHintIcons(content, {}, {}, view.active_family);
    published = signature;
    return;
  }
  std::array<std::vector<rb_blitz::input::icons::Icon>, 3> variants;
  std::array<rb_blitz::input::icons::Icon, 3> calibration;
  constexpr std::array families{rb_blitz::input::icons::Family::kController,
                                rb_blitz::input::icons::Family::kKeyboard,
                                rb_blitz::input::icons::Family::kMouse};
  for (size_t family = 0; family < families.size(); ++family) {
    auto family_view = view;
    family_view.active_family = families[family];
    auto& icons = variants[family];
    icons.reserve(kTargets.size());
    for (const char* target : kTargets)
      icons.push_back(rb_blitz::input::icons::Hint(family_view,
          {rb_blitz::profile::InputRowKind::kGeneralBinding, target}));
    icons[font::kSplashStartHint] = rb_blitz::input::icons::SplashStartHint(family_view);
    // The header's LT/RT labels use the retail buttons font. Supply their
    // effective actions to the texture provider for its keyboard/mouse sheets.
    icons[rb_blitz::assets::kHeaderPreviousIcon] = rb_blitz::input::icons::First(family_view,
        {rb_blitz::profile::InputRowKind::kGeneralBinding, "left_trigger"});
    icons[rb_blitz::assets::kHeaderNextIcon] = rb_blitz::input::icons::First(family_view,
        {rb_blitz::profile::InputRowKind::kGeneralBinding, "right_trigger"});
    // Localized gameplay labels share the hint font, but each action has its own tile.
    icons[font::kGameplayPowerupHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kPowerup);
    icons[font::kGameplayFirstNoteHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kFirstNote);
    icons[font::kGameplaySecondNoteHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kSecondNote);
    icons[font::kGameplayMoveRightHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kMoveLaneRight);
    icons[font::kGameplayMoveLeftHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kMoveLaneLeft);
    icons[font::kGameplayPauseHint] = rb_blitz::input::icons::First(
        family_view, rb_blitz::profile::gameplay_remap::Action::kPause);
    calibration[family] = rb_blitz::input::icons::Hint(family_view,
        {rb_blitz::profile::InputRowKind::kGeneralBinding, "a"});
  }
  rb_blitz::assets::PublishHintIcons(content, std::move(variants),
                                     std::move(calibration), view.active_family);
  published_content = content;
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

namespace rb_blitz::hooks {
std::shared_ptr<const ui::input_icon_rows::Catalog> EnsureInputCatalog(
    const input::icons::View& view) {
  static std::mutex mutex;
  static std::string prepared_signature;
  static std::shared_ptr<const ui::input_icon_rows::Catalog> current;
  static const std::vector<std::vector<std::string>> panel_symbols = [] {
    std::vector<std::vector<std::string>> symbols;
    for (const auto& panel : ui::screen::BindingPanelNames())
      symbols.push_back(ui::screen::PanelRowSymbols(panel));
    return symbols;
  }();
  const std::string signature = std::to_string(view.revision) + ":" + view.controller_scheme;
  std::lock_guard lock(mutex);
  if (current && signature == prepared_signature) return current;
  auto catalog = std::make_shared<ui::input_icon_rows::Catalog>(
      ui::input_icon_rows::BuildCatalog(panel_symbols, view, current.get()));
  assets::PublishInputIcons("catalog:" + signature, catalog->icons);
  prepared_signature = signature;
  current = catalog;
  REXLOG_INFO("input_icons: prepared stable catalog with {} source slots", catalog->icons.size());
  return current;
}

void PublishActionHints() {
  if (!rb_blitz::assets::InputIconsAvailable()) return;
  const auto view = rb_blitz::input::icons::CaptureActive(
      rb_blitz::assets::ControllerSchemeRegistry());
  Publish(view, Signature(view));
}
}  // namespace rb_blitz::hooks

REX_HOOK_RAW(sub_824D9330) {
  if (rb_blitz::assets::InputIconsAvailable()) {
    const auto view = rb_blitz::input::icons::CaptureActive(
        rb_blitz::assets::ControllerSchemeRegistry());
    Publish(view, Signature(view));
  }
  const auto entries = Entries(base, ctx.r3.u32);
  const auto* previous = g_entries;
  g_entries = &entries;
  rex::ScopeGuard restore_entries([&] { g_entries = previous; });
  __imp__sub_824D9330(ctx, base);
  // Retail skips formatting entirely when ButtonToString has no glyph (for
  // example Cancel in some menus). Every mapped action still needs its live
  // input icon, or an empty icon string when touch is active.
  if (rb_blitz::assets::InputIconsAvailable() || rb_blitz::input::last_device::Current().using_touch) {
    const auto format = Be32(base, 0x8298E8D4);
    for (const auto& entry : entries) {
      if (entry.action >= kTargets.size() || !*kTargets[entry.action]) continue;
      auto caption = Be32(base, entry.record + 16);
      const auto alternate = Be32(base, entry.record + 24);
      if (base[0x8298E8AA] && alternate && base[alternate]) caption = alternate;
      rex::ppc::Call<void>(ctx, base, sub_824D8FF0, entry.label, format, caption, caption);
    }
  }
}

REX_HOOK_RAW(sub_824D8FF0) {
  if (g_entries) {
    for (const auto& entry : *g_entries) {
      if (entry.label != ctx.r3.u32 || entry.action >= kTargets.size() ||
          !*kTargets[entry.action]) continue;
      const bool text_only = rb_blitz::input::last_device::Current().using_touch;
      const bool replace_icon = text_only || rb_blitz::assets::InputIconsAvailable();
      const auto token = rb_blitz::input::InlineHelpCaptionToken(entry,
          [base](uint32_t address) { return Be32(base, address); },
          [base](uint32_t address) {
            const char* chars = address ? reinterpret_cast<const char*>(base + address) : "";
            return std::string_view(chars, strnlen(chars, 64));
          });
      const auto next = rb_blitz::input::CalibrationFooterCaption(
          rb_blitz::input::CalibrationMenuActive(), token);
      const bool replace_caption = next != token;
      if (!replace_icon && !replace_caption) continue;
      const uint32_t code = font::GlyphCode(entry.action);
      if (TraceEnabled()) {
        static std::atomic<unsigned> traced{0};
        if (traced.fetch_add(1) < 12)
          REXLOG_INFO("p10_trace: formatted label={:08X} action={} glyph={:02X}",
                      entry.label, entry.action, code);
      }
      PPCContext call=ctx;
      rex::ScopeGuard restore_fpscr([&] { ctx.fpscr=call.fpscr; });
      rex::ppc::OpenCallFrame(call,base,0x100);
      const uint32_t chars = call.r1.u32 + 0x80;
      if (text_only) {
        base[chars] = 0;
      } else if (code < 0x80) {
        base[chars] = uint8_t(code);
        base[chars + 1] = 0;
      } else {
        base[chars] = uint8_t(0xC0 | (code >> 6));
        base[chars + 1] = uint8_t(0x80 | (code & 0x3F));
        base[chars + 2] = 0;
      }
      uint32_t caption = ctx.r6.u32;
      if (replace_caption) {
        std::memcpy(base + chars + 8, next.data(), next.size());
        base[chars + 8 + next.size()] = 0;
        const uint32_t symbol_slot = chars + 0x30;
        rex::ppc::Call<void>(call, base, sub_8245C878, symbol_slot, chars + 8);
        // The native Action::SetToken uses this same Locale instance.
        const auto translated = rex::ppc::Call<uint32_t>(call, base, sub_82462FA0,
            Be32(base, symbol_slot), uint32_t(0), uint32_t(0x8298D810));
        if (translated) caption = translated;
      }
      call.r3 = ctx.r3;
      call.r4 = ctx.r4;
      call.r5.u32 = replace_icon ? chars : ctx.r5.u32;
      call.r6.u32 = caption;
      __imp__sub_824D8FF0(call, base);
      ctx.r3 = call.r3;
      return;
    }
  }
  __imp__sub_824D8FF0(ctx, base);
}

REX_HOOK_RAW(sub_824D85E8) {
  const uint32_t owner = ctx.r3.u32 >= 224 ? ctx.r3.u32 - 224 : 0;
  const auto entries = owner ? Entries(base, owner) : std::vector<Entry>{};
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
  if ((rb_blitz::assets::InputIconsAvailable() || rb_blitz::input::PortraitSettingsActive() ||
       rb_blitz::input::PortraitConnectionActive()) &&
      ctx.r3.u32 >= 224) {
    const uint32_t object = ctx.r3.u32 - 224;
    if (!entries.empty()) {
      const auto view = rb_blitz::input::icons::CaptureActive(
          rb_blitz::assets::ControllerSchemeRegistry());
      const auto signature = Signature(view);
      if (rb_blitz::assets::InputIconsAvailable()) Publish(view, signature);
      auto widget_signature = signature +
          (rb_blitz::input::last_device::Current().using_touch ? ":text" : ":icons") +
          ":" + std::to_string(rb_blitz::input::MenuScreenGeneration());
      for (const auto& entry : entries)
        widget_signature += ":" + std::to_string(entry.label) + ":" + std::to_string(entry.action);
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
        rex::ppc::Call<void>(ctx,base,sub_824D9330,object);
      }
    }
  }
  // InlineHelp sets every label's world transform before drawing it. Capture
  // those labels directly after the draw instead of depending on a nested
  // UILabel callback (which may receive an adjusted base-class this pointer).
  const auto visible = rb_blitz::input::PortraitMenuHintEntries(ctx, base, owner);
  if (!visible.empty() && visible.size() <= 3 &&
      rb_blitz::input::PortraitMenuHintsAvailable(ctx, base, owner)) {
    // Keep the original action and label vectors: calibration replaces them as
    // its state changes. Draw each live label through the guest widget in its
    // own slot, then restore the complete vectors before the next guest update.
    const auto transform = owner + 4 + Be32(base, Be32(base, owner + 4) + 16);
    const auto world = rb_blitz::input::WorldTransformForDraw(transform,
        [base](uint32_t address) { return base[address]; },
        [&](uint32_t object) { return rex::ppc::Call<uint32_t>(ctx, base, sub_821F13D0, object); });
    const auto x = Be32(base, world + 48), z = Be32(base, world + 56);
    const auto put = [&](uint32_t address, uint32_t value) {
      rex::memory::store_and_swap<uint32_t>(base + address, value);
    };
    rex::ScopeGuard restore([&] {
      put(world + 48, x); put(world + 56, z);
    });
    const auto incoming = ctx;
    const bool drawn = rb_blitz::input::DrawInlineHelpLabelsSeparately(owner,
        [base](uint32_t address) { return Be32(base, address); }, put, [&](size_t i) {
      const auto entry = std::find_if(visible.begin(), visible.end(),
          [&](const auto& entry) { return entry.label == entries[i].label; });
      if (entry == visible.end()) return;
      const auto index = size_t(entry - visible.begin());
      const auto placement = rb_blitz::input::PortraitMenuHintPlacement(
          ctx, base, owner, *entry, index, visible.size());
      const auto slot = rb_blitz::ui::PortraitFooterAction(
          rb_blitz::ui::screen::CurrentMenuConfiguration().portrait_aspect, placement.index, placement.count);
      const auto position = rb_blitz::input::PortraitDrawPoint(slot.x, slot.z);
      rex::memory::store_and_swap<float>(base + world + 48, position.x);
      rex::memory::store_and_swap<float>(base + world + 56, position.y);
      ctx = incoming;
      rb_blitz::input::DrawMenuHints(ctx, base, owner, placement.index, placement.count, __imp__sub_824D85E8);
    });
    if (!drawn) rb_blitz::input::DrawMenuHints(ctx, base, owner, 0, entries.size(), __imp__sub_824D85E8);
  } else {
    rb_blitz::input::DrawMenuHints(ctx, base, owner, 0, entries.size(), __imp__sub_824D85E8);
  }
  for (const auto& entry : visible)
    rb_blitz::input::CaptureMenuHint(ctx, base, entry.label, entry.action, owner);
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
