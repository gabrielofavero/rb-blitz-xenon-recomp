// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The tab shell (launcher/src/shell.h, A1) and the two input sources.

#include "shell.h"

#include "imgui.h"

#include <memory>
#include <string>
#include <utility>

#include "row_ui.h"
#include "settings_edit.h"

namespace rb_blitz::launcher {
namespace {

// The one window's ImGui id. ImGuiWindowFlags_NoSavedSettings means ImGui never writes an ini
// for it, so the name is only an identity.
constexpr const char* kWindowId = "Rock Band Blitz Launcher";

// The scrolling region under the tab strip, so a tab taller than the window scrolls its rows
// while the strip and the unsaved-changes marker stay put.
constexpr const char* kBodyId = "##body";

// How many ring entries a tab's schema rows take. A tab's own block of rows starts after them
// (GeneralTab::kExtraRows, ControllerTab::kRowCount), so the loop that sizes the ring and the
// loop that draws it have to agree on this number - which is why both ask here rather than each
// doing its own arithmetic.
std::size_t SchemaFocusEntries(const TabLayout& tab) {
  std::size_t entries = 0;
  for (const LayoutGroup& group : tab.groups) {
    for (const settings::Setting* row : group.rows) {
      entries += FocusEntriesFor(*row);
    }
  }
  return entries;
}

// The keyboard's mapping (A1). ImGui's own navigation is deliberately off, so every key the
// launcher binds is translated here and nowhere else.
class KeyboardNavSource : public NavSource {
 public:
  NavAction Poll() override {
    // While a text field is being edited the ring must not steal the keys the caret needs -
    // ImGui's navigation is off, so nothing else would stop it - and Escape belongs to the
    // field (ImGui reverts it), not to the launcher. A second Escape, with no field active,
    // leaves.
    if (ImGui::GetIO().WantTextInput) {
      return NavAction::kNone;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_B, false)) {
      return NavAction::kCancel;
    }
    const bool shift = ImGui::GetIO().KeyShift;
    // A held arrow repeats; a held Tab does not, because Tab is also the focus-move key.
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
      return shift ? NavAction::kPrevious : NavAction::kNext;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
      return NavAction::kNext;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
      return NavAction::kPrevious;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
      return NavAction::kFirst;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
      return NavAction::kLast;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ||
        ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
      return NavAction::kNextTab;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) ||
        ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
      return NavAction::kPreviousTab;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      return NavAction::kActivate;
    }
    return NavAction::kNone;
  }
};

// The pad is a device like any other (A3, D6): what launcher/src/pad_source.h produces is the
// same NavAction the keyboard produces, so nothing below this point can tell which one moved the
// ring. The stub that stood here is what the pad was built behind.

// The help region's height in text lines (A2, D7). Two, and reserved whether or not the row's
// help needs them: the body is given the rest of the window *before* the help is drawn, so a bar
// that measured itself afterwards would be a bar whose height the body could not have been told.
constexpr int kHelpLines = 2;

// The bar's own height, from the same pieces the bar draws with: the line of controls and status,
// the help under it, and the window's bottom padding.
float BottomBarHeight(const ImGuiStyle& style) {
  return ImGui::GetFrameHeight() + style.ItemSpacing.y +
         ImGui::GetTextLineHeight() * static_cast<float>(kHelpLines) + style.WindowPadding.y;
}

// A run of text cut to what will fit in `max_lines` lines at this width, with an ellipsis where it
// was cut. The row's tooltip is a sentence or two and the bar has room for two lines of it: what
// does not fit is not shown rather than allowed to push the three actions off the bar (A2's
// "don't"). The cut is at a word boundary, because half a word reads as a rendering fault.
std::string FitToLines(const std::string& text, float wrap_width, int max_lines) {
  if (text.empty() || wrap_width <= 0.0f) {
    return text;
  }
  const float room = ImGui::GetTextLineHeight() * static_cast<float>(max_lines);
  if (ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap_width).y <= room) {
    return text;
  }
  constexpr const char* kEllipsis = "...";
  std::string fitted = text;
  while (!fitted.empty() &&
         ImGui::CalcTextSize((fitted + kEllipsis).c_str(), nullptr, false, wrap_width).y > room) {
    const std::size_t space = fitted.find_last_of(' ');
    if (space == std::string::npos) {
      return kEllipsis;
    }
    fitted.erase(space);
  }
  return fitted + kEllipsis;
}

