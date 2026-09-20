// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Rock Band Blitz Ultimate payload overlay (docs/ultimate-compat.md)
//
// The mod ships its content as files next to the game's own (game/ultimate/gen/...)
// and expects the title to read them *out of the same directory* as the base content:
// boot opens d:\gen\main_xbox.hdr, d:\gen\main_xbox_0.ark and d:\gen\patch_xbox* with
// no mod-specific prefix. Installing the payload next to an existing gen/ is therefore
// a merge of two directories, which the SDK virtual file system cannot express:
// VirtualFileSystem::OpenFile() resolves the *base* path of the file (d:\gen) to one
// entry and then asks that entry for the child name, so neither a device alias nor a
// per-file symbolic link gets a chance to redirect an individual file (see
// virtual_file_system.cpp, and out/modprobe/*.png for what that costs: the patch
// header resolves to 0xc000000f and the game boots vanilla).
//
// This file is the union the SDK does not have: a read-only device over the payload
// directory and the game root, where a record both sides have resolves to the payload
// copy and a record one side has resolves to that side. Writes stay on the game root,
// so the payload cannot be damaged by the game and deleting game/ultimate is a
// complete uninstall. The rules live in src/fs/overlay_merge.h, SDK-free, so
// tests/payload_overlay_tests.cpp can cover them on the host.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>

#include "fs/overlay_merge.h"

namespace rb_blitz::fs {

// The SDK's status type and its X_STATUS_* macros spell the name unqualified, and the
// overlay's overrides return the same codes the SDK returns.
using rex::X_STATUS;

// What the union found, for the boot log: the numbers that say whether the payload was
// picked up at all, and which records came from where.
struct OverlayStats {
  size_t payload_only = 0;
  size_t base_only = 0;
  size_t shadowed = 0;      // files both sides have: the payload copy is the one read
  size_t merged_dirs = 0;   // directories both sides have: their children are unioned
  size_t hidden = 0;        // payload records kept out of the union by the hide list
};

class PayloadOverlayDevice;
class PayloadOverlayEntry;

class PayloadOverlayDevice final : public rex::filesystem::Device {
 public:
  // `payload_root` is read-only by construction. `base_read_only` mirrors how the game
  // root device is mounted (Runtime::SetupVfs() passes the allow_game_relative_writes
  // cvar inverted), so the union can never be more writable than the game root itself.
  // `hidden_paths` are overlay-relative paths whose payload copies must not join the
  // union, compared case-insensitively with either separator.
  PayloadOverlayDevice(std::string_view mount_path, const std::filesystem::path& payload_root,
                       const std::filesystem::path& base_root, bool base_read_only,
                       std::vector<std::string> hidden_paths);
  ~PayloadOverlayDevice() override;

  bool Initialize() override;
  bool is_read_only() const override;
  void Dump(rex::string::StringBuffer* string_buffer) override;
  rex::filesystem::Entry* ResolvePath(std::string_view path) override;

  const std::string& name() const override;
  uint32_t attributes() const override;
  uint32_t component_name_max_length() const override;

  uint32_t total_allocation_units() const override;
  uint32_t available_allocation_units() const override;
  uint32_t sectors_per_allocation_unit() const override;
  uint32_t bytes_per_sector() const override;

  const std::filesystem::path& payload_root() const { return payload_root_; }
  const std::filesystem::path& base_root() const { return base_root_; }
  const std::vector<std::string>& hidden() const { return hidden_; }
  const OverlayStats& stats() const { return stats_; }

  bool IsHidden(std::string_view relative_path) const;

 private:
  friend class PayloadOverlayEntry;

  std::filesystem::path payload_root_;
  std::filesystem::path base_root_;
  std::vector<std::string> hidden_;
  OverlayStats stats_;
  std::unique_ptr<rex::filesystem::HostPathDevice> payload_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> base_device_;
  std::unique_ptr<rex::filesystem::Entry> root_entry_;
};

// One record of the union. payload_ and base_ point at the same name on either side, so
// a record only one side has leaves the other null. The union owns its own children
// because Entry::GetChild() - the only lookup VirtualFileSystem::OpenFile() performs for
// a file name - walks the parent entry's own child list, and nothing can add a second
// device's records to a HostPathEntry's vector.
class PayloadOverlayEntry final : public rex::filesystem::Entry {
 public:
  PayloadOverlayEntry(PayloadOverlayDevice* device, rex::filesystem::Entry* parent,
                      std::string_view path, rex::filesystem::Entry* payload,
                      rex::filesystem::Entry* base);

  rex::filesystem::Entry* payload() const { return payload_; }
  rex::filesystem::Entry* base() const { return base_; }

  X_STATUS Open(uint32_t desired_access, rex::filesystem::File** out_file) override;
  bool Truncate() override;
  bool can_map() const override;
  std::unique_ptr<rex::memory::MappedMemory> OpenMapped(rex::memory::MappedMemory::Mode mode,
                                                       size_t offset, size_t length) override;
  void update() override;
  bool SetAttributes(uint64_t attributes) override;
  bool SetCreateTimestamp(uint64_t timestamp) override;
  bool SetAccessTimestamp(uint64_t timestamp) override;
  bool SetWriteTimestamp(uint64_t timestamp) override;

 protected:
  std::unique_ptr<rex::filesystem::Entry> CreateEntryInternal(std::string_view name,
                                                              uint32_t attributes) override;
  bool DeleteEntryInternal(rex::filesystem::Entry* entry) override;
  X_STATUS RenameEntryInternal(const std::vector<std::string_view>& path_parts) override;

 private:
  // Handle-level work (open, map, truncate, timestamps) follows the read backing rule.
  rex::filesystem::Entry* HandleBacking() const;
  // This record's membership, in the shape the merge rules take.
  OverlayChild Describe() const;
  void MergeChildren();

  rex::filesystem::Entry* payload_ = nullptr;
  rex::filesystem::Entry* base_ = nullptr;
};

}  // namespace rb_blitz::fs
