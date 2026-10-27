// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Library menu music uses the title's ShellMusic player with file streaming and native
// looping disabled. At EOF, retire the active Stream, load the next source and Start it.
// Load alone does not replace a playing Stream: that was the cause of the apparent
// rotation logging different names while the same audio kept playing.
//
// A session playlist keeps stable song identities and history while a small rolling
// cache mounts nearby packages on demand. Play Current Song publishes only a source producing
// samples, and menu exit clears it without starting another track.
//
// Guest ABI and runtime evidence: docs/engine/main-menu.md, part 2, 2026-10-15 correction.
// With DLC Menu Songs off, the independent navigator can use the three title tracks.

#include "hooks/shell_music.h"

#include "hooks/menu_music_pool.h"
#include "hooks/menu_music_playback.h"
#include "hooks/menu_music_metadata.h"
#include "hooks/menu_music_queue.h"
#include "hooks/menu_music_session.h"
#include "hooks/menu_music_catalog.h"
#include "hooks/menu_music_cache.h"
#include "hooks/menu_music_now_playing.h"
#include "input/native_menu.h"
#include "input/input_capture.h"
#include <rex/kernel/xam/module.h>
#include "hooks/menu_audio_policy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
#include <rex/filesystem/file.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/types.h>

#include "hooks/dlc.h"
#include "hooks/content_progress.h"
#include "fs/dlc_song_metadata.h"
#include "settings/menu_compat.h"
#include "settings/boot.h"
#include "settings/ultimate_adapter.h"
#include "graphics/preparation_loading.h"

#if defined(_WIN32)

#include <windows.h>

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table. Both are read here: the
// first is the carrier of the in-game "DLC Menu Songs" row (the boot writes the row into it at the
// config rank), and the second is the retired pool-size row, read only to say it is ignored.
REXCVAR_DECLARE(bool, enhancements_menu_dlc_songs);
REXCVAR_DECLARE(bool, enhancements_background_music_nav);
REXCVAR_DECLARE(bool, enhancements_music_player);
REXCVAR_DEFINE_STRING(background_music_nav_probe, "", "Diagnostics",
    "One main-menu command after background_music_nav_probe_delay seconds: next, previous, play_pause.");
REXCVAR_DEFINE_INT32(background_music_nav_probe_delay, 5, "Diagnostics", "Seconds before the one-shot music navigator probe.");
REXCVAR_DEFINE_BOOL(background_music_nav_pause_trial, false, "Diagnostics",
    "Legacy pause-trial flag: log paused sample progress each second. Native pause no longer requires this flag.");
REXCVAR_DECLARE(int32_t, enhancements_menu_dlc_song_count);

REX_EXTERN(__imp__sub_82757F50);
REX_EXTERN(__imp__sub_827586D8);
REX_EXTERN(__imp__sub_82758B98);  // ShellMusic::Start
REX_EXTERN(__imp__sub_827583F8);  // ShellMusic::StopStreams
REX_EXTERN(__imp__sub_82758998);  // ShellMusic::Stop (menu exit)
REX_EXTERN(__imp__sub_82758490);  // ShellMusic destructor
REX_EXTERN(__imp__sub_823E5BE0);  // StandardStream constructor, f2 = buffer seconds
REX_EXTERN(__imp__sub_82757D40);  // ShellMusic random start-offset selector
REX_EXTERN(sub_823E3FD8);         // StandardStream::Stop: pause channels, state 3 -> 5
REX_EXTERN(__imp__sub_823E4808);  // StandardStream::Play: resume channels, state 5 -> 3

namespace {

// Keep the actual context, including scratch contexts used during a handoff,
// available to the first-chance logger. This observes faults; it never handles
// one or attempts to continue a damaged stream.
struct MusicFaultScope {
  static inline thread_local MusicFaultScope* current = nullptr;
  PPCContext& context;
  uint8_t* base;
  const char* stage;
  MusicFaultScope* previous;
  MusicFaultScope(PPCContext& ctx, uint8_t* memory, const char* name)
      : context(ctx), base(memory), stage(name), previous(current) { current = this; }
  ~MusicFaultScope() { current = previous; }
};

std::atomic<uintptr_t> g_music_fault_base{0};

LONG CALLBACK LogMusicFault(EXCEPTION_POINTERS* info) {
  const auto* scope = MusicFaultScope::current;
  if (!info || !info->ExceptionRecord ||
      info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      info->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
  const auto address = uintptr_t(info->ExceptionRecord->ExceptionInformation[1]);
  const auto base = scope ? reinterpret_cast<uintptr_t>(scope->base)
                          : g_music_fault_base.load(std::memory_order_relaxed);
  if (!base || address < base || address - base >= 0x100000) return EXCEPTION_CONTINUE_SEARCH;
  static std::atomic<unsigned> faults{0};
  if (faults.fetch_add(1, std::memory_order_relaxed) >= 8) return EXCEPTION_CONTINUE_SEARCH;
  REXLOG_ERROR("music_nav: fault stage={} guest={:08X} rip={:X} module={:X}",
               scope ? scope->stage : "outside ShellMusic", uint32_t(address - base),
               reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress),
               reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
  if (scope) {
    const auto& ctx = scope->context;
    REXLOG_ERROR("music_nav: fault lr={:08X} sp={:08X} r3={:08X} r4={:08X} "
                 "r11={:08X} r30={:08X} r31={:08X}",
                 uint32_t(ctx.lr), ctx.r1.u32, ctx.r3.u32, ctx.r4.u32,
                 ctx.r11.u32, ctx.r30.u32, ctx.r31.u32);
  }
  void* frames[24]{};
  const auto count = RtlCaptureStackBackTrace(1, 24, frames, nullptr);
  for (USHORT i = 0; i < count; ++i)
    REXLOG_ERROR("music_nav: fault frame {} {:X}", i, reinterpret_cast<uintptr_t>(frames[i]));
  return EXCEPTION_CONTINUE_SEARCH;
}

void CallMusicGuest(void (*function)(PPCContext&, uint8_t*), PPCContext& ctx,
                    uint8_t* base, const char* stage) {
  const MusicFaultScope scope(ctx, base, stage);
  function(ctx, base);
}

// The pool's bookkeeping lives in a header of its own so it can be tested without the SDK
// (tests/menu_music_pool_tests.cpp): the sample and the rotation are host logic, not hooks.
// (BuildPool and Rotation are named in full from the namespace below.)
using rb_blitz::shell_music::PoolEntry;

// The instance `ShellMusic::Load` is hooked at (0x82757F50), and the prefix the panel
// builds every pool entry's path with (`sprintf("sfx/shell/%s", ...)`).
constexpr char kShellMusicPrefix[] = "sfx/shell/";
constexpr size_t kShellMusicPrefixSize = sizeof(kShellMusicPrefix) - 1;

// One guest string buffer per substitution, cycled. Several are kept because the
// engine may still be holding the previous path when the next pool entry arrives: the
// panel asks for the next track as the current one ends.
constexpr size_t kBufferCount = 8;
constexpr size_t kBufferBytes = 512;

// Verified in the retail image: ShellMusic+44 owns StandardStream, whose +0xA0
// counts source samples. EOF is StandardStream state 6, not a guessed duration.
constexpr uint32_t kStreamOffset = 44;
constexpr uint32_t kPositionOffset = 0xA0;
constexpr double kSamplesPerSecond = 44100.0;  // diagnostic display for these mixdowns

bool g_enabled = false;
bool g_navigation_enabled = false;
bool g_nav_pause_trace = false;
uint32_t g_nav_paused_stream = 0;
uint32_t g_nav_pause_position = 0;
std::chrono::steady_clock::time_point g_nav_pause_log_at{};
namespace nav = rb_blitz::input::menu_music_nav;
std::optional<nav::Request> g_nav_request;
size_t g_nav_target = rb_blitz::shell_music::kNoIndex;
bool g_nav_started = false;
int g_nav_title_target = -1;
int g_title_index = -1;
uint64_t g_nav_epoch = 0;
std::string g_nav_probe;
int32_t g_nav_probe_delay = 5;
std::chrono::steady_clock::time_point g_nav_menu_since{};
rb_blitz::shell_music::MusicSession g_session;
size_t g_stream_song = rb_blitz::shell_music::kNoIndex;

// The setting's own value, as read in Configure(): true when `general.dlc_menu_songs` (the revived
// Background Music row) asked for the pool this boot. `g_enabled` is the stricter fact - the
// setting and a usable pool - and the two differ when neither the base archive nor any DLC
// package provides a readable song. The settings screen reads
// both to say which of the two states a boot is in.
bool g_menu_music_enabled = false;

// Audible and loading indices are separate. PlaySelection snapshots a press and bridges the
// short handoff without claiming a loading stream is audible. Never read outside g_mutex.
int g_current_index = -1;
int g_pending_index = -1;
rb_blitz::shell_music::PlaySelection g_play_selection;

std::mutex g_mutex;
std::mt19937 MakeRandom() {
  std::random_device entropy;
  std::array<uint32_t, 16> words;
  for (auto& word : words) word = entropy();
  std::seed_seq seed(words.begin(), words.end());
  return std::mt19937(seed);
}
std::mt19937 g_rng = MakeRandom();

// Playback bounds startup and stalled progress separately. Limit consecutive attempts before
// handing the menu back to the title's tracks.
constexpr int kMaxAttemptsPerSeam = 3;
rb_blitz::shell_music::Playback g_playback;
rb_blitz::shell_music::PlaybackAge g_nav_age;
thread_local bool g_nav_start_at_zero = false;

// True while the track playing on `g_music` is one of R9's own. Once the rotation hands the menu a
// title track instead - a pool of one song, or every candidate failing - the poll stops watching,
// because from that point on the title is playing its own looping music.
bool g_watch = false;
int g_attempts = 0;

// True while the track playing on `g_music` is a title fallback (the pool had nothing
// distinct left). The poll reports it - heartbeats, and the loop it takes when the title
// track wraps - but never rotates: the title's own music is the title's own business.
bool g_watch_title = false;
// A sole startup song may end before the background queue begins. A title
// fallback fills the gap, then gives the grown DLC pool its next loop boundary.
bool g_waiting_for_pool = false;

// True while the host or ShellMusic::Start is assembling a track. Both call StopStreams as
// cleanup before the new stream exists (Start does it inside its own body), and that cleanup
// must not be read as the game tearing the menu music down. Never read outside the guest thread.
thread_local bool g_setting_up_music = false;

// The ShellMusic the last substitution went to, and the Load arguments the panel passed
// it - the poll hook replays them when it starts the next track. Zero outside R9.
uint32_t g_music = 0;
double g_gain = 0.0;
uint64_t g_scale_pan = 1;

// The stream R9 is watching, and the last position read from it, so a jump backwards can
// be told from the first sample of a track. Reset with every track R9 starts.
uint32_t g_pos = 0;

std::vector<uint32_t> g_buffers;
size_t g_buffer_next = 0;

// Timeline: native EOF to the next stream's advancing samples. This is a playback
// proxy, not an audio-device latency measurement. Source identity is logged at startup.
std::chrono::steady_clock::time_point g_track_started_at{};
std::chrono::steady_clock::time_point g_seam_at{};
std::string g_playing_name;      // the song key of the track R9 last handed the menu

// How often the playing track's position is reported while it plays. The stream's own position is
// the only clock the guest offers, so a line that shows it advancing is what separates "the menu
// is playing this song" from "the menu stopped playing anything".
constexpr int64_t kHeartbeatMs = 60000;
std::chrono::steady_clock::time_point g_heartbeat_at{};

int64_t MillisecondsSince(std::chrono::steady_clock::time_point from) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - from)
      .count();
}

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Record the completed source before starting the next one.
void NoteSeam(uint32_t samples) {
  g_seam_at = std::chrono::steady_clock::now();
  REXLOG_INFO("shell_music: \"{}\" reached its end after {:.1f} s of samples ({:.1f} s in the "
              "menu) (R9)",
              g_playing_name.empty() ? std::string("?") : g_playing_name,
              static_cast<double>(samples) / kSamplesPerSecond,
              static_cast<double>(MillisecondsSince(g_track_started_at)) / 1000.0);
}

