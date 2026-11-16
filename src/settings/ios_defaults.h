// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "settings/store.h"

namespace rb_blitz::settings {
inline constexpr int kIOSPolicyVersion = 5;
// The first bundled-Ultimate iOS release replaces its former retail default
// once. Later explicit Game Version choices survive restart and upgrades.
inline bool UpgradeIOSLaunchDefault(Store& store) {
  if (store.document().automatic_policy_version >= kIOSPolicyVersion) return false;
  const auto target = store.document().values.find(SettingId::kTarget);
  if (target == store.document().values.end() || target->second.text == "common") {
    Value ultimate;
    ultimate.kind = ValueKind::kEnum;
    ultimate.text = "ultimate";
    store.MarkChanged(SettingId::kTarget, ultimate, Origin::kAutomatic);
  }
  store.MarkPolicyVersion(kIOSPolicyVersion);
  return true;
}
// Importer setup makes an explicit choice, independent of the older bundled
// release migration. Subsequent manual selections survive ordinary relaunches.
inline constexpr int kIOSImporterPolicyVersion = 6;
inline bool ApplyIOSImportLaunchDefault(Store& store, bool use_ultimate, bool new_setup = false) {
  if (!new_setup && store.document().automatic_policy_version >= kIOSImporterPolicyVersion) return false;
  Value target;
  target.kind = ValueKind::kEnum;
  target.text = use_ultimate ? "ultimate" : "common";
  store.MarkChanged(SettingId::kTarget, target, Origin::kAutomatic);
  store.MarkPolicyVersion(kIOSImporterPolicyVersion);
  return true;
}
}  // namespace rb_blitz::settings
