// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Unit tests for P5 of the RhythmVerse store plan: the download pipeline.
//
// Everything here is hermetic. The transfer is scripted (no socket is opened), the destination
// is a temporary folder, and the bridge is exercised through the same functions the guest hooks
// call (`AnswerDownloadQuery` / `HandleDownloadWrite`). The cases pin:
//
//   * the job-token and bridge grammars the DTA and the host have to agree on;
//   * the retry decision and the backoff;
//   * the staging/commit rules - nothing partial is enumerable, the layout is preserved and no
//     existing package is overwritten;
//   * the transfer policy: manual redirects, archive/HTML refusals, the size ceiling, the length
//     cross-check and the completed file's own STFS identity;
//   * the service: one install at a time, cancel, master Off, receipts and the post-install
//     refresh callback.

#include "check.h"

#include "fs/tree_fingerprint.h"
#include "fs/dlc_library.h"
#include "fs/dlc_layout.h"
#include "store/store_download.h"
#include "store/store_download_service.h"
#include "store/store_install.h"
#include "store/store_settings.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace rb_blitz::test;
namespace fs = std::filesystem;
namespace rbfs = rb_blitz::fs;
using namespace rb_blitz::store;

void CheckStrEq(const std::string& actual, const std::string& expected) {
  if (actual == expected) {
    ++g_checks;
    return;
  }
  Fail(__FILE__, __LINE__, "expected \"" + expected + "\", got \"" + actual + "\"");
}

#define CHECK_STR_EQ(actual, expected) CheckStrEq((actual), (expected))

struct Scratch {
  fs::path base;

  Scratch() {
    std::error_code ec;
    base = fs::temp_directory_path(ec) / "rb_blitz-store-download";
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
  }

  ~Scratch() {
    std::error_code ec;
    fs::remove_all(base, ec);
  }
};

std::string StfsBody(const uint32_t title_id = kRb3TitleId, const uint32_t content_type = 1,
                     const std::size_t extra = 0) {
  std::string body(rbfs::kStfsHeaderSize + extra, '\0');
  std::memcpy(body.data(), "CON ", 4);
  const auto store_be = [&body](const std::size_t offset, const uint32_t value) {
    body[offset] = static_cast<char>(value >> 24);
    body[offset + 1] = static_cast<char>(value >> 16);
    body[offset + 2] = static_cast<char>(value >> 8);
    body[offset + 3] = static_cast<char>(value);
  };
  store_be(rbfs::kStfsContentTypeOffset, content_type);
  store_be(rbfs::kStfsTitleIdOffset, title_id);
  return body;
}

