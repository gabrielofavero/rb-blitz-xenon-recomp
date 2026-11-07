// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <string_view>
namespace rb_blitz::diag::shader_trace {
// Called by the existing UIManager::Poll observer, independent of mouse/focus
// settings.
void ObserveScreen(std::string_view screen, std::string_view panel,
                   std::string_view focus, bool transitioning);
// Set by the guest UI observer when a named panel is stable. The startup
// indicator combines this with the presenter's first guest image signal.
bool NativeStartupPanelReady();
bool NativeSplashReady();
void ObserveSongController(bool accepts_input);
void LogConfiguration();
} // namespace rb_blitz::diag::shader_trace
