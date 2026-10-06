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

// Registers the DLC sources as extra read-only content, so packages dropped there
// enumerate and open exactly like content installed under Documents\Rock Band Blitz:
//
//   * --dlc_root (default <game_data_root>/dlc), laid out as
//     <title_id>/<content_type>/<package>, mounted in place; and
//   * --dlc_library, ';'-separated folders of STFS packages in any arrangement, each
//     placed under the title id and content type in its own header. A --dlc_root that
//     is not the structured layout is read as a library too, so pointing it at a
//     dumped song folder needs no other configuration.
//
// Call once from RbBlitzApp::OnPostLoadXexImage(): the content manager exists, the
// paths are final and the guest has not started yet. Without any source this logs and
// returns, leaving the content root as the only DLC source.
void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

}  // namespace rb_blitz::dlc
