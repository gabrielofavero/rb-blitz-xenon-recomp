// SPDX-License-Identifier: GPL-2.0-only
#include "messages/messages_api.h"

#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <regex>
#include <set>

#include <nlohmann/json.hpp>

#include "messages/messages_icons.h"

namespace rb_blitz::messages {
namespace {
using Json = nlohmann::json;

bool VersionParts(std::string_view text, std::array<int, 3>* parts) {
  for (int i = 0; i < 3; ++i) {
    const auto dot = text.find('.');
    const auto part = text.substr(0, dot);
    if (part.empty() || part.size() > 5) return false;
    for (char c : part) if (c < '0' || c > '9') return false;
    auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), (*parts)[i]);
    if (error != std::errc{} || end != part.data() + part.size()) return false;
    if (i < 2) {
      if (dot == std::string_view::npos) return false;
      text.remove_prefix(dot + 1);
    } else if (dot != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

bool Field(const Json& object, const char* name, std::string* value) {
  auto it = object.find(name);
  if (it == object.end() || !it->is_string()) return false;
  *value = it->get<std::string>();
  return true;
}

bool Date(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
  const int year = std::stoi(std::string(text.substr(0, 4)));
  const int month = std::stoi(std::string(text.substr(5, 2)));
  const int day = std::stoi(std::string(text.substr(8, 2)));
  if (year < 1 || month < 1 || month > 12) return false;
  const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day >= 1 && day <= days[month] + (month == 2 && leap ? 1 : 0);
}

bool Visible(std::string_view text, std::size_t max, bool multiline) {
  if (text.empty()) return false;
  std::size_t characters = 0;
  for (unsigned char c : text) {
    if (c < 0x20 && !(multiline && c == '\n')) return false;
    if (c == 0x7f) return false;
    if ((c & 0xc0) != 0x80) ++characters;  // nlohmann already checked UTF-8
  }
  return characters <= max;
}

bool Id(std::string_view text) {
  if (text.empty() || text.size() > 64 || text[0] < 'a' || text[0] > 'z') {
    if (text.empty() || text.size() > 64 || text[0] < '0' || text[0] > '9') return false;
  }
  for (char c : text) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  return true;
}

bool Icon(std::string_view text) {
  static const std::regex custom(R"(^[a-z0-9][a-z0-9_-]{0,63}\.(png|jpg|jpeg)$)",
                                 std::regex::icase);
  const std::string value(text);
  return IsBuiltInIcon(text) || std::regex_match(value, custom);
}
}  // namespace

bool ParseFeed(std::string_view body, MessageFeed* feed, std::string* error) {
  if (!feed) return false;
  if (body.size() > 256 * 1024) { if (error) *error = "feed exceeds 256 KiB"; return false; }
  Json root = Json::parse(body.begin(), body.end(), nullptr, false);
  if (root.is_discarded() || !root.is_object()) {
    if (error) *error = "malformed JSON object";
    return false;
  }
  auto schema = root.find("schema_version");
  if (schema == root.end() || !schema->is_number_integer() || *schema != 1) {
    if (error) *error = "unknown or missing schema_version";
    return false;
  }
  auto rows = root.find("messages");
  if (rows == root.end() || !rows->is_array() || rows->size() > 32) {
    if (error) *error = "messages must be an array of at most 32 entries";
    return false;
  }
  MessageFeed parsed;
  if (!Field(root, "latest_version", &parsed.latest_version) ||
      CompareVersions("0.0.0", parsed.latest_version) == VersionStatus::kMalformed) {
    parsed.latest_version.clear();
    parsed.warnings.push_back("latest_version is malformed; update prompt disabled");
  }
  if (!Field(root, "generated_at", &parsed.generated_at) ||
      !std::regex_match(parsed.generated_at, std::regex(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?Z$)")) ||
      !Date(std::string_view(parsed.generated_at).substr(0, 10)) ||
      std::stoi(parsed.generated_at.substr(11, 2)) > 23 ||
      std::stoi(parsed.generated_at.substr(14, 2)) > 59 ||
      std::stoi(parsed.generated_at.substr(17, 2)) > 59) {
    parsed.generated_at.clear();
    parsed.warnings.push_back("generated_at is unreadable");
  }
  std::set<std::string> ids;
  for (const auto& row : *rows) {
    Message message;
    if (!row.is_object() || !Field(row, "id", &message.id) || !Id(message.id) ||
        !Field(row, "title", &message.title) || !Visible(message.title, 64, false) ||
        !Field(row, "body", &message.body) || !Visible(message.body, 1024, true) ||
        !Field(row, "published", &message.published) || !Date(message.published) ||
        !ids.insert(message.id).second) {
      parsed.warnings.push_back("dropped invalid or duplicate message");
      continue;
    }
    auto icon = row.find("icon");
    if (icon != row.end()) {
      if (icon->is_string() && Icon(icon->get<std::string>())) message.icon = icon->get<std::string>();
      else parsed.warnings.push_back("icon " + message.id + ": invalid; using icon_misc");
    }
    parsed.messages.push_back(std::move(message));
  }
  *feed = std::move(parsed);
  if (error) error->clear();
  return true;
}

VersionStatus CompareVersions(std::string_view current, std::string_view latest) {
  std::array<int, 3> a{}, b{};
  if (!VersionParts(current, &a) || !VersionParts(latest, &b)) return VersionStatus::kMalformed;
  if (a < b) return VersionStatus::kUpdateAvailable;
  if (a > b) return VersionStatus::kLocalNewer;
  return VersionStatus::kCurrent;
}

std::string_view VersionStatusToken(VersionStatus status) {
  switch (status) {
    case VersionStatus::kCurrent: return "current";
    case VersionStatus::kUpdateAvailable: return "available";
    case VersionStatus::kLocalNewer: return "local-newer";
    case VersionStatus::kMalformed: return "malformed";
  }
  return "malformed";
}

bool IsAllowedFeedUrl(std::string_view url, bool diagnostic_override) {
  if (url == kFeedUrl) return true;
  if (!diagnostic_override || !url.starts_with("http://")) return false;
  auto rest = url.substr(7);
  auto slash = rest.find('/');
  if (slash == std::string_view::npos || rest.substr(slash) != "/feed.json") return false;
  auto host = rest.substr(0, slash);
  for (auto loopback : {"127.0.0.1", "localhost", "[::1]"}) {
    if (host == loopback) return true;
    if (host.starts_with(loopback) && host.size() > std::string_view(loopback).size() &&
        host[std::string_view(loopback).size()] == ':') {
      auto port = host.substr(std::string_view(loopback).size() + 1);
      if (!port.empty() && port.size() <= 5) {
        bool digits = true;
        for (char c : port) digits &= c >= '0' && c <= '9';
        if (digits && std::stoi(std::string(port)) <= 65535 && std::stoi(std::string(port)) > 0) return true;
      }
    }
  }
  return false;
}
}  // namespace rb_blitz::messages
