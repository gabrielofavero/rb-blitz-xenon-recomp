// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rb_blitz::platform::window_host {

inline constexpr std::string_view kSwitch = "--window-host=";

// The window belongs to a small host process; each game boot gets fresh process
// state and a new rendering child inside the same top-level native window.
struct Identity {
  uint32_t pid = 0;
  uint64_t created = 0;
  uint64_t window = 0;
};
std::string Encode(const Identity& identity);
bool Decode(std::string_view text, Identity* out);

// Called before SDK initialization. A normal Windows launch runs the window
// host here until exit. A hosted game registers with it and returns instead.
// Other platforms retain their existing window lifecycle.
void Bootstrap(const std::vector<std::string>& arguments);

bool ExpectRestart(uint32_t replacement_pid);
bool Attach(void* native_window, bool resizable, std::function<void(bool)> focus_changed);
bool SetDisplayMode(int mode); // 0 windowed, 1 borderless, 2 desktop fullscreen
void SetTitle(std::string_view title);
bool Active();

}  // namespace rb_blitz::platform::window_host
