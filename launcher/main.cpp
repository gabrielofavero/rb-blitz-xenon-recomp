// rb_blitz launcher - entry point (docs/plans/launcher-plan.md, prompts P0.4 and A1).
//
// P0.4 answered "does the SDL3 + ImGui stack work in this build tree". A1 replaced the
// placeholder with the tab shell (launcher/src/shell.h): three tabs from the settings
// schema, one focus ring, read-only rows, and window geometry remembered through the
// launcher profile (src/launcher/profile.h). Saving, the bottom bar, the pad and the game
// launch are later prompts - see launcher/README.md for what exists today.
//
// Renderer backend: SDL_GPU, via imgui_impl_sdlgpu3. D1 prefers it because this build
// turns SDL_RENDER off on purpose (rexglue-sdk/thirdparty/CMakeLists.txt) and SDL_GPU is
// core SDL3, already compiled into the SDL3-static the game links - so the launcher pulls
// in no renderer, no D3D12 device of its own, and no emulator runtime (D1: it does not
// link rex::runtime).

#include <SDL3/SDL.h>
// SDL.h does not pull SDL_main.h in (SDL 3.x), so ask for it explicitly: on Windows it
// supplies the WinMain the WIN32 subsystem needs and renames main() to SDL_main().
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include "game_launch.h"
#include "general_report.h"
#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "prefill.h"
#include "schema_view.h"
#include "shell.h"
#include "ultimate_state.h"
#include "virtual_pad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr const char* kWindowTitle = "Rock Band Blitz Launcher";

// The profile's own defaults (src/launcher/profile.h), used when a stored size is missing or
// nonsense. The number is in logical points, the same unit the profile stores: a DPI change
// moves the pixels the user sees, not the number in the file. main() multiplies it by the
// display's content scale, so a 300% display opens a window three times as large in pixels
// rather than one a third the size. The size is chosen so every tab's content - the General
// tab's settings-file block included - fits without scrolling at 100%.
constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 840;
// The range a stored size is clamped to, in points. The floor is where the layout stops being
// readable rather than what the launcher prefers, and it is also the window's own minimum size,
// so the frame's maximize box never offers less than the content can use.
constexpr int kMinWindowWidth = 720;
constexpr int kMinWindowHeight = 520;
constexpr int kMaxWindowSize = 8192;

// The point size of the UI face before DPI scaling. ImGui's built-in face is a 13px pixel
// font; a real outline font at 16px reads as a normal application window.
constexpr float kUiFontSize = 16.0f;

namespace fs = std::filesystem;

// Defined with the rest of the path helpers below; the dump modes need it before the loop.
fs::path ExecutableDir();
// Defined with the window helpers below; --dump-display reports what they decide.
int WindowDimension(int value, int fallback, int minimum);
void FitToDisplay(int* width, int* height);
std::string LoadUiFont();

// A WIN32-subsystem executable has no console, so a bring-up failure would otherwise be
// silent. Name the step that failed and let SDL say why.
void Fatal(const char* step) {
  char message[512];
  std::snprintf(message, sizeof(message), "%s failed:\n%s", step, SDL_GetError());
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, message, nullptr);
}

