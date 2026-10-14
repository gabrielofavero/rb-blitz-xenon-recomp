// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// DLC content root registration. See src/hooks/dlc.cpp for what the guest does with
// it and docs/guides/dlc.md for where packages go.

#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

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

// R7's in-game refresh, asked for by the main menu's last row. This is what the in-game
// trigger calls: it runs from a filesystem hook that has no arguments, and it does
// nothing unless the R7 toggle is on. Cheap to call - the toggle and the pointers are
// read without touching any registry - so a hook may call it freely.
//
// It *starts* the refresh and returns. The scan reads every package in the library
// (measured 31 s on a 1,402-package library, docs/plans/finished/2026-10-10-song-cache-plan.md §9.5), so it
// runs on a worker thread and the frame that asked for it keeps drawing; the result is
// handed to the content manager by PublishPendingRefresh() on a later guest thread.
// Only one refresh runs at a time: a second ask while the scan is in flight is logged and
// ignored, so a trigger that fires more than once cannot queue a second scan.
void RefreshConfigured();
// Serializes the restart barrier with refresh worker creation and waits for cache writes.
void DrainRefreshForRestart();

// Hands the content manager a refresh a worker has finished, and does nothing at all when
// there is none - so a hook may call it on every frame or every call. Called from a
// *guest* thread on purpose: ContentManager's `extra_content_library_` is a plain
// std::vector with no lock of its own, so writing it from the worker while the guest lists
// or resolves content would be a data race. Configure() writes it from exactly this kind
// of context at boot, and this is the same write at a different time.
//
// Two callers, both guest threads: the main menu's own per-frame music poll
// (src/hooks/shell_music.cpp), which is the only hook that runs every frame while the menu
// - the screen the refresh row lives on - is up; and the file-call hooks
// (src/hooks/dlc_refresh.cpp). The measured shape (docs/guides/dlc.md §4.2): the scan finishes on
// the worker and the publish lands on the next frame, ~10 ms later; the file hooks alone
// were not enough, because the main menu makes no file call at all. A refresh that
// finishes while the menu is not drawing is published when it next is - the cache file is
// rewritten either way, which is the half the next boot reads.
void PublishPendingRefresh();

// Whether an in-game refresh can happen at all: the R7 toggle is on and Configure() has
// run. A hook checks this before it does any work - including reading the path a call
// names - so a run with the toggle off pays nothing.
bool RefreshArmed();

// True when this boot's library enumeration came out of the persisted cache rather than
// a scan, i.e. R7 was on and the fingerprint proved the tree unchanged. The menu filter
// reads it to pick the label the discovery screen draws: the title's own "Discovering
// Downloadable Content" when the walk is real, and a "loading cache" label when the host
// answered from the file it wrote last boot. False before Configure() and with R7 off.
bool CacheServedThisBoot();

// Registered flat and structured package count, for diagnostics. The progress
// model reads the actual guest enumerators, which also contain writable/user content.
std::size_t ContentItemCount();

// The host paths of the flat library's packages, in the walk order the scan produced
// them, as Configure() last left them. Empty with no library, before Configure(), or
// when the content manager was not reached. R9 reads it to pick the DLC songs the main
// menu plays instead of its own three tracks (src/hooks/shell_music.cpp); the list is
// the one the boot already walked, so asking for it costs nothing.
const std::vector<std::filesystem::path>& LibraryPackagePaths();

}  // namespace rb_blitz::dlc
