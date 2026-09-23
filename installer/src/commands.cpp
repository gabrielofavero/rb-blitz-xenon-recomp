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
#include "install.h"
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

// --- finishing ------------------------------------------------------------

// Rebuilds the whole picture from what is actually on disk rather than from what
// the wizard remembers doing, so the manifest cannot describe an install that is
// not there.
int CommandFinalize(const std::vector<std::string>& args) {
  CommandContext context;
  if (!Begin(args,
             Keys({"dest", "game-source", "payload-source", "payload-version", "ultimate",
                   "ultimate-source", "ultimate-version", "installer-version"}),
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
  install.payload_commit = pins.payload.commit;

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

  install.ultimate_installed = context.GetBool("ultimate", true) &&
                               DirectoryExists(install.game_dir / PathOf(kUltimateDirName));
  if (install.ultimate_installed) {
    const fs::path ultimate_root = install.game_dir / PathOf(kUltimateDirName);
    TreeTotals(ultimate_root, &install.ultimate.files, &install.ultimate.bytes);
    install.ultimate.version = context.Get("ultimate-version", pins.ultimate.version);
    install.ultimate.source = context.Get("ultimate-source", S("folder ", Display(ultimate_root)));
    Override(&install.ultimate.version, context.options, "ultimate-version");
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
    {"finalize", CommandFinalize,
     "--dest <dir> [--game-source <text>] [--payload-source <text>] [--payload-version <v>] "
     "[--ultimate 0|1] [--ultimate-source <text>] [--ultimate-version <v>] "
     "[--installer-version <v>]",
     "manifest report game_dir ultimate_installed"},
    {"uninstall-cleanup", CommandUninstallCleanup, "--dest <dir> [--keep-game-data]",
     "game_data_kept removed removed_items"},
    {"version", CommandVersion, "", "helper installer payload payload_commit "
                                    "payload_download_pinned ultimate ultimate_url "
                                    "game_fingerprints"},
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
