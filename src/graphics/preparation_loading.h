// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <string_view>
namespace rex { class Runtime; }
namespace rb_blitz::graphics {
// Why preparation failed, when it did. The authored failure row names the
// reason; the three cases need different advice, which is why they are not one
// string (docs/engine/shader-cache.md, part 7).
enum class GraphicsFailure : uint32_t {
  kNone = 0,
  // The installed content could not resolve the catalogue's shaders.
  kShaderData = 1,
  // A live generation ended failed or incomplete (a real driver/runtime result).
  kPreparation = 2,
  // The cache root cannot be written, so preparation cannot persist or reopen
  // its storage. Distinct from a GPU failure: nothing about the pipeline work
  // itself was wrong.
  kCacheNotWritable = 3,
};

void ConfigureLoading(rex::Runtime* runtime);
void BeginLoadingAttempt(); // guest/UI owner thread
void ContentLoadingReady();
void ObserveContentLoading(uint8_t* base, uint32_t panel);
void PollLoading();
bool GraphicsLoadingPending();
bool LoadingQuery(std::string_view path, uint32_t* answer);
void CloseLoading(); // before runtime teardown
// A successful DLC or store refresh changed the content set. This retires the
// live readiness claim and nothing else: storage is never reset, so it is safe
// during a song, and the next preparation reuses the layers that are still
// valid. Safe from any thread.
void NoteContentChanged(std::string_view reason);
}  // namespace rb_blitz::graphics
