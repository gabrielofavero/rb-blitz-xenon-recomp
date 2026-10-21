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

#include "store/store_metadata.h"

namespace rb_blitz::store {
namespace {

namespace fs = std::filesystem;

// The installed offer source (see store_serve.h). Written once at boot, read from the guest thread
// when a menu page is answered.
std::mutex g_source_mutex;
OfferSource g_offer_source;

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
  if (IsMenuRecordPath(path)) {
    return BuildMenuText(CatalogueOffers(), why);
  }
  if (why) *why = "no built-in record for this path";
  return {};
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
