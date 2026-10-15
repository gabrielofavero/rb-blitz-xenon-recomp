// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The helper's commands, one function each.
//
// Every command follows the same shape: parse the keys it knows, do the work,
// then write `--summary` on the way out whether it succeeded or not, so the
// wizard always has something to show the user. Nothing is communicated through
// stdout: the wizard reads the summary file (a single `key=value;key=value`
// line) and, on failure, the tail of the log.
//
// Exit codes are the contract:
//   0  the command did what it was asked
//   1  it did not, and the summary carries `ok=0` plus `error=<reason>`
//   2  the command line was wrong, which is a bug in the wizard, not a user error

#include "commands.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.h"
#include "download.h"
#include "install.h"
#include "profile/profile_path.h"
#include "paths.h"
#include "release_api.h"
#include "stfs.h"
#include "util.h"
#include "util/game_fingerprint.h"
#include "util/sha256.h"
#include "zip.h"

namespace rb_blitz::installer {
namespace {

namespace fs = std::filesystem;

constexpr int kOk = kSuccessExitCode;
constexpr int kFailed = kFailedExitCode;
constexpr int kUsage = kUsageExitCode;

// Arguments arrive as UTF-8 from main(); the filesystem wants wide characters,
// because the ANSI code page on a machine this installer runs on is not
// necessarily UTF-8.
fs::path PathFromUtf8(std::string_view utf8) { return fs::path(Widen(utf8)); }

// The layout constants in install.h are UTF-8 names, so they go through the same
// conversion as an argument does.
fs::path PathOf(std::string_view name) { return PathFromUtf8(name); }

std::string Display(const fs::path& path) { return Narrow(path.native()); }

// --- the command line -----------------------------------------------------

// `--key value` and `--key=value` are both accepted: the wizard generates the
// first form, and a person debugging an install at a prompt tends to type the
// second. A key with no value at all reads back as "1".
struct Options {
  std::map<std::string, std::string, std::less<>> values;

  bool Has(std::string_view key) const { return values.find(key) != values.end(); }

  const std::string* Find(std::string_view key) const {
    const auto found = values.find(key);
    return found == values.end() ? nullptr : &found->second;
  }

  std::string Get(std::string_view key, std::string_view fallback = {}) const {
    const std::string* found = Find(key);
    return found != nullptr ? *found : std::string(fallback);
  }

  bool GetBool(std::string_view key, bool fallback = false) const {
    const std::string* found = Find(key);
    if (found == nullptr || found->empty()) {
      return fallback;
    }
    return *found == "1" || EqualsIgnoreCase(*found, "true") || EqualsIgnoreCase(*found, "yes");
  }

  std::uint64_t GetUnsigned(std::string_view key, std::uint64_t fallback = 0) const {
    const std::string* found = Find(key);
    if (found == nullptr || found->empty()) {
      return fallback;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(found->c_str(), &end, 10);
    return end != nullptr && *end == '\0' ? value : fallback;
  }
};

// Rejects anything the command does not understand. A typo in the wizard's
// argument list should stop the install on the first run, not silently ignore
// the key it meant to pass.
bool ParseOptions(const std::vector<std::string>& args,
                  const std::vector<std::string_view>& allowed, Options* out, std::string* error) {
  for (std::size_t index = 0; index < args.size(); ++index) {
    const std::string& token = args[index];
    if (token.size() < 3 || token.compare(0, 2, "--") != 0) {
      SetError(error, S("unexpected argument '", token, "'"));
      return false;
    }
    std::string key = token.substr(2);
    std::string value;
    bool has_value = false;
    if (const std::size_t equals = key.find('='); equals != std::string::npos) {
      value = key.substr(equals + 1);
      key.resize(equals);
      has_value = true;
    }
    if (std::find(allowed.begin(), allowed.end(), std::string_view(key)) == allowed.end()) {
      std::vector<std::string> names;
      names.reserve(allowed.size());
      for (const std::string_view name : allowed) {
        names.emplace_back(S("--", name));
      }
      SetError(error, S("unknown option '--", key, "'; this command accepts ", Join(names, ", ")));
      return false;
    }
    if (!has_value && index + 1 < args.size() && args[index + 1].compare(0, 2, "--") != 0) {
      value = args[++index];
      has_value = true;
    }
    out->values[key] = has_value ? value : "1";
  }
  return true;
}

// --- summary --------------------------------------------------------------

// `--summary` is one `key=value;key=value` line, so a value may not contain the
// separators. A path or a hash never does; a log line might, so those are
// flattened instead of escaped.
std::string SummarySafe(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(character == ';' || character == '=' || character == '\r' ||
                             character == '\n'
                         ? ' '
                         : character);
  }
  return result;
}

struct Summary {
  fs::path file;
  std::vector<std::pair<std::string, std::string>> pairs;

  void Text(std::string_view key, std::string_view value) {
    pairs.emplace_back(std::string(key), SummarySafe(value));
  }
  void Number(std::string_view key, std::uint64_t value) { Text(key, S(value)); }
  void Flag(std::string_view key, bool value) { Text(key, value ? "1" : "0"); }

  void Write() const { WriteSummaryFile(file, pairs); }
};

struct CommandContext {
  Options options;
  Summary summary;
  std::string error;

  bool Has(std::string_view key) const { return options.Has(key); }
  std::string Get(std::string_view key, std::string_view fallback = {}) const {
    return options.Get(key, fallback);
  }
  bool GetBool(std::string_view key, bool fallback = false) const {
    return options.GetBool(key, fallback);
  }
  std::uint64_t GetUnsigned(std::string_view key, std::uint64_t fallback = 0) const {
    return options.GetUnsigned(key, fallback);
  }
};

std::vector<std::string_view> Keys(std::initializer_list<std::string_view> extra) {
  // The wizard wraps every helper call in the same way, so the reporting keys it
  // always passes are always accepted: a command with nothing to report simply
  // writes nothing. `--details` is not one of them, because a stale file list
  // being read back would be worse than no list at all.
  std::vector<std::string_view> keys = {"summary", "log", "progress"};
  keys.insert(keys.end(), extra.begin(), extra.end());
  return keys;
}

// The summary path straight off the raw command line, for the case where the
// rest of the line was rejected: the caller needs the rejection in the summary
// far more than it needs the keys it got wrong.
fs::path SummaryPathFromRawArgs(const std::vector<std::string>& args) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index) {
    if (args[index] == "--summary") {
      return PathFromUtf8(args[index + 1]);
    }
    if (args[index].compare(0, 10, "--summary=") == 0) {
      return PathFromUtf8(args[index].substr(10));
    }
  }
  return {};
}

// Every command starts here. A command line this tool cannot parse is reported
// on stderr and, when the line named one, through the summary as well.
bool Begin(const std::vector<std::string>& args, const std::vector<std::string_view>& allowed,
           CommandContext* context) {
  if (!ParseOptions(args, allowed, &context->options, &context->error)) {
    context->summary.file = SummaryPathFromRawArgs(args);
    context->summary.Flag("ok", false);
    context->summary.Text("error", context->error);
    context->summary.Write();
    std::fprintf(stderr, "error: %s\n\n%s", context->error.c_str(), UsageText().c_str());
    return false;
  }
  context->summary.file = PathFromUtf8(context->options.Get("summary"));
  log::Open(PathFromUtf8(context->options.Get("log")));
  return true;
}

