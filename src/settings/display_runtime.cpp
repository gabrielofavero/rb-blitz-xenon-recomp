// SPDX-License-Identifier: GPL-2.0-only
#include "settings/display_runtime.h"

#include <condition_variable>
#include <mutex>

#include "settings/runtime_settings.h"
#include <rex/ui/ui_system.h>
#include <rex/ui/native_window_host.h>
#include <SDL3/SDL.h>
#include "settings/runtime_values.h"

#include "settings/store.h"

namespace rb_blitz::settings {
namespace {

bool ValidMode(std::string_view token) {
  return token == "windowed" || token == "borderless" || token == "fullscreen";
}

std::filesystem::path MarkerPath(const std::filesystem::path& settings_file) {
  auto result = settings_file;
  result += ".display-preview.toml";
  return result;
}

bool WriteMarker(const std::filesystem::path& settings_file, std::string_view previous,
                 std::string_view requested, std::string* error) {
  toml::Document document;
  document.Set("", "schema_version", "1");
  document.Set("", "previous", toml::RenderBasic(std::string(previous)));
  document.Set("", "requested", toml::RenderBasic(std::string(requested)));
  return WriteFileAtomic(MarkerPath(settings_file), document.Render(), error);
}
}  // namespace

namespace {
std::string HostDisplayMode(const rex::ui::INativeWindowHost& host) {
  const auto mode = host.GetDisplayMode();
  if (!mode) return {};
  switch (*mode) {
    case rex::ui::NativeHostDisplayMode::kWindowed: return "windowed";
    case rex::ui::NativeHostDisplayMode::kBorderless: return "borderless";
    case rex::ui::NativeHostDisplayMode::kFullscreen: return "fullscreen";
  }
  return {};
}
rex::ui::NativeHostDisplayMode HostMode(std::string_view token) {
  if (token == "fullscreen") return rex::ui::NativeHostDisplayMode::kFullscreen;
  if (token == "borderless") return rex::ui::NativeHostDisplayMode::kBorderless;
  return rex::ui::NativeHostDisplayMode::kWindowed;
}
std::string NativeDisplayMode(SDL_Window* window) {
  if (!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)) return "windowed";
  return SDL_GetWindowFullscreenMode(window) ? "fullscreen" : "borderless";
}
}

std::string RuntimeDisplayMode(rex::ui::IUISystem& context) {
  std::string mode;
  if (!context.RunSynchronous([&] {
    if (const auto host = rex::ui::GetNativeWindowHost()) {
      mode = HostDisplayMode(*host);
      return;
    }
    const auto native=context.GetNativeWindow();
    if (native && native->sdl) mode=NativeDisplayMode(static_cast<SDL_Window*>(native->sdl));
  })) return {};
  return mode;
}

