// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production ShellMusic poll and handoff with synthetic guest
// memory. Runtime services and native callees are stand-ins; no game/UI runs.
// Inclusion makes its private owner state available without a shipping test API.
#include "check.h"
#include "../src/hooks/shell_music.cpp"

namespace {
constexpr uint32_t kMusic = 0x100, kOldStream = 0x200, kNewStream = 0x400;
constexpr uint32_t kVtable = 0x800, kDestroy = 0x1234, kPath = 0x1000;
unsigned loads = 0, starts = 0, stops = 0, destroys = 0, polls = 0;
bool native_loading = false;
std::string loaded;
void Store(uint8_t* base, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(base + address, value);
}
void DestroyStream(PPCContext& ctx, uint8_t*) {
  CHECK_EQ(ctx.r3.u32, kOldStream);
  CHECK_EQ(ctx.r4.u32, 1u);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
  ++destroys;
}
}

bool& FLAGS_enhancements_menu_dlc_songs_storage_() { static bool value = true; return value; }
int32_t& FLAGS_enhancements_menu_dlc_song_count_storage_() { static int32_t value = 3; return value; }
namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t address) {
  CHECK_EQ(address, kDestroy);
  return DestroyStream;
}
}
namespace rb_blitz::settings::boot {
SettingsBoot* SettingsBoot::Current() { return nullptr; }
void SettingsBoot::MenuFrame() {}
}
namespace rb_blitz::settings::menu_compat {
std::string LegacyPoolCountNote(int32_t) { return {}; }
}
namespace rb_blitz::content_progress { void Tick() {} }
namespace rb_blitz::graphics { bool GraphicsLoadingPending() { return native_loading; } }
namespace rb_blitz::dlc {
bool RefreshArmed() { return false; }
void PublishPendingRefresh() {}
std::vector<std::filesystem::path> LibraryPackagePaths() { return {}; }
}
namespace rb_blitz::fs {
std::unique_ptr<rex::filesystem::Device> MountPackage(
    const std::string&, const std::filesystem::path&, std::error_code&) { return {}; }
std::vector<shell_music::SongMetadata> ReadDeviceSongMetadata(rex::filesystem::Device&) { return {}; }
}
namespace rb_blitz::shell_music {
std::string SongKeyForStream(const std::vector<SongMetadata>&, const std::string&) { return {}; }
}

extern "C" REX_FUNC(__imp__sub_82757F50) {
  ++loads;
  CHECK_EQ(ctx.r3.u32, kMusic);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
  loaded = reinterpret_cast<const char*>(base + ctx.r4.u32);
  CHECK_EQ(ctx.r7.u64, loaded.starts_with("sfx/shell/") ? 1u : 0u);
  Store(base, kMusic + 68, ctx.r4.u32);
}
extern "C" REX_FUNC(__imp__sub_82758B98) {
  ++starts;
  Store(base, kMusic + kStreamOffset, kNewStream);
  Store(base, kNewStream + 20, 3);
  Store(base, kNewStream + kPositionOffset, 100);
}
extern "C" REX_FUNC(__imp__sub_827583F8) {
  ++stops;
  Store(base, kMusic + kStreamOffset, 0);
}
extern "C" REX_FUNC(__imp__sub_827586D8) {
  ++polls;
  CHECK_EQ(ctx.r3.u32, kMusic);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
}
extern "C" REX_FUNC(__imp__sub_82758998) {}
extern "C" REX_FUNC(__imp__sub_82758490) {}
extern "C" REX_FUNC(__imp__sub_823E5BE0) {}

void TestLoadingPreservesBootSong() {
  using namespace rb_blitz::shell_music;
  rb_blitz::test::BeginCase("a staged boot song waits for native Start, even on a slow boot");
  std::array<uint8_t, 65536> memory{};
  auto* base = memory.data();
  PPCContext ctx{};
  ctx.r1.u32 = 0xF000;
  ctx.r3.u32 = kMusic;
  ctx.r4.u32 = 0x2000;
  ctx.r31.u64 = 0xABCDEF;
  std::strcpy(reinterpret_cast<char*>(base + ctx.r4.u32), "sfx/shell/samples/shellmusic_1");
  rex::filesystem::HostPathDevice ready("\\Device\\ReadyMusic", {}, true);
  g_enabled = true;
  g_buffers = {kPath};
  g_session.Discover(0, {{"menumusic0", "songs/startup/audio", "BootSong"}});
  CHECK_TRUE(g_cache.Insert(0, &ready));
  sub_82757F50(ctx, base);
  CHECK_EQ(loads, 1u);
  CHECK_FALSE(g_playback.active());
  g_track_started_at -= std::chrono::seconds(28);
  sub_827586D8(ctx, base); // no stream yet: the panel has not called Start
  CHECK_EQ(loads, 1u);
  CHECK_EQ(starts, 0u);
  CHECK_FALSE(g_queue_started);
  CHECK_EQ(g_pending_index, 0);
  sub_82758B98(ctx, base);
  CHECK_TRUE(g_playback.active());
  sub_827586D8(ctx, base);
  CHECK_EQ(g_current_index, 0);
  CHECK_TRUE(SelectedSongKey() == "BootSong");

  rb_blitz::test::BeginCase("cache publication and a loading stall preserve the audible stream");
  g_session.Discover(1, {{"menumusic1", "songs/next/audio", "Successor"}});
  CHECK_TRUE(g_cache.Insert(1, &ready));
  const auto now = NowMs();
  g_playback.Begin(now - 10000);
  CHECK_TRUE(g_playback.Observe(now - 10000, kNewStream, true, false, 100) == Playback::Event::kAudible);
  PublishQueuedPool(true); // native preparation stalled decoding for >3 s
  sub_827586D8(ctx, base); // still state 3 at the same position
  CHECK_EQ(loads, 1u);
  CHECK_EQ(starts, 1u);
  CHECK_EQ(destroys, 0u);
  CHECK_EQ(LoadBe32(base, kMusic + kStreamOffset), kNewStream);
  CHECK_EQ(g_session.Cursor(), 0u);
  CHECK_EQ(g_current_index, 0);
  CHECK_TRUE(SelectedSongKey() == "BootSong");
  g_playback.Begin(now - 10000);
  g_playback.Observe(now - 10000, kNewStream, true, false, 100);
  native_loading = true; // preparation itself blocked after the last keepalive
  sub_827586D8(ctx, base);
  native_loading = false;
  CHECK_EQ(loads, 1u);
  CHECK_EQ(starts, 1u);
  CHECK_EQ(g_current_index, 0);
  Store(base, kNewStream + kPositionOffset, 200);
  sub_827586D8(ctx, base); // decoding resumes after loading, same stream/song
  CHECK_EQ(loads, 1u);
  CHECK_EQ(starts, 1u);
  CHECK_EQ(g_playback.position(), 200u);

  g_session = MusicSession{};
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  g_playback.Stop();
  g_play_selection.Stop();
  g_current_index = g_pending_index = -1;
  g_stream_song = kNoIndex;
  loads = starts = stops = destroys = polls = 0;
}

