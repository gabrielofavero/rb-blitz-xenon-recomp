// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include "input/input_icons.h"
#include "ui/input_icon_rows.h"

namespace rb_blitz::hooks {
// Refresh the guest-owned localized action labels from the effective input
// snapshot on each UI frame, including before a button is pressed.
void PublishActionHints();
std::shared_ptr<const ui::input_icon_rows::Catalog> EnsureInputCatalog(
    const input::icons::View& view);
}
