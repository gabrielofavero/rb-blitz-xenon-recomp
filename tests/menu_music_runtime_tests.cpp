// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production ShellMusic poll and handoff with synthetic guest
// memory. Runtime services and native callees are stand-ins; no game/UI runs.
// Inclusion makes its private owner state available without a shipping test API.
#include "check.h"
#include <fstream>
#include "../src/hooks/shell_music.cpp"

namespace {
constexpr uint32_t kMusic = 0x100, kOldStream = 0x200, kNewStream = 0x400;
constexpr uint32_t kVtable = 0x800, kDestroy = 0x1234, kPath = 0x1000;
unsigned loads = 0, starts = 0, stops = 0, destroys = 0, polls = 0;
uint32_t native_start_state = 3, native_start_position = 100, native_start_offset_ms = 0;
bool native_loading = false;
bool main_menu = true;
bool capture_active = false;
bool modal_active = false;
bool window_focused = true;
bool poll_retries_play = false;
bool native_pause_works = true;
bool native_resume_works = true;
unsigned pause_calls = 0, resume_calls = 0;
std::string loaded;
void Store(uint8_t* base, uint32_t address, uint32_t value) {
  rex::memory::store_and_swap<uint32_t>(base + address, value);
}
void DestroyStream(PPCContext& ctx, uint8_t*) {
  CHECK_TRUE(ctx.r3.u32 == kOldStream || ctx.r3.u32 == kNewStream);
  CHECK_EQ(ctx.r4.u32, 1u);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
  ++destroys;
}
}

bool& FLAGS_enhancements_background_music_nav_storage_() { static bool value = true; return value; }
bool& FLAGS_enhancements_music_player_storage_() { static bool value = true; return value; }
bool& FLAGS_enhancements_menu_dlc_songs_storage_() { static bool value = true; return value; }
int32_t& FLAGS_enhancements_menu_dlc_song_count_storage_() { static int32_t value = 3; return value; }
namespace rex::runtime {
PPCFunc* ResolveIndirectFunction(uint32_t address) {
  CHECK_EQ(address, kDestroy);
  return DestroyStream;
}
}
namespace rb_blitz::input {
bool MainMenuMusicInputAvailable() { return main_menu; }
bool HostInputFocused() { return window_focused; }
namespace capture { bool Armed() { return capture_active; } }
}
namespace rex::kernel::xam { bool xeXamIsUIActive() { return modal_active; } }
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

