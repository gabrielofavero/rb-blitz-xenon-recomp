// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R9: change the main menu's background songs for loaded DLC ones.
//
// What the title does by itself (recovered from the image; see
// docs/engine/menu-music.md for the addresses and the evidence):
//
//   * the main menu's background music is `ShellMusicPanel`, Blitz's rename of the
//     RB3 engine's MetaMusic. It reads the `shellmusic` block of the synth config -
//     `config/synth.dtb` in the ark, `(shellmusic (music (samples/shellmusic_1
//     (gain 0.0)) (samples/shellmusic_2 ...) (samples/shellmusic_3 ...)))` - and
//     holds those three entries as its whole pool.
//   * when a track ends (0x822B7968) it picks one entry at random, formats
//     `sprintf("sfx/shell/%s", entry)`, reads that entry's `gain`, makes a
//     `ShellMusic` and calls `ShellMusic::Load(path, gain, loop, play_from_buffer)`
//     (0x82757F50). `Load` streams the path; the three entries resolve to
//     `sfx/shell/samples/shellmusic_1..3.mogg`, three stereo tracks in the ark.
//
// So "random song, then the next" is already the title's own behaviour - over three
// shipped tracks. R9 widens the pool: the hook below replaces the path of every pool
// entry with one of the DLC songs this boot loaded, which the title streams exactly
// the same way.
//
// Where a DLC song comes from: the title opens its downloadable songs from a mounted
// content device (`cntXXXXXXXX:\songs\<key>\<key>.<ext>`) and never from `sfx/shell/`,
// so a path in the synth config cannot name one. This layer therefore mounts a few of
// the packages rb_blitz::dlc::Configure() enumerated itself, read-only, as devices of
// its own (menumusic0:, menumusic1:, ...), and hands ShellMusic a path into one of
// them. Nothing is copied, extracted or written under game/.
//
// Hook hygiene (docs/rb3-references.md §8): the faithful behaviour, and the deviation.
//
// Faithful behaviour: with R9 off - its default - Configure() mounts nothing and the
// hook is inert, so the boot is the one this project had before R9 existed: the menu
// loops the title's own three tracks. The same is true with R9 on and no DLC library,
// or when none of the chosen packages can be mounted or holds a stream: nothing joins
// the pool, so the hook has nothing to substitute and every read is the guest's own.
//
// Deviation: with R9 on and at least one package mounted, each pool entry's path is
// replaced, at the load that carries it, with `<alias>:\songs\<song>\<song>` - a
// different song of the same kind (a streamed .mogg), played with the same gain, loop
// and FX the panel already chose. Every substitution is logged with the path the title
// asked for and the one it is given instead.

#include "hooks/shell_music.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem/device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/dlc.h"

#if defined(_WIN32)

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
REXCVAR_DECLARE(bool, enhancements_menu_dlc_songs);
REXCVAR_DECLARE(int32_t, enhancements_menu_dlc_song_count);

REX_EXTERN(__imp__sub_82757F50);

