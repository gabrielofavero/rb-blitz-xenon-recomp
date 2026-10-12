// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "settings/ultimate_adapter.h"

namespace rb_blitz::settings {
using RecordReader = std::function<bool(const std::string&, std::vector<uint8_t>*, std::string*)>;
using MenuRecords = std::map<std::string, std::vector<uint8_t>>;

// Read through the mounted archive view, so provider edits survive the extension.
bool BuildMenuRecords(const RecordReader& read, const std::vector<std::string>& locales,
                      MenuRecords* records, std::string* error, bool merge_provider = false);
// Extend an existing patch index with an additional part. Existing parts and untouched
// records retain their offsets, compression and checksums; no original archive is copied.
std::vector<uint8_t> ExtendMenuArchive(ultimate_adapter::ArkIndex index, const MenuRecords& records,
                                     std::vector<uint8_t>* part);
}