// The guest is big-endian, and its pointers are plain ints in its own image.
uint32_t LoadBe32(uint8_t* base, uint32_t address) {
  const uint8_t* p = rex::memory::GuestPtr<const uint8_t*>(base, address);
  if (p == nullptr) {
    return 0;
  }
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

constexpr size_t kMoggSize = 5;  // ".mogg"

// Walks a package's `\songs` subtree and appends every song it holds, as the path
// `ShellMusic::Load` wants it (the `.mogg` is left off - the engine appends it). Both
// shapes a package uses are covered: the usual `songs/<song>/<song>.mogg`, and a `.mogg`
// sitting directly in `\songs`. The depth is bounded so a malformed tree cannot recurse
// forever.
void CollectSongStreams(rex::filesystem::Entry* node, const std::string& path, size_t depth,
                        std::vector<std::string>& out) {
  if (node == nullptr || depth > 4) {
    return;
  }
  for (const std::unique_ptr<rex::filesystem::Entry>& child : node->children()) {
    if (child == nullptr || child->name().empty()) {
      continue;
    }
    const std::string child_path = path + "\\" + child->name();
    if ((child->attributes() & rex::filesystem::kFileAttributeDirectory) != 0) {
      CollectSongStreams(child.get(), child_path, depth + 1, out);
      continue;
    }
    const std::string& name = child->name();
    if (name.size() > kMoggSize &&
        name.compare(name.size() - kMoggSize, kMoggSize, ".mogg") == 0) {
      out.push_back(child_path.substr(0, child_path.size() - kMoggSize));
    }
  }
}

// Every song a package holds. A downloaded song is usually one package with one song, but a
// merged pack is one container holding a whole disc's worth of them (the retail packs:
// `songs/<song>/<song>.mogg` for each song, dozens to a package), so taking only the first -
// which this used to do - pinned the menu to whichever song sorted first, every boot. An
// art-only package (a stub whose whole table is an icon and an asset blob) yields nothing and
// is skipped rather than guessed at.
std::vector<std::string> FindSongStreams(rex::filesystem::Device& device) {
  std::vector<std::string> streams;
  rex::filesystem::Entry* songs = device.ResolvePath("\\songs");
  if (songs == nullptr) {
    songs = device.ResolvePath("songs");
  }
  if (songs == nullptr) {
    return streams;
  }
  CollectSongStreams(songs, "\\songs", 0, streams);
  return streams;
}

std::vector<rb_blitz::shell_music::SongMetadata> FindSongMetadata(rex::filesystem::Device& device) {
  return rb_blitz::fs::ReadDeviceSongMetadata(device);
}

// The package as a read-only guest device: an STFS container mounted where it lies, or
// an already-extracted package directory. Null when the SDK will not mount it.
// (The reader itself is shared with the store's Downloaded matcher; see
// src/fs/dlc_song_metadata.h.)
using rb_blitz::fs::MountPackage;

struct PreparedPackage {
  size_t index = 0;
  std::unique_ptr<rex::filesystem::Device> device;
  std::vector<PoolEntry> songs;
  std::shared_ptr<std::vector<rb_blitz::shell_music::catalog::Package>> catalog;
  bool catalog_complete = false;
};

rb_blitz::shell_music::MenuMusicQueue<PreparedPackage> g_queue;
rex::filesystem::VirtualFileSystem* g_file_system = nullptr;
std::vector<std::filesystem::path> g_packages;  // catalog aliases use this boot's stable indices
size_t g_next_package = 0;
rb_blitz::shell_music::MusicMountCache<rex::filesystem::Device> g_cache;
std::optional<PreparedPackage> g_ready_package;
bool g_queue_started = false;
std::atomic<bool> g_closing{false};
using MusicCatalog = std::vector<rb_blitz::shell_music::catalog::Package>;
std::shared_ptr<MusicCatalog> g_catalog_pending;
std::filesystem::path g_catalog_path;
std::atomic<bool> g_catalog_io_allowed{false};
constexpr size_t kCatalogJob = rb_blitz::shell_music::kNoIndex;

// Bundled songs stay in the game's ARK and use native relative paths. They
// never occupy or request a slot in the rolling DLC mount cache.
constexpr size_t kBasePackage = rb_blitz::shell_music::kNoIndex;
rb_blitz::settings::ultimate_adapter::ArkIndex g_base_archive;

bool PackageReady(size_t package) {
  return package == kBasePackage || g_cache.Contains(package);
}

std::string SourcePath(const PoolEntry& source) {
  return source.alias.empty() ? source.stream : source.alias + ":" + source.stream;
}

struct ArchiveSlice {
  rex::filesystem::Entry* part = nullptr;
  uint64_t offset = 0;
  size_t size = 0;
};

ArchiveSlice FindBaseRecord(std::string_view path) {
  const auto* record = g_base_archive.Find(path);
  if (!g_file_system || !record || record->compressed() || !record->size) return {};
  auto offset = record->offset;
  for (size_t i = 0; i < g_base_archive.parts.size(); ++i) {
    const auto part_size = g_base_archive.part_sizes[i];
    if (offset >= part_size) { offset -= part_size; continue; }
    auto* part = g_file_system->ResolvePath("game:\\" + g_base_archive.parts[i]);
    if (!part || offset > part->size() || record->size > part_size - offset ||
        record->size > part->size() - offset) return {};
    return {part, offset, size_t(record->size)};
  }
  return {};
}

bool ReadBaseRecord(std::string_view path, std::vector<uint8_t>* bytes) {
  const auto slice = FindBaseRecord(path);
  if (!slice.part || slice.size > 2 * 1024 * 1024) return false;
  rex::filesystem::File* file = nullptr;
  if (slice.part->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file) return false;
  bytes->resize(slice.size);
  size_t read = 0;
  const auto status = file->ReadSync(*bytes, slice.offset, &read);
  file->Destroy();
  return status == 0 && read == slice.size;
}

// Called before guest launch, after the base game is mounted. Catalog only
// metadata-declared streams present in the archive, excluding the two lessons.
void DiscoverBaseSongs() {
  auto* header = g_file_system->ResolvePath("game:\\gen\\main_xbox.hdr");
  if (!header || !header->size() || header->size() > 2 * 1024 * 1024) return;
  rex::filesystem::File* file = nullptr;
  if (header->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file) return;
  std::vector<uint8_t> bytes(header->size());
  size_t read = 0;
  const auto status = file->ReadSync(bytes, 0, &read);
  file->Destroy();
  std::string error;
  if (status != 0 || read != bytes.size() ||
      !rb_blitz::settings::ultimate_adapter::ReadArkIndex(
          bytes.data(), bytes.size(), &g_base_archive, &error)) {
    REXLOG_WARN("shell_music: cannot read base-game song archive: {}", error);
    return;
  }
  if (!ReadBaseRecord("songs/gen/songs.dtb", &bytes)) return;
  const auto metadata = rb_blitz::shell_music::ReadSongMetadata(bytes, true, &error);
  std::vector<std::string> artwork;
  for (const auto& record : g_base_archive.entries)
    if (record.path.ends_with(".png_xbox") && FindBaseRecord(record.path).part)
      artwork.push_back(record.path);
  std::vector<PoolEntry> songs;
  for (const auto& song : metadata) {
    if (song.key == "tutorialsong" || song.key == "tutorialsong3") continue;
    const auto stream = rb_blitz::shell_music::StreamIdentity(song.stream);
    if (stream.size() + 1 > kBufferBytes || !FindBaseRecord(stream + ".mogg").part) continue;
    PoolEntry entry{"", stream, song.key, song.title, song.artist};
    entry.artwork_locator = rb_blitz::ui::music::ResolveArtwork(stream, artwork);
    songs.push_back(std::move(entry));
  }
  const auto count = g_session.Discover(kBasePackage, std::move(songs));
  REXLOG_INFO("shell_music: {} bundled song(s) ready through the native game archive", count);
}


std::string MountPath(size_t index) {
  return fmt::format("\\Device\\BlitzMenuMusic\\{}\\", index);
}

void CollectArtwork(rex::filesystem::Entry* node, const std::string& path, size_t depth,
                    std::vector<std::string>& out) {
  if (!node || depth > 6) return;
  for (const auto& child : node->children()) {
    const auto next = path + "\\" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory)
      CollectArtwork(child.get(), next, depth+1, out);
    else if (rb_blitz::shell_music::StreamIdentity(next).ends_with(".png_xbox"))
      out.push_back(next);
  }
}

