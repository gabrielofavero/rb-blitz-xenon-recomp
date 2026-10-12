// SPDX-License-Identifier: GPL-2.0-only
#include "settings/graphics_boot.h"
#include "settings/schema.h"
#include "settings/store.h"

namespace rb_blitz::settings {
namespace {
constexpr SettingId kRows[] = {SettingId::kRenderer, SettingId::kResolution,
                               SettingId::kAntiAliasing, SettingId::kFiltering};
std::string Signature(const GraphicsValues& values) {
  std::string result;
  for (const auto id : kRows) {
    const auto found = values.find(id);
    if (found == values.end()) continue;
    std::string text;
    toml::Style style;
    if (PersistedValue(id, found->second, &text, &style))
      result += std::string(SettingIdToken(id)) + "=" + text + ";";
  }
  return result;
}
GraphicsValues ReadValues(const toml::Document& doc, const char* section) {
  GraphicsValues result;
  for (const auto id : kRows) {
    const auto item = doc.Get(section, SettingIdToken(id));
    if (!item.found) continue;
    std::string token = item.value;
    if (id == SettingId::kResolution) {
      if (!item.list_ok || item.list.size() != 2) continue;
      token = item.list[0] + "x" + item.list[1];
    }
    Value value;
    if (schema::ParseTokenToValue(schema::FindRow(id)->descriptor, token, &value))
      result[id] = std::move(value);
  }
  return result;
}
}

bool GraphicsBootState::Open(const std::filesystem::path& settings_file, std::string* error) {
  if (settings_file.empty()) return false;
  file_ = settings_file;
  file_ += ".graphics-boot.toml";
  if (!lock_.Acquire(file_, error, false)) return false;
  std::string text;
  bool exists = false;
  if (!ReadFileText(file_, &text, &exists)) {
    if (error) *error = "cannot read graphics startup journal";
    return false;
  }
  if (exists) {
    bool ok;
    const auto doc = toml::Document::Parse(text, &ok, error);
    if (!ok || doc.Get("", "schema_version").value != "1") {
      if (error) *error = "unrecognized graphics startup journal; left untouched";
      return false;
    }
    pending_ = doc.Get("", "pending").value == "true";
    requested_ = doc.Get("", "requested").value;
    recovery_for_ = doc.Get("", "recovery_for").value;
    attempted_ = ReadValues(doc, "attempted");
    good_ = ReadValues(doc, "last_good");
  }
  active_ = true;
  return true;
}

GraphicsValues GraphicsBootState::Select(const GraphicsValues& requested,
                                         const GraphicsValues& safe, bool force_safe) {
  const auto signature = Signature(requested);
  // A changed user preference gets a fresh attempt. A failed preference keeps the
  // working recovery choice until changed, even after recovery itself presents.
  recovered_ = active_ && ((pending_ && requested_ == signature) ||
                           (!recovery_for_.empty() && recovery_for_ == signature));
  if (recovered_) recovery_for_ = signature;
  else recovery_for_.clear();
  requested_ = signature;
  if (force_safe) return safe;
  if (!recovered_) return requested;
  return good_.size() == 4 && Signature(good_) != signature ? good_ : safe;
}

bool GraphicsBootState::Begin(const GraphicsValues& effective, std::string* error) {
  if (!active_) return false;
  attempted_ = effective;
  pending_ = true;
  return Save(error);
}
bool GraphicsBootState::Presented(std::string* error) {
  if (!active_ || !pending_) return false;
  good_ = attempted_;
  pending_ = false;
  return Save(error);
}
bool GraphicsBootState::Closed(std::string* error) {
  if (!active_ || !pending_) return true;
  pending_ = false;
  return Save(error);
}
bool GraphicsBootState::Save(std::string* error) {
  toml::Document doc;
  doc.Set("", "schema_version", "1");
  doc.Set("", "pending", pending_ ? "true" : "false");
  doc.Set("", "requested", toml::RenderBasic(requested_));
  doc.Set("", "recovery_for", toml::RenderBasic(recovery_for_));
  for (const auto& [section, values] :
       std::map<std::string, GraphicsValues>{{"attempted", attempted_}, {"last_good", good_}}) {
    for (const auto& [id, value] : values) {
      std::string text;
      toml::Style style;
      if (PersistedValue(id, value, &text, &style)) doc.Set(section, SettingIdToken(id), text);
    }
  }
  return WriteFileAtomic(file_, doc.Render(), error);
}
}  // namespace rb_blitz::settings