namespace {
bool ApplyDisplay(rex::ui::IUISystem& context, std::string_view token,
                  bool write_settings, std::string* error) {
  if (!ValidMode(token)) {
    if (error) *error="unknown display mode";
    return false;
  }
  bool applied=false;
  std::string failure;
  const bool dispatched=context.RunSynchronous([&] {
    const auto native=context.GetNativeWindow();
    if (!native || !native->sdl) { failure="native display window is unavailable"; return; }
    auto* window=static_cast<SDL_Window*>(native->sdl);
    const bool fullscreen=token!="windowed";
    const bool exclusive=token=="fullscreen";
    const char* fullscreen_text=fullscreen ? "true" : "false";
    const char* exclusive_text=exclusive ? "true" : "false";
    if (write_settings) for (const auto* name : {"ui.fullscreen","ui.fullscreen_exclusive"}) {
      auto setting=REX_SETTINGS().Find(name);
      if (!setting || (*setting)->IsFrozen() || (*setting)->GetLayer()==rex::SettingLayer::kLock) {
        failure="display settings cannot change";
        return;
      }
    }
    const auto old_fullscreen=InspectRuntimeSetting("ui.fullscreen").active_value;
    const auto old_exclusive=InspectRuntimeSetting("ui.fullscreen_exclusive").active_value;
    // An embedded rendering child never owns the top-level fullscreen state.
    // Require an acknowledged native mode before changing the settings snapshot.
    if (const auto host = rex::ui::GetNativeWindowHost()) {
      const auto old_mode = HostDisplayMode(*host);
      if (!ValidMode(old_mode)) { failure = "native host display state is unavailable"; return; }
      const auto restore_host = [&] {
        return host->SetDisplayMode(HostMode(old_mode)) && HostDisplayMode(*host) == old_mode;
      };
      if (!host->SetDisplayMode(HostMode(token)) || HostDisplayMode(*host) != token) {
        failure = restore_host() ? "native host refused the mode; the window was restored"
                                 : "native host refused the mode and rollback";
        return;
      }
      const bool wrote_fullscreen = !write_settings || WriteRuntimeSetting("ui.fullscreen", fullscreen_text,
          rex::SettingLayer::kRuntime).accepted();
      const bool wrote_exclusive = !write_settings || WriteRuntimeSetting("ui.fullscreen_exclusive", exclusive_text,
          rex::SettingLayer::kRuntime).accepted();
      if (write_settings && (!wrote_fullscreen || !wrote_exclusive ||
          InspectRuntimeSetting("ui.fullscreen").active_value != fullscreen_text ||
          InspectRuntimeSetting("ui.fullscreen_exclusive").active_value != exclusive_text)) {
        WriteRuntimeSetting("ui.fullscreen", old_fullscreen, rex::SettingLayer::kRuntime);
        WriteRuntimeSetting("ui.fullscreen_exclusive", old_exclusive, rex::SettingLayer::kRuntime);
        failure = restore_host() ? "display settings refused the mode; the window was restored"
                                 : "display settings refused the mode and native rollback";
        return;
      }
      SDL_Event exposed{};
      exposed.type = SDL_EVENT_WINDOW_EXPOSED;
      exposed.window.windowID = SDL_GetWindowID(window);
      SDL_PushEvent(&exposed);
      applied = true;
      return;
    }
    const std::string old_mode=NativeDisplayMode(window);
    const SDL_DisplayMode* old_selected=SDL_GetWindowFullscreenMode(window);
    const SDL_DisplayMode previous=old_selected ? *old_selected : SDL_DisplayMode{};
    SDL_DisplayMode selected{};
    if (exclusive) {
      const auto requested=context.GetVideoMode();
      if (!SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(window),
          int(requested.width),int(requested.height),requested.refresh_rate,true,&selected)) {
        failure="the display has no suitable exclusive mode";
        return;
      }
    }
    const auto restore_native=[&] {
      const bool selected=SDL_SetWindowFullscreenMode(window,old_selected ? &previous : nullptr);
      const bool toggled=SDL_SetWindowFullscreen(window,old_mode!="windowed");
      const bool synced=SDL_SyncWindow(window);
      return selected && toggled && synced && NativeDisplayMode(window)==old_mode;
    };
    if (!SDL_SetWindowFullscreenMode(window,exclusive ? &selected : nullptr) ||
        !SDL_SetWindowFullscreen(window,fullscreen) || !SDL_SyncWindow(window) ||
        NativeDisplayMode(window)!=token) {
      failure=restore_native() ? "SDL refused the display transition; the window was restored"
                               : "SDL refused the display transition and native rollback";
      return;
    }
    const bool wrote_fullscreen=!write_settings || WriteRuntimeSetting("ui.fullscreen",fullscreen_text,rex::SettingLayer::kRuntime).accepted();
    const bool wrote_exclusive=!write_settings || WriteRuntimeSetting("ui.fullscreen_exclusive",exclusive_text,rex::SettingLayer::kRuntime).accepted();
    if (write_settings && (!wrote_fullscreen || !wrote_exclusive ||
        InspectRuntimeSetting("ui.fullscreen").active_value!=fullscreen_text ||
        InspectRuntimeSetting("ui.fullscreen_exclusive").active_value!=exclusive_text)) {
      WriteRuntimeSetting("ui.fullscreen",old_fullscreen,rex::SettingLayer::kRuntime);
      WriteRuntimeSetting("ui.fullscreen_exclusive",old_exclusive,rex::SettingLayer::kRuntime);
      failure=restore_native() ? "display settings refused the mode; the window was restored"
                               : "display settings refused the mode and native rollback";
      return;
    }
    SDL_Event exposed{};
    exposed.type=SDL_EVENT_WINDOW_EXPOSED;
    exposed.window.windowID=SDL_GetWindowID(window);
    SDL_PushEvent(&exposed);
    applied=true;
  });
  if (!dispatched) failure="UI thread is no longer available";
  if (!applied && error) *error=failure;
  return applied;
}

 }  // namespace

bool ApplyRuntimeDisplay(rex::ui::IUISystem& context, std::string_view token, std::string* error) {
  return ApplyDisplay(context, token, true, error);
}