int Fail(CommandContext* context, std::string_view reason) {
  log::Error(reason);
  context->summary.Flag("ok", false);
  context->summary.Text("error", reason);
  context->summary.Write();
  return kFailed;
}

int Succeed(CommandContext* context, std::string_view detail = {}) {
  if (!detail.empty()) {
    log::Info(detail);
  }
  context->summary.Flag("ok", true);
  context->summary.Write();
  return kOk;
}

// Leaves whatever the command already reported in place, so the wizard can still
// show the user which files it found before the failure.
void Override(std::string* target, const Options& options, std::string_view key) {
  if (const std::string* value = options.Find(key); value != nullptr && !value->empty()) {
    *target = *value;
  }
}

// --- shared reporting -----------------------------------------------------

void AddPlanSummary(Summary* summary, const GameSourcePlan& plan) {
  summary->Text("kind", plan.kind == GameSourceKind::kPackage ? "package" : "folder");
  summary->Text("location", Display(plan.location));
  summary->Text("description", plan.description);
  summary->Number("files", plan.files.size());
  summary->Number("required", plan.RequiredCount());
  summary->Number("present", plan.PresentCount());
  summary->Number("bytes", plan.PresentBytes());
  summary->Flag("complete", plan.Complete());
  summary->Text("missing", Join(plan.Missing(), ", "));
  if (plan.kind == GameSourceKind::kPackage) {
    summary->Text("title_id", HexU32(plan.volume.title_id));
    summary->Number("entries", plan.package_entries);
  }
}

// The `--details` memo: what the source holds, one line per file, for the page
// that shows the user what was found.
void WritePlanDetails(const fs::path& file, const GameSourcePlan& plan) {
  if (file.empty()) {
    return;
  }
  std::size_t required_present = 0;
  std::vector<std::string> lines;
  lines.push_back(S("source   : ", plan.description));
  lines.push_back(S("location : ", Display(plan.location)));
  for (const GameFilePlan& entry : plan.files) {
    if (entry.required && entry.present) {
      ++required_present;
    }
    const std::string status = !entry.present ? "missing" : (entry.required ? "found  " : "extra  ");
    lines.push_back(S(status, " : ", entry.path,
                     entry.sha256.empty() ? "" : S("  ", entry.sha256.substr(0, 16))));
  }
  lines.push_back(S("required : ", required_present, " of ", plan.RequiredCount(), " present"));
  std::string error;
  if (!WriteFileText(file, Join(lines, "\r\n") + "\r\n", false, &error)) {
    log::Warn(S("cannot write ", Display(file), ": ", error));
  }
}

void WriteLines(const fs::path& file, const std::vector<std::string>& lines) {
  if (file.empty() || lines.empty()) {
    return;
  }
  std::string error;
  if (!WriteFileText(file, Join(lines, "\r\n") + "\r\n", false, &error)) {
    log::Warn(S("cannot write ", Display(file), ": ", error));
  }
}

void AddComponentSummary(Summary* summary, const ComponentResult& component) {
  summary->Text("source", component.source);
  summary->Text("version", component.version.empty() ? "unknown" : component.version);
  summary->Number("files", component.files);
  summary->Number("bytes", component.bytes);
  summary->Flag("verified", component.fingerprints_matched);
  summary->Number("warning_count", component.notes.size());
  for (std::size_t index = 0; index < component.notes.size(); ++index) {
    summary->Text(S("warning_", index), component.notes[index]);
  }
}

void TreeTotals(const fs::path& root, std::size_t* files, std::uint64_t* bytes) {
  *files = 0;
  *bytes = 0;
  std::error_code code;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                            code);
  const fs::recursive_directory_iterator end;
  for (; !code && iterator != end; iterator.increment(code)) {
    if (!iterator->is_regular_file(code)) {
      continue;
    }
    ++*files;
    *bytes += iterator->file_size(code);
  }
}

// --- source resolution ----------------------------------------------------

// The download pin for whichever component is being resolved, so the payload and
// the Ultimate mod can share one function.
struct PinSource {
  std::string version;
  std::string url;
  std::string sha256;
  std::uint64_t size = 0;
};

PinSource PinOfPayload(const PinsPayload& pins) {
  return PinSource{pins.version, pins.url, pins.sha256, pins.size};
}

PinSource PinOfUltimate(const PinsUltimate& pins) {
  return PinSource{pins.version, pins.url, pins.sha256, pins.size};
}

// Turns the user's choice into a SourceRef. At most one of the `--from*` keys,
// and none at all means "the file is already in place" for the payload.
bool ResolveSource(const CommandContext& context, std::string_view label, const PinSource& pinned,
                   bool allow_embedded, SourceRef* out, std::string* error) {
  std::vector<std::string_view> chosen;
  for (const std::string_view key : {"from-pinned", "from-zip", "from-dir", "from-url"}) {
    if (context.Has(key)) {
      chosen.push_back(key);
    }
  }
  if (chosen.size() > 1) {
    SetError(error, S("pick one source for the ", label, ": --", chosen[0], " and --", chosen[1],
                     " cannot be combined"));
    return false;
  }
  out->version = context.Get("version", pinned.version);

  if (chosen.empty()) {
    if (!allow_embedded) {
      SetError(error, S("no source given for the ", label));
      return false;
    }
    out->kind = SourceKind::kEmbedded;
    return true;
  }

  const std::string_view key = chosen.front();
  if (key == "from-pinned") {
    if (pinned.url.empty()) {
      SetError(error, S("this build has no ", label,
                        " download pinned; add one to installer/config/pins.toml or pick a local "
                        "archive"));
      return false;
    }
    out->kind = SourceKind::kUrl;
    out->url = pinned.url;
    out->sha256 = pinned.sha256;
    out->size = pinned.size;
    return true;
  }
  if (key == "from-url") {
    out->kind = SourceKind::kUrl;
    out->url = context.Get("from-url");
    out->sha256 = context.Get("sha256");
    out->size = context.GetUnsigned("size");
    if (out->url.empty()) {
      SetError(error, "--from-url needs a URL");
      return false;
    }
    return true;
  }
  out->kind = key == "from-zip" ? SourceKind::kZip : SourceKind::kFolder;
  out->path = PathFromUtf8(context.Get(key));
  if (out->path.empty()) {
    SetError(error, S("--", key, " needs a path"));
    return false;
  }
  return true;
}

// --- zip helpers ----------------------------------------------------------