// Runs on either the boot thread or the worker. No VFS registration, guest
// memory, RNG or live pool access happens here.
std::optional<PreparedPackage> PreparePackage(const std::filesystem::path& package, size_t index) {
  try {
    PreparedPackage prepared;
    prepared.index = index;
    std::error_code ec;
    prepared.device = MountPackage(MountPath(index), package, ec);
    if (!prepared.device) {
      REXLOG_WARN("shell_music: cannot mount {} as DLC menu music ({})", package.string(),
                  ec ? ec.message() : "not a mountable package");
      return std::nullopt;
    }
    auto streams = FindSongStreams(*prepared.device);
    const auto metadata = streams.empty() ? std::vector<rb_blitz::shell_music::SongMetadata>{}
                                         : FindSongMetadata(*prepared.device);
    const std::string alias = fmt::format("menumusic{}", index);
    std::vector<std::string> artwork;
    if (g_navigation_enabled) CollectArtwork(prepared.device->ResolvePath(""), "", 0, artwork);
    for (auto& stream : streams) {
      if (alias.size() + stream.size() + 2 > kBufferBytes) continue;
      auto key = rb_blitz::shell_music::SongKeyForStream(metadata, stream);
      if (!metadata.empty() && key.empty()) continue;
      PoolEntry entry{alias, stream, std::move(key)};
      entry.artwork_locator = rb_blitz::ui::music::ResolveArtwork(stream, artwork);
      for (const auto& song : metadata) {
        if (song.key == entry.song_key &&
            rb_blitz::shell_music::StreamIdentity(song.stream) ==
                rb_blitz::shell_music::StreamIdentity(stream)) {
          entry.title = song.title;
          entry.artist = song.artist;
          break;
        }
      }
      prepared.songs.push_back(std::move(entry));
    }
    return prepared;
  } catch (const std::exception& error) {
    REXLOG_WARN("shell_music: preparing {} failed: {}", package.string(), error.what());
    return std::nullopt;
  }
}

// Caller holds g_mutex or runs before guest launch. Published song IDs are
// catalog IDs, never positions in the mounted cache or the session playlist.
std::set<size_t> WarmPackagesLocked() {
  std::vector<size_t> pins{g_stream_song};
  if (g_nav_target != rb_blitz::shell_music::kNoIndex) pins.push_back(g_session.SongAt(g_nav_target));
  if (g_pending_index >= 0) pins.push_back(size_t(g_pending_index));
  if (g_current_index >= 0) pins.push_back(size_t(g_current_index));
  const int selected = g_play_selection.Query(NowMs());
  if (selected >= 0) pins.push_back(size_t(selected));
  return g_session.WarmPackages(pins);
}

void RetireColdLocked() {
  const auto retired = g_cache.RetireCold(WarmPackagesLocked(), [](size_t package) {
    // Neither the active stream, its pending replacement nor a captured press
    // can refer to this source. Open native Files defer its destruction further.
    g_file_system->UnregisterSymbolicLink(fmt::format("menumusic{}:", package));
    return g_file_system->UnregisterDevice(MountPath(package));
  });
  if (retired) REXLOG_INFO("shell_music: retired {} cold package(s); {} resident, {} catalog songs",
                           retired, g_cache.Size(), g_session.Sources().size());
}

// Returns false only when all residency slots are pinned; keep the prepared
// device pending and retry on a later poll without blocking or growing the cache.
bool PublishPackage(PreparedPackage& prepared) {
  if (prepared.catalog) {
    if (!prepared.catalog_complete) return true; // another bounded batch follows on menu polls
    for (size_t i = 0; i < prepared.catalog->size(); ++i) {
      auto songs = (*prepared.catalog)[i].songs;
      for (auto& song : songs) song.alias = fmt::format("menumusic{}", i);
      g_session.Discover(i, std::move(songs));
    }
    g_next_package = g_packages.size();
    g_session.DiscoveryComplete();
    g_session.PlanAhead(g_rng);
    g_catalog_pending.reset();
    REXLOG_INFO("shell_music: complete catalog ready: {} songs; choosing uniformly by song",
                g_session.Sources().size());
    return true;
  }
  if (!prepared.device) {
    g_session.BlockPackage(prepared.index);
    return true;
  }
  g_session.Discover(prepared.index, std::move(prepared.songs));
  g_session.PlanAhead(g_rng);
  if (!WarmPackagesLocked().contains(prepared.index)) return true;
  if (g_cache.Contains(prepared.index)) return true;
  RetireColdLocked();
  if (g_cache.Full()) return false;
  const auto mount_path = MountPath(prepared.index);
  auto* device = prepared.device.get();
  if (!g_file_system->RegisterDevice(std::move(prepared.device))) {
    g_session.BlockPackage(prepared.index);
    return true;
  }
  if (!g_file_system->RegisterSymbolicLink(fmt::format("menumusic{}:", prepared.index), mount_path)) {
    g_file_system->UnregisterDevice(mount_path);
    g_session.BlockPackage(prepared.index);
    return true;
  }
  g_cache.Insert(prepared.index, device);
  REXLOG_INFO("shell_music: package {} ready; {} resident, {} catalog songs, {} playlist entries",
              prepared.index, g_cache.Size(), g_session.Sources().size(), g_session.Playlist().size());
  return true;
}

size_t ReadyNextPositionLocked() {
  g_session.PlanAhead(g_rng);
  const size_t position = g_session.NextPosition();
  return position != rb_blitz::shell_music::kNoIndex &&
         PackageReady(g_session.Package(g_session.SongAt(position)))
      ? position : rb_blitz::shell_music::kNoIndex;
}

bool HasReadyNext() {
  std::lock_guard lock(g_mutex);
  return ReadyNextPositionLocked() != rb_blitz::shell_music::kNoIndex;
}

std::string NextPathLocked() {
  const size_t position = ReadyNextPositionLocked();
  if (position == rb_blitz::shell_music::kNoIndex) {
    g_pending_index = g_current_index = -1;
    g_play_selection.Stop();
    return {};
  }
  g_session.Select(position);
  const size_t id = g_session.SongAt(position);
  g_pending_index = static_cast<int>(id);
  g_current_index = -1;
  g_play_selection.Loading(g_pending_index, NowMs());
  const auto& source = g_session.Sources()[id];
  return SourcePath(source);
}

void SchedulePreparationLocked() {
  if (!g_queue_started || !g_queue.Idle() || g_ready_package) return;
  if (g_catalog_pending) {
    // A cold catalog scans metadata only after startup/discovery has finished.
    // While it runs, native shell music covers the gap; ready bundled songs
    // never get substituted for a random choice that is still unknown/loading.
    const bool allowed = rb_blitz::input::MainMenuMusicInputAvailable() &&
                         !rb_blitz::graphics::GraphicsLoadingPending();
    g_catalog_io_allowed = allowed;
    if (allowed) g_queue.Request(kCatalogJob);
    return;
  }
  if (g_next_package == g_packages.size()) g_session.DiscoveryComplete();
  g_session.PlanAhead(g_rng);
  RetireColdLocked();
  if (g_cache.Full()) return;  // backpressure while retired streams still hold Files
  if (g_nav_target != rb_blitz::shell_music::kNoIndex) {
    const auto id = g_session.SongAt(g_nav_target);
    const auto package = g_session.Package(id);
    if (g_session.Usable(id) && !PackageReady(package)) { g_queue.Request(package, true); return; }
  }
  // The immediate successor has priority over previous/history cache warming.
  const auto next = g_session.NextPosition();
  if (next != rb_blitz::shell_music::kNoIndex) {
    const auto package = g_session.Package(g_session.SongAt(next));
    if (!PackageReady(package)) { g_queue.Request(package, true); return; }
  }
  for (size_t package : WarmPackagesLocked()) {
    if (!g_session.PackageBlocked(package) && !PackageReady(package)) {
      g_queue.Request(package);
      return;
    }
  }
  if (g_session.NeedsDiscovery() && g_next_package < g_packages.size()) {
    if (g_queue.Request(g_next_package)) ++g_next_package;
  }
}

// The title's own pool entries, as the panel's own loader spells them (`sprintf("sfx/shell/%s")`
// over the three `shellmusic_N` entries of config/synth.dtb). Handing one of these back is how the
// menu gets distinct music once the rotation has run out instead of a loop of one song.
constexpr const char* kTitleTracks[] = {"sfx/shell/samples/shellmusic_1",
                                        "sfx/shell/samples/shellmusic_2",
                                        "sfx/shell/samples/shellmusic_3"};

// Retire the stream a ShellMusic currently holds, the way the engine's own Start does
// (0x82758C08): the stream's own destructor with the "remove from the pool" flag, the field
// cleared, then StopStreams. The rotation uses this when handing over to its next track.
void RetireStreamOnGuest(uint8_t* base, PPCContext& parent, uint32_t music) {
  g_nav_paused_stream = 0;
  const uint32_t old_stream = LoadBe32(base, music + kStreamOffset);
  if (old_stream == 0) {
    return;
  }
  {
    PPCContext destroy(parent);
    destroy.r1.u32 -= 0x100;
    destroy.r3.u32 = old_stream;
    destroy.r4.u32 = 1;
    const uint32_t destructor = LoadBe32(base, LoadBe32(base, old_stream));
    CallMusicGuest(rex::runtime::ResolveIndirectFunction(destructor), destroy, base, "retire");
  }
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, music + kStreamOffset), 0, 4);
  PPCContext stop(parent);
  stop.r1.u32 -= 0x100;
  stop.r3.u32 = music;
  CallMusicGuest(__imp__sub_827583F8, stop, base, "stop streams");
}

