// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the launch contract (launcher/src/game_launch.h, B7).

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

namespace rb_blitz::launcher {
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

// What the launcher would hand the game for one row: the profile's value, or the compiled
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
                                 LaunchTarget target) {
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

  // The one flag with no cvar behind it: the game takes where its data is, and the launcher is
  // the thing that knows (B1's detection, now walked up the tree).
  add("--game_data_root=" + QuoteArgument(roots.game_root.string()));

  // Every managed row the profile has moved off its compiled default, in the schema's order -
  // which is also the order the tabs show them in, so the printed command reads like the UI.
  // A row the launcher does not manage is not passed at all: the game's own file decides it
  // (D3), and passing the whole registry would take that choice away.
  for (const settings::Setting& setting : settings::kSettings) {
    if (!setting.argv_flag) {
      continue;  // a launcher-level row; its meaning is the block below
    }
    const std::string value = RowValue(session, setting);
    if (value.empty() || value == setting.default_text) {
      continue;  // never pass an empty flag, and never repeat a default
    }
    add("--" + std::string(setting.key) + "=" +
        (ValueIsText(setting.kind) ? QuoteArgument(value) : value));
  }

  // D4's three targets, which are what the game is *for*, rather than a value the user set.
  switch (target) {
    case LaunchTarget::kUltimate:
      add("--ultimate_mode=1");
      break;
    case LaunchTarget::kCommon:
      add("--ultimate_mode=0");
      break;
    case LaunchTarget::kDemo:
      // The XBLA trial: no licence, which is the path that takes the trial mode.
      add("--ultimate_mode=0");
      add("--license_mask=0");
      break;
  }

  // So the game reads the same file the launcher just wrote (D3).
  if (!session.path().empty()) {
    add("--launcher_profile=" + QuoteArgument(session.path().string()));
  }

  command.arguments = std::move(tail);

  if (roots.game_root.empty()) {
    command.error = "the game's data folder is not known";
  } else if (!roots.game_root_found) {
    command.error = "no game was found in " + roots.game_root.string();
  } else if (session.executable_dir().empty()) {
    command.error = "the launcher does not know its own folder";
  } else {
    std::error_code code;
    if (!std::filesystem::is_regular_file(command.executable, code) || code) {
      command.error = std::string(kGameExecutableName) + " was not found next to the launcher (in " +
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
  out += error.empty() ? "the launcher could not start it" : std::string(error);
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
  const std::filesystem::path probe = dir / ".rb_blitz_launcher_write_probe";
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
    // The launcher passes no profile path, so there is nothing the game could fail to read.
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

}  // namespace rb_blitz::launcher