// The single top-level folder every entry lives under, or empty when the archive
// has no such folder. Release archives are built both ways.
std::string ArchiveRoot(const ZipArchive& archive) {
  std::string root;
  for (const ZipEntry& entry : archive.entries()) {
    const std::size_t slash = entry.name.find('/');
    if (slash == std::string::npos) {
      return {};
    }
    const std::string_view top(entry.name.data(), slash);
    if (root.empty()) {
      root.assign(top);
    } else if (!EqualsIgnoreCase(root, top)) {
      return {};
    }
  }
  return root;
}

// --- checks ---------------------------------------------------------------

int CommandCheckSpace(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "required-bytes"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "check-space needs --dest");
  }
  const std::uint64_t required = context.GetUnsigned("required-bytes");

  // The install directory usually does not exist yet, so ask the nearest
  // ancestor that does: it is the same volume, which is what the question is
  // about.
  fs::path probe = PathFromUtf8(dest);
  while (!probe.empty() && !DirectoryExists(probe)) {
    const fs::path parent = probe.parent_path();
    if (parent == probe) {
      probe.clear();
      break;
    }
    probe = parent;
  }
  if (probe.empty()) {
    return Fail(&context, S("cannot find a usable folder for ", dest));
  }

  const std::uint64_t free_bytes = FreeBytesAvailable(probe);
  const std::uint64_t total_bytes = TotalBytes(probe);
  context.summary.Text("path", Display(probe));
  context.summary.Number("free", free_bytes);
  context.summary.Number("total", total_bytes);
  context.summary.Number("required", required);
  context.summary.Flag("enough", free_bytes >= required);
  if (free_bytes < required) {
    return Fail(&context,
                S("only ", HumanBytes(free_bytes), " free in ", Display(probe), ", but ",
                  HumanBytes(required), " is needed"));
  }
  return Succeed(&context, S(HumanBytes(free_bytes), " free in ", Display(probe)));
}

// --- probing sources ------------------------------------------------------

int CommandProbeFolder(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"folder", "details"}), &context)) {
    return kUsage;
  }
  const std::string folder = context.Get("folder");
  if (folder.empty()) {
    return Fail(&context, "probe-folder needs --folder");
  }

  GameSourcePlan plan;
  std::string error;
  if (!PlanGameFolder(PathFromUtf8(folder), &plan, &error)) {
    return Fail(&context, error);
  }
  AddPlanSummary(&context.summary, plan);
  WritePlanDetails(PathFromUtf8(context.Get("details")), plan);
  if (!plan.Complete()) {
    return Fail(&context, S("the folder is missing ", plan.Missing().size(),
                            " mandatory file(s): ", Join(plan.Missing(), ", ")));
  }
  return Succeed(&context, plan.description);
}

int CommandProbePackage(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"package", "details"}), &context)) {
    return kUsage;
  }
  const std::string package = context.Get("package");
  if (package.empty()) {
    return Fail(&context, "probe-package needs --package");
  }

  GameSourcePlan plan;
  std::string error;
  if (!PlanGamePackage(PathFromUtf8(package), &plan, &error)) {
    return Fail(&context, error);
  }
  AddPlanSummary(&context.summary, plan);
  WritePlanDetails(PathFromUtf8(context.Get("details")), plan);
  if (!plan.Complete()) {
    return Fail(&context, S("the package is missing ", plan.Missing().size(),
                            " mandatory file(s): ", Join(plan.Missing(), ", ")));
  }
  return Succeed(&context, plan.description);
}

// Reads an archive's central directory only. Nothing is unpacked, so this is
// cheap enough to run while the user is looking at the page: it answers "is this
// the right zip?" before the install commits to it.
int CommandProbeArchive(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"archive", "kind", "details"}), &context)) {
    return kUsage;
  }
  const std::string archive = context.Get("archive");
  if (archive.empty()) {
    return Fail(&context, "probe-archive needs --archive");
  }
  const std::string kind = Lower(context.Get("kind", "payload"));
  if (kind != "payload" && kind != "ultimate") {
    return Fail(&context, S("--kind is payload or ultimate, not '", kind, "'"));
  }
  const fs::path path = PathFromUtf8(archive);
  if (!FileExists(path)) {
    return Fail(&context, S(Display(path), " does not exist"));
  }

  ZipArchive zip;
  std::string error;
  if (!zip.Open(path, &error)) {
    return Fail(&context, error);
  }
  std::error_code code;
  std::uint64_t hashed = 0;
  const std::string sha256 = util::HashFileHex(path, &hashed, code);

  const std::string root = ArchiveRoot(zip);
  context.summary.Text("kind", kind);
  context.summary.Number("entries", zip.entries().size());
  context.summary.Text("sha256", sha256);
  context.summary.Text("root", root);

  std::vector<std::string> lines;
  std::size_t required = 0;
  std::size_t present = 0;
  if (kind == "payload") {
    const std::string manifest =
        root.empty() ? std::string(kPayloadManifestName) : root + "/" + std::string(kPayloadManifestName);
    const ZipEntry* entry = zip.Find(manifest);
    const bool has_manifest = entry != nullptr && !entry->is_directory;
    context.summary.Flag("has_manifest", has_manifest);
    context.summary.Text("description", S("archive ", Display(path), " (",
                                          zip.entries().size(), " entries)"));
    lines.push_back(S("manifest : ", has_manifest ? manifest : "not found"));
    for (const ZipEntry& member : zip.entries()) {
      lines.push_back(S(member.is_directory ? "dir      : " : "file     : ", member.name, "  ",
                        member.uncompressed_size));
    }
    WriteLines(PathFromUtf8(context.Get("details")), lines);
    if (!has_manifest) {
      return Fail(&context, S(Display(path),
                              " does not look like a packaged build: payload-manifest.toml is "
                              "missing"));
    }
    return Succeed(&context, S("archive holds ", zip.entries().size(), " entries"));
  }

  fingerprint::GameFingerprint print;
  if (!fingerprint::ParseGameFingerprint(EmbeddedUltimateFingerprints(), &print, &error)) {
    return Fail(&context, S("the embedded Ultimate fingerprints are invalid: ", error));
  }
  for (const std::string_view role : kUltimateRoles) {
    const fingerprint::FileFingerprint* expected = fingerprint::FindByRole(print, role);
    if (expected == nullptr) {
      continue;
    }
    ++required;
    const std::string name = root.empty() ? expected->path : root + "/" + expected->path;
    const ZipEntry* entry = zip.Find(name);
    const bool found = entry != nullptr && !entry->is_directory;
    if (found) {
      ++present;
    }
    lines.push_back(S(found ? "found    : " : "missing  : ", expected->path,
                      found ? S("  ", entry->uncompressed_size) : ""));
  }
  context.summary.Number("required", required);
  context.summary.Number("present", present);
  context.summary.Flag("complete", required > 0 && present == required);
  context.summary.Text("description", S("archive ", Display(path), " (", zip.entries().size(),
                                        " entries)"));
  WriteLines(PathFromUtf8(context.Get("details")), lines);
  if (present != required) {
    return Fail(&context, S(Display(path), " does not look like the Ultimate release: ",
                            required - present, " file(s) missing"));
  }
  return Succeed(&context, S("archive holds the Ultimate archives (", zip.entries().size(),
                             " entries)"));
}

