// SPDX-License-Identifier: GPL-2.0-only
// Headless probe for the startup function-table registration cost.
#include <chrono>
#include <cstdio>
#include <vector>

#include <rex/ppc/context.h>
#include <rex/runtime.h>

namespace {
void Stub(PPCContext&, uint8_t*) {}
}

int main() {
  constexpr uint32_t code_base = 0x82190000;
  constexpr uint32_t code_size = 0x00666ED4;
  constexpr uint32_t image_base = 0x82000000;
  constexpr uint32_t image_size = 0x00A40000;
  constexpr size_t count = 38450;
  std::vector<PPCFuncMapping> mappings;
  mappings.reserve(count + 1);
  for (size_t i = 0; i < count; ++i) {
    mappings.push_back({code_base + i * 160, &Stub});
  }
  mappings.push_back({0, nullptr});

  rex::PPCImageInfo image{};
  image.code_base = code_base;
  image.code_size = code_size;
  image.image_base = image_base;
  image.image_size = image_size;
  image.func_mappings = mappings.data();
  rex::RuntimeConfig config{};
  config.tool_mode = true;
  rex::Runtime runtime({});
  const auto start = std::chrono::steady_clock::now();
  const auto status = runtime.Setup(image, std::move(config));
  const auto ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  std::printf("registration probe: status=%08X mappings=%zu setup_ms=%.1f\n",
              status, count, ms);
  return status == 0 ? 0 : 1;
}
