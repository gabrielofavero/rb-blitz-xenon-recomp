// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_catalogue_status.h: the store's credit, empty-state and offline sentences, and the
// precedence between them.

#include "store/store_catalogue_status.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>

#include "ui/notifications.h"

namespace rb_blitz::store {
namespace {

using Clock = std::chrono::steady_clock;

std::mutex g_mutex;
Clock::time_point g_last_surface{};
bool g_surface_seen = false;
CatalogueFetchState g_state = CatalogueFetchState::kIdle;
int g_pages = 0;
std::size_t g_offers = 0;
std::string g_reason;
bool g_fetch_enabled = true;
bool g_incomplete = false;
std::chrono::milliseconds g_surface_window = kCatalogueSurfaceWindow;
std::string g_notice;

// A reason can be a transport sentence ("WinHttpConnect: win32 error 12029"); the strip wraps at
// the caption width, so only an absurd one is shortened. The sentence stays readable either way.
std::string Shortened(std::string reason) {
  constexpr std::size_t kMaxReasonChars = 120;
  if (reason.size() <= kMaxReasonChars) {
    return reason;
  }
  return reason.substr(0, kMaxReasonChars) + "...";
}

std::string OfflineText(const std::string& reason) {
  if (reason.empty()) {
    return "RhythmVerse could not be reached. Press Back and open Recently Added again.";
  }
  return "RhythmVerse could not be reached: " + Shortened(reason) +
         ". Press Back and open Recently Added again.";
}

std::string LoadingText(int pages) {
  std::string text(kCatalogueLoadingText);
  text += "...";
  if (pages > 1) {
    text += " (" + std::to_string(pages) + " pages)";
  }
  return text;
}

// Called at state changes, never per rendered frame. Preserve the existing surface gate:
// background prefetches and a catalogue that was never opened must remain silent.
void NotifyLocked() {
  namespace notifications = ui::notifications;
  using notifications::Level;
  if (!g_surface_seen || Clock::now() - g_last_surface > g_surface_window) return;
  std::string state;
  Level level = Level::kInfo;
  std::string title;
  std::string description;
  if (!g_fetch_enabled) {
    state = "disabled";
    title = "RhythmVerse is disabled";
    description = kCatalogueDisabledText;
  } else if (g_state == CatalogueFetchState::kFailed || g_incomplete) {
    state = "failed";
    level = g_offers ? Level::kWarning : Level::kError;
    title = g_offers ? "RhythmVerse catalogue incomplete" : "RhythmVerse connection failed";
    description = g_offers ? "Some songs could not be loaded. Reopen Recently Added to try again."
        : "Could not load the catalogue. Check your connection and reopen Recently Added.";
  } else if (g_state == CatalogueFetchState::kReady && g_offers == 0) {
    state = "empty";
    title = "No RhythmVerse songs found";
    description = kCatalogueEmptyText;
  } else if (g_state == CatalogueFetchState::kFetching && g_offers == 0) {
    state = "loading";
    title = "RhythmVerse";
    description = "Loading the song catalogue...";
  }
  if (state == g_notice) return;
  g_notice = state;
  if (state.empty()) notifications::Dismiss("store.catalogue");
  else notifications::Post("store.catalogue", level, std::move(title), std::move(description));
}

}  // namespace

// The surface value no longer selects a sentence (the credit line was removed, 2026-10-23); the
// note only says that the store was handed a record now.
void NoteStoreSurface(StoreSurface /*surface*/) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_surface_seen = true;
  g_last_surface = Clock::now();
  NotifyLocked();
}

void PublishCatalogueFetchState(CatalogueFetchState state, int pages_fetched,
                                std::size_t offers, std::string reason) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_state = state;
  g_pages = pages_fetched;
  g_offers = offers;
  g_incomplete = state == CatalogueFetchState::kReady && !reason.empty();
  if (state != CatalogueFetchState::kFailed) {
    g_reason.clear();
  } else {
    g_reason = std::move(reason);
  }
  NotifyLocked();
}

void SetCatalogueFetchEnabled(bool enabled) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_fetch_enabled = enabled;
  NotifyLocked();
}

std::string CatalogueStatusText() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_surface_seen) {
    return {};
  }
  const Clock::time_point now = Clock::now();
  const std::chrono::milliseconds since_surface =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_surface);
  if (since_surface > g_surface_window) {
    // The store is not the surface any more (left, or the boot never came back): the strip must
    // not follow the player out, the same rule the search strip learned (2026-10-22).
    return {};
  }
  if (!g_fetch_enabled) {
    return kCatalogueDisabledText;
  }
  switch (g_state) {
    case CatalogueFetchState::kFailed:
      return OfflineText(g_reason);
    case CatalogueFetchState::kReady:
      if (g_offers == 0) {
        return kCatalogueEmptyText;
      }
      break;  // a full list says nothing (the credit line was removed, 2026-10-23)
    case CatalogueFetchState::kFetching:
      if (g_offers == 0) {
        return LoadingText(g_pages);
      }
      break;  // offers are already on screen; a loading line over them would be noise
    case CatalogueFetchState::kIdle:
      break;
  }
  return {};
}

void ResetCatalogueStatus() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_last_surface = {};
  g_surface_seen = false;
  g_state = CatalogueFetchState::kIdle;
  g_pages = 0;
  g_offers = 0;
  g_reason.clear();
  g_fetch_enabled = true;
  g_incomplete = false;
  g_surface_window = kCatalogueSurfaceWindow;
  g_notice.clear();
  ui::notifications::Dismiss("store.catalogue");
}

void SetCatalogueStatusTiming(std::chrono::milliseconds surface_window) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  g_surface_window = surface_window;
}

}  // namespace rb_blitz::store