// --- the update channel ---------------------------------------------------

// Fetches update.toml - the release manifest Controller Settings's update check reads and
// the updater installs from - and reports what it says. The file is kept at
// --dest so the updater can hand the same bytes to its stages, and the values are
// reported even when the manifest turns out to be unusable, because the wizard
// still has something to tell the user.
int CommandFetchUpdate(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"url", "dest"}), &context)) {
    return kUsage;
  }
  const std::string url = context.Get("url");
  if (url.empty()) {
    return Fail(&context,
                "fetch-update needs --url: this build has no update channel pinned (see "
                "[update] manifest_url in installer/config/pins.toml)");
  }
  const fs::path dest = PathFromUtf8(context.Get("dest"));
  if (dest.empty()) {
    return Fail(&context, "fetch-update needs --dest");
  }
  if (!EnsureParentDirectory(dest, &context.error)) {
    return Fail(&context, context.error);
  }

  DownloadRequest request;
  request.url = url;
  request.destination = dest;
  DownloadResult download;
  std::string error;
  if (!DownloadToFile(request, &download, &error)) {
    return Fail(&context, error);
  }

  std::string text;
  if (!ReadFileText(dest, &text, &error)) {
    return Fail(&context, error);
  }
  UpdateManifest manifest;
  if (!ParseUpdateManifest(text, &manifest, &error)) {
    return Fail(&context, error);
  }

  // Written before the two checks below, so a caller that has to give up still has
  // the version to name.
  context.summary.Number("schema_version", manifest.schema_version);
  context.summary.Text("version", manifest.version);
  context.summary.Flag("requires_game_data", manifest.requires_game_data);
  context.summary.Text("payload_version", manifest.payload_version);
  context.summary.Text("payload_url", manifest.payload_url);
  context.summary.Text("payload_sha256", manifest.payload_sha256);
  context.summary.Number("payload_size", manifest.payload_size);
  context.summary.Text("payload_commit", manifest.payload_commit);
  context.summary.Number("bytes", download.size);

  if (!manifest.HasPayload()) {
    // The download and the parse both succeeded, so this is not a failure of the
    // command; it is a release whose payload travels inside its setup executable,
    // which an updater cannot install from. The user is told to fetch that file.
    return Fail(&context, S("release ", manifest.version,
                            " was built without a published payload: download its setup "
                            "executable from the release page instead"));
  }
  return Succeed(&context, S("the newest release is ", manifest.version, " (",
                             HumanBytes(manifest.payload_size), " of payload)"));
}

// --- the newest build of a channel ----------------------------------------

// What both bootstrap setup executables are built on (README.md, section "The
// three products"): they carry no payload, so this command is asked which build to
// install and where its one file is, and the wizard then runs that file.
//
// `--channel` is the only difference between the two bootstraps:
//
//   latest    the newest release (`GET /releases/latest`, which GitHub resolves
//             to the newest published release that is not a prerelease)
//   nightly   the newest build of main (`GET /releases`, taking the newest entry
//             marked as a prerelease)
//
// One command rather than two steps, because "what is the newest build" and
// "where is the file" are one question with one answer: the release resource
// reports the tag and its asset together, and a wizard that could ask for the tag
// without the URL would have the two out of step.
//
// The file is downloaded with the size and the digest the resource reports, so a
// truncated transfer and a file that is not the published one are both refused
// here - before the wizard runs anything. A release whose asset carries no
// digest is still installed, on the size check alone, and says so in the summary:
// the bytes travel over https from GitHub either way, and refusing the install
// would be worse than doing it and naming what was verified.
// The newest commit of the default branch, or empty with a reason. Only the nightly
// channel asks: "the newest build of main" and "the newest commit of main" are two
// different answers the moment a commit is pushed, and a nightly build should be able
// to say which of the two the user is about to install.
std::string FetchMainCommit(const fs::path& dest, std::string* error) {
  const std::string url = NewestMainCommitApiUrl(EmbeddedPins().installer.homepage_url);
  if (url.empty()) {
    *error = "[installer] homepage_url is not a github.com repository URL";
    return {};
  }
  const fs::path file = dest / "main-commit.json";
  if (!EnsureParentDirectory(file, error)) {
    return {};
  }
  DownloadRequest request;
  request.url = url;
  request.destination = file;
  request.accept = "application/vnd.github+json";
  DownloadResult download;
  if (!DownloadToFile(request, &download, error)) {
    return {};
  }
  std::string json;
  if (!ReadFileText(file, &json, error)) {
    return {};
  }
  std::string sha;
  if (!ParseMainCommitSha(json, &sha, error)) {
    return {};
  }
  return sha;
}

