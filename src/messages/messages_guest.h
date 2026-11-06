// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::messages {
// Called once on the host during OnPostLoadXexImage. The hook itself only reads snapshots and
// guest memory; it never fetches or writes files.
void InstallMessagesGuest();
}  // namespace rb_blitz::messages
