// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "store/store_serve.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "store/store_metadata.h"

namespace rb_blitz::store {
namespace {

namespace fs = std::filesystem;

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
    const std::string text = BuildFrontMenuText(RhythmVerseFrontMenu(), why);
    return text;
  }
  if (why) *why = "no built-in record for this path";
  return {};
}

ServedRecord RecordForRequest(std::string_view path, std::string_view serve_dir) {
  ServedRecord record;
  std::string why;
  record.text = ReadServedRecord(serve_dir, path, &why);
  if (!record.text.empty()) {
    record.source = "file '" + std::string(serve_dir) + "/" + std::string(path) + "'";
    return record;
  }
  std::string builtin_why;
  record.text = BuiltinRecord(path, &builtin_why);
  if (!record.text.empty()) {
    record.source = "the built-in front menu";
    return record;
  }
  record.source = "nothing (" + why + "; " + builtin_why + ")";
  return record;
}

}  // namespace rb_blitz::store
