// SPDX-License-Identifier: GPL-2.0-only
#include "setup_preferences.h"
#include "settings/store.h"

namespace rb_blitz::installer::macos {
bool SelectUltimate(const std::filesystem::path& settings_file, std::string* error) {
  if (settings_file.empty()) {
    if (error) *error = "No settings location is available.";
    return false;
  }
  namespace settings = rb_blitz::settings;
  settings::Store store({settings_file, {}, {}});
  const auto loaded = store.Load();
  if (!loaded.usable()) {
    if (error) *error = loaded.error;
    return false;
  }
  settings::Value target;
  target.kind = settings::ValueKind::kEnum;
  target.text = "ultimate";
  store.MarkChanged(settings::SettingId::kTarget, target, settings::Origin::kManual);
  const auto saved = store.Save();
  if (saved.status == settings::CommitStatus::kSaved ||
      saved.status == settings::CommitStatus::kNoChanges) return true;
  if (error) *error = saved.errors.empty() ? "Cannot save the Ultimate game version." : saved.errors.front();
  return false;
}
}  // namespace rb_blitz::installer::macos
