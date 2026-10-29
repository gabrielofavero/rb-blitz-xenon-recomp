# Steam Deck asset audit

Compared with the 25 controller button IDs in [slots.toml](../../slots.toml) and one generic arrow-free `pad` diagram. Direct means the art already depicts the control; Adapt requires the stated edit. The source has separate controls and a logo, but no full arrow-free Deck outline.

The pad row assesses source art only; fitting the eight runtime pad textures and packaging PNGs are separate steps.

Here `next_arrow` means Start and `previous_arrow` means Select. Each `_adapt.svg` is an unchanged source copy; any described art edit is still needed. Original files that are not direct slot art are in [`unused/`](unused/).

**11 direct · 14 adaptable · 1 missing**

| ID | Result | File, provenance, or remaining edit |
| --- | --- | --- |
| `pad` | Missing | — |
| `a_button` | Direct | [a_button.svg](a_button.svg) |
| `b_button` | Direct | [b_button.svg](b_button.svg) |
| `x_button` | Direct | [x_button.svg](x_button.svg) |
| `y_button` | Direct | [y_button.svg](y_button.svg) |
| `left_trigger` | Adapt | [left_trigger_adapt.svg](left_trigger_adapt.svg); copy of [unused/l2_button.svg](unused/l2_button.svg); map the Deck shoulder label to this game slot |
| `left_bumper` | Adapt | [left_bumper_adapt.svg](left_bumper_adapt.svg); copy of [unused/l1_button.svg](unused/l1_button.svg); map the Deck shoulder label to this game slot |
| `right_trigger` | Adapt | [right_trigger_adapt.svg](right_trigger_adapt.svg); copy of [unused/r2_button.svg](unused/r2_button.svg); map the Deck shoulder label to this game slot |
| `right_bumper` | Adapt | [right_bumper_adapt.svg](right_bumper_adapt.svg); copy of [unused/r1_button.svg](unused/r1_button.svg); map the Deck shoulder label to this game slot |
| `next_arrow` | Adapt | [next_arrow_adapt.svg](next_arrow_adapt.svg); copy of [unused/menu_button.svg](unused/menu_button.svg); use Menu as Start |
| `previous_arrow` | Adapt | [previous_arrow_adapt.svg](previous_arrow_adapt.svg); copy of [unused/view_button.svg](unused/view_button.svg); use View as Select |
| `dpad` | Direct | [dpad.svg](dpad.svg) |
| `dpad_up` | Direct | [dpad_up.svg](dpad_up.svg) |
| `dpad_right` | Direct | [dpad_right.svg](dpad_right.svg) |
| `dpad_down` | Direct | [dpad_down.svg](dpad_down.svg) |
| `dpad_left` | Direct | [dpad_left.svg](dpad_left.svg) |
| `left_stick` | Direct | [left_stick.svg](left_stick.svg) |
| `right_stick` | Direct | [right_stick.svg](right_stick.svg) |
| `left_stick_up` | Adapt | [left_stick_up_adapt.svg](left_stick_up_adapt.svg); copy of [left_stick.svg](left_stick.svg); offset the stick cap up within its bezel |
| `left_stick_right` | Adapt | [left_stick_right_adapt.svg](left_stick_right_adapt.svg); copy of [left_stick.svg](left_stick.svg); offset the stick cap right within its bezel |
| `left_stick_down` | Adapt | [left_stick_down_adapt.svg](left_stick_down_adapt.svg); copy of [left_stick.svg](left_stick.svg); offset the stick cap down within its bezel |
| `left_stick_left` | Adapt | [left_stick_left_adapt.svg](left_stick_left_adapt.svg); copy of [left_stick.svg](left_stick.svg); offset the stick cap left within its bezel |
| `right_stick_up` | Adapt | [right_stick_up_adapt.svg](right_stick_up_adapt.svg); copy of [right_stick.svg](right_stick.svg); offset the stick cap up within its bezel |
| `right_stick_right` | Adapt | [right_stick_right_adapt.svg](right_stick_right_adapt.svg); copy of [right_stick.svg](right_stick.svg); offset the stick cap right within its bezel |
| `right_stick_left` | Adapt | [right_stick_left_adapt.svg](right_stick_left_adapt.svg); copy of [right_stick.svg](right_stick.svg); offset the stick cap left within its bezel |
| `right_stick_down` | Adapt | [right_stick_down_adapt.svg](right_stick_down_adapt.svg); copy of [right_stick.svg](right_stick.svg); offset the stick cap down within its bezel |