// Hand `path` to the same ShellMusic the menu is polling, with the Load arguments the panel chose
// for its own pool entry. Runs on the guest thread from inside the poll and takes no lock while
// calling into the guest. `loop` is the panel's own `loop` argument (Load stores it into
// `ShellMusic+48`, and Start turns it into the stream's own jump at 0x82758D64): off for a DLC
// song, which must end so the rotation can advance, on for a title fallback, which loops exactly
// as the title's own music does.
void LoadOnGuest(uint8_t* base, PPCContext& parent, uint32_t music, uint32_t buffer,
                 const std::string& path, bool loop) {
  // This handoff starts music; the StopStreams cleanup inside Start (0x82758C28) must not be
  // mistaken for the game letting the track go.
  const bool was_setting_up = g_setting_up_music;
  g_setting_up_music = true;
  // Load only changes the source. It does NOT replace the active stream. Dispose of that
  // stream through its own destructor before loading/starting another song, as Start does
  // when stopped (0x82758C08). Otherwise every logged "next" song is still the old audio.
  RetireStreamOnGuest(base, parent, music);
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, buffer), path.c_str(), path.size() + 1);
  PPCContext call(parent);
  call.r1.u32 -= 0x100;              // a scratch frame on the guest's own stack
  call.r3.u32 = music;               // this
  call.r4.u32 = buffer;              // filename
  call.f1.f64 = g_gain;              // volume, the one the panel passed
  call.r6.u64 = g_scale_pan;         // preserve the panel's pan scaling
  call.r7.u64 = loop ? 1 : 0;        // the panel's own loop flag (Load stores it at +48)
  CallMusicGuest(__imp__sub_82757F50, call, base, "load");
  *rex::memory::GuestPtr<uint8_t*>(base, music + 48) = loop ? 1 : 0;  // same field, read back
  PPCContext start(parent);
  start.r1.u32 -= 0x100;
  start.r3.u32 = music;
  CallMusicGuest(__imp__sub_82758B98, start, base, "start");
  {
    std::lock_guard lock(g_mutex);
    g_stream_song = !loop && g_pending_index >= 0 ? size_t(g_pending_index)
                                                 : rb_blitz::shell_music::kNoIndex;
  }
  g_title_index = -1;
  if (loop) for (int i = 0; i < 3; ++i) if (path == kTitleTracks[i]) g_title_index = i;
  g_pos = 0;
  g_nav_age.Reset();
  g_track_started_at = std::chrono::steady_clock::now();
  g_heartbeat_at = g_track_started_at;
  g_playback.Begin(NowMs());
  g_setting_up_music = was_setting_up;
}

// Retire the old stream and start the next distinct song. A pool with no distinct
// candidate falls back to the title's own tracks, which loop as the title's music does;
// it never repeats the sole DLC song.
void StartNextTrack(uint8_t* base, PPCContext& parent, uint32_t music) {
  std::string path;
  uint32_t buffer = 0;
  bool title_track = false;
  bool new_pass = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto old_pass = g_session.Cursor() == rb_blitz::shell_music::kNoIndex ? 0
        : g_session.Playlist()[g_session.Cursor()].pass;
    path = NextPathLocked();
    new_pass = g_session.Cursor() != rb_blitz::shell_music::kNoIndex &&
               g_session.Playlist()[g_session.Cursor()].pass != old_pass;
    if (path.empty() && !g_session.Sources().empty()) {
      std::uniform_int_distribution<size_t> pick(0, 2);
      path = kTitleTracks[pick(g_rng)];
      title_track = true;
    }
    if (!path.empty() && !g_buffers.empty()) {
      buffer = g_buffers[g_buffer_next];
      g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
    }
    g_playing_name = g_pending_index >= 0
        ? rb_blitz::shell_music::PoolSongKey(g_session.Sources()[g_pending_index]) : std::string();
  }
  if (path.empty() || buffer == 0 || path.size() + 1 > kBufferBytes) {
    return;
  }
  if (title_track) {
    g_waiting_for_pool = true;
    g_watch = false;        // the title's own music loops; the rotation has nothing left to do
    g_watch_title = true;   // ...but the poll still reports it, so a stalled fallback is visible
    REXLOG_INFO("shell_music: the library pool has nothing new to play; the menu goes back to the "
                "title's own tracks (\"{}.mogg\") (R9)",
                path);
  } else {
    g_waiting_for_pool = false;
    g_watch = true;
    g_watch_title = false;
    REXLOG_INFO("shell_music: the menu track ended; playing \"{}.mogg\" next (R9), handed over "
                "{} ms after the seam{}",
                path, MillisecondsSince(g_seam_at),
                new_pass ? fmt::format(", opening a new pass over the {} song(s) of the pool",
                                       g_session.Sources().size())
                         : std::string());
  }
  LoadOnGuest(base, parent, music, buffer, path, /*loop=*/title_track);
}

// A candidate whose load failed: it sits out the rest of this rotation, the menu gets the next
// song, and a run of failures ends the rotation for this boot rather than spinning on it.
void NoteFailedCandidate(uint8_t* base, PPCContext& parent, uint32_t music) {
  std::string failed;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    failed = g_playing_name;
    if (!failed.empty()) {
      if (g_pending_index >= 0) g_session.Failed(size_t(g_pending_index));
      g_playing_name.clear();
    }
    g_play_selection.Failed(g_pending_index);
    g_current_index = -1;
  }
  REXLOG_WARN("shell_music: \"{}\" failed to start or stopped advancing ({} ms since start); "
              "it sits out the rest of this rotation (R9)",
              failed, MillisecondsSince(g_track_started_at));
  // A single startup candidate can fail before the post-A cache-loading event.
  // Start bounded recovery preparation now instead of waiting for that event.
  rb_blitz::shell_music::StartQueuedPool();
  if (++g_attempts >= kMaxAttemptsPerSeam && !g_session.Sources().empty()) {
    REXLOG_WARN("shell_music: {} candidate(s) in a row could not be started; the menu goes back "
                "to the title's own tracks (R9)",
                g_attempts);
    std::string path;
    uint32_t buffer = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::uniform_int_distribution<size_t> pick(0, 2);
      path = kTitleTracks[pick(g_rng)];
      buffer = g_buffers.empty() ? 0 : g_buffers[g_buffer_next];
      if (buffer != 0) {
        g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
      }
    }
    if (buffer != 0 && path.size() + 1 <= kBufferBytes) {
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending_index = -1;
        g_play_selection.Stop();
      }
      g_watch = false;
      g_watch_title = true;
      g_waiting_for_pool = false;  // repeated failures, not a queue still filling
      LoadOnGuest(base, parent, music, buffer, path, /*loop=*/true);
    }
    return;
  }
  g_watch = true;
  StartNextTrack(base, parent, music);
}

// Retail StandardStream's slot 7 (0x823E3FD8) pauses every channel through
// 0x823E96B0 and sets state 5; slot 6 (0x823E4808) resumes through 0x823E9640.
// ShellMusic's poll would immediately call Play again (0x827588A0), so the Play
// hook below preserves an explicitly paused stream. State 4 is not this seam.
// Owner listening confirmation and rb_blitz_367.log prove pause across 7.3 s
// with a frozen cursor and same-stream/same-position resume (2026-10-26).
void PublishNavigator(uint8_t* base, uint32_t music) {
  using namespace rb_blitz::shell_music;
  if (!g_navigation_enabled || music != g_music) return;
  PlaybackUpdate update;
  const bool source = music && (g_watch || g_watch_title) && !g_closing;
  const bool available = source &&
      rb_blitz::input::HostInputFocused() &&
      !rb_blitz::input::capture::Armed() && !rex::kernel::xam::xeXamIsUIActive() &&
      !rb_blitz::graphics::GraphicsLoadingPending();
  if (source) {
    const auto stream = LoadBe32(base, music + kStreamOffset);
    const auto state = stream ? LoadBe32(base, stream + 20) : 0;
    const auto position = stream ? LoadBe32(base, stream + kPositionOffset) : 0;
    const bool paused = stream && stream == g_nav_paused_stream && state == 5;
    if (paused) g_nav_age.Hold(stream, position);
    else g_nav_age.Observe(stream, state == 3, position);
    update.state = paused ? TransportState::kPaused :
        state == 3 && position > 0 ? TransportState::kPlaying : TransportState::kLoading;
    update.capabilities = {true, true, paused || (state == 3 && position > 0)};
    std::lock_guard lock(g_mutex);
    if (g_watch && g_pending_index >= 0) {
      const auto id = size_t(g_pending_index);
      const auto& song = g_session.Sources()[id];
      update.track = {g_session.Package(id) == kBasePackage ? MusicSource::kBaseGame : MusicSource::kDlc,
                      id, PoolSongKey(song), song.title, song.artist, song.artwork_locator};
      if (update.track.title.empty()) update.track.title = update.track.key;
    } else if (g_title_index >= 0) {
      update.track = {MusicSource::kTitle, size_t(g_title_index), kTitleTracks[g_title_index],
                     fmt::format("Menu Track {}", g_title_index + 1), "Unknown artist", {}};
    }
  }
  NowPlaying().Publish(g_nav_epoch, std::move(update), available);
}

