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
}  // namespace rb_blitz::settings