void WriteFile(const fs::path& path, const std::string& bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ReadFile(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// --- A scripted transport -------------------------------------------------------------------------

struct ScriptedResponse {
  int status = 200;
  std::string body;
  std::optional<long long> length;  // nullopt = "send the body's size"
  bool omit_length = false;
  std::string content_type = "application/octet-stream";
  std::string disposition;
  std::string encoding;
  std::string location;
  std::string retry_after;
  bool transport_failure = false;
  std::size_t chunk = 8192;
  // Runs once, after the first body chunk, so a test can change settings or block the transfer.
  std::function<void()> after_first_chunk;
};

struct FakeServer {
  std::map<std::string, std::vector<ScriptedResponse>> responses;
  std::vector<std::string> requests;

  void Add(std::string url, ScriptedResponse response) {
    responses[url].push_back(std::move(response));
  }

  TransferFn Fn() {
    return [this](const std::string_view url, const TransferReceiver& receiver) -> TransferResult {
      const std::string key(url);
      requests.push_back(key);
      const auto found = responses.find(key);
      if (found == responses.end()) {
        return {false, "no scripted response for " + key};
      }
      const ScriptedResponse& response =
          found->second[std::min<std::size_t>(served_[key], found->second.size() - 1)];
      ++served_[key];
      if (response.transport_failure) {
        return {false, "scripted transport failure"};
      }
      TransferHead head;
      head.status = response.status;
      if (!response.omit_length) {
        head.content_length =
            response.length ? response.length
                            : std::optional<long long>(static_cast<long long>(response.body.size()));
      }
      head.content_type = response.content_type;
      head.content_disposition = response.disposition;
      head.content_encoding = response.encoding;
      head.location = response.location;
      head.retry_after = response.retry_after;
      if (receiver.on_head && !receiver.on_head(head)) {
        return {true, {}};
      }
      if (head.status != 200) {
        return {true, {}};
      }
      bool first = true;
      for (std::size_t offset = 0; offset < response.body.size(); offset += response.chunk) {
        const std::size_t size = std::min(response.chunk, response.body.size() - offset);
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(response.body.data() + offset);
        if (receiver.on_data &&
            !receiver.on_data(std::span<const std::uint8_t>(bytes, size))) {
          return {true, {}};
        }
        if (first) {
          first = false;
          if (response.after_first_chunk) {
            response.after_first_chunk();
          }
        }
      }
      return {true, {}};
    };
  }

 private:
  std::map<std::string, std::size_t> served_;
};

void PublishEnabled(const bool enabled) {
  StoreSettings settings;
  settings.enabled = enabled;
  PublishSettings(settings);
}

// --- Job tokens and bridge grammar ----------------------------------------------------------------

void TestJobTokens() {
  BeginCase("a job token is stable, bounded and carries no part of the file id");
  const std::string token = JobTokenForFileId("5b947da4929df7.93833456");
  CHECK_EQ(token.size(), kJobTokenLength);
  CHECK_TRUE(IsValidJobToken(token));
  CHECK_STR_EQ(token, JobTokenForFileId("5b947da4929df7.93833456"));
  CHECK_TRUE(JobTokenForFileId("other-id") != token);
  CHECK_TRUE(token.front() == 'j');
  CHECK_FALSE(IsValidJobToken(""));
  CHECK_FALSE(IsValidJobToken("5b947da4929df7.93833456"));
  CHECK_FALSE(IsValidJobToken("J0123456789ABCDEF"));
  CHECK_FALSE(IsValidJobToken("j0123456789abcde"));
}

void TestBridgeGrammar() {
  BeginCase("the bridge grammar accepts the documented paths and nothing else");
  const std::string token = JobTokenForFileId("id");
  const std::optional<JobQuery> installed = ParseJobQuery("state/" + token + "/installed");
  CHECK_TRUE(installed.has_value());
  CHECK_TRUE(installed && installed->kind == JobQueryKind::kState);
  CHECK_TRUE(installed && installed->state == JobState::kInstalled);

  const std::optional<JobQuery> progress =
      ParseJobQuery("progress/" + token + "/at-least/75");
  CHECK_TRUE(progress.has_value());
  CHECK_TRUE(progress && progress->kind == JobQueryKind::kProgressAtLeast);
  CHECK_EQ(progress ? progress->bucket : -1, 75);

  const std::optional<JobQuery> error = ParseJobQuery("error/" + token + "/archive");
  CHECK_TRUE(error.has_value());
  CHECK_TRUE(error && error->error == DownloadError::kArchive);

  CHECK_FALSE(ParseJobQuery("state/" + token + "/making-tea").has_value());
  CHECK_FALSE(ParseJobQuery("progress/" + token + "/at-least/33").has_value());
  CHECK_FALSE(ParseJobQuery("progress/" + token + "/at-least/10x").has_value());
  CHECK_FALSE(ParseJobQuery("state/not-a-token/installed").has_value());
  CHECK_FALSE(ParseJobQuery("error/" + token + "/nonsense").has_value());
  CHECK_FALSE(ParseJobQuery("enabled").has_value());

  const std::optional<JobCommand> start = ParseJobCommand("download/start/" + token);
  CHECK_TRUE(start.has_value());
  CHECK_TRUE(start && start->kind == JobCommandKind::kStart);
  const std::optional<JobCommand> cancel = ParseJobCommand("download/cancel/" + token);
  CHECK_TRUE(cancel.has_value());
  CHECK_TRUE(cancel && cancel->kind == JobCommandKind::kCancel);
  const std::optional<JobCommand> again = ParseJobCommand("download/again/" + token);
  CHECK_TRUE(again.has_value());
  CHECK_TRUE(again && again->kind == JobCommandKind::kDownloadAgain);
  CHECK_FALSE(ParseJobCommand("download/start/not-a-token").has_value());
  CHECK_FALSE(ParseJobCommand("download/resume/" + token).has_value());

  CHECK_EQ(ProgressBucket(0, 0), 0);
  CHECK_EQ(ProgressBucket(0, 100), 0);
  CHECK_EQ(ProgressBucket(9, 100), 0);
  CHECK_EQ(ProgressBucket(10, 100), 10);
  CHECK_EQ(ProgressBucket(74, 100), 50);
  CHECK_EQ(ProgressBucket(100, 100), 100);
  CHECK_EQ(ProgressBucket(200, 100), 100);
}

void TestRetryPolicy() {
  BeginCase("only network/5xx/408/429 failures are retried, with bounded backoff");
  CHECK_TRUE(IsTransient(DownloadError::kNetwork));
  CHECK_TRUE(IsTransient(DownloadError::kHttpStatus, 500));
  CHECK_TRUE(IsTransient(DownloadError::kHttpStatus, 503));
  CHECK_TRUE(IsTransient(DownloadError::kHttpStatus, 429));
  CHECK_TRUE(IsTransient(DownloadError::kHttpStatus, 408));
  CHECK_FALSE(IsTransient(DownloadError::kHttpStatus, 404));
  CHECK_FALSE(IsTransient(DownloadError::kArchive));
  CHECK_FALSE(IsTransient(DownloadError::kLengthMismatch));

  CHECK_EQ(RetryDelayMs(RetryPolicy{}, 0, std::nullopt), 500);
  CHECK_EQ(RetryDelayMs(RetryPolicy{}, 1, std::nullopt), 1000);
  CHECK_EQ(RetryDelayMs(RetryPolicy{}, 0, std::optional<int>(3)), 3000);
  CHECK_EQ(RetryDelayMs(RetryPolicy{}, 1, std::optional<int>(600)), 8000);
  CHECK_TRUE(RetryAfterSeconds("2").value_or(-1) == 2);
  CHECK_FALSE(RetryAfterSeconds("Wed, 21 Oct 2015 07:28:00 GMT").has_value());
  CHECK_FALSE(RetryAfterSeconds("").has_value());

  for (int i = 1; i <= static_cast<int>(DownloadError::kStorage); ++i) {
    const auto error = static_cast<DownloadError>(i);
    CHECK_TRUE(!DownloadErrorToken(error).empty());
    CHECK_TRUE(DownloadErrorFromToken(DownloadErrorToken(error)).value_or(DownloadError::kNone) ==
               error);
  }
}

// --- Install rules --------------------------------------------------------------------------------

void TestInstallPlanning() {
  BeginCase("an install is staged under the root and named after the song, made safe");
  const fs::path root = fs::path("D:/Songs");
  InstallLayout flat;
  flat.destination_root = root;
  std::string error;
  const std::optional<StagedInstall> flat_plan =
      PlanInstall(flat, "opaque-id", "The Artist - The Song", &error);
  CHECK_TRUE(flat_plan.has_value());
  CHECK_STR_EQ(flat_plan ? flat_plan->staging_dir.filename().string() : "",
               std::string(rbfs::kStoreStagingDirName));
  CHECK_TRUE(flat_plan && flat_plan->staging_dir.parent_path() == root);
  CHECK_TRUE(flat_plan && flat_plan->final_dir == root);
  CHECK_STR_EQ(flat_plan ? flat_plan->final_name : "", std::string("The Artist - The Song"));
  CHECK_TRUE(flat_plan && IsValidInstallName(flat_plan->final_name));
  // No extension: a dot would read as a format, and a package has none.
  CHECK_TRUE(flat_plan && flat_plan->final_name.find('.') == std::string::npos);
  // The part file keeps the id's hash, so staging never carries the song's name.
  CHECK_TRUE(flat_plan && flat_plan->part_path.filename().string().starts_with("rv_"));
  CHECK_TRUE(flat_plan && flat_plan->part_path.filename().string().ends_with(".part"));

  InstallLayout structured;
  structured.destination_root = root;
  structured.structured = true;
  const std::optional<StagedInstall> structured_plan =
      PlanInstall(structured, "opaque-id", "The Artist - The Song", &error);
  CHECK_TRUE(structured_plan.has_value());
  CHECK_TRUE(structured_plan &&
             structured_plan->final_dir == root / "45410914" / "00000002");

  InstallLayout empty;
  CHECK_FALSE(PlanInstall(empty, "opaque-id", "The Artist - The Song", &error).has_value());
  CHECK_FALSE(PlanInstall(flat, "", "The Artist - The Song", &error).has_value());

  // An offer with no usable name still installs, under its id's hash.
  const std::optional<StagedInstall> unnamed = PlanInstall(flat, "opaque-id", "", &error);
  CHECK_TRUE(unnamed.has_value());
  CHECK_STR_EQ(unnamed ? unnamed->final_name : "", InstallStem("opaque-id"));
  CHECK_TRUE(unnamed && IsValidInstallName(unnamed->final_name));

  // The sanitizer: dots, the punctuation Windows refuses, the ends, and device names.
  CHECK_STR_EQ(InstallName("Mr. Brightside"), std::string("Mr Brightside"));
  CHECK_STR_EQ(InstallName("AC/DC: Back in Black?"), std::string("ACDC Back in Black"));
  CHECK_STR_EQ(InstallName("  A  -  B  "), std::string("A - B"));
  CHECK_STR_EQ(InstallName("Tab\tand\nnewline"), std::string("Tabandnewline"));
  CHECK_STR_EQ(InstallName("CON"), std::string("CON_"));
  CHECK_STR_EQ(InstallName("nul"), std::string("nul_"));
  CHECK_STR_EQ(InstallName("LPT2"), std::string("LPT2_"));
  CHECK_STR_EQ(InstallName("CONSOLE"), std::string("CONSOLE"));
  CHECK_STR_EQ(InstallName("NUL ~ x"), std::string("NUL ~ x"));
  CHECK_STR_EQ(InstallName("..."), std::string(""));

  // The guest's field is 42 bytes: the name is cut at a code point, never inside one, and never
  // ends in the space the cut may have exposed.
  CHECK_TRUE(InstallName(std::string(80, 'a')).size() == kMaxInstallFileNameBytes);
  CHECK_TRUE(InstallName(std::string(41, 'a') + " b").size() == 41);
  const std::string emoji = InstallName(std::string(40, 'a') + "\xF0\x9F\x98\x80" + "bbbb");
  CHECK_TRUE(emoji.size() == 40);  // the 4-byte code point would have been cut in half
  CHECK_TRUE(IsValidInstallName(emoji));

  // The validator: what this module can produce, and nothing a host would refuse.
  CHECK_TRUE(IsValidInstallName("The Artist - The Song"));
  CHECK_FALSE(IsValidInstallName(""));
  CHECK_FALSE(IsValidInstallName("Artist - Song.con"));
  CHECK_FALSE(IsValidInstallName("Artist/Song"));
  CHECK_FALSE(IsValidInstallName("Artist: Song"));
  CHECK_FALSE(IsValidInstallName("Trailing "));
  CHECK_FALSE(IsValidInstallName("CON"));
  CHECK_FALSE(IsValidInstallName(std::string(kMaxInstallFileNameBytes + 1, 'a')));
}

void TestStagingAndCommit() {
  BeginCase("a validated file is renamed in; an existing package is never replaced");
  Scratch scratch;
  InstallLayout layout;
  layout.destination_root = scratch.base / "library";
  std::string error;
  const std::optional<StagedInstall> plan = PlanInstall(layout, "same-offer", "Same Offer", &error);
  CHECK_TRUE(plan.has_value());
  CHECK_TRUE(plan && EnsureStagingDirectory(*plan, &error));
  WriteFile(plan->part_path, "CON first bytes");

  fs::path first;
  CHECK_TRUE(plan && CommitStagedInstall(*plan, &first, &error));
  CHECK_TRUE(fs::exists(first));
  CHECK_STR_EQ(ReadFile(first), "CON first bytes");
  CHECK_TRUE(first.parent_path() == layout.destination_root);
  CHECK_STR_EQ(first.filename().string(), std::string("Same Offer"));
  CHECK_FALSE(fs::exists(plan->part_path));

  CHECK_TRUE(plan && EnsureStagingDirectory(*plan, &error));
  WriteFile(plan->part_path, "CON second bytes");
  fs::path second;
  CHECK_TRUE(plan && CommitStagedInstall(*plan, &second, &error));
  CHECK_TRUE(second != first);
  CHECK_TRUE(fs::exists(first));
  CHECK_STR_EQ(ReadFile(first), "CON first bytes");
  CHECK_STR_EQ(ReadFile(second), "CON second bytes");
  CHECK_TRUE(IsValidInstallName(second.filename().string()));
  CHECK_STR_EQ(second.filename().string(), std::string("Same Offer~1"));
  // The collision suffix still fits the guest's 42-byte field.
  CHECK_TRUE(second.filename().string().size() <= rbfs::kMaxContentFileName);

  // A name already at the ceiling gives bytes up to the suffix rather than overflow the field.
  const std::string long_name(kMaxInstallFileNameBytes, 'a');
  const std::optional<StagedInstall> long_plan =
      PlanInstall(layout, "long-offer", long_name, &error);
  CHECK_TRUE(long_plan.has_value());
  if (long_plan && EnsureStagingDirectory(*long_plan, &error)) {
    WriteFile(long_plan->part_path, "CON long one");
    fs::path long_first;
    CHECK_TRUE(CommitStagedInstall(*long_plan, &long_first, &error));
    WriteFile(long_plan->part_path, "CON long two");
    fs::path long_second;
    CHECK_TRUE(CommitStagedInstall(*long_plan, &long_second, &error));
    CHECK_TRUE(long_second.filename().string().size() == kMaxInstallFileNameBytes);
    CHECK_TRUE(long_second.filename().string().ends_with("~1"));
    CHECK_TRUE(IsValidInstallName(long_second.filename().string()));
  }

  // A structured layout creates its title/content-type levels.
  InstallLayout structured;
  structured.destination_root = scratch.base / "structured";
  structured.structured = true;
  const std::optional<StagedInstall> tree =
      PlanInstall(structured, "other-offer", "Other Offer", &error);
  CHECK_TRUE(tree.has_value());
  CHECK_TRUE(tree && EnsureStagingDirectory(*tree, &error));
  WriteFile(tree->part_path, "CON tree");
  fs::path committed;
  CHECK_TRUE(tree && CommitStagedInstall(*tree, &committed, &error));
  CHECK_TRUE(committed.parent_path() == structured.destination_root / "45410914" / "00000002");

  // Discard removes the part file and never touches a committed package.
  WriteFile(tree->part_path, "CON to discard");
  DiscardStagedFile(*tree);
  CHECK_FALSE(fs::exists(tree->part_path));
  CHECK_TRUE(fs::exists(committed));
}

// --- The transfer policy --------------------------------------------------------------------------

struct TransferFixture {
  Scratch scratch;
  InstallLayout layout;
  fs::path part;
  FakeServer server;

  TransferFixture() {
    layout.destination_root = scratch.base / "library";
    part = scratch.base / "part.bin";
  }

  TransferOutcome Run(const OfferRecord& offer, const std::atomic<bool>* cancel = nullptr,
                      std::function<bool()> enabled = {}) {
    return TransferPackage(offer, part, server.Fn(), cancel, enabled);
  }
};

void TestTransferHappyPath() {
  BeginCase("a direct same-site package is accepted and written to the part file");
  TransferFixture f;
  const std::string body = StfsBody();
  OfferRecord offer{"file-1", "https://rhythmverse.co/download_file/abc", static_cast<long long>(body.size())};
  ScriptedResponse response;
  response.body = body;
  f.server.Add(offer.url, response);
  const TransferOutcome outcome = f.Run(offer);
  CHECK_TRUE(outcome.error == DownloadError::kNone);
  CHECK_EQ(outcome.bytes, body.size());
  CHECK_EQ(outcome.title_id, kRb3TitleId);
  CHECK_STR_EQ(outcome.final_url, offer.url);
  CHECK_STR_EQ(ReadFile(f.part), body);
}

void TestTransferRedirects() {
  BeginCase("redirects are followed manually, validated, and bounded");
  TransferFixture f;
  const std::string body = StfsBody();
  OfferRecord offer{"file-2", "https://rhythmverse.co/download_file/start", body.size()};
  ScriptedResponse hop;
  hop.status = 302;
  hop.location = "/download_file/final";
  hop.body = "<html>not read</html>";
  hop.content_type = "text/html";
  f.server.Add(offer.url, hop);
  ScriptedResponse final;
  final.body = body;
  f.server.Add("https://rhythmverse.co/download_file/final", final);
  TransferOutcome outcome = f.Run(offer);
  CHECK_TRUE(outcome.error == DownloadError::kNone);
  CHECK_STR_EQ(outcome.final_url, "https://rhythmverse.co/download_file/final");

  // An off-site redirect is a policy failure before the body is read.
  TransferFixture offsite;
  ScriptedResponse evil;
  evil.status = 302;
  evil.location = "https://evil.example/download_file/x";
  offsite.server.Add(offer.url, evil);
  OfferRecord other{"file-3", offer.url, body.size()};
  outcome = offsite.Run(other);
  CHECK_TRUE(outcome.error == DownloadError::kPolicy);

  // A scheme-relative redirect is refused too.
  TransferFixture relative;
  ScriptedResponse scheme_relative;
  scheme_relative.status = 302;
  scheme_relative.location = "//rhythmverse.co/download_file/x";
  relative.server.Add(offer.url, scheme_relative);
  outcome = relative.Run(other);
  CHECK_TRUE(outcome.error == DownloadError::kPolicy);
}

void TestTransferRefusals() {
  BeginCase("archives, pages and wrong bytes never reach the library");
  const std::string body = StfsBody();
  const OfferRecord offer{"file-4", "https://rhythmverse.co/download_file/x", body.size()};

  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = "PK\x03\x04 an archive";
    response.length = static_cast<long long>(response.body.size());
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kArchive);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = "<!DOCTYPE html><html>login</html>";
    response.length = static_cast<long long>(response.body.size());
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kBadPrefix);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.content_type = "text/html";
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kPolicy);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.disposition = "attachment; filename=\"song.zip\"";
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kArchive);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.encoding = "gzip";
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kPolicy);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = StfsBody(0x12345678);
    response.length = static_cast<long long>(response.body.size());
    f.server.Add(offer.url, response);
    const OfferRecord wrong{"file-5", offer.url, response.body.size()};
    CHECK_TRUE(f.Run(wrong).error == DownloadError::kWrongTitle);
  }
  {
    // STFS magic, but too short to be a mountable container.
    TransferFixture f;
    ScriptedResponse response;
    response.body = "CON \x00\x00\x00\x00";
    response.length = static_cast<long long>(response.body.size());
    f.server.Add(offer.url, response);
    const OfferRecord invalid{"file-6", offer.url, response.body.size()};
    CHECK_TRUE(f.Run(invalid).error == DownloadError::kInvalidPackage);
  }
}

