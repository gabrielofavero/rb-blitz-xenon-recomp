// rb_blitz launcher - entry point (docs/plans/launcher-plan.md, prompt P0.4).
//
// This is the walking skeleton and nothing more: it opens the launcher's window, draws
// a placeholder, and leaves through one of the three exits the rest of the plan assumes
// (Esc, B, or closing the window). No settings, no tabs, no game launch - P0.4 is only
// allowed to answer "does the SDL3 + ImGui stack work in this build tree".
//
// Renderer backend: SDL_GPU, via imgui_impl_sdlgpu3. D1 prefers it because this build
// turns SDL_RENDER off on purpose (rexglue-sdk/thirdparty/CMakeLists.txt) and SDL_GPU is
// core SDL3, already compiled into the SDL3-static the game links - so the launcher
// pulls in no renderer, no D3D12 device of its own, and no emulator runtime (D1: it does
// not link rex::runtime).

#include <SDL3/SDL.h>
// SDL.h does not pull SDL_main.h in (SDL 3.x), so ask for it explicitly: on Windows it
// supplies the WinMain the WIN32 subsystem needs and renames main() to SDL_main().
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include <cstdio>

namespace {

constexpr const char* kWindowTitle = "rb_blitz launcher";
constexpr const char* kBackendName = "SDL_GPU";

// A WIN32-subsystem executable has no console, so a bring-up failure would otherwise be
// silent. Name the step that failed and let SDL say why.
void Fatal(const char* step) {
    char message[512];
    std::snprintf(message, sizeof(message), "%s failed:\n%s", step, SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, message, nullptr);
}

// Esc and B are the two keys P0.4 promises; the window-close and quit events are handled
// beside them. A pressed state is read from the event, not from ImGui, because there is
// no focus model yet (that is A1/A3).
bool IsExitKey(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
        return false;
    }
    return event.key.key == SDLK_ESCAPE || event.key.key == SDLK_B;
}

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const float display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const float ui_scale = display_scale > 0.0f ? display_scale : 1.0f;

    SDL_Window* window = SDL_CreateWindow(
        kWindowTitle, static_cast<int>(1280 * ui_scale), static_cast<int>(720 * ui_scale),
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // Window geometry belongs to the profile the launcher owns (D2), so ImGui must not
    // write its own imgui.ini into whatever directory the launcher was started from.
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

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window)) ||
                IsExitKey(event)) {
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

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("rb_blitz launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("rb_blitz launcher");
        ImGui::Separator();
        ImGui::Text("Bring-up build: window, %s backend, no settings yet.", kBackendName);
        ImGui::Text("Esc, B, or the window's close button exits.");
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::End();

        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool minimized =
            draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;

        SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(gpu_device);
        if (command_buffer == nullptr) {
            Fatal("SDL_AcquireGPUCommandBuffer");
            break;
        }
        SDL_GPUTexture* swapchain_texture = nullptr;
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture,
                                              nullptr, nullptr);
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
    }

    SDL_WaitForGPUIdle(gpu_device);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(gpu_device, window);
    SDL_DestroyGPUDevice(gpu_device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