void FinishNavigator(nav::CompletionStatus status, const std::string& reason = {}) {
  if (g_nav_request) {
    REXLOG_INFO("music_nav: request {} {} ({})", g_nav_request->id,
                status == nav::CompletionStatus::kApplied ? "applied" : "ended", reason);
    rb_blitz::shell_music::NowPlaying().Complete(*g_nav_request, status, reason);
  }
  g_nav_request.reset();
  g_nav_target = rb_blitz::shell_music::kNoIndex;
  g_nav_started = false;
  g_nav_title_target = -1;
}

void ApplyNavigatorCommand(uint8_t* base, PPCContext& ctx, uint32_t music) {
  using namespace rb_blitz::shell_music;
  if (g_closing) {
    FinishNavigator(nav::CompletionStatus::kCancelled, "shutdown");
    return;
  }
  if (!g_navigation_enabled || music != g_music || !(g_watch || g_watch_title)) return;
  const bool blocked = !rb_blitz::input::HostInputFocused() ||
                       rb_blitz::input::capture::Armed() || rex::kernel::xam::xeXamIsUIActive() ||
                       rb_blitz::graphics::GraphicsLoadingPending();
  if (blocked) {
    FinishNavigator(nav::CompletionStatus::kCancelled, "input/loading owner changed");
    NowPlaying().SetAvailable(false);
    return;
  }
  if (g_nav_request && g_nav_request->producer != nav::Producer::kControlsPage &&
      !rb_blitz::input::MainMenuMusicInputAvailable()) {
    FinishNavigator(nav::CompletionStatus::kCancelled, "main menu left before handoff");
    return;
  }
  if (!g_nav_request) {
    g_nav_request = nav::Take();
    if (!g_nav_request) return;
    if (g_nav_request->producer != nav::Producer::kControlsPage &&
        !rb_blitz::input::MainMenuMusicInputAvailable()) {
      FinishNavigator(nav::CompletionStatus::kCancelled, "main menu left before execution");
      return;
    }
    if (g_nav_request->command == nav::Command::kPlayPause) {
      const auto stream = LoadBe32(base, music + kStreamOffset);
      const auto state = stream ? LoadBe32(base, stream + 20) : 0;
      const bool resume = stream && stream == g_nav_paused_stream && state == 5;
      if (!stream || (!resume && state != 3)) {
        FinishNavigator(nav::CompletionStatus::kFailed, "native pause unavailable for this stream");
        return;
      }
      PPCContext call(ctx);
      call.r1.u32 -= 0x100;
      call.r3.u32 = stream;
      const auto position = LoadBe32(base, stream + kPositionOffset);
      if (resume) {
        g_nav_age.Hold(stream, position);
        __imp__sub_823E4808(call, base);
        if (LoadBe32(base, stream + 20) == 3) g_nav_paused_stream = 0;
      } else {
        sub_823E3FD8(call, base);
        if (LoadBe32(base, stream + 20) == 5) g_nav_paused_stream = stream;
        g_nav_pause_position = LoadBe32(base, stream + kPositionOffset);
        g_nav_pause_log_at = std::chrono::steady_clock::now();
      }
      g_playback.DeferWatchdog(NowMs());
      const auto after = LoadBe32(base, stream + 20);
      REXLOG_INFO("music_nav: {} stream {:08X} state {} -> {} samples {} -> {} pause_samples {}",
                  resume ? "resume" : "pause", stream, state, after, position,
                  LoadBe32(base, stream + kPositionOffset), g_nav_pause_position);
      PublishNavigator(base, music);
      const bool applied = after == (resume ? 3u : 5u);
      FinishNavigator(applied ? nav::CompletionStatus::kApplied : nav::CompletionStatus::kFailed,
                      applied ? (resume ? "native resume" : "native pause") : "native pause transition failed");
      return;
    }
    const auto stream = LoadBe32(base, music + kStreamOffset);
    const auto position = stream ? LoadBe32(base, stream + kPositionOffset) : 0;
    g_nav_age.Observe(stream, stream && LoadBe32(base, stream + 20) == 3, position);
    const auto samples = g_nav_request->previous_step_back ? 0 : g_nav_age.samples();
    std::lock_guard lock(g_mutex);
    g_session.PlanAhead(g_rng);
    g_nav_target = g_nav_request->command == nav::Command::kNext ? g_session.NextPosition()
                                                               : g_session.PreviousPosition(samples);
    REXLOG_INFO("music_nav: request {} action {} raw_position {} elapsed_samples {} cursor {} target {}",
                g_nav_request->id, int(g_nav_request->command), position, samples,
                g_session.Cursor(), g_nav_target);
    // A temporary native fallback is audible, but is not an entry in the DLC
    // history. Restart that source after five seconds; before five, return to
    // the last DLC entry rather than skipping another entry backward.
    if (g_enabled && g_watch_title && g_nav_request->command == nav::Command::kPrevious) {
      if (samples >= nav::kRestartSamples || g_session.Cursor() == kNoIndex) {
        g_nav_title_target = g_title_index;
        g_nav_target = kNoIndex;
      } else {
        g_nav_target = g_session.Cursor();
      }
    }
  }
  if (g_nav_started) return; // confirmation follows observation of the new stream
  std::string path;
  uint32_t buffer = 0;
  {
    std::lock_guard lock(g_mutex);
    if (g_nav_title_target >= 0) {
      path = kTitleTracks[g_nav_title_target];
    } else {
      if (g_nav_target == kNoIndex && g_nav_request->command == nav::Command::kNext) {
        g_session.PlanAhead(g_rng);
        g_nav_target = g_session.NextPosition();
        if (g_nav_target == kNoIndex && !g_session.DiscoveryFinished()) {
          SchedulePreparationLocked();
          return;
        }
      }
      if (g_nav_target == kNoIndex) {
        FinishNavigator(nav::CompletionStatus::kFailed, "no successor ready in the session");
        return;
      }
      const auto id = g_session.SongAt(g_nav_target);
      if (!g_session.Usable(id)) {
        FinishNavigator(nav::CompletionStatus::kFailed, "target package unavailable");
        return;
      }
      if (g_enabled && !PackageReady(g_session.Package(id))) {
        SchedulePreparationLocked();
        return; // keep audible track, metadata and cursor while a cold target remounts
      }
      const auto& song = g_session.Sources()[id];
      path = g_enabled ? SourcePath(song) : song.stream;
    }
    if (g_buffers.empty()) {
      FinishNavigator(nav::CompletionStatus::kFailed, "no guest path buffer");
      return;
    }
    if (path.size() + 1 > kBufferBytes) {
      FinishNavigator(nav::CompletionStatus::kFailed, "source path too long");
      return;
    }
    buffer = g_buffers[g_buffer_next];
    g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
    if (g_nav_title_target < 0) {
      g_session.Skipped();
      g_session.Select(g_nav_target);
    }
    g_pending_index = g_enabled && g_nav_title_target < 0 ? int(g_session.SongAt(g_nav_target)) : -1;
    g_current_index = -1;
    g_play_selection.Skipped(g_pending_index, NowMs());
    g_playing_name = g_pending_index >= 0 ? PoolSongKey(g_session.Sources()[g_pending_index]) : path;
  }
  const bool title_track = !g_enabled || g_nav_title_target >= 0;
  g_watch = !title_track;
  g_watch_title = title_track;
  g_waiting_for_pool = false;
  g_attempts = 0;
  g_nav_started = true;
  REXLOG_INFO("music_nav: request {} loading {} (cursor {})", g_nav_request->id, path, g_nav_target);
  g_nav_start_at_zero = true;
  LoadOnGuest(base, ctx, music, buffer, path, title_track);
  g_nav_start_at_zero = false;
}

// The path the title is about to stream is one of its own three pool entries; give it
// one of the bundled or loaded DLC songs instead. Anything else - a path that is not the shell
// music pool's - is left exactly as it is.
void SubstituteFilename(uint8_t* base, PPCContext& ctx) {
  const auto requested = ctx.r4.u32 ? rex::memory::GuestPtr<const char*>(base, ctx.r4.u32) : nullptr;
  // Take a completed selected mount without waiting for it. A cold/slow DLC
  // package must never hold the splash screen behind disk I/O, and a ready
  // bundled source must never replace the song selected from the full catalog.
  if (requested && std::strncmp(requested, kShellMusicPrefix, kShellMusicPrefixSize) == 0)
    rb_blitz::shell_music::PublishQueuedPool(true);
  if (g_navigation_enabled && requested && std::strncmp(requested, kShellMusicPrefix, kShellMusicPrefixSize) == 0) {
    FinishNavigator(nav::CompletionStatus::kCancelled, "native source changed");
    g_nav_paused_stream = 0;
    g_nav_epoch = rb_blitz::shell_music::NowPlaying().Reset();
    g_music = ctx.r3.u32;
    g_gain = ctx.f1.f64;
    g_scale_pan = ctx.r6.u64;
    g_nav_age.Reset();
    g_title_index = -1;
    for (int i = 0; i < 3; ++i) if (requested == std::string_view(kTitleTracks[i])) g_title_index = i;
    if (!g_enabled && g_title_index >= 0 && !g_buffers.empty()) {
      std::lock_guard lock(g_mutex);
      {
        g_session = rb_blitz::shell_music::MusicSession{};
        std::vector<PoolEntry> songs;
        for (auto path : kTitleTracks) songs.push_back({"", path, path});
        g_session.Discover(0, std::move(songs));
        g_session.AdoptFirst(size_t(g_title_index));
        g_session.DiscoveryComplete();
        g_session.PlanAhead(g_rng);
      }
      g_watch_title = true;
      g_watch = g_waiting_for_pool = false;
      g_current_index = g_pending_index = -1;
      g_play_selection.Stop();
      g_playback.Stop();
      g_pos = 0;
    }
  }
  if (!g_enabled || g_buffers.empty()) {
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
  if (path.empty()) {
    // Returning from gameplay can require an asynchronous remount. Watch the
    // title's own source until the requested successor is ready; never wait here.
    g_music = ctx.r3.u32;
    g_gain = ctx.f1.f64;
    g_scale_pan = ctx.r6.u64;
    g_pos = 0;
    g_watch = false;
    g_watch_title = g_waiting_for_pool = true;
    g_heartbeat_at = g_track_started_at = std::chrono::steady_clock::now();
    ctx.r7.u64 = 1;
    return;
  }
  if (path.size() + 1 > kBufferBytes) {
    return;
  }
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, buffer), path.c_str(), path.size() + 1);
  ctx.r4.u32 = buffer;
  // Remember the object and the arguments the panel chose: the poll hook starts every
  // later track of the rotation the same way, when the song it started comes round again.
  g_music = ctx.r3.u32;
  // Stream the library song from its file, with no native jump back to its beginning. The retail
  // shell player otherwise buffers and loops one source indefinitely (0x82758D64).
  *rex::memory::GuestPtr<uint8_t*>(base, g_music + 60) = 0;
  ctx.r7.u64 = 0;
  g_gain = ctx.f1.f64;
  g_scale_pan = ctx.r6.u64;
  g_pos = 0;
  size_t pool_size = 0;
  int index = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_playing_name = rb_blitz::shell_music::PoolSongKey(g_session.Sources()[g_pending_index]);
    pool_size = g_session.Sources().size();
    index = g_pending_index;
  }
  g_track_started_at = std::chrono::steady_clock::now();
  g_seam_at = g_track_started_at;
  g_heartbeat_at = g_track_started_at;
  g_playback.Stop();  // Load only stages a path; Start arms the startup watchdog.
  g_watch = true;
  g_watch_title = false;
  g_attempts = 0;
  REXLOG_INFO("shell_music: the main menu asked for \"{}\"; playing \"{}.mogg\" instead (R9) "
              "(pool {} of {})",
              asked, path, index, pool_size);
}

}  // namespace

