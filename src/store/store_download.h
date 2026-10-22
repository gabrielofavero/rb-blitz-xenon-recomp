// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P5 of the RhythmVerse store plan (docs/plans/finished/2026-10-13-rhythmverse-store-plan.md
// §3.1, §5/P5): the download pipeline's policy core.
//
// Everything a download decides that can be decided without a network, a thread or a boot lives
// here, so `tests/store_download_tests.cpp` can walk the whole matrix hermetically:
//
//   * the **job token** the guest may send - a hash of the catalogue's opaque file id, never the
//     id itself and never a URL or a destination (the host holds both);
//   * the **bridge grammar** the DTA reads through `file_exists` (`state/<job>/<state>`,
//     `progress/<job>/at-least/<bucket>`, `error/<job>/<token>`) and writes through `write_file`
//     (`download/start|cancel|again/<job>`);
//   * the **error vocabulary** and which errors are worth a retry;
//   * the **transfer** itself: manual redirects, response-header refusal, the STFS prefix, the
//     size ceiling and the length/identity checks. The transport is a function pointer
//     (`TransferFn`) so the test drives it with fakes and the game binds it to
//     `store_transfer_winhttp.cpp`.
//
// The one rule this file leans on: a `rb3xbox` API tag proves nothing about the bytes. Only the
// streamed prefix and the completed file's own STFS header do (§3.1 step 4).

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "store/rhythmverse_client.h"

