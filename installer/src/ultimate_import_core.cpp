// SPDX-License-Identifier: GPL-2.0-only
#include "ultimate_import_core.h"
#include "zip.h"
#include "util/sha256.h"

#if !defined(RB_BLITZ_IOS)
#include <curl/curl.h>
#endif
#include <algorithm>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace rb_blitz::installer::portable {
namespace {
namespace fs = std::filesystem;
const std::vector<std::string> kRoles{"ultimate-archive-header", "ultimate-archive-data"};

bool Cancelled(const ImportCallbacks& callbacks, std::string* error) {
  if (callbacks.cancelled && callbacks.cancelled()) {
    SetError(error, "Ultimate installation canceled");
    return true;
  }
  return false;
}

bool Download(const fs::path& file, const PinsUltimate& pins, const ImportCallbacks& callbacks,
              std::string* error) {
#if defined(RB_BLITZ_IOS)
  SetError(error, "The iOS Ultimate download transport is unavailable");
  return false;
#else
  static std::once_flag initialized;
  static CURLcode init_result;
  std::call_once(initialized, [] { init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
  if (init_result != CURLE_OK || !pins.url.starts_with("https://") || pins.size == 0 || pins.sha256.size() != 64) {
    SetError(error, "The Ultimate download configuration is invalid"); return false;
  }
  CURL* curl = curl_easy_init();
  if (!curl) { SetError(error, "Cannot initialize the Ultimate download"); return false; }
  struct Cleanup { CURL* value; ~Cleanup() { curl_easy_cleanup(value); } } cleanup{curl};
  struct State {
    std::ofstream stream;
    const ImportCallbacks& callbacks;
    std::uint64_t received = 0, limit;
  } state{std::ofstream(file, std::ios::binary | std::ios::trunc), callbacks, 0, pins.size};
  if (!state.stream) { SetError(error, "Cannot create the Ultimate download file"); return false; }
  curl_easy_setopt(curl, CURLOPT_URL, pins.url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "rb_blitz_macos_launcher/0.1");
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* bytes, size_t size, size_t count, void* opaque) -> size_t {
    auto& state = *static_cast<State*>(opaque);
    const size_t length = size * count;
    if (length > state.limit - state.received ||
        (state.callbacks.cancelled && state.callbacks.cancelled())) return 0;
    state.stream.write(bytes, static_cast<std::streamsize>(length));
    if (!state.stream) return 0;
    state.received += length;
    if (state.callbacks.progress) state.callbacks.progress(
        static_cast<int>(40 * state.received / state.limit), "Downloading Ultimate");
    return length;
  });
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
    const auto& state = *static_cast<State*>(opaque);
    return state.callbacks.cancelled && state.callbacks.cancelled() ? 1 : 0;
  });
  const CURLcode result = curl_easy_perform(curl);
  state.stream.close();
  if (Cancelled(callbacks, error)) return false;
  if (result != CURLE_OK || state.stream.fail()) {
    SetError(error, S("Ultimate download failed: ", curl_easy_strerror(result))); return false;
  }
  std::error_code code;
  if (state.received != pins.size || rb_blitz::util::HashFileHex(file, nullptr, code) != pins.sha256 || code) {
    SetError(error, "The Ultimate download does not match the pinned size and SHA-256"); return false;
  }
  return true;
#endif
}
}  // namespace

UltimateInstallConfig EmbeddedUltimateConfig() {
  UltimateInstallConfig result;
  result.pins = EmbeddedPins().ultimate;
  std::string error;
  if (!fingerprint::ParseGameFingerprint(EmbeddedUltimateFingerprints(), &result.fingerprint, &error))
    throw std::runtime_error(error);
  return result;
}

bool HasUltimate(const std::filesystem::path& game) {
  return FileSizeOrZero(game / "ultimate/gen/patch_xbox.hdr") > 0 &&
         FileSizeOrZero(game / "ultimate/gen/patch_xbox_0.ark") > 0;
}

