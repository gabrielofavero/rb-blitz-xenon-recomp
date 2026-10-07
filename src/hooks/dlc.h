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

// R7's in-game refresh: re-scan the configured DLC libraries, rewrite the enumeration
// cache and hand the content manager the fresh list, so a package dropped in while the
// title is running is enumerated from then on. Never reads the cache - that is the
// point - and runs on the calling thread, so whatever asked for it is the progress
// display. Safe to call before the guest exists; a missing content manager is logged
// and nothing else happens.
void Refresh(rex::Runtime* runtime, const std::filesystem::path& game_data_root);

// The same refresh, using the runtime and root Configure() was given. This is what the
// in-game trigger calls: it runs from a filesystem hook that has no arguments, and it
// does nothing unless the R7 toggle is on. Cheap to call - the toggle and the pointers
// are read without touching any registry - so a hook may call it freely.
void RefreshConfigured();

// Whether an in-game refresh can happen at all: the R7 toggle is on and Configure() has
// run. A hook checks this before it does any work - including reading the path a call
// names - so a run with the toggle off pays nothing.
bool RefreshArmed();

}  // namespace rb_blitz::dlc
