// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// P3 host tests for the store (src/settings/store.{h,cpp}). No SDK, no boot.
//
// What is pinned here is the pair of promises parent plan §4.4 makes: a save patches only the
// keys it owns and leaves comments/unknown keys alone (a no-op renders the identical text), and
// a save re-reads the file, refuses a key another writer changed, and never clobbers a file it
// could not parse.

#include "check.h"

#include "fs/atomic_file.h"
#include "settings/store.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace rb_blitz::settings;
using namespace rb_blitz::test;

namespace fs = std::filesystem;

void CheckString(const char* file, int line, const std::string& actual, const std::string& expected,
                 const char* actual_expr, const char* expected_expr) {
  ++g_checks;
  if (actual == expected) {
    return;
  }
  Fail(file, line, std::string(actual_expr) + " == " + expected_expr +
                      " failed\n         actual   " + actual + "\n         expected " + expected);
}

#define CHECK_STR_EQ(actual, expected) \
  CheckString(__FILE__, __LINE__, (actual), (expected), #actual, #expected)

void CheckContains(const char* file, int line, const std::string& haystack,
                   const std::string& needle, const char* haystack_expr, const char* needle_expr) {
  ++g_checks;
  if (haystack.find(needle) != std::string::npos) {
    return;
  }
  Fail(file, line, std::string(haystack_expr) + " does not contain " + needle_expr +
                      "\n         actual   " + haystack + "\n         expected to contain " +
                      needle);
}

#define CHECK_CONTAINS(haystack, needle) \
  CheckContains(__FILE__, __LINE__, (haystack), (needle), #haystack, #needle)

std::string ReadFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
  std::error_code code;
  fs::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct Scratch {
  fs::path base;
  Scratch() {
    std::error_code code;
    base = fs::temp_directory_path(code) / "rb_blitz-settings-store";
    fs::remove_all(base, code);
    fs::create_directories(base, code);
  }
  ~Scratch() {
    std::error_code code;
    fs::remove_all(base, code);
  }
};

Value BoolV(bool value) {
  Value out;
  out.kind = ValueKind::kBool;
  out.boolean = value;
  return out;
}

Value EnumV(std::string token) {
  Value out;
  out.kind = ValueKind::kEnum;
  out.text = std::move(token);
  return out;
}

Value IntV(int32_t value) {
  Value out;
  out.kind = ValueKind::kInt;
  out.integer = value;
  return out;
}

std::string Sample() {
  return std::string(
      "# Kept by hand; the tool must not eat this.\n"
      "schema_version = 1\n"
      "\n"
      "[general]\n"
      "target = 'common'   # a trailing comment\n"
      "cache_dlc_library = false\n"
      "\n"
      "[unknown_table]\n"
      "mystery = 42\n"
      "\n"
      "[graphics]\n"
      "renderer = 'd3d12'\n"
      "resolution = [1280, 720]\n");
}

void TestDocumentKeepsEverythingElse() {
  BeginCase("document: a no-op patch renders the identical text");
  bool ok = false;
  std::string error;
  toml::Document doc = toml::Document::Parse(Sample(), &ok, &error);
  CHECK_TRUE(ok);
  CHECK_FALSE(doc.dirty());
  doc.Set("general", "target", "'common'");
  CHECK_FALSE(doc.dirty());
  CHECK_TRUE(doc.Render() == Sample());

  BeginCase("document: a changed key keeps its comment and the unknown table");
  doc.Set("general", "target", "'ultimate'");
  CHECK_TRUE(doc.dirty());
  CHECK_TRUE(doc.Render().find("target = 'ultimate'   # a trailing comment") != std::string::npos);
  CHECK_TRUE(doc.Render().find("[unknown_table]\nmystery = 42") != std::string::npos);
  CHECK_TRUE(doc.Render().find("# Kept by hand; the tool must not eat this.") != std::string::npos);

  BeginCase("document: a new key is added without disturbing its neighbours");
  doc.Set("general", "dlc_menu_songs", "true");
  CHECK_TRUE(doc.Render().find("dlc_menu_songs = true") != std::string::npos);
  CHECK_TRUE(doc.Render().find("cache_dlc_library = false") != std::string::npos);
}

void TestDocumentReadsArrays() {
  BeginCase("document: an array and an array-of-strings read back");
  const std::string text =
      "schema_version = 1\n\n[graphics]\nresolution = [1920, 1080]\n\n[interface]\n"
      "hidden_main_menu = ['splash_exit', 'splash_dlc']\n";
  bool ok = false;
  std::string error;
  toml::Document doc = toml::Document::Parse(text, &ok, &error);
  CHECK_TRUE(ok);
  const toml::Item resolution = doc.Get("graphics", "resolution");
  CHECK_TRUE(resolution.found);
  CHECK_FALSE(resolution.scalar);
  CHECK_TRUE(resolution.list_ok);
  CHECK_EQ(resolution.list.size(), 2);
  CHECK_STR_EQ(resolution.list[0], "1920");
  CHECK_STR_EQ(resolution.list[1], "1080");
  const toml::Item hidden = doc.Get("interface", "hidden_main_menu");
  CHECK_TRUE(hidden.list_ok);
  CHECK_EQ(hidden.list.size(), 2);
  CHECK_STR_EQ(hidden.list[0], "splash_exit");
}

void TestLoadDocument() {
  Scratch scratch;

  BeginCase("load: a missing file is not an error");
  const DocumentLoad missing = LoadSettingsDocument(scratch.base / "settings.toml");
  CHECK_EQ(static_cast<int>(missing.status), static_cast<int>(DocumentStatus::kMissing));
  CHECK_TRUE(missing.usable());

  BeginCase("load: a file with no schema_version is malformed and left alone");
  const fs::path bad = scratch.base / "bad.toml";
  WriteFile(bad, "general = 1\n");
  const DocumentLoad malformed = LoadSettingsDocument(bad);
  CHECK_EQ(static_cast<int>(malformed.status), static_cast<int>(DocumentStatus::kMalformed));
  CHECK_FALSE(malformed.usable());
  CHECK_STR_EQ(ReadFile(bad), "general = 1\n");

  BeginCase("load: an unknown schema version is refused, not reinterpreted");
  const fs::path future = scratch.base / "future.toml";
  WriteFile(future, "schema_version = 7\n");
  const DocumentLoad unsupported = LoadSettingsDocument(future);
  CHECK_EQ(static_cast<int>(unsupported.status), static_cast<int>(DocumentStatus::kUnsupportedVersion));
  CHECK_STR_EQ(ReadFile(future), "schema_version = 7\n");

  BeginCase("load: values, provenance and the absent-vs-empty list selection");
  const fs::path good = scratch.base / "good.toml";
  WriteFile(good, std::string("schema_version = 1\n\n[general]\ntarget = 'ultimate'\n\ndlc_scan_threads = "
                              "4\n\n[interface]\nhidden_main_menu = []\n\n[provenance]\n"
                              "'general.target' = 'manual'\n\n[automatic]\npolicy_version = 3\n"));
  const DocumentLoad loaded = LoadSettingsDocument(good);
  CHECK_EQ(static_cast<int>(loaded.status), static_cast<int>(DocumentStatus::kOk));
  CHECK_TRUE(loaded.document.values.count(SettingId::kTarget) == 1);
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kTarget).text, "ultimate");
  CHECK_EQ(loaded.document.values.at(SettingId::kDlcScanThreads).integer, 4);
  // Present [] means "hide nothing", which is a real selection the model must keep.
  CHECK_TRUE(loaded.document.values.at(SettingId::kHiddenMainMenu).kind == ValueKind::kList);
  CHECK_EQ(loaded.document.values.at(SettingId::kHiddenMainMenu).list.size(), 0);
  CHECK_TRUE(loaded.document.origins.at(SettingId::kTarget) == Origin::kManual);
  CHECK_EQ(loaded.document.automatic_policy_version, 3);
}