struct Options {
  // --launcher_profile=<path>, the highest-priority step of D2's resolution order.
  std::string profile_path;
  // --game_data_root=<path>, the game's own flag for where its data is. The launcher takes it
  // too, because that is how a shortcut to the launcher already points at an install (B1).
  std::string game_data_root;
  // --dump-layout[=<path>] prints the rows the shell would draw and leaves, before any
  // window is created. It is A1's headless evidence that every schema row reaches a tab,
  // and the part of the launcher a build machine can run. With no path it writes to stdout;
  // with one it writes the file, because a WIN32-subsystem process has no console of its
  // own to be captured from.
  bool dump_layout = false;
  std::string dump_layout_path;
  // --dump-general[=<path>] prints what the General tab would decide - the game root it found,
  // D5's Ultimate state, the effective target, and each path row's verdict - and leaves. It is
  // B1's headless evidence, so the four states can be asserted on text instead of OCR.
  bool dump_general = false;
  std::string dump_general_path;
  // --dump-profile[=<path>] prints B4's write path and the precedence audit - where the settings
  // file is, whether it is portable, what a save would change, what a reset would remove, and
  // every row the game's own rb_blitz.toml decides - and leaves. The badge is the one part of
  // B4 that otherwise only exists on screen.
  bool dump_profile = false;
  std::string dump_profile_path;
  // --dump-prefill[=<path>] prints D4's first-run prefill - what install-manifest.toml says,
  // whether it applies, and what it would seed - and leaves. It is how "an install the launcher
  // has never seen" is checked on a build machine instead of by reading a folder by eye.
  bool dump_prefill = false;
  std::string dump_prefill_path;
  // --dump-display[=<path>] prints what the launcher made of the display - the usable bounds,
  // the content scale, the window size it would open at, and the face it loaded - and leaves.
  // A1's "legible at 100% and 200%" is otherwise only checkable from a screenshot.
  bool dump_display = false;
  std::string dump_display_path;
  // --print-command[=<path>] prints the exact command line B7 would start the game with
  // (Contract 3) and leaves. It is the same builder a real launch uses, so the contract is
  // assertable without booting anything - and it is the first thing a bug report wants.
  bool print_command = false;
  std::string print_command_path;
  // --focus-log=<path> writes what the focus ring and the input devices did, and leaves the
  // window alone (A3). It is how "the pad moved the ring" and "the bar named the focused row" are
  // read back without a screenshot: a WIN32 process has no console to print to, so it is a file.
  std::string focus_log_path;
  // --no-gamepad opens no pad for navigation (D6's recovery switch). A pad that is holding a
  // direction down - a snapped stick, a cushion on the D-pad - cannot move the ring while it is
  // set, and the launcher is otherwise unusable with one plugged in.
  bool no_gamepad = false;
  // --test-pad=<script> attaches a virtual pad and presses it on a schedule (launcher/src/
  // virtual_pad.h). It is A3's evidence hook: a pad plugging in mid-session, moving the ring and
  // being unplugged again, on a machine with no controller attached to it.
  std::string test_pad_script;
};

Options ParseOptions(int argc, char** argv) {
  Options options;
  constexpr std::string_view kProfilePrefix = "--launcher_profile=";
  constexpr std::string_view kGameDataPrefix = "--game_data_root=";
  constexpr std::string_view kDumpPrefix = "--dump-layout=";
  constexpr std::string_view kDumpGeneralPrefix = "--dump-general=";
  constexpr std::string_view kDumpProfilePrefix = "--dump-profile=";
  constexpr std::string_view kDumpPrefillPrefix = "--dump-prefill=";
  constexpr std::string_view kDumpDisplayPrefix = "--dump-display=";
  constexpr std::string_view kPrintCommandPrefix = "--print-command=";
  constexpr std::string_view kFocusLogPrefix = "--focus-log=";
  constexpr std::string_view kTestPadPrefix = "--test-pad=";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--dump-layout") {
      options.dump_layout = true;
    } else if (argument == "--dump-general") {
      options.dump_general = true;
    } else if (argument == "--dump-profile") {
      options.dump_profile = true;
    } else if (argument == "--dump-prefill") {
      options.dump_prefill = true;
    } else if (argument == "--dump-display") {
      options.dump_display = true;
    } else if (argument == "--print-command") {
      options.print_command = true;
    } else if (argument == "--no-gamepad") {
      options.no_gamepad = true;
    } else if (argument.size() > kTestPadPrefix.size() &&
               argument.substr(0, kTestPadPrefix.size()) == kTestPadPrefix) {
      options.test_pad_script = std::string(argument.substr(kTestPadPrefix.size()));
    } else if (argument.size() > kFocusLogPrefix.size() &&
               argument.substr(0, kFocusLogPrefix.size()) == kFocusLogPrefix) {
      options.focus_log_path = std::string(argument.substr(kFocusLogPrefix.size()));
    } else if (argument.size() > kPrintCommandPrefix.size() &&
               argument.substr(0, kPrintCommandPrefix.size()) == kPrintCommandPrefix) {
      options.print_command = true;
      options.print_command_path = std::string(argument.substr(kPrintCommandPrefix.size()));
    } else if (argument.size() > kDumpDisplayPrefix.size() &&
               argument.substr(0, kDumpDisplayPrefix.size()) == kDumpDisplayPrefix) {
      options.dump_display = true;
      options.dump_display_path = std::string(argument.substr(kDumpDisplayPrefix.size()));
    } else if (argument.size() > kDumpProfilePrefix.size() &&
               argument.substr(0, kDumpProfilePrefix.size()) == kDumpProfilePrefix) {
      options.dump_profile = true;
      options.dump_profile_path = std::string(argument.substr(kDumpProfilePrefix.size()));
    } else if (argument.size() > kDumpPrefillPrefix.size() &&
               argument.substr(0, kDumpPrefillPrefix.size()) == kDumpPrefillPrefix) {
      options.dump_prefill = true;
      options.dump_prefill_path = std::string(argument.substr(kDumpPrefillPrefix.size()));
    } else if (argument.size() > kDumpGeneralPrefix.size() &&
               argument.substr(0, kDumpGeneralPrefix.size()) == kDumpGeneralPrefix) {
      options.dump_general = true;
      options.dump_general_path = std::string(argument.substr(kDumpGeneralPrefix.size()));
    } else if (argument.size() > kDumpPrefix.size() &&
               argument.substr(0, kDumpPrefix.size()) == kDumpPrefix) {
      options.dump_layout = true;
      options.dump_layout_path = std::string(argument.substr(kDumpPrefix.size()));
    } else if (argument.size() >= kProfilePrefix.size() &&
               argument.substr(0, kProfilePrefix.size()) == kProfilePrefix) {
      options.profile_path = std::string(argument.substr(kProfilePrefix.size()));
    } else if (argument.size() >= kGameDataPrefix.size() &&
               argument.substr(0, kGameDataPrefix.size()) == kGameDataPrefix) {
      options.game_data_root = std::string(argument.substr(kGameDataPrefix.size()));
    }
    // Anything else is somebody else's business: the launch contract is one-way
    // (Contract 3), so an unknown switch here is not fatal.
  }
  return options;
}

