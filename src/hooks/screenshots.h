// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>

namespace rex::kernel { class TitleRuntime; }
namespace rb_blitz::screenshots {
void Configure(rex::kernel::TitleRuntime* runtime, const std::filesystem::path& shared_documents = {});
}
