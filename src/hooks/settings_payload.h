// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <functional>
namespace rex::kernel { class TitleRuntime; }
namespace rb_blitz::settings {
bool InstallMenuPayload(rex::kernel::TitleRuntime* runtime, bool settings_restart_boot = false,
                        const std::function<void()>& pump_startup_window = {});
}
