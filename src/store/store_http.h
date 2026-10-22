// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P0's host transport for the RhythmVerse store (docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md
// §2.11, §3.1 step 3, R7): a small WinHTTP wrapper for the game executable.
//
// Two properties are the reason this is its own file rather than a copy of the installer's downloader:
//
//   * **Redirects are never followed by the transport.** The installer deliberately follows GitHub's
//     release redirects (`WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS`); the store's policy is the opposite
//     (§3.1 step 3) - a 3xx comes back as an ordinary response whose `Location` the caller validates
//     before following it itself. That is why `HttpResponse::status` is returned for 3xx bodies at all.
//   * **Nothing here touches the game.** Callers run it on host worker threads; no cvar registry, no
//     guest memory, no shared state (the project's rule for anything network-facing).
//
// The transport does not validate payloads, lengths or hosts - `rhythmverse_client.h` owns those
// decisions, so the one policy is testable without a network.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb_blitz::store {

struct HttpOptions {
  // Politeness (§P7): a real product string, not a browser's.
  const wchar_t* user_agent = L"rb-blitz-store/0.1 (+https://rhythmverse.co/)";
  int resolve_timeout_ms = 10000;
  int connect_timeout_ms = 10000;
  int send_timeout_ms = 15000;
  int receive_timeout_ms = 30000;
  // Catalogue pages are small; this bound stops a captive portal or an error page from becoming a
  // memory problem. A package download uses its own transport in P5 (`kMaxDownloadBytes`).
  std::size_t max_body_bytes = 4 * 1024 * 1024;
};

struct HttpHeader {
  std::string name;   // lowercased for responses
  std::string value;  // trimmed
};

struct HttpResponse {
  bool ok = false;  // the exchange completed and a status line was read
  int status = 0;
  std::string body;
  std::vector<HttpHeader> headers;
  std::string error;  // transport failure, empty when `ok`

  // Case-insensitive lookup ("content-length"); nullptr when absent.
  const HttpHeader* Find(std::string_view name) const;
};

// `application/x-www-form-urlencoded` POST. Never follows a redirect.
HttpResponse PostForm(std::string_view url, std::string_view body,
                      const std::vector<std::pair<std::string, std::string>>& headers,
                      const HttpOptions& options = {});

// GET with an empty body. Never follows a redirect.
HttpResponse Get(std::string_view url,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 const HttpOptions& options = {});

// The `Retry-After` header in seconds, when the server sent the delta-seconds form (the only form
// this API uses; an HTTP-date is not parsed, and an unparsable value answers nothing). Plan §P7's
// backoff turns this into the wait before the next request.
std::optional<int> RetryAfterSeconds(const HttpResponse& response);

}  // namespace rb_blitz::store