bool ApplyTransientDisplay(rex::ui::IUISystem& context, std::string_view token, std::string* error) {
  return ApplyDisplay(context, token, false, error);
}

bool ReadUnconfirmedDisplayPreview(const std::filesystem::path& settings_file,
                                   std::string* previous, bool* pending, std::string* error) {
  if (!previous || !pending || settings_file.empty()) {
    if (error) *error = "invalid display preview recovery arguments";
    return false;
  }
  *pending = false;
  previous->clear();
  std::string text;
  bool exists = false;
  if (!ReadFileText(MarkerPath(settings_file), &text, &exists)) {
    if (error) *error = "cannot read display preview marker";
    return false;
  }
  if (!exists) return true;
  bool parsed = false;
  const auto document = toml::Document::Parse(text, &parsed, error);
  if (!parsed || document.Get("", "schema_version").value != "1" ||
      !ValidMode(document.Get("", "previous").value) ||
      !ValidMode(document.Get("", "requested").value)) {
    if (error) *error = "unrecognized display preview marker; left untouched";
    return false;
  }
  *previous = document.Get("", "previous").value;
  *pending = true;
  return true;
}

bool ClearUnconfirmedDisplayPreview(const std::filesystem::path& settings_file,
                                    std::string* error) {
  if (settings_file.empty()) {
    if (error) *error = "empty settings path for display preview marker";
    return false;
  }
  std::error_code ec;
  std::filesystem::remove(MarkerPath(settings_file), ec);
  if (ec) {
    if (error) *error = "cannot remove display preview marker: " + ec.message();
    return false;
  }
  return true;
}

struct DisplayPreviewTransaction::State {
  State(rex::ui::IUISystem& ui_context,
        std::filesystem::path file)
      : context(ui_context), settings_file(std::move(file)) {}
  mutable std::mutex mutex;
  rex::ui::IUISystem& context;
  std::filesystem::path settings_file;
  std::string previous;
  bool active = false;
  bool transitioning = false;
  uint64_t generation = 0;
  std::chrono::steady_clock::time_point deadline{};
};

DisplayPreviewTransaction::DisplayPreviewTransaction(rex::ui::IUISystem& context,
                                                     std::filesystem::path settings_file,
                                                     std::chrono::steady_clock::duration timeout)
    : state_(std::make_shared<State>(context, std::move(settings_file))),
      duration_(timeout) {}

DisplayPreviewTransaction::~DisplayPreviewTransaction() {
  timeout_.request_stop();
  if (timeout_.joinable()) timeout_.join();
  // An abandoned preview is deliberately left marked. The next boot restores
  // the previous choice even if shutdown could not reach the UI thread.
  std::lock_guard lock(state_->mutex);
  state_->active = false;
  ++state_->generation;
}

bool DisplayPreviewTransaction::Begin(std::string_view previous, std::string_view requested,
                                      std::string* error) {
  if (!ValidMode(previous) || !ValidMode(requested) || previous == requested) {
    if (error) *error = "invalid display preview transition";
    return false;
  }
  {
    std::lock_guard lock(state_->mutex);
    if (state_->active || state_->transitioning) {
      if (error) *error = "a display preview is already active";
      return false;
    }
    state_->transitioning = true;
  }
  timeout_.request_stop();
  if (timeout_.joinable()) timeout_.join();
  std::string unconfirmed_previous;
  bool unconfirmed = false;
  if (!ReadUnconfirmedDisplayPreview(state_->settings_file, &unconfirmed_previous,
                                     &unconfirmed, error)) {
    std::lock_guard lock(state_->mutex);
    state_->transitioning = false;
    return false;
  }
  if (unconfirmed) {
    if (error) *error = "an unconfirmed display preview must be recovered first";
    std::lock_guard lock(state_->mutex);
    state_->transitioning = false;
    return false;
  }
  const bool marked = WriteMarker(state_->settings_file, previous, requested, error);
  const bool applied = marked && ApplyRuntimeDisplay(state_->context, requested, error);
  if (!applied) {
    // The native transition may have partially succeeded even when its
    // rollback failed. Preserve the marker until boot recovery proves the
    // old mode can present again.
    std::lock_guard lock(state_->mutex);
    state_->transitioning = false;
    return false;
  }
  uint64_t generation;
  std::chrono::steady_clock::time_point deadline;
  {
    std::lock_guard lock(state_->mutex);
    state_->previous = previous;
    state_->active = true;
    state_->transitioning = false;
    generation = ++state_->generation;
    state_->deadline = std::chrono::steady_clock::now() + duration_;
    deadline = state_->deadline;
  }
  std::weak_ptr<State> weak_state = state_;
  rex::ui::IUISystem* context = &state_->context;
  timeout_ = std::jthread([weak_state, context, deadline, generation](std::stop_token stop) {
    std::condition_variable_any condition;
    std::mutex mutex;
    std::unique_lock wait_lock(mutex);
    condition.wait_until(wait_lock, stop, deadline, [] { return false; });
    while (!stop.stop_requested()) {
      // This callback owns no transaction object. Shutdown can invalidate
      // State while a UI callback is queued. A save in progress may delay the
      // rollback, so keep retrying until Keep or Revert ends the generation.
      context->PostTask([weak_state, generation] {
            if (auto state = weak_state.lock()) {
              std::string ignored;
              RevertState(state, generation, &ignored);
            }
          });
      if (auto state = weak_state.lock()) {
        std::lock_guard lock(state->mutex);
        if (!state->active || state->generation != generation) return;
      } else {
        return;
      }
      condition.wait_for(wait_lock, stop, std::chrono::milliseconds(100), [] { return false; });
    }
  });
  return true;
}

