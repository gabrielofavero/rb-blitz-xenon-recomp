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
//   * **A name is the song's own and never overwritten.** The guest carries
//     `XCONTENT_DATA::file_name` in 42 bytes (`fs::kMaxContentFileName`), so the install name
//     is the catalogue's "Artist - Song", made safe for a host name *and* for that field:
//     every character Windows refuses and **every `.`** removed (a dot would read as an
//     extension, and a package has none), whitespace runs collapsed, the ends trimmed,
//     truncated to 42 UTF-8 bytes at a code-point boundary, and a Windows device name (`CON`,
//     `NUL`, `LPT1`, ...) suffixed with `_`. A collision gets `~<n>` rather than replacing the
//     user's package. An offer with no usable name falls back to `rv_<32 hex>` of its opaque
//     file id; no `Content-Disposition` name, raw API id or untrusted path ever reaches the
//     library without passing through that sanitizer.
//
// SDK-free (host path arithmetic and file IO), so `tests/store_download_tests.cpp` drives
// every rule against a temporary directory without a boot, a network or a game image.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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

// XCONTENT_DATA::file_name_raw is 42 bytes; a longer name is truncated by the guest and then
// cannot be opened (fs::kMaxContentFileName). It is a *byte* budget, so a multi-byte name is cut
// at a UTF-8 code-point boundary and the result is never longer than this.
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
  std::string stem;        // `rv_<32 hex>`: the staged part file's name, never the package's
  std::string final_name;  // the sanitized "Artist - Song"; a collision adds `~<n>` to it
};

// The stable 32-hex stem for a file id (the same id the catalogue returned). It is a hash, so
// no part of the untrusted id reaches the file system, and it is deterministic, so the same
// offer stages under the same part name on every attempt.
std::string InstallStem(std::string_view file_id);

// The name an installed package gets for a display name (the catalogue's "Artist - Song"):
// every character a host name cannot hold (`< > : " / \ | ? *`, the control range) and every
// `.` removed, whitespace runs collapsed to one space, both ends trimmed, the result truncated to
// kMaxInstallFileNameBytes at a UTF-8 code-point boundary, and a reserved Windows device name
// (`CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`) suffixed with `_`. Empty when the
// display name holds nothing usable - the caller then installs under InstallStem's hash.
std::string InstallName(std::string_view display_name);

// True when `name` is one this module can produce: non-empty, at most 42 bytes, no `.` (a package
// has no extension), no character Windows reserves and no control character, no trailing space,
// and not a device name.
bool IsValidInstallName(std::string_view name);

// Plans the transfer for `file_id`, whose package is installed under InstallName(display_name).
// An empty destination root or an empty file id is refused: the store must be told where the boot
// resolved the DLC destination and which offer it is installing, never guess either.
std::optional<StagedInstall> PlanInstall(const InstallLayout& layout, std::string_view file_id,
                                         std::string_view display_name, std::string* error);

// The staging directory and its parent are created here. Failure is reported with the host's
// own error text, and a read-only or full volume is reported as such rather than as a generic
// "cannot create".
bool EnsureStagingDirectory(const StagedInstall& install, std::string* error);

// Removes the part file (safe when it is absent). Called on cancel, on a policy/validation
// failure and before a retry, so a failed transfer leaves no fragment behind.
void DiscardStagedFile(const StagedInstall& install);

// Renames the part file into `final_dir`, picking the first free `<Artist - Song>[~n]`. Never
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
