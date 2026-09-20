// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See src/fs/payload_overlay.h for why this device exists. The merge, backing and
// hide rules themselves are in src/fs/overlay_merge.h (SDK-free, host-tested); this
// file only turns them into SDK entries.

#include "fs/payload_overlay.h"

#include <string>
#include <utility>
#include <vector>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/string/utf8.h>

namespace rb_blitz::fs {
namespace {

using rex::filesystem::Entry;
using rex::filesystem::File;
using rex::filesystem::kFileAttributeDirectory;

// Mount paths of the two devices the union reads through. The base device is
// deliberately not the registered game root device: nothing in the VFS can hand that
// one back, so the union mounts its own and these names only label entries (they show
// up in Entry::absolute_path() and Dump()).
constexpr std::string_view kPayloadMount = "\\Device\\BlitzUltimate";
constexpr std::string_view kBaseMount = "\\Device\\Harddisk0\\Partition1";

bool IsDirectory(const Entry* entry) {
  return entry != nullptr && (entry->attributes() & kFileAttributeDirectory) != 0;
}

}  // namespace

PayloadOverlayDevice::PayloadOverlayDevice(std::string_view mount_path,
                                           const std::filesystem::path& payload_root,
                                           const std::filesystem::path& base_root,
                                           bool base_read_only,
                                           std::vector<std::string> hidden_paths)
    : Device(mount_path),
      payload_root_(payload_root),
      base_root_(base_root),
      hidden_(std::move(hidden_paths)),
      payload_device_(std::make_unique<rex::filesystem::HostPathDevice>(kPayloadMount, payload_root,
                                                                       /*read_only=*/true)),
      base_device_(std::make_unique<rex::filesystem::HostPathDevice>(kBaseMount, base_root,
                                                                    base_read_only)) {}

PayloadOverlayDevice::~PayloadOverlayDevice() = default;

bool PayloadOverlayDevice::Initialize() {
  if (!payload_device_->Initialize()) {
    REXLOG_ERROR("ultimate: overlay cannot mount the payload at {}", payload_root_.string());
    return false;
  }
  if (!base_device_->Initialize()) {
    REXLOG_ERROR("ultimate: overlay cannot mount the game root at {}", base_root_.string());
    return false;
  }
  Entry* payload_root = payload_device_->ResolvePath("");
  Entry* base_root = base_device_->ResolvePath("");
  if (payload_root == nullptr || base_root == nullptr) {
    REXLOG_ERROR("ultimate: overlay cannot reach the root of {} or {}",
                 payload_root_.string(), base_root_.string());
    return false;
  }
  root_entry_ = std::make_unique<PayloadOverlayEntry>(this, nullptr, "", payload_root, base_root);
  REXLOG_INFO("ultimate: overlay {} = {} + {} - {} payload-only record(s), {} game-root-only,"
              " {} payload copies preferred, {} merged director(ies), {} hidden",
              mount_path_, payload_root_.string(), base_root_.string(), stats_.payload_only,
              stats_.base_only, stats_.shadowed, stats_.merged_dirs, stats_.hidden);
  return true;
}

bool PayloadOverlayDevice::is_read_only() const { return base_device_->is_read_only(); }

void PayloadOverlayDevice::Dump(rex::string::StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  if (root_entry_ != nullptr) {
    root_entry_->Dump(string_buffer, 0);
  }
}

Entry* PayloadOverlayDevice::ResolvePath(std::string_view path) {
  if (root_entry_ == nullptr) {
    return nullptr;
  }
  if (path.empty()) {
    return root_entry_.get();
  }
  return root_entry_->ResolvePath(path);
}

const std::string& PayloadOverlayDevice::name() const { return base_device_->name(); }

uint32_t PayloadOverlayDevice::attributes() const { return base_device_->attributes(); }

uint32_t PayloadOverlayDevice::component_name_max_length() const {
  return base_device_->component_name_max_length();
}

uint32_t PayloadOverlayDevice::total_allocation_units() const {
  return base_device_->total_allocation_units();
}

uint32_t PayloadOverlayDevice::available_allocation_units() const {
  return base_device_->available_allocation_units();
}

uint32_t PayloadOverlayDevice::sectors_per_allocation_unit() const {
  return base_device_->sectors_per_allocation_unit();
}

uint32_t PayloadOverlayDevice::bytes_per_sector() const {
  return base_device_->bytes_per_sector();
}

bool PayloadOverlayDevice::IsHidden(std::string_view relative_path) const {
  return IsHiddenPath(hidden_, relative_path);
}

PayloadOverlayEntry::PayloadOverlayEntry(PayloadOverlayDevice* device, Entry* parent,
                                         std::string_view path, Entry* payload, Entry* base)
    : Entry(device, parent, path), payload_(payload), base_(base) {
  // Metadata follows the payload copy, so a shadowed file reports the size and
  // timestamps of the bytes that will actually be read.
  const Entry* source = payload_ != nullptr ? payload_ : base_;
  attributes_ = source->attributes();
  size_ = source->size();
  allocation_size_ = source->allocation_size();
  create_timestamp_ = source->create_timestamp();
  access_timestamp_ = source->access_timestamp();
  write_timestamp_ = source->write_timestamp();
  MergeChildren();
}

void PayloadOverlayEntry::MergeChildren() {
  if (!IsDirectory(this)) {
    return;
  }
  auto* device = static_cast<PayloadOverlayDevice*>(device_);

  std::vector<OverlayChild> payload_children;
  std::vector<OverlayChild> base_children;
  if (payload_ != nullptr) {
    for (const std::unique_ptr<Entry>& child : payload_->children()) {
      payload_children.push_back(PayloadChild(child->name(), IsDirectory(child.get())));
    }
  }
  if (base_ != nullptr) {
    for (const std::unique_ptr<Entry>& child : base_->children()) {
      base_children.push_back(BaseChild(child->name(), IsDirectory(child.get())));
    }
  }
  // A payload record that the hide list names stays out, and a hidden directory takes
  // its subtree with it because this is where its parent is merged.
  payload_children =
      VisiblePayloadChildren(payload_children, path_, device->hidden(), &device->stats_.hidden);

  for (const OverlayChild& child : MergeOverlayChildren(payload_children, base_children)) {
    Entry* payload = child.in_payload ? payload_->GetChild(child.name) : nullptr;
    Entry* base = child.in_base ? base_->GetChild(child.name) : nullptr;
    const std::string relative = rex::string::utf8_join_guest_paths(path_, child.name);
    if (payload != nullptr && base != nullptr) {
      if (IsDirectory(payload) && IsDirectory(base)) {
        ++device->stats_.merged_dirs;
      } else {
        ++device->stats_.shadowed;
        REXLOG_DEBUG("ultimate: overlay reads the payload copy of {}, the game root has one too",
                     relative);
      }
    } else if (payload != nullptr) {
      ++device->stats_.payload_only;
    } else if (base != nullptr) {
      ++device->stats_.base_only;
    } else {
      continue;
    }
    children_.push_back(std::make_unique<PayloadOverlayEntry>(device, this, relative, payload, base));
  }
}

OverlayChild PayloadOverlayEntry::Describe() const {
  OverlayChild self;
  self.name = name_;
  self.in_payload = payload_ != nullptr;
  self.in_base = base_ != nullptr;
  self.payload_is_dir = IsDirectory(payload_);
  self.base_is_dir = IsDirectory(base_);
  return self;
}

Entry* PayloadOverlayEntry::HandleBacking() const {
  switch (ReadBackingFor(Describe())) {
    case OverlayBacking::kPayload:
      return payload_;
    case OverlayBacking::kBase:
      return base_;
    case OverlayBacking::kNone:
      break;
  }
  return nullptr;
}

X_STATUS PayloadOverlayEntry::Open(uint32_t desired_access, File** out_file) {
  // Delegated rather than reimplemented: the backing entry owns the host handle, and
  // HostPathEntry::Open() is what turns a write on a read-only device into
  // X_STATUS_ACCESS_DENIED - the same answer vanilla gives for game/.
  Entry* backing = HandleBacking();
  if (backing == nullptr) {
    return X_STATUS_NO_SUCH_FILE;
  }
  return backing->Open(desired_access, out_file);
}

bool PayloadOverlayEntry::Truncate() {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->Truncate();
}

bool PayloadOverlayEntry::can_map() const {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->can_map();
}

std::unique_ptr<rex::memory::MappedMemory> PayloadOverlayEntry::OpenMapped(
    rex::memory::MappedMemory::Mode mode, size_t offset, size_t length) {
  Entry* backing = HandleBacking();
  if (backing == nullptr) {
    return nullptr;
  }
  return backing->OpenMapped(mode, offset, length);
}

void PayloadOverlayEntry::update() {
  Entry* backing = HandleBacking();
  if (backing == nullptr) {
    return;
  }
  // Callers refresh the union's entry, not the one behind it, so the metadata has to
  // be mirrored back.
  backing->update();
  size_ = backing->size();
  allocation_size_ = backing->allocation_size();
  create_timestamp_ = backing->create_timestamp();
  access_timestamp_ = backing->access_timestamp();
  write_timestamp_ = backing->write_timestamp();
}

bool PayloadOverlayEntry::SetAttributes(uint64_t attributes) {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->SetAttributes(attributes);
}

bool PayloadOverlayEntry::SetCreateTimestamp(uint64_t timestamp) {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->SetCreateTimestamp(timestamp);
}

bool PayloadOverlayEntry::SetAccessTimestamp(uint64_t timestamp) {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->SetAccessTimestamp(timestamp);
}

bool PayloadOverlayEntry::SetWriteTimestamp(uint64_t timestamp) {
  Entry* backing = HandleBacking();
  return backing != nullptr && backing->SetWriteTimestamp(timestamp);
}

std::unique_ptr<Entry> PayloadOverlayEntry::CreateEntryInternal(std::string_view name,
                                                               uint32_t attributes) {
  // The base-side CreateEntry() is the entry point on purpose: the protected
  // CreateEntryInternal() of another entry type cannot be called from here.
  if (WriteBackingFor(Describe()) != OverlayBacking::kBase) {
    return nullptr;
  }
  Entry* created = base_->CreateEntry(name, attributes);
  if (created == nullptr) {
    return nullptr;
  }
  return std::make_unique<PayloadOverlayEntry>(static_cast<PayloadOverlayDevice*>(device_), this,
                                               rex::string::utf8_join_guest_paths(path_, name),
                                               nullptr, created);
}

bool PayloadOverlayEntry::DeleteEntryInternal(Entry* entry) {
  auto* child = static_cast<PayloadOverlayEntry*>(entry);
  // A record only the payload has is not the game's to delete, and the payload is
  // read-only in any case.
  if (base_ == nullptr || child->base() == nullptr) {
    return false;
  }
  return base_->Delete(child->base());
}

X_STATUS PayloadOverlayEntry::RenameEntryInternal(
    const std::vector<std::string_view>& path_parts) {
  if (WriteBackingFor(Describe()) != OverlayBacking::kBase) {
    return X_STATUS_ACCESS_DENIED;
  }
  // Entry::Rename() drops the first part of the path it is handed before calling this,
  // so the game root entry has to get a path it drops the same way. The placeholder
  // stands in for the guest's drive alias and is never part of a name. A payload copy
  // of the record keeps its old name on disk: the payload is read-only, and this only
  // happens when the game root is writable (allow_game_relative_writes).
  std::vector<std::string_view> parts;
  parts.reserve(path_parts.size() + 1);
  parts.emplace_back("d:");
  parts.insert(parts.end(), path_parts.begin(), path_parts.end());
  return base_->Rename(rex::to_path(rex::string::utf8_join_guest_paths(parts)));
}

}  // namespace rb_blitz::fs
