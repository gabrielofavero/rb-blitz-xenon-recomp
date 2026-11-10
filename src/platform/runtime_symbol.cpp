// SPDX-License-Identifier: GPL-2.0-only
#include "platform/runtime_symbol.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rb_blitz::platform {

void* ResolveRuntimeSymbol(const char* symbol) {
  if (!symbol || !*symbol) return nullptr;
#if defined(_WIN32)
  const HMODULE runtime = GetModuleHandleA("rexruntime.dll");
  return runtime ? reinterpret_cast<void*>(GetProcAddress(runtime, symbol)) : nullptr;
#else
  // The SDK loads rexruntime into this process before guest imports are used.
  // RTLD_DEFAULT searches its exported symbols without opening another copy.
  return dlsym(RTLD_DEFAULT, symbol);
#endif
}

}  // namespace rb_blitz::platform