namespace {

// The instance `ShellMusic::Load` is hooked at (0x82757F50), and the prefix the panel
// builds every pool entry's path with (`sprintf("sfx/shell/%s", ...)`).
constexpr char kShellMusicPrefix[] = "sfx/shell/";
constexpr size_t kShellMusicPrefixSize = sizeof(kShellMusicPrefix) - 1;

// The pool is a handful of songs, so the cap is about how many containers it is
// reasonable to hold open at once, not about the menu: each mounted package is one
// file handle and its own block-hash cache.
constexpr int kMaxSongs = 64;

// One guest string buffer per substitution, cycled. Several are kept because the
// engine may still be holding the previous path when the next pool entry arrives: the
// panel asks for the next track as the current one ends.
constexpr size_t kBufferCount = 8;
constexpr size_t kBufferBytes = 512;

struct PoolEntry {
  std::string alias;   // the guest device name, without its trailing ':'
  std::string stream;  // `\songs\<song>\<song>`, without the extension Load appends
};

bool g_enabled = false;
std::vector<PoolEntry> g_pool;

std::mutex g_mutex;
std::mt19937 g_rng{std::random_device{}()};
constexpr size_t kNoIndex = static_cast<size_t>(-1);
size_t g_last_index = kNoIndex;

std::vector<uint32_t> g_buffers;
size_t g_buffer_next = 0;

// The stream path a package holds, or empty. A package's song files sit in a folder
// named after the song, beside the chart; `songs/<song>/<song>.mogg` is the shape the
// title opens them by, so the first `.mogg` in the first song folder is the one to
// play. Anything else - an art-only package, a damaged container - yields nothing and
// the package is skipped rather than guessed at.
std::string FindSongStream(rex::filesystem::Device& device) {
  rex::filesystem::Entry* songs = device.ResolvePath("\\songs");
  if (songs == nullptr) {
    songs = device.ResolvePath("songs");
  }
  if (songs == nullptr) {
    return {};
  }
  constexpr size_t kMoggSize = 5;  // ".mogg"
  for (const std::unique_ptr<rex::filesystem::Entry>& folder : songs->children()) {
    if (folder == nullptr || folder->name().empty()) {
      continue;
    }
    const std::string root = "\\songs\\" + folder->name();
    rex::filesystem::Entry* song = device.ResolvePath(root);
    if (song == nullptr) {
      continue;
    }
    for (const std::unique_ptr<rex::filesystem::Entry>& file : song->children()) {
      if (file == nullptr) {
        continue;
      }
      const std::string& name = file->name();
      if (name.size() > kMoggSize &&
          name.compare(name.size() - kMoggSize, kMoggSize, ".mogg") == 0) {
        return root + "\\" + name.substr(0, name.size() - kMoggSize);
      }
    }
  }
  return {};
}

// The package as a read-only guest device: an STFS container mounted where it lies, or
// an already-extracted package directory. Null when the SDK will not mount it.
std::unique_ptr<rex::filesystem::Device> MountPackage(const std::string& mount_path,
                                                      const std::filesystem::path& package,
                                                      std::error_code& ec) {
  if (std::filesystem::is_directory(package, ec)) {
    auto device =
        std::make_unique<rex::filesystem::HostPathDevice>(mount_path, package, /*read_only=*/true);
    if (!device->Initialize()) {
      return nullptr;
    }
    return device;
  }
  auto device = std::make_unique<rex::filesystem::StfsContainerDevice>(mount_path, package);
  if (!device->Initialize()) {
    return nullptr;
  }
  return device;
}

// The next song to play: random among the pool, never the one just played, so a short
// pool does not sound like a short pool. The caller holds g_mutex.
std::string NextPathLocked() {
  if (g_pool.empty()) {
    return {};
  }
  size_t index = 0;
  if (g_pool.size() > 1) {
    std::uniform_int_distribution<size_t> dist(0, g_pool.size() - 1);
    index = dist(g_rng);
    if (index == g_last_index) {
      index = (index + 1) % g_pool.size();
    }
  }
  g_last_index = index;
  return g_pool[index].alias + ":" + g_pool[index].stream;
}

// The path the title is about to stream is one of its own three pool entries; give it
// one of the loaded DLC songs instead. Anything else - a path that is not the shell
// music pool's - is left exactly as it is.
void SubstituteFilename(uint8_t* base, PPCContext& ctx) {
  if (!g_enabled || g_pool.empty() || g_buffers.empty()) {
    return;
  }
  const uint32_t filename = ctx.r4.u32;
  if (filename == 0) {
    return;
  }
  const char* asked = rex::memory::GuestPtr<const char*>(base, filename);
  if (asked == nullptr || std::strncmp(asked, kShellMusicPrefix, kShellMusicPrefixSize) != 0) {
    return;
  }

  std::string path;
  uint32_t buffer = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    path = NextPathLocked();
    buffer = g_buffers[g_buffer_next];
    g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
  }
  if (path.empty() || path.size() + 1 > kBufferBytes) {
    return;
  }
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, buffer), path.c_str(), path.size() + 1);
  ctx.r4.u32 = buffer;
  REXLOG_INFO("shell_music: the main menu asked for \"{}\"; playing \"{}.mogg\" instead (R9)",
              asked, path);
}

}  // namespace

