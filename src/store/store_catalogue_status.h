// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P7 of the RhythmVerse store plan (docs/plans/finished/2026-10-13-rhythmverse-store-plan.md
// §5/P7): the words the store draws about itself - the **credit line**, the **empty state** and the
// **offline text** - and the one rule that decides which of them a player sees.
//
// Why this is its own module rather than a few strings in the catalogue worker:
//
//   * the trio has to be decided from two thread's worth of state (the worker's fetch state, the
//     guest hook's "which store surface is on screen") and drawn from a third (the caption
//     overlay), so the decision belongs in one place with one lock;
//   * it is SDK-free, so tests/store_catalogue_status_tests.cpp pins every sentence and every
//     precedence without a boot - the sentences are part of what the plan's exit is written
//     against, not an implementation detail.
//
// The credit line is P7's "refine the required RhythmVerse heading/credit": the heading itself is
// `kStoreHeadingText` = "RHYTHMVERSE", because "CUSTOMS FROM RHYTHMVERSE" clips to "CUSTOMS FROM"
// in the retail header (measured 2026-10-21, `out/_swap/p3-boot/`). The full attribution fits in a
// caption, so this is where the plan's fallback sentence ("Customs from RhythmVerse") is drawn:
// when the store opens, and again whenever nothing more urgent has to be said.
//
// The empty and offline sentences are P7's "empty-state and offline text". Retail's own
// `store_info_unavailable_screen` stays the route for an *enum error* and is untouched; it is not
// fabricated for a page that is merely empty, because an empty page is a normal answer the DTA
// already handles (it refreshes the menu). The strip is what turns that silent return into a
// sentence.
//
// The surface note is what keeps all of it inside the store: a record served in the ark-review
// screen, or a store the player left a minute ago, draws nothing.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace rb_blitz::store {

// Which store surface was just handed a record, reported from the file-cache hook (guest thread).
// The front menu means the store was opened; a menu page means a list was asked for.
enum class StoreSurface { kFrontMenu, kMenuPage };

// The catalogue worker's fetch state, mirroring `store_catalogue.cpp`'s internal enum so this
// module stays SDK-free.
enum class CatalogueFetchState { kIdle, kFetching, kReady, kFailed };

// Plan §1.8's attribution, in the spelling the site uses ([RhythmVerse](https://rhythmverse.co/)).
inline constexpr char kStoreCreditText[] = "Customs from RhythmVerse";

// The empty state: the catalogue answered, and nothing survived the filter. The sentence names
// where the filter lives, because that is the only thing the player can change.
inline constexpr char kCatalogueEmptyText[] =
    "RhythmVerse has no offers for this filter. Change it in Help & Options, Downloadable Content.";

// The diagnostics state (`--store_catalogue_off`): the store is honest about why it is empty.
inline constexpr char kCatalogueDisabledText[] =
    "Fetching the RhythmVerse catalogue is turned off.";

// The prefix of the loading sentence; the page count is appended while a multi-page fetch runs.
inline constexpr char kCatalogueLoadingText[] = "Loading the RhythmVerse catalogue";

// How long after a surface was served the strip may speak at all, and how long the credit line
// stays after the store opens. Both are held by the module, not by the caller.
inline constexpr auto kCatalogueSurfaceWindow = std::chrono::milliseconds(20000);
inline constexpr auto kCatalogueCreditHold = std::chrono::milliseconds(12000);

// The file-cache hook's note: a record for this surface was served now.
void NoteStoreSurface(StoreSurface surface);

// The worker's own report, called whenever its state or its counts move.
void PublishCatalogueFetchState(CatalogueFetchState state, int pages_fetched,
                                std::size_t offers, std::string reason = {});

// Whether the catalogue is allowed to fetch at all (the `store_catalogue_off` diagnostic). Set once
// per boot by the catalogue's installation.
void SetCatalogueFetchEnabled(bool enabled);

// The sentence the caption strip draws, or empty when the store is not the surface (or there is
// nothing to say). Precedence: failure > empty > loading > credit.
std::string CatalogueStatusText();

// Test seams: the suite resets the state and shrinks the two windows so expiry is provable.
void ResetCatalogueStatus();
void SetCatalogueStatusTiming(std::chrono::milliseconds surface_window,
                              std::chrono::milliseconds credit_hold);

}  // namespace rb_blitz::store
