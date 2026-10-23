// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_serve.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "store/album_art.h"
#include "store/store_metadata.h"

namespace rb_blitz::store {
namespace {

namespace fs = std::filesystem;

// The installed offer source (see store_serve.h). Written once at boot, read from the guest thread
// when a menu page is answered.
std::mutex g_source_mutex;
OfferSource g_offer_source;
SearchPageSource g_search_page_source;
DownloadedPredicate g_downloaded_predicate;
ArtPrefetch g_art_prefetch;
OfferRegistrar g_offer_registrar;

// A record bigger than this is not one of ours: the retail front menu and a 25-offer page are a
// few kilobytes. The cap keeps a misnamed file from being copied into the guest heap whole.
constexpr std::size_t kMaxRecordBytes = 1 << 20;

// `path` is composed by the guest (a provider string plus the record's own template), so it is
// never trusted as a host path: only plain relative segments are allowed.
bool IsPlainRelativePath(std::string_view path) {
  if (path.empty() || path.front() == '/' || path.front() == '\\') {
    return false;
  }
  if (path.size() >= 2 && path[1] == ':') {
    return false;
  }
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = path.find_first_of("/\\", start);
    const std::string_view segment =
        path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (segment == "..") {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return true;
}

}  // namespace

bool IsStoreRequestPath(std::string_view path) {
  return path.size() > sizeof(kStoreRequestPrefix) - 1 &&
         path.compare(0, sizeof(kStoreRequestPrefix) - 1, kStoreRequestPrefix) == 0;
}

bool IsFrontMenuPath(std::string_view path) {
  return path == FrontMenuRecordPath(kStoreRegion, kStorePlatform, kStoreLocale);
}

void SetOfferSource(OfferSource source) {
  std::lock_guard<std::mutex> lock(g_source_mutex);
  g_offer_source = std::move(source);
}

std::vector<Offer> CatalogueOffers() {
  OfferSource source;
  {
    std::lock_guard<std::mutex> lock(g_source_mutex);
    source = g_offer_source;
  }
  if (!source) {
    return {};
  }
  return source();
}

void SetSearchPageSource(SearchPageSource source) {
  std::lock_guard<std::mutex> lock(g_source_mutex);
  g_search_page_source = std::move(source);
}

std::vector<Offer> SearchPageOffers() {
  SearchPageSource source;
  {
    std::lock_guard<std::mutex> lock(g_source_mutex);
    source = g_search_page_source;
  }
  if (!source) {
    return {};
  }
  return source();
}

void SetDownloadedPredicate(DownloadedPredicate predicate) {
  std::lock_guard<std::mutex> lock(g_source_mutex);
  g_downloaded_predicate = std::move(predicate);
}

bool IsDownloadedOffer(const Offer& offer) {
  DownloadedPredicate predicate;
  {
    std::lock_guard<std::mutex> lock(g_source_mutex);
    predicate = g_downloaded_predicate;
  }
  if (!predicate) {
    return false;
  }
  return predicate(offer);
}

std::vector<Offer> OffersForPage() {
  std::vector<Offer> offers = CatalogueOffers();
  for (Offer& offer : offers) {
    offer.downloaded = IsDownloadedOffer(offer);
  }
  return offers;
}

void SetArtPrefetch(ArtPrefetch prefetch) {
  std::lock_guard<std::mutex> lock(g_source_mutex);
  g_art_prefetch = std::move(prefetch);
}

void SetOfferRegistrar(OfferRegistrar registrar) {
  std::lock_guard<std::mutex> lock(g_source_mutex);
  g_offer_registrar = std::move(registrar);
}

void AttachDownloadJob(Offer* offer) {
  if (offer == nullptr || offer->file_id.empty() || offer->download_url.empty()) {
    offer->job.clear();
    return;
  }
  OfferRegistrar registrar;
  {
    std::lock_guard<std::mutex> lock(g_source_mutex);
    registrar = g_offer_registrar;
  }
  if (!registrar) {
    offer->job.clear();
    return;
  }
  // Called outside the source mutex: the registrar is the download service, which has a mutex of
  // its own, and two locks in a guest-thread call is exactly the nesting this file avoids.
  const std::string title = offer->artist.empty() ? offer->name : offer->artist + " - " + offer->name;
  offer->job = registrar(offer->file_id, offer->download_url, offer->size_bytes, title);
}

namespace {

// Started outside the source mutex: the prefetch is another module's worker, and holding this lock
// across it would couple the two (the art cache has its own anyway).
void StartArtPrefetch(const std::vector<Offer>& offers) {
  ArtPrefetch prefetch;
  {
    std::lock_guard<std::mutex> lock(g_source_mutex);
    prefetch = g_art_prefetch;
  }
  if (prefetch) {
    prefetch(offers);
  }
}

// Only advertise a cover the serving path can answer *now*. `load_art` is answered synchronously
// out of the art cache (store_fetch.cpp), and the record this build compiles links the material
// immediately after that call returns (store_payload.h, `LinkAlbumArt`) - so an offer whose cover
// has not been encoded yet would link a texture the panel never loaded. Clearing the key is exactly
// the shape of an offer with no art: the DTA's `{offer has_data preview_art}` test fails and retail's
// own placeholder stays. `art_url` is host-only (never written into the record) and is kept, so a
// later prefetch for this offer still resolves.
void KeepOnlyHeldArt(std::vector<Offer>* offers) {
  for (Offer& offer : *offers) {
    if (offer.preview_art.empty()) {
      continue;
    }
    if (!HasArtTexture(ArtKeyFromPath(offer.preview_art))) {
      offer.preview_art.clear();
    }
  }
}

// Everything a page's offers need before they become a record, for both sources. The Downloaded
// mark and the download job are resolved here - one place, so a page can never carry a row the
// worker was not told about (the guest half of the job is `Offer::job`, and a missing key walks a
// null when the details page reads it).
void PreparePageOffers(std::vector<Offer>* offers) {
  for (Offer& offer : *offers) {
    offer.downloaded = IsDownloadedOffer(offer);
    AttachDownloadJob(&offer);
  }
  StartArtPrefetch(*offers);
  KeepOnlyHeldArt(offers);
}

}  // namespace

std::string ReadServedRecord(std::string_view dir, std::string_view path, std::string* why) {
  if (dir.empty()) {
    if (why) *why = "no serve directory";
    return {};
  }
  if (!IsPlainRelativePath(path)) {
    if (why) *why = "the path is not a plain relative path";
    return {};
  }
  std::error_code ec;
  const fs::path file = fs::path(std::string(dir)) / fs::path(std::string(path));
  if (!fs::is_regular_file(file, ec) || ec) {
    if (why) *why = "no such file";
    return {};
  }
  const auto size = fs::file_size(file, ec);
  if (ec) {
    if (why) *why = "the file size could not be read";
    return {};
  }
  if (size > kMaxRecordBytes) {
    if (why) *why = "the file is larger than a store record";
    return {};
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    if (why) *why = "the file could not be opened";
    return {};
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!in.eof() && in.fail()) {
    if (why) *why = "the file could not be read";
    return {};
  }
  return text;
}

std::string BuiltinRecord(std::string_view path, std::string* why) {
  if (path == FrontMenuRecordPath(kStoreRegion, kStorePlatform, kStoreLocale)) {
    // Text, not the compiled writer: the insert's `.dta` branch hands the bytes to the engine's
    // *text* reader (`sub_82418878` -> its tokenizer `sub_82417BF8`, which compares tokens against
    // `#define`/`#merge`/`#ifdef`/`#autorun`). Measured 2026-10-20 evening: the same record as
    // compiled DTB parsed to one stray symbol node, the text form to the `submenus` map.
    return BuildFrontMenuText(RhythmVerseFrontMenu(), why);
  }
  // A menu row's own request, measured 2026-10-20 evening (`rb_blitz_088.log`): pressing the
  // front menu's first row asked for `dlc_store/europe/eng/recent/`. The offers page is what that
  // path answers (plan §5/P3: the same `/base + provider` builder, so the record is the menu
  // record `BuildMenuText` writes). Its offers come from the installed catalogue source; with no
  // source, or a catalogue that has not answered yet, the page is **empty** - a shape the retail
  // DTA already handles (`on_offers_populated` with zero offers refreshes the menu instead of
  // opening the browser), and an honest answer rather than a fabricated list.
  if (IsSearchMenuPath(path)) {
    // The search's answer as a page (P4). The rows are the accepted offers, mapped through the same
    // `OfferFromSong` the catalogue uses (in the installed source), so the browser, the details
    // screen, the Downloaded mark and the art path all behave exactly as they do for Recently
    // Added. The source also marks the answer presented, which is what makes "press Search again"
    // mean "type a new query" once this page has been listed.
    std::vector<Offer> offers = SearchPageOffers();
    PreparePageOffers(&offers);
    return BuildMenuText(offers, why);
  }
  if (IsMenuRecordPath(path)) {
    std::vector<Offer> offers = OffersForPage();
    // The covers for this page, in the background, and the per-offer state (`PreparePageOffers`):
    // the panel asks for one about a second after it draws the list
    // (`store_details_panel.refresh` -> `load_art`), and the catalogue's own page may have landed
    // long after the front menu was served.
    PreparePageOffers(&offers);
    return BuildMenuText(offers, why);
  }
  if (why) *why = "no built-in record for this path";
  return {};
}

bool IsSearchMenuPath(std::string_view path) {
  return path == MenuRecordPath(kStoreRegion, kStoreLocale, kSearchMenuId);
}

bool IsMenuRecordPath(std::string_view path) {
  // `dlc_store/<region>/<locale>/<name>/` - the region/platform prefix is the request's own base
  // and the trailing segment the provider the row named. Anything deeper belongs to an offer.
  if (!IsStoreRequestPath(path) || path.back() != '/') {
    return false;
  }
  const std::string_view rest = path.substr(sizeof(kStoreRequestPrefix) - 1);
  int slashes = 0;
  for (const char c : rest) {
    if (c == '/') ++slashes;
  }
  return slashes == 3 && rest.find("offers/") == std::string_view::npos &&
         rest.find("related/") == std::string_view::npos;
}

ServedRecord RecordForRequest(std::string_view path, std::string_view serve_dir) {
  ServedRecord record;
  std::string why;
  record.bytes = ReadServedRecord(serve_dir, path, &why);
  if (!record.bytes.empty()) {
    record.source = "file '" + std::string(serve_dir) + "/" + std::string(path) + "'";
    return record;
  }
  std::string builtin_why;
  record.bytes = BuiltinRecord(path, &builtin_why);
  if (!record.bytes.empty()) {
    record.source = "the built-in front menu";
    return record;
  }
  record.source = "nothing (" + why + "; " + builtin_why + ")";
  return record;
}

}  // namespace rb_blitz::store
