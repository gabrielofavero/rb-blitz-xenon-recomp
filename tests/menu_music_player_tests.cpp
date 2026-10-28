// SPDX-License-Identifier: GPL-2.0-only
#include <atomic>
#include <chrono>

#include "assets/texture_codec.h"
#include "check.h"
#include "ui/menu_music_artwork.h"
#include "ui/menu_music_player_policy.h"
#include "ui/menu_music_text.h"
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
  CHECK_EQ(PlayerLayout(0, 0).scale, 0);
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
  ArtworkTests();
  return test::Finish();
}
