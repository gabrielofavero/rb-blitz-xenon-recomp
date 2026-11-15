// SPDX-License-Identifier: GPL-2.0-only
#include "platform/runtime_symbol.h"
#include <cstdio>
#include <dlfcn.h>

// Reproduce the title's import override. Never call the SDK function: only its
// address matters, so this test does not need a guest runtime or a game dump.
extern "C" void __imp__NtReadFile() {}

int main() {
  const void* hook = reinterpret_cast<void*>(&__imp__NtReadFile);
  const void* global = dlsym(RTLD_DEFAULT, "__imp__NtReadFile");
  const void* original = rb_blitz::platform::ResolveRuntimeSymbol("__imp__NtReadFile");
  if (global != hook || !original || original == hook ||
      rb_blitz::platform::ResolveRuntimeSymbol(nullptr) ||
      rb_blitz::platform::ResolveRuntimeSymbol("rb_blitz_missing_export")) {
    std::fprintf(stderr, "SDK import lookup returned an override or failed to resolve the original\n");
    return 1;
  }
  return 0;
}
