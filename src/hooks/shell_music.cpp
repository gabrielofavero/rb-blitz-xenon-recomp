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
//   * when the panel is entered (0x822B7968) it picks one entry at random
//     (0x822B7550, a `[0, size)` pick), formats `sprintf("sfx/shell/%s", entry)`,
//     reads that entry's `gain`, makes a `ShellMusic` and calls
//     `ShellMusic::Load(path, gain, loop=1, play_from_buffer=1)` (0x82757F50). `Load`
//     streams the path; the three entries resolve to
//     `sfx/shell/samples/shellmusic_1..3.mogg`, three stereo tracks in the ark.
//   * the panel's per-frame poll (0x827586D8) then drives that stream, and the stream
//     loops: the `loop` argument only scales a fade at the seam, it does not stop the
//     loop. The title therefore plays ONE of its three tracks per menu visit and
//     repeats it until the menu is left; nothing asks for a next track on its own.
//
// R9 widens the pool and gives the menu a jukebox rotation of it:
//
//   * the pool is the songs this boot found in the DLC it mounted - **every** `.mogg` a
//     package holds, not just the first. A downloaded song is usually one package with one
//     song, but a merged pack is one container holding a whole disc's worth of them, and
//     taking only its first would pin the menu to whichever song sorted first, every boot.
//     The songs are sampled at random from the whole library and the sample is redrawn
//     every boot (Configure() below), so the menu is different every time;
//   * `ShellMusic::Load` is hooked, so every pooled track the panel asks for becomes
//     one of those songs;
//   * the poll is hooked too. The stream it drives carries its own play position
//     (`Stream+0xA0`, in samples, advancing with the audio), and at the seam where the
//     song repeats that position jumps **backwards**. A position that went backwards on
//     the object R9 substituted into is therefore the end of one of its songs, and the
//     moment R9 hands the same ShellMusic the next song of the shuffled pool. The pool
//     is deliberately small: the walk mounts only as many containers as it needs to fill
//     it, and the deck is reshuffled when it runs out.
//
// Where a DLC song comes from: the title opens its downloadable songs from a mounted
// content device (`cntXXXXXXXX:\songs\<key>\<key>.<ext>`) and never from `sfx/shell/`,
// so a path in the synth config cannot name one. This layer therefore mounts the packages
// rb_blitz::dlc::Configure() enumerated that carry songs, read-only, as devices of its own
// (menumusic0:, menumusic1:, ...), and hands ShellMusic a path into one of them. Nothing is
// copied, extracted or written under game/.
//
// Hook hygiene (docs/rb3-references.md §8): the faithful behaviour, and the deviation.
//
// Faithful behaviour: with R9 off - its default - Configure() mounts nothing and the
// hook is inert, so the boot is the one this project had before R9 existed: the menu
// loops the title's own three tracks. The same is true with R9 on and no DLC library,
// or when none of the chosen packages can be mounted or holds a stream: nothing joins
// the pool, so the hook has nothing to substitute and every read is the guest's own.
//
// Deviation: with R9 on and at least one package mounted, each pooled track is replaced,
// at the load that carries it, with `<alias>:\songs\<song>\<song>` - a different song of
// the same kind (a streamed .mogg), played with the same gain, loop and FX the panel
// already chose. The poll hook watches that stream's own position and, at the seam where
// the song would repeat, loads the next song of the pool onto the same ShellMusic, so the
// menu walks its library instead of repeating one track. Every substitution and every
// turn of the deck is logged with the path the title asked for and the one it is given
// instead.

#include "hooks/shell_music.h"

#include "hooks/menu_music_pool.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <numeric>
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
#include "hooks/content_progress.h"
#include "settings/menu_compat.h"
#include "settings/boot.h"

#if defined(_WIN32)

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table.
REXCVAR_DECLARE(bool, enhancements_menu_dlc_songs);
REXCVAR_DECLARE(int32_t, enhancements_menu_dlc_song_count);

REX_EXTERN(__imp__sub_82757F50);
REX_EXTERN(__imp__sub_827586D8);

