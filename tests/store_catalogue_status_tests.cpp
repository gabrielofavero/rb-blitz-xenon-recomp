// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for P7's store sentences (src/store/store_catalogue_status.h).
//
// What these pin down (plan §5/P7, "refine the required RhythmVerse heading/credit" and
// "empty-state and offline text"):
//   * nothing is said before the store served a record, and nothing after the surface window - the
//     strip must not follow a player out of the store;
//   * the credit line is drawn when the store opens, and expires after its hold;
//   * a failure is the loudest sentence, then the empty list, then the loading line, then the credit;
//   * the loading line appears only while nothing is on screen yet, and counts the pages read;
//   * the diagnostics switch (`store_catalogue_off`) explains its own silence;
//   * the server's own reasons are carried, and an absurd one is shortened rather than drawn whole.

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#include "check.h"
#include "store/store_catalogue_status.h"

namespace {

using namespace rb_blitz::store;

namespace test = rb_blitz::test;

void CheckStr(const char* file, int line, std::string_view actual, std::string_view expected) {
  ++test::g_checks;
  if (actual == expected) {
    return;
  }
  test::Fail(file, line, "expected \"" + std::string(expected) + "\"\n         actual   \"" +
                             std::string(actual) + "\"");
}

#define CHECK_STR(actual, expected) CheckStr(__FILE__, __LINE__, (actual), (expected))

void SleepMs(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

void Reset() {
  ResetCatalogueStatus();
  SetCatalogueStatusTiming(kCatalogueSurfaceWindow, kCatalogueCreditHold);
}

void TestSilence() {
  test::BeginCase("nothing is said before a surface is served");
  Reset();
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0, "HTTP 503");
  CHECK_TRUE(CatalogueStatusText().empty());
  PublishCatalogueFetchState(CatalogueFetchState::kReady, 4, 0);
  CHECK_TRUE(CatalogueStatusText().empty());
}

void TestCredit() {
  test::BeginCase("the credit line follows the store's opening and expires");
  Reset();
  SetCatalogueStatusTiming(std::chrono::milliseconds(300), std::chrono::milliseconds(80));
  PublishCatalogueFetchState(CatalogueFetchState::kReady, 4, 99);
  NoteStoreSurface(StoreSurface::kFrontMenu);
  CHECK_STR(CatalogueStatusText(), kStoreCreditText);
  SleepMs(100);  // past the credit hold, still inside the surface window
  CHECK_TRUE(CatalogueStatusText().empty());
  NoteStoreSurface(StoreSurface::kFrontMenu);
  CHECK_STR(CatalogueStatusText(), kStoreCreditText);

  // A menu page is not the store opening: the credit belongs to the front menu.
  Reset();
  SetCatalogueStatusTiming(std::chrono::milliseconds(300), std::chrono::milliseconds(300));
  PublishCatalogueFetchState(CatalogueFetchState::kReady, 4, 99);
  NoteStoreSurface(StoreSurface::kMenuPage);
  CHECK_TRUE(CatalogueStatusText().empty());
}

void TestLoading() {
  test::BeginCase("the loading line names the pages read, and only while nothing is on screen");
  Reset();
  NoteStoreSurface(StoreSurface::kMenuPage);
  PublishCatalogueFetchState(CatalogueFetchState::kFetching, 0, 0);
  CHECK_STR(CatalogueStatusText(), std::string(kCatalogueLoadingText) + "...");
  PublishCatalogueFetchState(CatalogueFetchState::kFetching, 2, 0);
  const std::string two_pages = CatalogueStatusText();
  CHECK_TRUE(two_pages.find(kCatalogueLoadingText) == 0);
  CHECK_TRUE(two_pages.find("2 pages") != std::string::npos);
  // Offers already on screen: a loading line over them would be noise.
  PublishCatalogueFetchState(CatalogueFetchState::kFetching, 2, 24);
  CHECK_TRUE(CatalogueStatusText().empty());
}

void TestEmpty() {
  test::BeginCase("an empty list says where the filter lives");
  Reset();
  NoteStoreSurface(StoreSurface::kMenuPage);
  PublishCatalogueFetchState(CatalogueFetchState::kReady, 4, 0);
  CHECK_STR(CatalogueStatusText(), kCatalogueEmptyText);
  // ...and a full list has nothing to explain.
  PublishCatalogueFetchState(CatalogueFetchState::kReady, 4, 24);
  CHECK_TRUE(CatalogueStatusText().empty());
}

void TestOffline() {
  test::BeginCase("a failure is the loudest sentence, with the server's own reason");
  Reset();
  NoteStoreSurface(StoreSurface::kMenuPage);
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0, "the catalogue answered HTTP 503");
  const std::string failed = CatalogueStatusText();
  CHECK_TRUE(failed.find("could not be reached") != std::string::npos);
  CHECK_TRUE(failed.find("HTTP 503") != std::string::npos);
  CHECK_TRUE(failed.find("Press Back") != std::string::npos);

  // A failure the catalogue cannot explain still says what it can.
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0);
  CHECK_TRUE(CatalogueStatusText().find("could not be reached") != std::string::npos);

  // An absurd reason is shortened, not drawn whole.
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0, std::string(400, 'x'));
  const std::string long_reason = CatalogueStatusText();
  CHECK_TRUE(long_reason.size() < 260);
  CHECK_TRUE(long_reason.find("...") != std::string::npos);

  // A failure outranks an empty list: the player has to know which of the two it is.
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0, "timeout");
  CHECK_TRUE(CatalogueStatusText().find("could not be reached") != std::string::npos);
}

void TestDisabled() {
  test::BeginCase("the diagnostics switch explains its own silence");
  Reset();
  SetCatalogueFetchEnabled(false);
  NoteStoreSurface(StoreSurface::kFrontMenu);
  PublishCatalogueFetchState(CatalogueFetchState::kIdle, 0, 0);
  CHECK_STR(CatalogueStatusText(), kCatalogueDisabledText);
  SetCatalogueFetchEnabled(true);
  CHECK_STR(CatalogueStatusText(), kStoreCreditText);
}

void TestWindow() {
  test::BeginCase("the strip stops when the store is no longer the surface");
  Reset();
  SetCatalogueStatusTiming(std::chrono::milliseconds(40), std::chrono::milliseconds(40));
  NoteStoreSurface(StoreSurface::kMenuPage);
  PublishCatalogueFetchState(CatalogueFetchState::kFailed, 0, 0, "HTTP 500");
  CHECK_TRUE(!CatalogueStatusText().empty());
  SleepMs(60);
  CHECK_TRUE(CatalogueStatusText().empty());
  // Serving a record again (a later visit) brings the sentence back.
  NoteStoreSurface(StoreSurface::kMenuPage);
  CHECK_TRUE(!CatalogueStatusText().empty());
  Reset();
  CHECK_TRUE(CatalogueStatusText().empty());
}

void TestStrings() {
  test::BeginCase("the sentences the plan asks for are the ones the module carries");
  // Plan §1.8's attribution: "Customs from RhythmVerse", in the site's own spelling.
  CHECK_STR(std::string(kStoreCreditText), "Customs from RhythmVerse");
  CHECK_TRUE(std::string(kCatalogueEmptyText).find("RhythmVerse") == 0);
  CHECK_TRUE(std::string(kCatalogueDisabledText).find("RhythmVerse") != std::string::npos);
}

}  // namespace

int main() {
  TestSilence();
  TestCredit();
  TestLoading();
  TestEmpty();
  TestOffline();
  TestDisabled();
  TestWindow();
  TestStrings();
  ResetCatalogueStatus();
  return test::Finish();
}
