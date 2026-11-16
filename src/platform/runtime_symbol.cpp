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
  // The executable deliberately overrides several __imp__ imports. Searching
  // RTLD_DEFAULT resolves those hooks back to themselves (NtReadFile recurses
  // until its stack overflows). Search the already-loaded SDK image explicitly.
#if defined(RB_BLITZ_IOS)
  static void* const runtime = dlopen("@rpath/rexruntime.framework/rexruntime", RTLD_LAZY | RTLD_NOLOAD);
#elif defined(__APPLE__)
  static void* const runtime = dlopen("@rpath/librexruntime.dylib", RTLD_LAZY | RTLD_NOLOAD);
#else
  static void* const runtime = dlopen("librexruntime.so", RTLD_LAZY | RTLD_NOLOAD);
#endif
  return runtime ? dlsym(runtime, symbol) : nullptr;
#endif
}

}  // namespace rb_blitz::platform
