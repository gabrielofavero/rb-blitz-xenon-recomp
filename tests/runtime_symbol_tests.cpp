// SPDX-License-Identifier: GPL-2.0-only
#include "platform/runtime_symbol.h"
#include "check.h"
#include <rex/kernel/export.h>
#if defined(_WIN32)
#include <windows.h>
#define IMPORT_TEST_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define IMPORT_TEST_EXPORT __attribute__((visibility("default")))
#endif

// Reproduce the title import override without calling either implementation.
extern "C" IMPORT_TEST_EXPORT void __imp__NtReadFile(PPCContext&, uint8_t*) {}

int main() {
  using namespace rb_blitz::test;
  const void* hook = reinterpret_cast<void*>(&__imp__NtReadFile);
#if defined(_WIN32)
  const void* global = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(nullptr), "__imp__NtReadFile"));
#else
  const void* global = dlsym(RTLD_DEFAULT, "__imp__NtReadFile");
#endif
  BeginCase("Original modular SDK imports exclude the executable override");
  CHECK_TRUE(global == hook);
  for (const auto* name : {"__imp__NtReadFile", "__imp__NtReadFileScatter", "__imp__NtWriteFile",
                           "__imp__XMsgStartIORequestEx", "__imp__XMsgInProcessCall",
                           "__imp__XMsgSystemProcessCall"}) {
    const auto* original = rb_blitz::platform::ResolveRuntimeSymbol(name);
    CHECK_TRUE(original != nullptr);
    CHECK_TRUE(original != hook);
#if !defined(_WIN32)
    Dl_info expected{}, actual{};
    CHECK_TRUE(dladdr(reinterpret_cast<void*>(&rex::kernel::GetExports), &expected) != 0);
    CHECK_TRUE(dladdr(original, &actual) != 0);
    CHECK_TRUE(actual.dli_fbase == expected.dli_fbase);
#endif
  }
  BeginCase("Only function imports in the public SDK tables are eligible");
  CHECK_TRUE(rb_blitz::platform::ResolveRuntimeSymbol(nullptr) == nullptr);
  CHECK_TRUE(rb_blitz::platform::ResolveRuntimeSymbol("") == nullptr);
  CHECK_TRUE(rb_blitz::platform::ResolveRuntimeSymbol("rb_blitz_missing_export") == nullptr);
  CHECK_TRUE(rb_blitz::platform::ResolveRuntimeSymbol("__imp__rb_blitz_missing_export") == nullptr);
  CHECK_TRUE(rb_blitz::platform::ResolveRuntimeSymbol("__imp__XboxKrnlVersion") == nullptr);
  return Finish();
}
