// SPDX-License-Identifier: GPL-2.0-only
#include "check.h"
#include "hooks/menu_music_session.h"
#include "hooks/menu_music_playback.h"

using namespace rb_blitz::shell_music;

namespace {
PoolEntry Song(size_t package, std::string name) {
  return {"menumusic" + std::to_string(package), "songs/" + name + "/audio", std::move(name)};
}
void Discover(MusicSession& session, size_t package) {
  session.Discover(package, {Song(package, "Song" + std::to_string(package))});
}
}

int main() {
  std::mt19937 rng(42);
  rb_blitz::test::BeginCase("a lazy session crosses 50 songs before beginning another pass");
  MusicSession session;
  Discover(session, 0);
  session.PlanAhead(rng);
  CHECK_EQ(session.Sources().size(), 1u);
  CHECK_EQ(session.Playlist().size(), 1u);
  CHECK_TRUE(session.NeedsDiscovery());
  std::set<size_t> heard;
  size_t discovered = 1, previous = kNoIndex;
  for (size_t played = 0; played < 150; ++played) {
    while (discovered < 150 && session.Playlist().size() < played + 4) {
      Discover(session, discovered++);
      session.PlanAhead(rng);
    }
    if (discovered == 150) session.DiscoveryComplete();
    session.PlanAhead(rng);
    const auto position = session.NextPosition();
    CHECK_TRUE(position != kNoIndex);
    if (position == kNoIndex) break;
    CHECK_TRUE(session.Select(position));
    const auto id = session.SongAt(position);
    CHECK_TRUE(heard.insert(id).second);
    CHECK_TRUE(id != previous);
    CHECK_EQ(session.Playlist()[position].pass, 0u);
    session.Heard(id);
    previous = id;
  }
  CHECK_EQ(heard.size(), 150u);
  session.PlanAhead(rng);
  const auto next = session.NextPosition();
  CHECK_TRUE(next != kNoIndex);
  if (next != kNoIndex) {
    CHECK_TRUE(session.SongAt(next) != previous);
    CHECK_EQ(session.Playlist()[next].pass, 1u);
  }

  rb_blitz::test::BeginCase("a merged pack has a large catalog and only a small planned window");
  MusicSession merged;
  std::vector<PoolEntry> songs;
  for (size_t i = 0; i < 200; ++i) songs.push_back(Song(7, "Merged" + std::to_string(i)));
  CHECK_EQ(merged.Discover(7, std::move(songs)), 200u);
  merged.PlanAhead(rng);
  CHECK_EQ(merged.Sources().size(), 200u);
  CHECK_EQ(merged.Playlist().size(), 4u);
  CHECK_FALSE(merged.NeedsDiscovery());
  CHECK_EQ(merged.WarmPackages().size(), 1u);
  CHECK_TRUE(merged.WarmPackages().contains(7));

  rb_blitz::test::BeginCase("a cursor follows shared backward and forward history without redrawing it");
  MusicSession history;
  for (size_t i = 0; i < 20; ++i) Discover(history, i);
  history.DiscoveryComplete();
  for (size_t i = 0; i < 10; ++i) {
    history.PlanAhead(rng);
    CHECK_TRUE(history.Select(history.NextPosition()));
    history.Heard(history.SongAt(history.Cursor()));
  }
  history.PlanAhead(rng);
  const auto cursor = history.Cursor();
  const auto forward = history.SongAt(cursor);
  const auto length = history.Playlist().size();
  CHECK_EQ(history.PreviousPosition(5 * 44100), cursor);
  CHECK_EQ(history.PreviousPosition(5 * 44100 - 1), cursor-1);
  CHECK_TRUE(history.Select(history.PreviousPosition(0)));
  CHECK_EQ(history.NextPosition(), cursor);
  CHECK_TRUE(history.Select(history.NextPosition()));
  CHECK_EQ(history.SongAt(history.Cursor()), forward);
  CHECK_EQ(history.Playlist().size(), length);
  CHECK_TRUE(history.Select(0));
  CHECK_EQ(history.PreviousPosition(0), 0u);

  rb_blitz::test::BeginCase("warmth is bounded independently of history and explicit source pins");
  CHECK_TRUE(history.Select(cursor));
  auto warm = history.WarmPackages();
  CHECK_EQ(warm.size(), 6u); // current, 3 upcoming, 2 recent
  const auto pinned = history.Package(history.SongAt(0));
  warm = history.WarmPackages({history.SongAt(0)});
  CHECK_TRUE(warm.contains(pinned));
  CHECK_EQ(warm.size(), 7u);
  CHECK_TRUE(history.Sources().size() > warm.size());

  rb_blitz::test::BeginCase("stable metadata IDs preserve a captured press across growth");
  PlaySelection selection;
  selection.Audible(0);
  selection.Capture(0);
  const auto key = PoolSongKey(history.Sources()[0]);
  for (size_t i = 20; i < 250; ++i) Discover(history, i);
  CHECK_EQ(selection.Query(1), 0u);
  CHECK_TRUE(PoolSongKey(history.Sources()[0]) == key);
  CHECK_EQ(history.Discover(999, {Song(999, "sOnG0")}), 0u);

  rb_blitz::test::BeginCase("failures and missing packages cannot poison the future window");
  MusicSession failures;
  for (size_t i = 0; i < 8; ++i) Discover(failures, i);
  failures.DiscoveryComplete();
  failures.PlanAhead(rng);
  const auto bad = failures.SongAt(failures.NextPosition());
  failures.Failed(bad); failures.Failed(bad); failures.Failed(bad);
  CHECK_FALSE(failures.Usable(bad));
  failures.PlanAhead(rng);
  CHECK_TRUE(failures.NextPosition() != kNoIndex);
  CHECK_TRUE(failures.SongAt(failures.NextPosition()) != bad);
  const auto missing = failures.Package(failures.SongAt(failures.NextPosition()));
  failures.BlockPackage(missing);
  failures.PlanAhead(rng);
  CHECK_TRUE(failures.Package(failures.SongAt(failures.NextPosition())) != missing);
  CHECK_FALSE(failures.WasHeard(bad));
  CHECK_TRUE(failures.Select(failures.NextPosition()));
  failures.Skipped();
  CHECK_TRUE(failures.Playlist()[failures.Cursor()].outcome == MusicSession::Outcome::kSkipped);

  rb_blitz::test::BeginCase("a sole song never spins through repeated passes or repeats itself");
  MusicSession sole;
  Discover(sole, 0);
  sole.DiscoveryComplete();
  sole.PlanAhead(rng);
  CHECK_EQ(sole.Playlist().size(), 1u);
  CHECK_TRUE(sole.Select(sole.NextPosition()));
  for (int i = 0; i < 20; ++i) sole.PlanAhead(rng);
  CHECK_EQ(sole.Playlist().size(), 1u);
  CHECK_TRUE(sole.NextPosition() == kNoIndex);
  return rb_blitz::test::Finish();
}
