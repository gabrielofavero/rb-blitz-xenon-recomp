// SPDX-License-Identifier: GPL-2.0-only
// Exercise the real emitted registration implementation with controlled image tables.
#include <rex/kernel/function_dispatcher.h>
#include <rex/ppc/image.h>
#include <cstdio>
#include <vector>

void rb_blitz_RegisterFunctions(rex::kernel::IFunctionRegistrar* registrar);
void First(PPCContext&, uint8_t*) {}
void Second(PPCContext&, uint8_t*) {}
const PPCFuncMapping kPPCFuncMappings[] = {
#if !defined(EMPTY_MAPPING)
  {0x82190000, First},
  {0x82190004, Second},
  {0x82190000, Second},
#endif
  {0, nullptr},
  {0xDEADBEEF, First},
};
extern const rex::ppc::PPCImage kPPCImageConfig{.func_mappings = kPPCFuncMappings};

struct Recorder final : rex::kernel::IFunctionRegistrar {
  std::vector<PPCFuncMapping> values;
  bool SetFunction(rex::u32 address, PPCFunc* function) override {
    values.push_back({address, function});
    return true;
  }
};

int main() {
  Recorder recorder;
  rb_blitz_RegisterFunctions(&recorder);
#if defined(EMPTY_MAPPING)
  if (!recorder.values.empty()) return 1;
#else
  if (recorder.values.size() != 3) return 2;
  for (size_t i = 0; i < recorder.values.size(); ++i) {
    if (recorder.values[i].address != kPPCFuncMappings[i].address ||
        recorder.values[i].host != kPPCFuncMappings[i].host) return 3;
  }
#endif
  std::printf("Registration order, duplicates, and sentinel: pass (%zu entries)\n",
              recorder.values.size());
}
