// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rex::ui { struct RawImage; }

namespace rb_blitz::platform {
std::vector<std::string> IOSCommandLineArguments();
// Create the user-facing directories shared through the iOS Files app.
std::filesystem::path PrepareIOSDocuments();
// Bounded device evidence for guest rendering versus UIKit presentation failures.
void SaveIOSStartupFrame(const std::filesystem::path& documents, const rex::ui::RawImage& frame);
void SetIOSApplicationActive(bool active);
void WaitForIOSForeground();  // Guest main-loop checkpoint; never called by the UI thread.
void LogIOSPresentationState();  // UI thread only.
}  // namespace rb_blitz::platform
