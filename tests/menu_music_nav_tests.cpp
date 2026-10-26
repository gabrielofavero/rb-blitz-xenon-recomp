// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_now_playing.h"
#include "hooks/menu_music_playback.h"
#include "hooks/menu_music_cache.h"

#include <atomic>
#include <thread>

using namespace rb_blitz::test;
using namespace rb_blitz::shell_music;

void TestLatch() {
  BeginCase("navigator: rising edges, held gates and simultaneous controls");
  nav::Latch latch;
  nav::Input input;
  for (const auto& [button, command] : {
           std::pair{nav::kNextButton, nav::Command::kNext},
           std::pair{nav::kPreviousButton, nav::Command::kPrevious},
           std::pair{nav::kPlayPauseButton, nav::Command::kPlayPause}}) {
    latch.Reset();
    input = {button};
    auto press = latch.Update(input, true);
    CHECK_TRUE(press && press->command == command && press->producer == nav::Producer::kPad);
    CHECK_FALSE(latch.Update(input, true));
    CHECK_FALSE(latch.Update({}, true));
    CHECK_TRUE(latch.Update(input, true));
    latch.Reset(input);
    CHECK_FALSE(latch.Update(input, true));
  }
  for (int key = 0; key < 3; ++key) {
    latch.Reset();
    input = {};
    if (key == 0) input.media_next = true;
    if (key == 1) input.media_previous = true;
    if (key == 2) input.media_play_pause = true;
    CHECK_FALSE(latch.Update(input, false));
    CHECK_FALSE(latch.Update(input, true));  // held while becoming available
    CHECK_FALSE(latch.Update({}, true));
    const auto press = latch.Update(input, true);
    CHECK_TRUE(press && press->producer == nav::Producer::kMediaKey);
    CHECK_TRUE(press && press->command == (key == 0 ? nav::Command::kNext :
                                         key == 1 ? nav::Command::kPrevious : nav::Command::kPlayPause));
    CHECK_FALSE(latch.Update(input, true));
  }
  latch.Reset();
  input = {uint16_t(nav::kNextButton | nav::kPreviousButton | nav::kPlayPauseButton), true, true, true};
  const auto first = latch.Update(input, true);
  CHECK_TRUE(first && first->command == nav::Command::kNext);
  CHECK_FALSE(latch.Update(input, true));  // other simultaneous edges aren't deferred
  latch.Reset();
  CHECK_FALSE(latch.Update({nav::kNextButton}, false));
  CHECK_FALSE(latch.Update({nav::kNextButton}, true));
}

PlaybackUpdate Track(size_t id) {
  PlaybackUpdate update;
  update.track = {MusicSource::kDlc, id, "key" + std::to_string(id),
                 "title" + std::to_string(id), "artist" + std::to_string(id),
                 "art" + std::to_string(id)};
  update.state = TransportState::kPlaying;
  update.capabilities = {true, true, true};
  return update;
}

