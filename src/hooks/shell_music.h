// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R9: the main menu's background music, drawn from the loaded DLC instead of the
// three tracks the title ships. See src/hooks/shell_music.cpp for the flow and
// docs/engine/menu-music.md for the evidence.

#pragma once

#include <filesystem>

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::shell_music {

// Hooks the engine's shell-music loader (0x82757F50) so that, with R9 on and a DLC
// package mounted, every track the menu asks for is one of the loaded DLC songs rather
// than the title's own three. The packages are mounted read-only from
// rb_blitz::dlc::LibraryPackagePaths(), so nothing is copied and nothing under game/ is
// written.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(), after dlc::Configure(): it reads the
// library that layer just enumerated, mounts the chosen packages, and leaves the guest
// untouched. With the toggle off - its default - it logs and returns, and the hook is
// inert, so a boot without R9 is the boot this project had before the hook existed.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

}  // namespace rb_blitz::shell_music
