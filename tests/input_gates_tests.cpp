// SPDX-License-Identifier: GPL-2.0-only
// rb_blitz - ReXGlue Recompiled Project
//
// The source gates, the physical snapshot and the gameplay edges
// (src/input/source_gates.h; P2 of docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md).
// Dependency-free like the remap suites: no pad, no window, no SDL, no boot - the device side is
// the plain data a sampler hands in.

#include "check.h"
#include "input/pad_packet.h"
#include "input/source_gates.h"
#include "profile/gameplay_remap.h"
#include "profile/remap.h"

#include <set>
#include <string>
#include <vector>

using namespace rb_blitz::input;
using namespace rb_blitz::test;
namespace gameplay = rb_blitz::profile::gameplay_remap;
namespace remap = rb_blitz::profile::remap;

namespace {

remap::Source Pad(std::string name) {
  return remap::Source{remap::SourceKind::kPad, std::move(name)};
}
remap::Source Key(std::string name) {
  return remap::Source{remap::SourceKind::kKey, std::move(name)};
}
remap::Source Mouse(std::string name) {
  return remap::Source{remap::SourceKind::kMouse, std::move(name)};
}

// A physical layer with a keyboard the test drives by name.
class FakeDevices {
 public:
  PhysicalInput Snapshot() const {
    PhysicalInput input;
    input.window_focused = focused;
    input.pad_connected = pad_connected;
    input.pad = pad;
    input.mouse_buttons = mouse_buttons;
    input.key_down = [this](std::string_view name) {
      return keys_.count(std::string(name)) != 0;
    };
    return input;
  }

  void KeyDown(const std::string& name) { keys_.insert(name); }
  void KeyUp(const std::string& name) { keys_.erase(name); }
  void ReleaseAll() { keys_.clear(); }

  bool focused = true;
  bool pad_connected = false;
  remap::PadState pad;
  uint32_t mouse_buttons = 0;

 private:
  std::set<std::string> keys_;
};

const SourceGates kOpen{true, true};

}  // namespace

