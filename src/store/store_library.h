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
//   * Building it touches every package, so it happens on a worker when the store opens. The details page
//     queries the live index and shows Checking installed songs until that first pass finishes.
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
#include <filesystem>
#include <map>
#include <vector>

namespace rb_blitz::store {

// Trimmed, whitespace-collapsed and Unicode-case-folded using FoldForMatch. Applied to both
// sides, including Latin-1 metadata, so case, encoding and trailing spaces cannot hide a match.
std::string NormalizeSongField(std::string_view text);

// The (artist, title) pairs a loaded library declares.
class LoadedSongIndex {
 public:
  // A pair with an empty artist or title is refused: the plan requires both, and an empty field
  // would match every offer that also lacks it.
  void Add(std::string_view artist, std::string_view title,
           const std::filesystem::path& package = {});
  bool Contains(std::string_view artist, std::string_view title) const;
  std::vector<std::filesystem::path> Packages(std::string_view artist, std::string_view title) const;
  void RemovePackage(const std::filesystem::path& package);
  std::size_t size() const { return songs_.size(); }
  void Clear() { songs_.clear(); }

 private:
  std::map<std::pair<std::string, std::string>, std::set<std::filesystem::path>> songs_;
};

// Installs the Downloaded predicate the serving policy asks (`store_serve.h`) and starts the
// builder thread. Called once per boot on the host thread, beside the other store installations.
void InstallStoreLibrary(const std::filesystem::path& user_data_root = {});

// Asks for the index to be (re)built. Never blocks: one build at a time, and a build already in
// flight is not queued twice (single flight, like the catalogue). A no-op before
// InstallStoreLibrary.
void RequestLibraryRefresh(std::string_view why);

// Drops the published index and schedules a rebuild. Called when the library changed
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
std::vector<std::filesystem::path> InstalledSongPackages(std::string_view artist,
                                                       std::string_view title);
void ForgetInstalledPackage(const std::filesystem::path& package);

}  // namespace rb_blitz::store
