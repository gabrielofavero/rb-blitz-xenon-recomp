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

#include "general_report.h"
#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "schema_view.h"
#include "shell.h"
#include "ultimate_state.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr const char* kWindowTitle = "rb_blitz launcher";

// The profile's own defaults (src/launcher/profile.h), used when a stored size is missing or
// nonsense. SDL window sizes are logical points, and so is what the profile stores: a DPI
// change moves the pixels the user sees, not the number in the file.
constexpr int kDefaultWidth = 1100;
constexpr int kDefaultHeight = 720;
constexpr int kMinWindowSize = 640;
constexpr int kMaxWindowSize = 8192;

namespace fs = std::filesystem;

// Defined with the rest of the path helpers below; the dump modes need it before the loop.
fs::path ExecutableDir();

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
};

Options ParseOptions(int argc, char** argv) {
  Options options;
  constexpr std::string_view kProfilePrefix = "--launcher_profile=";
  constexpr std::string_view kGameDataPrefix = "--game_data_root=";
  constexpr std::string_view kDumpPrefix = "--dump-layout=";
  constexpr std::string_view kDumpGeneralPrefix = "--dump-general=";
  constexpr std::string_view kDumpProfilePrefix = "--dump-profile=";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--dump-layout") {
      options.dump_layout = true;
    } else if (argument == "--dump-general") {
      options.dump_general = true;
    } else if (argument == "--dump-profile") {
      options.dump_profile = true;
    } else if (argument.size() > kDumpProfilePrefix.size() &&
               argument.substr(0, kDumpProfilePrefix.size()) == kDumpProfilePrefix) {
      options.dump_profile = true;
      options.dump_profile_path = std::string(argument.substr(kDumpProfilePrefix.size()));
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

// Where the running executable lives. SDL_GetBasePath returns SDL's own cached buffer
// (thirdparty/sdl3/src/filesystem/SDL_filesystem.c: the CachedBasePath global), so it is not
// the caller's to free and stays valid for the process's life.
fs::path ExecutableDir() {
  const char* base = SDL_GetBasePath();
  return base == nullptr ? fs::path{} : fs::path(base);
}

int WindowDimension(int value, int fallback) {
  return value > 0 ? std::clamp(value, kMinWindowSize, kMaxWindowSize) : fallback;
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

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
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

  const float display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const float ui_scale = display_scale > 0.0f ? display_scale : 1.0f;

  SDL_Window* window = SDL_CreateWindow(
      kWindowTitle, WindowDimension(stored_width, kDefaultWidth),
      WindowDimension(stored_height, kDefaultHeight),
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == nullptr) {
    Fatal("SDL_CreateWindow");
    SDL_Quit();
    return 1;
  }
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

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

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
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

  std::unique_ptr<rb_blitz::launcher::Shell> shell =
      std::make_unique<rb_blitz::launcher::Shell>(std::move(session), roots);

  // The title names the current tab. It is the only piece of the launcher's state a script
  // can read back (MainWindowTitle), which is what makes A1's "tab through every tab"
  // checkable rather than eyeballed - and it tells the user where they are in the taskbar.
  std::string window_title;
  const auto sync_title = [&]() {
    const std::string wanted =
        std::string(kWindowTitle) + " - " +
        std::string(rb_blitz::launcher::settings::TabName(shell->CurrentTab()));
    if (wanted != window_title) {
      window_title = wanted;
      SDL_SetWindowTitle(window, window_title.c_str());
    }
  };
  sync_title();

  bool done = false;
  while (!done) {
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

  // A1's geometry persistence (D2), now through B4's session: the size, and only when it
  // changed, so a launcher the user never resized neither creates the profile nor touches its
  // mtime. What is written is the profile as the file last had it, so a setting the user changed
  // and did not save is not written by the back door either - the panel called it unsaved, and it
  // stays unsaved.
  {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const rb_blitz::launcher::SaveOutcome outcome =
        shell->session().SaveWindowGeometry(width, height);
    if (!outcome.ok) {
      std::fprintf(stderr, "could not save the window size to %s: %s\n",
                   shell->session().path().string().c_str(), outcome.error.c_str());
    }
  }

  SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
  SDL_DestroyGPUDevice(gpu_device);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