int main() {
  using namespace rb_blitz::shell_music;
  TestLoadingPreservesBootSong();
  rb_blitz::test::BeginCase("temporary title fallback recovers without a counter rewind");
  std::array<uint8_t, 65536> memory{};
  auto* base = memory.data();
  PPCContext ctx{};
  ctx.r1.u32 = 0xF000;
  ctx.r3.u32 = kMusic;
  ctx.r31.u64 = 0xABCDEF;
  g_music = kMusic;
  g_enabled = g_watch_title = g_waiting_for_pool = true;
  g_watch = false;
  g_buffers = {kPath};
  g_heartbeat_at = std::chrono::steady_clock::now();
  g_pos = 2700000;
  Store(base, kMusic + kStreamOffset, kOldStream);
  Store(base, kOldStream, kVtable);
  Store(base, kVtable, kDestroy);
  Store(base, kOldStream + 20, 3);
  Store(base, kOldStream + kPositionOffset, 2717696);
  g_session.Discover(0, {{"menumusic0", "songs/failed/audio", "FailedStartup"}});
  g_session.PlanAhead(g_rng);
  g_session.Select(0);
  g_session.Failed(0);
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, 0u); // no successor yet: keep the title source
  CHECK_EQ(polls, 1u);

  // Match the owner's log: successor ready, same monotonically increasing title
  // position. These device objects need no initialization or file I/O here.
  rex::filesystem::HostPathDevice ready("\\Device\\ReadyMusic", {}, true);
  g_session.Discover(1, {{"menumusic1", "songs/next/audio", "QueuedSong"}});
  CHECK_TRUE(g_cache.Insert(1, &ready));
  Store(base, kOldStream + kPositionOffset, 2720000);
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, 1u);
  CHECK_EQ(starts, 1u);
  CHECK_EQ(stops, 1u);
  CHECK_EQ(destroys, 1u);
  CHECK_EQ(polls, 2u);
  CHECK_TRUE(loaded == "menumusic1:songs/next/audio");
  CHECK_TRUE(g_watch);
  CHECK_FALSE(g_watch_title);
  CHECK_FALSE(g_waiting_for_pool);
  CHECK_EQ(g_pending_index, 1u);
  CHECK_EQ(g_stream_song, 1u);

  sub_827586D8(ctx, base); // advancing successor publishes its actual audible ID
  CHECK_EQ(loads, 1u);
  CHECK_EQ(g_current_index, 1u);
  CHECK_TRUE(SelectedSongKey() == "QueuedSong");

  rb_blitz::test::BeginCase("a deliberate failure fallback keeps its circuit breaker");
  g_watch = false;
  g_watch_title = true;
  g_waiting_for_pool = false;
  g_session.Discover(2, {{"menumusic2", "songs/another/audio", "AnotherSong"}});
  CHECK_TRUE(g_cache.Insert(2, &ready));
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, 1u);
  CHECK_EQ(polls, 4u);

  rb_blitz::test::BeginCase("a failed pre-A startup song starts recovery preparation immediately");
  g_session = MusicSession{};
  g_session.Discover(0, {{"menumusic0", "songs/startup/audio", "StartupSong"}});
  g_session.PlanAhead(g_rng);
  CHECK_TRUE(g_session.Select(0));
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  CHECK_TRUE(g_cache.Insert(0, &ready));
  g_packages = {"fixture-startup", "fixture-successor"};
  g_next_package = 1;
  CHECK_FALSE(g_queue_started);
  g_watch = true;
  g_watch_title = g_waiting_for_pool = false;
  g_pending_index = 0;
  g_current_index = -1;
  g_playing_name = "StartupSong";
  g_playback.Begin(NowMs() - Playback::kStartupDeadlineMs - 1);
  g_track_started_at = std::chrono::steady_clock::now() - std::chrono::seconds(16);
  Store(base, kMusic + kStreamOffset, 0);
  sub_827586D8(ctx, base);
  CHECK_TRUE(g_queue_started);
  CHECK_TRUE(g_watch_title);
  CHECK_TRUE(g_waiting_for_pool);
  CHECK_EQ(loads, 2u);
  CHECK_TRUE(loaded.starts_with("sfx/shell/"));
  CloseQueuedPool();
  return rb_blitz::test::Finish();
}
