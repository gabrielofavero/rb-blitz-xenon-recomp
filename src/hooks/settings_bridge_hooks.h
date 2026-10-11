// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The one entry point of the typed settings channel's guest hooks
// (src/hooks/settings_bridge_hooks.cpp; the transport is described in src/hooks/settings_bridge.h).
//
// The hooks themselves are installed at link time - a strong `extern "C" REX_FUNC(sub_...)`
// replaces the recompiled builtin's call sites, so the channel is live from the first guest call
// and needs no configuration point to exist. What Configure() adds is the *cvar read*: the two
// flags are read once, on a host thread, and cached, because a guest thread must not touch the cvar
// registry. Calling it is therefore recommended but not required - the compiled defaults
// (enabled, not forward-only) apply until it runs.

#pragma once

#include "hooks/settings_bridge.h"

namespace rb_blitz::settings::bridge {

// Reads settings_bridge / settings_bridge_forward_only and logs the result. Idempotent; safe to
// call more than once, and safe not to call at all (the defaults are the faithful ones).
void Configure();

// P11a: point the channel at the boot's own store - the settings service
// (src/settings/bridge_sink.h), whose rows are the same tokens, ops and one-bit answers the
// in-memory stand-in carries, but whose values land in `settings.toml` through a real transaction.
//
// Both pointers are borrowed and must outlive the guest session; a null pair restores the
// in-memory store, which is what a build with no service keeps using. Called from the boot's
// OnPostLoadXexImage, before the guest can send a frame - the guest hooks themselves are installed
// at link time and never change.
void InstallSink(RequestSink* sink, SinkQueries* queries);

}  // namespace rb_blitz::settings::bridge
