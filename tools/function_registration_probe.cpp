// SPDX-License-Identifier: GPL-2.0-only
// Headless probe for the startup function-table registration cost.
#include <chrono>
#include <cstdio>
#include <vector>

#include <rex/ppc/context.h>
#include <rex/kernel/function_dispatcher.h>
#include <rex/kernel/guest_memory.h>
#include <rex/ppc/image.h>

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

  rex::kernel::GuestAddressSpace memory;
  if (!memory.Initialize()) return 1;
  rex::kernel::FunctionDispatcher dispatcher(&memory);
  const auto start = std::chrono::steady_clock::now();
  if (!dispatcher.InitializeFunctionTable(code_base, code_size, image_base, image_size, true))
    return 2;
  for (const auto& mapping : mappings) {
    if (!mapping.address) break;
    if (!dispatcher.SetFunction(mapping.address, mapping.host)) return 3;
  }
  const auto ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  for (const auto& mapping : mappings) {
    if (!mapping.address) break;
    if (dispatcher.GetFunction(mapping.address) != mapping.host) return 4;
  }
  std::printf("registration probe: mappings=%zu registration_ms=%.1f\n", count, ms);
  return 0;
}