void TestTransferLengths() {
  BeginCase("lengths must agree, and the ceiling is enforced without one");
  const std::string body = StfsBody();
  const OfferRecord offer{"file-7", "https://rhythmverse.co/download_file/x", body.size()};

  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    f.server.Add(offer.url, response);
    const OfferRecord mismatched{"file-8", offer.url, static_cast<long long>(body.size() + 100)};
    const TransferOutcome outcome = f.Run(mismatched);
    CHECK_TRUE(outcome.error == DownloadError::kLengthMismatch);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.length = static_cast<long long>(body.size() + 100);  // truncated transfer
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kLengthMismatch);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.length = static_cast<long long>(kMaxDownloadBytes + 1);
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kTooLarge);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.omit_length = true;
    f.server.Add(offer.url, response);
    const OfferRecord unknown{"file-9", offer.url, std::nullopt};
    CHECK_TRUE(f.Run(unknown).error == DownloadError::kNone);
  }
}

void TestTransferStatusAndCancel() {
  BeginCase("HTTP failures are reported, and the master switch stops a transfer mid-body");
  const std::string body = StfsBody();
  const OfferRecord offer{"file-10", "https://rhythmverse.co/download_file/x", body.size()};

  {
    TransferFixture f;
    ScriptedResponse response;
    response.status = 429;
    response.retry_after = "2";
    response.body = "slow down";
    f.server.Add(offer.url, response);
    const TransferOutcome outcome = f.Run(offer);
    CHECK_TRUE(outcome.error == DownloadError::kHttpStatus);
    CHECK_EQ(outcome.http_status, 429);
    CHECK_TRUE(outcome.retry_after_seconds.has_value() &&
               *outcome.retry_after_seconds == 2);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.status = 404;
    response.body = "not found";
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kHttpStatus);
  }
  {
    TransferFixture f;
    ScriptedResponse response;
    response.transport_failure = true;
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer).error == DownloadError::kNetwork);
  }
  {
    // The settings snapshot is read per chunk: turning the store off stops the transfer.
    TransferFixture f;
    ScriptedResponse response;
    response.body = body;
    response.chunk = 4096;
    bool enabled = true;
    response.after_first_chunk = [&enabled] { enabled = false; };
    f.server.Add(offer.url, response);
    const TransferOutcome outcome = f.Run(offer, nullptr, [&enabled] { return enabled; });
    CHECK_TRUE(outcome.error == DownloadError::kDisabled);
  }
  {
    TransferFixture f;
    std::atomic<bool> cancel{false};
    ScriptedResponse response;
    response.body = body;
    response.chunk = 4096;
    response.after_first_chunk = [&cancel] { cancel.store(true); };
    f.server.Add(offer.url, response);
    CHECK_TRUE(f.Run(offer, &cancel).error == DownloadError::kCancelled);
  }
}