const char* const kNoHelpText = "\xe2\x80\x94";  // D7's explicit empty state: an em dash.

}  // namespace

Shell::Shell(ProfileSession session, GameRoots roots, RowEnvironment environment,
             ShellEnvironment shell_environment)
    : log_(shell_environment.focus_log_path),
      roots_(std::move(roots)),
      session_(std::move(session)),
      general_(roots_),
      controller_(pads_),
      layout_(BuildLayout(environment)),
      keyboard_(std::make_unique<KeyboardNavSource>()) {
  if (shell_environment.gamepads) {
    gamepad_ = std::make_unique<GamepadNavSource>(pads_);
  }
  // D5's target fallback lives in the General tab, which applies it to what it shows rather than
  // to the profile (B4): with a write path, mutating the stored target here would have made a
  // window resize record a choice the user never made.
  rings_.resize(layout_.size());
  for (std::size_t index = 0; index < layout_.size(); ++index) {
    // A row takes one ring entry, except an enum, which takes one per choice because each
    // choice is drawn as its own radio (schema_view's FocusEntriesFor). B4's profile block is
    // focusable too and lives at the end of the General tab, and D16's mapping block at the end
    // of the Controller tab, so those tabs add their rows. A row the environment hides is not
    // in the layout at all, so the ring cannot land on one.
    const std::size_t extra = layout_[index].tab == settings::Tab::kGeneral
                                  ? GeneralTab::kExtraRows
                                  : (layout_[index].tab == settings::Tab::kController
                                         ? ControllerTab::kRowCount
                                         : 0);
    rings_[index].Reset(SchemaFocusEntries(layout_[index]) + extra);
  }
}

settings::Tab Shell::CurrentTab() const {
  return tab_ < layout_.size() ? layout_[tab_].tab : settings::Tab::kGeneral;
}

std::size_t Shell::FocusedRow() const {
  return tab_ < rings_.size() ? rings_[tab_].Index() : 0;
}

void Shell::RequestTab(int delta) {
  const int count = static_cast<int>(layout_.size());
  if (count == 0) {
    return;
  }
  int next = (static_cast<int>(tab_) + delta) % count;
  if (next < 0) {
    next += count;
  }
  tab_ = static_cast<std::size_t>(next);
}

void Shell::ApplyAction(NavAction action) {
  switch (action) {
    case NavAction::kNextTab:
      RequestTab(1);
      return;
    case NavAction::kPreviousTab:
      RequestTab(-1);
      return;
    default:
      break;
  }
  if (tab_ >= rings_.size()) {
    return;
  }
  FocusModel& ring = rings_[tab_];
  switch (action) {
    case NavAction::kNext:
      ring.MoveNext();
      break;
    case NavAction::kPrevious:
      ring.MovePrevious();
      break;
    case NavAction::kFirst:
      ring.MoveFirst();
      break;
    case NavAction::kLast:
      ring.MoveLast();
      break;
    default:
      // kActivate belongs to the tab - B1's General rows are the first with anything to
      // activate - so it is left alone here and handed to the tab when it is drawn.
      break;
  }
}

