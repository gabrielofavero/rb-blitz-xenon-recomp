// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "check.h"
#include <rex/app/app.h>
#include <rex/cli.h>
#include <rex/settings_registry.h>

/** Loads the public filesystem services without native UI, audio or graphics. */
class FileServiceApp : public rex::app::App {
 public:
  FileServiceApp() : App("content_filesystem_test") {}
 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(root.Set(std::filesystem::current_path()));
    for (const auto* key : {"ui.driver", "hid.drivers", "apu.driver", "gpu.driver"})
      REX_TRY_VOID(settings.SetText(rex::SettingLayer::kTitle, key, "none"));
    return settings.SetText(rex::SettingLayer::kTitle, "log.file", "false");
  }
};
