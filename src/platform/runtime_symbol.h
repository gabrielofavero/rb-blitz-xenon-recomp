// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::platform {

// Resolve an original function import from the public kernel export tables.
// Executable overrides are excluded; only already-loaded SDK modules are used.
void* ResolveRuntimeSymbol(const char* symbol);

}  // namespace rb_blitz::platform