bool InstallUltimate(const std::filesystem::path& source, const std::filesystem::path& game,
                     const UltimateInstallConfig& config, const ImportCallbacks& callbacks,
                     std::string* warning, std::string* error) {
  if (warning) warning->clear();
  if (!DirectoryExists(game)) { SetError(error, "Import the base game before installing Ultimate"); return false; }
  if (Cancelled(callbacks, error)) return false;
  // A sibling work tree keeps downloads/extraction away from the live game.
  const fs::path work = game.parent_path() / (game.filename().string() + ".ultimate-work");
  if (!source.empty() && PathIsWithin(work, source)) {
    SetError(error, "The Ultimate source overlaps its work directory"); return false;
  }
  if (!RemoveTree(work, error) || !EnsureDirectory(work, error)) return false;
  struct Cleanup { fs::path path; ~Cleanup() { RemoveTree(path, nullptr); } } cleanup{work};
  fs::path input = source;
  if (input.empty()) {
    input = work / "download.zip";
    if (!config.pins.url.starts_with("https://") || config.pins.size == 0 ||
        config.pins.sha256.size() != 64) {
      SetError(error, "The Ultimate download configuration is invalid"); return false;
    }
    if (!(config.download ? config.download(input, config.pins, callbacks, error)
                          : Download(input, config.pins, callbacks, error))) return false;
    if (Cancelled(callbacks, error)) return false;
    std::error_code code;
    if (fs::file_size(input, code) != config.pins.size || code ||
        rb_blitz::util::HashFileHex(input, nullptr, code) != config.pins.sha256 || code) {
      SetError(error, "The Ultimate download does not match the pinned size and SHA-256"); return false;
    }
  }
  fs::path tree = input;
  if (!DirectoryExists(input)) {
    ZipArchive archive;
    if (!archive.Open(input, error)) return false;
    tree = work / "extracted";
    if (!EnsureDirectory(tree, error)) return false;
    std::uint64_t extracted_bytes = 0;
    for (const auto& entry : archive.entries()) {
      if (Cancelled(callbacks, error)) return false;
      const fs::path name(entry.name);
      if (entry.is_directory || !entry.name.starts_with(config.pins.archive_prefix + "/") ||
          EqualsIgnoreCase(name.filename().string(), "default.xex")) continue;
      if (entry.uncompressed_size > 512 * 1024 * 1024 - extracted_bytes) {
        SetError(error, "The Ultimate archive exceeds the extraction size limit"); return false;
      }
      extracted_bytes += entry.uncompressed_size;
      if (callbacks.progress) callbacks.progress(50, "Extracting Ultimate");
      if (!archive.ExtractToFile(entry, tree / name, error)) return false;
    }
  }
  // Match the same Xbox subtree accepted by Windows, whether picked at its root
  // or in the official release wrapper. Copy every data file except default.xex.
  GameSourcePlan plan;
  if (!ProbeFolder(tree, config.fingerprint, kRoles, {}, &plan, error)) return false;
  std::vector<std::string> optional;
  std::error_code code;
  for (fs::recursive_directory_iterator it(plan.location, code), end; !code && it != end; it.increment(code)) {
    if (it->is_symlink(code)) { SetError(error, "Linked Ultimate source files are not supported"); return false; }
    if (!it->is_regular_file(code) || EqualsIgnoreCase(it->path().filename().string(), "default.xex")) continue;
    const std::string name = it->path().lexically_relative(plan.location).generic_string();
    if (SafeRelativePath(name, error).empty()) return false;
    const bool required = std::any_of(plan.files.begin(), plan.files.end(),
        [&](const auto& item) { return item.path == name; });
    if (!required) optional.push_back(name);
  }
  if (code) { SetError(error, code.message()); return false; }
  // Local revisions are permitted with the same warning policy as Windows.
  std::string mismatch;
  if (!VerifyTree(plan.location, config.fingerprint, kRoles, nullptr, &mismatch)) {
    if (source.empty()) { SetError(error, "The downloaded Ultimate files do not match the release fingerprints"); return false; }
    if (warning) *warning = "This local Ultimate revision differs from " + config.pins.version + ". Compatibility is unverified.";
  }
  auto selected = config.fingerprint;
  for (auto& file : selected.files) {
    if (std::find(kRoles.begin(), kRoles.end(), file.role) == kRoles.end()) continue;
    file.size = fs::file_size(plan.location / file.path, code);
    if (code || file.size == 0) { SetError(error, "The Ultimate archives are missing or empty"); return false; }
    file.sha256 = rb_blitz::util::HashFileHex(plan.location / file.path, nullptr, code);
    if (code) { SetError(error, code.message()); return false; }
  }
  if (!ProbeFolder(plan.location, selected, kRoles, optional, &plan, error)) return false;
  ImportCallbacks mapped = callbacks;
  mapped.progress = [&](int percent, std::string_view) {
    if (callbacks.progress) callbacks.progress(60 + 40 * percent / 100, "Installing Ultimate");
  };
  return Import(plan, game / "ultimate", selected, kRoles, optional, mapped, error);
}
}  // namespace rb_blitz::installer::portable