void TestSaveMergeAndConflict() {
  Scratch scratch;
  const fs::path file = scratch.base / "settings.toml";
  WriteFile(file, Sample());

  BeginCase("save: only owned, changed keys are written");
  SaveRequest request;
  request.base[SettingId::kTarget] = EnumV("common");
  request.changed[SettingId::kTarget] = EnumV("ultimate");
  request.origins[SettingId::kTarget] = Origin::kManual;
  const SaveOutcome saved = SaveSettingsDocument(file, request);
  CHECK_EQ(static_cast<int>(saved.status), static_cast<int>(CommitStatus::kSaved));
  CHECK_TRUE(saved.wrote);
  const std::string after = ReadFile(file);
  CHECK_TRUE(after.find("target = 'ultimate'") != std::string::npos);
  CHECK_TRUE(after.find("[unknown_table]\nmystery = 42") != std::string::npos);
  CHECK_TRUE(after.find("[provenance]\n'general.target' = 'manual'") != std::string::npos);

  BeginCase("save: a concurrent change to the same key is a conflict and writes nothing");
  const std::string before = ReadFile(file);
  SaveRequest stale;
  stale.base[SettingId::kTarget] = EnumV("common");  // the base the writer loaded
  stale.changed[SettingId::kTarget] = EnumV("demo");
  const SaveOutcome conflicted = SaveSettingsDocument(file, stale);
  CHECK_EQ(static_cast<int>(conflicted.status), static_cast<int>(CommitStatus::kConflict));
  CHECK_EQ(conflicted.conflicts.size(), 1);
  CHECK_TRUE(conflicted.conflicts[0] == SettingId::kTarget);
  CHECK_STR_EQ(ReadFile(file), before);

  BeginCase("save: a no-op does not open the file");
  const fs::file_time_type mtime = fs::last_write_time(file);
  SaveRequest noop;
  noop.base[SettingId::kTarget] = EnumV("ultimate");
  noop.changed[SettingId::kTarget] = EnumV("ultimate");
  const SaveOutcome unchanged = SaveSettingsDocument(file, noop);
  CHECK_EQ(static_cast<int>(unchanged.status), static_cast<int>(CommitStatus::kNoChanges));
  CHECK_FALSE(unchanged.wrote);
  CHECK_TRUE(fs::last_write_time(file) == mtime);
}