int CommandResolveBuild(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "channel", "api"}), &context)) {
    return kUsage;
  }
  const fs::path dest = PathFromUtf8(context.Get("dest"));
  if (dest.empty()) {
    return Fail(&context, "resolve-build needs --dest: the directory to download into");
  }
  ReleaseChannel channel = ReleaseChannel::kRelease;
  const std::string channel_text = context.Get("channel");
  if (!channel_text.empty() && !ParseReleaseChannel(channel_text, &channel)) {
    return Fail(&context, S("resolve-build does not know the channel '", channel_text,
                            "': 'latest' installs the newest release and 'nightly' installs the "
                            "newest build of main"));
  }
  const char* const channel_name = ReleaseChannelName(channel);

  std::string api = context.Get("api");
  if (api.empty()) {
    const std::string homepage = EmbeddedPins().installer.homepage_url;
    api = ReleaseApiUrlFor(channel, homepage);
    if (api.empty()) {
      return Fail(&context,
                  S("resolve-build has no release API to ask: [installer] homepage_url is '",
                    homepage, "', which is not a github.com repository URL "
                    "(installer/config/pins.toml)"));
    }
  }

  const fs::path listing = dest / "release.json";
  if (!EnsureParentDirectory(listing, &context.error)) {
    return Fail(&context, context.error);
  }

  DownloadRequest listing_request;
  listing_request.url = api;
  listing_request.destination = listing;
  // GitHub's API refuses a request with no User-Agent, and the helper's default
  // is one, so nothing has to be added here - but the Accept value is what picks
  // this shape of answer over an older one.
  listing_request.accept = "application/vnd.github+json";
  DownloadResult listing_download;
  std::string error;
  if (!DownloadToFile(listing_request, &listing_download, &error)) {
    return Fail(&context, S("could not ask ", api, ": ", error));
  }

  std::string json;
  if (!ReadFileText(listing, &json, &error)) {
    return Fail(&context, error);
  }
  LatestRelease release;
  const bool read = channel == ReleaseChannel::kNightly ? ParseNewestNightlyJson(json, &release, &error)
                                                       : ParseLatestReleaseJson(json, &release, &error);
  if (!read) {
    return Fail(&context, S("could not read what ", api, " answered: ", error));
  }

  // A nightly handed to the `latest` bootstrap is refused here rather than
  // installed: `latest` means the newest release, and installing a build of main
  // for somebody who asked for a release is the one mistake this channel exists to
  // prevent. (GitHub's own endpoint excludes prereleases, so this can only happen
  // through an --api override - which is exactly what a test does.)
  if (channel == ReleaseChannel::kRelease && release.prerelease) {
    return Fail(&context, S("release ", release.tag_name,
                            " is marked as a prerelease, so it is a build of main rather than a "
                            "release: the nightly channel is the one that installs these"));
  }

  // Which commit this is, and whether it is main's tip. The commit comes out of the
  // release's own tag or file name - a convention the continuous build owns, checked
  // by CommitMarkerFromName - and the tip comes from main itself. A failure to ask
  // main is not a failure to install: the build is still the newest one published,
  // and every sentence below stays true without the comparison.
  std::string release_commit;
  std::string head_commit;
  bool at_tip = false;
  if (channel == ReleaseChannel::kNightly) {
    release_commit = CommitMarkerFromName(release.tag_name);
    if (release_commit.empty()) {
      release_commit = CommitMarkerFromName(release.asset_name);
    }
    std::string head_error;
    head_commit = FetchMainCommit(dest, &head_error);
    if (head_commit.empty()) {
      log::Warn(S("could not ask main for its newest commit: ", head_error,
                  " - the newest published nightly is installed either way, but this report "
                  "cannot say whether it is main's tip"));
    }
    if (!release_commit.empty() && !head_commit.empty()) {
      // A short commit is a prefix of the long one, so a prefix comparison is what
      // "this is that commit" means.
      at_tip = head_commit.size() >= release_commit.size() &&
               head_commit.compare(0, release_commit.size(), release_commit) == 0;
    }
  }

  // Everything the caller may want to report is written before the download, so
  // a failed transfer still names the release it was for.
  context.summary.Text("channel", channel_name);
  context.summary.Text("tag_name", release.tag_name);
  context.summary.Flag("prerelease", release.prerelease);
  context.summary.Number("asset_count", static_cast<std::uint64_t>(release.asset_count));
  context.summary.Text("asset_name", release.asset_name);
  context.summary.Text("asset_url", release.asset_url);
  context.summary.Number("asset_size", release.asset_size);
  context.summary.Text("asset_sha256", release.asset_sha256);
  context.summary.Flag("digest_published", release.digest_published);
  if (channel == ReleaseChannel::kNightly) {
    context.summary.Text("commit", release_commit);
    context.summary.Text("head_commit", head_commit);
    context.summary.Flag("at_tip", at_tip);
  }

  if (release.asset_count != 1) {
    // The release model is one file per release: the setup executable the user
    // downloads, and nothing else. A release with several files is one this
    // downloader cannot choose from without guessing.
    return Fail(&context, S("release ", release.tag_name, " carries ", release.asset_count,
                            " files; this downloader installs a release with exactly one"));
  }

  const fs::path asset_path = dest / PathFromUtf8(release.asset_name).filename();
  DownloadRequest request;
  request.url = release.asset_url;
  request.destination = asset_path;
  request.expected_size = release.asset_size;
  request.expected_sha256 = release.asset_sha256;
  request.user_agent = "rb_blitz_setup_helper/1.0";
  DownloadResult download;
  if (!DownloadToFile(request, &download, &error)) {
    return Fail(&context, S("could not fetch ", release.asset_name, ": ", error));
  }
  context.summary.Number("bytes", download.size);
  context.summary.Text("sha256", download.sha256);
  context.summary.Text("path", Display(asset_path));

  if (!release.digest_published) {
    log::Warn(S("release ", release.tag_name,
                " published no digest for ", release.asset_name,
                ": the download was checked against its published size alone"));
  }

  std::string sentence = S("the newest ", channel_name, " build is ", release.tag_name, ": ",
                           release.asset_name, " (", HumanBytes(release.asset_size),
                           release.digest_published ? ", sha256 verified)" : ", size verified)");
  if (!release_commit.empty()) {
    sentence += ", commit ";
    sentence += release_commit;
    if (!head_commit.empty()) {
      // The two answers a nightly user actually wants: which commit did I get, and is
      // it the newest one main has.
      sentence += at_tip ? ", which is main's tip"
                         : S(", and main's tip is ", head_commit.substr(0, 7));
    }
  } else if (channel == ReleaseChannel::kNightly) {
    sentence += ", and its tag names no commit";
  }
  return Succeed(&context, sentence);
}

// --- payload --------------------------------------------------------------

int CommandInstallPayload(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args,
             Keys({"dest", "from-pinned", "from-zip", "from-dir", "from-url", "sha256", "size",
                   "version"}),
             &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "install-payload needs --dest");
  }

  const Pins& pins = EmbeddedPins();
  SourceRef source;
  std::string error;
  if (!ResolveSource(context, "recompiled build", PinOfPayload(pins.payload), true, &source,
                     &error)) {
    return Fail(&context, error);
  }
  log::Info(S("recompiled build: ", DescribeSource(source)));

  ComponentResult component;
  const ProgressSink progress{PathFromUtf8(context.Get("progress"))};
  if (!InstallPayload(source, PathFromUtf8(dest), pins, progress, &component, &error)) {
    return Fail(&context, error);
  }
  AddComponentSummary(&context.summary, component);
  return Succeed(&context, S("installed the recompiled build from ", component.source));
}

int CommandVerifyPayload(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "details"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "verify-payload needs --dest");
  }

  ComponentResult component;
  std::string error;
  if (!VerifyPayload(PathFromUtf8(dest), EmbeddedPins(), &component, &error)) {
    return Fail(&context, error);
  }
  AddComponentSummary(&context.summary, component);
  return Succeed(&context, S("verified ", component.files, " files (",
                             HumanBytes(component.bytes), ")"));
}

// --- game data ------------------------------------------------------------

int CommandImportGame(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "package", "folder", "details"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  const std::string package = context.Get("package");
  const std::string folder = context.Get("folder");
  if (dest.empty()) {
    return Fail(&context, "import-game needs --dest");
  }
  if (package.empty() == folder.empty()) {
    return Fail(&context, "import-game needs exactly one of --package or --folder");
  }

  std::string error;
  GameSourcePlan plan;
  const bool planned = !package.empty() ? PlanGamePackage(PathFromUtf8(package), &plan, &error)
                                        : PlanGameFolder(PathFromUtf8(folder), &plan, &error);
  if (!planned) {
    return Fail(&context, error);
  }
  AddPlanSummary(&context.summary, plan);
  WritePlanDetails(PathFromUtf8(context.Get("details")), plan);
  if (!plan.Complete()) {
    return Fail(&context, S("the source is missing ", plan.Missing().size(),
                            " mandatory file(s): ", Join(plan.Missing(), ", ")));
  }

  const fs::path game_root = GameRoot(PathFromUtf8(dest));
  const ProgressSink progress{PathFromUtf8(context.Get("progress"))};
  if (!ImportGame(plan, game_root, progress, &error)) {
    return Fail(&context, error);
  }
  context.summary.Flag("imported", true);
  context.summary.Text("game_dir", Display(game_root));
  return Succeed(&context, S("imported the game data into ", Display(game_root)));
}

