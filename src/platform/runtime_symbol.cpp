// SPDX-License-Identifier: GPL-2.0-only
#include "platform/runtime_symbol.h"
#include <rex/kernel/export.h>
#if !defined(_WIN32)
#include <rex/platform/dynlib.h>
#include <dlfcn.h>
#endif

namespace rb_blitz::platform {

void* ResolveRuntimeSymbol(const char* symbol) {
  if (!symbol) return nullptr;
  std::string_view name(symbol);
  if (!name.starts_with("__imp__")) return nullptr;
  name.remove_prefix(7);
  for (const auto module : {rex::kernel::ExportModule::kXboxkrnl,
                            rex::kernel::ExportModule::kXAM,
                            rex::kernel::ExportModule::kXBDM}) {
    for (const auto& entry : rex::kernel::GetExports(module)) {
      if (entry.name != name || entry.type != rex::kernel::ExportType::kFunction || !entry.fn)
        continue;
#if defined(_WIN32)
      return reinterpret_cast<void*>(entry.fn);
#else
      // ELF may bind the export table's function pointer to the executable's
      // override. The public table identifies the import; its owning SDK image
      // supplies the original definition without searching the executable.
      static rex::platform::DynamicLibrary kernel = [] {
        rex::platform::DynamicLibrary library;
        Dl_info image{};
        if (dladdr(reinterpret_cast<void*>(&rex::kernel::GetExports), &image) && image.dli_fname)
          library.Attach(image.dli_fname);
        return library;
      }();
      return kernel.GetRawSymbol(symbol);
#endif
    }
  }
  return nullptr;
}

}  // namespace rb_blitz::platform
