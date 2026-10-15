// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// R9: change the main menu's background songs for loaded DLC ones.
//
// What the title does by itself (recovered from the image; see
// docs/engine/main-menu.md (part 2, the background music) for the addresses and the evidence):
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
// Hook hygiene (docs/reference/rb3-references.md §8): the faithful behaviour, and the deviation.
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
#include <chrono>
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

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table. Both are read here: the
// first is the carrier of the in-game "DLC Menu Songs" row (the boot writes the row into it at the
// config rank), and the second is the retired pool-size row, read only to say it is ignored.
REXCVAR_DECLARE(bool, enhancements_menu_dlc_songs);
REXCVAR_DECLARE(int32_t, enhancements_menu_dlc_song_count);

REX_EXTERN(__imp__sub_82757F50);
REX_EXTERN(__imp__sub_827586D8);

namespace {

// The pool's bookkeeping lives in a header of its own so it can be tested without the SDK
// (tests/menu_music_pool_tests.cpp): the sample and the rotation are host logic, not hooks.
// (BuildPool and Rotation are named in full from the namespace below.)
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
// The position counts samples and these mixdowns run at the standard rate (measured
// 2026-10-10: ~44,100 counts a second), so a position is also a duration in seconds.
constexpr double kSamplesPerSecond = 44100.0;

bool g_enabled = false;
std::vector<PoolEntry> g_pool;

// The setting's own value, as read in Configure(): true when `general.dlc_menu_songs` (the revived
// "DLC Menu Songs" row) asked for the pool this boot. `g_enabled` is the stricter fact - the
// setting *and* a pool that could actually be mounted - and the two differ exactly when the
// setting is on with nothing to play (no library, or no mountable song). The settings screen reads
// both to say which of the two states a boot is in.
bool g_menu_music_enabled = false;

// The pool index of the track the menu is playing right now (-1 before one has been chosen, and
// with no pool). Set wherever a track is picked for the deck, so both ways a track starts - the
// panel's own load and R9's rotation - move it. The "Play Current Song" probe channel answers from
// it; never read outside g_mutex.
int g_current_index = -1;

std::mutex g_mutex;
std::mt19937 g_rng{std::random_device{}()};

// The play order of the pool, walked to the end and then reshuffled. A rotation rather
// than a per-load pick: every song gets heard before any repeats, the song after the last
// one is never the last one again, and a song whose load failed steps aside
// (src/hooks/menu_music_pool.h).
rb_blitz::shell_music::Rotation g_rotation{g_rng};

// How long a track R9 started may stay silent before the candidate is treated as a failed load,
// and how many candidates in a row it tries before handing the menu back to the title's tracks.
constexpr int64_t kAudibleDeadlineMs = 3000;
constexpr int kMaxAttemptsPerSeam = 3;

// True while the track playing on `g_music` is one of R9's own. Once the rotation hands the menu a
// title track instead - a pool of one song, or every candidate failing - the poll stops watching,
// because from that point on the title is playing its own music and loops it exactly as it does
// without this feature.
bool g_watch = false;
int g_attempts = 0;

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

// The transition timeline (P5). The guest offers exactly two observable moments: the stream's
// position jumping backwards at the seam (the song ended and the stream restarted) and a fresh
// stream's position advancing from zero (the next song is really being played). Both are logged
// with wall-clock deltas, so a transition's cost is a measurement rather than an impression, and
// the last position read before a seam doubles as the song's own length - neither the pool nor a
// package carries a duration the host could read.
std::chrono::steady_clock::time_point g_track_started_at{};
std::chrono::steady_clock::time_point g_seam_at{};
std::string g_playing_name;      // the song key of the track R9 last handed the menu
bool g_awaiting_audible = false; // a track was started; its first samples have not been seen yet
uint32_t g_seam_samples = 0;     // the position the ended song reached, in samples
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

// The song key of a pool path, for the timeline log lines.
std::string KeyOf(const std::string& path) {
  return std::string(rb_blitz::shell_music::SongName(path));
}

// A seam was seen on the track that is playing: the guest restarted it, which is the only
// "this song ended" signal it offers. Recorded before the next track is loaded, because the
// wait the player hears is the distance between this instant and the next track's first samples.
void NoteSeam(uint32_t samples) {
  g_seam_at = std::chrono::steady_clock::now();
  g_seam_samples = samples;
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

// The next song to play: the next song of the rotation, which hears every pooled song before it
// repeats one and never hands back the song that just played. Also records which index that is,
// because the index is what "Play Current Song" asks for. An empty path means the rotation has
// nothing distinct left to offer (the caller then decides what the menu hears instead).
// The caller holds g_mutex.
std::string NextPathLocked() {
  const size_t index = g_rotation.Next();
  if (index == rb_blitz::shell_music::kNoIndex) {
    g_current_index = -1;
    return {};
  }
  g_current_index = static_cast<int>(index);
  return g_pool[index].alias + ":" + g_pool[index].stream;
}

// The title's own pool entries, as the panel's own loader spells them (`sprintf("sfx/shell/%s")`
// over the three `shellmusic_N` entries of config/synth.dtb). Handing one of these back is how the
// menu gets distinct music once the rotation has run out instead of a loop of one song.
constexpr const char* kTitleTracks[] = {"sfx/shell/samples/shellmusic_1",
                                        "sfx/shell/samples/shellmusic_2",
                                        "sfx/shell/samples/shellmusic_3"};

// Hand `path` to the same ShellMusic the menu is polling, with the Load arguments the panel chose
// for its own pool entry. Runs on the guest thread from inside the poll and takes no lock while
// calling into the guest.
void LoadOnGuest(uint8_t* base, PPCContext& parent, uint32_t music, uint32_t buffer,
                 const std::string& path) {
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
  g_track_started_at = std::chrono::steady_clock::now();
  g_heartbeat_at = g_track_started_at;
  g_awaiting_audible = true;
}

// Hand the ShellMusic the menu is already polling the next song of the rotation. This is the
// same call the panel's own pool loader makes - a `ShellMusic::Load` on a live object - with
// the volume, loop and play-from-buffer flags the panel chose for the pool entry it
// substituted (the stream loops either way, so the poll hook below is what notices the song
// ending). When the rotation has nothing distinct left, the menu is given one of the title's own
// three tracks instead, which is what stops a one-song pool from looping that one song forever.
void StartNextTrack(uint8_t* base, PPCContext& parent, uint32_t music) {
  std::string path;
  uint32_t buffer = 0;
  bool title_track = false;
  bool new_pass = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    path = NextPathLocked();
    new_pass = g_rotation.reshuffled();
    if (path.empty() && !g_pool.empty()) {
      std::uniform_int_distribution<size_t> pick(0, 2);
      path = kTitleTracks[pick(g_rng)];
      title_track = true;
    }
    if (!path.empty() && !g_buffers.empty()) {
      buffer = g_buffers[g_buffer_next];
      g_buffer_next = (g_buffer_next + 1) % g_buffers.size();
    }
    g_playing_name = title_track ? std::string() : KeyOf(path);
  }
  if (path.empty() || buffer == 0 || path.size() + 1 > kBufferBytes) {
    return;
  }
  if (title_track) {
    g_watch = false;  // the title's own music loops; the rotation has nothing left to do
    REXLOG_INFO("shell_music: the DLC pool has nothing new to play; the menu goes back to the "
                "title's own tracks (\"{}.mogg\") (R9)",
                path);
  } else {
    g_watch = true;
    REXLOG_INFO("shell_music: the menu track ended; playing \"{}.mogg\" next (R9), handed over "
                "{} ms after the seam{}",
                path, MillisecondsSince(g_seam_at),
                new_pass ? fmt::format(", opening a new pass over the {} song(s) of the pool",
                                       g_pool.size())
                         : std::string());
  }
  LoadOnGuest(base, parent, music, buffer, path);
}

// A candidate whose load failed: it sits out the rest of this rotation, the menu gets the next
// song, and a run of failures ends the rotation for this boot rather than spinning on it.
void NoteFailedCandidate(uint8_t* base, PPCContext& parent, uint32_t music) {
  std::string failed;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    failed = g_playing_name;
    if (!failed.empty()) {
      g_rotation.MarkFailed(failed);
      g_playing_name.clear();
    }
    g_current_index = -1;
  }
  REXLOG_WARN("shell_music: \"{}\" produced no samples {} ms after it was started; it sits out "
              "the rest of this rotation (R9)",
              failed, MillisecondsSince(g_track_started_at));
  if (++g_attempts >= kMaxAttemptsPerSeam && !g_pool.empty()) {
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
      g_watch = false;
      LoadOnGuest(base, parent, music, buffer, path);
    }
    return;
  }
  g_watch = true;
  StartNextTrack(base, parent, music);
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
  size_t pool_size = 0;
  int index = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_playing_name = KeyOf(path);
    pool_size = g_pool.size();
    index = g_current_index;
  }
  g_track_started_at = std::chrono::steady_clock::now();
  g_seam_at = g_track_started_at;
  g_heartbeat_at = g_track_started_at;
  g_awaiting_audible = true;
  g_watch = true;
  g_attempts = 0;
  REXLOG_INFO("shell_music: the main menu asked for \"{}\"; playing \"{}.mogg\" instead (R9) "
              "(pool {} of {})",
              asked, path, index, pool_size);
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
  // The main menu's DLC pool is a **setting** again (owner request, 2026-10-14): "DLC Menu Songs"
  // - `general.dlc_menu_songs`, drawn on the merged surface's Downloadable Content page - is its one
  // control. The store row is written into `enhancements_menu_dlc_songs` by the boot at the config
  // rank (SettingsBoot::ConfigurePaths), which has already run by the time this hook does, and this
  // reads that cvar - so a command-line flag or an environment value still outranks the row, exactly
  // like every other enhancement row. Both defaults are `true`, which is the behaviour R9 has had
  // since 2026-10-13, so reviving the row changes no existing boot.
  //
  // The read happens once, on this host thread; no guest thread ever touches the cvar registry.
  g_menu_music_enabled = REXCVAR_GET(enhancements_menu_dlc_songs);
  // The index only means something for this boot's pool, and this is the one place the pool is
  // built: a fresh boot has no current track until the panel picks one.
  g_current_index = -1;
  g_enabled = g_menu_music_enabled;
  REXLOG_INFO("shell_music: \"DLC Menu Songs\" is {} (enhancements_menu_dlc_songs = {}); {}",
              g_menu_music_enabled ? "on" : "off",
              REXCVAR_GET(enhancements_menu_dlc_songs) ? "true" : "false",
              g_menu_music_enabled ? "building the DLC pool" : "the menu keeps the title's tracks");
  if (!g_menu_music_enabled) {
    // The off state is the whole feature: nothing is mounted, the loader and poll hooks stay inert,
    // and the boot is the one the title ships. `g_enabled` is already false, so a poll that arrives
    // anyway watches nothing.
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
    g_current_index = -1;
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
    g_current_index = -1;
    return;
  }

  // Hand the rotation the pool's song keys. Identity is the song, not the index: a pool rebuilt
  // later (a refresh) keeps the history, so a song that has just been heard is not new again
  // because the list around it changed shape (src/hooks/menu_music_pool.h).
  {
    std::vector<std::string> keys;
    keys.reserve(g_pool.size());
    for (const PoolEntry& entry : g_pool) {
      keys.emplace_back(rb_blitz::shell_music::SongName(entry.stream));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_rotation.SetPool(std::move(keys));
    g_watch = false;
    g_attempts = 0;
    g_playing_name.clear();
    g_current_index = -1;
  }

  REXLOG_INFO("shell_music: R9 plays the loaded DLC in the main menu: {} song(s) drawn at random "
              "from {} mounted package(s) of {} (pool cap {})",
              g_pool.size(), mounts, total, cap);
  for (const PoolEntry& entry : g_pool) {
    REXLOG_INFO("shell_music:   {}:{}.mogg", entry.alias, entry.stream);
  }
}

