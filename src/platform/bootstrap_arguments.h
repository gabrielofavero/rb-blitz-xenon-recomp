// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <rex/cli.h>
#include "platform/game_restart.h"
#include "platform/window_host.h"

namespace rb_blitz::platform {
inline rex::CLIArgList NormalizeBootstrapArguments(const rex::CLIArgList& arguments) {
  std::vector<std::string> retained, bare_options;
  const auto flush_options = [&] {
    retained.insert(retained.end(), bare_options.begin(), bare_options.end());
    bare_options.clear();
  };
  const auto source = arguments.GetAll();
  for (size_t index = 0; index < source.size(); ++index) {
    std::string normalized = source[index];
    // Older title launchers spell boolean negation with an underscore.
    // Keep their option order and use the SDK's supported negation spelling.
    if (normalized.starts_with("--no_")) normalized.replace(0, 5, "--no-");
    const std::string_view token = normalized;
    if (token.starts_with(window_host::kSwitch) || token.starts_with(kHandoffSwitch) ||
        token == kBootstrapSwitch) continue;
    if (!token.starts_with("--")) {
      retained.emplace_back(token);
      continue;
    }
    if (token.find('=') != token.npos) {
      flush_options();
      retained.emplace_back(token);
    }
    else if (index + 1 < source.size() && !source[index + 1].starts_with("--")) {
      flush_options();
      retained.emplace_back(token);
      retained.push_back(source[++index]);
    } else {
      // Removing an internal option must not make a bare user flag consume the
      // next positional. Keep option order and move only that positional ahead.
      bare_options.emplace_back(token);
    }
  }
  flush_options();
  return rex::CLIArgList::Parse(retained);
}
}  // namespace rb_blitz::platform