int Emit(const std::string& text, const std::string& path, int code) {
  if (path.empty()) {
    std::fputs(text.c_str(), stdout);
    return code;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return 2;
  }
  file << text;
  return file.good() ? code : 1;
}

int DumpLayout(const std::string& path) {
  const std::string text = rb_blitz::launcher::DescribeLayout(rb_blitz::launcher::BuildLayout());
  const bool complete = text.find("MISSING-TOOLTIP") == std::string::npos;
  const std::string all =
      text + (complete ? "every row carries a tooltip\n" : "a row is missing its tooltip\n");
  return Emit(all, path, complete ? 0 : 1);
}

// The session both dump modes describe: D2's own resolution from the same inputs the launcher
// itself uses, so a report is about the profile and the root the launcher would really pick.
rb_blitz::launcher::ProfileSession MakeDumpSession(const Options& options) {
  const fs::path launcher_dir = ExecutableDir();
  rb_blitz::launcher::ProfilePathInputs inputs =
      rb_blitz::launcher::ProfilePathInputsFromEnvironment(launcher_dir);
  inputs.command_line_value = options.profile_path;
  const fs::path profile_path = rb_blitz::launcher::ResolveProfilePath(inputs);

  rb_blitz::launcher::ProfileLoadResult load;
  if (!profile_path.empty()) {
    load = rb_blitz::launcher::LoadProfile(profile_path);
  }
  return rb_blitz::launcher::ProfileSession(std::move(inputs), std::move(load));
}

// The General tab, decided without a window.
int DumpGeneral(const Options& options) {
  const rb_blitz::launcher::GameRoots roots =
      rb_blitz::launcher::DetectGameRoots(ExecutableDir(), options.game_data_root);
  const rb_blitz::launcher::ProfileSession session = MakeDumpSession(options);
  return Emit(rb_blitz::launcher::DescribeGeneral(session, roots), options.dump_general_path, 0);
}