bool Shell::Frame() {
  // The pads are matched against what SDL says is connected before anything reads one, so a pad
  // plugged in this frame is usable in it: the ring's source and C5's capture both read the
  // registry rather than opening anything themselves.
  pads_.Refresh();

  // One action per frame from whichever device produced it (D6). The keyboard answers first
  // because it is the one a user can always reach - and because a pad whose stick is resting
  // against its stop should not be able to out-shout a deliberate key.
  NavAction action = keyboard_->Poll();
  const bool keyboard_acted = action != NavAction::kNone;
  const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
  if (keyboard_acted || mouse_delta.x != 0.0f || mouse_delta.y != 0.0f) {
    // The mouse and the keyboard share one set of hints: both are the desk, and the pad's glyphs
    // are for someone holding a pad.
    device_ = InputDevice::kKeyboard;
  }
  if (action == NavAction::kNone && gamepad_ != nullptr) {
    action = gamepad_->Poll();
  }
  if (!keyboard_acted && gamepad_ != nullptr && gamepad_->saw_input()) {
    // A press that a modal swallowed still counts as "the pad is what the user is holding".
    device_ = InputDevice::kGamepad;
  }
  LogPads();
  LogDevice();

  if (action == NavAction::kCancel) {
    // Escape belongs to the modal that is up - B4's reset confirmation, or B8's install
    // progress - and only means "leave the launcher" when nothing modal is on screen (A1). A
    // listen that is running owns Escape too: cancelling it is what a user pressing Escape
    // while counting down means, and quitting the launcher instead would be a trap.
    if (!general_.ModalOpen() && !controller_.Listening()) {
      return false;
    }
  }
  if (action == NavAction::kLaunch) {
    // Start, on a pad (D6): the launch the bottom bar's button offers, without the mouse. It is
    // refused in exactly the cases the button is disabled in, and it never reaches the tabs - a
    // row has no "launch me" of its own.
    if (!general_.ModalOpen() && !controller_.Listening() && !game_.Running()) {
      LaunchGame();
    }
    action = NavAction::kNone;
  }
  // A modal owns the launcher while it is up, so the tab cannot be changed behind it: its own
  // tab has to keep being drawn for its state machine to keep running, and walking away from it
  // would leave the helper installing with nothing watching. A listen needs the same handling
  // for the same reason - its countdown only advances while its tab is on screen.
  const bool owns_keyboard = general_.ModalOpen() || controller_.Listening();
  if (owns_keyboard &&
      (action == NavAction::kNextTab || action == NavAction::kPreviousTab)) {
    action = NavAction::kNone;
  }
  if (controller_.Listening() && !general_.ModalOpen() &&
      action == NavAction::kActivate) {
    // While a listen is running the key being pressed is the *input being captured*, not a
    // command: Space has to assign Space rather than press whatever the ring happens to be on.
    // Escape still cancels the listen, and clicking is what the ring's own button is for.
    action = NavAction::kNone;
  }
  ApplyAction(action);
  action_ = action;
  // After the ring has moved, so the trace names the row the user will see, and after the device
  // rule above, so a move and the device that made it land on one pair of lines.
  LogFocus();

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin(kWindowId, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  // The tab strip is the model's, not ImGui's tab bar. Two reasons: the selection must change
  // on the frame the key is read, and the ring - which the pad drives as well (A3) - has to be
  // the only thing that moves it. ImGui's own tab bar would also answer Ctrl+Tab behind our back.
  for (std::size_t index = 0; index < layout_.size(); ++index) {
    if (index != 0) {
      ImGui::SameLine();
    }
    const bool selected = index == tab_;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const std::string name = DisplayTabName(layout_[index].tab);
    if (ImGui::Button(name.c_str())) {
      tab_ = index;
    }
    if (selected) {
      ImGui::PopStyleColor();
    }
  }
  ImGui::Separator();

  // The tab body is its own scrolling region above the bottom bar, so a tab taller than the
  // window scrolls its rows while the strip and the bar stay where the user can reach them.
  // The bar's height is reserved here rather than measured afterwards: the body has to know how
  // much room it does *not* have before it draws, and the outer window must never scroll - a
  // scrolled window would move the bar off its own edge. The popups the tabs open are keyed off
  // this same id stack, which is why they are drawn from here too.
  const float bar_height = BottomBarHeight(ImGui::GetStyle());
  if (ImGui::BeginChild(kBodyId, ImVec2(0.0f, -bar_height), ImGuiChildFlags_None,
                        ImGuiWindowFlags_None)) {
    if (tab_ < layout_.size()) {
      // The two tabs with a block of their own after the schema's rows draw it here, once the
      // generic renderer has taken its share of the ring - which is why the count of rows a tab
      // draws and the count its ring was sized for are the same arithmetic in one place
      // (SchemaFocusEntries) rather than two.
      if (layout_[tab_].tab == settings::Tab::kGeneral) {
        general_.Draw(layout_[tab_], rings_[tab_], session_, action_);
      } else if (layout_[tab_].tab == settings::Tab::kController) {
        DrawTab(layout_[tab_], rings_[tab_]);
        controller_.Draw(SchemaFocusEntries(layout_[tab_]), rings_[tab_], session_, action_);
      } else {
        DrawTab(layout_[tab_], rings_[tab_]);
      }
    }
  }
  ImGui::EndChild();

  const bool running = DrawBottomBar();

  ImGui::End();
  action_ = NavAction::kNone;
  return running;
}

Shell::StatusLine Shell::CurrentStatus() const {
  StatusLine line;
  // Ordered by what the user has to act on: a failure first, then "this is not saved", then
  // whatever the last action did. A block that is fine says nothing at all - the bar is empty
  // on a first run, and that is the ordinary case.
  if (!save_error_.empty()) {
    line.text = save_error_;
    line.error = true;
    return line;
  }
  if (const std::string refusal = session_.Refusal(); !session_.CanSave() && !refusal.empty()) {
    line.text = refusal;
    line.error = true;
    return line;
  }
  if (session_.Dirty()) {
    line.text = "Unsaved changes";
    line.warning = true;
    return line;
  }
  if (!general_.status_error().empty()) {
    line.text = general_.status_error();
    line.error = true;
    return line;
  }
  if (!save_note_.empty()) {
    line.text = save_note_;
    return line;
  }
  line.text = general_.status_message();
  return line;
}

bool Shell::DrawBottomBar() {
  const ImGuiStyle& style = ImGui::GetStyle();
  const StatusLine status = CurrentStatus();
  // Where this line starts, so the help line can be placed under it from the same arithmetic
  // BottomBarHeight used - the bar cannot ask ImGui where it is once the text is drawn.
  const float line_top = ImGui::GetCursorPosY();

  // The status is the left half of the bar. It is drawn first, and when there is nothing to
  // say the line is still opened with a spacer: SameLine below continues from the *previous
  // line*, and with no line at all the buttons would be placed over the tab strip.
  if (!status.text.empty()) {
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text,
                          status.error    ? ImVec4(0.95f, 0.42f, 0.38f, 1.0f)
                          : status.warning ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f)
                                           : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(status.text.c_str());
    ImGui::PopStyleColor();
  } else {
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
  }

  const bool running = game_.Running();
  const char* labels[3] = {"Close", "Save", running ? "Game is running" : "Launch Game"};
  float total = 0.0f;
  for (const char* label : labels) {
    total += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
  }
  total += style.ItemSpacing.x * 2.0f;

  // Right-aligned, in the order they read: the two that end a session, then the one it is for.
  // The x is set explicitly rather than derived from the text, so a long status line cannot
  // push the buttons off the edge.
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - style.WindowPadding.x - total);
  if (ImGui::Button(labels[0])) {
    return false;
  }
  ImGui::SameLine();
  if (ImGui::Button(labels[1])) {
    save_error_.clear();
    const SaveOutcome outcome = session_.Save();
    if (!outcome.ok) {
      save_note_.clear();
      save_error_ = "Cannot save: " + outcome.error;
    } else {
      save_note_ = outcome.wrote ? "Saved" : "Nothing to save: the profile already matches";
    }
  }
  ImGui::SameLine();
  // A second copy of the title writing the same save folder is not something to discover by
  // trying, so the button stays down until the game the launcher started has exited.
  ImGui::BeginDisabled(running);
  if (ImGui::Button(labels[2])) {
    LaunchGame();
  }
  ImGui::EndDisabled();

  // Under the buttons: the focused row's own help, which is where D7 puts it and where RPCS3 puts
  // it too - the value is read before the press, not after a floating tooltip has come and gone.
  // The tooltip takes the room the hints do not, and the hints are measured first for that
  // reason: whatever else is cut, the row's help is not.
  const HelpEntry help = HelpForEntry(FocusedRow());
  const std::string hints = HintLine(help);
  const std::string* hint_source = hints.empty() ? nullptr : &hints;
  const float hints_width = hint_source == nullptr
                                ? 0.0f
                                : ImGui::CalcTextSize(hint_source->c_str()).x;
  const float left = style.WindowPadding.x;
  const float right = ImGui::GetWindowWidth() - style.WindowPadding.x;
  const float help_top = line_top + ImGui::GetFrameHeight() + style.ItemSpacing.y;
  const float tip_width = right - left - hints_width - style.ItemSpacing.x * 2.0f;

  ImGui::SetCursorPos(ImVec2(left, help_top));
  const std::string tip =
      help.text.empty() ? std::string(kNoHelpText) : FitToLines(help.text, tip_width, kHelpLines);
  if (help.text.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  }
  ImGui::PushTextWrapPos(left + tip_width);
  ImGui::TextUnformatted(tip.c_str());
  ImGui::PopTextWrapPos();
  if (help.text.empty()) {
    ImGui::PopStyleColor();
  }
  if (hint_source != nullptr) {
    // Back up to the help line's own top: the hints belong beside the row's help, not under it,
    // and a wrapped tooltip would otherwise have moved them down a line of its own.
    ImGui::SetCursorPos(ImVec2(right - hints_width, help_top));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(hint_source->c_str());
    ImGui::PopStyleColor();
  }
  return true;
}

