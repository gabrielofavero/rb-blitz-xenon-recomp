// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// See release_api.h for why this reader exists and what it refuses.

#include "release_api.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "util.h"

namespace rb_blitz::installer {

namespace {

constexpr std::size_t kNpos = std::string_view::npos;

bool IsSpace(char character) {
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

// Skips a JSON string starting at `start` (which must be the opening quote) and
// returns the position just past the closing quote, or kNpos for an unterminated
// one. Backslash escapes are skipped, so a quote inside a value does not end it.
std::size_t SkipString(std::string_view json, std::size_t start) {
  std::size_t index = start + 1;
  while (index < json.size()) {
    const char character = json[index];
    if (character == '\\') {
      index += 2;
      continue;
    }
    if (character == '"') {
      return index + 1;
    }
    ++index;
  }
  return kNpos;
}

// The value of the string at `start`, escapes decoded. The caller has already
// checked that `start` is a quote and that the string terminates.
std::string ReadString(std::string_view json, std::size_t start) {
  std::string result;
  std::size_t index = start + 1;
  while (index < json.size() && json[index] != '"') {
    const char character = json[index];
    if (character != '\\') {
      result.push_back(character);
      ++index;
      continue;
    }
    ++index;
    if (index >= json.size()) {
      break;
    }
    const char escaped = json[index];
    switch (escaped) {
      case 'n': result.push_back('\n'); break;
      case 'r': result.push_back('\r'); break;
      case 't': result.push_back('\t'); break;
      case 'b': result.push_back('\b'); break;
      case 'f': result.push_back('\f'); break;
      case 'u': {
        // Only the ASCII range is decoded: a tag or an asset name with an escape
        // above 0x7f is written UTF-8 by GitHub, not escaped, so anything else
        // here is a document this reader does not understand rather than text a
        // user would see.
        unsigned code = 0;
        std::size_t digits = 0;
        for (; digits < 4 && index + 1 < json.size(); ++digits) {
          const char digit = json[index + 1];
          if (!std::isxdigit(static_cast<unsigned char>(digit))) {
            break;
          }
          code = code * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(digit))
                                                       ? digit - '0'
                                                       : (std::tolower(static_cast<unsigned char>(digit)) - 'a' + 10));
          ++index;
        }
        result.push_back(code >= 0x20 && code < 0x7f ? static_cast<char>(code) : '?');
        break;
      }
      default: result.push_back(escaped); break;
    }
    ++index;
  }
  return result;
}

// The position of the value of the top-level `key`, or kNpos. Every string in
// the document is examined: one whose closing quote is followed (past
// whitespace) by ':' is a key, and a key whose text is `key` is the field. A
// value string - followed by ',', '}' or ']' - is never mistaken for one.
std::size_t FindField(std::string_view json, std::string_view key, std::size_t from = 0) {
  std::size_t index = from;
  while (index < json.size()) {
    if (json[index] != '"') {
      ++index;
      continue;
    }
    const std::size_t after = SkipString(json, index);
    if (after == kNpos) {
      return kNpos;
    }
    std::size_t probe = after;
    while (probe < json.size() && IsSpace(json[probe])) {
      ++probe;
    }
    const bool is_key = probe < json.size() && json[probe] == ':';
    if (is_key && json.substr(index + 1, after - index - 2) == key) {
      std::size_t value = probe + 1;
      while (value < json.size() && IsSpace(json[value])) {
        ++value;
      }
      return value;
    }
    index = after;
  }
  return kNpos;
}

// The substring of the balanced `open`/`close` pair starting at `start`, and the
// position just past it. Strings are skipped, so a brace inside a value does not
// count. This is what isolates `assets` and its first element: an asset object is
// flat, and the array has no nested arrays, which is what lets a counter be
// enough for a document this well-known.
bool ReadBalanced(std::string_view json, char open, char close, std::size_t start,
                  std::string_view* out, std::size_t* after) {
  if (start >= json.size() || json[start] != open) {
    return false;
  }
  int depth = 0;
  std::size_t index = start;
  while (index < json.size()) {
    const char character = json[index];
    if (character == '"') {
      const std::size_t next = SkipString(json, index);
      if (next == kNpos) {
        return false;
      }
      index = next;
      continue;
    }
    if (character == open) {
      ++depth;
    } else if (character == close) {
      --depth;
      if (depth == 0) {
        *out = json.substr(start, index - start + 1);
        *after = index + 1;
        return true;
      }
    }
    ++index;
  }
  return false;
}

// A string field, or false when it is absent or is `null`.
bool FieldString(std::string_view json, std::string_view key, std::string* out) {
  const std::size_t value = FindField(json, key);
  if (value == kNpos || value >= json.size() || json[value] != '"') {
    return false;
  }
  if (SkipString(json, value) == kNpos) {
    return false;
  }
  *out = ReadString(json, value);
  return true;
}

// A number field, or false when it is absent, `null` or not a number.
bool FieldNumber(std::string_view json, std::string_view key, std::uint64_t* out) {
  const std::size_t value = FindField(json, key);
  if (value == kNpos || value >= json.size()) {
    return false;
  }
  std::size_t index = value;
  std::uint64_t result = 0;
  bool any = false;
  while (index < json.size() && std::isdigit(static_cast<unsigned char>(json[index]))) {
    const std::uint64_t digit = static_cast<std::uint64_t>(json[index] - '0');
    // A size is bytes on disk; refusing an absurd one here keeps the overflow off
    // the comparison with the downloaded length.
    if (result > (UINT64_MAX - digit) / 10) {
      return false;
    }
    result = result * 10 + digit;
    any = true;
    ++index;
  }
  if (!any) {
    return false;
  }
  *out = result;
  return true;
}

// A boolean field, or false when it is absent, `null`, or not `true`/`false`.
bool FieldBool(std::string_view json, std::string_view key, bool* out) {
  const std::size_t value = FindField(json, key);
  if (value == kNpos || value >= json.size()) {
    return false;
  }
  if (json.compare(value, 4, "true") == 0) {
    *out = true;
    return true;
  }
  if (json.compare(value, 5, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

// `sha256:<hex>` as GitHub writes it, or empty when the field is absent, null or
// not a sha256. Lower-cased, because the comparison is case-insensitive and the
// summary is read by eye.
std::string FieldDigest(std::string_view json, std::string_view key, bool* published, bool* malformed) {
  std::string text;
  *published = false;
  *malformed = false;
  if (!FieldString(json, key, &text)) {
    return {};
  }
  *published = true;
  constexpr std::string_view kPrefix = "sha256:";
  if (text.size() <= kPrefix.size() || text.compare(0, kPrefix.size(), kPrefix) != 0) {
    *malformed = true;
    return {};
  }
  std::string digest = text.substr(kPrefix.size());
  std::transform(digest.begin(), digest.end(), digest.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (!IsHexSha256(digest)) {
    *malformed = true;
    return {};
  }
  return digest;
}

}  // namespace

bool IsHexSha256(std::string_view text) {
  if (text.size() != 64) {
    return false;
  }
  return std::all_of(text.begin(), text.end(), [](unsigned char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}

bool IsLoopbackHttpUrl(std::string_view url) {
  constexpr std::string_view kHttp = "http://";
  if (url.size() <= kHttp.size() || url.compare(0, kHttp.size(), kHttp) != 0) {
    return false;
  }
  std::string_view authority = url.substr(kHttp.size());
  const std::size_t end = authority.find_first_of("/?#");
  if (end != kNpos) {
    authority = authority.substr(0, end);
  }
  // user@host and host:port are both written inside the authority; neither
  // changes which machine is named.
  const std::size_t at = authority.rfind('@');
  if (at != kNpos) {
    authority = authority.substr(at + 1);
  }
  std::string_view host = authority;
  if (!host.empty() && host.front() == '[') {
    const std::size_t close = host.find(']');
    if (close == kNpos) {
      return false;
    }
    host = host.substr(1, close - 1);
  } else {
    const std::size_t colon = host.find(':');
    if (colon != kNpos) {
      host = host.substr(0, colon);
    }
  }
  return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

std::string LatestReleaseApiUrl(std::string_view homepage_url) {
  constexpr std::string_view kPrefixes[] = {"https://github.com/", "http://github.com/",
                                            "https://www.github.com/"};
  std::string_view tail;
  bool matched = false;
  for (const std::string_view prefix : kPrefixes) {
    if (homepage_url.size() > prefix.size() && homepage_url.compare(0, prefix.size(), prefix) == 0) {
      tail = homepage_url.substr(prefix.size());
      matched = true;
      break;
    }
  }
  if (!matched) {
    return {};
  }
  if (tail.size() > 4 && tail.compare(tail.size() - 4, 4, ".git") == 0) {
    tail = tail.substr(0, tail.size() - 4);
  }
  // owner/repo only: anything after the second slash is GitHub's own business
  // (a tree link, a page), and the API wants the two names.
  const std::size_t slash = tail.find('/');
  if (slash == std::string_view::npos || slash == 0) {
    return {};
  }
  std::string owner(tail.substr(0, slash));
  std::string_view rest = tail.substr(slash + 1);
  const std::size_t second = rest.find('/');
  std::string repo(second == std::string_view::npos ? rest : rest.substr(0, second));
  if (repo.empty()) {
    return {};
  }
  return S("https://api.github.com/repos/", owner, "/", repo, "/releases/latest");
}

// One release object, read by key. The two entry points below are its only
// callers, so a field of the release resource is read in exactly one place.
static bool ReadReleaseObject(std::string_view json, LatestRelease* out, std::string* error) {
  *out = LatestRelease{};
  if (json.empty()) {
    SetError(error, "the release API answered with nothing");
    return false;
  }

  if (!FieldString(json, "tag_name", &out->tag_name)) {
    // A 404 body ("message": "Not Found") lands here, so the message says what
    // that means rather than "tag_name is missing".
    SetError(error,
             "the release API answer has no tag_name: the repository has no published release, "
             "or the URL is not a release URL");
    return false;
  }

  // The channel marker. Absent means false; present and not a boolean is refused,
  // because reading a nightly as a release would have `latest` install a build
  // that was never released.
  if (FindField(json, "prerelease") != kNpos && !FieldBool(json, "prerelease", &out->prerelease)) {
    SetError(error, S("release ", out->tag_name,
                      " has a prerelease flag that is not true or false"));
    return false;
  }

  const std::size_t assets_key = FindField(json, "assets");
  if (assets_key == kNpos || assets_key >= json.size() || json[assets_key] != '[') {
    SetError(error, "the release API answer has no assets array");
    return false;
  }
  std::string_view assets;
  std::size_t after_assets = 0;
  if (!ReadBalanced(json, '[', ']', assets_key, &assets, &after_assets)) {
    SetError(error, "the release API answer's assets array is not terminated");
    return false;
  }

  // Every element, so a release with more than one file is reported as such
  // rather than silently reduced to the first.
  std::size_t index = 0;
  std::string_view first;
  int count = 0;
  while (index < assets.size()) {
    const std::size_t brace = assets.find('{', index);
    if (brace == kNpos) {
      break;
    }
    std::string_view object;
    std::size_t after_object = 0;
    if (!ReadBalanced(assets, '{', '}', brace, &object, &after_object)) {
      SetError(error, "an asset in the release API answer is not terminated");
      return false;
    }
    if (count == 0) {
      first = object;
    }
    ++count;
    index = after_object;
  }
  out->asset_count = count;
  if (count == 0) {
    SetError(error, S("release ", out->tag_name, " carries no files"));
    return false;
  }

  if (!FieldString(first, "name", &out->asset_name) || out->asset_name.empty()) {
    SetError(error, S("release ", out->tag_name, " names no file"));
    return false;
  }
  if (!FieldString(first, "browser_download_url", &out->asset_url) || out->asset_url.empty()) {
    SetError(error, S("file '", out->asset_name, "' has no download URL"));
    return false;
  }
  if (out->asset_url.compare(0, 8, "https://") != 0 && !IsLoopbackHttpUrl(out->asset_url)) {
    SetError(error, S("file '", out->asset_name, "' is not served over https"));
    return false;
  }
  if (!FieldNumber(first, "size", &out->asset_size) || out->asset_size == 0) {
    SetError(error, S("file '", out->asset_name, "' has no size"));
    return false;
  }

  bool malformed_digest = false;
  out->asset_sha256 = FieldDigest(first, "digest", &out->digest_published, &malformed_digest);
  if (malformed_digest) {
    SetError(error, S("file '", out->asset_name, "' has a digest that is not a sha256"));
    return false;
  }
  return true;
}

// --- the two channels -----------------------------------------------------

// The first character that is part of the document, or kNpos for a document that is
// all whitespace. A UTF-8 byte-order mark is not part of it: GitHub sends none, but a
// hand-written answer can carry one, and "the answer is one release, not a list" is a
// bad way to find out.
static std::size_t FirstMeaningful(std::string_view json) {
  std::size_t index = 0;
  if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
      static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF) {
    index = 3;
  }
  while (index < json.size() && IsSpace(json[index])) {
    ++index;
  }
  return index == json.size() ? kNpos : index;
}

bool ParseLatestReleaseJson(std::string_view json, LatestRelease* out, std::string* error) {
  *out = LatestRelease{};
  const std::size_t start = FirstMeaningful(json);
  if (start == kNpos) {
    SetError(error, "the release API answered with nothing");
    return false;
  }
  // A list where one release was expected is a mistake worth naming: the two
  // endpoints answer different shapes, and "no tag_name" would send the reader
  // looking for a missing field instead of the wrong URL.
  if (json[start] == '[') {
    SetError(error,
             "the answer is a list of releases, not one release: the newest-release URL answers "
             "a single release, and the nightly channel is the one that reads the list");
    return false;
  }
  return ReadReleaseObject(json, out, error);
}

bool ParseNewestNightlyJson(std::string_view json, LatestRelease* out, std::string* error) {
  *out = LatestRelease{};
  const std::size_t start = FirstMeaningful(json);
  if (start == kNpos) {
    SetError(error, "the release API answered with nothing");
    return false;
  }
  if (json[start] != '[') {
    SetError(error,
             "the answer is one release, not a list: the nightly channel reads the release list, "
             "and a single release is what the newest-release URL answers");
    return false;
  }
  std::string_view list;
  std::size_t after_list = 0;
  if (!ReadBalanced(json, '[', ']', start, &list, &after_list)) {
    SetError(error, "the release API answer's list is not terminated");
    return false;
  }

  std::size_t index = 0;
  int seen = 0;
  bool any_prerelease = false;
  std::string skipped;
  while (index < list.size()) {
    const std::size_t brace = list.find('{', index);
    if (brace == kNpos) {
      break;
    }
    std::string_view object;
    std::size_t after_object = 0;
    if (!ReadBalanced(list, '{', '}', brace, &object, &after_object)) {
      SetError(error, "a release in the release API answer is not terminated");
      return false;
    }
    index = after_object;
    ++seen;

    LatestRelease candidate;
    std::string candidate_error;
    if (!ReadReleaseObject(object, &candidate, &candidate_error)) {
      // A release this reader cannot make sense of is not a nightly build, and
      // saying so about the *newest* one is more useful than a list length.
      if (skipped.empty()) {
        skipped = candidate_error;
      }
      continue;
    }
    if (!candidate.prerelease) {
      continue;
    }
    any_prerelease = true;
    if (candidate.asset_count != 1) {
      if (skipped.empty()) {
        skipped = S("nightly build ", candidate.tag_name, " carries ", candidate.asset_count,
                    " files; this downloader installs a build with exactly one");
      }
      continue;
    }
    *out = candidate;
    return true;
  }

  if (seen == 0) {
    SetError(error, "the release API answer has no releases in it");
  } else if (!any_prerelease) {
    // The honest answer when a repository has only ever cut releases: there is no
    // build of main to install, and the user should be sent to the release one.
    SetError(error,
             "this repository has no nightly build: no published release is marked as a "
             "prerelease (use the latest build, which installs the newest release)");
  } else {
    SetError(error, S("the newest nightly build cannot be installed: ",
                      skipped.empty() ? std::string("the release API described none that is") : skipped));
  }
  return false;
}

bool ParseReleaseChannel(std::string_view text, ReleaseChannel* out) {
  if (text == "release" || text == "latest") {
    *out = ReleaseChannel::kRelease;
    return true;
  }
  if (text == "nightly") {
    *out = ReleaseChannel::kNightly;
    return true;
  }
  return false;
}

const char* ReleaseChannelName(ReleaseChannel channel) {
  return channel == ReleaseChannel::kNightly ? "nightly" : "latest";
}

std::string ReleaseChannelDescription(ReleaseChannel channel) {
  return channel == ReleaseChannel::kNightly
             ? std::string("the newest build of main, published as a prerelease")
             : std::string("the newest release");
}

std::string NewestNightlyApiUrl(std::string_view homepage_url) {
  std::string base = LatestReleaseApiUrl(homepage_url);
  if (base.empty()) {
    return {};
  }
  // `.../releases/latest` -> `.../releases?per_page=30`: the list endpoint, with
  // enough of a page that a broken nightly or two does not hide the newest usable
  // one. GitHub sorts it newest first, which is the order the reader relies on.
  constexpr std::string_view kLatest = "/latest";
  if (base.size() > kLatest.size() &&
      base.compare(base.size() - kLatest.size(), kLatest.size(), kLatest) == 0) {
    base.resize(base.size() - kLatest.size());
  }
  return S(base, "?per_page=30");
}

std::string ReleaseApiUrlFor(ReleaseChannel channel, std::string_view homepage_url) {
  return channel == ReleaseChannel::kNightly ? NewestNightlyApiUrl(homepage_url)
                                             : LatestReleaseApiUrl(homepage_url);
}

std::string NewestMainCommitApiUrl(std::string_view homepage_url) {
  std::string base = LatestReleaseApiUrl(homepage_url);
  if (base.empty()) {
    return {};
  }
  // `.../releases/latest` -> `.../commits/main`: the repository part of the URL is
  // the same one the two release endpoints are built from.
  constexpr std::string_view kSuffix = "/releases/latest";
  if (base.size() > kSuffix.size() &&
      base.compare(base.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
    base.resize(base.size() - kSuffix.size());
  }
  return S(base, "/commits/main");
}

bool ParseMainCommitSha(std::string_view json, std::string* out, std::string* error) {
  out->clear();
  std::string sha;
  if (!FieldString(json, "sha", &sha) || sha.empty()) {
    // A 404 body, or a resource that is not a commit: either way there is no tip to
    // compare against, and the caller reports that rather than inventing one.
    SetError(error, "the commit API answer has no sha: the repository has no default branch to "
                    "ask, or the URL is not a commit URL");
    return false;
  }
  *out = std::move(sha);
  return true;
}

std::string CommitMarkerFromName(std::string_view name) {
  const auto is_hex = [](char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
  };
  std::string best;
  std::size_t index = 0;
  while (index < name.size()) {
    if (!is_hex(name[index])) {
      ++index;
      continue;
    }
    const std::size_t start = index;
    while (index < name.size() && is_hex(name[index])) {
      ++index;
    }
    // Seven characters is git's own short form, and shorter runs are as likely to be
    // a date or a word as a commit.
    if (index - start >= 7) {
      best.assign(name.substr(start, index - start));
      for (char& character : best) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
      }
    }
  }
  return best;
}

}  // namespace rb_blitz::installer
