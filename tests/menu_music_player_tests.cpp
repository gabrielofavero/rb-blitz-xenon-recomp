// SPDX-License-Identifier: GPL-2.0-only
#include <atomic>
#include <chrono>

#include "assets/texture_codec.h"
#include "check.h"
#include "ui/menu_music_artwork.h"
#include "ui/menu_music_player_policy.h"
#include "ui/menu_music_text.h"
#include "ui/text_marquee.h"
using namespace rb_blitz;
using namespace ui::music;
using test::BeginCase;

namespace {
void TextTests() {
  BeginCase("music player converts Latin-1 DTA titles and preserves UTF-8 titles");
  CHECK_TRUE(DisplayTextUtf8("California \xDC" "ber Alles") ==
             "California \xC3\x9C" "ber Alles");
  CHECK_TRUE(DisplayTextUtf8("California \xC3\x9C" "ber Alles") ==
             "California \xC3\x9C" "ber Alles");
  CHECK_TRUE(DisplayTextUtf8("Bj\xF6rk and Caf\xC3\xA9") ==
             "Bj\xC3\xB6rk and Caf\xC3\xA9");

  BeginCase("long titles pause at either end and scroll back to their start");
  ui::TextMarquee title;
  CHECK_EQ(title.Offset(90, 100, 1, 1), 0);
  CHECK_EQ(title.Offset(100, 100, 1, 25), 0);
  constexpr double start = 100;
  const auto shift = [&](double elapsed) { return title.Offset(154, 100, 1, start + elapsed); };
  CHECK_EQ(shift(0), 0);
  CHECK_EQ(shift(1.25), 0);
  CHECK_EQ(shift(2.75), 27);
  CHECK_EQ(shift(4.25), 54);
  CHECK_EQ(shift(5.5), 54);
  CHECK_EQ(shift(7), 27);
  CHECK_EQ(shift(8.5), 0);
  CHECK_EQ(shift(11.25), 27);

  BeginCase("title changes and narrower or wider layouts restart at the beginning");
  title.Reset();
  CHECK_EQ(shift(12), 0);
  CHECK_EQ(shift(14.75), 27);
  CHECK_EQ(title.Offset(154, 110, 1, 115), 0);
  CHECK_EQ(title.Offset(154, 180, 1, 120), 0);
  CHECK_EQ(title.Offset(77, 50, .5f, 123), 0);
  CHECK_EQ(title.Offset(77, 50, .5f, 125.75), 13.5);
  CHECK_EQ(title.Offset(87, 60, .5f, 126), 0);
}
MenuPoint Center(MenuBounds b) {
  return {(b.left + b.right) / 2, (b.top + b.bottom) / 2};
}
template <typename F>
bool Until(F f) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < end) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}
std::vector<uint8_t> CoverBytes() {
  std::vector<uint8_t> rgba(256 * 256 * 4, 255);
  auto bytes = assets::BuildPlatformTexture(
      rgba.data(), 256, 256, assets::TextureFormat::kDxt1, 1, true);
  return {bytes.begin(), bytes.end()};
}
void PointerTests() {
  BeginCase(
      "player layout and physical/logical hit conversion at viewport and DPI "
      "sizes");
  for (auto size :
       {MenuPoint{1280, 720}, {2560, 1080}, {640, 360}, {1920, 1080}}) {
    const auto layout = PlayerLayout(size.x, size.y);
    CHECK_TRUE(layout.panel.left > 0 && layout.panel.right < size.x);
    CHECK_TRUE(layout.panel.top > size.y * .6f &&
               layout.panel.bottom < size.y - 8);
    for (auto b : layout.controls) {
      CHECK_TRUE(layout.panel.Contains(Center(b)));
      const auto center = Center(b);
      const auto p =
          LogicalPointer(center.x * 1.5f, center.y * 1.5f, size.x * 1.5f,
                         size.y * 1.5f, size.x, size.y);
      CHECK_TRUE(b.Contains(p));
    }
  }
  const auto wide = PlayerLayout(2520, 1080), superwide = PlayerLayout(3840, 1080);
  CHECK_EQ(superwide.panel.right - wide.panel.right, (3840 - 2520) / 2);
  CHECK_EQ(superwide.panel.bottom, wide.panel.bottom);
  CHECK_EQ(superwide.panel.right - superwide.panel.left, wide.panel.right - wide.panel.left);
  CHECK_EQ(PlayerLayout(0, 0).scale, 0);
  BeginCase("portrait music player fills the bottom edge and respects the safe area");
  for (const auto size : {MenuPoint{320, 568}, {402, 874}, {768, 1024}}) {
    const auto compact = PlayerLayout(size.x, size.y);
    CHECK_TRUE(compact.compact);
    CHECK_EQ(compact.panel.left, 0);
    CHECK_EQ(compact.panel.right, size.x);
    CHECK_TRUE(compact.panel.top > size.y * .75f);
    CHECK_EQ(compact.panel.bottom, size.y);
    CHECK_TRUE(compact.expanded_cover_size > 140);
    CHECK_TRUE(compact.controls[0].left > compact.panel.left + 100);
    for (int i = 0; i < 3; ++i) {
      const auto control = compact.controls[i];
      CHECK_TRUE(control.right - control.left >= 43.99f);
      CHECK_TRUE(control.bottom - control.top >= 43.99f);
      CHECK_TRUE(compact.panel.Contains(Center(control)));
      if (i) CHECK_TRUE(compact.controls[i - 1].right < control.left);
      PointerRouter touch;
      touch.Present(compact, 1, 7, true, {true, true, true});
      CHECK_TRUE(touch.Route(PointerEvent::kDown, Center(control)).consumed);
      CHECK_EQ(*touch.Route(PointerEvent::kUp, Center(control)).clicked, i);
    }
  }
  CHECK_FALSE(PlayerLayout(1280, 720).compact);
  const auto narrow = PlayerLayout(203, 360);
  CHECK_TRUE(narrow.compact && narrow.scale > 0);
  CHECK_EQ(narrow.panel.right, 203);
  const auto safe = PlayerLayout(402, 874, {13, 58, 392, 840});
  CHECK_EQ(safe.panel.bottom, 874);
  CHECK_TRUE(safe.cover.left >= 13 && safe.controls[2].right <= 392);
  CHECK_TRUE(safe.controls[2].bottom <= 840 && safe.expanded_top >= 58);
  CHECK_TRUE(safe.content.left >= 13 + 27 * safe.scale);
  CHECK_TRUE(safe.content.right <= 392 - 27 * safe.scale);
  CHECK_TRUE(safe.expanded_cover_size <= 288 * safe.scale);
  CHECK_TRUE(safe.expanded_controls[1].bottom + 194 * safe.scale <= 840);
  BeginCase(
      "one release on the armed control posts once; hold, disabled, drag and "
      "boundaries");
  PointerRouter router;
  const auto layout = PlayerLayout(1280, 720);
  const auto a = Center(layout.controls[0]), b = Center(layout.controls[2]);
  router.Present(layout, 1, 7, true, {true, false, true});
  CHECK_FALSE(router.Route(PointerEvent::kDown, {1, 1}).consumed);
  CHECK_TRUE(router.Route(PointerEvent::kDown, a).consumed);
  CHECK_FALSE(router.Route(PointerEvent::kMove, a).clicked.has_value());
  CHECK_FALSE(router.Route(PointerEvent::kDown, a).clicked.has_value());
  CHECK_EQ(*router.Route(PointerEvent::kUp, a).clicked, 0u);
  CHECK_FALSE(router.Route(PointerEvent::kUp, a).clicked.has_value());
  router.Route(PointerEvent::kDown, a);
  router.Route(PointerEvent::kMove, b);
  CHECK_FALSE(router.Route(PointerEvent::kUp, b).clicked.has_value());
  router.Route(PointerEvent::kDown, Center(layout.controls[1]));
  CHECK_FALSE(router.Route(PointerEvent::kUp, Center(layout.controls[1]))
                  .clicked.has_value());
  CHECK_TRUE(router.Route(PointerEvent::kWheel, a).consumed);
  router.Route(PointerEvent::kDown, a);
  router.Cancel();
  CHECK_TRUE(router.Route(PointerEvent::kUp, a).consumed);
  CHECK_FALSE(router.Route(PointerEvent::kUp, a).clicked.has_value());
  router.Present(layout, 1, 7, true, {true, true, true});
  router.Route(PointerEvent::kDown, a);
  router.Present(layout, 2, 7, true, {true, true, true});
  CHECK_FALSE(router.Route(PointerEvent::kUp, a).clicked.has_value());
  router.Route(PointerEvent::kDown, a);
  router.Present(PlayerLayout(1920, 1080), 2, 7, true, {true, true, true});
  CHECK_FALSE(router.Route(PointerEvent::kUp, a).clicked.has_value());
  router.Present(layout, 2, 7, true, {true, true, true});
  router.Route(PointerEvent::kDown, a);
  router.Present(layout, 2, 8, true, {true, true, true});
  CHECK_FALSE(router.Route(PointerEvent::kUp, a).clicked.has_value());
  router.Route(PointerEvent::kDown, a, 1);
  CHECK_FALSE(router.Route(PointerEvent::kUp, a, 1).clicked.has_value());
  router.Present(layout, 2, 8, false, {true, true, true});
  CHECK_FALSE(router.Route(PointerEvent::kWheel, a).consumed);
}