Shell::HelpEntry Shell::HelpForEntry(std::size_t entry) const {
  if (tab_ >= layout_.size() || rings_[tab_].Empty()) {
    return HelpEntry{};
  }
  const TabLayout& tab = layout_[tab_];
  // The schema's own rows first, in the order the ring was sized in: the walk and the draw agree
  // because they count entries the same way (schema_view's FocusEntriesFor), which is the only
  // reason the bar can name the row the user is looking at.
  if (const settings::Setting* setting = SettingForEntry(tab, entry)) {
    return HelpEntry{FlattenHelpText(setting->tooltip), RowActionVerb(*setting)};
  }
  // What is left is the block a tab draws after its schema rows - B4's panel, D16's mapping table
  // - whose sentences only those blocks know, so they are asked rather than copied here.
  const std::size_t schema_entries = SchemaFocusEntries(tab);
  const std::size_t local = entry >= schema_entries ? entry - schema_entries : 0;
  switch (tab.tab) {
    case settings::Tab::kGeneral:
      return HelpEntry{ProfilePanel::HelpText(local), "Activate"};
    case settings::Tab::kController:
      return HelpEntry{ControllerTab::HelpText(local), "Activate"};
    case settings::Tab::kGraphics:
      break;
  }
  return HelpEntry{};
}

