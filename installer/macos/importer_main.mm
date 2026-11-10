// SPDX-License-Identifier: GPL-2.0-only
// Normal-user pre-game importer for the Finder-launched application bundle.
#import <Cocoa/Cocoa.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <mach-o/dyld.h>
#include <unistd.h>

#include "game_import_core.h"

namespace fs = std::filesystem;
namespace portable = rb_blitz::installer::portable;
namespace fingerprint = rb_blitz::fingerprint;

@interface RBImportProgress : NSObject <NSWindowDelegate> {
 @public
  std::atomic<bool>* cancelled;
}
- (void)cancelImport:(id)sender;
@end

@implementation RBImportProgress
- (void)cancelImport:(id)sender {
  (void)sender;
  cancelled->store(true);
}
- (BOOL)windowShouldClose:(id)sender {
  (void)sender;
  cancelled->store(true);
  return NO;
}
@end

namespace {

const std::vector<std::string> kRoles{"entrypoint", "archive-header", "archive-data"};
const std::vector<std::string> kOptional{"ArcadeInfo.xml", "charnames.zbm", "nxeart"};

NSString* Text(const std::string& value) {
  NSString* result = [NSString stringWithUTF8String:value.c_str()];
  return result ? result : @"The text could not be displayed.";
}

void Alert(NSAlertStyle style, const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  [alert setAlertStyle:style];
  [alert setMessageText:@"Rock Band Blitz"];
  [alert setInformativeText:Text(message)];
  [alert runModal];
}

bool Confirm(const std::string& message) {
  NSAlert* alert = [[NSAlert alloc] init];
  [alert setMessageText:@"Rock Band Blitz"];
  [alert setInformativeText:Text(message)];
  [alert addButtonWithTitle:@"Continue"];
  [alert addButtonWithTitle:@"Cancel"];
  return [alert runModal] == NSAlertFirstButtonReturn;
}

enum class SourceType { kCanceled, kPackage, kFolder };
SourceType ChooseSourceType() {
  NSAlert* alert = [[NSAlert alloc] init];
  [alert setMessageText:@"Choose your game source"];
  [alert setInformativeText:@"Select your own supported Rock Band Blitz package or extracted folder."];
  [alert addButtonWithTitle:@"Package file"];
  [alert addButtonWithTitle:@"Extracted folder"];
  [alert addButtonWithTitle:@"Cancel"];
  switch ([alert runModal]) {
    case NSAlertFirstButtonReturn: return SourceType::kPackage;
    case NSAlertSecondButtonReturn: return SourceType::kFolder;
    default: return SourceType::kCanceled;
  }
}

fs::path PickSource(SourceType type) {
  NSOpenPanel* picker = [NSOpenPanel openPanel];
  [picker setAllowsMultipleSelection:NO];
  [picker setCanChooseFiles:type == SourceType::kPackage];
  [picker setCanChooseDirectories:type == SourceType::kFolder];
  [picker setPrompt:@"Select"];
  if ([picker runModal] != NSModalResponseOK) return {};
  const char* path = [[[[picker URLs] firstObject] path] fileSystemRepresentation];
  return path ? fs::path(path) : fs::path{};
}

fs::path ExecutableDir() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size == 0 || size > 32768) return {};
  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
  std::error_code error;
  const fs::path absolute = fs::weakly_canonical(fs::path(path.c_str()), error);
  return error ? fs::path{} : absolute.parent_path();
}

fs::path GameDestination() {
  const char* home = std::getenv("HOME");
  if (!home || !*home) return {};
  return fs::path(home) / "Library/Application Support/rb_blitz/game";
}

std::string SizeLabel(std::uint64_t bytes) {
  return std::to_string((bytes + 1048575) / 1048576) + " MiB";
}

