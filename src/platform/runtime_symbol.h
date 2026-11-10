// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::platform {

// Resolve an import exported by the already-loaded rexruntime module. No load
// or unload occurs here; the host owns that module's lifetime.
void* ResolveRuntimeSymbol(const char* symbol);

}  // namespace rb_blitz::platform