std::string Shell::HintLine(const HelpEntry& help) const {
  const bool pad = device_ == InputDevice::kGamepad && gamepad_ != nullptr;
  const PadButtonNames names = pad ? gamepad_->names() : PadButtonNames{};
  std::vector<std::string> parts;
  if (!help.verb.empty()) {
    parts.push_back(std::string(pad ? names.confirm : "Enter") + " " + std::string(help.verb));
  }
  parts.push_back(std::string(pad ? names.cancel : "Esc") + " Quit");
  parts.push_back(pad ? std::string(names.shoulder_left) + "/" + std::string(names.shoulder_right) +
                            " Switch tab"
                      : std::string("Tab Switch tab"));
  if (pad) {
    parts.push_back(std::string(names.start) + " Launch");
  }

  // Composed here and cut from the end if the window is too narrow for all of it: a hint that has
  // run under the row's help is worse than a hint nobody read, and the last one is the one a user
  // is least likely to need.
  const float room = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
  const float spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
  std::string line;
  float width = 0.0f;
  for (const std::string& part : parts) {
    const float part_width = ImGui::CalcTextSize(part.c_str()).x + (line.empty() ? 0.0f : spacing);
    if (!line.empty() && width + part_width > room) {
      break;
    }
    if (!line.empty()) {
      line += "    ";
    }
    line += part;
    width += part_width;
  }
  return line;
}

void Shell::LaunchGame() {
  save_error_.clear();
  save_note_.clear();
  // A change on screen that the run would not see is a bug the user cannot explain, so the
  // launcher writes first: "which profile did it read?" must not be a question.
  if (session_.CanSave() && session_.Dirty()) {
    const SaveOutcome outcome = session_.Save();
    if (!outcome.ok) {
      save_error_ = "Cannot save the settings, so the game was not started: " + outcome.error;
      return;
    }
  }
  const LaunchTarget target = FallbackTarget(session_.profile().target, general_.state());
  const LaunchCommand command = BuildLaunchCommand(session_, roots_, target);
  std::string error;
  if (!game_.Start(command, &error)) {
    save_error_ = "Cannot start the game: " + error;
    return;
  }
  save_note_ = "Started " + command.executable.filename().string();
}

