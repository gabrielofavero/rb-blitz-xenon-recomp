// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// DLC content root registration. See src/hooks/dlc.cpp for what the guest does with
// it and docs/dlc.md for where packages go.

#pragma once

#include <filesystem>

namespace rex {
class Runtime;
}  // namespace rex

namespace rb_blitz::dlc {

// Registers the DLC directory resolved from --dlc_root (default <game_data_root>/dlc)
// as an extra read-only content root, so packages dropped there enumerate and open
// exactly like content installed under Documents\rb_blitz.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(): the content manager exists, the
// paths are final and the guest has not started yet. Without the directory this logs
// and returns, leaving the content root as the only DLC source.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

}  // namespace rb_blitz::dlc