void TestBridge() {
  BeginCase("bridge: queue acceptance is separate from execution and actual playback");
  NowPlayingBridge bridge;
  auto snapshot = bridge.ReadSnapshot();
  const auto epoch = snapshot.epoch;
  CHECK_FALSE(snapshot.available);
  CHECK_EQ(bridge.Post(nav::Command::kNext, nav::Producer::kPlayer).status, nav::PostStatus::kUnavailable);
  CHECK_TRUE(bridge.Publish(epoch, Track(7)));
  bridge.SetAvailable(true);
  const auto posted = bridge.Post(nav::Command::kNext, nav::Producer::kPlayer, epoch);
  CHECK_EQ(posted.status, nav::PostStatus::kAccepted);
  CHECK_TRUE(posted.id != 0);
  snapshot = bridge.ReadSnapshot();
  CHECK_EQ(snapshot.playback.track.song, 7u);
  CHECK_TRUE(snapshot.pending && snapshot.pending->id == posted.id);
  CHECK_FALSE(snapshot.result);
  CHECK_EQ(bridge.Post(nav::Command::kPrevious, nav::Producer::kMediaKey).status, nav::PostStatus::kBusy);
  const auto request = bridge.Take();
  CHECK_TRUE(request);
  if (!request) return;
  CHECK_EQ(request->producer, nav::Producer::kPlayer);
  CHECK_FALSE(bridge.Take());
  CHECK_EQ(bridge.Post(nav::Command::kNext, nav::Producer::kPad).status, nav::PostStatus::kBusy);
  // A cold successor does not change the audible identity merely because Take succeeded.
  CHECK_EQ(bridge.ReadSnapshot().playback.track.song, 7u);
  CHECK_TRUE(bridge.Publish(epoch, Track(8)));
  CHECK_TRUE(bridge.Complete(*request, nav::CompletionStatus::kApplied));
  snapshot = bridge.ReadSnapshot();
  CHECK_FALSE(snapshot.pending);
  CHECK_TRUE(snapshot.result && snapshot.result->status == nav::CompletionStatus::kApplied);
  CHECK_EQ(snapshot.playback.track.song, 8u);
  CHECK_FALSE(bridge.Complete(*request, nav::CompletionStatus::kApplied));

  BeginCase("bridge: capabilities, failed commands and pause state remain honest");
  auto update = Track(8);
  update.capabilities.play_pause = false;
  CHECK_TRUE(bridge.Publish(epoch, update));
  CHECK_EQ(bridge.Post(nav::Command::kPlayPause, nav::Producer::kPlayer).status,
           nav::PostStatus::kUnavailable);
  CHECK_EQ(bridge.Post(nav::Command::kNext, nav::Producer::kPad).status, nav::PostStatus::kAccepted);
  const auto failing = bridge.Take();
  CHECK_TRUE(failing);
  if (failing) CHECK_TRUE(bridge.Complete(*failing, nav::CompletionStatus::kFailed, "no ready target"));
  snapshot = bridge.ReadSnapshot();
  CHECK_TRUE(snapshot.result && snapshot.result->explanation == "no ready target");
  CHECK_EQ(snapshot.playback.state, TransportState::kPlaying);
  CHECK_EQ(snapshot.playback.track.song, 8u);

  CHECK_EQ(bridge.Post(nav::Command::kNext, nav::Producer::kPad).status, nav::PostStatus::kAccepted);
  update.capabilities.next = false;
  CHECK_TRUE(bridge.Publish(epoch, update));
  CHECK_FALSE(bridge.Take());  // revalidate capability at execution
  CHECK_EQ(bridge.ReadSnapshot().result->status, nav::CompletionStatus::kCancelled);

  BeginCase("bridge: stale art/session work cannot replace the current song or command");
  CHECK_TRUE(bridge.Publish(epoch, Track(9)));
  CHECK_FALSE(bridge.PublishArtwork(epoch, 8, "art8", ArtworkState::kReady));
  CHECK_FALSE(bridge.PublishArtwork(epoch, 9, "art8", ArtworkState::kReady));
  CHECK_TRUE(bridge.PublishArtwork(epoch, 9, "art9", ArtworkState::kReady));
  const auto copy = bridge.ReadSnapshot();
  CHECK_EQ(copy.artwork, ArtworkState::kReady);
  CHECK_EQ(bridge.Post(nav::Command::kPrevious, nav::Producer::kControlsPage).status,
           nav::PostStatus::kAccepted);
  const auto obsolete = bridge.Take();
  CHECK_TRUE(obsolete);
  const auto new_epoch = bridge.Reset();
  CHECK_TRUE(new_epoch > epoch);
  snapshot = bridge.ReadSnapshot();
  CHECK_TRUE(snapshot.revision > copy.revision);
  CHECK_FALSE(snapshot.available);
  CHECK_FALSE(snapshot.pending);
  CHECK_TRUE(snapshot.result && snapshot.result->status == nav::CompletionStatus::kCancelled);
  CHECK_FALSE(bridge.Publish(epoch, Track(1)));
  CHECK_FALSE(bridge.PublishArtwork(epoch, 9, "art9", ArtworkState::kReady));
  CHECK_EQ(bridge.Post(nav::Command::kNext, nav::Producer::kPlayer, epoch).status,
           nav::PostStatus::kStaleSession);
  if (obsolete) CHECK_FALSE(bridge.Complete(*obsolete, nav::CompletionStatus::kApplied));
  CHECK_TRUE(copy.playback.track.title == "title9");  // immutable owned copy survives reset
  CHECK_TRUE(bridge.Publish(new_epoch, Track(10)));
  bridge.SetAvailable(true);
  const auto next = bridge.Post(nav::Command::kNext, nav::Producer::kPlayer, new_epoch);
  CHECK_TRUE(next.id > posted.id);
  bridge.SetAvailable(false);
  CHECK_FALSE(bridge.Take());
  CHECK_FALSE(bridge.ReadSnapshot().pending);

  BeginCase("bridge: shared navigator API starts unavailable and uses the same mailbox");
  NowPlaying().Reset();
  CHECK_FALSE(nav::Available());
  CHECK_EQ(nav::Post(nav::Command::kNext).status, nav::PostStatus::kUnavailable);
  CHECK_TRUE(NowPlaying().Publish(NowPlaying().ReadSnapshot().epoch, Track(0)));
  nav::SetAvailable(true);
  CHECK_TRUE(nav::Available());
  CHECK_EQ(nav::Post(nav::Command::kNext).status, nav::PostStatus::kAccepted);
  auto shared = nav::Take();
  CHECK_TRUE(shared);
  if (shared) CHECK_TRUE(NowPlaying().Complete(*shared, nav::CompletionStatus::kApplied));
  NowPlaying().Reset();
}

