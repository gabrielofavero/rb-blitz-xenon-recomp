// SPDX-License-Identifier: GPL-2.0-only
// Persistent first-run setup checks using synthetic game bytes and no network/UI.
#include "import_session.h"
#include "util.h"
#include "util/sha256.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace i = rb_blitz::installer::ios;
namespace p = rb_blitz::installer::portable;
namespace u = rb_blitz::installer;
namespace hash = rb_blitz::util;
using rb_blitz::fingerprint::GameFingerprint;

namespace {
int checks = 0;
void Check(bool result, std::string_view message) {
  ++checks;
  if (!result) throw std::runtime_error(std::string(message));
}
void Write(const fs::path& path, std::string_view bytes) {
  std::string error;
  Check(u::WriteFileText(path, bytes, false, &error), error);
}
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
GameFingerprint Fingerprint(std::initializer_list<std::tuple<std::string, std::string, std::string>> files) {
  GameFingerprint result;
  result.schema_version = 1;
  for (const auto& [role, path, bytes] : files)
    result.files.push_back({role, path, bytes.size(), hash::HashHex(bytes)});
  return result;
}
void MakeGame(const fs::path& source) {
  Write(source / "default.xex", "retail entry");
  Write(source / "gen/main_xbox.hdr", "retail header");
  Write(source / "gen/main_xbox_0.ark", "retail archive");
}
void MakeUltimate(const fs::path& source) {
  Write(source / "gen/patch_xbox.hdr", "mod header");
  Write(source / "gen/patch_xbox_0.ark", "mod archive");
  Write(source / "default.xex", "mod executable must be omitted");
  Write(source / "extra notes.txt", "optional data");
}
// A valid stored ZIP with no external compression library or real game content.
std::string MakeZip() {
  std::string zip, directory;
  const auto put = [](std::string& out, std::uint64_t value, unsigned bytes) {
    for (unsigned index = 0; index < bytes; ++index) out.push_back(static_cast<char>(value >> (8 * index)));
  };
  const std::vector<std::pair<std::string, std::string>> files{
      {"fixture/gen/patch_xbox.hdr", "mod header"},
      {"fixture/gen/patch_xbox_0.ark", "mod archive"},
      {"fixture/default.xex", "omitted executable"}};
  for (const auto& [name, bytes] : files) {
    const auto offset = zip.size();
    const auto crc = u::Crc32(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    put(zip, 0x04034b50, 4); put(zip, 20, 2); put(zip, 0, 2); put(zip, 0, 2);
    put(zip, 0, 4); put(zip, crc, 4); put(zip, bytes.size(), 4); put(zip, bytes.size(), 4);
    put(zip, name.size(), 2); put(zip, 0, 2); zip += name; zip += bytes;
    put(directory, 0x02014b50, 4); put(directory, 20, 2); put(directory, 20, 2);
    put(directory, 0, 2); put(directory, 0, 2); put(directory, 0, 4);
    put(directory, crc, 4); put(directory, bytes.size(), 4); put(directory, bytes.size(), 4);
    put(directory, name.size(), 2); put(directory, 0, 2); put(directory, 0, 2);
    put(directory, 0, 2); put(directory, 0, 2); put(directory, 0, 4); put(directory, offset, 4);
    directory += name;
  }
  const auto offset = zip.size();
  zip += directory;
  put(zip, 0x06054b50, 4); put(zip, 0, 2); put(zip, 0, 2);
  put(zip, files.size(), 2); put(zip, files.size(), 2);
  put(zip, directory.size(), 4); put(zip, offset, 4); put(zip, 0, 2);
  return zip;
}
}  // namespace

int main() {
  const fs::path scratch = fs::temp_directory_path() / ("blitz-ios-import-session-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { fs::path root; ~Cleanup() { std::error_code ignored; fs::remove_all(root, ignored); } } cleanup{scratch};
  try {
    i::ImportSessionConfig config;
    config.game = Fingerprint({{"entrypoint", "default.xex", "retail entry"},
        {"archive-header", "gen/main_xbox.hdr", "retail header"},
        {"archive-data", "gen/main_xbox_0.ark", "retail archive"}});
    config.ultimate.fingerprint = Fingerprint({
        {"ultimate-archive-header", "gen/patch_xbox.hdr", "mod header"},
        {"ultimate-archive-data", "gen/patch_xbox_0.ark", "mod archive"}});
    config.ultimate.pins.archive_prefix = "fixture";
    config.ultimate.pins.version = "fixture";
    config.ultimate.pins.url = "https://example.invalid/not-contacted.zip";
    const std::string archive = MakeZip();
    config.ultimate.pins.size = archive.size();
    config.ultimate.pins.sha256 = hash::HashHex(archive);
    int downloads = 0;
    config.ultimate.download = [&](const fs::path& file, const u::PinsUltimate& pins,
                                   const p::ImportCallbacks&, std::string* error) {
      ++downloads;
      Check(pins.url == config.ultimate.pins.url, "transport receives pinned URL");
      return u::WriteFileText(file, archive, false, error);
    };
    const auto embedded = i::EmbeddedImportSessionConfig();
    Check(embedded.game.files.size() == 3, "production game fingerprint is embedded");
    Check(!embedded.ultimate.pins.sha256.empty(), "production Ultimate pins are embedded");
    const fs::path source = scratch / "source", mod = scratch / "mod", game = scratch / "game";
    MakeGame(source);
    MakeUltimate(mod);
    Write(scratch / "saves/profile", "persistent save");
    std::string error, warning;
    i::ImportSession session(game, config);
    Check(!session.Open(&error) && !session.BaseReady() && !session.Ready(), "empty app requires import");
    Check(!session.Finish(i::UltimateChoice::kSkip, {}, {}, &warning, &error), "cannot skip required retail files");
    Check(session.ImportSource(source, {}, &error), error);
    Check(session.BaseReady() && !session.Ready(), "base import waits for optional choice");
    Check(!fs::exists(game / ".ios-import-v1"), "base alone does not complete setup");
    i::ImportSession interrupted(game, config);
    Check(!interrupted.Open(&error) && interrupted.BaseReady(), "restart resumes choice without reimport");
    Check(!interrupted.Finish(i::UltimateChoice::kLocal, {}, {}, &warning, &error), "local requires selection");
    Check(!interrupted.Finish(i::UltimateChoice::kLocal, scratch / "missing.zip", {}, &warning, &error), "bad mod stays in setup");
    Check(interrupted.BaseReady() && !interrupted.Ready(), "mod failure preserves base");
    Check(interrupted.Finish(i::UltimateChoice::kSkip, {}, {}, &warning, &error), error);
    Check(interrupted.Ready() && downloads == 0, "offline skip commits without network");
    const auto entry_time = fs::last_write_time(game / "default.xex");
    i::ImportSession reused(game, config);
    Check(reused.Open(&error) && reused.Choice() == i::UltimateChoice::kSkip, error);
    Check(fs::last_write_time(game / "default.xex") == entry_time, "relaunch does not reimport");
    Check(reused.Finish(i::UltimateChoice::kLocal, mod, {}, &warning, &error), error);
    Check(warning.empty(), "supported local mod has no warning");
    Check(!fs::exists(game / "ultimate/default.xex"), "mod executable is omitted");
    Check(fs::exists(game / "ultimate/screenshots/_keepme"), "screenshot placeholder is supplied");
    Check(Read(game / "default.xex") == "retail entry", "retail executable is retained");
    Check(reused.Open(&error) && reused.Choice() == i::UltimateChoice::kLocal, error);
    Write(game / "ultimate/extra notes.txt", "tampered data");
    Check(!reused.Open(&error) && reused.BaseReady(), "corrupt optional file returns to optional setup");
    Write(mod / "gen/patch_xbox_0.ark", "local variant");
    Check(reused.Finish(i::UltimateChoice::kLocal, mod, {}, &warning, &error), error);
    Check(!warning.empty(), "different local revisions retain warning policy");
    Check(reused.Open(&error), "local revision verifies against recorded snapshot");
    Write(game / "ultimate/default.xex", "unexpected executable");
    Check(!reused.Open(&error) && reused.BaseReady(), "unexpected optional executable prevents reuse");
    fs::remove(game / "ultimate/default.xex");
    Check(reused.Open(&error), "removing unexpected optional file restores verified cache");
    Write(game / "ultimate/gen/patch_xbox_0.ark", "bad revision!");
    Check(!reused.Open(&error) && reused.BaseReady(), "changed local revision fails recorded hash");
    Check(reused.Finish(i::UltimateChoice::kDownload, {}, {}, &warning, &error), error);
    Check(downloads == 1, "only explicit download invokes transport");
    Check(reused.Open(&error) && reused.Choice() == i::UltimateChoice::kDownload, error);
    Check(downloads == 1, "successful relaunch does not download again");
    // Simulate termination after active retail data was moved to its backup.
    fs::rename(game, scratch / "game.import-backup");
    Write(scratch / "game.import-stage/junk", "unfinished");
    Check(reused.Open(&error), "retail transaction recovery restores completed setup");
    Check(!fs::exists(scratch / "game.import-backup") && !fs::exists(scratch / "game.import-stage"),
          "retail recovery removes abandoned staging");
    fs::rename(game / "ultimate", game / "ultimate.import-backup");
    Write(game / "ultimate.import-stage/junk", "unfinished");
    Check(reused.Open(&error), "optional transaction recovery restores completed setup");
    // An injected transport cannot bypass the shared archive identity gate.
    auto bad_config = config;
    bad_config.ultimate.download = [&](const fs::path& file, const auto&, const auto&, std::string* e) {
      std::string changed = archive;
      changed.back() = '!';
      return u::WriteFileText(file, changed, false, e);
    };
    i::ImportSession bad_transport(game, bad_config);
    Check(bad_transport.Open(&error), error);
    Check(!bad_transport.Finish(i::UltimateChoice::kDownload, {}, {}, &warning, &error) &&
          error.find("pinned size and SHA-256") != std::string::npos, "injected corrupt response fails hash gate");
    Check(bad_transport.BaseReady() && !bad_transport.Ready(), "failed download retains imported retail data");
    Check(!fs::exists(scratch / "game.ultimate-work"), "failed download work is cleaned");
    Check(bad_transport.Finish(i::UltimateChoice::kSkip, {}, {}, &warning, &error), error);
    Check(!fs::exists(game / "ultimate"), "skip removes incomplete optional content");
    Check(bad_transport.Open(&error), "skip after failed download remains persistent");
    p::ImportCallbacks cancel;
    cancel.cancelled = [] { return true; };
    Check(!bad_transport.Finish(i::UltimateChoice::kDownload, {}, cancel, &warning, &error), "cancellation stops setup");
    Check(bad_transport.Open(&error), "early cancellation preserves previous ready setup");
    Write(game / ".ios-import-v1", "rb-blitz-ios-import 1\nlocal 1\n" + std::string(64, 'a') + " 1 \"../escape\"\n");
    Check(!bad_transport.Open(&error) && bad_transport.BaseReady(), "unsafe setup record is rejected");
    Check(bad_transport.Finish(i::UltimateChoice::kSkip, {}, {}, &warning, &error), error);
    bool cancel_verify = false;
    cancel.cancelled = [&] { return cancel_verify; };
    cancel.progress = [&](int percent, std::string_view) { if (percent == 75) cancel_verify = true; };
    Check(!bad_transport.ImportSource(source, cancel, &error), "cancellation during base verification stops activation");
    Check(bad_transport.Open(&error), "late canceled base import keeps completed setup");
    cancel_verify = false;
    cancel.progress = [&](int percent, std::string_view) { if (percent == 90) cancel_verify = true; };
    Check(!bad_transport.Finish(i::UltimateChoice::kLocal, mod, cancel, &warning, &error),
          "cancellation during mod verification stops activation");
    Check(!fs::exists(game / "ultimate"), "late canceled optional import preserves skipped content");
    Check(bad_transport.Finish(i::UltimateChoice::kSkip, {}, {}, &warning, &error), error);
    Write(game / "default.xex", "badxx entry!");
    Check(!bad_transport.Open(&error) && !bad_transport.BaseReady(), "corrupt base requires retail repair");
    Check(Read(scratch / "saves/profile") == "persistent save", "setup never touches saves");
    std::cout << checks << " iOS import session checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