extern "C" REX_FUNC(__imp__sub_82757F50) {
  ++loads;
  CHECK_EQ(ctx.r3.u32, kMusic);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
  loaded = reinterpret_cast<const char*>(base + ctx.r4.u32);
  CHECK_EQ(ctx.r7.u64, loaded.starts_with("sfx/shell/") ? 1u : 0u);
  Store(base, kMusic + 68, ctx.r4.u32);
}
extern "C" REX_FUNC(__imp__sub_82757D40) { ctx.r3.u32 = native_start_offset_ms; }
extern "C" REX_FUNC(__imp__sub_82758B98) {
  ++starts;
  PPCContext offset(ctx);
  sub_82757D40(offset, base);
  Store(base, kMusic + kStreamOffset, kNewStream);
  Store(base, kNewStream + 20, native_start_state);
  Store(base, kNewStream + kPositionOffset, native_start_position + offset.r3.u32 * 44100 / 1000);
}
extern "C" REX_FUNC(__imp__sub_827583F8) {
  ++stops;
  Store(base, kMusic + kStreamOffset, 0);
}
extern "C" REX_FUNC(__imp__sub_827586D8) {
  ++polls;
  CHECK_EQ(ctx.r3.u32, kMusic);
  CHECK_EQ(ctx.r31.u64, 0xABCDEFu);
  const auto stream = LoadBe32(base, kMusic + kStreamOffset);
  if (poll_retries_play && stream && LoadBe32(base, stream + 20) == 5) {
    PPCContext call(ctx);
    call.r3.u32 = stream;
    sub_823E4808(call, base); // retail's IsReady/!IsPlaying branch
  }
}
extern "C" REX_FUNC(sub_823E3FD8) {
  ++pause_calls;
  if (native_pause_works) Store(base, ctx.r3.u32 + 20, 5);
}
extern "C" REX_FUNC(__imp__sub_823E4808) {
  ++resume_calls;
  if (native_resume_works) Store(base, ctx.r3.u32 + 20, 3);
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

void TestNavigatorRuntime() {
  using namespace rb_blitz::shell_music;
  rb_blitz::test::BeginCase("production navigator: commands, audible metadata, history, restart and gates");
  std::array<uint8_t, 65536> memory{};
  auto* base = memory.data();
  PPCContext ctx{};
  ctx.r1.u32 = 0xF000; ctx.r3.u32 = kMusic; ctx.r31.u64 = 0xABCDEF;
  rex::filesystem::HostPathDevice ready("\\Device\\ReadyMusic", {}, true);
  g_navigation_enabled = g_enabled = true;
  g_closing = false;
  g_nav_epoch = NowPlaying().Reset();
  g_buffers = {kPath}; g_music = kMusic;
  g_session = MusicSession{};
  g_session.Discover(0, {{"menumusic0", "songs/one/audio", "One", "First", "Artist One"}});
  g_session.Discover(1, {{"menumusic1", "songs/two/audio", "Two", "Second", "Artist Two"}});
  g_session.AdoptFirst(0);
  g_session.PlanAhead(g_rng);
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  CHECK_TRUE(g_cache.Insert(0, &ready)); CHECK_TRUE(g_cache.Insert(1, &ready));
  g_watch = true; g_watch_title = false;
  g_pending_index = g_current_index = 0; g_stream_song = 0;
  g_play_selection.Audible(0);
  g_playback.Begin(NowMs());
  Store(base, kMusic + kStreamOffset, kOldStream);
  Store(base, kOldStream, kVtable); Store(base, kVtable, kDestroy);
  Store(base, kNewStream, kVtable);
  Store(base, kOldStream + 20, 3); Store(base, kOldStream + kPositionOffset, 100);
  sub_827586D8(ctx, base);
  auto snapshot = NowPlaying().ReadSnapshot();
  CHECK_TRUE(snapshot.available);
  CHECK_TRUE(snapshot.playback.track.title == "First");
  CHECK_TRUE(snapshot.playback.capabilities.play_pause);
  bool answer = false;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  native_start_offset_ms = 30000; // native Start would begin at 30 seconds without the manual override
  CHECK_EQ(loads, 0u); // file_exists only posts; it never calls native playback
  CHECK_TRUE(NowPlaying().ReadSnapshot().pending.has_value());
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, 1u); CHECK_EQ(g_session.Cursor(), 1u);
  CHECK_EQ(LoadBe32(base, kNewStream + kPositionOffset), native_start_position);
  snapshot = NowPlaying().ReadSnapshot();
  CHECK_TRUE(snapshot.playback.track.key == "Two");
  CHECK_TRUE(snapshot.result && snapshot.result->status == nav::CompletionStatus::kApplied);
  CHECK_FALSE(snapshot.pending);
  CHECK_TRUE(SelectedSongKey() == "Two"); // no old-song grace after manual skip
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/begin", &answer)); CHECK_TRUE(answer);
  CHECK_TRUE(SelectedSongKey() == "Two");
  g_play_selection.Complete();

  Store(base, kNewStream + kPositionOffset, 7 * 44100);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 1u); CHECK_TRUE(loaded == "menumusic1:songs/two/audio");
  CHECK_EQ(LoadBe32(base, kNewStream + kPositionOffset), 100u);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 0u); CHECK_TRUE(SelectedSongKey() == "One");
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 1u); CHECK_TRUE(SelectedSongKey() == "Two");

  g_session.Discover(2, {{"menumusic2", "songs/cold/audio", "Cold"}});
  g_session.PlanAhead(g_rng);
  const auto before = loads;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, before); CHECK_EQ(g_session.Cursor(), 1u);
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.key == "Two");
  CHECK_TRUE(NowPlaying().ReadSnapshot().pending.has_value());
  CHECK_TRUE(g_cache.Insert(2, &ready));
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, before + 1); CHECK_TRUE(SelectedSongKey() == "Cold");

  CHECK_TRUE(nav::Post(nav::Command::kPrevious).status == nav::PostStatus::kAccepted);
  main_menu = false;
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, before + 1);
  CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kCancelled);
  main_menu = true;
  g_session.DiscoveryComplete();
  g_session.PlanAhead(g_rng);
  Store(base, kNewStream + 20, 6); // automatic EOF publishes the successor too
  sub_827586D8(ctx, base);
  sub_827586D8(ctx, base);
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.key != "Cold");
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPlaying);
  const auto fallback_cursor = g_session.Cursor();
  const auto fallback_previous = PoolSongKey(g_session.Sources()[g_session.SongAt(fallback_cursor)]);
  g_watch = false; g_watch_title = true; g_waiting_for_pool = false; g_title_index = 0;
  g_nav_age.Reset();
  Store(base, kNewStream + kPositionOffset, 100);
  sub_827586D8(ctx, base);
  g_current_index = g_pending_index = -1; g_play_selection.Stop();
  Store(base, kNewStream + kPositionOffset, 7 * 44100);
  sub_827586D8(ctx, base);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_TRUE(loaded == kTitleTracks[0]);
  CHECK_EQ(g_session.Cursor(), fallback_cursor);
  CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kApplied);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), fallback_cursor);
  CHECK_TRUE(SelectedSongKey() == fallback_previous);
  capture_active = true;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_FALSE(answer);
  capture_active = false;
  modal_active = true;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_FALSE(answer);
  modal_active = false;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/pause", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPaused);
  g_navigation_enabled = false;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_FALSE(answer);
  g_navigation_enabled = true;
  ForgetMenuMusic(kMusic);
  CHECK_FALSE(NowPlaying().ReadSnapshot().available);
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.key.empty());

  rb_blitz::test::BeginCase("navigator with R9 off adopts and navigates the native title source");
  g_enabled = false;
  g_session = MusicSession{};
  g_watch = g_watch_title = false;
  g_playback.Stop();
  Store(base, kMusic + kStreamOffset, 0);
  ctx.r4.u32 = 0x2000;
  ctx.r7.u64 = 1;
  std::strcpy(reinterpret_cast<char*>(base + ctx.r4.u32), kTitleTracks[1]);
  sub_82757F50(ctx, base);
  CHECK_EQ(g_title_index, 1);
  CHECK_EQ(g_session.SongAt(g_session.Cursor()), 1u);
  sub_827586D8(ctx, base); // staged native Load must survive the deferred Start
  CHECK_TRUE(g_watch_title);
  sub_82758B98(ctx, base);
  sub_827586D8(ctx, base);
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.source == MusicSource::kTitle);
  CHECK_FALSE(HasPool()); CHECK_TRUE(SelectedSongKey().empty());
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 1u);
  CHECK_TRUE(loaded.starts_with("sfx/shell/"));
  CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kApplied);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_TRUE(loaded == kTitleTracks[1]);
  native_start_state = 6; native_start_position = 0;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_FALSE(NowPlaying().ReadSnapshot().pending);
  CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kFailed);
  CHECK_EQ(g_nav_target, kNoIndex);
  CHECK_FALSE(g_nav_request);
  native_start_state = 3; native_start_position = 100; native_start_offset_ms = 0;
  ForgetMenuMusic(kMusic);
  g_navigation_enabled = false;
  g_session = MusicSession{};
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  g_stream_song = kNoIndex;
  loads = starts = stops = destroys = polls = pause_calls = resume_calls = 0;
}