void TestCoherentPublication() {
  BeginCase("bridge: concurrent rendering reads one coherent owned track revision");
  NowPlayingBridge bridge;
  const auto epoch = bridge.ReadSnapshot().epoch;
  bridge.Publish(epoch, Track(0));
  std::atomic<bool> done = false;
  std::jthread publisher([&] {
    for (size_t id = 1; id < 2000; ++id) bridge.Publish(epoch, Track(id));
    done = true;
  });
  bool coherent = true;
  uint64_t revision = 0;
  do {
    auto snapshot = bridge.ReadSnapshot();
    const auto id = std::to_string(snapshot.playback.track.song);
    coherent &= snapshot.playback.track.title == "title" + id &&
                snapshot.playback.track.artist == "artist" + id && snapshot.revision >= revision;
    revision = snapshot.revision;
  } while (!done);
  publisher.join();
  CHECK_TRUE(coherent);
}

void TestSessionAndSelection() {
  BeginCase("navigator: five-second boundary and first-song restart use shared session history");
  CHECK_EQ(nav::PreviousAction(nav::kRestartSamples - 1, 1), nav::Previous::kStepBack);
  CHECK_EQ(nav::PreviousAction(nav::kRestartSamples, 1), nav::Previous::kRestart);
  CHECK_EQ(nav::PreviousAction(0, 0), nav::Previous::kRestart);
  CHECK_EQ(nav::PreviousAction(0, kNoIndex), nav::Previous::kRestart);

  MusicSession session;
  PoolEntry song{"a", "songs/a/a", "a", "A title", "An artist", "songs/a/gen/a_keep.png_xbox"};
  session.Discover(1, {song});
  session.Discover(2, {{"b", "songs/b/b", "b"}});
  session.DiscoveryComplete();
  std::mt19937 rng(42);
  session.PlanAhead(rng);
  CHECK_TRUE(session.Select(session.NextPosition()));
  const auto before = session.Cursor();
  const auto target = session.NextPosition();
  CHECK_TRUE(target != kNoIndex);
  // A cold device is not an excuse to advance the shared audible cursor.
  struct Device { size_t open_file_count() const { return 0; } } device;
  MusicMountCache<Device> cache;
  CHECK_FALSE(cache.Contains(session.Package(session.SongAt(target))));
  CHECK_EQ(session.Cursor(), before);
  CHECK_TRUE(cache.Insert(session.Package(session.SongAt(target)), &device));
  CHECK_TRUE(session.Select(target));
  const auto length = session.Playlist().size();
  CHECK_TRUE(session.Select(session.PreviousPosition(0)));
  CHECK_EQ(session.NextPosition(), target);
  CHECK_TRUE(session.Select(session.NextPosition()));
  CHECK_EQ(session.Playlist().size(), length);
  CHECK_EQ(session.PreviousPosition(nav::kRestartSamples), target);
  CHECK_EQ(cache.RetireCold({}, [](size_t) { return true; }), 1u);
  CHECK_TRUE(session.Sources()[0].title == song.title);
  CHECK_TRUE(session.Sources()[0].artist == song.artist);
  CHECK_TRUE(session.Sources()[0].artwork_locator == song.artwork_locator);

  BeginCase("selection: manual skip clears automatic grace without rewriting a captured press");
  PlaySelection selection;
  selection.Audible(4);
  selection.Loading(5, 100);
  CHECK_EQ(selection.Choose(101), 4);
  selection.Capture(101);
  selection.Skipped(6, 102);
  CHECK_EQ(selection.Choose(103), 6);
  CHECK_EQ(selection.Query(103), 4);
  selection.Complete();
  CHECK_EQ(selection.Query(103), 6);
  selection.Failed(6);
  CHECK_EQ(selection.Choose(104), -1);
}

int main() {
  TestLatch();
  TestBridge();
  TestCoherentPublication();
  TestSessionAndSelection();
  return Finish();
}
