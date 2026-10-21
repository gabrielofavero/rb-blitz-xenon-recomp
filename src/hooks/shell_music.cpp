// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// DLC menu music uses the title's ShellMusic player with file streaming and native
// looping disabled. At EOF, retire the active Stream, load the next source and Start it.
// Load alone does not replace a playing Stream: that was the cause of the apparent
// rotation logging different names while the same audio kept playing.
//
// The bounded pool contains up to 50 distinct songs from mounted DLC. Rotation history
// uses canonical song identities. Play Current Song publishes only a source producing
// samples, and menu exit clears it without starting another track.
//
// Guest ABI and runtime evidence: docs/engine/main-menu.md, part 2, 2026-10-15 correction.
// With DLC Menu Songs off, the guest loader, loop and navigation remain unchanged.

#include "hooks/shell_music.h"

#include "hooks/menu_music_pool.h"
#include "hooks/menu_music_playback.h"
#include "hooks/menu_music_metadata.h"

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

#if defined(_WIN32)

// Defined by src/enhancements.cpp, which owns the `[enhancements]` table. Both are read here: the
// first is the carrier of the in-game "DLC Menu Songs" row (the boot writes the row into it at the
// config rank), and the second is the retired pool-size row, read only to say it is ignored.
REXCVAR_DECLARE(bool, enhancements_menu_dlc_songs);
REXCVAR_DECLARE(int32_t, enhancements_menu_dlc_song_count);

REX_EXTERN(__imp__sub_82757F50);
REX_EXTERN(__imp__sub_827586D8);
REX_EXTERN(__imp__sub_82758B98);  // ShellMusic::Start
REX_EXTERN(__imp__sub_827583F8);  // ShellMusic::StopStreams
REX_EXTERN(__imp__sub_82758998);  // ShellMusic::Stop (menu exit)
REX_EXTERN(__imp__sub_82758490);  // ShellMusic destructor

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

// Verified in the retail image: ShellMusic+44 owns StandardStream, whose +0xA0
// counts source samples. EOF is StandardStream state 6, not a guessed duration.
constexpr uint32_t kStreamOffset = 44;
constexpr uint32_t kPositionOffset = 0xA0;
constexpr double kSamplesPerSecond = 44100.0;  // diagnostic display for these mixdowns

bool g_enabled = false;
std::vector<PoolEntry> g_pool;

// The setting's own value, as read in Configure(): true when `general.dlc_menu_songs` (the revived
// "DLC Menu Songs" row) asked for the pool this boot. `g_enabled` is the stricter fact - the
// setting *and* a pool that could actually be mounted - and the two differ exactly when the
// setting is on with nothing to play (no library, or no mountable song). The settings screen reads
// both to say which of the two states a boot is in.
bool g_menu_music_enabled = false;

// Audible and loading indices are separate. PlaySelection snapshots a press and bridges the
// short handoff without claiming a loading stream is audible. Never read outside g_mutex.
int g_current_index = -1;
int g_pending_index = -1;
rb_blitz::shell_music::PlaySelection g_play_selection;

std::mutex g_mutex;
std::mt19937 g_rng{std::random_device{}()};

// The play order of the pool, walked to the end and then reshuffled. A rotation rather
// than a per-load pick: every song gets heard before any repeats, the song after the last
// one is never the last one again, and a song whose load failed steps aside
// (src/hooks/menu_music_pool.h).
rb_blitz::shell_music::Rotation g_rotation{g_rng};

// Playback bounds startup and stalled progress separately. Limit consecutive attempts before
// handing the menu back to the title's tracks.
constexpr int kMaxAttemptsPerSeam = 3;
rb_blitz::shell_music::Playback g_playback;

// True while the track playing on `g_music` is one of R9's own. Once the rotation hands the menu a
// title track instead - a pool of one song, or every candidate failing - the poll stops watching,
// because from that point on the title is playing its own looping music.
bool g_watch = false;
int g_attempts = 0;

// True while the track playing on `g_music` is a title fallback (the pool had nothing
// distinct left). The poll reports it - heartbeats, and the loop it takes when the title
// track wraps - but never rotates: the title's own music is the title's own business.
bool g_watch_title = false;

