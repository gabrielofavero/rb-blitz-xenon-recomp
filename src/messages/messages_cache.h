// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <filesystem>
#include <string>

#include "messages/messages_api.h"

namespace rb_blitz::messages {
bool Load(const std::filesystem::path& path, MessageFeed* feed, std::string* error);
// Save only a document already validated by ParseFeed; never modify the bundled copy.
bool Save(const std::filesystem::path& path, const std::string& body, std::string* error);
}  // namespace rb_blitz::messages
