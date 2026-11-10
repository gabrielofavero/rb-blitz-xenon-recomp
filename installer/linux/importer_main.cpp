// SPDX-License-Identifier: GPL-2.0-only
// Normal-user, pre-game Linux importer. The desktop entry starts this process.
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "game_import_core.h"

namespace fs = std::filesystem;
namespace portable = rb_blitz::installer::portable;
namespace fingerprint = rb_blitz::fingerprint;

namespace {

constexpr const char* kTitle = "Rock Band Blitz";
const std::vector<std::string> kRoles{"entrypoint", "archive-header", "archive-data"};
const std::vector<std::string> kOptional{"ArcadeInfo.xml", "charnames.zbm", "nxeart"};

struct Dialog {
  pid_t pid = -1;
  int fd = -1;
};

Dialog StartDialog(const std::vector<std::string>& args, bool capture_output = false,
                   bool send_input = false) {
  int channel[2];
  if ((capture_output || send_input) && pipe(channel) != 0) return {};
  const pid_t child = fork();
  if (child < 0) {
    if (capture_output || send_input) { close(channel[0]); close(channel[1]); }
    return {};
  }
  if (child == 0) {
    if (capture_output) { close(channel[0]); dup2(channel[1], STDOUT_FILENO); close(channel[1]); }
    if (send_input) { close(channel[1]); dup2(channel[0], STDIN_FILENO); close(channel[0]); }
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("zenity"));
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    execvp("zenity", argv.data());
    _exit(127);
  }
  Dialog result{child, -1};
  if (capture_output) { close(channel[1]); result.fd = channel[0]; }
  if (send_input) { close(channel[0]); result.fd = channel[1]; }
  return result;
}

int FinishDialog(Dialog* dialog) {
  if (dialog->fd >= 0) { close(dialog->fd); dialog->fd = -1; }
  if (dialog->pid < 0) return 127;
  int status = 0;
  while (waitpid(dialog->pid, &status, 0) < 0) {
    if (errno != EINTR) return 127;
  }
  dialog->pid = -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

std::pair<int, std::string> Ask(const std::vector<std::string>& args) {
  Dialog dialog = StartDialog(args, true);
  std::string result;
  if (dialog.fd >= 0) {
    char buffer[4096];
    ssize_t count;
    while ((count = read(dialog.fd, buffer, sizeof(buffer))) > 0)
      result.append(buffer, static_cast<size_t>(count));
  }
  const int status = FinishDialog(&dialog);
  if (!result.empty() && result.back() == '\n') result.pop_back();
  return {status, result};
}

int Show(const char* kind, const std::string& message) {
  Dialog dialog = StartDialog({kind, "--title", kTitle, "--text", message});
  return FinishDialog(&dialog);
}

fs::path ExecutableDir() {
  std::vector<char> path(4096);
  const ssize_t count = readlink("/proc/self/exe", path.data(), path.size());
  if (count <= 0 || static_cast<size_t>(count) == path.size()) return {};
  return fs::path(std::string(path.data(), static_cast<size_t>(count))).parent_path();
}

fs::path GameDestination() {
  const char* data = std::getenv("XDG_DATA_HOME");
  fs::path base = data && *data ? fs::path(data) : fs::path{};
  if (!base.is_absolute()) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    base = fs::path(home) / ".local/share";
  }
  return base / "rb_blitz" / "game";
}

std::string SizeLabel(std::uint64_t bytes) {
  return std::to_string((bytes + 1048575) / 1048576) + " MiB";
}

bool WriteProgress(int fd, int percent, std::string_view detail) {
  if (fd < 0) return false;
  const std::string line = std::to_string(percent) + "\n# " + std::string(detail) + "\n";
  return write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}

}  // namespace