// --- The service ----------------------------------------------------------------------------------

struct ServiceFixture {
  Scratch scratch;
  int refresh_calls = 0;
  std::vector<std::string> log_lines;

  ServiceFixture() {
    DownloadConfig config;
    config.layout.destination_root = scratch.base / "library";
    config.receipt_path = scratch.base / "store" / "downloads.tsv";
    config.retry.base_delay_ms = 1;
    config.retry.max_delay_ms = 2;
    PublishEnabled(true);
    ResetDownloadsForTesting();
    // Configure after the reset (Reset drops the configuration).
    config.transfer = server.Fn();
    config.refresh = [this] { ++refresh_calls; };
    config.log = [this](const std::string_view line, const bool) {
      log_lines.emplace_back(line);
    };
    ConfigureDownloads(std::move(config));
  }

  ~ServiceFixture() {
    ResetDownloadsForTesting();
    PublishEnabled(false);
  }

  std::string StartJob(const std::string& file_id, const std::string& url,
                       const std::string& body) {
    OfferRecord offer;
    offer.file_id = file_id;
    offer.url = url;
    offer.api_size = static_cast<long long>(body.size());
    ScriptedResponse response;
    response.body = body;
    server.Add(url, response);
    const std::optional<std::string> token = RegisterOffer(offer);
    if (!token) {
      return {};
    }
    HandleDownloadWrite("rbbz-store/download/start/" + *token);
    WaitForDownloadsIdle();
    return *token;
  }

