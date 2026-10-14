// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the launch contract (controller-settings/src/game_launch.h, B7).

#include "game_launch.h"

#include <fstream>
#include <system_error>
#include <string>
#include <string_view>

#include "settings_table.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rb_blitz::profile {
namespace {

// The Windows quoting rule (MSDN, "Parsing C++ command-line arguments"): a run of backslashes
// that precedes a quote is doubled, and the quote itself is escaped. Nothing else needs
// quoting, which is why this is short - a path is the only argument here that can hold a space.
std::string QuoteArgument(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  std::size_t backslashes = 0;
  for (const char character : text) {
    if (character == '\\') {
      ++backslashes;
      continue;
    }
    if (character == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out.push_back('"');
    } else {
      out.append(backslashes, '\\');
      out.push_back(character);
    }
    backslashes = 0;
  }
  out.append(backslashes * 2, '\\');
  out.push_back('"');
  return out;
}

// A value a user or a config file writes as text - a folder, a path, an enum word - can hold a
// space, so it is quoted; a number or a bool cannot. This is the same split the profile's own
// writer makes (row_ui's StyleForKind), and the reason it is repeated rather than shared is
// that this module must not need ImGui to answer it.
bool ValueIsText(settings::Kind kind) {
  return kind == settings::Kind::kString || kind == settings::Kind::kPathDir ||
         kind == settings::Kind::kPathFile;
}

// What Controller Settings would hand the game for one row: the profile's value, or the compiled
// default when the profile records nothing.
std::string RowValue(const ProfileSession& session, const settings::Setting& setting) {
  const ProfileSetting* stored = session.profile().FindSetting(setting.key);
  return stored == nullptr ? std::string(setting.default_text) : stored->value;
}

#if defined(_WIN32)
std::wstring ToWide(std::string_view text) {
  if (text.empty()) {
    return std::wstring();
  }
  const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                        nullptr, 0);
  if (count <= 0) {
    return std::wstring();
  }
  std::wstring out(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
  return out;
}
#endif

// Rows Controller Settings no longer hands to the game (cutover, owner decision 2026-10-13). Each one is
// backed by a row of the game's own in-game settings now (`src/settings/schema.cpp`): the game
// keeps them in its settings file, Help & Options edits them, and a command-line copy of the same
// value would outrank that file and turn the in-game rows read-only - which is exactly the state
// this list ends. The three retired rows (`enhancements_menu_dlc_songs`, `..._song_count`,
// `enhancements_rename_mod_settings`) are listed for the same reason: their features are fixed
// behaviour now (always-on / removed), so a passed value is noise the game would have to ignore.
//
// `present_letterbox` is the one key here whose in-game counterpart is not the same variable: the
// Interface page's Letterbox row edits the *guest's* own `letterbox` setting in the payload
// (ultimate_adapter.cpp), while this host cvar is what the SDK's mouse mapping reads. It is left at
// its default for an ordinary boot and stays settable through the game's own `rb_blitz.toml` (F4's
// *Save to config* writes it), which is why it is not passed any more either.
//
// What still travels: the two paths (a folder Controller Settings' General tab picks has no in-game row
// yet), the monitor/safe-area/dither/audio rows (fixed developer rows with no in-game equivalent
// either), and the controller rows - which the game also reads out of the profile itself.
constexpr std::string_view kGameOwnedRows[] = {
    "enhancements_dlc_cache",           "dlc_scan_threads",
    "enhancements_hide_menu_options",   "enhancements_hidden_menu_options",
    "enhancements_skip_offline_dialog", "enhancements_menu_dlc_songs",
    "enhancements_menu_dlc_song_count", "enhancements_rename_mod_settings",
    "resolution_scale",                 "fullscreen",
    "vsync",                            "gpu_backend",
    "swap_post_effect",                 "native_2x_msaa",
    "anisotropic_override",             "present_letterbox",
};

bool GameOwnsRow(const std::string_view key) {
  for (const std::string_view owned : kGameOwnedRows) {
    if (key == owned) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string FormatLaunchCommand(const LaunchCommand& command) {
  std::string out = QuoteArgument(command.executable.string());
  if (!command.arguments.empty()) {
    out += ' ';
    out += command.arguments;
  }
  return out;
}

LaunchCommand BuildLaunchCommand(const ProfileSession& session, const GameRoots& roots,
                                 LaunchTarget target, const LaunchOptions& options) {
  LaunchCommand command;
  command.executable = session.executable_dir() / kGameExecutableName;
  command.working_directory = session.executable_dir();

  std::string tail;
  const auto add = [&tail](const std::string& argument) {
    if (!tail.empty()) {
      tail += ' ';
    }
    tail += argument;
  };

  // The one flag with no cvar behind it: the game takes where its data is, and Controller Settings is
  // the thing that knows (B1's detection, now walked up the tree).
  add("--game_data_root=" + QuoteArgument(roots.game_root.string()));

  // Every managed row the profile has moved off its compiled default, in the schema's order -
  // which is also the order the tabs show them in, so the printed command reads like the UI.
  // A row Controller Settings does not manage is not passed at all: the game's own file decides it
  // (D3), and passing the whole registry would take that choice away.
  //
  // Controller Settings skips this block entirely (P8): its Play action is the game's data, the
  // target and the profile, and nothing else - the settings it owns are read by the game from the
  // profile itself, at the controller allowlist (contracts §8), not from a command line.
  if (options.settings_rows) {
    for (const settings::Setting& setting : settings::kSettings) {
      if (!setting.argv_flag) {
        continue;  // a host-level row; its meaning is the block below
      }
      if (GameOwnsRow(setting.key)) {
        continue;  // the game's own settings file owns this row now (P11b cutover)
      }
      const std::string value = RowValue(session, setting);
      if (value.empty() || value == setting.default_text) {
        continue;  // never pass an empty flag, and never repeat a default
      }
      add("--" + std::string(setting.key) + "=" +
          (ValueIsText(setting.kind) ? QuoteArgument(value) : value));
    }
  }

  // The target is **not** passed any more (P11b cutover, 2026-10-13). It used to travel as
  // `--ultimate_mode` / `--license_mask`, chosen by Controller Settings' dropdown; the game keeps its
  // own target in its settings file now (Help & Options > General > Target), and a command-line
  // copy would outrank that file and make the in-game row read-only. A fresh install still takes
  // Controller Settings' choice, once: the game's first boot migrates `[launch] target` out of the
  // profile it is handed below.
  (void)target;

  // So the game reads the same file Controller Settings just wrote (D3).
  if (!session.path().empty()) {
    add("--launcher_profile=" + QuoteArgument(session.path().string()));
  }

  command.arguments = std::move(tail);

  if (roots.game_root.empty()) {
    command.error = "the game's data folder is not known";
  } else if (!roots.game_root_found) {
    command.error = "no game was found in " + roots.game_root.string();
  } else if (session.executable_dir().empty()) {
    command.error = "Controller Settings does not know its own folder";
  } else {
    std::error_code code;
    if (!std::filesystem::is_regular_file(command.executable, code) || code) {
      command.error = std::string(kGameExecutableName) + " was not found next to Controller Settings (in " +
                      command.working_directory.string() + ")";
    }
  }
  command.ok = command.error.empty();
  return command;
}

std::filesystem::path GameLogDirectory(const LaunchCommand& command) {
  return command.executable.parent_path() / "logs";
}

std::string LaunchFailureMessage(const LaunchCommand& command, std::string_view error) {
  std::string out = "Cannot start the game: ";
  out += error.empty() ? "Controller Settings could not start it" : std::string(error);
  out += "\n\nThe command line was:\n";
  out += FormatLaunchCommand(command);
  out += "\n\nThe game writes its own log under ";
  out += GameLogDirectory(command).string();
  out += ", as a file named like ";
  out += command.executable.stem().string();
  out += "_001.log - that log is the only record a start leaves.";
  return out;
}

namespace {

// Proves a folder can be written by putting a file in it and taking it away again. Windows does
// not enforce the read-only attribute on a *directory* for creation, so the permissions bits
// cannot answer this; the write can.
bool DirectoryWritable(const std::filesystem::path& dir, std::string* why) {
  std::error_code code;
  if (!std::filesystem::exists(dir, code)) {
    // Nothing to write into yet: a save creates it, and a parent that cannot be created is
    // caught when that save runs.
    return true;
  }
  if (!std::filesystem::is_directory(dir, code) || code) {
    *why = "is not a folder";
    return false;
  }
  const std::filesystem::path probe = dir / ".rb_blitz_controller_settings_write_probe";
  {
    std::ofstream file(probe, std::ios::binary | std::ios::trunc);
    if (!file) {
      *why = "cannot be written to";
      return false;
    }
    file << 'x';
    if (!file) {
      *why = "cannot be written to";
      return false;
    }
  }
  std::error_code ignored;
  std::filesystem::remove(probe, ignored);
  return true;
}

}  // namespace

std::string LaunchReadiness(const ProfileSession& session) {
  const std::filesystem::path profile = session.path();
  if (profile.empty()) {
    // Controller Settings passes no profile path, so there is nothing the game could fail to read.
    return {};
  }

  std::error_code code;
  if (std::filesystem::exists(profile, code) && !code) {
    std::ifstream file(profile, std::ios::binary);
    if (!file) {
      return "the settings file " + profile.string() +
             " cannot be read, so the game would start on its defaults instead";
    }
  }

  const std::filesystem::path dir = profile.parent_path();
  std::string why;
  if (!DirectoryWritable(dir, &why)) {
    return "the settings folder " + dir.string() + " " + why +
           ", so the profile the game reads cannot be kept";
  }
  return {};
}

GameProcess::~GameProcess() { Release(); }

void GameProcess::Release() {
#if defined(_WIN32)
  if (handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
#else
  handle_ = nullptr;
#endif
}

bool GameProcess::Start(const LaunchCommand& command, std::string* error) {
#if defined(_WIN32)
  if (!command.ok) {
    *error = command.error;
    return false;
  }
  Release();

  std::wstring line = ToWide(FormatLaunchCommand(command));
  std::wstring application = command.executable.wstring();
  std::wstring directory = command.working_directory.wstring();
  if (line.empty() || application.empty()) {
    *error = "the command line could not be built";
    return false;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  // CreateProcessW may write into the command line buffer, which is why it is the mutable
  // `line` and not a temporary.
  if (!CreateProcessW(application.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
    *error = "CreateProcess failed with error " + std::to_string(GetLastError());
    return false;
  }
  CloseHandle(process.hThread);
  handle_ = process.hProcess;
  return true;
#else
  (void)command;
  *error = "starting the game is implemented for Windows only";
  return false;
#endif
}

bool GameProcess::Running() const {
#if defined(_WIN32)
  return handle_ != nullptr &&
         WaitForSingleObject(static_cast<HANDLE>(handle_), 0) == WAIT_TIMEOUT;
#else
  return false;
#endif
}

}  // namespace rb_blitz::profile