// True while the host or ShellMusic::Start is assembling a track. Both call StopStreams as
// cleanup before the new stream exists (Start does it inside its own body), and that cleanup
// must not be read as the game tearing the menu music down. Never read outside the guest thread.
bool g_setting_up_music = false;

// Set by R7's refresh stand-down and cleared by the menu frame that starts the rotation's next
// song. The menu's own re-entry does *not* reload the pool after the refresh pass (measured
// 2026-10-16: no `the main menu asked for` line follows the pass), so this layer owes the
// resume; the delay keeps a poll that slips through the screen transition from starting music
// into a screen that is already leaving.
bool g_stand_down = false;
std::chrono::steady_clock::time_point g_stand_down_at{};
constexpr int64_t kResumeAfterStandDownMs = 1500;

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

// The ShellMusic the menu's own poll last reported (`sub_827586D8`'s `this`). Learned with R9 on
// or off, because R7's refresh press takes whatever the menu is playing down - the title's own
// track included - so nothing is left repeating while the press blocks the frame.
uint32_t g_menu_music = 0;

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

// The next song to play: the next song of the rotation, which hears every pooled song before it
// repeats one and never hands back the song that just played. Also records which index that is,
// because the index is what "Play Current Song" asks for. An empty path means the rotation has
// nothing distinct left to offer (the caller then decides what the menu hears instead).
// The caller holds g_mutex.
std::string NextPathLocked() {
  const size_t index = g_rotation.Next();
  if (index == rb_blitz::shell_music::kNoIndex) {
    g_pending_index = -1;
    g_current_index = -1;
    g_play_selection.Stop();
    return {};
  }
  g_pending_index = static_cast<int>(index);
  g_current_index = -1;  // publish only after this stream starts advancing
  g_play_selection.Loading(g_pending_index, NowMs());
  return g_pool[index].alias + ":" + g_pool[index].stream;
}

// The title's own pool entries, as the panel's own loader spells them (`sprintf("sfx/shell/%s")`
// over the three `shellmusic_N` entries of config/synth.dtb). Handing one of these back is how the
// menu gets distinct music once the rotation has run out instead of a loop of one song.
constexpr const char* kTitleTracks[] = {"sfx/shell/samples/shellmusic_1",
                                        "sfx/shell/samples/shellmusic_2",
                                        "sfx/shell/samples/shellmusic_3"};