void TestRapidPreviousDuringRestart() {
  using namespace rb_blitz::shell_music;
  rb_blitz::test::BeginCase("rapid Previous survives a slow restart and reaches the boot song");
  std::array<uint8_t, 65536> memory{};
  auto* base = memory.data();
  PPCContext ctx{};
  ctx.r1.u32 = 0xF000; ctx.r3.u32 = kMusic; ctx.r31.u64 = 0xABCDEF;
  rex::filesystem::HostPathDevice ready("\\Device\\ReadyMusic", {}, true);
  g_navigation_enabled = g_enabled = true;
  g_nav_epoch = NowPlaying().Reset();
  g_session = MusicSession{};
  g_session.Discover(0, {{"menumusic0", "songs/boot/audio", "Boot"},
                         {"menumusic0", "songs/second/audio", "Second"},
                         {"menumusic0", "songs/third/audio", "Third"}});
  g_session.AdoptFirst(0); g_session.PlanAhead(g_rng);
  CHECK_TRUE(g_session.Select(2));
  const auto before_key = PoolSongKey(g_session.Sources()[g_session.SongAt(2)]);
  g_cache = MusicMountCache<rex::filesystem::Device>{}; CHECK_TRUE(g_cache.Insert(0, &ready));
  g_music = kMusic; g_buffers = {kPath};
  g_watch = true; g_watch_title = false; g_waiting_for_pool = false;
  g_current_index = g_pending_index = int(g_session.SongAt(2));
  g_stream_song = g_session.SongAt(2);
  g_play_selection.Audible(g_current_index);
  g_nav_age.Reset(); g_playback.Begin(NowMs());
  Store(base, kMusic + kStreamOffset, kOldStream);
  Store(base, kOldStream, kVtable); Store(base, kNewStream, kVtable);
  Store(base, kVtable, kDestroy);
  Store(base, kOldStream + 20, 3); Store(base, kOldStream + kPositionOffset, 6 * 44100);
  sub_827586D8(ctx, base);
  Store(base, kOldStream + kPositionOffset, 13 * 44100);
  bool answer = false;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  native_start_state = 1; native_start_position = 0; // decoder opens for several polls
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 2u); // first press restarts after seven seconds
  CHECK_TRUE(loaded.find(g_session.Sources()[g_session.SongAt(2)].stream) != std::string::npos);
  CHECK_TRUE(g_nav_started);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  CHECK_EQ(NowPlaying().ReadSnapshot().queued.size(), 2u);
  const auto pending_loads = loads;
  sub_827586D8(ctx, base);
  CHECK_EQ(loads, pending_loads);
  CHECK_TRUE(NowPlaying().ReadSnapshot().pending.has_value());
  CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.key == before_key);
  native_start_state = 3; native_start_position = 100;
  Store(base, kNewStream + 20, 3); Store(base, kNewStream + kPositionOffset, 6 * 44100);
  sub_827586D8(ctx, base); // restart finally plays; queued press becomes pending
  CHECK_EQ(g_session.Cursor(), 2u);
  // Even if execution is delayed beyond five seconds, the queued press was made
  // while loading and must step back, not be reinterpreted as another restart.
  Store(base, kNewStream + kPositionOffset, 13 * 44100);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 1u);
  sub_827586D8(ctx, base);
  CHECK_EQ(g_session.Cursor(), 0u);
  CHECK_TRUE(SelectedSongKey() == "Boot");
  CHECK_FALSE(NowPlaying().ReadSnapshot().pending);
  CHECK_TRUE(NowPlaying().ReadSnapshot().queued.empty());
  CHECK_EQ(loads, pending_loads + 2);

  rb_blitz::test::BeginCase("focus loss cancels the active navigation and its queued burst");
  native_start_state = 1; native_start_position = 0;
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/next", &answer)); CHECK_TRUE(answer);
  sub_827586D8(ctx, base);
  CHECK_TRUE(AnswerPlayQuery("rbbz-play/previous", &answer)); CHECK_TRUE(answer);
  window_focused = false;
  sub_827586D8(ctx, base);
  CHECK_FALSE(NowPlaying().ReadSnapshot().pending);
  CHECK_TRUE(NowPlaying().ReadSnapshot().queued.empty());
  CHECK_FALSE(g_nav_request);
  window_focused = true;
  native_start_state = 3; native_start_position = 100;
  ForgetMenuMusic(kMusic);
  g_navigation_enabled = false;
  g_session = MusicSession{}; g_cache = MusicMountCache<rex::filesystem::Device>{};
  g_stream_song = kNoIndex;
  loads = starts = stops = destroys = polls = 0;
}

