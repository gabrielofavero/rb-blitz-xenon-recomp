# Steam Controller asset audit

Compared with the 25 controller button IDs in [slots.toml](../../slots.toml) and one generic arrow-free `pad` diagram. Direct means the art already depicts the control; Adapt requires the stated edit. The source has controller gestures with arrows, but no separate arrow-free full controller or stick glyphs.

The pad row assesses source art only; fitting the eight runtime pad textures and packaging PNGs are separate steps.

Here `next_arrow` means Start and `previous_arrow` means Select. Each `_adapt.svg` is an unchanged source copy; any described art edit is still needed. Original files that are not direct slot art are in [`unused/`](unused/).

**10 direct · 6 adaptable · 10 missing**

| ID | Result | File, provenance, or remaining edit |
| --- | --- | --- |
| `pad` | Adapt | [pad_adapt.svg](pad_adapt.svg); copy of [unused/gesture_rotate.svg](unused/gesture_rotate.svg); remove the gesture arrows from the controller silhouette |
| `a_button` | Direct | [a_button.svg](a_button.svg) |
| `b_button` | Direct | [b_button.svg](b_button.svg) |
| `x_button` | Direct | [x_button.svg](x_button.svg) |
| `y_button` | Direct | [y_button.svg](y_button.svg) |
| `left_trigger` | Direct | [left_trigger.svg](left_trigger.svg) |
| `left_bumper` | Direct | [left_bumper.svg](left_bumper.svg) |
| `right_trigger` | Direct | [right_trigger.svg](right_trigger.svg) |
| `right_bumper` | Direct | [right_bumper.svg](right_bumper.svg) |
| `next_arrow` | Direct | [next_arrow.svg](next_arrow.svg) |
| `previous_arrow` | Direct | [previous_arrow.svg](previous_arrow.svg) |
| `dpad` | Adapt | [dpad_adapt.svg](dpad_adapt.svg); copy of [unused/trackpad_variant_12.svg](unused/trackpad_variant_12.svg); rework the trackpad cross as a D-pad |
| `dpad_up` | Adapt | [dpad_up_adapt.svg](dpad_up_adapt.svg); copy of [unused/trackpad_variant_11.svg](unused/trackpad_variant_11.svg); reuse the highlighted trackpad sector as a D-pad state |
| `dpad_right` | Adapt | [dpad_right_adapt.svg](dpad_right_adapt.svg); copy of [unused/trackpad_variant_10.svg](unused/trackpad_variant_10.svg); reuse the highlighted trackpad sector as a D-pad state |
| `dpad_down` | Adapt | [dpad_down_adapt.svg](dpad_down_adapt.svg); copy of [unused/trackpad_variant_09.svg](unused/trackpad_variant_09.svg); reuse the highlighted trackpad sector as a D-pad state |
| `dpad_left` | Adapt | [dpad_left_adapt.svg](dpad_left_adapt.svg); copy of [unused/trackpad_variant_08.svg](unused/trackpad_variant_08.svg); reuse the highlighted trackpad sector as a D-pad state |
| `left_stick` | Missing | — |
| `right_stick` | Missing | — |
| `left_stick_up` | Missing | — |
| `left_stick_right` | Missing | — |
| `left_stick_down` | Missing | — |
| `left_stick_left` | Missing | — |
| `right_stick_up` | Missing | — |
| `right_stick_right` | Missing | — |
| `right_stick_left` | Missing | — |
| `right_stick_down` | Missing | — |