bool ImportWithProgress(const rb_blitz::installer::GameSourcePlan& source,
                        const fs::path& destination,
                        const fingerprint::GameFingerprint& expected,
                        std::string* error) {
  NSRect bounds = NSMakeRect(0, 0, 460, 138);
  NSWindow* window = [[NSWindow alloc] initWithContentRect:bounds
      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
      backing:NSBackingStoreBuffered defer:NO];
  [window setTitle:@"Importing Rock Band Blitz"];
  [window center];

  NSTextField* detail = [NSTextField labelWithString:@"Preparing game data…"];
  [detail setFrame:NSMakeRect(22, 94, 415, 24)];
  [[window contentView] addSubview:detail];
  NSProgressIndicator* progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(22, 61, 415, 18)];
  [progress setIndeterminate:NO];
  [progress setMinValue:0];
  [progress setMaxValue:100];
  [[window contentView] addSubview:progress];
  NSButton* cancel = [[NSButton alloc] initWithFrame:NSMakeRect(340, 17, 95, 30)];
  [cancel setTitle:@"Cancel"];
  [cancel setBezelStyle:NSBezelStyleRounded];
  [[window contentView] addSubview:cancel];

  std::atomic<bool> cancelled{false};
  std::atomic<bool> done{false};
  std::atomic<int> percent{0};
  std::mutex detail_mutex;
  std::string detail_text = "Preparing game data";
  RBImportProgress* handler = [[RBImportProgress alloc] init];
  handler->cancelled = &cancelled;
  [cancel setTarget:handler];
  [cancel setAction:@selector(cancelImport:)];
  [window setDelegate:handler];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  bool imported = false;
  portable::ImportCallbacks callbacks;
  callbacks.progress = [&](int value, std::string_view text) {
    percent.store(value);
    std::lock_guard lock(detail_mutex);
    detail_text.assign(text);
  };
  callbacks.cancelled = [&] { return cancelled.load(); };
  callbacks.continue_at = [&](portable::ImportPhase) { return !cancelled.load(); };
  std::thread worker([&] {
    imported = portable::Import(source, destination, expected, kRoles, kOptional,
                                callbacks, error);
    done.store(true);
  });
  while (!done.load()) {
    [progress setDoubleValue:percent.load()];
    {
      std::lock_guard lock(detail_mutex);
      [detail setStringValue:Text(detail_text)];
    }
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  }
  worker.join();
  [window setDelegate:nil];
  [window close];
  return imported;
}

int LaunchGame(const fs::path& bin, const fs::path& game) {
  const fs::path executable = bin / "rb_blitz";
  const std::string root_arg = "--game_data_root=" + game.string();
  char* arguments[] = {const_cast<char*>(executable.c_str()),
                       const_cast<char*>(root_arg.c_str()), nullptr};
  execv(executable.c_str(), arguments);
  Alert(NSAlertStyleCritical, "Cannot start the game: " + std::string(std::strerror(errno)));
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    const fs::path bin = ExecutableDir();
    const fs::path game = GameDestination();
    if (bin.empty() || game.empty()) {
      Alert(NSAlertStyleCritical, "Cannot find the application or your home data directory.");
      return 1;
    }
    fingerprint::GameFingerprint expected;
    std::string error;
    if (!fingerprint::LoadGameFingerprint(bin / "game-fingerprints.toml", &expected, &error)) {
      Alert(NSAlertStyleCritical, "The installed game fingerprint record is invalid: " + error);
      return 1;
    }
    if (!portable::RecoverImport(game, expected, kRoles, &error)) {
      Alert(NSAlertStyleCritical, "Cannot recover the previous import: " + error);
      return 1;
    }
    const bool repair = argc > 1 && std::string_view(argv[1]) == "--repair";
    error.clear();
    const bool valid = portable::VerifyTree(game, expected, kRoles, nullptr, &error);
    if (!valid || repair) {
      if (valid && !Confirm("Choose a supported source to replace the current import. Cancel keeps the current game data."))
        return LaunchGame(bin, game);
      const SourceType type = ChooseSourceType();
      if (type == SourceType::kCanceled) return valid ? LaunchGame(bin, game) : 0;
      const fs::path selected = PickSource(type);
      if (selected.empty()) return valid ? LaunchGame(bin, game) : 0;
      rb_blitz::installer::GameSourcePlan source;
      error.clear();
      const bool probed = type == SourceType::kFolder
          ? portable::ProbeFolder(selected, expected, kRoles, kOptional, &source, &error)
          : portable::ProbePackage(selected, expected, kRoles, kOptional, &source, &error);
      if (!probed) {
        Alert(NSAlertStyleCritical, error);
        return valid ? LaunchGame(bin, game) : 1;
      }
      const std::string preview = "Source: " + source.description +
          "\nExpected game version: " + expected.xex_version +
          "\nRequired space: at least " + SizeLabel(source.PresentBytes()) +
          "\nDestination: " + game.string() + "\n\nImport now?";
      if (!Confirm(preview)) return valid ? LaunchGame(bin, game) : 0;
      error.clear();
      if (!ImportWithProgress(source, game, expected, &error)) {
        Alert(NSAlertStyleCritical, "Game import failed: " + error);
        return valid && portable::VerifyTree(game, expected, kRoles, nullptr, nullptr)
            ? LaunchGame(bin, game) : 1;
      }
    }
    return LaunchGame(bin, game);
  }
}