void TestFailedWritePreservesBytes() {
  Scratch scratch;
  const fs::path file = scratch.base / "settings.toml";
  WriteFile(file, Sample());
  const std::string original = ReadFile(file);

  BeginCase("save: a malformed destination is refused and keeps its bytes");
  const fs::path broken = scratch.base / "broken.toml";
  WriteFile(broken, "this is not toml\n");
  SaveRequest request;
  request.changed[SettingId::kVsync] = BoolV(false);
  const SaveOutcome refused = SaveSettingsDocument(broken, request);
  CHECK_FALSE(refused.wrote);
  CHECK_TRUE(static_cast<int>(refused.status) == static_cast<int>(CommitStatus::kInvalid));
  CHECK_STR_EQ(ReadFile(broken), "this is not toml\n");

  BeginCase("save: an unwritable destination keeps its bytes, and the failure names the reason");
  std::error_code code;
#ifdef _WIN32
  const auto original_permissions = fs::status(file, code).permissions();
  fs::permissions(file, fs::perms::owner_read, fs::perm_options::replace, code);
#else
  // Atomic replacement needs write permission on the directory, not on the existing file.
  // Create the sidecar first so this case reaches replacement rather than failing to lock.
  {
    rb_blitz::fs::FileLock lock;
    CHECK_TRUE(lock.Acquire(file, nullptr));
  }
  const auto original_permissions = fs::status(scratch.base, code).permissions();
  fs::permissions(scratch.base, fs::perms::owner_read | fs::perms::owner_exec,
                  fs::perm_options::replace, code);
  CHECK_FALSE(static_cast<bool>(code));
  const auto probe = scratch.base / "permission-probe";
  std::ofstream probe_stream(probe);
  const bool permissions_bypassed = probe_stream.is_open();
  probe_stream.close();
  if (permissions_bypassed) {
    fs::permissions(scratch.base, original_permissions, fs::perm_options::replace, code);
    fs::remove(probe, code);
    std::printf("[ SKIP ] process privileges allow writing the protected directory\n");
    CHECK_STR_EQ(ReadFile(file), original);
    return;
  }
#endif
  // The base has to agree with what the file holds or this is a *conflict* - which writes nothing and
  // reports no I/O error at all, so the write path this case means to fail would never be reached.
  // (The earlier shape of this test did exactly that: its base named a key the file does not have.)
  SaveRequest write;
  write.base[SettingId::kTarget] = EnumV("common");
  write.changed[SettingId::kTarget] = EnumV("ultimate");
  const SaveOutcome failed = SaveSettingsDocument(file, write);
  CHECK_FALSE(failed.wrote);
  CHECK_EQ(static_cast<int>(failed.status), static_cast<int>(CommitStatus::kIoError));
  // The failure names the operation, the path and the OS's reason. On Windows replacement of the
  // read-only file fails; POSIX instead refuses to create the temporary file in the directory.
  CHECK_TRUE(!failed.errors.empty());
  if (!failed.errors.empty()) {
    CHECK_CONTAINS(failed.errors.front(), file.filename().string());
    CHECK_CONTAINS(failed.errors.front(), "cannot ");
#ifdef _WIN32
    CHECK_CONTAINS(failed.errors.front(), "unchanged");
#else
    CHECK_CONTAINS(failed.errors.front(), "errno");
#endif
  }
#ifdef _WIN32
  fs::permissions(file, original_permissions, fs::perm_options::replace, code);
#else
  fs::permissions(scratch.base, original_permissions, fs::perm_options::replace, code);
#endif
  CHECK_STR_EQ(ReadFile(file), original);
}

