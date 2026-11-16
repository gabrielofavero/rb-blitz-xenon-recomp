// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <functional>
#include <memory>

namespace rb_blitz::platform {
class IOSImporter {
 public:
  virtual ~IOSImporter() = default;
  // Recheck at the deferred SDL handoff; background work must never start Metal.
  virtual bool PrepareLaunch() = 0;
};
// Called before ReXApp initializes paths, settings or the graphics runtime.
// Completion is asynchronous on UIKit's thread. Destroying the handle cancels work.
std::unique_ptr<IOSImporter> StartIOSImporter(std::function<void()> ready);
std::filesystem::path IOSImportedGameRoot();
bool IOSImportUsesUltimate();  // Read only after the session has become ready.
bool IOSImportCompletedSetup();  // This launch selected new assets/options.
}  // namespace rb_blitz::platform