int main(int argc, char** argv) {
  // A closed progress window makes write return EPIPE, so import can cancel safely.
  std::signal(SIGPIPE, SIG_IGN);
  const fs::path bin = ExecutableDir();
  const fs::path game = GameDestination();
  if (bin.empty() || game.empty()) {
    Show("--error", "Cannot find the application or your home data directory.");
    return 1;
  }
  fingerprint::GameFingerprint expected;
  std::string error;
  if (!fingerprint::LoadGameFingerprint(bin / "game-fingerprints.toml", &expected, &error)) {
    Show("--error", "The installed game fingerprint record is missing or invalid: " + error);
    return 1;
  }
  if (!portable::RecoverImport(game, expected, kRoles, &error)) {
    Show("--error", "Cannot recover the previous import: " + error);
    return 1;
  }
  const bool repair = argc > 1 && std::string_view(argv[1]) == "--repair";
  error.clear();
  const bool valid = portable::VerifyTree(game, expected, kRoles, nullptr, &error);
  const auto launch = [&]() -> int {
    const fs::path executable = bin / "rb_blitz";
    const std::string game_arg = "--game_data_root=" + game.string();
    std::vector<char*> game_argv{const_cast<char*>(executable.c_str()),
                                 const_cast<char*>(game_arg.c_str()), nullptr};
    execv(executable.c_str(), game_argv.data());
    Show("--error", "Cannot start the game: " + std::string(std::strerror(errno)));
    return 1;
  };
  if (!valid || repair) {
    if (valid) {
      if (Show("--question", "Choose a supported Rock Band Blitz source to replace the current import. Cancel keeps the current game data.") != 0)
        return launch();
    } else {
      if (Show("--question", "Select your own supported Rock Band Blitz package or extracted folder. The game data will be stored in your user account.") != 0)
        return 0;
    }
    const auto [choice_status, choice] = Ask({"--list", "--title", kTitle, "--text", "Choose the source type",
                                               "--column", "Source", "Package file", "Extracted folder"});
    if (choice_status != 0) return valid ? launch() : 0;
    const bool folder = choice == "Extracted folder";
    if (!folder && choice != "Package file") return 1;
    std::vector<std::string> picker{"--file-selection", "--title", "Select Rock Band Blitz source"};
    if (folder) picker.push_back("--directory");
    const auto [pick_status, selected] = Ask(picker);
    if (pick_status != 0 || selected.empty()) return valid ? launch() : 0;
    rb_blitz::installer::GameSourcePlan source;
    error.clear();
    const bool probed = folder
        ? portable::ProbeFolder(selected, expected, kRoles, kOptional, &source, &error)
        : portable::ProbePackage(selected, expected, kRoles, kOptional, &source, &error);
    if (!probed) { Show("--error", error); return valid ? launch() : 1; }
    const std::string preview = "Source: " + source.description + "\nVersion: " + expected.xex_version +
        "\nRequired space: at least " + SizeLabel(source.PresentBytes()) +
        "\nDestination: " + game.string() + "\n\nImport now?";
    if (Show("--question", preview) != 0) return valid ? launch() : 0;
    Dialog progress = StartDialog({"--progress", "--title", kTitle, "--text", "Importing game data",
                                   "--percentage", "0", "--auto-close"}, false, true);
    if (progress.pid < 0) { Show("--error", "Cannot start the import progress window."); return 1; }
    bool canceled = false;
    portable::ImportCallbacks callbacks;
    callbacks.progress = [&](int percent, std::string_view detail) {
      if (!WriteProgress(progress.fd, percent, detail)) canceled = true;
    };
    callbacks.cancelled = [&] { return canceled; };
    error.clear();
    const bool imported = portable::Import(source, game, expected, kRoles, kOptional, callbacks, &error);
    if (!imported) kill(progress.pid, SIGTERM);
    FinishDialog(&progress);
    if (!imported) {
      Show("--error", "Game import failed: " + error);
      return valid && portable::VerifyTree(game, expected, kRoles, nullptr, nullptr)
          ? launch() : 1;
    }
  }
  return launch();
}
