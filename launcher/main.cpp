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

#include "launcher/profile.h"
#include "launcher/profile_path.h"
#include "schema_view.h"
#include "shell.h"

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
  // --dump-layout[=<path>] prints the rows the shell would draw and leaves, before any
  // window is created. It is A1's headless evidence that every schema row reaches a tab,
  // and the part of the launcher a build machine can run. With no path it writes to stdout;
  // with one it writes the file, because a WIN32-subsystem process has no console of its
  // own to be captured from.
  bool dump_layout = false;
  std::string dump_layout_path;
};

Options ParseOptions(int argc, char** argv) {
  Options options;
  constexpr std::string_view kProfilePrefix = "--launcher_profile=";
  constexpr std::string_view kDumpPrefix = "--dump-layout=";
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--dump-layout") {
      options.dump_layout = true;
    } else if (argument.size() > kDumpPrefix.size() &&
               argument.substr(0, kDumpPrefix.size()) == kDumpPrefix) {
      options.dump_layout = true;
      options.dump_layout_path = std::string(argument.substr(kDumpPrefix.size()));
    } else if (argument.size() >= kProfilePrefix.size() &&
               argument.substr(0, kProfilePrefix.size()) == kProfilePrefix) {
      options.profile_path = std::string(argument.substr(kProfilePrefix.size()));
    }
    // Anything else is the game's business, not the launcher's: the launch contract is
    // one-way (Contract 3), so an unknown switch here is not fatal.
  }
  return options;
}

int DumpLayout(const std::string& path) {
  const std::string text = rb_blitz::launcher::DescribeLayout(rb_blitz::launcher::BuildLayout());
  const bool complete = text.find("MISSING-TOOLTIP") == std::string::npos;
  const std::string all =
      text + (complete ? "every row carries a tooltip\n" : "a row is missing its tooltip\n");
  if (path.empty()) {
    std::fputs(all.c_str(), stdout);
    return complete ? 0 : 1;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    std::fprintf(stderr, "cannot write %s\n", path.c_str());
    return 2;
  }
  file << all;
  return file.good() && complete ? 0 : 1;
}

fs::path ExecutableDir() {
  const char* base = SDL_GetBasePath();
  if (base == nullptr) {
    return {};
  }
  const fs::path dir(base);
  SDL_free(const_cast<char*>(base));
  return dir;
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

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // D2's order: the executable's directory and the environment first, then argv on top.
  rb_blitz::launcher::ProfilePathInputs path_inputs =
      rb_blitz::launcher::ProfilePathInputsFromEnvironment(ExecutableDir());
  path_inputs.command_line_value = options.profile_path;
  const fs::path profile_path = rb_blitz::launcher::ResolveProfilePath(path_inputs);

  rb_blitz::launcher::Profile profile;
  bool profile_writable = true;
  if (!profile_path.empty()) {
    const rb_blitz::launcher::ProfileLoadResult loaded =
        rb_blitz::launcher::LoadProfile(profile_path);
    profile = loaded.profile;
    // A malformed file is left exactly as it is and never written over (D2). The window
    // still opens, with the compiled defaults, and the geometry is simply not saved.
    profile_writable = loaded.usable();
    if (!profile_writable) {
      std::fprintf(stderr, "%s is not usable, leaving it alone: %s\n",
                   profile_path.string().c_str(), loaded.error.c_str());
    }
  }

  const float display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const float ui_scale = display_scale > 0.0f ? display_scale : 1.0f;

  SDL_Window* window = SDL_CreateWindow(
      kWindowTitle, WindowDimension(profile.window_width, kDefaultWidth),
      WindowDimension(profile.window_height, kDefaultHeight),
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
      std::make_unique<rb_blitz::launcher::Shell>();

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

  // A1's geometry persistence (D2): the size, and only when it changed, so a launcher the
  // user never resized neither creates the profile nor touches its mtime. Logical points,
  // because that is what the profile stores.
  if (profile_writable && !profile_path.empty()) {
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (width > 0 && height > 0 &&
        (width != profile.window_width || height != profile.window_height)) {
      profile.window_width = width;
      profile.window_height = height;
      std::string error;
      if (!rb_blitz::launcher::SaveProfile(profile_path, profile, &error)) {
        std::fprintf(stderr, "could not save the window size to %s: %s\n",
                     profile_path.string().c_str(), error.c_str());
      }
    }
  }

  SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
  SDL_DestroyGPUDevice(gpu_device);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
