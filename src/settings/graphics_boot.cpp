// SPDX-License-Identifier: GPL-2.0-only
#include "settings/graphics_boot.h"
#include "settings/schema.h"
#include "settings/store.h"

namespace rb_blitz::settings {
namespace {
constexpr SettingId kRows[] = {SettingId::kInterfaceOrientation, SettingId::kGameplayOrientation, SettingId::kAspectRatio, SettingId::kRenderScale,
                               SettingId::kForce16By9,
                               SettingId::kInterfaceResolution, SettingId::kGameplayResolution,
                               SettingId::kInterfaceRenderScale, SettingId::kGameplayRenderScale,
                               SettingId::kRenderer, SettingId::kResolution,
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

void ApplyReleaseGraphicsRules(GraphicsValues& values, const schema::ReleaseSettings& release) {
  for (auto& [id, value] : values) {
    const auto rule = release.rules.find(id);
    if (rule != release.rules.end() && rule->second.fixed_default && rule->second.default_value)
      value = *rule->second.default_value;
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
    const auto version = doc.Get("", "schema_version").value;
    if (!ok || (version != "1" && version != "2")) {
      if (error) *error = "unrecognized graphics startup journal; left untouched";
      return false;
    }
    pending_ = doc.Get("", "pending").value == "true";
    requested_ = doc.Get("", "requested").value;
    if (version == "2") {
      rejected_ = doc.Get("", "rejected").value == "true";
      recovery_for_ = doc.Get("", "recovery_for").value;
      retried_ = doc.Get("", "retried").value == "true";
      if (pending_ && !rejected_ && doc.Get("", "retrying").value == "true")
        retried_ = false;
    }
    attempted_ = ReadValues(doc, "attempted");
    good_ = ReadValues(doc, "last_good");
  }
  active_ = true;
  return true;
}

GraphicsValues GraphicsBootState::Select(const GraphicsValues& requested,
                                         const GraphicsValues& effective,
                                         const GraphicsValues& safe, bool force_safe) {
  // What the player chose keys the journal; what the boot would apply decides whether the
  // recovery choice is already the attempt again (then recovering would loop, so the safe
  // floor is used instead).
  const auto signature = Signature(requested);
  const auto attempt = Signature(effective);
  recovered_ = false;
  retrying_ = false;
  if (active_ && pending_ && rejected_ && requested_ == signature) {
    // A confirmed refusal during the retry keeps the recovery latched.
    recovered_ = true;
    if (recovery_for_ != signature) {
      recovery_for_ = signature;
      retried_ = false;
    }
  } else if (active_ && !recovery_for_.empty() && recovery_for_ == signature) {
    if (retried_) {
      recovered_ = true;  // the request failed twice already: it keeps the recovery values
    } else {
      retrying_ = true;  // An interrupted retry can still receive a verdict.
    }
  } else {
    // A changed preference is a fresh attempt.
    recovery_for_.clear();
    retried_ = false;
  }
  requested_ = signature;
  if (force_safe) {
    retrying_ = false;  // the forced route attempted nothing, so nothing was retried
    return safe;
  }
  if (!recovered_) {
    if (retrying_) retried_ = true;
    return effective;
  }
  return good_.size() == effective.size() && Signature(good_) != attempt ? good_ : safe;
}

bool GraphicsBootState::Begin(const GraphicsValues& effective, std::string* error) {
  if (!active_) return false;
  attempted_ = effective;
  pending_ = true;
  rejected_ = false;
  return Save(error);
}
bool GraphicsBootState::Rejected(std::string* error) {
  if (!active_ || !pending_) return false;
  rejected_ = true;
  return Save(error);
}
bool GraphicsBootState::Presented(std::string* error) {
  if (!active_ || !pending_ || rejected_) return false;
  good_ = attempted_;
  pending_ = false;
  // The retry proved the request: it is an ordinary request again.
  if (retrying_) {
    recovery_for_.clear();
    retried_ = false;
    retrying_ = false;
  }
  return Save(error);
}
bool GraphicsBootState::Closed(std::string* error) {
  if (!active_ || !pending_ || rejected_) return true;
  pending_ = false;
  // A clean close before the menu frame says nothing about the request, so a retry that was
  // interrupted this way has not been spent and gets another chance on the next boot.
  if (retrying_) {
    retried_ = false;
    retrying_ = false;
  }
  return Save(error);
}
bool GraphicsBootState::Save(std::string* error) {
  toml::Document doc;
  doc.Set("", "schema_version", "2");
  doc.Set("", "pending", pending_ ? "true" : "false");
  doc.Set("", "rejected", rejected_ ? "true" : "false");
  doc.Set("", "requested", toml::RenderBasic(requested_));
  doc.Set("", "recovery_for", toml::RenderBasic(recovery_for_));
  doc.Set("", "retried", retried_ ? "true" : "false");
  doc.Set("", "retrying", retrying_ ? "true" : "false");
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

bool ResolutionProfileState::Open(const std::filesystem::path& settings_file, std::string* error) {
  if (settings_file.empty()) return false;
  file_ = settings_file; file_ += ".render-profiles.toml";
  if (!lock_.Acquire(file_, error, false)) return false;
  std::string text; bool exists = false;
  if (!ReadFileText(file_, &text, &exists)) {
    if (error) *error = "cannot read render profile journal";
    return false;
  }
  if (exists) {
    bool ok;
    const auto doc = toml::Document::Parse(text, &ok, error);
    const auto version = doc.Get("", "schema_version").value;
    if (!ok || (version != "1" && version != "2")) {
      if (error) *error = "unrecognized render profile journal; left untouched";
      return false;
    }
    // Version 1 also latched generic crashes and process kills. Its failed
    // signatures carry no evidence that a graphics allocation was refused.
    if (version == "2") failed_ = doc.Get("", "failed").value;
  }
  active_ = true;
  return true;
}

bool ResolutionProfileState::Recover(const GraphicsValues& requested, std::string* error) {
  if (!active_) return false;
  const auto signature = Signature(requested);
  if (failed_ != signature) failed_.clear();
  request_ = signature;
  pending_.clear();
  Save(error);
  return !failed_.empty();
}

bool ResolutionProfileState::Begin(const GraphicsValues& requested, SettingId context,
                                   std::string fingerprint, std::string* error) {
  if (!active_) return false;
  const auto signature = Signature(requested);
  if (pending_.contains(context) && pending_.at(context) == fingerprint && request_ == signature)
    return true;
  const auto previous_request = request_;
  const auto previous_pending = pending_;
  request_ = signature;
  // Only Recover compares a new saved request with the failure latch. Safe
  // running frames and changes still awaiting restart must not clear it.
  pending_[context] = std::move(fingerprint);
  if (Save(error)) return true;
  request_ = previous_request;
  pending_ = previous_pending;
  return false;
}

bool ResolutionProfileState::Rejected(std::string* error) {
  if (!active_ || pending_.empty() || request_.empty()) return false;
  failed_ = request_;
  return Save(error);
}

bool ResolutionProfileState::Closed(std::string* error) {
  if (!active_) return true;
  pending_.clear();
  active_ = false;
  return Save(error);
}

bool ResolutionProfileState::Save(std::string* error) {
  toml::Document doc;
  doc.Set("", "schema_version", "2");
  doc.Set("", "request", toml::RenderBasic(request_));
  doc.Set("", "failed", toml::RenderBasic(failed_));
  for (const auto& [id, value] : pending_)
    doc.Set("pending", SettingIdToken(id), toml::RenderBasic(value));
  return WriteFileAtomic(file_, doc.Render(), error);
}
}  // namespace rb_blitz::settings