void TestLockErrorsSayWhichFailure() {
  Scratch scratch;
  const fs::path file = scratch.base / "settings.toml";
  WriteFile(file, Sample());  // a regular file, so `<file>/nested` can never exist

  BeginCase("lock: a held lock and an unusable path are told apart, by name and by code");

  // The text this replaced was "cannot lock settings file (another writer or access denied)", which
  // cannot tell a transient competing writer (retry, or report a retryable conflict) from a path the
  // player may not write (stop, and say where). Both messages now name the lock path, the OS reason
  // and the numeric code, so a log line or a regression test can tell them apart.
  std::string contended;
  {
    rb_blitz::fs::FileLock held;
    std::string first_error;
    CHECK_TRUE(held.Acquire(file, &first_error, /*wait=*/true));
    CHECK_TRUE(first_error.empty());  // nothing is written on success

    rb_blitz::fs::FileLock second;
    CHECK_FALSE(second.Acquire(file, &contended, /*wait=*/false));
  }
  CHECK_CONTAINS(contended, "settings.toml.lock");  // the sidecar the lock is taken on
  CHECK_CONTAINS(contended, "cannot lock");
#ifdef _WIN32
  CHECK_CONTAINS(contended, "error");
#else
  CHECK_CONTAINS(contended, "errno");
#endif

  // A path that cannot hold the sidecar at all: `<a regular file>/nested.lock` has no directory.
  const fs::path unusable = file / "nested";
  rb_blitz::fs::FileLock third;
  std::string missing;
  CHECK_FALSE(third.Acquire(unusable, &missing, /*wait=*/false));
  CHECK_CONTAINS(missing, "nested.lock");
  CHECK_TRUE(missing != contended);

#ifdef _WIN32
  // The two reasons, in the words the log will carry.
  CHECK_CONTAINS(contended, "another writer");
  CHECK_TRUE(contended.find("does not exist") == std::string::npos);
  CHECK_CONTAINS(missing, "does not exist");
  CHECK_TRUE(missing.find("another writer") == std::string::npos);
#endif

  // A released lock is a file, not a latch on this process: it can be taken again.
  rb_blitz::fs::FileLock again;
  CHECK_TRUE(again.Acquire(file, nullptr, /*wait=*/true));
}