bool DisplayPreviewTransaction::Keep(bool presented, const Persist& persist, std::string* error) {
  if (!presented || !persist) {
    if (error) *error = "display preview has not presented or has no durable save";
    return false;
  }
  uint64_t generation;
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->active || state_->transitioning) {
      if (error) *error = "no active display preview";
      return false;
    }
    if (std::chrono::steady_clock::now() >= state_->deadline) {
      if (error) *error = "display preview expired";
      return false;
    }
    state_->transitioning = true;
    generation = state_->generation;
  }
  // Clear the recovery marker before making the requested mode durable. If the
  // process stops between these steps, the still-saved previous mode is the
  // next boot's choice. If marker removal fails, leave the preview active and
  // do not save a mode that recovery would override on the next boot.
  if (!ClearUnconfirmedDisplayPreview(state_->settings_file, error)) {
    std::lock_guard lock(state_->mutex);
    state_->transitioning = false;
    return false;
  }
  const bool saved = persist(error);
  if (!saved) {
    {
      std::lock_guard lock(state_->mutex);
      state_->transitioning = false;
    }
    std::string rollback_error;
    if (!RevertState(state_, generation, &rollback_error) && error) {
      if (!error->empty()) *error += "; ";
      *error += "display rollback failed: " + rollback_error;
    }
    return false;
  }
  {
    std::lock_guard lock(state_->mutex);
    if (generation != state_->generation) {
      state_->transitioning = false;
      if (error) *error = "display preview generation changed during save";
      return false;
    }
    state_->active = false;
    ++state_->generation;
  }
  timeout_.request_stop();
  {
    std::lock_guard lock(state_->mutex);
    state_->transitioning = false;
  }
  return true;
}

bool DisplayPreviewTransaction::RevertState(const std::shared_ptr<State>& state,
                                            uint64_t generation, std::string* error) {
  std::string previous;
  {
    std::lock_guard lock(state->mutex);
    if (!state->active || generation != state->generation) return true;
    if (state->transitioning) return false;
    state->transitioning = true;
    previous = state->previous;
  }
  const bool applied = ApplyRuntimeDisplay(state->context, previous, error);
  {
    std::lock_guard lock(state->mutex);
    if (!applied) {
      state->transitioning = false;
      return false;
    }
  }
  const bool cleared = ClearUnconfirmedDisplayPreview(state->settings_file, error);
  {
    std::lock_guard lock(state->mutex);
    if (cleared) {
      state->active = false;
      ++state->generation;
    }
    state->transitioning = false;
  }
  return cleared;
}

bool DisplayPreviewTransaction::Revert(std::string* error) {
  uint64_t generation;
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->active) return true;
    generation = state_->generation;
  }
  const bool reverted = RevertState(state_, generation, error);
  if (reverted) timeout_.request_stop();
  return reverted;
}

bool DisplayPreviewTransaction::active() const {
  std::lock_guard lock(state_->mutex);
  return state_->active;
}

std::chrono::steady_clock::time_point DisplayPreviewTransaction::deadline() const {
  std::lock_guard lock(state_->mutex);
  return state_->deadline;
}

}  // namespace rb_blitz::settings
