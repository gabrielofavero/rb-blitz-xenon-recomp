// SPDX-License-Identifier: GPL-2.0-only
#include "hooks/menu_music_metadata.h"

#include <string_view>
#include "ui/dtb_author.h"

namespace rb_blitz::shell_music {
namespace {
using ui::dtb::Node;

// songs.dta is an array of song records. Keep quoting/comments intact so display names,
// parentheses in strings and similarly named nested fields cannot become song keys.
class TextReader {
 public:
  explicit TextReader(std::string_view text) : text_(text) {
    if (text_.starts_with("\xEF\xBB\xBF")) text_.remove_prefix(3);
  }
  bool Read(std::vector<Node>* nodes) {
    while (Space() && pos_ < text_.size()) {
      Node node;
      if (!ReadNode(&node, 0)) return false;
      nodes->push_back(std::move(node));
    }
    return valid_;
  }

 private:
  bool Space() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++pos_; continue; }
      if (c == ';') {
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
        continue;
      }
      if (text_.substr(pos_, 2) == "/*") {
        const auto end = text_.find("*/", pos_ + 2);
        if (end == std::string_view::npos) return valid_ = false;
        pos_ = end + 2;
        continue;
      }
      break;
    }
    return valid_;
  }
  bool ReadNode(Node* node, size_t depth) {
    if (!Space() || pos_ == text_.size() || depth > 64 || ++nodes_ > 200000) return false;
    const char first = text_[pos_++];
    if (first == '(') {
      *node = Node::Array({});
      while (Space() && pos_ < text_.size() && text_[pos_] != ')') {
        Node child;
        if (!ReadNode(&child, depth + 1)) return false;
        node->kids.push_back(std::move(child));
      }
      if (!valid_ || pos_ == text_.size()) return false;
      ++pos_;
      return true;
    }
    if (first == ')') return false;
    std::string value;
    if (first == '"' || first == '\'') {
      bool closed = false;
      while (pos_ < text_.size()) {
        char c = text_[pos_++];
        if (c == first) { closed = true; break; }
        if (c == '\\' && pos_ < text_.size() &&
            (text_[pos_] == first || text_[pos_] == '\\')) c = text_[pos_++];
        value += c;
      }
      if (!closed) return false;
    } else {
      value += first;
      while (pos_ < text_.size()) {
        const char c = text_[pos_];
        if (c == '(' || c == ')' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        value += c;
        ++pos_;
      }
    }
    *node = first == '"' ? Node::String(value) : Node::Symbol(value);
    return true;
  }
  std::string_view text_;
  size_t pos_ = 0;
  size_t nodes_ = 0;
  bool valid_ = true;
};

const Node* Field(const Node& record, std::string_view key) {
  for (const Node& child : record.kids) {
    if (child.tag == ui::dtb::kArray && child.kids.size() >= 2 && child.kids[0].text == key)
      return &child;
  }
  return nullptr;
}
}  // namespace

std::vector<SongMetadata> ReadSongMetadata(std::span<const uint8_t> bytes, bool compiled,
                                         std::string* error) {
  if (error) error->clear();
  std::vector<Node> records;
  ui::dtb::RootHeader header;
  if (bytes.empty() || bytes.size() > 2 * 1024 * 1024 ||
      !(compiled ? ui::dtb::Parse(bytes.data(), bytes.size(), &records, &header, error)
                 : TextReader(std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                               bytes.size())).Read(&records))) {
    if (error && error->empty()) *error = "invalid or oversized song metadata";
    return {};
  }
  std::vector<SongMetadata> songs;
  for (const Node& record : records) {
    if (record.tag != ui::dtb::kArray || record.kids.empty() || record.kids[0].text.empty()) continue;
    const Node* song = Field(record, "song");
    const Node* name = song ? Field(*song, "name") : nullptr;
    if (name && !name->kids[1].text.empty())
      songs.push_back({record.kids[0].text, name->kids[1].text});
  }
  return songs;
}

std::string StreamIdentity(std::string path) {
  for (char& c : path) {
    if (c == '\\') c = '/';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  while (path.starts_with("./")) path.erase(0, 2);
  while (path.starts_with('/')) path.erase(0, 1);
  if (path.ends_with(".mogg")) path.resize(path.size() - 5);
  return path;
}

std::string SongKeyForStream(const std::vector<SongMetadata>& songs, const std::string& stream) {
  const std::string identity = StreamIdentity(stream);
  std::string key;
  for (const auto& song : songs) {
    if (StreamIdentity(song.stream) != identity) continue;
    if (!key.empty() && key != song.key) return {};  // ambiguous metadata must not pick another song
    key = song.key;
  }
  return key;
}
}  // namespace rb_blitz::shell_music
