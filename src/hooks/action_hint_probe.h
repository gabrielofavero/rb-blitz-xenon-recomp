// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace rb_blitz::hooks {
// Refresh the guest-owned localized action labels from the effective input
// snapshot on each UI frame, including before a button is pressed.
void PublishActionHints();
}