void TestStoreFacade() {
  Scratch scratch;
  StorePaths paths;
  paths.settings_file = scratch.base / "settings.toml";
  paths.paths_file = scratch.base / "paths.toml";
  paths.legacy_profile = scratch.base / "launcher.toml";

  BeginCase("store: a load establishes the base so a later save is not a false conflict");
  Store store(paths);
  CHECK_EQ(static_cast<int>(store.Load().status), static_cast<int>(DocumentStatus::kMissing));
  store.MarkChanged(SettingId::kVsync, BoolV(false), Origin::kManual);
  const SaveOutcome first = store.Save();
  CHECK_EQ(static_cast<int>(first.status), static_cast<int>(CommitStatus::kSaved));
  store.MarkChanged(SettingId::kDlcMenuSongs, BoolV(true), Origin::kManual);
  const SaveOutcome second = store.Save();
  CHECK_EQ(static_cast<int>(second.status), static_cast<int>(CommitStatus::kSaved));

  BeginCase("store: two disjoint writers both survive (merge, not whole-document clobber)");
  Store writer_a(paths);
  Store writer_b(paths);
  writer_a.Load();
  writer_b.Load();
  writer_a.MarkChanged(SettingId::kDlcScanThreads, IntV(6), Origin::kManual);
  writer_b.MarkChanged(SettingId::kDlcMenuSongs, BoolV(false), Origin::kManual);
  CHECK_EQ(static_cast<int>(writer_a.Save().status), static_cast<int>(CommitStatus::kSaved));
  CHECK_EQ(static_cast<int>(writer_b.Save().status), static_cast<int>(CommitStatus::kSaved));
  const DocumentLoad reloaded = LoadSettingsDocument(paths.settings_file);
  CHECK_EQ(reloaded.document.values.at(SettingId::kDlcScanThreads).integer, 6);
  CHECK_TRUE(reloaded.document.values.at(SettingId::kDlcMenuSongs).boolean == false);
}

void TestResolutionPreferenceMigration() {
  Scratch scratch;
  const auto file = scratch.base / "settings.toml";
  BeginCase("store: split legacy dimensions, inherit provenance, preserve the original text");
  const std::string old = "schema_version = 1\n[graphics]\nresolution = [1920, 1080] # keep\n"
                          "unknown = 'untouched'\n[provenance]\n'graphics.resolution' = 'manual'\n";
  WriteFile(file, old);
  const auto loaded = LoadSettingsDocument(file);
  CHECK_TRUE(loaded.document.graphics_preferences_migrated);
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kAspectRatio).text, "16:9");
  CHECK_STR_EQ(loaded.document.values.at(SettingId::kRenderScale).text, "1x");
  CHECK_TRUE(loaded.document.origins.at(SettingId::kAspectRatio) == Origin::kManual);
  CHECK_STR_EQ(ReadFile(file), old);
  SaveRequest save;
  save.base = loaded.document.values;
  save.changed[SettingId::kAspectRatio] = loaded.document.values.at(SettingId::kAspectRatio);
  save.changed[SettingId::kRenderScale] = loaded.document.values.at(SettingId::kRenderScale);
  CHECK_TRUE(SaveSettingsDocument(file, save).status == CommitStatus::kSaved);
  CHECK_CONTAINS(ReadFile(file), "resolution = [1920, 1080] # keep");
  CHECK_CONTAINS(ReadFile(file), "unknown = 'untouched'");
  CHECK_FALSE(LoadSettingsDocument(file).document.graphics_preferences_migrated);

  BeginCase("store: legacy high resolutions choose a scale without increasing pixel cost");
  for (const auto& entry : {std::array<int32_t, 3>{1280, 720, 1}, {2560, 1440, 2},
                           {2880, 1620, 2}, {3840, 2160, 3}, {3840, 1080, 2}}) {
    Value legacy;
    legacy.kind = ValueKind::kResolution;
    legacy.width = entry[0]; legacy.height = entry[1];
    Value ratio, scale;
    CHECK_TRUE(SplitLegacyResolution(legacy, &ratio, &scale));
    CHECK_STR_EQ(scale.text, std::to_string(entry[2]) + "x");
  }

  BeginCase("store: explicit new preferences win independently, including invalid hand edits");
  WriteFile(file, "schema_version = 1\n[graphics]\nresolution = [2560, 1440]\n"
                  "aspect_ratio = '4:3'\nrender_scale = 'unsupported' # left for the user\n");
  const auto explicit_rows = LoadSettingsDocument(file);
  CHECK_STR_EQ(explicit_rows.document.values.at(SettingId::kAspectRatio).text, "4:3");
  CHECK_STR_EQ(explicit_rows.document.values.at(SettingId::kRenderScale).text, "unsupported");
  CHECK_FALSE(explicit_rows.document.graphics_preferences_migrated);

  BeginCase("store: concurrent edits of the legacy source are conflicts during conversion");
  WriteFile(file, old);
  save.base = LoadSettingsDocument(file).document.values;
  WriteFile(file, "schema_version = 1\n[graphics]\nresolution = [2560, 1440]\n");
  CHECK_TRUE(SaveSettingsDocument(file, save).status == CommitStatus::kConflict);

  BeginCase("store: unknown legacy resolutions survive without guessed scale preferences");
  WriteFile(file, "schema_version = 1\n[graphics]\nresolution = [1234, 777]\n");
  const auto unsupported = LoadSettingsDocument(file);
  CHECK_FALSE(unsupported.document.graphics_preferences_migrated);
  CHECK_FALSE(unsupported.document.values.contains(SettingId::kRenderScale));
  CHECK_EQ(unsupported.document.values.at(SettingId::kResolution).width, 1234);

  BeginCase("store: legacy Auto stays Auto instead of turning into a fixed aspect");
  WriteFile(file, "schema_version = 1\n[graphics]\nresolution = 'auto'\n");
  const auto automatic = LoadSettingsDocument(file);
  CHECK_STR_EQ(automatic.document.values.at(SettingId::kAspectRatio).text, "auto");
  CHECK_STR_EQ(automatic.document.values.at(SettingId::kRenderScale).text, "auto");
}