// Retire the stream a ShellMusic currently holds, the way the engine's own Start does
// (0x82758C08): the stream's own destructor with the "remove from the pool" flag, the field
// cleared, then StopStreams. Shared by the rotation's handover and R7's refresh stand-down, which
// must leave nothing playing that a blocked frame cannot advance.
void RetireStreamOnGuest(uint8_t* base, PPCContext& parent, uint32_t music) {
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
    rex::runtime::ResolveIndirectFunction(destructor)(destroy, base);
  }
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, music + kStreamOffset), 0, 4);
  PPCContext stop(parent);
  stop.r1.u32 -= 0x100;
  stop.r3.u32 = music;
  __imp__sub_827583F8(stop, base);
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
  __imp__sub_82757F50(call, base);
  *rex::memory::GuestPtr<uint8_t*>(base, music + 48) = loop ? 1 : 0;  // same field, read back
  PPCContext start(parent);
  start.r1.u32 -= 0x100;
  start.r3.u32 = music;
  __imp__sub_82758B98(start, base);
  g_pos = 0;
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
    g_playing_name = g_pending_index >= 0
        ? rb_blitz::shell_music::PoolSongKey(g_pool[g_pending_index]) : std::string();
  }
  if (path.empty() || buffer == 0 || path.size() + 1 > kBufferBytes) {
    return;
  }
  if (title_track) {
    g_watch = false;        // the title's own music loops; the rotation has nothing left to do
    g_watch_title = true;   // ...but the poll still reports it, so a stalled fallback is visible
    REXLOG_INFO("shell_music: the DLC pool has nothing new to play; the menu goes back to the "
                "title's own tracks (\"{}.mogg\") (R9)",
                path);
  } else {
    g_watch = true;
    g_watch_title = false;
    REXLOG_INFO("shell_music: the menu track ended; playing \"{}.mogg\" next (R9), handed over "
                "{} ms after the seam{}",
                path, MillisecondsSince(g_seam_at),
                new_pass ? fmt::format(", opening a new pass over the {} song(s) of the pool",
                                       g_pool.size())
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
      g_rotation.MarkFailed(failed);
      g_playing_name.clear();
    }
    g_play_selection.Failed(g_pending_index);
    g_current_index = -1;
  }
  REXLOG_WARN("shell_music: \"{}\" failed to start or stopped advancing ({} ms since start); "
              "it sits out the rest of this rotation (R9)",
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
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending_index = -1;
        g_play_selection.Stop();
      }
      g_watch = false;
      g_watch_title = true;
      LoadOnGuest(base, parent, music, buffer, path, /*loop=*/true);
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
  // Stream DLC from the file, with no native jump back to its beginning. The retail
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
    g_playing_name = rb_blitz::shell_music::PoolSongKey(g_pool[g_pending_index]);
    pool_size = g_pool.size();
    index = g_pending_index;
  }
  g_track_started_at = std::chrono::steady_clock::now();
  g_seam_at = g_track_started_at;
  g_heartbeat_at = g_track_started_at;
  g_playback.Begin(NowMs());
  g_watch = true;
  g_watch_title = false;
  g_attempts = 0;
  // The panel asked for music itself, so there is no stand-down left to resume.
  g_stand_down = false;
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
  g_pending_index = -1;
  g_play_selection.Stop();
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
    const auto metadata = FindSongMetadata(*device);
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
      std::string key = rb_blitz::shell_music::SongKeyForStream(metadata, stream);
      if (!metadata.empty() && key.empty()) {
        REXLOG_WARN("shell_music: {}:{} has no unambiguous song metadata; excluding it from "
                    "the menu pool", alias, stream);
        continue;
      }
      if (!key.empty() && key != rb_blitz::shell_music::SongName(stream)) {
        REXLOG_INFO("shell_music: song metadata maps {}:{} to key \"{}\"", alias, stream, key);
      }
      candidates.push_back({alias, std::move(stream), std::move(key)});
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
      keys.emplace_back(rb_blitz::shell_music::PoolSongKey(entry));
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_rotation.SetPool(std::move(keys));
    g_watch = false;
    g_watch_title = false;
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

bool StandDownForRefresh(uint8_t* base, PPCContext& ctx) {
  if (base == nullptr) {
    return false;
  }
  const uint32_t music = g_menu_music;
  if (music == 0) {
    // No menu poll has ever run: the menu is not up, so there is nothing of ours playing.
    return false;
  }
  // Only take music down when this layer can start it again. With R9 off, or a pool that mounted
  // nothing, the track belongs to the title, whose own screens own restarting it - this hook must
  // not leave the menu silent for music it did not play. The R9-with-a-pool case is the owner's
  // report, and it is the one the poll resumes.
  if (!g_enabled || g_pool.empty()) {
    return false;
  }

  // The bookkeeping a screen change does (ForgetMenuMusic): the rotation stops watching, but its
  // history stays - the song that was cut counts as played, so the next start is a different one.
  std::string name;
  bool watched = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    watched = g_watch || g_watch_title;
    name = g_playing_name;
    g_watch = false;
    g_watch_title = false;
    g_playback.Stop();
    g_current_index = -1;
    g_pending_index = -1;
    g_play_selection.Stop();
  }

  const bool had_stream = LoadBe32(base, music + kStreamOffset) != 0;
  if (had_stream) {
    // Our own teardown, not the game's: g_setting_up_music keeps the StopStreams hook inside
    // RetireStreamOnGuest from reading this as a navigation.
    const bool was_setting_up = g_setting_up_music;
    g_setting_up_music = true;
    RetireStreamOnGuest(base, ctx, music);
    g_setting_up_music = was_setting_up;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stand_down = true;
    g_stand_down_at = std::chrono::steady_clock::now();
  }
  if (had_stream || watched) {
    REXLOG_INFO("shell_music: the refresh press takes this menu track down (\"{}\") before the "
                "scan blocks the frame; the menu reloads its pool when the discovery pass "
                "returns, with the next song of the rotation (R7/R9)",
                name.empty() ? std::string("the title's own track") : name);
  }
  return had_stream;
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
    const std::string key = current >= 0 && static_cast<size_t>(current) < g_pool.size()
        ? PoolSongKey(g_pool[current]) : std::string();
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
                index, g_pool.size(),
                index < static_cast<int>(g_pool.size()) ? PoolSongKey(g_pool[index])
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
    g_playback.Stop();
    g_current_index = -1;
    g_pending_index = -1;
    g_play_selection.Stop();
  }
  if (watched) {
    REXLOG_INFO("shell_music: the game took this menu track down; the rotation stops watching "
                "(\"{}\") (R9)",
                name);
  }
}