int main() {
  BeginCase("a source kind maps to exactly one device family");
  CHECK_TRUE(DeviceOf(remap::SourceKind::kPad) == SourceDevice::kPad);
  CHECK_TRUE(DeviceOf(remap::SourceKind::kKey) == SourceDevice::kKeyboard);
  CHECK_TRUE(DeviceOf(remap::SourceKind::kMouse) == SourceDevice::kMouse);
  CHECK_EQ(MouseMaskFor("left"), mouse_button::kLeft);
  CHECK_EQ(MouseMaskFor("right"), mouse_button::kRight);
  CHECK_EQ(MouseMaskFor("middle"), mouse_button::kMiddle);
  CHECK_EQ(MouseMaskFor("x1"), mouse_button::kX1);
  CHECK_EQ(MouseMaskFor("x2"), mouse_button::kX2);
  CHECK_EQ(MouseMaskFor("scroll"), 0);
  // SDL's own mask value: `SDL_GetMouseState()` result bits are handed in untranslated.
  CHECK_EQ(mouse_button::kLeft, 1);
  CHECK_EQ(mouse_button::kRight, 4);

  BeginCase("the keyboard gate blocks keys and leaves the pad alone");
  {
    FakeDevices devices;
    devices.KeyDown("Space");
    devices.pad.buttons = remap::TargetBit(remap::Target::kA);
    devices.pad_connected = true;
    const SourceGates keyboard_off{false, true};
    CHECK_FALSE(SourceHeld(devices.Snapshot(), keyboard_off, Key("Space")));
    CHECK_TRUE(SourceHeld(devices.Snapshot(), keyboard_off, Pad("a")));
    CHECK_TRUE(SourceHeld(devices.Snapshot(), kOpen, Key("Space")));
  }

  BeginCase("the mouse gate blocks buttons, and no other gate touches them");
  {
    FakeDevices devices;
    devices.mouse_buttons = mouse_button::kLeft | mouse_button::kRight;
    const SourceGates mouse_off{true, false};
    CHECK_FALSE(SourceHeld(devices.Snapshot(), mouse_off, Mouse("left")));
    CHECK_TRUE(SourceHeld(devices.Snapshot(), kOpen, Mouse("left")));
    CHECK_TRUE(SourceHeld(devices.Snapshot(), kOpen, Mouse("right")));
    CHECK_FALSE(SourceHeld(devices.Snapshot(), kOpen, Mouse("middle")));
    CHECK_FALSE(SourceHeld(devices.Snapshot(), kOpen, Mouse("scroll")));
  }

  BeginCase("an unfocused window plays nothing from any source");
  {
    FakeDevices devices;
    devices.KeyDown("Space");
    devices.mouse_buttons = mouse_button::kLeft;
    devices.pad.buttons = remap::TargetBit(remap::Target::kA);
    devices.pad_connected = true;
    devices.focused = false;
    CHECK_FALSE(SourceHeld(devices.Snapshot(), kOpen, Key("Space")));
    CHECK_FALSE(SourceHeld(devices.Snapshot(), kOpen, Mouse("left")));
    CHECK_FALSE(SourceHeld(devices.Snapshot(), kOpen, Pad("a")));
  }

  BeginCase("pad sources answer from the physical pad only, controls and directions alike");
  {
    FakeDevices devices;
    remap::PadState state;
    state.buttons = remap::TargetBit(remap::Target::kA);
    state.left_trigger = 0xFF;
    state.left_y = remap::kStickThreshold + 1;
    CHECK_TRUE(remap::PadSourceHeld(state, "a"));
    CHECK_FALSE(remap::PadSourceHeld(state, "b"));
    CHECK_TRUE(remap::PadSourceHeld(state, "left_trigger"));
    CHECK_FALSE(remap::PadSourceHeld(state, "right_trigger"));
    CHECK_TRUE(remap::PadSourceHeld(state, "lstick_up"));
    CHECK_FALSE(remap::PadSourceHeld(state, "lstick_down"));
    CHECK_FALSE(remap::PadSourceHeld(state, "not_a_control"));
    // The threshold is pinned, and exactly-at-threshold is not yet a direction.
    state.left_y = remap::kStickThreshold;
    CHECK_FALSE(remap::PadSourceHeld(state, "lstick_up"));
    state.left_y = -remap::kStickThreshold - 1;
    CHECK_TRUE(remap::PadSourceHeld(state, "lstick_down"));
    // A state with nothing pressed answers nothing, whatever another device may have synthesized.
    remap::PadState neutral;
    CHECK_FALSE(remap::PadSourceHeld(neutral, "a"));
    CHECK_FALSE(remap::PadSourceHeld(neutral, "lstick_left"));
  }

  BeginCase("each gameplay table fires only its own source kinds");
  {
    FakeDevices devices;
    devices.KeyDown("Space");
    devices.mouse_buttons = mouse_button::kLeft;
    devices.pad.buttons = remap::TargetBit(remap::Target::kX);
    devices.pad_connected = true;

    // The defaults are of each table's own kind ...
    gameplay::Table controller(gameplay::TableKind::kController);
    gameplay::Table keyboard_mouse(gameplay::TableKind::kKeyboardMouse);
    CHECK_TRUE(ActionHeld(devices.Snapshot(), kOpen, controller, gameplay::Action::kPowerup));
    CHECK_TRUE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kPowerup));

    // ... and a wrong-kind source injected past the parser never fires the table it is in.
    gameplay::Table wrong_in_keyboard(gameplay::TableKind::kKeyboardMouse);
    wrong_in_keyboard.Set(gameplay::Action::kPowerup, {Pad("x")});
    CHECK_FALSE(
        ActionHeld(devices.Snapshot(), kOpen, wrong_in_keyboard, gameplay::Action::kPowerup));
    gameplay::Table wrong_in_controller(gameplay::TableKind::kController);
    wrong_in_controller.Set(gameplay::Action::kPowerup, {Key("Space"), Mouse("left")});
    CHECK_FALSE(
        ActionHeld(devices.Snapshot(), kOpen, wrong_in_controller, gameplay::Action::kPowerup));
  }

  BeginCase("one source on two actions fires both, and a second source keeps the action held");
  {
    FakeDevices devices;
    devices.KeyDown("J");
    devices.pad.buttons = remap::TargetBit(remap::Target::kA);
    devices.pad_connected = true;

    // The shipped keyboard defaults put J on Second Note, and the controller defaults put A there
    // too: pressed together, one action, either released, still held.
    gameplay::Table keyboard_mouse(gameplay::TableKind::kKeyboardMouse);
    gameplay::Table controller(gameplay::TableKind::kController);
    CHECK_TRUE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kSecondNote));
    CHECK_TRUE(ActionHeld(devices.Snapshot(), kOpen, controller, gameplay::Action::kSecondNote));
    devices.KeyUp("J");
    CHECK_FALSE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kSecondNote));
    CHECK_TRUE(ActionHeld(devices.Snapshot(), kOpen, controller, gameplay::Action::kSecondNote));
    devices.pad.buttons = 0;
    CHECK_FALSE(ActionHeld(devices.Snapshot(), kOpen, controller, gameplay::Action::kSecondNote));

    // One key bound to two actions in one table: both fire, neither is silently dropped.
    gameplay::Table shared(gameplay::TableKind::kKeyboardMouse);
    shared.Set(gameplay::Action::kFirstNote, {Key("G")});
    shared.Set(gameplay::Action::kSecondNote, {Key("G")});
    CHECK_FALSE(ActionHeld(devices.Snapshot(), kOpen, shared, gameplay::Action::kFirstNote));
    devices.KeyDown("G");
    CHECK_TRUE(ActionHeld(devices.Snapshot(), kOpen, shared, gameplay::Action::kFirstNote));
    CHECK_TRUE(ActionHeld(devices.Snapshot(), kOpen, shared, gameplay::Action::kSecondNote));
    CHECK_TRUE(remap::FormatSource(*HeldSource(devices.Snapshot(), kOpen, shared,
                                               gameplay::Action::kFirstNote)) == "key:G");
  }

  BeginCase("a key on a gameplay action and General Mapping stay independent");
  {
    FakeDevices devices;
    devices.KeyDown("K");
    // Change General Mapping's A binding; the gameplay table's Second Note still answers to its
    // own sources (the shipped keyboard defaults) and not to General Mapping's rows.
    remap::Table general = remap::Table::FromRows({});
    general.Set(remap::Target::kA, {Key("K")});
    gameplay::Table keyboard_mouse(gameplay::TableKind::kKeyboardMouse);
    CHECK_FALSE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kFirstNote));
    CHECK_FALSE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kSecondNote));
    // ... while the General Mapping rewrite does fire A from the same key.
    remap::PadState out;
    struct DownCtx {
      PhysicalInput input;
      SourceGates gates;
    } ctx{devices.Snapshot(), kOpen};
    remap::Apply(general, remap::PadState{}, [](void* raw, const remap::Source& source) {
      auto* view = static_cast<DownCtx*>(raw);
      return SourceHeld(view->input, view->gates, source);
    }, &ctx, &out);
    CHECK_EQ(out.buttons & remap::TargetBit(remap::Target::kA),
             remap::TargetBit(remap::Target::kA));
  }

  BeginCase("action edges are one per press, one per release, and none for a quiet poll");
  {
    ActionHold hold;
    CHECK_FALSE(hold.held);
    ActionHold::Edge edge = hold.Observe(true);
    CHECK_TRUE(edge.pressed && !edge.released);
    CHECK_TRUE(hold.held);
    CHECK_FALSE(hold.Observe(true).any());
    edge = hold.Observe(false);
    CHECK_TRUE(edge.released && !edge.pressed);
    CHECK_FALSE(hold.held);
    CHECK_FALSE(hold.Observe(false).any());
    // A rapid press-release-press sequence keeps every edge.
    CHECK_TRUE(hold.Observe(true).pressed);
    CHECK_TRUE(hold.Observe(false).released);
    CHECK_TRUE(hold.Observe(true).pressed);
  }

  BeginCase("a boundary drops the hold without inventing a release, and re-arms on release");
  {
    ActionHold hold;
    hold.Observe(true);
    hold.Invalidate();
    CHECK_FALSE(hold.held);
    // Still held across the boundary: no press edge, because the new context must not inherit the
    // key the old one was holding (plan D3's release-before-rearming rule).
    CHECK_FALSE(hold.Observe(true).any());
    CHECK_FALSE(hold.held);
    CHECK_FALSE(hold.Observe(false).any());  // the release only arms the next press
    CHECK_TRUE(hold.Observe(true).pressed);  // and pressing again presses
  }

  BeginCase("a table acts only while playing and only while its own switch is on");
  {
    const GameplayRouting both_on{true, true};
    const GameplayRouting controller_only{true, false};
    const GameplayRouting keyboard_only{false, true};
    const GameplayRouting both_off{false, false};
    for (const GameplayRouting routing : {both_on, controller_only, keyboard_only, both_off}) {
      CHECK_FALSE(TableActive(routing, gameplay::TableKind::kController, false));
      CHECK_FALSE(TableActive(routing, gameplay::TableKind::kKeyboardMouse, false));
    }
    CHECK_TRUE(TableActive(both_on, gameplay::TableKind::kController, true));
    CHECK_TRUE(TableActive(both_on, gameplay::TableKind::kKeyboardMouse, true));
    CHECK_TRUE(TableActive(controller_only, gameplay::TableKind::kController, true));
    CHECK_FALSE(TableActive(controller_only, gameplay::TableKind::kKeyboardMouse, true));
    CHECK_FALSE(TableActive(keyboard_only, gameplay::TableKind::kController, true));
    CHECK_TRUE(TableActive(keyboard_only, gameplay::TableKind::kKeyboardMouse, true));
    CHECK_FALSE(TableActive(both_off, gameplay::TableKind::kController, true));
    CHECK_FALSE(TableActive(both_off, gameplay::TableKind::kKeyboardMouse, true));
  }

  BeginCase("General Mapping's key/mouse sources follow the gates and the context");
  {
    // Menus: keys drive the UI, the native pointer owns the mouse.
    CHECK_TRUE(GeneralSourceAllowed(kOpen, false, false, false, remap::SourceKind::kKey));
    CHECK_FALSE(GeneralSourceAllowed(kOpen, false, false, false, remap::SourceKind::kMouse));
    // Playing with both gameplay switches off: General Mapping feeds the native preset.
    CHECK_TRUE(GeneralSourceAllowed(kOpen, true, false, false, remap::SourceKind::kKey));
    CHECK_TRUE(GeneralSourceAllowed(kOpen, true, false, false, remap::SourceKind::kMouse));
    // Playing with either direct table on: gameplay is the tables' business.
    CHECK_FALSE(GeneralSourceAllowed(kOpen, true, true, false, remap::SourceKind::kKey));
    CHECK_FALSE(GeneralSourceAllowed(kOpen, true, false, true, remap::SourceKind::kKey));
    CHECK_FALSE(GeneralSourceAllowed(kOpen, true, true, false, remap::SourceKind::kMouse));
    // A pad source is the rewrite's own; a closed gate stops its device everywhere.
    CHECK_FALSE(GeneralSourceAllowed(kOpen, false, false, false, remap::SourceKind::kPad));
    CHECK_FALSE(GeneralSourceAllowed({false, true}, false, false, false, remap::SourceKind::kKey));
    CHECK_FALSE(
        GeneralSourceAllowed({true, false}, true, false, false, remap::SourceKind::kMouse));
    CHECK_TRUE(GeneralSourceAllowed({true, false}, false, false, false, remap::SourceKind::kKey));
  }

  BeginCase("host actions belong to player one, and the synthetic device keeps a host connected");
  {
    CHECK_TRUE(IsGameplayUser(0));
    CHECK_FALSE(IsGameplayUser(1));
    CHECK_FALSE(IsGameplayUser(2));
    CHECK_FALSE(IsGameplayUser(3));
    CHECK_TRUE(HostDeviceConnected({true, true}));
    CHECK_TRUE(HostDeviceConnected({true, false}));
    CHECK_TRUE(HostDeviceConnected({false, true}));
    CHECK_FALSE(HostDeviceConnected({false, false}));
  }

  BeginCase("the cursor is hidden in gameplay and visible wherever a menu can use it");
  {
    // Gameplay: no native menu, no overlay capture -> hidden.
    CHECK_FALSE(CursorVisible(true, false, false));
    // A native menu, an overlay that asked, or both -> visible.
    CHECK_TRUE(CursorVisible(true, true, false));
    CHECK_TRUE(CursorVisible(true, false, true));
    // Mouse support off hides it even with a menu up.
    CHECK_FALSE(CursorVisible(false, true, false));
    CHECK_FALSE(CursorVisible(false, true, true));
  }

  BeginCase("a remapped key changes the delivered pad, and the packet number says so");
  {
    // The guest skips unchanged packet numbers, so the number has to follow the *delivered* state:
    // a key that General Mapping turns into A must bump it even though no device counter moved.
    FakeDevices devices;
    remap::Table table = remap::Table::FromRows({});
    table.Set(remap::Target::kA, {Key("G")});
    PadPacket packets;
    auto deliver = [&] {
      remap::PadState out;
      struct DownCtx {
        PhysicalInput input;
        SourceGates gates;
      } ctx{devices.Snapshot(), kOpen};
      remap::Apply(table, remap::PadState{}, [](void* raw, const remap::Source& source) {
        auto* view = static_cast<DownCtx*>(raw);
        return SourceHeld(view->input, view->gates, source);
      }, &ctx, &out);
      return packets.Observe({out.buttons, out.left_trigger, out.right_trigger, out.left_x,
                              out.left_y, out.right_x, out.right_y});
    };
    const uint32_t quiet = deliver();
    CHECK_EQ(deliver(), quiet);
    devices.KeyDown("G");
    const uint32_t pressed = deliver();
    CHECK_TRUE(pressed != quiet);
    CHECK_EQ(deliver(), pressed);
    devices.KeyUp("G");
    CHECK_TRUE(deliver() != pressed);
  }

  BeginCase("no physical pad means every controller source is inert, keys still work");
  {
    FakeDevices devices;  // pad_connected false, no buttons
    devices.KeyDown("Space");
    gameplay::Table controller(gameplay::TableKind::kController);
    gameplay::Table keyboard_mouse(gameplay::TableKind::kKeyboardMouse);
    for (const gameplay::Action action : gameplay::Actions()) {
      CHECK_FALSE(ActionHeld(devices.Snapshot(), kOpen, controller, action));
    }
    CHECK_TRUE(
        ActionHeld(devices.Snapshot(), kOpen, keyboard_mouse, gameplay::Action::kPowerup));
    CHECK_TRUE(HostDeviceConnected(kOpen));
  }

  BeginCase("Start is the game's own pause bit, and the controller table owns its pad controls");
  {
    // The bare bit above must be the pad's Start, or the overlay would raise a button the game
    // does not read as Pause.
    CHECK_EQ(kStartButtonBit, remap::TargetBit(remap::Target::kStart));
    const gameplay::Table controller(gameplay::TableKind::kController);
    // Preset 0's controls: the four D-pad directions, A, X, both shoulders and Start. The
    // triggers and the stick directions have no bit (analog), so the mask never names them.
    CHECK_EQ(ControllerOwnedButtons(controller), uint16_t{0x531F});
    // A table whose rows bind nothing owns nothing, and a key source owns no pad control.
    gameplay::Table empty(gameplay::TableKind::kController);
    for (const gameplay::Action action : gameplay::Actions()) {
      empty.Set(action, {});
    }
    CHECK_EQ(ControllerOwnedButtons(empty), uint16_t{0x0000});
    empty.Set(gameplay::Action::kPowerup, {Key("Space")});
    CHECK_EQ(ControllerOwnedButtons(empty), uint16_t{0x0000});
    // A stick direction is travel, not a control: only `pad:x` contributes a bit.
    empty.Set(gameplay::Action::kPowerup, {Pad("x"), Pad("lstick_up")});
    CHECK_EQ(ControllerOwnedButtons(empty), uint16_t{0x4000});
  }

  BeginCase("the pad overlay clears what the controller table owns and raises Pause only");
  {
    const uint16_t owned = 0x4000;
    // Controller routing: its controls and Start are taken out of the delivered pad.
    const GameplayPadOverlay controller =
        GameplayOverlay(true, false, false, owned);
    CHECK_EQ(controller.clear_buttons, uint16_t{owned | kStartButtonBit});
    CHECK_EQ(controller.set_buttons, uint16_t{0});
    // A held Pause puts Start back, whether or not the table binds it.
    CHECK_EQ(GameplayOverlay(true, false, true, owned).set_buttons, kStartButtonBit);
    // Keyboard + Mouse only: the native pad path is untouched, so nothing is cleared - but a
    // direct Pause still has to reach the same screen.
    const GameplayPadOverlay keyboard_only =
        GameplayOverlay(false, true, false, owned);
    CHECK_EQ(keyboard_only.clear_buttons, uint16_t{0});
    CHECK_EQ(keyboard_only.set_buttons, uint16_t{0});
    const GameplayPadOverlay keyboard_pause = GameplayOverlay(false, true, true, owned);
    CHECK_EQ(keyboard_pause.clear_buttons, uint16_t{0});
    CHECK_EQ(keyboard_pause.set_buttons, kStartButtonBit);
    // In a menu neither table routes: the pad is the menu's own.
    const GameplayPadOverlay menu = GameplayOverlay(false, false, false, owned);
    CHECK_EQ(menu.clear_buttons, uint16_t{0});
    CHECK_EQ(menu.set_buttons, uint16_t{0});
  }

  BeginCase("a tap between two visits arrives as a press and a release, in that order");
  {
    ActionEdges edges;
    // Nothing happened: nothing to deliver.
    CHECK_FALSE(edges.Pending(gameplay::Action::kFirstNote));
    edges.Observe(gameplay::Action::kFirstNote, true);
    edges.Observe(gameplay::Action::kFirstNote, false);
    CHECK_TRUE(edges.Pending(gameplay::Action::kFirstNote));
    bool out[2] = {false, true};
    CHECK_EQ(edges.Drain(gameplay::Action::kFirstNote, out, 2), std::size_t{2});
    CHECK_TRUE(out[0]);
    CHECK_FALSE(out[1]);
    CHECK_FALSE(edges.Pending(gameplay::Action::kFirstNote));
    CHECK_FALSE(edges.Held(gameplay::Action::kFirstNote));
    // A quiet poll produces nothing, and a repeated press cannot queue twice.
    edges.Observe(gameplay::Action::kFirstNote, false);
    CHECK_FALSE(edges.Pending(gameplay::Action::kFirstNote));
    edges.Observe(gameplay::Action::kFirstNote, true);
    edges.Observe(gameplay::Action::kFirstNote, true);
    CHECK_EQ(edges.Drain(gameplay::Action::kFirstNote, out, 2), std::size_t{1});
    CHECK_FALSE(edges.Pending(gameplay::Action::kFirstNote));
    CHECK_TRUE(edges.Held(gameplay::Action::kFirstNote));
    // The release is the last state, so the consumer reconciles it.
    edges.Observe(gameplay::Action::kFirstNote, false);
    CHECK_FALSE(edges.Held(gameplay::Action::kFirstNote));
  }

  BeginCase("a boundary drops the queue, and a short drain keeps the rest pending");
  {
    ActionEdges edges;
    edges.Observe(gameplay::Action::kMoveLaneRight, true);
    edges.Observe(gameplay::Action::kMoveLaneRight, false);
    bool out[2] = {false, false};
    // One slot: the press comes out, the release stays.
    CHECK_EQ(edges.Drain(gameplay::Action::kMoveLaneRight, out, 1), std::size_t{1});
    CHECK_TRUE(out[0]);
    CHECK_TRUE(edges.Pending(gameplay::Action::kMoveLaneRight));
    CHECK_EQ(edges.Drain(gameplay::Action::kMoveLaneRight, out, 2), std::size_t{1});
    CHECK_FALSE(out[0]);
    // A boundary drops what is queued and reads as released: the next poll cannot deliver a
    // press or a release from the old context.
    edges.Observe(gameplay::Action::kPowerup, true);
    edges.Clear();
    CHECK_FALSE(edges.Pending(gameplay::Action::kPowerup));
    CHECK_FALSE(edges.Held(gameplay::Action::kPowerup));
    edges.Observe(gameplay::Action::kPowerup, true);
    CHECK_EQ(edges.Drain(gameplay::Action::kPowerup, out, 2), std::size_t{1});
    CHECK_TRUE(out[0]);
  }

  return Finish();
}
