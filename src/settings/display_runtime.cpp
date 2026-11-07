// SPDX-License-Identifier: GPL-2.0-only
#include "settings/display_runtime.h"

#include <condition_variable>
#include <mutex>

#include <rex/cvar.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

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

bool ApplyRuntimeDisplay(rex::ui::WindowedAppContext& context, rex::ui::Window& window,
                         std::string_view token, std::string* error) {
  rex::ui::Window::DisplayMode mode;
  if (token == "windowed") {
    mode = rex::ui::Window::DisplayMode::kWindowed;
  } else if (token == "borderless") {
    mode = rex::ui::Window::DisplayMode::kBorderless;
  } else if (token == "fullscreen") {
    mode = rex::ui::Window::DisplayMode::kFullscreen;
  } else {
    if (error) *error = "unknown display mode";
    return false;
  }
  bool applied = false;
  std::string failure;
  const bool dispatched = context.CallInUIThreadSynchronous([&] {
    const std::string old_fullscreen = rex::cvar::GetFlagByName("fullscreen");
    const std::string old_borderless = rex::cvar::GetFlagByName("borderless");
    if (old_fullscreen.empty() || old_borderless.empty()) {
      failure = "display cvars are unavailable";
      return;
    }
    const auto old_mode = window.GetDisplayMode();
    if (!window.SetDisplayMode(mode)) {
      failure = "SDL refused the display transition";
      return;
    }
    const char* fullscreen = mode == rex::ui::Window::DisplayMode::kFullscreen ? "true" : "false";
    const char* borderless = mode == rex::ui::Window::DisplayMode::kBorderless ? "true" : "false";
    const bool wrote_fullscreen = rex::cvar::SetFlagByName("fullscreen", fullscreen);
    const bool wrote_borderless = rex::cvar::SetFlagByName("borderless", borderless);
    if (!wrote_fullscreen || !wrote_borderless ||
        rex::cvar::GetFlagByName("fullscreen") != fullscreen ||
        rex::cvar::GetFlagByName("borderless") != borderless) {
      rex::cvar::SetFlagByName("fullscreen", old_fullscreen);
      rex::cvar::SetFlagByName("borderless", old_borderless);
      const bool restored = window.SetDisplayMode(old_mode);
      failure = restored ? "display cvars refused the mode; native window was restored"
                         : "display cvars refused the mode; native rollback also failed";
      return;
    }
    window.RequestPaint();
    applied = true;
  });
  if (!dispatched) failure = "UI thread is no longer available";
  if (!applied && error) *error = failure;
  return applied;
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
  State(rex::ui::WindowedAppContext& ui_context, rex::ui::Window& ui_window,
        std::filesystem::path file)
      : context(ui_context), window(ui_window), settings_file(std::move(file)) {}
  mutable std::mutex mutex;
  rex::ui::WindowedAppContext& context;
  rex::ui::Window& window;
  std::filesystem::path settings_file;
  std::string previous;
  bool active = false;
  bool transitioning = false;
  uint64_t generation = 0;
  std::chrono::steady_clock::time_point deadline{};
};

DisplayPreviewTransaction::DisplayPreviewTransaction(rex::ui::WindowedAppContext& context,
                                                     rex::ui::Window& window,
                                                     std::filesystem::path settings_file,
                                                     std::chrono::steady_clock::duration timeout)
    : state_(std::make_shared<State>(context, window, std::move(settings_file))),
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
  const bool applied = marked && ApplyRuntimeDisplay(state_->context, state_->window, requested, error);
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
  rex::ui::WindowedAppContext* context = &state_->context;
  timeout_ = std::jthread([weak_state, context, deadline, generation](std::stop_token stop) {
    std::condition_variable_any condition;
    std::mutex mutex;
    std::unique_lock wait_lock(mutex);
    condition.wait_until(wait_lock, stop, deadline, [] { return false; });
    while (!stop.stop_requested()) {
      // This callback owns no transaction object. Shutdown can invalidate
      // State while a UI callback is queued. A save in progress may delay the
      // rollback, so keep retrying until Keep or Revert ends the generation.
      if (!context->CallInUIThread([weak_state, generation] {
            if (auto state = weak_state.lock()) {
              std::string ignored;
              RevertState(state, generation, &ignored);
            }
          })) return;
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
  const bool applied = ApplyRuntimeDisplay(state->context, state->window, previous, error);
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
