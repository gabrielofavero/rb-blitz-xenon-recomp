// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5 of the RhythmVerse store plan (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §2.9, §5/P5): where a validated download becomes a library package.
//
// The rules this file exists for:
//
//   * **Nothing partial is ever enumerable.** A transfer is written under
//     `<resolved_dlc_root>/.rbbz-store-staging/` - a name every scan skips
//     (`fs::IsStoreStagingEntryName`) - and only a file that passed the transfer's length,
//     prefix and STFS checks is renamed into the library.
//   * **The layout a root already has is preserved.** A structured root
//     (`<title_id>/<content_type>/...`) gets the package under `45410914/00000002/`; a flat
//     root gets it as a loose container. Creating a title-ID directory inside a flat root
//     would make `fs::HasStructuredLayout()` answer differently on the next boot and could
//     exclude the loose packages that were already there.
//   * **A name is short, generated and never overwritten.** The guest carries
//     `XCONTENT_DATA::file_name` in 42 bytes (`fs::kMaxContentFileName`), so the install
//     name is `rv_<32 hex>.con` (39 bytes) derived from the stable RhythmVerse file id -
//     never the untrusted title, artist, `Content-Disposition` or raw opaque id. A
//     collision gets `~<n>` rather than replacing the user's package.
//
// SDK-free (host path arithmetic and file IO), so `tests/store_download_tests.cpp` drives
// every rule against a temporary directory without a boot, a network or a game image.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace rb_blitz::store {

// A custom song package is RB3 content (title 45410914), whatever content type its own STFS
// header uses - dumped folders present saved-game packages and the library adapts them to
// marketplace content (src/fs/dlc_library.h, docs/guides/dlc.md §2.1). The title id is what the
// transfer validates; the content type is recorded, not policed.
inline constexpr std::uint32_t kRb3TitleId = 0x45410914;
inline constexpr std::uint32_t kMarketplaceContentType = 0x00000002;

// `rv_` + 32 hex + `.con`. The suffix a collision adds (`~1`, `~2`, ...) still fits 42 bytes.
inline constexpr std::size_t kInstallNameBytes = 39;
// XCONTENT_DATA::file_name_raw is 42 bytes; a longer name is truncated by the guest and then
// cannot be opened (fs::kMaxContentFileName).
inline constexpr std::size_t kMaxInstallFileNameBytes = 42;
inline constexpr std::size_t kMaxInstallAttempts = 100;

// Where the store installs and how the destination root is shaped. `destination_root` is the
// boot's resolved DLC root (src/hooks/dlc.cpp Configure), already absolute.
struct InstallLayout {
  std::filesystem::path destination_root;
  // True when the root holds `<title_id>/<content_type>/<package>`; false for a flat library.
  // An absent root is flat: the store creates it and drops a loose container, because creating
  // a title-ID level there would reclassify the folder on the next boot.
  bool structured = false;
};

// One planned transfer: the staging directory, the part file the bytes are written to and the
// directory + name the validated file is renamed to.
struct StagedInstall {
  std::filesystem::path staging_dir;
  std::filesystem::path part_path;
  std::filesystem::path final_dir;
  std::string stem;        // `rv_<32 hex>`; a collision suffix is added to this
  std::string final_name;  // the first candidate; CommitStagedInstall may add a suffix
};

// The stable 32-hex stem for a file id (the same id the catalogue returned). It is a hash, so
// no part of the untrusted id reaches the file system, and it is deterministic, so the same
// offer installs under the same name on every attempt.
std::string InstallStem(std::string_view file_id);

// True when `name` is one this module generates: `rv_<32 lowercase hex>.con` with an optional
// `~<n>` collision suffix, and never longer than 42 bytes.
bool IsValidInstallName(std::string_view name);

// Plans the transfer for `file_id`. An empty destination root is refused: the store must be
// told where the boot resolved the DLC destination, never guess one.
std::optional<StagedInstall> PlanInstall(const InstallLayout& layout, std::string_view file_id,
                                         std::string* error);

// The staging directory and its parent are created here. Failure is reported with the host's
// own error text, and a read-only or full volume is reported as such rather than as a generic
// "cannot create".
bool EnsureStagingDirectory(const StagedInstall& install, std::string* error);

// Removes the part file (safe when it is absent). Called on cancel, on a policy/validation
// failure and before a retry, so a failed transfer leaves no fragment behind.
void DiscardStagedFile(const StagedInstall& install);

// Renames the part file into `final_dir`, picking the first free `rv_<32 hex>[~n].con`. Never
// overwrites an existing package: `final_path` receives the name that was used. The rename is
// on the same volume (staging sits under the destination root), so it is atomic.
bool CommitStagedInstall(const StagedInstall& install, std::filesystem::path* final_path,
                         std::string* error);

// What a host file error means to the store's UI/state vocabulary.
enum class FileError {
  kNone,
  kDiskFull,
  kReadOnly,
  kNotFound,
  kOther,
};

FileError ClassifyFileError(const std::error_code& ec);

// The message a failed transfer shows: the store's own words, with the host's detail only
// where it adds information (a full or read-only volume names the destination).
std::string DescribeFileError(FileError error, std::string_view detail);

}  // namespace rb_blitz::store