  FakeServer server;
};

void TestServiceInstall() {
  BeginCase("the service installs one package, answers its state and writes a receipt");
  ServiceFixture f;
  const std::string body = StfsBody(kRb3TitleId, 1, 16);
  const std::string token = f.StartJob("file-100", "https://rhythmverse.co/download_file/a", body);
  CHECK_TRUE(IsValidJobToken(token));
  const std::optional<JobStatus> status = QueryJob(token);
  CHECK_TRUE(status.has_value());
  CHECK_TRUE(status && status->state == JobState::kInstalled);
  CHECK_EQ(status ? status->received : 0, body.size());
  const fs::path installed = InstalledPath(token);
  CHECK_TRUE(!installed.empty());
  CHECK_STR_EQ(installed.parent_path().string(), (f.scratch.base / "library").string());
  CHECK_STR_EQ(ReadFile(installed), body);
  CHECK_EQ(f.refresh_calls, 1);

  bool answer = false;
  CHECK_TRUE(AnswerDownloadQuery("rbbz-store/state/" + token + "/installed", &answer));
  CHECK_TRUE(answer);
  CHECK_TRUE(AnswerDownloadQuery("rbbz-store/state/" + token + "/downloading", &answer));
  CHECK_FALSE(answer);
  CHECK_TRUE(AnswerDownloadQuery("rbbz-store/error/" + token + "/archive", &answer));
  CHECK_FALSE(answer);
  CHECK_FALSE(AnswerDownloadQuery("rbbz-store/enabled", &answer));
  CHECK_FALSE(AnswerDownloadQuery("somewhere/else", &answer));

  const std::vector<Receipt> receipts = ReadReceipts(f.scratch.base / "store" / "downloads.tsv");
  CHECK_EQ(receipts.size(), 1);
  CHECK_TRUE(!receipts.empty() && receipts[0].file_id == "file-100");
  CHECK_TRUE(!receipts.empty() && receipts[0].pending);
  CHECK_EQ(!receipts.empty() ? receipts[0].bytes : 0, body.size());

  // Start is idempotent; Download Again installs a second, distinct package.
  HandleDownloadWrite("rbbz-store/download/start/" + token);
  WaitForDownloadsIdle();
  CHECK_EQ(ReadReceipts(f.scratch.base / "store" / "downloads.tsv").size(), 1);
  HandleDownloadWrite("rbbz-store/download/again/" + token);
  WaitForDownloadsIdle();
  CHECK_EQ(ReadReceipts(f.scratch.base / "store" / "downloads.tsv").size(), 2);
  CHECK_EQ(f.refresh_calls, 2);
}