void Shell::LogFocus() {
  if (!log_.enabled()) {
    return;
  }
  const settings::Tab tab = CurrentTab();
  const std::size_t entry = FocusedRow();
  if (logged_focus_ && tab == logged_tab_ && entry == logged_entry_) {
    return;
  }
  logged_focus_ = true;
  logged_tab_ = tab;
  logged_entry_ = entry;

  const std::size_t count = tab_ < rings_.size() ? rings_[tab_].Count() : 0;
  std::string text = "tab=";
  text += settings::TabName(tab);
  text += " entry=" + std::to_string(entry) + "/" + std::to_string(count);
  if (tab_ < layout_.size()) {
    if (const settings::Setting* setting = SettingForEntry(layout_[tab_], entry)) {
      text += " row=";
      text += setting->key;
    } else {
      // The block a tab draws after its rows: named rather than numbered, because "which button
      // of the mapping table is that?" is the question the trace is meant to answer.
      text += tab == settings::Tab::kGeneral ? " row=settings-file-block"
                                             : " row=button-mapping-block";
    }
  }  const HelpEntry help = HelpForEntry(entry);
  if (!help.verb.empty()) {
    text += " enter=";
    text += help.verb;
  }
  log_.Write("focus", text);
}

void Shell::LogDevice() {
  if (!log_.enabled() || (logged_device_.has_value() && *logged_device_ == device_)) {
    return;
  }
  logged_device_ = device_;
  if (device_ != InputDevice::kGamepad || gamepad_ == nullptr) {
    log_.Write("device", "keyboard");
    return;
  }
  const PadButtonNames names = gamepad_->names();
  std::string text = "gamepad name=\"";
  text += gamepad_->name();
  text += "\" confirm=";
  text += names.confirm;
  text += " cancel=";
  text += names.cancel;
  text += " shoulders=";
  text += names.shoulder_left;
  text += "/";
  text += names.shoulder_right;
  text += " start=";
  text += names.start;
  log_.Write("device", text);
}

void Shell::LogPads() {
  if (!log_.enabled()) {
    return;
  }
  const std::string name = gamepad_ == nullptr ? std::string() : gamepad_->name();
  if (logged_pads_ && pads_.count() == logged_pad_count_ && name == logged_pad_name_) {
    return;
  }
  logged_pads_ = true;
  logged_pad_count_ = pads_.count();
  logged_pad_name_ = name;
  std::string text = "count=" + std::to_string(pads_.count());
  if (!name.empty()) {
    text += " name=\"";
    text += name;
    text += "\"";
  }
  log_.Write("pads", text);
}

void Shell::DrawTab(const TabLayout& tab, FocusModel& ring) {
  std::size_t row_index = 0;
  for (const LayoutGroup& group : tab.groups) {
    ImGui::SeparatorText(group.group->name.data());
    for (const settings::Setting* row : group.rows) {
      const settings::Setting& setting = *row;
      RowScope scope(setting.key);
      // A row whose widget is a line of radios (an enum) needs the width for them, so it takes
      // most of the row and keeps its label in what is left; everything else is a single
      // widget in a value column beside a full-width label.
      const RowColumns columns =
          RowColumnWidths(setting.kind == settings::Kind::kEnum ? 0.72f : 0.35f);
      DrawRowLabel(setting, row_index, ring, columns.label_width);
      ImGui::SameLine();
      // B2: the row is the widget its kind calls for, and what it changes lands in the
      // profile - which B4's Save is what writes to disk.
      DrawEditableSetting(setting, columns.value_width, row_index, ring, action_, session_);
      // D12: the row's own truth. A restart row says so rather than letting a user believe the
      // change took effect while the game is not even running.
      if (setting.applies == settings::Applies::kRestart) {
        ImGui::SameLine();
        ImGui::TextDisabled("(needs restart)");
      }
      // B4's precedence badge: a row the game's own file decides says so here too, so the
      // warning does not depend on which tab the user is looking at.
      DrawOverrideBadge(setting, session_);
      row_index += FocusEntriesFor(setting);
    }
  }
}

}  // namespace rb_blitz::launcher
