// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// Implementation of the shared binding table (controller-settings/src/binding_table.h).

#include "binding_table.h"

#include "imgui.h"

#include <algorithm>
#include <utility>

#include "row_ui.h"

namespace rb_blitz::profile {
namespace {

// The warning the block already uses for a binding that presses nothing.
const ImVec4 kWarning{0.95f, 0.75f, 0.25f, 1.0f};

}  // namespace

bool DrawBlockButton(std::size_t index, FocusModel& ring, NavAction action, const char* label,
                     bool enabled) {
  const bool focused = action == NavAction::kActivate && !ring.Empty() && ring.Index() == index;
  const bool in_ring = !ring.Empty() && ring.Index() == index;
  if (!enabled) {
    ImGui::BeginDisabled();
  }
  if (in_ring) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked = ImGui::Button(label);
  if (in_ring) {
    ImGui::PopStyleColor();
  }
  DrawFocusOutline(in_ring);
  if (!enabled) {
    ImGui::EndDisabled();
  }
  AdoptRingOnHover(index, ring);
  if (in_ring && !ImGui::IsItemVisible()) {
    ImGui::SetScrollHereY(0.5f);
  }
  return enabled && (clicked || focused);
}

namespace {
// The measured spans, drawn source by source: an enabled source in the text colour, a disabled
// one in the subtitle colour with a line through it, and the separators in between untouched.
// The strike is drawn per wrapped line, under the characters it crosses rather than across the
// whole cell.
void DrawSpans(const std::vector<binding_text::Span>& spans, float max_width) {
  const binding_text::Measure measure = [](std::string_view text) {
    return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
  };
  const binding_text::Layout layout = binding_text::Wrap(spans, max_width, measure);
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const float thickness = binding_text::StrikeThickness(ImGui::GetFontSize());
  const ImU32 disabled_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  bool first_in_line = true;
  for (const binding_text::Line& line : layout.lines) {
    first_in_line = true;
    for (const binding_text::Segment& segment : line.segments) {
      // SameLine with no spacing keeps the measured layout: every segment begins exactly where
      // the wrap put it, separators included.
      if (!first_in_line) {
        ImGui::SameLine(0.0f, 0.0f);
      }
      first_in_line = false;
      if (segment.struck) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      }
      ImGui::TextUnformatted(segment.text.data(), segment.text.data() + segment.text.size());
      if (segment.struck) {
        ImGui::PopStyleColor();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = (min.y + max.y) * 0.5f;
        draw_list->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), disabled_color, thickness);
      }
    }
  }
}

}  // namespace

BindingTableResult DrawBindingTable(const char* table_id, const std::vector<BindingRowView>& rows,
                                    std::size_t first_row, FocusModel& ring, NavAction action) {
  BindingTableResult result;
  // Three columns with an equal share of the width each, so the columns sit at even intervals
  // down the whole list and the row reads as one small table (D16's original arrangement).
  ImGui::BeginTable(table_id, 3,
                    ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings);
  ImGui::TableSetupColumn("Control");
  ImGui::TableSetupColumn("Assigned to");
  ImGui::TableSetupColumn("##actions");
  for (std::size_t position = 0; position < rows.size(); ++position) {
    const BindingRowView& row = rows[position];
    // A table disambiguates its columns but not its rows: every row's "Assign" would be the same
    // ImGui id and the click would land on the first row that submitted one.
    ImGui::PushID(static_cast<int>(row.id));
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(row.label.data(), row.label.data() + row.label.size());

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (row.listening_here) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::TextUnformatted(row.listening_text.c_str());
      ImGui::PopStyleColor();
    } else if (row.nothing_bound) {
      ImGui::PushStyleColor(ImGuiCol_Text, kWarning);
      ImGui::TextUnformatted("Nothing can press this");
      ImGui::PopStyleColor();
    } else if (!row.spans.empty()) {
      // The cell's own width, not the window's: a long binding wraps inside the column instead of
      // running under the buttons.
      const float wrap = std::max(1.0f, ImGui::GetContentRegionAvail().x);
      DrawSpans(row.spans, wrap);
      if (!row.disabled_hint.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", row.disabled_hint.c_str());
      }
    }

    ImGui::TableSetColumnIndex(2);
    const std::size_t assign_index = first_row + position * 2;
    if (DrawBlockButton(assign_index, ring, action, row.listening_here ? "Cancel" : "Assign",
                        row.assign_enabled)) {
      result.assign = row.id;
    }
    ImGui::SameLine();
    if (DrawBlockButton(assign_index + 1, ring, action, "Reset",
                        row.reset_enabled && !row.listening)) {
      result.reset = row.id;
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
  return result;
}

}  // namespace rb_blitz::profile
