// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "settings/ultimate_adapter.h"
#include "ui/dtb_author.h"

namespace rb_blitz::settings {
using RecordReader = std::function<bool(const std::string&, std::vector<uint8_t>*, std::string*)>;
using MenuRecords = std::map<std::string, std::vector<uint8_t>>;

// Read through the mounted archive view, so provider edits survive the extension.
bool BuildMenuRecords(const RecordReader& read, const std::vector<std::string>& locales,
                      MenuRecords* records, std::string* error, bool merge_provider = false,
                      bool cached_dlc_list = false);
// Applied to the complete generated locale record, before it is archived. This
// path has no split-read length constraint and serves both installed providers.
bool SetDiscoveryCacheLabel(std::vector<ui::dtb::Node>* roots, bool cache_hit);
// Extend an existing patch index with our records. Existing records retain their offsets,
// compression and checksums, and the ark keeps the part count it had: when the mounted index has a
// single part its bytes are carried into the new one (`mounted_part`) and only the last records
// move, which is what keeps the archive shaped like every archive this title reads. With no
// mounted part (the base game), our own part is appended instead. `part_path` receives the record
// path to write `part` to. No installed archive is modified on disk.
std::vector<uint8_t> ExtendMenuArchive(ultimate_adapter::ArkIndex index, const MenuRecords& records,
                                       const std::vector<uint8_t>& mounted_part,
                                       std::string* part_path, std::vector<uint8_t>* part);
}