void DrawerTests() {
  BeginCase("portrait handle follows the pointer, settles open and keeps the live transport");
  PointerRouter router;
  const auto layout = PlayerLayout(402, 874);
  const auto handle = Center(layout.Handle(0));
  const auto settle = [&] {
    for (int i = 0; i < 40; ++i) router.Animate(1.f / 60);
  };
  const auto down = [&](MenuPoint p) { return router.Route(PointerEvent::kDown, p).consumed; };
  const auto move = [&](MenuPoint p) { return router.Route(PointerEvent::kMove, p).consumed; };
  const auto up = [&](MenuPoint p) { return router.Route(PointerEvent::kUp, p); };
  router.Present(layout, 1, 7, true, {true, true, true});
  CHECK_TRUE(down(handle));
  CHECK_TRUE(move({handle.x, handle.y - 90}));
  router.Animate(1.f / 60);
  CHECK_TRUE(router.expansion() > 0 && router.expansion() < 1);
  CHECK_TRUE(std::abs(layout.Panel(router.expansion()).top - (layout.panel.top - 90)) < .01f);
  CHECK_TRUE(up({handle.x, handle.y - 90}).consumed);
  settle();
  CHECK_EQ(router.expansion(), 1);
  router.Present(layout, 1, 8, true, {true, true, true});
  CHECK_EQ(router.expansion(), 1);
  const auto next = Center(layout.expanded_controls[2]);
  CHECK_TRUE(down(next));
  CHECK_EQ(up(next).clicked.value_or(-1), 2);
  const auto upper_handle = Center(layout.Handle(1));
  CHECK_TRUE(down(upper_handle));
  CHECK_TRUE(up({upper_handle.x, upper_handle.y + 120}).consumed);
  settle();
  CHECK_EQ(router.expansion(), 0);

  BeginCase("drawer taps dismiss safely and cannot take over a gesture started on the guest");
  const MenuPoint guest{120, 100};
  CHECK_FALSE(down(guest));
  CHECK_FALSE(move(handle));
  CHECK_FALSE(up(handle).consumed);
  CHECK_TRUE(down(handle));
  CHECK_TRUE(up(handle).consumed);
  settle();
  CHECK_EQ(router.expansion(), 1);
  CHECK_TRUE(down(guest));
  CHECK_TRUE(up(guest).consumed);
  settle();
  CHECK_EQ(router.expansion(), 0);
  CHECK_TRUE(down(handle));
  router.Cancel();
  CHECK_TRUE(up(guest).consumed);
  settle();
  CHECK_EQ(router.expansion(), 0);
}
void ArtworkTests() {
  BeginCase(
      "exact package-relative art selection handles single and merged layouts");
  const std::vector<std::string> assets = {
      "\\songs\\one\\gen\\one_keep.png_xbox",
      "\\songs\\two\\gen\\two_keep.png_xbox",
      "\\songs\\wrong\\gen\\one_keep.png_xbox"};
  CHECK_TRUE(ResolveArtwork("songs/ONE/one", assets) == assets[0]);
  CHECK_TRUE(ResolveArtwork("songs/two/two", assets) == assets[1]);
  CHECK_TRUE(ResolveArtwork("songs/absent/one", assets).empty());
  BeginCase(
      "Xbox cover decode is bounded and rejects corrupt and oversized assets");
  const auto bytes = CoverBytes();
  const auto cover = DecodeCover({1, 0, "cover"}, bytes);
  CHECK_EQ(cover->state, shell_music::ArtworkState::kReady);
  CHECK_EQ(cover->rgba.size(), 256 * 256 * 4u);
  auto v1 = bytes;
  std::copy(bytes.begin() + 5, bytes.begin() + 17, v1.begin() + 1);
  std::fill(v1.begin() + 13, v1.begin() + 32, 0);
  v1[0] = 1;
  CHECK_EQ(DecodeCover({1, 0, "v1"}, v1)->state,
           shell_music::ArtworkState::kReady);
  CHECK_TRUE(DecodeCover({1, 0, "v1"}, v1)->rgba == cover->rgba);
  CHECK_EQ(DecodeCover({1, 0, "bad"}, {1, 2, 3})->state,
           shell_music::ArtworkState::kError);
  auto oversized = bytes;
  oversized[12] = 8;  // width 2048, rejected before allocation
  CHECK_TRUE(DecodeCover({1, 0, "bad"}, oversized)->rgba.empty());
  BeginCase(
      "latest-only queued work, stale completions, off/reset and bounded LRU "
      "cache");
  std::mutex mutex;
  std::condition_variable wake;
  bool entered = false, release = false;
  std::atomic<int> calls = 0;
  Artwork worker([&](const ArtTag& tag, std::vector<uint8_t>* out) {
    ++calls;
    if (tag.key == "slow") {
      std::unique_lock lock(mutex);
      entered = true;
      wake.notify_all();
      wake.wait(lock, [&] { return release; });
    }
    *out = bytes;
    return ArtRead::kReady;
  });
  worker.Request(ArtTag{1, 0, "slow"});
  {
    std::unique_lock lock(mutex);
    CHECK_TRUE(
        wake.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }));
  }
  worker.Request(ArtTag{1, 1, "superseded"});
  const ArtTag final{1, 2, "final"};
  worker.Request(final);
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  wake.notify_one();
  std::shared_ptr<const Cover> current;
  CHECK_TRUE(Until([&] {
    current = worker.Request(final);
    return current != nullptr;
  }));
  CHECK_TRUE(current && current->tag == final);
  CHECK_EQ(calls,
           2u);  // middle request was never read; old cover cannot attach
  worker.Request({});
  CHECK_FALSE(worker.Request(final) !=
              nullptr);  // disable clears even a ready cover
  CHECK_TRUE(Until([&] { return worker.Request(final) != nullptr; }));
  for (size_t i = 3; i < 14; ++i) {
    const ArtTag tag{1, i, "cover" + std::to_string(i)};
    CHECK_TRUE(Until([&] { return worker.Request(tag) != nullptr; }));
  }
  const int before = calls;
  CHECK_FALSE(worker.Request(final) !=
              nullptr);  // ninth older cover was evicted
  CHECK_TRUE(Until([&] { return worker.Request(final) != nullptr; }));
  CHECK_TRUE(calls > before);
  const ArtTag epoch{2, 2, "final"};
  CHECK_FALSE(worker.Request(epoch) != nullptr);
  CHECK_TRUE(Until([&] { return worker.Request(epoch) != nullptr; }));
  BeginCase("deferred leases retry on the worker and stop on disable");
  std::atomic<int> attempts = 0;
  Artwork deferred([&](const ArtTag&, std::vector<uint8_t>* out) {
    if (++attempts == 1) return ArtRead::kDeferred;
    *out = bytes;
    return ArtRead::kReady;
  });
  CHECK_TRUE(Until([&] { return deferred.Request(final) != nullptr; }));
  CHECK_TRUE(attempts >= 2);
  deferred.Request({});
}
}  // namespace
int main() {
  TextTests();
  PointerTests();
  DrawerTests();
  ArtworkTests();
  return test::Finish();
}