void TestNativePauseRuntime() {
  using namespace rb_blitz::shell_music;
  rb_blitz::test::BeginCase("native pause: poll ownership, watchdog, same-position resume and skip");
  for (bool title : {false, true}) {
    std::array<uint8_t, 65536> memory{};
    auto* base = memory.data();
    PPCContext ctx{};
    ctx.r1.u32 = 0xF000; ctx.r3.u32 = kMusic; ctx.r31.u64 = 0xABCDEF;
    rex::filesystem::HostPathDevice ready("\\Device\\PauseMusic", {}, true);
    g_navigation_enabled = true;
    g_nav_pause_trace = false; // ordinary boots support pause without the trial flag
    g_enabled = !title; g_closing = false; g_music = kMusic;
    g_watch = !title; g_watch_title = title; g_title_index = title ? 0 : -1;
    g_waiting_for_pool = false; g_nav_paused_stream = 0; g_nav_age.Reset();
    g_nav_epoch = NowPlaying().Reset(); g_buffers = {kPath};
    g_session = MusicSession{};
    g_session.Discover(0, {{"menumusic0", title ? kTitleTracks[0] : "songs/one/audio", "One"}});
    g_session.Discover(1, {{"menumusic1", title ? kTitleTracks[1] : "songs/two/audio", "Two"}});
    g_session.AdoptFirst(0); g_session.PlanAhead(g_rng);
    g_cache = MusicMountCache<rex::filesystem::Device>{};
    CHECK_TRUE(g_cache.Insert(0, &ready)); CHECK_TRUE(g_cache.Insert(1, &ready));
    g_pending_index = g_current_index = title ? -1 : 0;
    if (!title) g_play_selection.Audible(0);
    g_playback.Begin(NowMs());
    Store(base, kMusic + kStreamOffset, kOldStream);
    Store(base, kOldStream, kVtable); Store(base, kVtable, kDestroy);
    Store(base, kNewStream, kVtable);
    Store(base, kOldStream + 20, 3); Store(base, kOldStream + kPositionOffset, 123456);
    poll_retries_play = true;
    sub_827586D8(ctx, base);
    const auto track = NowPlaying().ReadSnapshot().playback.track.key;
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.capabilities.play_pause);
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPaused);
    CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kApplied);
    CHECK_EQ(pause_calls, 1u); CHECK_EQ(resume_calls, 0u);
    // A long intentional hold must survive both the host watchdog and native
    // auto-Play without reloading, advancing history or losing song identity.
    g_playback.Begin(NowMs() - 60000);
    g_playback.Observe(NowMs() - 60000, kOldStream, true, false, 123456);
    sub_827586D8(ctx, base);
    CHECK_TRUE(g_playback.active()); CHECK_EQ(g_session.Cursor(), 0u);
    CHECK_EQ(LoadBe32(base, kOldStream + 20), 5u);
    CHECK_EQ(LoadBe32(base, kOldStream + kPositionOffset), 123456u);
    CHECK_EQ(loads + starts + stops + destroys + resume_calls, 0u);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.track.key == track);
    // A final in-flight decoder block may settle during pause (owner log 367).
    Store(base, kOldStream + kPositionOffset, 123456 + 4096);
    sub_827586D8(ctx, base);
    CHECK_EQ(g_nav_age.samples(), 0u);
    native_resume_works = false;
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kFailed);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPaused);
    CHECK_EQ(g_nav_paused_stream, kOldStream);
    native_resume_works = true;
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_EQ(resume_calls, 2u); CHECK_EQ(g_nav_paused_stream, 0u);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPlaying);
    CHECK_EQ(LoadBe32(base, kOldStream + kPositionOffset), 123456u + 4096);
    CHECK_TRUE(g_playback.active());
    Store(base, kOldStream + kPositionOffset, 124456 + 4096);
    sub_827586D8(ctx, base);
    CHECK_EQ(g_nav_age.samples(), 1000u);
    // A failed native call never publishes a paused state.
    native_pause_works = false;
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_TRUE(NowPlaying().ReadSnapshot().result->status == nav::CompletionStatus::kFailed);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPlaying);
    native_pause_works = true;
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_TRUE(nav::Post(nav::Command::kNext).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    CHECK_EQ(g_nav_paused_stream, 0u);
    CHECK_EQ(loads, 1u); CHECK_EQ(g_session.Cursor(), 1u);
    CHECK_TRUE(NowPlaying().ReadSnapshot().playback.state == TransportState::kPlaying);
    CHECK_EQ(LoadBe32(base, kNewStream + kPositionOffset), native_start_position);
    CHECK_TRUE(nav::Post(nav::Command::kPlayPause).status == nav::PostStatus::kAccepted);
    sub_827586D8(ctx, base);
    ForgetMenuMusic(kMusic);
    CHECK_EQ(g_nav_paused_stream, 0u);
    CHECK_FALSE(NowPlaying().ReadSnapshot().available);
    // The ownership guard must not suppress a foreign stream's native Play.
    PPCContext foreign(ctx); foreign.r3.u32 = kNewStream;
    const auto calls = resume_calls;
    sub_823E4808(foreign, base);
    CHECK_EQ(resume_calls, calls + 1);
    poll_retries_play = false; g_navigation_enabled = false;
    g_session = MusicSession{}; g_cache = MusicMountCache<rex::filesystem::Device>{};
    g_stream_song = kNoIndex;
    loads = starts = stops = destroys = polls = pause_calls = resume_calls = 0;
  }
}

