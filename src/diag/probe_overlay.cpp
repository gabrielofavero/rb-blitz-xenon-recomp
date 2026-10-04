// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The overlay's drawing. See src/diag/probe_overlay.h for the one thing it needs
// from the application, and src/diag/probe.h for the probe itself.
//
// Everything shown here is either a cvar (the guest's requested video mode, the
// present path) or an event from the ring. Nothing is inferred: where a value is
// not reachable through a public interface - the presenter's own paint rectangle
// is the standing example, docs/engine/probe.md records it - the overlay says so
// instead of guessing, because a probe that reports a plausible number is worse
// than one that reports none.

#include "diag/probe_overlay.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>
#include <rex/cvar.h>

#include "diag/probe.h"
#include "diag/probe_trace.h"

namespace rb_blitz::diag {

namespace {

// Where a row's value starts, so the panel reads as two columns.
constexpr float kProbeOverlayValueColumn = 170.0f;
// How many of the newest events the panel lists: the overlay is for "is it
// alive and in what order", the log dump is for the whole window.
constexpr size_t kProbeOverlayEventRows = 12;

std::string CvarString(const char* name) { return rex::cvar::GetFlagByName(name); }

bool CvarBool(const char* name) { return rex::cvar::Query<bool>(name); }

int32_t CvarInt(const char* name) { return rex::cvar::Query<int32_t>(name); }

double CvarDouble(const char* name) { return rex::cvar::Query<double>(name); }

// A KeyValue row, laid out so the column of values lines up down the panel.
void Row(const char* key, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char value[192];
  std::vsnprintf(value, sizeof(value), fmt, args);
  va_end(args);
  ImGui::TextUnformatted(key);
  ImGui::SameLine(kProbeOverlayValueColumn);
  ImGui::TextUnformatted(value);
}

}  // namespace

ProbeOverlayDialog::ProbeOverlayDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                       WindowStateProvider provider)
    : rex::ui::ImGuiDialog(imgui_drawer), provider_(std::move(provider)) {}

void ProbeOverlayDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  // The per-frame probe work happens here, before the visibility check, because
  // the dialog is registered for the app's whole life and this is the one
  // reliable UI-thread tick the probe has (the live trace file is written here).
  Tick();
  // Off means invisible, not "drawn and empty": returning before Begin() draws
  // nothing at all, and because the cvar is re-read here the toggle is live.
  if (!overlay_enabled()) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.65f);
  if (!ImGui::Begin("Probe##rb_blitz", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  const WindowState window = provider_ ? provider_() : WindowState{};

  if (ImGui::CollapsingHeader("Guest", ImGuiTreeNodeFlags_DefaultOpen)) {
    Row("video mode", "%d x %d @ %.1f Hz", CvarInt("video_mode_width"),
        CvarInt("video_mode_height"), CvarDouble("video_mode_refresh_rate"));
    const std::string resolution = CvarString("resolution");
    Row("resolution preset", "%s", resolution.empty() ? "(unset)" : resolution.c_str());
    Row("letterbox", "%s", CvarBool("present_letterbox") ? "on" : "off");
    Row("safe area", "%d%% x %d%%", CvarInt("present_safe_area_x"),
        CvarInt("present_safe_area_y"));
    const std::string effect = CvarString("present_effect");
    Row("present effect", "%s", effect.empty() ? "(default)" : effect.c_str());
    Row("dither", "%s", CvarBool("present_dither") ? "on" : "off");
    Row("overscan cutoff", "%s", CvarBool("present_allow_overscan_cutoff") ? "on" : "off");
    // The presenter's own paint rectangle is not on any public SDK interface, so
    // it is named as unavailable rather than derived here (docs/engine/probe.md).
    Row("paint rect", "%s", "(not exposed by the SDK)");
  }

  if (ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
    Row("physical", "%u x %u", window.physical_width, window.physical_height);
    Row("logical", "%u x %u", window.logical_width, window.logical_height);
    Row("fullscreen", "%s", window.fullscreen ? "on" : "off");
    Row("dpi scale", "%.3f", static_cast<double>(window.dpi_scale));
  }

  const TraceBuffer& trace = buffer();
  if (ImGui::CollapsingHeader("Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
    Row("recording", "%s", tracing_enabled() ? "on" : "off");
    Row("events", "%zu held, %llu dropped, %llu total", trace.size(),
        static_cast<unsigned long long>(trace.dropped()),
        static_cast<unsigned long long>(trace.total()));

    const std::vector<TraceEvent> events = buffer().Snapshot(kProbeOverlayEventRows);
    if (events.empty()) {
      ImGui::TextUnformatted("no events (set probe_trace = true)");
    } else {
      ImGui::Separator();
      for (const TraceEvent& event : events) {
        // The dump line, minus the argument tail the panel has no room for: the
        // overlay is for "what is the order", the log dump is for the registers.
        if (event.kind == TraceKind::kMark) {
          ImGui::Text("#%06llu %8.3fs %-5s %s", static_cast<unsigned long long>(event.seq),
                      event.time_ms / 1000.0, "mark", event.tag);
        } else {
          ImGui::Text("#%06llu %8.3fs 0x%08X %s  lr=0x%08X",
                      static_cast<unsigned long long>(event.seq), event.time_ms / 1000.0,
                      event.address, event.tag, event.lr);
        }
      }
    }
  }

  ImGui::End();
}

}  // namespace rb_blitz::diag