void TestServiceCancelAndOff() {
  BeginCase("cancel, queued cancel and master Off leave no package and no receipt");
  const std::string body = StfsBody(kRb3TitleId, 1, 64);

  {
    // Master Off refuses a start before it is queued, and records why.
    ServiceFixture f;
    PublishEnabled(false);
    OfferRecord offer{"file-off", "https://rhythmverse.co/download_file/off", body.size()};
    const std::optional<std::string> token = RegisterOffer(offer);
    CHECK_TRUE(token.has_value());
    HandleDownloadWrite("rbbz-store/download/start/" + *token);
    WaitForDownloadsIdle();
    const std::optional<JobStatus> status = QueryJob(*token);
    CHECK_TRUE(status && status->state == JobState::kCancelled &&
               status->error == DownloadError::kDisabled);
    CHECK_TRUE(ReadReceipts(f.scratch.base / "store" / "downloads.tsv").empty());
  }

  {
    // A running transfer is cancelled by master Off mid-body.
    ServiceFixture f;
    ScriptedResponse response;
    response.body = body;
    response.chunk = 4096;
    response.after_first_chunk = [] { PublishEnabled(false); };
    const std::string url = "https://rhythmverse.co/download_file/off-mid";
    f.server.Add(url, response);
    OfferRecord offer{"file-off-mid", url, body.size()};
    const auto token = RegisterOffer(offer);
    CHECK_TRUE(token.has_value());
    HandleDownloadWrite("rbbz-store/download/start/" + *token);
    WaitForDownloadsIdle();
    const std::optional<JobStatus> status = QueryJob(*token);
    CHECK_TRUE(status.has_value());
    CHECK_TRUE(status && status->state == JobState::kCancelled);
    CHECK_TRUE(status && status->error == DownloadError::kDisabled);
    bool answer = false;
    CHECK_TRUE(AnswerDownloadQuery("rbbz-store/error/" + *token + "/disabled", &answer));
    CHECK_TRUE(answer);
    CHECK_TRUE(ReadReceipts(f.scratch.base / "store" / "downloads.tsv").empty());
    CHECK_TRUE(InstalledPath(*token).empty());
  }

  {
    // A queued job is cancelled before the worker reaches it.
    ServiceFixture f;
    std::atomic<bool> release{false};
    std::atomic<bool> seen_chunk{false};
    ScriptedResponse slow;
    slow.body = body;
    slow.chunk = 1024;
    const std::string url = "https://rhythmverse.co/download_file/slow";
    slow.after_first_chunk = [&] {
      seen_chunk.store(true);
      while (!release.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    };
    f.server.Add(url, slow);
    OfferRecord first{"file-slow", url, body.size()};
    OfferRecord second{"file-queued", url, body.size()};
    const auto first_token = RegisterOffer(first);
    const auto second_token = RegisterOffer(second);
    CHECK_TRUE(first_token.has_value() && second_token.has_value());
    HandleDownloadWrite("rbbz-store/download/start/" + *first_token);
    for (int i = 0; i < 2000 && !seen_chunk.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_TRUE(seen_chunk.load());
    HandleDownloadWrite("rbbz-store/download/start/" + *second_token);
    HandleDownloadWrite("rbbz-store/download/cancel/" + *second_token);
    const std::optional<JobStatus> queued = QueryJob(*second_token);
    CHECK_TRUE(queued && queued->state == JobState::kCancelled);
    release.store(true);
    WaitForDownloadsIdle();
    CHECK_TRUE(QueryJob(*first_token)->state == JobState::kInstalled);
    CHECK_TRUE(QueryJob(*second_token)->state == JobState::kCancelled);
    CHECK_EQ(ReadReceipts(f.scratch.base / "store" / "downloads.tsv").size(), 1);
  }
}

void TestServiceUnknownOffer() {
  BeginCase("a token that was never registered is refused, never a URL");
  ServiceFixture f;
  const std::string forged = JobTokenForFileId("forged");
  HandleDownloadWrite("rbbz-store/download/start/" + forged);
  WaitForDownloadsIdle();
  CHECK_FALSE(QueryJob(forged).has_value());
  CHECK_FALSE(RegisterOffer(OfferRecord{"", "https://rhythmverse.co/download_file/x", 1})
                  .has_value());
  CHECK_FALSE(RegisterOffer(OfferRecord{"id", "", 1}).has_value());
  bool consumed = HandleDownloadWrite("rbbz-store/download/start/not-a-token");
  CHECK_TRUE(consumed);
  CHECK_FALSE(HandleDownloadWrite("rbbz-settings/something"));
}

void TestStagingExclusion() {
  BeginCase("the staging directory is invisible to both library scans and the fingerprint");
  Scratch scratch;
  const fs::path root = scratch.base / "library";
  WriteFile(root / "song.con", StfsBody(kRb3TitleId, 1));
  WriteFile(root / rbfs::kStoreStagingDirName / "partial.con", StfsBody(kRb3TitleId, 1));

  const rbfs::LibraryScanResult scan = rbfs::ScanDlcLibraries({root}, 0, 1);
  CHECK_EQ(scan.items.size(), 1);
  CHECK_EQ(scan.rejected.size(), 0);
  CHECK_TRUE(!scan.items.empty() &&
             scan.items[0].host_path.filename().string() == "song.con");

  // A structured root reports the staging directory as neither a title nor a rejection.
  const fs::path structured_root = scratch.base / "structured-root";
  WriteFile(structured_root / rbfs::kStoreStagingDirName / "partial.con",
            StfsBody(kRb3TitleId, 1));
  const rbfs::DlcScanResult structured = rbfs::ScanDlcRoot(structured_root);
  CHECK_EQ(structured.packages.size(), 0);
  CHECK_EQ(structured.rejected.size(), 0);
  CHECK_FALSE(rbfs::HasStructuredLayout(structured_root));

  rbfs::TreeFingerprint before;
  std::string reason;
  CHECK_TRUE(rbfs::ComputeTreeFingerprint({root}, &before, &reason));
  WriteFile(root / rbfs::kStoreStagingDirName / "another.part",
            std::string(1024 * 64, 'x'));
  rbfs::TreeFingerprint after;
  CHECK_TRUE(rbfs::ComputeTreeFingerprint({root}, &after, &reason));
  CHECK_TRUE(before == after);
}

void TestDeleteAndDownloadFlow() {
  BeginCase("manual installs match by artist and title, delete, and can be downloaded again");
  Scratch scratch;
  PublishEnabled(true);
  ResetDownloadsForTesting();
  const fs::path original = scratch.base / "manual-song.con";
  const fs::path unrelated = scratch.base / "unrelated.con";
  WriteFile(original, StfsBody());
  WriteFile(unrelated, "keep me");
  FakeServer server;
  DownloadConfig config;
  config.layout.destination_root = scratch.base / "downloads";
  config.transfer = server.Fn();
  config.packages = [&](std::string_view artist, std::string_view song) {
    if (FoldForMatch(artist) == "artist" && FoldForMatch(song) == "song" && fs::exists(original))
      return std::vector<fs::path>{original};
    return std::vector<fs::path>{};
  };
  bool forgotten = false;
  config.forget_package = [&](const fs::path& path) { forgotten = path == original; };
  config.library_ready = [] { return true; };
  ConfigureDownloads(config);
  const OfferRecord offer{"manual-match", "https://rhythmverse.co/download_file/manual",
                           std::nullopt, "Artist - Song", "ARTIST", "Song"};
  const auto token = RegisterOffer(offer).value_or("");
  bool answer = false;
  CHECK_TRUE(AnswerDownloadQuery("rbbz-store/owned/" + token, &answer));
  CHECK_TRUE(answer);
  // A title without the same artist never owns this package.
  auto other = offer;
  other.file_id = "other";
  other.artist = "Someone Else";
  const auto other_token = RegisterOffer(other).value_or("");
  AnswerDownloadQuery("rbbz-store/owned/" + other_token, &answer);
  CHECK_FALSE(answer);
  HandleDownloadWrite("rbbz-store/download/delete/" + token);
  WaitForDownloadsIdle();
  CHECK_TRUE(forgotten);
  CHECK_FALSE(fs::exists(original));
  CHECK_STR_EQ(ReadFile(unrelated), "keep me");
  CHECK_TRUE(QueryJob(token)->state == JobState::kDeleted);
  AnswerDownloadQuery("rbbz-store/owned/" + token, &answer);
  CHECK_FALSE(answer);
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_TRUE(answer);
  HandleDownloadWrite("rbbz-store/library/dismiss-warning");
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_FALSE(answer);
  ScriptedResponse response;
  response.body = StfsBody();
  server.Add(offer.url, response);
  HandleDownloadWrite("rbbz-store/download/start/" + token);
  WaitForDownloadsIdle();
  CHECK_TRUE(QueryJob(token)->state == JobState::kInstalled);
  AnswerDownloadQuery("rbbz-store/owned/" + token, &answer);
  CHECK_TRUE(answer);
  // Deleting a file installed in this session resets the installed job too.
  HandleDownloadWrite("rbbz-store/download/delete/" + token);
  WaitForDownloadsIdle();
  CHECK_TRUE(QueryJob(token)->state == JobState::kDeleted);
  CHECK_TRUE(InstalledPath(token).empty());
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_TRUE(answer);
  ClearStoreRefreshWarning();
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_FALSE(answer);
  ResetDownloadsForTesting();
  PublishEnabled(false);
}

void TestDeleteDuringInstallCompletion() {
  BeginCase("a delete arriving during install completion waits for the worker and can refresh");
  Scratch scratch;
  PublishEnabled(true);
  ResetDownloadsForTesting();
  FakeServer server;
  DownloadConfig config;
  config.layout.destination_root = scratch.base / "downloads";
  config.transfer = server.Fn();
  std::string token;
  int refreshes = 0;
  config.refresh = [&] {
    if (++refreshes == 1) HandleDownloadWrite("rbbz-store/download/delete/" + token);
  };
  ConfigureDownloads(config);
  OfferRecord offer{"completion", "https://rhythmverse.co/download_file/completion", {},
                     "Artist - Song", "Artist", "Song"};
  token = RegisterOffer(offer).value_or("");
  ScriptedResponse response;
  response.body = StfsBody();
  server.Add(offer.url, response);
  HandleDownloadWrite("rbbz-store/download/start/" + token);
  WaitForDownloadsIdle();
  CHECK_TRUE(QueryJob(token)->state == JobState::kDeleted);
  bool answer = false;
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_TRUE(answer);
  HandleDownloadWrite("rbbz-store/library/refresh");
  CHECK_EQ(refreshes, 2);
  AnswerDownloadQuery("rbbz-store/library/refresh-needed", &answer);
  CHECK_FALSE(answer);
  ResetDownloadsForTesting();
  PublishEnabled(false);
}

void TestDeleteRefusalsAndPacks() {
  BeginCase("deletion refuses unknown tokens and invalid roots, and handles whole extracted packs");
  Scratch scratch;
  PublishEnabled(true);
  ResetDownloadsForTesting();
  const auto pack = scratch.base / "pack";
  WriteFile(pack / "songs" / "songs.dta", "(songs)");
  WriteFile(pack / "songs" / "audio", "test audio");
  DownloadConfig config;
  config.layout.destination_root = scratch.base / "downloads";
  config.packages = [&](std::string_view, std::string_view) { return std::vector<fs::path>{pack}; };
  ConfigureDownloads(config);
  auto token = RegisterOffer({"pack", "https://rhythmverse.co/download_file/pack", {},
                             "Artist - Song", "Artist", "Song"}).value_or("");
  HandleDownloadWrite("rbbz-store/download/delete/" + JobTokenForFileId("unknown"));
  WaitForDownloadsIdle();
  CHECK_TRUE(fs::exists(pack));
  HandleDownloadWrite("rbbz-store/download/delete/" + token);
  WaitForDownloadsIdle();
  CHECK_FALSE(fs::exists(pack));
  CHECK_TRUE(QueryJob(token)->state == JobState::kDeleted);
  ResetDownloadsForTesting();
  config.packages = [&](std::string_view, std::string_view) {
    return std::vector<fs::path>{scratch.base};  // no songs directory: not an extracted package
  };
  ConfigureDownloads(config);
  token = RegisterOffer({"refuse", "https://rhythmverse.co/download_file/refuse", {},
                         "Artist - Song", "Artist", "Song"}).value_or("");
  HandleDownloadWrite("rbbz-store/download/delete/" + token);
  WaitForDownloadsIdle();
  CHECK_TRUE(fs::exists(scratch.base));
  CHECK_TRUE(QueryJob(token)->state == JobState::kFailed);
  ResetDownloadsForTesting();
  PublishEnabled(false);
}

}  // namespace

int main() {
  TestJobTokens();
  TestBridgeGrammar();
  TestRetryPolicy();
  TestInstallPlanning();
  TestStagingAndCommit();
  TestTransferHappyPath();
  TestTransferRedirects();
  TestTransferRefusals();
  TestTransferLengths();
  TestTransferStatusAndCancel();
  TestServiceInstall();
  TestServiceCancelAndOff();
  TestServiceUnknownOffer();
  TestStagingExclusion();
  TestDeleteAndDownloadFlow();
  TestDeleteRefusalsAndPacks();
  TestDeleteDuringInstallCompletion();
  return Finish();
}