int CommandInstallUltimate(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args,
             Keys({"dest", "from-pinned", "from-zip", "from-dir", "from-url", "sha256", "size",
                   "version"}),
             &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "install-ultimate needs --dest");
  }

  const Pins& pins = EmbeddedPins();
  SourceRef source;
  std::string error;
  if (!ResolveSource(context, "Ultimate mod", PinOfUltimate(pins.ultimate), false, &source,
                     &error)) {
    return Fail(&context, error);
  }
  log::Info(S("Ultimate ", source.version, ": ", DescribeSource(source)));

  ComponentResult component;
  const ProgressSink progress{PathFromUtf8(context.Get("progress"))};
  if (!InstallUltimate(source, GameRoot(PathFromUtf8(dest)), pins, progress, &component,
                       &error)) {
    return Fail(&context, error);
  }
  context.summary.Text("location", Display(GameRoot(PathFromUtf8(dest)) / "ultimate"));
  AddComponentSummary(&context.summary, component);
  return Succeed(&context, S("installed the Ultimate mod from ", component.source));
}

int CommandVerifyGame(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "details"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "verify-game needs --dest");
  }

  const fs::path game_root = GameRoot(PathFromUtf8(dest));
  std::string evidence;
  std::string error;
  const bool matched = VerifyGameTree(game_root, &evidence, &error);
  const std::vector<std::string> lines = SplitLines(evidence);
  WriteLines(PathFromUtf8(context.Get("details")), lines);
  context.summary.Text("game_dir", Display(game_root));
  context.summary.Text("evidence", Join(lines, " | "));
  if (!matched) {
    return Fail(&context, error);
  }
  return Succeed(&context, S("the game data in ", Display(game_root), " matches the supported dump"));
}

// --- the two folders the game reads from (parent plan §9.1) ----------------

// The folder page's prefill, and nothing else: it reads `<install>/paths.toml` and,
// when the wizard asked for it, the profile the user already has. Both are
// read-only, so a page the user cancels costs nothing. The wizard resolves the same
// order the plan names - unattended parameter, this file, the legacy profile, the
// default - by asking for the parts and choosing between them itself.
int CommandReadPaths(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"install-dir", "legacy-appdir"}), &context)) {
    return kUsage;
  }
  const std::string install_dir = context.Get("install-dir");
  if (install_dir.empty()) {
    return Fail(&context, "read-paths needs --install-dir");
  }

  const fs::path app_dir = PathFromUtf8(install_dir);
  const fs::path game_dir = GameRoot(app_dir);
  const PathFileState file = ReadPathFile(app_dir, game_dir);
  context.summary.Text("install_dir", Display(app_dir));
  context.summary.Text("file", Display(file.file));
  context.summary.Flag("has_file", file.has_file);
  context.summary.Flag("file_valid", file.valid);
  context.summary.Number("schema_version", static_cast<std::uint64_t>(file.schema_version));
  context.summary.Text("dlc_root", file.dlc_raw);
  context.summary.Text("save_root", file.save_raw);
  context.summary.Text("dlc_effective", Display(file.dlc_dir));
  context.summary.Text("save_effective", Display(file.save_dir));
  if (!file.error.empty()) {
    context.summary.Text("file_error", file.error);
  }

  // The legacy profile is only consulted when the wizard says which installation to
  // resolve it beside. An install running elevated leaves this out on purpose: an
  // administrator's own profile is not the player's (parent plan §9.1).
  if (context.Has("legacy-appdir")) {
    const fs::path exe_dir = PathFromUtf8(context.Get("legacy-appdir"));
    const LegacyPaths legacy = ReadLegacyPaths(exe_dir, profile::RoamingAppDataDir());
    context.summary.Flag("legacy_checked", true);
    context.summary.Text("legacy_profile", Display(legacy.profile));
    context.summary.Flag("legacy_found", legacy.has_file);
    context.summary.Text("legacy_dlc", legacy.dlc_dir);
    context.summary.Text("legacy_save", legacy.save_dir);
    if (!legacy.error.empty()) {
      context.summary.Text("legacy_error", legacy.error);
    }
  }
  return Succeed(&context, S("read the locations for ", Display(app_dir)));
}

// Checks the three answers before the install starts, on the folder page and again
// from PrepareToInstall so a silent install - which never sees the page - is checked
// the same way. Refusing here is the point: a save folder inside the read-only game
// data tree would silently lose every save.
int CommandValidatePaths(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"install-dir", "dlc-dir", "save-dir", "no-touch"}), &context)) {
    return kUsage;
  }
  const std::string install_dir = context.Get("install-dir");
  if (install_dir.empty()) {
    return Fail(&context, "validate-paths needs --install-dir");
  }

  const PathChoice dlc{context.Has("dlc-dir"), context.Get("dlc-dir")};
  const PathChoice save{context.Has("save-dir"), context.Get("save-dir")};
  // `--no-touch` is what the folder page uses: it reports exactly the same things
  // but creates no folder and writes no probe, because merely showing a page must
  // not change the machine. The check that runs when the user moves on creates and
  // probes the save folder for real.
  PathValidationOptions options;
  options.create_save_folder = !context.Has("no-touch");
  const PathReport report = ValidatePaths(PathFromUtf8(install_dir), dlc, save, options);

  context.summary.Text("install_dir", Display(report.install_dir));
  context.summary.Text("game_dir", Display(report.game_dir));
  context.summary.Text("dlc_dir", Display(report.dlc_dir));
  context.summary.Flag("dlc_default", report.dlc_default);
  context.summary.Text("dlc_layout", report.dlc_layout);
  context.summary.Number("dlc_packages", report.dlc_packages);
  context.summary.Flag("dlc_packages_exact", report.dlc_packages_exact);
  context.summary.Number("dlc_rejected", report.dlc_rejected);
  context.summary.Flag("save_default", report.save_default);
  context.summary.Text("save_dir", Display(report.save_dir));
  context.summary.Flag("save_inside_game", report.save_inside_game);
  context.summary.Flag("save_writable", report.save_writable);
  context.summary.Flag("save_created", report.save_created);
  context.summary.Number("note_count", report.notes.size());
  for (std::size_t index = 0; index < report.notes.size(); ++index) {
    context.summary.Text(S("note_", index), report.notes[index]);
  }

  if (!report.ok) {
    return Fail(&context, report.error);
  }
  return Succeed(&context, S("DLC ", report.dlc_layout, " (", report.dlc_packages,
                             " package(s)), saves on ",
                             report.save_default ? std::string("the game's own folder")
                                                  : Display(report.save_dir)));
}