// B7's dry run: the exact command line a Launch Game would start, from the same builder, so the
// launch contract is assertable on a build machine (`--print-command` in launcher/README.md).
int PrintCommand(const Options& options) {
  const rb_blitz::launcher::GameRoots roots =
      rb_blitz::launcher::DetectGameRoots(ExecutableDir(), options.game_data_root);
  const rb_blitz::launcher::ProfileSession session = MakeDumpSession(options);
  const rb_blitz::launcher::LaunchTarget target = rb_blitz::launcher::FallbackTarget(
      session.profile().target, rb_blitz::launcher::DetectUltimateState(roots.game_root));

  const rb_blitz::launcher::LaunchCommand command =
      rb_blitz::launcher::BuildLaunchCommand(session, roots, target);

  std::string text;
  text += "working dir : " + command.working_directory.string() + "\n";
  text += "game exe    : ";
  text += command.ok ? "found\n" : "MISSING\n";
  text += "command     : " + rb_blitz::launcher::FormatLaunchCommand(command) + "\n";
  if (!command.ok) {
    text += "cannot start: " + command.error + "\n";
  }
  return Emit(text, options.print_command_path, command.ok ? 0 : 1);
}

int DumpProfile(const Options& options) {
  const rb_blitz::launcher::ProfileSession session = MakeDumpSession(options);

  std::string text = "settings file  : ";
  text += session.path().empty() ? "(nowhere to save)" : session.path().string();
  text += session.portable() ? " (portable, from the marker beside the launcher)\n" : "\n";
  if (session.path_from_override()) {
    text += "                 an override named it, so portable mode cannot move it\n";
  }
  text += "writable       : ";
  text += session.CanSave() ? "yes\n" : ("no, " + session.Refusal() + "\n");
  text += "dirty          : ";
  text += session.Dirty() ? "yes, a save would write\n" : "no, a save would not touch the file\n";
  text += rb_blitz::launcher::DescribePrecedence(session);
  return Emit(text, options.dump_profile_path, 0);
}

// The three facts the prefill needs, from the same detection the General tab uses.
rb_blitz::launcher::PrefillInputs FirstRunInputs(const rb_blitz::launcher::GameRoots& roots,
                                                 const rb_blitz::launcher::ProfileSession& session) {
  rb_blitz::launcher::PrefillInputs inputs;
  inputs.has_profile = session.has_file();
  inputs.game_root_found = roots.game_root_found;
  inputs.detected_game_dir = roots.game_root.string();
  inputs.ultimate_available = rb_blitz::launcher::UltimateAvailable(
      rb_blitz::launcher::DetectUltimateState(roots.game_root));
  return inputs;
}

// D4's first run, decided without a window: what the install manifest says, whether it is
// usable, and what it would seed. This is the evidence an install the launcher has never seen
// is prefilled, without reading a settings folder by eye.
int DumpPrefill(const Options& options) {
  const fs::path launcher_dir = ExecutableDir();
  const rb_blitz::launcher::GameRoots roots =
      rb_blitz::launcher::DetectGameRoots(launcher_dir, options.game_data_root);
  const rb_blitz::launcher::ProfileSession session = MakeDumpSession(options);
  const rb_blitz::launcher::PrefillPlan plan =
      rb_blitz::launcher::PlanPrefill(launcher_dir, FirstRunInputs(roots, session));
  return Emit(rb_blitz::launcher::DescribePrefill(plan), options.dump_prefill_path, 0);
}

// What the launcher made of the display: the work area it clamps to, the content scale it
// scales by, the window size it would open at, and the face it loaded. Headless, because a DPI
// report that needs a screenshot cannot be checked on a build machine.
int DumpDisplay(const std::string& path) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  SDL_Rect bounds{};
  SDL_GetDisplayUsableBounds(display, &bounds);
  const float content_scale = SDL_GetDisplayContentScale(display);

  const float scale = content_scale > 0.0f ? content_scale : 1.0f;
  const auto to_units = [scale](int points) {
    return static_cast<int>(std::lround(points * scale));
  };
  int want_width = WindowDimension(to_units(kDefaultWidth), to_units(kDefaultWidth),
                                   to_units(kMinWindowWidth));
  int want_height = WindowDimension(to_units(kDefaultHeight), to_units(kDefaultHeight),
                                    to_units(kMinWindowHeight));
  FitToDisplay(&want_width, &want_height);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  const std::string face = LoadUiFont();

  std::string text;
  text += "usable bounds  : " + std::to_string(bounds.w) + "x" + std::to_string(bounds.h) + "\n";
  text += "content scale  : " + std::to_string(content_scale) + "\n";
  text += "ui scale       : " + std::to_string(scale) + "\n";
  text += "default points : " + std::to_string(kDefaultWidth) + "x" +
          std::to_string(kDefaultHeight) + "\n";
  text += "opening window : " + std::to_string(want_width) + "x" + std::to_string(want_height) +
          "\n";
  text += "font base size : " + std::to_string(static_cast<int>(kUiFontSize)) + "\n";
  text += "font face      : " + face + "\n";

  ImGui::DestroyContext();
  SDL_Quit();
  return Emit(text, path, 0);
}

