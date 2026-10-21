// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The store's Downloaded state (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md §3.2):
// an artist + title index of the songs this boot's DLC library actually declares, so an offer the
// player already has shows as Downloaded without a receipt and without owner-account state.
//
// Why it works this way:
//
//   * A receipt (`store/downloads.tsv`, P5) is transfer history, never ownership: deleting a song
//     or moving the library must stop the mark. So the authority is the packages themselves -
//     `songs/songs.dta` inside every DLC package the boot enumerated (`dlc::LibraryPackagePaths`),
//     the same read the R9 menu-music pool uses (src/fs/dlc_song_metadata.h). That covers
//     manually installed content and every song of a merged pack, which no filename or purchase
//     record can.
//   * Building it touches every package, so it happens on this module's own thread, started when
//     the store opens (the front menu is served ~14 s before a page is asked for). A page that is
//     asked for before the index is ready simply carries no Downloaded mark: unknown is not a
//     negative claim, and a false "Downloaded" would be the worse error.
//   * The comparison is the owner's deliberately simple one (plan §3.2): normalized artist and
//     title, both required, exact after normalization - no fuzzy matching, no punctuation or
//     version stripping. Alternate charts of one song therefore all match together, and a pack
//     keeps one offer per file.
//
// The matching half is SDK-free (`LoadedSongIndex`, `NormalizeSongField`) and tested by
// tests/store_library_tests.cpp; only the builder below needs the package reader.

#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace rb_blitz::store {

// Trimmed, interior whitespace collapsed to one space, ASCII-lowercased. Applied to both sides of
// every comparison, so a trailing space in songs.dta cannot hide a match. Unicode case folding is
// deliberately out of scope: the API and songs.dta carry the same spelling of the same title.
std::string NormalizeSongField(std::string_view text);

// The (artist, title) pairs a loaded library declares.
class LoadedSongIndex {
 public:
  // A pair with an empty artist or title is refused: the plan requires both, and an empty field
  // would match every offer that also lacks it.
  void Add(std::string_view artist, std::string_view title);
  bool Contains(std::string_view artist, std::string_view title) const;
  std::size_t size() const { return songs_.size(); }
  void Clear() { songs_.clear(); }

 private:
  std::set<std::pair<std::string, std::string>> songs_;
};

// Installs the Downloaded predicate the serving policy asks (`store_serve.h`) and starts the
// builder thread. Called once per boot on the host thread, beside the other store installations.
void InstallStoreLibrary();

// Asks for the index to be (re)built. Never blocks: one build at a time, and a build already in
// flight is not queued twice (single flight, like the catalogue). A no-op before
// InstallStoreLibrary.
void RequestLibraryRefresh(std::string_view why);

// Drops the published index so the next request rebuilds it. Called when the library changed
// under us: the refresh row's scan, or a verified store install.
void InvalidateLibrary();

// True when a build has completed at least once since the last invalidation.
bool LibraryReady();

// How many (artist, title) pairs the published index holds. Diagnostics.
std::size_t LoadedSongCount();

// The generation the published index carries: 0 before the first build, +1 per publish. The
// serving policy does not cache across pages, but a later matcher can.
std::uint64_t LibraryGeneration();

// True when the loaded library declares this artist + title. False while the index is still being
// built (unknown, not "not downloaded").
bool IsDownloadedSong(std::string_view artist, std::string_view title);

}  // namespace rb_blitz::store