namespace rb_blitz::store {

// --- Jobs -----------------------------------------------------------------------------------------

// `j` + 16 lowercase hex of the file id's SHA-256: 17 bytes, printable, no untrusted content, and
// stable across catalogue pages and boots.
inline constexpr std::size_t kJobTokenLength = 17;

std::string JobTokenForFileId(std::string_view file_id);
bool IsValidJobToken(std::string_view token);

enum class DownloadError : std::uint8_t {
  kNone,
  kDisabled,        // the master switch is off (start refused, or a running job cancelled)
  kUnknownOffer,    // no registered offer carries this token (a stale or forged job)
  kBusy,            // a different install is already running
  kArchive,         // ZIP/RAR/7z/... by name, Content-Disposition or response bytes
  kPolicy,          // off-site/credentialed/not-direct redirect or a disallowed response
  kHttpStatus,      // a non-2xx answer that is not a redirect
  kNetwork,         // the transport failed (connect, TLS, timeout, reset)
  kTooLarge,        // beyond kMaxDownloadBytes (API, Content-Length or actual bytes)
  kLengthMismatch,  // API size and Content-Length disagree, or the body does not match either
  kBadPrefix,       // not an STFS package and not a known archive either
  kInvalidPackage,  // STFS magic, but the header cannot be read / names no title id
  kWrongTitle,      // a readable package for a title other than RB3
  kCancelled,       // the player cancelled
  kDiskFull,
  kReadOnly,
  kStorage,  // any other host file error
};

std::string_view DownloadErrorText(DownloadError error);
// A single lowercase word, bounded for `file_exists` predicates. `kNone` has no token.
std::string_view DownloadErrorToken(DownloadError error);
std::optional<DownloadError> DownloadErrorFromToken(std::string_view token);
// A network failure, a 5xx, a 408 or a 429 is worth retrying; nothing about the data is.
bool IsTransient(DownloadError error, int http_status = 0);

enum class JobState : std::uint8_t {
  kUnknown,
  kQueued,
  kDownloading,
  kInstalled,
  kFailed,
  kCancelled,
};

std::string_view JobStateToken(JobState state);
std::optional<JobState> JobStateFromToken(std::string_view token);

// --- Bridge grammar -------------------------------------------------------------------------------

inline constexpr int kProgressBuckets[] = {10, 25, 50, 75, 100};
inline constexpr std::size_t kProgressBucketCount = 5;

// The highest bucket `received / total` has passed, or 0 when `total` is unknown.
int ProgressBucket(std::uint64_t received, std::uint64_t total);

enum class JobQueryKind { kState, kProgressAtLeast, kError };

struct JobQuery {
  JobQueryKind kind = JobQueryKind::kState;
  std::string token;
  JobState state = JobState::kUnknown;
  int bucket = 0;
  DownloadError error = DownloadError::kNone;
};

// `rest` is the path after `rbbz-store/`. Only the exact spellings above parse; anything else is
// refused so `TakeExists` can forward it.
std::optional<JobQuery> ParseJobQuery(std::string_view rest);

enum class JobCommandKind { kStart, kCancel, kDownloadAgain };

struct JobCommand {
  JobCommandKind kind = JobCommandKind::kStart;
  std::string token;
};

std::optional<JobCommand> ParseJobCommand(std::string_view rest);

// --- Retry ----------------------------------------------------------------------------------------

struct RetryPolicy {
  int max_retries = 2;  // §5/P5: at most twice
  int base_delay_ms = 500;
  int max_delay_ms = 8000;
};

// `Retry-After` in its delta-seconds form (an HTTP-date form is not used by this host and is
// ignored). A value beyond the policy's ceiling is clamped.
std::optional<int> RetryAfterSeconds(std::string_view header_value);
int RetryDelayMs(const RetryPolicy& policy, int retry_index,
                 std::optional<int> retry_after_seconds);

// --- Transfer -------------------------------------------------------------------------------------

// What a response's head says. The transport fills every field it can; the policy decides.
struct TransferHead {
  int status = 0;
  std::optional<std::int64_t> content_length;
  std::string content_type;         // lowercased by the transport
  std::string content_disposition;  // as sent
  std::string content_encoding;     // as sent (empty when none)
  std::string location;             // as sent, for a 3xx
  std::string retry_after;          // as sent, for a 429/503
};

// The transport calls `on_head` once, before any body byte, and `on_data` for each chunk while
// the callback returns true. Returning false aborts the exchange; for a redirect the policy
// aborts the body deliberately and reads `TransferHead::location` instead.
struct TransferReceiver {
  std::function<bool(const TransferHead&)> on_head;
  std::function<bool(std::span<const std::uint8_t>)> on_data;
};

struct TransferResult {
  bool ok = false;  // the exchange itself completed (the status may still be 4xx/5xx)
  std::string error;
};

using TransferFn = std::function<TransferResult(std::string_view url,
                                                const TransferReceiver& receiver)>;

inline constexpr int kMaxRedirectHops = 3;

struct OfferRecord {
  std::string file_id;
  std::string url;  // already resolved/validated by ResolveDownloadUrl
  // `file.size` from the catalogue: 0/null is "unknown", never "a zero-byte package".
  std::optional<std::int64_t> api_size;
  // What the strip calls the job ("Downloading 'Artist - Title': 42%"). Host-only: the guest
  // never sees it, and it is never used as a path.
  std::string title;
};

struct TransferOutcome {
  DownloadError error = DownloadError::kNone;
  std::string detail;  // for the log and the failure line; never parsed
  int http_status = 0;
  std::optional<int> retry_after_seconds;
  std::uint64_t bytes = 0;
  std::uint32_t title_id = 0;
  std::uint32_t content_type = 0;
  std::string final_url;  // the URL that served the accepted bytes
};

// One attempt of the whole §3.1 transfer policy: hop-limited manual redirects (each validated by
// `ResolveDownloadUrl`), header refusal, streaming prefix classification, the size ceiling, the
// length cross-check and the completed file's STFS identity. The body is written to `part_path`
// (created or truncated); removing the part file on failure is the caller's job, because the
// caller also owns the retry that may reuse it.
//
// `cancel` and `still_enabled` are polled before every chunk; either one ends the attempt with
// `kCancelled`/`kDisabled` and leaves a partial part file for the caller to discard.
TransferOutcome TransferPackage(const OfferRecord& offer, const std::filesystem::path& part_path,
                                const TransferFn& transfer, const std::atomic<bool>* cancel,
                                const std::function<bool()>& still_enabled,
                                const std::function<void(std::uint64_t received,
                                                         std::uint64_t total)>& progress = {});

// The message a failure shows: the policy's own words for a code, plus a specific detail when
// the store has one (an archive's name, a disagreement's two sizes, ...).
std::string DescribeTransferFailure(const TransferOutcome& outcome);

}  // namespace rb_blitz::store
