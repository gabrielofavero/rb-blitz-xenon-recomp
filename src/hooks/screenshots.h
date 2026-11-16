// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rex { class Runtime; }
namespace rb_blitz::screenshots {
void Configure(rex::Runtime* runtime, const std::filesystem::path& shared_documents = {});
}
