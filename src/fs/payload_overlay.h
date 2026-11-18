// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <rex/filesystem/file_system.h>
#include "fs/overlay_merge.h"

namespace rb_blitz::fs {

// Runs synchronously on filesystem callers; observers must not write settings or reenter mounts.
using StorePathObserver = void (*)(std::string_view path, bool open,
                                   uint32_t desired_access, int32_t status);
void SetStorePathObserver(StorePathObserver observer);

// Mounts the payload over the current base view, then moves game: and d: together.
// The SDK captures the base provider and owns all nodes and handles. Exclusions affect only
// payload records. Root-level redirects replace both read sides with player-owned files.
// Failure to mount player storage retains the read-only payload view and is logged.
bool MountPayloadOverlay(rex::filesystem::IFileSystem& filesystem, std::string_view mount,
    const std::filesystem::path& payload_root, std::string_view base_path,
    std::vector<std::string> hidden_paths = {},
    const std::filesystem::path& settings_root = {},
    std::vector<std::string> redirects = {}, std::string* error = nullptr);

}  // namespace rb_blitz::fs
