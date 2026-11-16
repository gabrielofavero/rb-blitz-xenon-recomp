// SPDX-License-Identifier: GPL-2.0-only
// Headless checks of the installer's real settings writer; no Cocoa or game boot.
#include "check.h"
#include "setup_preferences.h"
#include "settings/paths.h"
#include "settings/store.h"
#include <unistd.h>

int main() {
  namespace fs = std::filesystem;
  namespace settings = rb_blitz::settings;
  using namespace rb_blitz::test;
  using rb_blitz::installer::macos::SelectUltimate;
  const auto scratch = fs::temp_directory_path() / ("rb-blitz-macos-setup-" + std::to_string(getpid()));
  const auto file = scratch / "settings.toml";
  std::string error, text;
  bool existed = false;

  CHECK_TRUE(SelectUltimate(file, &error));
  CHECK_TRUE(settings::LoadSettingsDocument(file).document.values.at(settings::SettingId::kTarget).text == "ultimate");

  const std::string prior = "schema_version = 1\n# keep my settings\n[general]\ntarget = 'common' # chosen before installing\n"
                            "startup = 'fast'\n[custom]\nunknown = 'keep me'\n";
  CHECK_TRUE(settings::WriteFileAtomic(file, prior, &error));
  CHECK_TRUE(SelectUltimate(file, &error));
  CHECK_TRUE(settings::ReadFileText(file, &text, &existed));
  CHECK_TRUE(text.find("# keep my settings") != std::string::npos);
  CHECK_TRUE(text.find("# chosen before installing") != std::string::npos);
  CHECK_TRUE(text.find("startup = 'fast'") != std::string::npos);
  CHECK_TRUE(text.find("unknown = 'keep me'") != std::string::npos);
  const auto saved = text;
  CHECK_TRUE(SelectUltimate(file, &error));
  CHECK_TRUE(settings::ReadFileText(file, &text, &existed));
  CHECK_TRUE(saved == text);

  for (const std::string refused : {"schema_version = 99\n", "schema_version = [broken\n"}) {
    CHECK_TRUE(settings::WriteFileAtomic(file, refused, &error));
    CHECK_TRUE(!SelectUltimate(file, &error));
    CHECK_TRUE(settings::ReadFileText(file, &text, &existed));
    CHECK_TRUE(text == refused);
  }
  CHECK_TRUE(!SelectUltimate({}, &error));

  // Use the same settings path resolver as the game, including its moved-folder pointer.
  const auto app_data = scratch / "Application Support";
  const auto moved = scratch / "moved settings";
  CHECK_TRUE(rb_blitz::profile::WriteSettingsDirPointer(app_data, moved, &error));
  rb_blitz::profile::ProfilePathInputs inputs;
  inputs.app_data_dir = app_data;
  CHECK_TRUE(settings::ResolveSettingsFile(inputs) == moved / "settings.toml");
  CHECK_TRUE(SelectUltimate(settings::ResolveSettingsFile(inputs), &error));
  fs::remove_all(scratch);
  return g_failures == 0 ? 0 : 1;
}