void TestMotionDeployMigration() {
  BeginCase("store: legacy motion opt-out survives and enabled installs adopt Tilt");
  Scratch scratch;
  const auto file = scratch.base / "settings.toml";
  constexpr auto id = SettingId::kTouchMotionDeploy;
  for (const bool enabled : {false, true}) {
    const std::string original = std::string("schema_version = 1\n[controller]\ntouch_shake_deploy = ") +
        (enabled ? "true" : "false") + " # keep\nunknown = 'untouched'\n";
    WriteFile(file, original);
    const auto loaded = LoadSettingsDocument(file);
    CHECK_TRUE(loaded.status == DocumentStatus::kOk);
    CHECK_STR_EQ(loaded.document.values.at(id).text, enabled ? "tilt" : "none");
    CHECK_STR_EQ(ReadFile(file), original);
    SaveRequest request;
    request.base = loaded.document.values;
    Value shake; shake.kind = ValueKind::kEnum; shake.text = "shake";
    request.changed[id] = shake;
    CHECK_TRUE(SaveSettingsDocument(file, request).status == CommitStatus::kSaved);
    CHECK_STR_EQ(LoadSettingsDocument(file).document.values.at(id).text, "shake");
    CHECK_CONTAINS(ReadFile(file), "unknown = 'untouched'");
    request.base = LoadSettingsDocument(file).document.values;
    request.changed[id] = {};
    CHECK_TRUE(SaveSettingsDocument(file, request).status == CommitStatus::kSaved);
    CHECK_FALSE(LoadSettingsDocument(file).document.values.contains(id));
    CHECK_TRUE(ReadFile(file).find("touch_shake_deploy") == std::string::npos);
  }
  BeginCase("store: explicit motion mode wins and concurrent legacy edits conflict");
  WriteFile(file, "schema_version = 1\n[controller]\ntouch_shake_deploy = false\ntouch_motion_deploy = 'tilt'\n");
  CHECK_STR_EQ(LoadSettingsDocument(file).document.values.at(id).text, "tilt");
  WriteFile(file, "schema_version = 1\n[controller]\ntouch_shake_deploy = false\n");
  SaveRequest request;
  request.base = LoadSettingsDocument(file).document.values;
  request.changed[id] = request.base.at(id);
  WriteFile(file, "schema_version = 1\n[controller]\ntouch_shake_deploy = true\n");
  CHECK_TRUE(SaveSettingsDocument(file, request).status == CommitStatus::kConflict);
}

}  // namespace

int main() {
  TestDocumentKeepsEverythingElse();
  TestDocumentReadsArrays();
  TestLoadDocument();
  TestSaveMergeAndConflict();
  TestFailedWritePreservesBytes();
  TestLockErrorsSayWhichFailure();
  TestStoreFacade();
  TestResolutionPreferenceMigration();
  TestMotionDeployMigration();
  return Finish();
}
