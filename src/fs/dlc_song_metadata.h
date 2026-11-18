// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <rex/filesystem/file_system.h>
#include <rex/filesystem/mount_validation.h>
#include "hooks/menu_music_metadata.h"

namespace rb_blitz::fs {

/** A read-only package mounted in an isolated SDK filesystem. The descriptor can
 * be mounted in the title filesystem after host discovery; publication validates
 * that new provider independently before guest access.
 */
class PackageView {
 public:
  PackageView(std::unique_ptr<rex::filesystem::IFileSystem> files,
              rex::filesystem::MountEntry mount,
              std::shared_ptr<std::vector<rex::filesystem::MountValidationNode>> nodes)
      : files_(std::move(files)), mount_(std::move(mount)), nodes_(std::move(nodes)) {}
  rex::filesystem::IFileSystem& Files() const { return *files_; }
  const rex::filesystem::MountEntry& Mount() const { return mount_; }
  std::span<const rex::filesystem::MountValidationNode> Nodes() const { return *nodes_; }
  std::string Path(std::string_view relative) const;
 private:
  std::unique_ptr<rex::filesystem::IFileSystem> files_;
  rex::filesystem::MountEntry mount_;
  std::shared_ptr<std::vector<rex::filesystem::MountValidationNode>> nodes_;
};

std::unique_ptr<PackageView> MountPackage(const std::string& mount_path,
                                         const std::filesystem::path& package,
                                         std::error_code& ec);

std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(PackageView& package);

/** Checks every exact metadata node, including duplicate and case-variant names. */
bool IsSafeSongContent(const rex::filesystem::IMountValidationView& view, std::string* reason);

}  // namespace rb_blitz::fs
