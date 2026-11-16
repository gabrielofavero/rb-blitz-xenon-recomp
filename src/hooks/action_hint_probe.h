// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <memory>
#include <optional>
#include "input/input_icons.h"
#include "ui/input_icon_rows.h"

namespace rb_blitz::hooks {
// Refresh the guest-owned localized action labels from the effective input
// snapshot on each UI frame, including before a button is pressed.
void PublishActionHints();
// Present only while this label is being drawn by a guest InlineHelp footer.
std::optional<uint32_t> DrawingInlineHelpAction(uint32_t label);
std::shared_ptr<const ui::input_icon_rows::Catalog> EnsureInputCatalog(
    const input::icons::View& view);
}