void TestArtworkRead() {
  using namespace rb_blitz;
  using namespace shell_music;
  using ui::music::ArtRead;
  test::BeginCase("production artwork read uses cache leases and revalidates epoch/song/off gates");
  const auto root = std::filesystem::temp_directory_path() /
      ("rbbz-player-art-" + std::to_string(NowMs()));
  std::filesystem::create_directories(root / "songs" / "one" / "gen");
  const auto path = root / "songs" / "one" / "gen" / "one_keep.png_xbox";
  { std::ofstream file(path, std::ios::binary); file << "owned bytes"; }
  rex::filesystem::HostPathDevice device("\\Device\\ArtReadTest", root, true);
  CHECK_TRUE(device.Initialize());
  g_closing = false;
  g_navigation_enabled = true;
  g_session = MusicSession{};
  const std::string key = "\\songs\\one\\gen\\one_keep.png_xbox";
  g_session.Discover(0, {{"menumusic0", "songs/one/one", "One", "Title", "Artist", key}});
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  CHECK_TRUE(g_cache.Insert(0, &device));
  const auto epoch = NowPlaying().Reset();
  NowPlaying().Publish(epoch, {{MusicSource::kDlc, 0, "One", "Title", "Artist", key},
                              TransportState::kPlaying, {true,true,true}}, true);
  std::vector<uint8_t> bytes;
  const ui::music::ArtTag tag{epoch,0,key};
  CHECK_EQ(ReadArtwork(tag,&bytes),ArtRead::kReady);
  CHECK_TRUE(std::string(bytes.begin(),bytes.end()) == "owned bytes");
  CHECK_EQ(device.open_file_count(),0u);
  CHECK_EQ(g_cache.Size(),1u);
  CHECK_EQ(ReadArtwork({epoch+1,0,key},&bytes),ArtRead::kMissing);
  CHECK_EQ(ReadArtwork({epoch,1,key},&bytes),ArtRead::kMissing);
  CHECK_EQ(ReadArtwork({epoch,0,"different"},&bytes),ArtRead::kMissing);
  g_navigation_enabled = false;
  CHECK_EQ(ReadArtwork(tag,&bytes),ArtRead::kMissing);
  g_navigation_enabled = true;
  g_cache = MusicMountCache<rex::filesystem::Device>{};
  CHECK_EQ(ReadArtwork(tag,&bytes),ArtRead::kDeferred);
  CHECK_EQ(g_cache.Size(),0u);  // optional art never mounts a ninth device
  g_closing = true;
  CHECK_EQ(ReadArtwork(tag,&bytes),ArtRead::kMissing);
  std::filesystem::remove(path);
  std::filesystem::remove(path.parent_path());
  std::filesystem::remove(root / "songs" / "one");
  std::filesystem::remove(root / "songs");
  std::filesystem::remove(root);
}

