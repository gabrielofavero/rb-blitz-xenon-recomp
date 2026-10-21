// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Reading a DLC package's song metadata, shared by the two features that need it: the R9 menu
// music pool (which maps a `.mogg` stream to the song manager's own key) and the store's
// Downloaded matcher (RhythmVerse store plan §3.2, which compares artist + title against the
// songs the boot has loaded).
//
// The mount helpers used to live in src/hooks/shell_music.cpp; they are here because a second
// caller needs exactly the same read, and two copies of "which paths may hold songs.dta" would
// drift.
//
// SDK-using (devices and the guest file layer). The parsing half is
// `shell_music::ReadSongMetadata`, which is SDK-free and tested on its own.

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <rex/filesystem/device.h>

#include "hooks/menu_music_metadata.h"

namespace rb_blitz::fs {

// The package as a read-only device: an STFS container mounted where it lies, or an already
// extracted package directory. Null when the SDK will not mount it.
std::unique_ptr<rex::filesystem::Device> MountPackage(const std::string& mount_path,
                                                      const std::filesystem::path& package,
                                                      std::error_code& ec);

// The songs a mounted package's metadata declares (`\songs\songs.dta`, `\songs\songs.dtb`,
// `\songs\gen\songs.dtb`, first that parses). Empty when the package carries none.
std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(rex::filesystem::Device& device);

}  // namespace rb_blitz::fs
