// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The help caption overlay (owner request, 2026-10-13): a subtitle-like bar at the bottom of the
// game window that names the highlighted settings row's description.
//
// What it is, precisely: an ordinary SDK ImGui dialog, the same mechanism as the probe overlay
// (src/diag/probe_overlay.cpp), the mouse cursor and the SDK's achievement toast - so it draws
// over the presented frame, in window coordinates, for as long as the app lives, and it costs
// nothing when there is no caption to draw (OnDraw returns before Begin). White text in a
// translucent black box, centred, wrapped to about three quarters of the window width.
//
// How text arrives - two sources, one sink:
//   * the **guest's own caption events** (the generated DTA's `rbbz-settings/help/<symbol>/-`
//     writes) resolve their symbol through src/ui/help_topics.h and land here via the settings
//     bridge's HelpSink registration;
//   * the **retail pages** the merged menu routes into (no DTA of ours runs there) resolve through
//     the observer in src/hooks/help_observer.cpp and publish through PublishText.
//
// Threading: both publishers run on the guest thread; OnDraw runs on the UI thread. The text is a
// mutex-guarded string copied on draw - the achievement toast's shape - and equal text is a no-op,
// which also keeps the observer's per-frame call free.

#pragma once

#include <memory>
#include <string>
#include <string_view>

struct ImFontAtlas;  // imgui; only the font hook below needs the type

namespace rex::ui {
class ImGuiDrawer;
class ImGuiDialog;
}  // namespace rex::ui

namespace rb_blitz::ui::help {

// Creates the overlay and registers it as the settings bridge's caption sink. Owned by the app
// (src/rb_blitz_app.h) for as long as it lives; destroying it unregisters the sink.
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer);

// Loads the caption's own typeface into the drawer's atlas and remembers it for the overlay's draws
// (owner request, 2026-10-14). Called once, from `RbBlitzApp::OnConfigureFonts` - the only moment
// an `ImFontAtlas` may still be added to - and never refuses: a machine whose system fonts cannot
// be read keeps the SDK's own face, drawn at the same derived size, which is a legible fallback
// rather than no caption at all. What it chose, and why, is logged, because "the caption got
// bigger" is otherwise a claim with no evidence.
void ConfigureCaptionFont(ImFontAtlas* atlas);

// The thread-safe publish the caption sources call. Empty text hides the caption; text equal to
// the current caption is a no-op, so publishing every frame cannot churn the UI thread.
void PublishText(std::string text);

// The guest channel's spelling of PublishText: a row's canonical symbol, resolved through the
// text tables. An unknown or empty symbol publishes nothing/hides.
void PublishSymbol(std::string_view symbol);

}  // namespace rb_blitz::ui::help