void TestMusicFeatureConfiguration() {
  rb_blitz::test::BeginCase("player and pad/key navigation independently prepare the transport; Default keeps native music");
  using namespace rb_blitz::shell_music;
  for (bool music : {false, true}) for (bool player : {false, true}) for (bool keys : {false, true}) {
    FLAGS_enhancements_menu_dlc_songs_storage_() = music;
    FLAGS_enhancements_music_player_storage_() = player;
    FLAGS_enhancements_background_music_nav_storage_() = keys;
    // Supplied buffers avoid allocating guest memory; Configure itself still owns the flags.
    g_buffers = {kPath};
    Configure(nullptr, {});
    CHECK_TRUE(g_menu_music_enabled == music);
    CHECK_TRUE(g_navigation_enabled == (music && (player || keys)));
  }
  FLAGS_enhancements_menu_dlc_songs_storage_() = true;
  FLAGS_enhancements_music_player_storage_() = true;
  FLAGS_enhancements_background_music_nav_storage_() = true;
  g_navigation_enabled = false;
  g_buffers.clear();
}

int main() {
  TestMusicFeatureConfiguration();
  TestArtworkRead();
  using namespace rb_blitz::shell_music;
  TestNavigatorRuntime();
  TestRapidPreviousDuringRestart();
  TestNativePauseRuntime();
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
