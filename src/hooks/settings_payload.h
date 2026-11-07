// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
namespace rex { class Runtime; }
namespace rb_blitz::settings {
bool InstallMenuPayload(rex::Runtime* runtime, bool settings_restart_boot = false,
                        const std::function<void()>& pump_startup_window = {});
}
