// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "ui/dtb_author.h"

namespace rb_blitz::ui::portrait_dialog {
// Reuse the native Confirm branch verbatim. The host requests one action;
// the guest's normal poll hides its panel and restores its own return panel.
inline bool Install(std::vector<dtb::Node>& roots) {
  using dtb::Node;
  for (auto& root : roots) {
    if (root.tag != dtb::kCommand || root.kids.size() < 3 ||
        root.kids[2].text != "error_popup_panel") continue;
    const auto method = [&](std::string_view name) -> Node* {
      for (auto& child : root.kids)
        if (child.tag == dtb::kArray && !child.kids.empty() && child.kids[0].text == name) return &child;
      return nullptr;
    };
    auto* show = method("show_popup");
    auto* hide = method("hide_popup");
    auto* buttons = method("BUTTON_DOWN_MSG");
    if (!show || !hide || !buttons) return false;
    const auto branch = [&](const auto& self, const Node& node) -> const Node* {
      if (node.tag == dtb::kArray && node.kids.size() > 1 &&
          node.kids[0].text == "kAction_Confirm") return &node;
      for (const auto& child : node.kids) if (const auto* found = self(self, child)) return found;
      return nullptr;
    };
    const auto* confirm = branch(branch, *buttons);
    if (!confirm) return false;
    Node action = Node::Command({Node::Symbol("do")});
    action.kids.insert(action.kids.end(), confirm->kids.begin() + 1, confirm->kids.end());
    const auto query = [](const char* suffix) {
      return Node::Command({Node::Symbol("file_exists"),
          Node::String(std::string("rbbz-portrait-dialog/") + suffix)});
    };
    show->kids.push_back(query("open"));
    hide->kids.push_back(query("close"));
    auto dispatch = Node::Command({Node::Symbol("if"), query("confirm"), std::move(action)});
    if (auto* poll = method("poll")) poll->kids.push_back(std::move(dispatch));
    else root.kids.push_back(Node::Array({Node::Symbol("poll"), std::move(dispatch)}));
    return true;
  }
  return false;
}
}
