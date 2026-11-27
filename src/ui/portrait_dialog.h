// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

namespace rex::ui { class IDialog; struct ToastStyle; }
namespace rb_blitz::ui::portrait_dialog {
void Observe(uint32_t screen, uint32_t panel, uint32_t directory, bool eligible);
void PublishLabel(uint32_t directory, bool body, std::string_view text);
bool AnswerQuery(std::string_view path, bool* answer);
bool SuppressDirectory(uint32_t directory);
std::unique_ptr<rex::ui::IDialog> CreateOverlay(const rex::ui::ToastStyle& theme);
}