namespace {

// The pool's bookkeeping lives in a header of its own so it can be tested without the SDK
// (tests/menu_music_pool_tests.cpp): the sample and the deck are host logic, not hooks.
// (BuildPool is called from the namespace below, which finds it itself.)
using rb_blitz::shell_music::Deck;
using rb_blitz::shell_music::PoolEntry;

// The instance `ShellMusic::Load` is hooked at (0x82757F50), and the prefix the panel
// builds every pool entry's path with (`sprintf("sfx/shell/%s", ...)`).
constexpr char kShellMusicPrefix[] = "sfx/shell/";
constexpr size_t kShellMusicPrefixSize = sizeof(kShellMusicPrefix) - 1;

// How many songs the pool holds at most - and, because one package can hold any number of
// songs, how many containers it is reasonable to hold open at once: each mounted package is
// one file handle and its own block-hash cache, and the walk mounts no more than this.
constexpr int kMaxSongs = 64;

// One guest string buffer per substitution, cycled. Several are kept because the
// engine may still be holding the previous path when the next pool entry arrives: the
// panel asks for the next track as the current one ends.
constexpr size_t kBufferCount = 8;
constexpr size_t kBufferBytes = 512;

// ShellMusic's own fields (its ctor is 0x82758208): the stream it plays, and - on that
// stream - the sample position it has reached. The panel's poll loops the stream rather
// than ending it, so a position that jumps backwards is the only signal that a track has
// reached its end, and it is what R9 rotates on.
constexpr uint32_t kStreamOffset = 44;
constexpr uint32_t kPositionOffset = 0xA0;
// A backwards jump smaller than this is a torn read of a counter the audio thread is
// writing, not a seam: a seam restarts near zero and is minutes of samples away.
constexpr uint32_t kRewindSlack = 44100;  // ~1 s at 44.1 kHz

bool g_enabled = false;
std::vector<PoolEntry> g_pool;

std::mutex g_mutex;
std::mt19937 g_rng{std::random_device{}()};

// The play order of the pool, walked to the end and then reshuffled. A deck rather than
// a per-load pick: every song gets heard before any repeats, and the song after the last
// one is never the last one again (src/hooks/menu_music_pool.h).
Deck g_deck{g_rng};

// The ShellMusic the last substitution went to, and the Load arguments the panel passed
// it - the poll hook replays them when it starts the next track. Zero outside R9.
uint32_t g_music = 0;
double g_gain = 0.0;
uint64_t g_loop = 1;
uint64_t g_play_from_buffer = 1;

// The stream R9 is watching, and the last position read from it, so a jump backwards can
// be told from the first sample of a track. Reset with every track R9 starts.
uint32_t g_stream = 0;
uint32_t g_pos = 0;

std::vector<uint32_t> g_buffers;
size_t g_buffer_next = 0;

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

// The next song to play: the next card of the shuffled deck, reshuffled when the deck
// runs out so that the first song of a new deck is never the song that just played.
// The caller holds g_mutex.
std::string NextPathLocked() {
  const size_t index = g_deck.Next(g_pool.size());
  if (index == rb_blitz::shell_music::kNoIndex) {
    return {};
  }
  return g_pool[index].alias + ":" + g_pool[index].stream;
}

// Hand the ShellMusic the menu is already polling the next song of the deck. This is the
// same call the panel's own pool loader makes - a `ShellMusic::Load` on a live object - with
// the volume, loop and play-from-buffer flags the panel chose for the pool entry it
// substituted (the stream loops either way, so the poll hook below is what notices the song
// ending). Runs on the guest thread, from inside that poll, and takes no locks while calling.
void StartNextTrack(uint8_t* base, PPCContext& parent, uint32_t music) {
  std::string path;
  uint32_t buffer = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    path = NextPathLocked();
    if (!g_buffers.empty()) {
      buffer = g_buffers[g_buffer_next];
      g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
    }
  }
  if (path.empty() || buffer == 0 || path.size() + 1 > kBufferBytes) {
    return;
  }
  std::memcpy(rex::memory::GuestPtr<uint8_t*>(base, buffer), path.c_str(), path.size() + 1);

  PPCContext call(parent);
  call.r1.u32 -= 0x100;              // a scratch frame on the guest's own stack
  call.r3.u32 = music;               // this
  call.r4.u32 = buffer;              // filename
  call.f1.f64 = g_gain;              // volume, the one the panel passed
  call.r6.u64 = g_loop;              // the panel's own loop flag
  call.r7.u64 = g_play_from_buffer;  // and its play-from-buffer flag
  __imp__sub_82757F50(call, base);
  g_stream = 0;  // the next poll baselines the new stream's position
  g_pos = 0;
  REXLOG_INFO("shell_music: the menu track ended; playing \"{}.mogg\" next (R9)", path);
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
  // Remember the object and the arguments the panel chose: the poll hook starts every
  // later track of the rotation the same way, when the song it started comes round again.
  g_music = ctx.r3.u32;
  g_gain = ctx.f1.f64;
  g_loop = ctx.r6.u64;
  g_play_from_buffer = ctx.r7.u64;
  g_stream = 0;
  g_pos = 0;
  REXLOG_INFO("shell_music: the main menu asked for \"{}\"; playing \"{}.mogg\" instead (R9)",
              asked, path);
}

}  // namespace

