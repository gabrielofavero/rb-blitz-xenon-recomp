// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project

#include "generated/default/rb_blitz_init.h"

#include "rb_blitz_app.h"

// The graphics backend the game renders with - the Renderer row of the launcher's Audio / Video
// tab, and the flag the launcher passes as `--gpu_backend=`. It is defined here, beside the app
// that acts on it (RbBlitzApp::SelectGpuBackend), because which backend to load is the app's
// decision rather than the SDK's: the GPU plugin can only run a backend it was compiled with,
// and the app is what knows how to fall back when it cannot.
//
// "any" is the SDK's own word for "whichever this build has", so it is the default: a launcher
// that never touches the row passes nothing and the behaviour is the one that existed before
// the cvar did.
REXCVAR_DEFINE_STRING(gpu_backend, "any", "GPU",
                      "Graphics backend to render with: d3d12, vulkan, or any (whichever this "
                      "build has)")
    .allowed({"d3d12", "vulkan", "any"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_DEFINE_APP(rb_blitz, RbBlitzApp::Create)
