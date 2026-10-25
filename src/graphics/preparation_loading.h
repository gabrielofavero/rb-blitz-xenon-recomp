// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string_view>
namespace rex { class Runtime; }
namespace rb_blitz::graphics {
void ConfigureLoading(rex::Runtime* runtime);
void BeginLoadingAttempt(); // guest/UI owner thread
void ContentLoadingReady();
void ObserveContentLoading(uint8_t* base, uint32_t panel);
void PollLoading();
bool GraphicsLoadingPending();
bool LoadingQuery(std::string_view path, uint32_t* answer);
void CloseLoading(); // before runtime teardown
}
