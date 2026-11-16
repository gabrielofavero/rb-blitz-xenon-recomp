// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ultimate_import_core.h"
namespace rb_blitz::platform {
bool DownloadIOSUltimate(const std::filesystem::path& destination,
                        const installer::PinsUltimate& pins,
                        const installer::portable::ImportCallbacks& callbacks,
                        std::string* error);
}
