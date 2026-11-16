// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rb_blitz::platform {
std::vector<std::string> IOSCommandLineArguments();
// Create the user-facing directories shared through the iOS Files app.
std::filesystem::path PrepareIOSDocuments();
void SetIOSApplicationActive(bool active);
void SetIOSInSong(bool in_song);  // Guest-authored game_screen enter/exit.
bool ConsumeIOSAutoPause();      // Guest poll, when the native game can pause.
void WaitForIOSForeground();  // Guest main-loop checkpoint; never called by the UI thread.
void LogIOSPresentationState();  // UI thread only.
}  // namespace rb_blitz::platform