// Commits the answers to `<install>/paths.toml`. Runs last, after the install and
// its manifest have succeeded, so a cancelled or failed install leaves the previous
// configuration exactly as it was.
int CommandWritePaths(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "dlc-dir", "save-dir"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "write-paths needs --dest");
  }

  const PathChoice dlc{context.Has("dlc-dir"), context.Get("dlc-dir")};
  const PathChoice save{context.Has("save-dir"), context.Get("save-dir")};
  const PathWriteResult result = WritePathFile(PathFromUtf8(dest), dlc, save);

  context.summary.Text("file", Display(result.file));
  context.summary.Flag("written", result.written);
  context.summary.Flag("changed", result.changed);
  context.summary.Text("dlc_root", result.dlc_raw);
  context.summary.Text("save_root", result.save_raw);
  if (!result.error.empty()) {
    return Fail(&context, result.error);
  }
  if (!dlc.present && !save.present) {
    return Succeed(&context, "no folders were answered; the file was left as it is");
  }
  // The module logs what it did with the file; there is nothing to add here.
  return Succeed(&context);
}

// --- finishing ------------------------------------------------------------

// Rebuilds the whole picture from what is actually on disk rather than from what
// the wizard remembers doing, so the manifest cannot describe an install that is
// not there.
int CommandFinalize(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args,
             Keys({"dest", "game-source", "payload-source", "payload-version", "ultimate",
                   "ultimate-source", "ultimate-version", "installer-version",
                   "payload-commit", "dlc-dir", "save-dir"}),
             &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "finalize needs --dest");
  }

  const Pins& pins = EmbeddedPins();
  InstallSummary install;
  install.install_dir = PathFromUtf8(dest);
  install.game_dir = GameRoot(install.install_dir);
  install.installer_version = context.Get("installer-version", kHelperVersion);
  // The pinned commit is the build this *installer* was built from; an updater
  // records the commit of the build it installed instead, which the release
  // manifest carries.
  install.payload_commit = pins.payload.commit;
  Override(&install.payload_commit, context.options, "payload-commit");

  std::string error;
  if (!VerifyPayload(install.install_dir, pins, &install.payload, &error)) {
    return Fail(&context, error);
  }
  Override(&install.payload.source, context.options, "payload-source");
  Override(&install.payload.version, context.options, "payload-version");

  if (!VerifyGameTree(install.game_dir, &install.game_evidence, &error)) {
    return Fail(&context, error);
  }
  install.game_source = context.Get("game-source", "imported game data");

  // The folder answers, recorded in the manifest and the report (parent plan §9.2).
  // `written the file` is not this command's job - write-paths runs after it - so
  // what lands here is what the user chose, which is what a bug report needs.
  install.paths_answered = context.Has("dlc-dir") || context.Has("save-dir");
  install.dlc_choice = context.Get("dlc-dir");
  install.save_choice = context.Get("save-dir");

  install.ultimate_installed = context.GetBool("ultimate", true) &&
                               DirectoryExists(install.game_dir / PathOf(kUltimateDirName));
  if (install.ultimate_installed) {
    const fs::path ultimate_root = install.game_dir / PathOf(kUltimateDirName);
    TreeTotals(ultimate_root, &install.ultimate.files, &install.ultimate.bytes);
    install.ultimate.version = context.Get("ultimate-version", pins.ultimate.version);
    install.ultimate.source = context.Get("ultimate-source", S("folder ", Display(ultimate_root)));    Override(&install.ultimate.version, context.options, "ultimate-version");
    Override(&install.ultimate.source, context.options, "ultimate-source");

    // The overlay is hashed here as well as when it was installed, so the
    // manifest records what is on disk now, not what was supposed to land.
    fingerprint::GameFingerprint print;
    std::vector<fingerprint::FileCheck> checks;
    std::vector<std::string> roles;
    roles.reserve(std::size(kUltimateRoles));
    for (const std::string_view role : kUltimateRoles) {
      roles.emplace_back(role);
    }
    if (fingerprint::ParseGameFingerprint(EmbeddedUltimateFingerprints(), &print, &error) &&
        fingerprint::VerifyRoles(print, ultimate_root, roles, &checks, &error)) {
      std::vector<std::string> lines;
      for (const fingerprint::FileCheck& check : checks) {
        lines.push_back(fingerprint::FormatCheckLine(check));
      }
      if (!fingerprint::AllMatched(checks)) {
        install.ultimate.fingerprints_matched = false;
        install.ultimate.notes.push_back(S("the installed overlay does not match Rock Band Blitz "
                                          "Ultimate ",
                                          pins.ultimate.version, ":\n", Join(lines, "\n")));
      }
    } else {
      install.ultimate.notes.push_back(S("the overlay could not be verified: ", error));
    }
  }

  if (!FinalizeInstall(install, &error)) {
    return Fail(&context, error);
  }
  context.summary.Text("manifest", Display(install.install_dir / PathOf(kInstallManifestName)));
  context.summary.Text("report", Display(install.install_dir / PathOf(kInstallReportName)));
  context.summary.Text("game_dir", Display(install.game_dir));
  context.summary.Flag("ultimate_installed", install.ultimate_installed);
  return Succeed(&context, "wrote the install manifest and report");
}

int CommandUninstallCleanup(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({"dest", "keep-game-data"}), &context)) {
    return kUsage;
  }
  const std::string dest = context.Get("dest");
  if (dest.empty()) {
    return Fail(&context, "uninstall-cleanup needs --dest");
  }
  const bool keep = context.GetBool("keep-game-data");

  std::vector<std::string> removed;
  std::string error;
  if (!UninstallCleanup(PathFromUtf8(dest), keep, &removed, &error)) {
    return Fail(&context, error);
  }
  context.summary.Flag("game_data_kept", keep);
  context.summary.Number("removed", removed.size());
  context.summary.Text("removed_items", Join(removed, ", "));
  return Succeed(&context, S("removed ", removed.size(), " item(s)"));
}

int CommandVersion(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args, Keys({}), &context)) {
    return kUsage;
  }
  const Pins& pins = EmbeddedPins();
  context.summary.Text("helper", kHelperVersion);
  context.summary.Text("installer", pins.installer.version);
  context.summary.Text("payload", pins.payload.version);
  context.summary.Text("payload_commit", pins.payload.commit);
  context.summary.Flag("payload_download_pinned", pins.payload.HasDownload());
  context.summary.Text("ultimate", pins.ultimate.version);
  context.summary.Text("ultimate_url", pins.ultimate.url);
  // The update channel an older build checked, and which release asked for the game files
  // again. Nothing reads it any more (the check was retired on 2026-10-15) - it is reported so
  // a bug report can name what a dormant updater executable would look at.
  context.summary.Text("update_manifest_url", pins.update.manifest_url);
  context.summary.Flag("update_requires_game_data", pins.update.requires_game_data);
  // Where the two bootstrap setup executables look for the build they install,
  // derived from the same homepage pin: the newest release for `latest`, and the
  // newest prerelease for `nightly`. Empty means this build's homepage is not a
  // github.com repository, which is the one configuration a bootstrap cannot work
  // in.
  context.summary.Text("release_api", LatestReleaseApiUrl(pins.installer.homepage_url));
  context.summary.Text("nightly_api", NewestNightlyApiUrl(pins.installer.homepage_url));
  context.summary.Text("game_fingerprints", "embedded");
  std::string line = S("helper ", kHelperVersion, ", installer ", pins.installer.version,
                       ", payload ", pins.payload.version);
  if (!pins.payload.commit.empty()) {
    line += S(", commit ", pins.payload.commit.substr(0, 12));
  }
  return Succeed(&context, S(line, ", Ultimate ", pins.ultimate.version));
}