namespace rb_blitz::shell_music {

void Configure(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  (void)game_data_root;
  // P10: the pool is a fixed 50 songs (§3.5), capped by what this boot can mount. Both lines are
  // said *before* the early returns, so a boot that never mounts a package still reports the fixed
  // cap and that a retired `enhancements_menu_dlc_song_count` is ignored instead of obeyed.
  const size_t cap = rb_blitz::settings::menu_compat::PoolCap(kMaxSongs);
  REXLOG_INFO("shell_music: the menu's DLC pool holds up to {} song(s) (fixed, plan 3.5)", cap);
  const std::string legacy_note = rb_blitz::settings::menu_compat::LegacyPoolCountNote(
      REXCVAR_GET(enhancements_menu_dlc_song_count));
  if (!legacy_note.empty()) {
    REXLOG_WARN("shell_music: {}", legacy_note);
  }
  // The DLC pool is **always on** (owner decision, 2026-10-13): "DLC Menu Songs" is retired as a
  // setting, so every DLC song the library yields belongs in the menu rotation. The legacy cvar is
  // still read, only so a boot says out loud that its value no longer decides anything.
  g_enabled = true;
  if (!REXCVAR_GET(enhancements_menu_dlc_songs)) {
    REXLOG_INFO("shell_music: enhancements_menu_dlc_songs is retired; the DLC pool is always on");
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

  // P10: the pool is a fixed 50 songs (§3.5), capped by what this boot can actually mount. The
  // retired `enhancements_menu_dlc_song_count` is read only to say it is ignored - a hand-edited
  // config must not silently re-enable a size the product no longer offers.
  const size_t total = packages.size();

  // Draw packages at random from the whole library, and collect every song each one holds.
  // A package is one open container, so the walk stops as soon as there are enough songs to
  // fill the pool and never mounts more than `cap` of them: a merged pack fills the pool from
  // a single handle, while a library of one-song packages is mounted as many times as the
  // pool is deep. The old index stride picked the same packages in the same order on every
  // run; a shuffle of the library's indices is a distinct sample by construction.
  std::vector<size_t> order(total);
  std::iota(order.begin(), order.end(), size_t{0});
  std::shuffle(order.begin(), order.end(), g_rng);

  std::vector<PoolEntry> candidates;
  size_t mounts = 0;
  for (size_t i = 0; i < order.size() && candidates.size() < cap && mounts < cap; ++i) {
    const std::filesystem::path& package = packages[order[i]];
    const std::string mount_path = fmt::format("\\Device\\BlitzMenuMusic\\{}\\", mounts);
    std::error_code ec;
    std::unique_ptr<rex::filesystem::Device> device = MountPackage(mount_path, package, ec);
    if (device == nullptr) {
      REXLOG_WARN("shell_music: cannot mount {} as DLC menu music ({})", package.string(),
                  ec ? ec.message() : "not a mountable package");
      continue;
    }
    std::vector<std::string> streams = FindSongStreams(*device);
    if (streams.empty()) {
      REXLOG_WARN("shell_music: {} holds no streamable .mogg, skipping it", package.string());
      continue;
    }
    const std::string alias = fmt::format("menumusic{}", mounts);
    if (!file_system->RegisterDevice(std::move(device))) {
      REXLOG_WARN("shell_music: cannot register the device for {}", package.string());
      continue;
    }
    if (!file_system->RegisterSymbolicLink(alias + ":", mount_path)) {
      REXLOG_WARN("shell_music: cannot point {}: at {}", alias, mount_path);
      continue;
    }
    ++mounts;
    for (std::string& stream : streams) {
      candidates.push_back({alias, std::move(stream)});
    }
  }

  // The pool is a random sample of those songs, redrawn every boot, with a song that two
  // packages both carry kept once. Shuffling the collected songs rather than cutting them in
  // walk order keeps one large pack from always opening with the same track
  // (src/hooks/menu_music_pool.h).
  g_pool = BuildPool(std::move(candidates), cap, g_rng);

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

  REXLOG_INFO("shell_music: R9 plays the loaded DLC in the main menu: {} song(s) drawn at random "
              "from {} mounted package(s) of {} (pool cap {})",
              g_pool.size(), mounts, total, cap);
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

// 0x827586D8 (ShellMusic::Poll): the panel calls it every frame while the menu is up, and
// it drives the track. The stream it plays loops - the `loop` argument only scales a fade
// at the seam, it does not end it - so the song repeats for as long as the menu is open.
// The stream carries its own position (`Stream+0xA0`, in samples, advancing with the
// audio), and at the seam that position restarts near zero. A position that went
// backwards on the object R9 substituted into is therefore the end of one of R9's songs
// and the cue to load the next one. Any other ShellMusic has a different `this` and is
// left alone; a stream that is already gone - the panel stopped the music on its way out -
// is ignored too, because a stream R9 never saw cannot have jumped.
extern "C" REX_FUNC(sub_827586D8) {
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
  const bool watched = g_enabled && !g_pool.empty() && music != 0 && music == g_music;
  uint32_t before = 0;
  if (watched) {
    before = LoadBe32(base, music + kStreamOffset);
  }
  __imp__sub_827586D8(ctx, base);
  if (!watched) {
    return;
  }
  const uint32_t stream = LoadBe32(base, music + kStreamOffset);
  if (stream == 0) {
    // The title's own end-of-track path tore the stream down instead of looping it.
    if (before != 0) {
      StartNextTrack(base, ctx, music);
    }
    return;
  }
  const uint32_t position = LoadBe32(base, stream + kPositionOffset);
  if (stream != g_stream) {
    g_stream = stream;
    g_pos = position;
    return;
  }
  if (position + kRewindSlack < g_pos) {
    StartNextTrack(base, ctx, music);
    return;
  }
  g_pos = position;
}

#endif  // _WIN32
