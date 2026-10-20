// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See store_download.h.

#include "store/store_download.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "fs/dlc_library.h"
#include "store/store_install.h"
#include "util/sha256.h"

namespace rb_blitz::store {
namespace {

std::string LowerAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

bool ContainsAscii(const std::string& text, const std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

std::optional<std::int64_t> ParsePositiveInt64(const std::string& text) {
  if (text.empty()) {
    return std::nullopt;
  }
  errno = 0;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0' || value <= 0) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(value);
}

// The file name a Content-Disposition carries, either the ASCII form or RFC 5987's
// `filename*=UTF-8''...`. Never used as a path - only as an archive check (§3.1 step 3).
std::string DispositionFileName(std::string_view value) {
  const std::string lowered = LowerAscii(std::string(value));
  std::size_t at = lowered.find("filename*=");
  bool extended = true;
  if (at == std::string::npos) {
    at = lowered.find("filename=");
    extended = false;
  }
  if (at == std::string::npos) {
    return {};
  }
  at += extended ? 10 : 9;
  std::size_t end = value.size();
  const std::size_t semicolon = value.find(';', at);
  if (semicolon != std::string_view::npos) {
    end = semicolon;
  }
  std::string name(value.substr(at, end - at));
  while (!name.empty() && (name.front() == ' ' || name.front() == '"')) {
    name.erase(name.begin());
  }
  while (!name.empty() && (name.back() == ' ' || name.back() == '"')) {
    name.pop_back();
  }
  if (extended) {
    const std::size_t quote = name.rfind('\'');
    if (quote != std::string::npos) {
      name = name.substr(quote + 1);
    }
  }
  return name;
}

bool IsArchiveContentType(const std::string& content_type) {
  static constexpr std::string_view kArchiveTypes[] = {
      "application/zip",       "application/x-zip",       "application/x-zip-compressed",
      "application/x-rar",     "application/vnd.rar",     "application/x-7z-compressed",
      "application/x-tar",     "application/gzip",        "application/x-gzip",
      "application/x-bzip2",   "application/x-xz",
  };
  for (const std::string_view type : kArchiveTypes) {
    if (content_type.starts_with(type)) {
      return true;
    }
  }
  return false;
}

// The response-level refusals §3.1 step 3 asks for: never consume a body that is an archive,
// an error/login page or a compressed representation whose length would no longer describe the
// package. Returns kNone when the head may proceed.
DownloadError ValidateHead(const TransferHead& head, std::string* detail) {
  if (!head.content_encoding.empty() && LowerAscii(head.content_encoding) != "identity") {
    *detail = "the server sent a compressed response";
    return DownloadError::kPolicy;
  }
  const std::string content_type = LowerAscii(head.content_type);
  if (IsArchiveContentType(content_type)) {
    *detail = content_type;
    return DownloadError::kArchive;
  }
  if (content_type.starts_with("text/") || ContainsAscii(content_type, "html") ||
      ContainsAscii(content_type, "json")) {
    *detail = "the server sent a web page";
    return DownloadError::kPolicy;
  }
  const std::string disposition_name = DispositionFileName(head.content_disposition);
  if (!disposition_name.empty() && IsArchiveFileName(disposition_name)) {
    *detail = disposition_name;
    return DownloadError::kArchive;
  }
  if (head.content_length && *head.content_length > 0 &&
      static_cast<std::uint64_t>(*head.content_length) > kMaxDownloadBytes) {
    *detail = std::to_string(*head.content_length) + " bytes";
    return DownloadError::kTooLarge;
  }
  return DownloadError::kNone;
}

bool IsRedirectStatus(const int status) {
  switch (status) {
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
      return true;
    default:
      return false;
  }
}

// Resolves one `Location` under the download policy. `ResolveDownloadUrl` already speaks
// root-relative `/download_file/...` and absolute same-site URLs; anything else is kPolicy.
bool ResolveRedirect(const std::string& location, std::string* next_url, std::string* detail) {
  const ResolvedUrl resolved = ResolveDownloadUrl(location);
  if (!resolved.ok) {
    *detail = std::string(UrlRejectText(resolved.reject));
    return false;
  }
  *next_url = resolved.url;
  return true;
}

}  // namespace

// --- Jobs and errors ------------------------------------------------------------------------------

std::string JobTokenForFileId(std::string_view file_id) {
  return "j" + util::HashHex(file_id).substr(0, kJobTokenLength - 1);
}

bool IsValidJobToken(std::string_view token) {
  if (token.size() != kJobTokenLength || token.front() != 'j') {
    return false;
  }
  for (std::size_t i = 1; i < token.size(); ++i) {
    const char c = token[i];
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    if (!digit && !lower) {
      return false;
    }
  }
  return true;
}

std::string_view DownloadErrorText(const DownloadError error) {
  switch (error) {
    case DownloadError::kNone: return {};
    case DownloadError::kDisabled: return "RhythmVerse is turned off";
    case DownloadError::kUnknownOffer: return "That song is no longer in the store list";
    case DownloadError::kBusy: return "Another download is already running";
    case DownloadError::kArchive: return "Archive downloads are not supported";
    case DownloadError::kPolicy: return "That link is not a direct RhythmVerse file";
    case DownloadError::kHttpStatus: return "The store server refused the download";
    case DownloadError::kNetwork: return "The download could not reach RhythmVerse";
    case DownloadError::kTooLarge: return "That package is larger than the store will download";
    case DownloadError::kLengthMismatch: return "The download was incomplete or the wrong size";
    case DownloadError::kBadPrefix: return "The downloaded file is not a Rock Band 3 package";
    case DownloadError::kInvalidPackage: return "The package cannot be read";
    case DownloadError::kWrongTitle: return "The package is not Rock Band 3 content";
    case DownloadError::kCancelled: return "Download cancelled";
    case DownloadError::kDiskFull: return "The DLC drive is full";
    case DownloadError::kReadOnly: return "The DLC folder is read-only";
    case DownloadError::kStorage: return "The package could not be saved";
  }
  return {};
}

std::string_view DownloadErrorToken(const DownloadError error) {
  switch (error) {
    case DownloadError::kNone: return {};
    case DownloadError::kDisabled: return "disabled";
    case DownloadError::kUnknownOffer: return "unknown";
    case DownloadError::kBusy: return "busy";
    case DownloadError::kArchive: return "archive";
    case DownloadError::kPolicy: return "policy";
    case DownloadError::kHttpStatus: return "http";
    case DownloadError::kNetwork: return "network";
    case DownloadError::kTooLarge: return "too-large";
    case DownloadError::kLengthMismatch: return "length";
    case DownloadError::kBadPrefix: return "prefix";
    case DownloadError::kInvalidPackage: return "invalid";
    case DownloadError::kWrongTitle: return "title";
    case DownloadError::kCancelled: return "cancelled";
    case DownloadError::kDiskFull: return "disk-full";
    case DownloadError::kReadOnly: return "read-only";
    case DownloadError::kStorage: return "storage";
  }
  return {};
}

std::optional<DownloadError> DownloadErrorFromToken(const std::string_view token) {
  for (int i = 1; i <= static_cast<int>(DownloadError::kStorage); ++i) {
    const auto error = static_cast<DownloadError>(i);
    if (DownloadErrorToken(error) == token) {
      return error;
    }
  }
  return std::nullopt;
}

bool IsTransient(const DownloadError error, const int http_status) {
  if (error == DownloadError::kNetwork) {
    return true;
  }
  if (error != DownloadError::kHttpStatus) {
    return false;
  }
  if (http_status == 408 || http_status == 429) {
    return true;
  }
  return http_status >= 500 && http_status <= 599;
}

std::string_view JobStateToken(const JobState state) {
  switch (state) {
    case JobState::kUnknown: return "unknown";
    case JobState::kQueued: return "queued";
    case JobState::kDownloading: return "downloading";
    case JobState::kInstalled: return "installed";
    case JobState::kFailed: return "failed";
    case JobState::kCancelled: return "cancelled";
  }
  return "unknown";
}

std::optional<JobState> JobStateFromToken(const std::string_view token) {
  for (const JobState state : {JobState::kQueued, JobState::kDownloading, JobState::kInstalled,
                               JobState::kFailed, JobState::kCancelled}) {
    if (JobStateToken(state) == token) {
      return state;
    }
  }
  return std::nullopt;
}

// --- Bridge grammar -------------------------------------------------------------------------------

int ProgressBucket(const std::uint64_t received, const std::uint64_t total) {
  if (total == 0) {
    return 0;
  }
  const std::uint64_t percent = std::min<std::uint64_t>(100, received * 100 / total);
  int bucket = 0;
  for (const int candidate : kProgressBuckets) {
    if (percent >= static_cast<std::uint64_t>(candidate)) {
      bucket = candidate;
    }
  }
  return bucket;
}

std::optional<JobQuery> ParseJobQuery(const std::string_view rest) {
  if (rest.size() > 96) {
    return std::nullopt;
  }
  constexpr std::string_view kStatePrefix = "state/";
  constexpr std::string_view kProgressPrefix = "progress/";
  constexpr std::string_view kErrorPrefix = "error/";

  if (rest.starts_with(kStatePrefix)) {
    const std::string_view tail = rest.substr(kStatePrefix.size());
    const std::size_t slash = tail.rfind('/');
    if (slash == std::string_view::npos) {
      return std::nullopt;
    }
    JobQuery query;
    query.kind = JobQueryKind::kState;
    query.token = std::string(tail.substr(0, slash));
    const std::optional<JobState> state = JobStateFromToken(tail.substr(slash + 1));
    if (!IsValidJobToken(query.token) || !state) {
      return std::nullopt;
    }
    query.state = *state;
    return query;
  }

  if (rest.starts_with(kProgressPrefix)) {
    const std::string_view tail = rest.substr(kProgressPrefix.size());
    const std::size_t slash = tail.find('/');
    if (slash == std::string_view::npos) {
      return std::nullopt;
    }
    const std::string_view token = tail.substr(0, slash);
    const std::string_view tail2 = tail.substr(slash + 1);
    constexpr std::string_view kAtLeast = "at-least/";
    if (!tail2.starts_with(kAtLeast)) {
      return std::nullopt;
    }
    const std::string_view bucket_text = tail2.substr(kAtLeast.size());
    JobQuery query;
    query.kind = JobQueryKind::kProgressAtLeast;
    query.token = std::string(token);
    if (!IsValidJobToken(query.token) || bucket_text.size() > 3) {
      return std::nullopt;
    }
    int bucket = 0;
    for (const char c : bucket_text) {
      if (c < '0' || c > '9') {
        return std::nullopt;
      }
      bucket = bucket * 10 + (c - '0');
    }
    if (std::find(std::begin(kProgressBuckets), std::end(kProgressBuckets), bucket) ==
        std::end(kProgressBuckets)) {
      return std::nullopt;
    }
    query.bucket = bucket;
    return query;
  }

  if (rest.starts_with(kErrorPrefix)) {
    const std::string_view tail = rest.substr(kErrorPrefix.size());
    const std::size_t slash = tail.find('/');
    if (slash == std::string_view::npos) {
      return std::nullopt;
    }
    JobQuery query;
    query.kind = JobQueryKind::kError;
    query.token = std::string(tail.substr(0, slash));
    const std::optional<DownloadError> error = DownloadErrorFromToken(tail.substr(slash + 1));
    if (!IsValidJobToken(query.token) || !error || *error == DownloadError::kNone) {
      return std::nullopt;
    }
    query.error = *error;
    return query;
  }

  return std::nullopt;
}

std::optional<JobCommand> ParseJobCommand(const std::string_view rest) {
  constexpr std::string_view kPrefix = "download/";
  if (!rest.starts_with(kPrefix)) {
    return std::nullopt;
  }
  const std::string_view tail = rest.substr(kPrefix.size());
  const std::size_t slash = tail.find('/');
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view verb = tail.substr(0, slash);
  JobCommand command;
  if (verb == "start") {
    command.kind = JobCommandKind::kStart;
  } else if (verb == "cancel") {
    command.kind = JobCommandKind::kCancel;
  } else if (verb == "again") {
    command.kind = JobCommandKind::kDownloadAgain;
  } else {
    return std::nullopt;
  }
  command.token = std::string(tail.substr(slash + 1));
  if (!IsValidJobToken(command.token)) {
    return std::nullopt;
  }
  return command;
}

// --- Retry ----------------------------------------------------------------------------------------

std::optional<int> RetryAfterSeconds(const std::string_view header_value) {
  std::string text(header_value);
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.erase(text.begin());
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  if (text.empty() || text.size() > 6) {
    return std::nullopt;
  }
  int seconds = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    seconds = seconds * 10 + (c - '0');
  }
  return seconds;
}

int RetryDelayMs(const RetryPolicy& policy, const int retry_index,
                 const std::optional<int> retry_after_seconds) {
  int delay = policy.base_delay_ms;
  for (int i = 0; i < retry_index && delay < policy.max_delay_ms; ++i) {
    delay *= 2;
  }
  if (retry_after_seconds) {
    delay = *retry_after_seconds * 1000;
  }
  return std::clamp(delay, 0, policy.max_delay_ms);
}

// --- Transfer -------------------------------------------------------------------------------------

std::string DescribeTransferFailure(const TransferOutcome& outcome) {
  std::string text(DownloadErrorText(outcome.error));
  switch (outcome.error) {
    case DownloadError::kHttpStatus:
      text += " (HTTP " + std::to_string(outcome.http_status) + ")";
      return text;
    case DownloadError::kDiskFull:
    case DownloadError::kReadOnly:
    case DownloadError::kStorage:
    case DownloadError::kNetwork:
    case DownloadError::kArchive:
      if (!outcome.detail.empty() && outcome.detail != text) {
        text += ": ";
        text += outcome.detail;
      }
      return text;
    default:
      break;
  }
  if (!outcome.detail.empty()) {
    text += ": ";
    text += outcome.detail;
  }
  return text;
}

TransferOutcome TransferPackage(const OfferRecord& offer, const std::filesystem::path& part_path,
                                const TransferFn& transfer, const std::atomic<bool>* cancel,
                                const std::function<bool()>& still_enabled,
                                const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
  TransferOutcome outcome;
  if (!transfer) {
    outcome.error = DownloadError::kNetwork;
    outcome.detail = "no transport is configured";
    return outcome;
  }
  if (offer.url.empty() || offer.file_id.empty()) {
    outcome.error = DownloadError::kPolicy;
    outcome.detail = "the offer has no validated download link";
    return outcome;
  }

  const auto switched_off = [&] {
    return cancel != nullptr && cancel->load();
  };
  const auto disabled = [&] { return still_enabled && !still_enabled(); };

  std::string current = offer.url;
  std::ofstream file;
  std::size_t hops = 0;
  std::uint64_t received = 0;
  bool prefix_checked = false;
  std::vector<std::uint8_t> prefix;

  for (;;) {
    if (switched_off()) {
      outcome.error = DownloadError::kCancelled;
      return outcome;
    }
    if (disabled()) {
      outcome.error = DownloadError::kDisabled;
      return outcome;
    }

    const auto report_progress = [&](const std::uint64_t total) {
      if (progress) {
        progress(received, total);
      }
    };

    TransferHead head;
    bool have_head = false;
    bool body_wanted = false;
    DownloadError head_error = DownloadError::kNone;
    std::string head_detail;
    std::optional<std::int64_t> content_length;
    bool write_failed = false;

    const TransferResult result = transfer(current, TransferReceiver{
        .on_head =
            [&](const TransferHead& response) {
              head = response;
              have_head = true;
              if (IsRedirectStatus(response.status)) {
                return false;  // the body is not ours to read
              }
              if (response.status != 200) {
                return false;
              }
              head_error = ValidateHead(response, &head_detail);
              if (head_error != DownloadError::kNone) {
                return false;
              }
              content_length = response.content_length;
              file.open(part_path, std::ios::binary | std::ios::trunc);
              if (!file) {
                head_error = DownloadError::kStorage;
                head_detail = DescribeFileError(ClassifyFileError(std::error_code(errno, std::generic_category())), {});
                return false;
              }
              body_wanted = true;
              return true;
            },
        .on_data =
            [&](const std::span<const std::uint8_t> bytes) {
              if (switched_off()) {
                return false;
              }
              if (disabled()) {
                return false;
              }
              if (!prefix_checked) {
                prefix.insert(prefix.end(), bytes.begin(), bytes.end());
                if (prefix.size() < 4) {
                  return true;
                }
                const PrefixKind kind = ClassifyPrefix(prefix);
                if (!IsStfsPrefix(kind)) {
                  head_error = IsArchivePrefix(kind) ? DownloadError::kArchive
                                                     : DownloadError::kBadPrefix;
                  return false;
                }
                prefix_checked = true;
                received += prefix.size();
                if (received > kMaxDownloadBytes) {
                  head_error = DownloadError::kTooLarge;
                  return false;
                }
                file.write(reinterpret_cast<const char*>(prefix.data()),
                           static_cast<std::streamsize>(prefix.size()));
                write_failed = !file.good();
                report_progress(static_cast<std::uint64_t>(content_length.value_or(0)));
                prefix.clear();
                return !write_failed;
              }
              received += bytes.size();
              if (received > kMaxDownloadBytes) {
                head_error = DownloadError::kTooLarge;
                return false;
              }
              file.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
              write_failed = !file.good();
              report_progress(static_cast<std::uint64_t>(content_length.value_or(0)));
              return !write_failed;
            },
    });

    if (file.is_open()) {
      file.close();
    }

    if (!result.ok) {
      outcome.error = switched_off() ? DownloadError::kCancelled
                                     : (disabled() ? DownloadError::kDisabled : DownloadError::kNetwork);
      outcome.detail = result.error;
      return outcome;
    }
    if (!have_head) {
      outcome.error = DownloadError::kNetwork;
      outcome.detail = "the server answered without a status";
      return outcome;
    }
    if (write_failed) {
      const std::error_code ec(errno, std::generic_category());
      switch (ClassifyFileError(ec)) {
        case FileError::kDiskFull:
          outcome.error = DownloadError::kDiskFull;
          break;
        case FileError::kReadOnly:
          outcome.error = DownloadError::kReadOnly;
          break;
        default:
          outcome.error = DownloadError::kStorage;
          break;
      }
      outcome.detail = ec.message();
      return outcome;
    }
    if (switched_off()) {
      outcome.error = DownloadError::kCancelled;
      return outcome;
    }
    if (disabled()) {
      outcome.error = DownloadError::kDisabled;
      return outcome;
    }
    if (head_error != DownloadError::kNone) {
      outcome.error = head_error;
      outcome.detail = head_detail;
      outcome.http_status = head.status;
      return outcome;
    }
    if (IsRedirectStatus(head.status)) {
      if (hops >= static_cast<std::size_t>(kMaxRedirectHops)) {
        outcome.error = DownloadError::kPolicy;
        outcome.detail = "too many redirects";
        return outcome;
      }
      std::string next;
      std::string detail;
      if (!ResolveRedirect(head.location, &next, &detail)) {
        outcome.error = DownloadError::kPolicy;
        outcome.detail = "redirected to " + detail;
        return outcome;
      }
      ++hops;
      current = std::move(next);
      continue;
    }
    if (head.status != 200) {
      outcome.error = DownloadError::kHttpStatus;
      outcome.http_status = head.status;
      outcome.retry_after_seconds = RetryAfterSeconds(head.retry_after);
      return outcome;
    }
    if (!body_wanted) {
      outcome.error = DownloadError::kPolicy;
      outcome.detail = "the response carried no package body";
      return outcome;
    }
    if (!prefix_checked || !prefix.empty()) {
      // A body shorter than the four magic bytes, or one that ended inside them.
      outcome.error = DownloadError::kBadPrefix;
      outcome.detail = "the response ended before the package's header";
      return outcome;
    }

    const LengthCheck length = CheckTransferLength(offer.api_size, content_length, received);
    if (!length.ok) {
      outcome.error = DownloadError::kLengthMismatch;
      outcome.detail = length.reason;
      return outcome;
    }

    fs::PackageIdentity identity;
    std::string identity_reason;
    const fs::PackageIdentityResult identity_result =
        fs::ReadPackageIdentity(part_path, &identity, &identity_reason);
    if (identity_result != fs::PackageIdentityResult::kRead) {
      outcome.error = DownloadError::kInvalidPackage;
      outcome.detail = identity_reason;
      return outcome;
    }
    if (identity.title_id != kRb3TitleId) {
      char title[9] = {};
      std::snprintf(title, sizeof(title), "%08X", identity.title_id);
      outcome.error = DownloadError::kWrongTitle;
      outcome.detail = std::string("title ") + title;
      return outcome;
    }

    outcome.error = DownloadError::kNone;
    outcome.bytes = received;
    outcome.title_id = identity.title_id;
    outcome.content_type = identity.content_type;
    outcome.final_url = current;
    return outcome;
  }
}

}  // namespace rb_blitz::store