namespace rb_blitz::shell_music {

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  (void)game_data_root;
  g_enabled = REXCVAR_GET(enhancements_menu_dlc_songs);
  if (!g_enabled) {
    REXLOG_INFO("shell_music: R9 off; the main menu loops the title's own tracks");
    return;
  }
  if (runtime == nullptr) {
    REXLOG_WARN("shell_music: R9 is on but there is no runtime to mount DLC with");
    g_enabled = false;
    return;
  }
  rex::filesystem::VirtualFileSystem* file_system = runtime->file_system();
  rex::memory::Memory* memory = runtime->memory();
  if (file_system == nullptr || memory == nullptr) {
    REXLOG_WARN("shell_music: R9 is on but the file system or memory is not up yet");
    g_enabled = false;
    return;
  }

  const std::vector<std::filesystem::path>& packages = rb_blitz::dlc::LibraryPackagePaths();
  if (packages.empty()) {
    REXLOG_WARN("shell_music: R9 is on but no DLC package was enumerated; the main menu keeps "
                "the title's own tracks");
    g_enabled = false;
    return;
  }

  int wanted = REXCVAR_GET(enhancements_menu_dlc_song_count);
  wanted = std::clamp(wanted, 1, kMaxSongs);
  const size_t total = packages.size();
  const size_t cap = std::min<size_t>(static_cast<size_t>(wanted), total);
  // Spread the picks over the whole library: the first N packages of a dumped folder
  // are neighbours, and a menu that only ever plays one album is not the feature.
  const size_t stride = std::max<size_t>(1, total / cap);

  for (size_t offset = 0; offset < total && g_pool.size() < cap; offset += stride) {
    const std::filesystem::path& package = packages[offset];
    const std::string mount_path =
        fmt::format("\\Device\\BlitzMenuMusic\\{}\\", g_pool.size());
    std::error_code ec;
    std::unique_ptr<rex::filesystem::Device> device = MountPackage(mount_path, package, ec);
    if (device == nullptr) {
      REXLOG_WARN("shell_music: cannot mount {} as DLC menu music ({})", package.string(),
                  ec ? ec.message() : "not a mountable package");
      continue;
    }
    const std::string stream = FindSongStream(*device);
    if (stream.empty()) {
      REXLOG_WARN("shell_music: {} holds no streamable .mogg, skipping it", package.string());
      continue;
    }
    const std::string alias = fmt::format("menumusic{}", g_pool.size());
    if (!file_system->RegisterDevice(std::move(device))) {
      REXLOG_WARN("shell_music: cannot register the device for {}", package.string());
      continue;
    }
    if (!file_system->RegisterSymbolicLink(alias + ":", mount_path)) {
      REXLOG_WARN("shell_music: cannot point {}: at {}", alias, mount_path);
      continue;
    }
    g_pool.push_back({alias, stream});
  }

  if (g_pool.empty()) {
    REXLOG_WARN("shell_music: no DLC song could be mounted; the main menu keeps the title's own "
                "tracks");
    g_enabled = false;
    return;
  }

  g_buffers.reserve(kBufferCount);
  for (size_t i = 0; i < kBufferCount; ++i) {
    const uint32_t address = memory->SystemHeapAlloc(static_cast<uint32_t>(kBufferBytes), 0x20);
    if (address == 0) {
      break;
    }
    g_buffers.push_back(address);
  }
  if (g_buffers.empty()) {
    REXLOG_WARN("shell_music: no guest memory for a stream path; R9 falls back to the title's own "
                "tracks");
    g_pool.clear();
    g_enabled = false;
    return;
  }

  REXLOG_INFO("shell_music: R9 plays the loaded DLC in the main menu: {} song(s) of {} package(s) "
              "mounted read-only ({} requested)",
              g_pool.size(), total, wanted);
  for (const PoolEntry& entry : g_pool) {
    REXLOG_INFO("shell_music:   {}:{}.mogg", entry.alias, entry.stream);
  }
}

}  // namespace rb_blitz::shell_music

// 0x82757F50 (ShellMusic::Load): this=ctx.r3, filename=ctx.r4, volume=f1, loop=r6,
// play_from_buffer=r7. Only the panel's own pool entries reach here, so the prefix
// check is all the discrimination the substitution needs.
extern "C" REX_FUNC(sub_82757F50) {
  SubstituteFilename(base, ctx);
  __imp__sub_82757F50(ctx, base);
}

#endif  // _WIN32