bool MenuMusicEnabled() { return g_menu_music_enabled; }

bool HasPool() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_enabled && !g_pool.empty();
}

std::vector<std::string> PoolSongNames() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<std::string> names;
  names.reserve(g_pool.size());
  for (const PoolEntry& entry : g_pool) {
    names.emplace_back(rb_blitz::shell_music::SongName(entry.stream));
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
  const int index = rb_blitz::shell_music::PlayProbeIndex(path);
  const int current = CurrentPoolIndex();
  const bool hit = index >= 0 && index == current;
  // The probe channel is the only thing the menu's Play Current Song row says to the host, so the
  // one line that matters is written here: which song was asked for, and whether it is the one
  // playing. A malformed probe in our namespace is answered false and never forwarded.
  if (hit) {
    const std::vector<std::string> names = PoolSongNames();
    REXLOG_INFO("shell_music: Play Current Song: the menu is playing pool {} of {} (\"{}\")",
                index, names.size(),
                index < static_cast<int>(names.size()) ? names[static_cast<size_t>(index)]
                                                       : std::string("?"));
  }
  if (answer != nullptr) {
    *answer = hit;
  }
  return true;
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
  const bool watched =
      g_enabled && g_watch && !g_pool.empty() && music != 0 && music == g_music;
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
      NoteSeam(g_pos);
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
  // One line a minute while a track plays: the position advancing is the audible track, and its
  // last value before a seam is the song's length.
  if (MillisecondsSince(g_heartbeat_at) >= kHeartbeatMs) {
    g_heartbeat_at = std::chrono::steady_clock::now();
    REXLOG_INFO("shell_music: playing \"{}\" at {:.1f} s (position {}) (R9)", g_playing_name,
                static_cast<double>(position) / kSamplesPerSecond, position);
  }
  // The handover is complete when the track R9 just started is really producing samples: this is
  // the moment the player hears the new song, and the distance from the seam is the transition.
  if (g_awaiting_audible && position > 0) {
    g_awaiting_audible = false;
    g_attempts = 0;
    REXLOG_INFO("shell_music: \"{}\" is audible {} ms after the previous track's end (R9)",
                g_playing_name, MillisecondsSince(g_seam_at));
  }
  // A candidate that never produced a sample was a load that did not work: it steps out of the
  // rotation and the menu gets the next song (bounded - a run of failures ends the rotation for
  // this boot). Measured rather than assumed: the deadline is generous next to the ~30-300 ms a
  // working handover takes on this machine.
  if (g_awaiting_audible && MillisecondsSince(g_track_started_at) > kAudibleDeadlineMs) {
    NoteFailedCandidate(base, ctx, music);
    return;
  }
  if (position + kRewindSlack < g_pos) {
    NoteSeam(g_pos);
    StartNextTrack(base, ctx, music);
    return;
  }
  g_pos = position;
}

#endif  // _WIN32