extern "C" REX_FUNC(sub_82758998) {
  ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_82758998(ctx, base);
}

extern "C" REX_FUNC(sub_82758490) {
  ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_82758490(ctx, base);
}

// Start builds the track that was just loaded; its body calls StopStreams as cleanup
// (0x82758C28) before the new stream exists. While it runs, that call is not a teardown.
extern "C" REX_FUNC(sub_82758B98) {
  const bool was_setting_up = g_setting_up_music;
  g_setting_up_music = true;
  __imp__sub_82758B98(ctx, base);
  g_setting_up_music = was_setting_up;
}

// StopStreams is how the game lets the menu music go (the faded-out stop finishing, the poll's
// own end-of-track teardown). That is navigation, never a reason to start the next song. The
// host's own handoffs run with g_setting_up_music set and call __imp__ directly, so only a
// guest-driven teardown reaches here.
extern "C" REX_FUNC(sub_827583F8) {
  if (!g_setting_up_music) ForgetMenuMusic(ctx.r3.u32);
  __imp__sub_827583F8(ctx, base);
}

// Poll observes one-shot completion before the guest's Ready/Play branch. Stop and
// destruction are handled separately, so a screen transition cannot advance the deck.
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
  // R7's refresh press needs to know which ShellMusic the menu is playing on when it takes the
  // track down (StandDownForRefresh). Learning it here costs one store per frame and needs no
  // gates: this poll *is* the menu's music, with R9 on or off.
  if (music != 0) {
    g_menu_music = music;
  }
  // ...and it is also where the resume happens. The stand-down fired on the press; a poll only
  // arrives again once the menu is really back (no poll runs while the connect screen owns the
  // audio), so the first one after the discovery - with the transition behind it - starts the
  // rotation's next song.
  if (g_stand_down && music != 0 && g_enabled && !g_pool.empty() &&
      MillisecondsSince(g_stand_down_at) >= kResumeAfterStandDownMs) {
    g_stand_down = false;
    REXLOG_INFO("shell_music: the menu is back after the refresh; the rotation starts the next "
                "song (R7/R9)");
    StartNextTrack(base, ctx, music);
  }
  const bool watched = g_enabled && (g_watch || g_watch_title) && !g_pool.empty() &&
                       music != 0 && music == g_music;
  if (watched) {
    const uint32_t stream = LoadBe32(base, music + kStreamOffset);
    const uint32_t state = stream ? LoadBe32(base, stream + 20) : 0;
    const uint32_t position = stream ? LoadBe32(base, stream + kPositionOffset) : 0;
    if (g_watch_title) {
      // The fallback plays the title's own track with the title's own loop (r7=1). Report it -
      // a heartbeat, and the loop itself, seen as the position restarting - and never rotate;
      // leaving the menu stops it through the hooks above. A fallback that ever *ends* is the
      // failure this watching exists to make visible.
      if (state == 6 || stream == 0) {
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
      const auto event = g_playback.Observe(NowMs(), stream, state == 3, state == 6, position);
      using Event = rb_blitz::shell_music::Playback::Event;
      if (event == Event::kEnded) {
        NoteSeam(g_pos);
        StartNextTrack(base, ctx, music);
      } else if (event == Event::kFailed) {
        NoteFailedCandidate(base, ctx, music);
      } else {
        if (event == Event::kAudible) {
          g_attempts = 0;
          {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_current_index = g_pending_index;
            g_play_selection.Audible(g_current_index);
            g_rotation.MarkStarted(g_playing_name);
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
}

#endif  // _WIN32