// --- dispatch -------------------------------------------------------------

struct CommandSpec {
  std::string_view name;
  int (*run)(const std::vector<std::string>& args);
  std::string_view usage;
  std::string_view summary;
};

constexpr CommandSpec kCommands[] = {
    {"check-space", CommandCheckSpace, "--dest <dir> [--required-bytes <n>]",
     "ok free total required enough path"},
    {"probe-folder", CommandProbeFolder, "--folder <dir> [--details <file>]",
     "kind location description files required present bytes complete missing"},
    {"probe-package", CommandProbePackage, "--package <file> [--details <file>]",
     "kind location description files required present bytes complete missing title_id entries"},
    {"probe-archive", CommandProbeArchive,
     "--archive <file> --kind payload|ultimate [--details <file>]",
     "kind entries sha256 root has_manifest required present complete description"},
    {"fetch-update", CommandFetchUpdate, "--url <url> --dest <file>",
     "schema_version version requires_game_data payload_version payload_url payload_sha256 "
     "payload_size payload_commit bytes"},
    {"resolve-build", CommandResolveBuild, "--dest <dir> [--channel latest|nightly] [--api <url>]",
     "channel tag_name prerelease asset_count asset_name asset_url asset_size asset_sha256 "
     "digest_published bytes sha256 path"},
    {"install-payload", CommandInstallPayload,
     "--dest <dir> [--from-pinned|--from-zip <p>|--from-dir <d>|--from-url <u>] [--sha256 <h>] "
     "[--size <n>] [--version <v>] [--progress <file>]",
     "source version files bytes verified warning_count"},
    {"verify-payload", CommandVerifyPayload, "--dest <dir>",
     "source version files bytes verified warning_count"},
    {"import-game", CommandImportGame,
     "--dest <dir> (--package <file>|--folder <dir>) [--details <file>] [--progress <file>]",
     "kind location description files required present bytes complete missing imported game_dir"},
    {"install-ultimate", CommandInstallUltimate,
     "--dest <dir> (--from-pinned|--from-zip <p>|--from-dir <d>|--from-url <u>) [--sha256 <h>] "
     "[--size <n>] [--version <v>] [--progress <file>]",
     "source version files bytes verified warning_count location"},
    {"verify-game", CommandVerifyGame, "--dest <dir> [--details <file>]",
     "game_dir evidence"},
    {"read-paths", CommandReadPaths, "--install-dir <dir> [--legacy-appdir <dir>]",
     "install_dir file has_file file_valid schema_version dlc_root save_root dlc_effective "
     "save_effective file_error legacy_checked legacy_profile legacy_found legacy_dlc "
     "legacy_save legacy_error"},
    {"validate-paths", CommandValidatePaths,
     "--install-dir <dir> [--dlc-dir <dir>] [--save-dir <dir>] [--no-touch]",
     "install_dir game_dir dlc_dir dlc_default dlc_layout dlc_packages dlc_packages_exact "
     "dlc_rejected save_default save_dir save_inside_game save_writable save_created "
     "note_count note_<n>"},
    {"write-paths", CommandWritePaths, "--dest <dir> [--dlc-dir <dir>] [--save-dir <dir>]",
     "file written changed dlc_root save_root"},
    {"finalize", CommandFinalize,
     "--dest <dir> [--game-source <text>] [--payload-source <text>] [--payload-version <v>] "
     "[--payload-commit <id>] [--ultimate 0|1] [--ultimate-source <text>] [--ultimate-version <v>] "
     "[--installer-version <v>] [--dlc-dir <dir>] [--save-dir <dir>]",
     "manifest report game_dir ultimate_installed"},
    {"uninstall-cleanup", CommandUninstallCleanup, "--dest <dir> [--keep-game-data]",
     "game_data_kept removed removed_items"},
    {"version", CommandVersion, "", "helper installer payload payload_commit "
                                    "payload_download_pinned ultimate ultimate_url "
                                    "update_manifest_url update_requires_game_data "
                                    "release_api nightly_api game_fingerprints"},
};

}  // namespace

std::string UsageText() {
  std::vector<std::string> lines;
  lines.push_back("rb_blitz_setup_helper - the work the Rock Band Blitz installer cannot do in");
  lines.push_back("Inno Setup Pascal. Every command accepts `--summary <file>` (one");
  lines.push_back("`key=value;key=value` line, `ok=1` or `ok=0`), `--log <file>` and");
  lines.push_back("`--progress <file>` (two lines: percent, then the current step); the keys are");
  lines.push_back("written by the commands that have something to report, and a rejected command");
  lines.push_back("line still reports itself through `--summary`.");
  lines.push_back("");
  lines.push_back("usage: rb_blitz_setup_helper <command> [options]");
  lines.push_back("");
  for (const CommandSpec& command : kCommands) {
    lines.push_back(S("  ", command.name, ' ', command.usage));
    if (!command.summary.empty()) {
      lines.push_back(S("      summary: ", command.summary));
    }
  }
  lines.push_back("");
  lines.push_back("exit codes: 0 success, 1 failed (see the summary), 2 bad command line");
  return Join(lines, "\r\n") + "\r\n";
}

void ReportUnhandledFailure(const std::vector<std::string>& args, std::string_view reason) {
  Summary summary;
  summary.file = SummaryPathFromRawArgs(args);
  if (summary.file.empty()) {
    return;
  }
  summary.Flag("ok", false);
  summary.Text("error", reason);
  summary.Write();
}

int RunCommand(const std::vector<std::string>& args) {
  try {
    const auto wants_help = [](const std::vector<std::string>& in) {
      return std::any_of(in.begin(), in.end(), [](const std::string& arg) {
        return arg == "--help" || arg == "-h" || arg == "/?";
      });
    };
    if (args.empty() || args.front() == "help") {
      std::fputs(UsageText().c_str(), stdout);
      return args.empty() ? kUsage : kOk;
    }
    if (wants_help(args)) {
      std::fputs(UsageText().c_str(), stdout);
      return kOk;
    }

    const std::string_view name = args.front();
    for (const CommandSpec& command : kCommands) {
      if (name == command.name) {
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        return command.run(rest);
      }
    }
    std::fprintf(stderr, "error: unknown command '%.*s'\n\n%s", static_cast<int>(name.size()),
                 name.data(), UsageText().c_str());
    return kUsage;
  } catch (const std::exception& exception) {
    const std::string reason = S("unhandled failure: ", exception.what());
    log::Error(reason);
    ReportUnhandledFailure(args, reason);
    std::fprintf(stderr, "error: %s\n", exception.what());
    return kFailed;
  }
}

}  // namespace rb_blitz::installer