// Where the running executable lives. SDL_GetBasePath returns SDL's own cached buffer
// (thirdparty/sdl3/src/filesystem/SDL_filesystem.c: the CachedBasePath global), so it is not
// the caller's to free and stays valid for the process's life.
fs::path ExecutableDir() {
  const char* base = SDL_GetBasePath();
  return base == nullptr ? fs::path{} : fs::path(base);
}

int WindowDimension(int value, int fallback, int minimum) {
  return value > 0 ? std::clamp(value, minimum, kMaxWindowSize) : fallback;
}

// Keeps the wanted size on screen. The launcher is resizable and its tabs scroll, so this is
// not a layout constraint - it only stops the first frame from opening larger than the display
// work area (a large default on a small panel, for instance).
void FitToDisplay(int* width, int* height) {
  SDL_Rect bounds{};
  if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds)) {
    return;
  }
  if (bounds.w > 0) {
    *width = std::min(*width, static_cast<int>(bounds.w));
  }
  if (bounds.h > 0) {
    *height = std::min(*height, static_cast<int>(bounds.h));
  }
}

// A window sized to the whole work area is a title bar taller than the screen once its frame is
// added, and Windows then withholds the maximize box. Measuring the frame after creation and
// shrinking to fit is what keeps the window maximizable, and keeps its bottom on screen.
//
// The window's minimum size is set in the same place and for the same reason: it is a size the
// frame will refuse to go below, so it has to be a size that fits on this display. A machine
// smaller than the layout's own floor gets the floor it can actually hold.
void FitWindowToWorkArea(SDL_Window* window, int min_width, int min_height) {
  SDL_Rect bounds{};
  if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &bounds)) {
    return;
  }
  int top = 0;
  int left = 0;
  int bottom = 0;
  int right = 0;
  if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right)) {
    return;
  }
  const int max_width = bounds.w - (left + right);
  const int max_height = bounds.h - (top + bottom);
  SDL_SetWindowMinimumSize(window, max_width > 0 ? std::min(min_width, max_width) : min_width,
                           max_height > 0 ? std::min(min_height, max_height) : min_height);
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);
  const int fitted_width = max_width > 0 ? std::min(width, max_width) : width;
  const int fitted_height = max_height > 0 ? std::min(height, max_height) : height;
  if (fitted_width != width || fitted_height != height) {
    SDL_SetWindowSize(window, fitted_width, fitted_height);
  }
}

// Puts the window's whole frame - title bar, borders and all - in the middle of the work area.
//
// SDL positions a window by its *client* area, not by its frame, so centring the client leaves
// the title bar hanging off the top of the screen exactly when the window is as tall as the work
// area, which is the size the fit above just produced. The maximize box lives in that title bar,
// so this is not cosmetic: measuring the frame and placing it is what keeps the window usable.
void CenterWindowFrame(SDL_Window* window) {
  SDL_Rect bounds{};
  if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &bounds)) {
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return;
  }
  int top = 0;
  int left = 0;
  int bottom = 0;
  int right = 0;
  if (!SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right)) {
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return;
  }
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);
  const int frame_x = bounds.x + std::max(0, (bounds.w - (width + left + right)) / 2);
  const int frame_y = bounds.y + std::max(0, (bounds.h - (height + top + bottom)) / 2);
  SDL_SetWindowPosition(window, frame_x + left, frame_y + top);
}