namespace rb_blitz::shell_music {

ui::music::ArtRead ReadArtwork(const ui::music::ArtTag& tag, std::vector<uint8_t>* bytes) {
  using ui::music::ArtRead;
  const auto snapshot = NowPlaying().ReadSnapshot();
  if (!bytes || !snapshot.available || tag.epoch != snapshot.epoch ||
      tag.song != snapshot.playback.track.song || tag.key != snapshot.playback.track.artwork_key)
    return ArtRead::kMissing;
  rex::filesystem::File* file = nullptr;
  size_t size = 0;
  uint64_t offset = 0;
  {
    std::lock_guard lock(g_mutex);
    if (g_closing || !g_navigation_enabled || tag.song >= g_session.Sources().size())
      return ArtRead::kMissing;
    const auto& source = g_session.Sources()[tag.song];
    if (source.artwork_locator != tag.key) return ArtRead::kMissing;
    rex::filesystem::Entry* entry = nullptr;
    if (g_session.Package(tag.song) == kBasePackage) {
      const auto slice = FindBaseRecord(source.artwork_locator);
      entry = slice.part;
      size = slice.size;
      offset = slice.offset;
    } else {
      auto* device = g_cache.Get(g_session.Package(tag.song));
      if (!device) return ArtRead::kDeferred;  // playback remounts; art adds no residency
      entry = device->ResolvePath(source.artwork_locator);
      if (entry) size = entry->size();
    }
    if (!entry || !size || size > 2*1024*1024) return ArtRead::kMissing;
    if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != 0 || !file)
      return ArtRead::kMissing;
  }
  // File's accounting pins the device across retirement without holding g_mutex
  // over disk I/O. Destroy the lease before returning bytes to the decoder.
  const auto destroy = [](rex::filesystem::File* value) { value->Destroy(); };
  std::unique_ptr<rex::filesystem::File, decltype(destroy)> lease(file, destroy);
  bytes->resize(size);
  size_t read = 0;
  return file->ReadSync(*bytes, offset, &read) == 0 && read == size
      ? ArtRead::kReady : ArtRead::kMissing;
}

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  (void)game_data_root;
  REXLOG_INFO("shell_music: rolling session: async startup package, {} upcoming and {} recent "
              "songs warm, at most {} resident packages",
              rb_blitz::shell_music::MusicSession::kAhead,
              rb_blitz::shell_music::MusicSession::kBehind,
              rb_blitz::shell_music::MusicSession::kResidentLimit);
  const std::string legacy_note = rb_blitz::settings::menu_compat::LegacyPoolCountNote(
      REXCVAR_GET(enhancements_menu_dlc_song_count));
  if (!legacy_note.empty()) {
    REXLOG_WARN("shell_music: {}", legacy_note);
  }
  // The main menu's DLC pool is a **setting** again (owner request, 2026-10-14): "DLC Menu Songs"
  // - `general.dlc_menu_songs`, drawn on the merged surface's Downloadable Content page - is its one
  // control. The store row is written into `enhancements_menu_dlc_songs` by the boot at the config
  // rank (SettingsBoot::ConfigurePaths), which has already run by the time this hook does, and this
  // reads that cvar - so a command-line flag or an environment value still outranks the row, exactly
  // like every other enhancement row. Both defaults are `true`, which is the behaviour R9 has had
  // since 2026-10-13, so reviving the row changes no existing boot.
  //
  // The read happens once, on this host thread; no guest thread ever touches the cvar registry.
  g_navigation_enabled = REXCVAR_GET(enhancements_menu_dlc_songs) &&
      (REXCVAR_GET(enhancements_background_music_nav) || REXCVAR_GET(enhancements_music_player));
  g_music_fault_base.store(g_navigation_enabled && runtime && runtime->memory()
      ? reinterpret_cast<uintptr_t>(runtime->memory()->virtual_membase()) : 0,
      std::memory_order_relaxed);
  if (g_music_fault_base.load(std::memory_order_relaxed)) {
    static const auto handler = AddVectoredExceptionHandler(1, LogMusicFault);
    REXLOG_INFO("music_nav: native fault trace {}", handler ? "installed" : "unavailable");
  }
  g_nav_pause_trace = REXCVAR_GET(background_music_nav_pause_trial);
  g_nav_paused_stream = 0;
  g_nav_epoch = rb_blitz::shell_music::NowPlaying().Reset();
  g_nav_probe = REXCVAR_GET(background_music_nav_probe);
  g_nav_probe_delay = REXCVAR_GET(background_music_nav_probe_delay);
  if ((g_navigation_enabled || REXCVAR_GET(enhancements_menu_dlc_songs)) && runtime && runtime->memory()) {
    for (size_t i = g_buffers.size(); i < kBufferCount; ++i) {
      const auto address = runtime->memory()->SystemHeapAlloc(static_cast<uint32_t>(kBufferBytes), 0x20);
      if (!address) break;
      g_buffers.push_back(address);
    }
  }
  if (g_buffers.empty()) g_navigation_enabled = false;
  g_menu_music_enabled = REXCVAR_GET(enhancements_menu_dlc_songs);
  // The index only means something for this boot's pool, and this is the one place the pool is
  // built: a fresh boot has no current track until the panel picks one.
  g_current_index = -1;
  g_pending_index = -1;
  g_play_selection.Stop();
  g_enabled = g_menu_music_enabled;
  REXLOG_INFO("shell_music: library background music is {} (enhancements_menu_dlc_songs = {}); {}",
              g_menu_music_enabled ? "on" : "off",
              REXCVAR_GET(enhancements_menu_dlc_songs) ? "true" : "false",
              g_menu_music_enabled ? "building the base-game and DLC pool" : "the menu keeps the title's tracks");
  if (!g_menu_music_enabled) {
    // R9 mounts nothing. The independent navigator may still capture and move
    // the native title sources; with both switches off the hooks remain inert.
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
  const auto pool_start = std::chrono::steady_clock::now();
  g_file_system = file_system;
  DiscoverBaseSongs();
  g_packages = packages;
  // Metadata is cheap to reuse, whereas opening every container competes with
  // startup reads. A complete catalog also gives merged-pack songs equal odds.
  g_catalog_path = runtime->cache_root() / "menu_music_catalog.cache";
  auto catalog = std::make_shared<MusicCatalog>(
      rb_blitz::shell_music::catalog::Load(g_catalog_path, g_packages));
  size_t cached = 0;
  for (size_t i = 0; i < catalog->size(); ++i) {
    if (!(*catalog)[i].identity) continue;
    auto songs = (*catalog)[i].songs;
    for (auto& song : songs) song.alias = fmt::format("menumusic{}", i);
    g_session.Discover(i, std::move(songs));
    ++cached;
  }
  g_session.RequireCompleteDiscovery();
  if (cached == g_packages.size()) {
    g_next_package = g_packages.size();
    g_session.DiscoveryComplete();
  } else {
    g_catalog_pending = std::move(catalog);
  }
  REXLOG_INFO("shell_music: catalog reused {}/{} packages; {} need background metadata reads",
              cached, g_packages.size(), g_packages.size() - cached);
  if (g_session.Sources().empty() && g_packages.empty()) {
    REXLOG_WARN("shell_music: no bundled or DLC song is playable; the main menu keeps the title's own "
                "tracks");
    g_enabled = false;
    g_current_index = -1;
    return;
  }

  if (g_buffers.empty()) {
    REXLOG_WARN("shell_music: no guest memory for a stream path; R9 falls back to the title's own "
                "tracks");
    g_enabled = false;
    g_current_index = -1;
    return;
  }

  if (g_next_package == g_packages.size()) g_session.DiscoveryComplete();
  g_watch = g_watch_title = false;
  g_attempts = 0;
  g_playing_name.clear();
  REXLOG_INFO("shell_music: startup metadata ready in {} ms: {} catalog song(s), {} mounted package(s) "
              "of {}; metadata order and mount readiness do not affect the random draw",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - pool_start).count(),
              g_session.Sources().size(), g_cache.Size(), packages.size());
  StartQueuedPool();
}

bool MenuMusicEnabled() { return g_menu_music_enabled; }