// Loads a neutral UI face from the operating system. Nothing is redistributed with the
// launcher: the font is the machine's, and a machine without any of these falls back to
// ImGui's built-in face rather than failing to start. Returns the face it used, so
// --dump-display can say which one that was.
std::string LoadUiFont() {
  ImGuiIO& io = ImGui::GetIO();
  const char* root = SDL_getenv("SystemRoot");
  const std::string fonts_dir =
      std::string(root != nullptr && root[0] != '\0' ? root : "C:\\Windows") + "\\Fonts\\";
  for (const char* name : {"segoeui.ttf", "tahoma.ttf", "arial.ttf"}) {
    const std::string path = fonts_dir + name;
    std::error_code code;
    if (!fs::exists(path, code)) {
      continue;
    }
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    if (io.Fonts->AddFontFromFileTTF(path.c_str(), kUiFontSize, &config) != nullptr) {
      return name;
    }
  }
  io.Fonts->AddFontDefault();
  return "ImGui's built-in face";
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = ParseOptions(argc, argv);
  if (options.dump_layout) {
    return DumpLayout(options.dump_layout_path);
  }
  if (options.dump_general) {
    return DumpGeneral(options);
  }
  if (options.dump_profile) {
    return DumpProfile(options);
  }
  if (options.dump_prefill) {
    return DumpPrefill(options);
  }
  if (options.dump_display) {
    return DumpDisplay(options.dump_display_path);
  }
  if (options.print_command) {
    return PrintCommand(options);
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // A3's two switches, and the pad a machine with no controller can be tested with. The script is
  // checked here, before there is a window to clean up: one this build cannot parse is worth
  // saying out loud rather than discovering from a pad that never presses anything.
  rb_blitz::launcher::ShellEnvironment shell_environment;
  shell_environment.gamepads = !options.no_gamepad;
  shell_environment.focus_log_path = options.focus_log_path;
  rb_blitz::launcher::VirtualPad test_pad;
  if (!options.test_pad_script.empty() && !test_pad.Start(options.test_pad_script, nullptr)) {
    std::fprintf(stderr, "--test-pad: '%s' is not a script this build understands\n",
                 options.test_pad_script.c_str());
    SDL_Quit();
    return 2;
  }

  // D2's order: the executable's directory and the environment first, then argv on top.
  const fs::path launcher_dir = ExecutableDir();
  rb_blitz::launcher::ProfilePathInputs path_inputs =
      rb_blitz::launcher::ProfilePathInputsFromEnvironment(launcher_dir);
  path_inputs.command_line_value = options.profile_path;
  const fs::path profile_path = rb_blitz::launcher::ResolveProfilePath(path_inputs);

  rb_blitz::launcher::ProfileLoadResult load;
  if (!profile_path.empty()) {
    load = rb_blitz::launcher::LoadProfile(profile_path);
    if (!load.usable()) {
      // A malformed file is left exactly as it is and is never written over (D2). The window
      // still opens, with the compiled defaults, and B4's block says so out loud.
      std::fprintf(stderr, "%s is not usable, leaving it alone: %s\n",
                   profile_path.string().c_str(), load.error.c_str());
    }
  }
  rb_blitz::launcher::ProfileSession session(std::move(path_inputs), std::move(load));
  // A1's window size comes from the profile as it was loaded; B4's block edits the session's own
  // copy, and the geometry on the way out is written from what the file last had (see below).
  const int stored_width = session.profile().window_width;
  const int stored_height = session.profile().window_height;

  // Where the game's data is: the override the game itself takes, then the installer's layout
  // next to the launcher (B1). The General tab reads the Ultimate state from it.
  const rb_blitz::launcher::GameRoots roots =
      rb_blitz::launcher::DetectGameRoots(launcher_dir, options.game_data_root);

  // D4: a first run seeds the General tab from the install manifest instead of making the user
  // retype what the installer wrote down. Nothing is saved here - the profile is left dirty so
  // the user's Save is what creates the file - and a profile that already exists is never
  // touched (a second install shares one per-user file).
  {
    const rb_blitz::launcher::PrefillPlan prefill =
        rb_blitz::launcher::PlanPrefill(launcher_dir, FirstRunInputs(roots, session));
    if (rb_blitz::launcher::ApplyPrefill(session.profile(), prefill)) {
      std::fprintf(stderr, "first run: %s\n", prefill.note.c_str());
    }
  }

  const float display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const float ui_scale = display_scale > 0.0f ? display_scale : 1.0f;

  // SDL sizes a window in the same units the UI is measured in, and the UI is scaled by the
  // display's content scale, so a size kept in points is multiplied by it here. That is what
  // makes the stored number mean the same thing on a 300% display and on a 100% one.
  const auto to_units = [ui_scale](int points) {
    return static_cast<int>(std::lround(points * ui_scale));
  };
  // A stored size wins, so the size the user dragged the window to is the size it opens at. The
  // clamp is only the range the layout is usable in: the profile's own defaults are what a
  // launcher with no stored size opens at, and the work area has the last word on both.
  int want_width = WindowDimension(to_units(stored_width), to_units(kDefaultWidth),
                                   to_units(kMinWindowWidth));
  int want_height = WindowDimension(to_units(stored_height), to_units(kDefaultHeight),
                                    to_units(kMinWindowHeight));
  FitToDisplay(&want_width, &want_height);
  SDL_Window* window =
      SDL_CreateWindow(kWindowTitle, want_width, want_height,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    Fatal("SDL_CreateWindow");
    SDL_Quit();
    return 1;
  }
  // The floor the frame's own resize handling enforces, so the window cannot be dragged smaller
  // than the layout can be read at. It is a minimum size and not a fixed one: maximizing and
  // dragging both still work, which is the point.
  FitWindowToWorkArea(window, to_units(kMinWindowWidth), to_units(kMinWindowHeight));
  CenterWindowFrame(window);

  SDL_GPUDevice* gpu_device =
      SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                              SDL_GPU_SHADERFORMAT_METALLIB,
                          /*debug_mode=*/false, /*name=*/nullptr);
  if (gpu_device == nullptr) {
    Fatal("SDL_CreateGPUDevice");
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  if (!SDL_ClaimWindowForGPUDevice(gpu_device, window)) {
    Fatal("SDL_ClaimWindowForGPUDevice");
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetGPUSwapchainParameters(gpu_device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                SDL_GPU_PRESENTMODE_VSYNC);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  // The focus ring is the launcher's own (A1, D6), so ImGui's built-in navigation stays
  // off: two rings would fight over every arrow key.
  // Window geometry belongs to the profile the launcher owns (D2), so ImGui must not write
  // its own imgui.ini into whatever directory the launcher was started from.
  io.IniFilename = nullptr;

  LoadUiFont();
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  // Roomier than ImGui's defaults. This is a settings dialog read at a glance, not a dense tool
  // window, so rows, buttons and the strip get air between them. Set before ScaleAllSizes so the
  // display's content scale multiplies these values too.
  style.WindowPadding = ImVec2(18.0f, 16.0f);
  style.FramePadding = ImVec2(12.0f, 7.0f);
  style.ItemSpacing = ImVec2(12.0f, 12.0f);
  style.ItemInnerSpacing = ImVec2(10.0f, 8.0f);
  style.IndentSpacing = 24.0f;
  style.ScrollbarSize = 18.0f;
  style.GrabMinSize = 14.0f;
  style.ScaleAllSizes(ui_scale);
  style.FontScaleDpi = ui_scale;

  ImGui_ImplSDL3_InitForSDLGPU(window);
  ImGui_ImplSDLGPU3_InitInfo init_info = {};
  init_info.Device = gpu_device;
  init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu_device, window);
  init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
  init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
  ImGui_ImplSDLGPU3_Init(&init_info);

  SDL_ShowWindow(window);
  // The launcher opens expanded, which is the size its content wants: every tab fits without
  // scrolling at 100%, and a maximized window is what a user who opens it on a small panel would
  // drag it to anyway. It is maximized *after* the window is shown, because maximizing a hidden
  // window is a request Windows answers when the window appears - which it did not do here, and
  // the window ended up merely clamped to the work area with the maximize box already used.
  // Un-maximizing still lands on the stored size below, so what the profile remembers is the
  // restored geometry and never the work area.
  SDL_MaximizeWindow(window);

  // The facts a row's `visible` rule is decided from, read once: a machine with one display has
  // no monitor to choose between, so the Monitor row is not shown at all.
  int display_count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
  rb_blitz::launcher::RowEnvironment environment;
  environment.multiple_monitors = displays != nullptr && display_count > 1;
  SDL_free(displays);

  std::unique_ptr<rb_blitz::launcher::Shell> shell =
      std::make_unique<rb_blitz::launcher::Shell>(std::move(session), roots, environment,
                                                  shell_environment);

  // The title names the current tab. It is the only piece of the launcher's state a script
  // can read back (MainWindowTitle), which is what makes A1's "tab through every tab"
  // checkable rather than eyeballed - and it tells the user where they are in the taskbar.
  std::string window_title;
  const auto sync_title = [&]() {
    const std::string wanted =
        std::string(kWindowTitle) + " - " +
        rb_blitz::launcher::DisplayTabName(shell->CurrentTab());
    if (wanted != window_title) {
      window_title = wanted;
      SDL_SetWindowTitle(window, window_title.c_str());
    }
  };
  sync_title();

  bool done = false;
  while (!done) {
    // The scripted pad is pressed before the event pump rather than after: SDL applies a virtual
    // joystick's state when it is pumped, so a press set here is one this same frame can see.
    if (!test_pad.finished()) {
      test_pad.Update(static_cast<double>(SDL_GetTicks()) / 1000.0);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
           event.window.windowID == SDL_GetWindowID(window))) {
        done = true;
      }
    }
    if (done) {
      break;
    }

    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
      SDL_Delay(10);
      continue;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Escape and B leave from inside the shell, because the keys are the focus model's to
    // read (A1); the window's close button is the event loop's.
    const bool running = shell->Frame();
    sync_title();

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (command_buffer == nullptr) {
      Fatal("SDL_AcquireGPUCommandBuffer");
      break;
    }
    SDL_GPUTexture* swapchain_texture = nullptr;
    SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, nullptr,
                                          nullptr);
    if (swapchain_texture != nullptr && !minimized) {
      // Mandatory before the render pass: uploads the vertex and index buffers.
      ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

      SDL_GPUColorTargetInfo target_info = {};
      target_info.texture = swapchain_texture;
      target_info.clear_color = SDL_FColor{0.09f, 0.09f, 0.11f, 1.0f};
      target_info.load_op = SDL_GPU_LOADOP_CLEAR;
      target_info.store_op = SDL_GPU_STOREOP_STORE;
      SDL_GPURenderPass* render_pass =
          SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);
      ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
      SDL_EndGPURenderPass(render_pass);
    }
    SDL_SubmitGPUCommandBuffer(command_buffer);

    if (!running) {
      break;
    }
  }

  SDL_WaitForGPUIdle(gpu_device);
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();

  // The pads are the shell's, and SDL's gamepad subsystem is about to be shut down with SDL_Quit:
  // closing a pad after that would be touching a handle SDL has already freed, so the shell - and
  // the pad handles it owns - go first.

  // A1's geometry persistence (D2), now through B4's session: the size, and only when it
  // changed, so a launcher the user never resized neither creates the profile nor touches its
  // mtime. What is written is the profile as the file last had it, so a setting the user changed
  // and did not save is not written by the back door either - the panel called it unsaved, and it
  // stays unsaved.
  {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    // A maximized window reports the work area, not the size the user chose: writing that back
    // would replace the remembered geometry with "as big as this display", so the restored size
    // would be lost. The launcher opens expanded every time anyway, so there is nothing to
    // record while it is.
    const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    // Back to the points the profile keeps, so the number survives a different-DPI monitor.
    const int point_width = static_cast<int>(std::lround(width / ui_scale));
    const int point_height = static_cast<int>(std::lround(height / ui_scale));
    const rb_blitz::launcher::SaveOutcome outcome =
        maximized ? rb_blitz::launcher::SaveOutcome{}
                  : shell->session().SaveWindowGeometry(point_width, point_height);
    if (!outcome.ok) {
      std::fprintf(stderr, "could not save the window size to %s: %s\n",
                   shell->session().path().string().c_str(), outcome.error.c_str());
    }
  }

  SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
  SDL_DestroyGPUDevice(gpu_device);
  SDL_DestroyWindow(window);
  // The shell's pads are closed by this, and the virtual pad is taken away, both before the gamepad
  // subsystem goes: the geometry above is the last thing that needs the shell.
  shell.reset();
  test_pad.Stop();
  SDL_Quit();
  return 0;
}