void StartQueuedPool() {
  std::lock_guard lock(g_mutex);
  if (!g_enabled || g_closing || g_queue_started) return;
  g_queue_started = true;
  g_queue.Start([packages = g_packages, catalog = g_catalog_pending,
                 catalog_path = g_catalog_path, catalog_cursor = size_t{0}]
                (size_t index) mutable -> std::optional<PreparedPackage> {
    if (index == kCatalogJob && catalog) {
      const auto started = std::chrono::steady_clock::now();
      size_t reads = 0;
      for (; catalog_cursor < catalog->size() && reads < 8; ++catalog_cursor) {
        if (g_closing) return {};
        if (!g_catalog_io_allowed) break;
        auto& record = (*catalog)[catalog_cursor];
        if (record.identity) continue;
        const auto before = rb_blitz::shell_music::catalog::Identity(record.path);
        auto prepared = PreparePackage(record.path, catalog_cursor);
        ++reads;
        if (prepared && before && before == rb_blitz::shell_music::catalog::Identity(record.path)) {
          record.identity = before;
          record.songs = std::move(prepared->songs);
        }
      }
      if (g_closing) return {};
      PreparedPackage completed;
      completed.catalog = catalog;
      completed.catalog_complete = catalog_cursor == catalog->size();
      if (completed.catalog_complete) {
        std::string why;
        const bool saved = rb_blitz::shell_music::catalog::Save(catalog_path, *catalog, &why);
        REXLOG_INFO("shell_music: background catalog complete; {} packages; saved={} {}",
                    catalog->size(), saved, why);
      }
      REXLOG_DEBUG("shell_music: catalog batch: {} packages read in {} ms ({}/{})",
                   reads, MillisecondsSince(started), catalog_cursor, catalog->size());
      return completed;
    }
    auto prepared = PreparePackage(packages.at(index), index);
    if (prepared) return prepared;
    PreparedPackage failed;
    failed.index = index;
    return failed;  // failures still retire the demand request and discovery slot
  });
  SchedulePreparationLocked();
  REXLOG_INFO("shell_music: rolling preparation started; bounded demand worker, no fixed song cap");
}

void PublishQueuedPool(bool during_loading) {
  std::lock_guard lock(g_mutex);
  // This also runs before the queue starts and on the final loading query.
  // Preparing caches is not evidence that the current music source failed.
  if (during_loading) g_playback.DeferWatchdog(NowMs());
  g_catalog_io_allowed = rb_blitz::input::MainMenuMusicInputAvailable() &&
                         !rb_blitz::graphics::GraphicsLoadingPending();
  if (g_closing || !g_queue_started || g_setting_up_music) return;
  const bool active = during_loading || g_watch || g_watch_title;
  g_queue.Pause(!active);
  if (!active) return;  // no new speculative I/O while gameplay owns audio
  if (!g_ready_package) g_ready_package = g_queue.Take();
  if (g_ready_package && PublishPackage(*g_ready_package)) g_ready_package.reset();
  SchedulePreparationLocked();
}

void CloseQueuedPool() {
  std::lock_guard lock(g_mutex);
  g_closing = true;
  g_catalog_io_allowed = false;
  NowPlaying().Reset();
  g_queue.Stop();
}

std::string SelectedSongKey() {
  std::lock_guard lock(g_mutex);
  const int index = g_play_selection.Query(NowMs());
  return index >= 0 && static_cast<size_t>(index) < g_session.Sources().size()
      ? PoolSongKey(g_session.Sources()[index]) : std::string();
}

bool HasPool() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_enabled && (!g_session.Sources().empty() || !g_packages.empty());
}

std::vector<std::string> PoolSongNames() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> names;
  names.reserve(g_session.Sources().size());
  for (const PoolEntry& entry : g_session.Sources()) {
    names.emplace_back(rb_blitz::shell_music::PoolSongKey(entry));
  }
  return names;
}

int CurrentPoolIndex() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_current_index;
}

bool AnswerPlayQuery(const std::string_view path, bool* answer) {
  if (!path.starts_with(rb_blitz::shell_music::kPlayNamespace)) {
    return false;
  }
  if (path == "rbbz-play/next" || path == "rbbz-play/previous" || path == "rbbz-play/pause") {
    const auto command = path == "rbbz-play/next" ? nav::Command::kNext :
                         path == "rbbz-play/previous" ? nav::Command::kPrevious : nav::Command::kPlayPause;
    const bool allowed = g_navigation_enabled && !rb_blitz::input::capture::Armed() &&
                         !rex::kernel::xam::xeXamIsUIActive();
    const auto result = allowed ? nav::Post(command, nav::Producer::kControlsPage) : nav::PostResult{};
    if (answer) *answer = result.status == nav::PostStatus::kAccepted;
    return true;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (path == "rbbz-play/begin") {
    const int chosen = g_play_selection.Capture(NowMs());
    REXLOG_INFO("shell_music: Play Current Song request: selected={} audible={} pending={} "
                "title_fallback={}", chosen, g_current_index, g_pending_index, g_watch_title);
    if (answer) *answer = chosen >= 0;
    return true;
  }
  const int current = g_play_selection.Query(NowMs());
  if (path == "rbbz-play/result/resolved" || path == "rbbz-play/result/no-track" ||
      path == "rbbz-play/result/missing-name" || path == "rbbz-play/result/missing-data") {
    const std::string key = current >= 0 && static_cast<size_t>(current) < g_session.Sources().size()
        ? PoolSongKey(g_session.Sources()[current]) : std::string();
    REXLOG_INFO("shell_music: Play Current Song result: {} (pool {}, key \"{}\")",
                path.substr(std::string_view("rbbz-play/result/").size()), current, key);
    g_play_selection.Complete();
    if (answer) *answer = true;
    return true;
  }
  const int index = rb_blitz::shell_music::PlayProbeIndex(path);
  const bool hit = index >= 0 && index == current;
  // This confirms selection only; the result query separately records the guest's lookup.
  // A malformed probe in our namespace is answered false and never forwarded.
  if (hit) {
    REXLOG_INFO("shell_music: Play Current Song: selected pool {} of {} (\"{}\")",
                index, g_session.Sources().size(),
                index < static_cast<int>(g_session.Sources().size()) ? PoolSongKey(g_session.Sources()[index])
                                                       : std::string("?"));
  }
  if (answer != nullptr) {
    *answer = hit;
  }
  return true;
}

}  // namespace rb_blitz::shell_music

// Load ABI: this=r3, filename=r4, gain=f1, scale_pan=r6, loop=r7. The config's
// play_from_memory field is separate (ShellMusic+60); r7 controls Stream::SetJump.
extern "C" REX_FUNC(sub_82757F50) {
  SubstituteFilename(base, ctx);
  __imp__sub_82757F50(ctx, base);
}

// Native Start calls this selector before creating the stream.
// Manual Next/Previous/restart must begin at zero; native and automatic starts
// retain their existing random offsets. No authored offset vector is modified.
extern "C" REX_FUNC(sub_82757D40) {
  if (g_nav_start_at_zero && ctx.r3.u32 == g_music) ctx.r3.u32 = 0;
  else __imp__sub_82757D40(ctx, base);
}

// Stopping a menu stream is navigation, not a song end. In particular, do not start
// another background song while the loading screen is taking over the audio system.
void ForgetMenuMusic(uint32_t music) {
  if (music != g_music) return;
  std::string name;
  bool watched = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    watched = g_watch || g_watch_title;
    name = g_playing_name;
    g_watch = false;
    g_watch_title = false;
    g_nav_paused_stream = 0;
    g_queue.Pause(true);
    g_catalog_io_allowed = false;
    g_playback.Stop();
    g_current_index = -1;
    g_pending_index = -1;
    g_play_selection.Stop();
    FinishNavigator(nav::CompletionStatus::kCancelled, "menu music stopped");
    g_nav_epoch = rb_blitz::shell_music::NowPlaying().Reset();
  }
  if (watched) {
    REXLOG_INFO("shell_music: the game took this menu track down; the rotation stops watching "
                "(\"{}\") (R9)",
                name);
  }
}

extern "C" REX_FUNC(sub_82758998) {
  const auto music = ctx.r3.u32;
  ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_82758998(ctx, base);
  std::lock_guard lock(g_mutex);
  if (music == g_music) g_stream_song = rb_blitz::shell_music::kNoIndex;
}

extern "C" REX_FUNC(sub_82758490) {
  const auto music = ctx.r3.u32;
  ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_82758490(ctx, base);
  std::lock_guard lock(g_mutex);
  if (music == g_music) g_stream_song = rb_blitz::shell_music::kNoIndex;
}

// Start builds the track that was just loaded; its body calls StopStreams as cleanup
// (0x82758C28) before the new stream exists. While it runs, that call is not a teardown.
extern "C" REX_FUNC(sub_82758B98) {
  const auto music = ctx.r3.u32;
  if (music == g_music) g_nav_paused_stream = 0;
  const bool was_setting_up = g_setting_up_music;
  g_setting_up_music = true;
  __imp__sub_82758B98(ctx, base);
  {
    std::lock_guard lock(g_mutex);
    if (music == g_music) {
      if (g_watch && g_pending_index >= 0) {
        g_stream_song = size_t(g_pending_index);
        // The panel can stage Load long before it calls Start. Charging that
        // interval to decoding falsely rejects the boot song on a slow boot.
        // Repeated Start calls on the same playing stream must not reset it.
        if (!g_playback.active()) {
          REXLOG_INFO("shell_music: native Start {} ms after Load for '{}' (stream {:08X})",
                      MillisecondsSince(g_track_started_at), g_playing_name,
                      LoadBe32(base, music + kStreamOffset));
          g_playback.Begin(NowMs());
          g_track_started_at = g_heartbeat_at = std::chrono::steady_clock::now();
        }
      }
      else if (g_watch_title) {
        g_stream_song = rb_blitz::shell_music::kNoIndex;
        if (!g_playback.active()) g_playback.Begin(NowMs());
      }
    }
  }
  g_setting_up_music = was_setting_up;
}

// StandardStream::Init (0x823E5748) stores f2 at +48 and resolves zero through
// synth.stream_buf_size. Channel setup (0x823E52B0) sizes its decoded ring from
// that value times sample rate. Only ShellMusic construction gets extra reserve;
// active-song streams and the host output queue retain their original timing.
extern "C" REX_FUNC(sub_823E5BE0) {
  const auto requested = ctx.f2.f64;
  ctx.f2.f64 = rb_blitz::shell_music::StreamBufferSeconds(g_setting_up_music,requested);
  if (ctx.f2.f64 != requested)
    REXLOG_INFO("shell_music: menu stream reserve {} -> {} seconds",requested,ctx.f2.f64);
  __imp__sub_823E5BE0(ctx,base);
}

// Retail ShellMusic retries Play whenever IsPlaying is false and IsReady true;
// paused state 5 satisfies that condition. Only the navigator's explicit pause
// owns this stream; unrelated/native Play calls continue through unchanged.
extern "C" REX_FUNC(sub_823E4808) {
  if (g_nav_paused_stream && ctx.r3.u32 == g_nav_paused_stream &&
      LoadBe32(base, ctx.r3.u32 + 20) == 5) return;
  __imp__sub_823E4808(ctx, base);
}

// StopStreams is how the game lets the menu music go (the faded-out stop finishing, the poll's
// own end-of-track teardown). That is navigation, never a reason to start the next song. The
// host's own handoffs run with g_setting_up_music set and call __imp__ directly, so only a
// guest-driven teardown reaches here.
extern "C" REX_FUNC(sub_827583F8) {
  const auto music = ctx.r3.u32;
  if (!g_setting_up_music) ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_827583F8(ctx, base);
  std::lock_guard lock(g_mutex);
  if (!g_setting_up_music && music == g_music) g_stream_song = rb_blitz::shell_music::kNoIndex;
}

// Poll observes one-shot completion before the guest's Ready/Play branch. Stop and
// destruction are handled separately, so a screen transition cannot advance the deck.
extern "C" REX_FUNC(sub_827586D8) {
  const MusicFaultScope fault_scope(ctx, base, "poll");
  rb_blitz::shell_music::PublishQueuedPool();
  if (auto* settings = rb_blitz::settings::boot::SettingsBoot::Current()) settings->MenuFrame();
  // R7's refreshed DLC library is handed to the content manager here, and this is the
  // only guest hook that runs every frame while the main menu is up - which is where the
  // refresh row lives. The refresh scans on a worker and finishes on its own schedule
  // (src/hooks/dlc.cpp), so the result waits until the next frame after it is ready; this
  // is that frame. The publish is done from a guest thread on purpose: ContentManager's
  // extra_content_library_ has no lock of its own, so Configure() writes it from exactly
  // this kind of context at boot (src/hooks/dlc.h). One atomic compare-exchange when
  // nothing is waiting, so a frame pays nothing for it. Not gated on R9: the title's own
  // menu music runs through this same poll, so it fires with the toggle off too.
  if (rb_blitz::dlc::RefreshArmed()) {
    rb_blitz::dlc::PublishPendingRefresh();
  }
  // R7's frame pacing for the discovery is settled from here too: this poll is proof the menu is
  // up, and the discovery screen is not, which is the one end of the enumeration the counters
  // inside it cannot see. One atomic load when nothing is in flight.
  rb_blitz::content_progress::Tick();
  const uint32_t music = ctx.r3.u32;
  PublishNavigator(base, music);
  if (g_navigation_enabled && !g_nav_probe.empty() && rb_blitz::input::MainMenuMusicInputAvailable()) {
    if (g_nav_menu_since == std::chrono::steady_clock::time_point{}) g_nav_menu_since = std::chrono::steady_clock::now();
    if (MillisecondsSince(g_nav_menu_since) >= int64_t(std::max(0, g_nav_probe_delay)) * 1000) {
      const auto command = g_nav_probe == "next" ? nav::Command::kNext :
                           g_nav_probe == "previous" ? nav::Command::kPrevious : nav::Command::kPlayPause;
      const auto result = nav::Post(command);
      REXLOG_INFO("music_nav: diagnostic {} status {}", g_nav_probe, int(result.status));
      g_nav_probe.clear();
    }
  }
  ApplyNavigatorCommand(base, ctx, music);
  const bool watched = (g_enabled || g_navigation_enabled) && (g_watch || g_watch_title) && !g_session.Sources().empty() &&
                       music != 0 && music == g_music;
  if (watched) {
    const uint32_t stream = LoadBe32(base, music + kStreamOffset);
    const uint32_t state = stream ? LoadBe32(base, stream + 20) : 0;
    const uint32_t position = stream ? LoadBe32(base, stream + kPositionOffset) : 0;
    const bool paused = stream && stream == g_nav_paused_stream && state == 5;
    if (g_nav_paused_stream && !paused) g_nav_paused_stream = 0;
    if (paused) {
      g_playback.DeferWatchdog(NowMs());
      if (g_nav_pause_trace && MillisecondsSince(g_nav_pause_log_at) >= 1000) {
        g_nav_pause_log_at = std::chrono::steady_clock::now();
        REXLOG_INFO("music_nav: paused stream {:08X} state {} samples {} pause_samples {}",
                    stream, state, position, g_nav_pause_position);
      }
    }
    if (g_watch_title) {
      // Temporary title audio fills an empty ready queue. Its sample counter can
      // keep increasing across loops, so recovery must not depend on a rewind.
      // Only a temporary fallback yields here; three consecutive failures still
      // leave the deliberate circuit-breaker fallback in control.
      if (!paused && !g_nav_request && g_waiting_for_pool && HasReadyNext()) {
        REXLOG_INFO("shell_music: DLC successor ready; leaving temporary title fallback");
        g_waiting_for_pool = false;
        g_seam_at = std::chrono::steady_clock::now();
        StartNextTrack(base, ctx, music);
        __imp__sub_827586D8(ctx, base);
        PublishNavigator(base, music);
        return;
      }
      if (g_nav_started && state == 3 && position > 0) {
        PublishNavigator(base, music);
        FinishNavigator(nav::CompletionStatus::kApplied);
      }
      if (g_nav_started && MillisecondsSince(g_track_started_at) >=
          rb_blitz::shell_music::Playback::kStartupDeadlineMs && !(state == 3 && position > 0))
        FinishNavigator(nav::CompletionStatus::kFailed, "title stream failed to start");
      if (state == 6 || (stream == 0 && g_playback.active())) {
        if (g_nav_request)
          FinishNavigator(nav::CompletionStatus::kFailed, "title stream ended before command completion");
        g_watch_title = false;
        REXLOG_WARN("shell_music: the title fallback track ended; the menu's background music "
                    "stops (R9)");
      } else {
        constexpr uint32_t kLoopSlack = 44100;  // ~1 s of samples: below this, a torn read
        if (position + kLoopSlack < g_pos) {
          REXLOG_INFO("shell_music: the title fallback track looped after {:.1f} s of samples "
                      "(R9)",
                      static_cast<double>(g_pos) / kSamplesPerSecond);
        }
        if (MillisecondsSince(g_heartbeat_at) >= kHeartbeatMs) {
          g_heartbeat_at = std::chrono::steady_clock::now();
          REXLOG_INFO("shell_music: the title fallback track is playing at {:.1f} s "
                      "(position {}, state {}) (R9)",
                      static_cast<double>(position) / kSamplesPerSecond, position, state);
        }
      }
      g_pos = position;
    } else {
      // StandardStream's native predicates: IsPlaying == state 3 (0x823E30F0),
      // IsFinished == state 6 (0x823E30D8). Observe EOF before Poll can restart anything.
      // A renderer preparation call can itself block longer than the watchdog,
      // after the loading poll's keepalive. Check the live gate here as well.
      if (rb_blitz::graphics::GraphicsLoadingPending()) g_playback.DeferWatchdog(NowMs());
      const auto event = g_playback.Observe(NowMs(), stream, state == 3, state == 6, position);
      using Event = rb_blitz::shell_music::Playback::Event;
      if (event == Event::kEnded) {
        NoteSeam(g_pos);
        if (g_nav_started)
          FinishNavigator(nav::CompletionStatus::kFailed, "replacement ended before becoming audible");
        if (!g_nav_request) StartNextTrack(base, ctx, music);
      } else if (event == Event::kFailed) {
        REXLOG_WARN("shell_music: native failure snapshot: stream={:08X} state={} position={}",
                    stream, state, position);
        if (g_nav_request) FinishNavigator(nav::CompletionStatus::kFailed, "active or replacement stream failed");
        NoteFailedCandidate(base, ctx, music);
      } else {
        if (event == Event::kAudible) {
          g_attempts = 0;
          {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_current_index = g_pending_index;
            g_play_selection.Audible(g_current_index);
            if (g_current_index >= 0) g_session.Heard(size_t(g_current_index));
          }
          if (g_nav_started) {
            PublishNavigator(base, music);
            FinishNavigator(nav::CompletionStatus::kApplied);
          }
          REXLOG_INFO("shell_music: \"{}\" started playback {} ms after the previous track's end "
                      "(stream {:08X}, source \"{}\") (R9)",
                      g_playing_name, MillisecondsSince(g_seam_at), stream,
                      rex::memory::GuestPtr<const char*>(base, LoadBe32(base, music + 68)));
        }
        g_pos = position;
        if (MillisecondsSince(g_heartbeat_at) >= kHeartbeatMs) {
          g_heartbeat_at = std::chrono::steady_clock::now();
          REXLOG_INFO("shell_music: playing \"{}\" at {:.1f} s (position {}, stream {:08X}, "
                      "state {}) (R9)", g_playing_name,
                      static_cast<double>(position) / kSamplesPerSecond, position, stream, state);
        }
      }
    }
  }
  __imp__sub_827586D8(ctx, base);
  PublishNavigator(base, music);
}

#endif  // _WIN32
