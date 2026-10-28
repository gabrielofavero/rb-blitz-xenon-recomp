"""Export private PS3 controller references; never commit or package this output.

Run from anywhere: python scripts/export_template_ps3.py
The Xbox 360 base game already contains the four PS pad diagrams.
"""

from export_controller_reference import ROOT, buttons, manifest, texture


PS3_SOURCE = ROOT / "assets/game/ps3/ui"
XBOX_SOURCE = ROOT / "assets/game/360/ui"
DEST = ROOT / "assets/controller/template-ps3/ps3"
CONTROLLER_GLYPHS = {
    "confirm_a", "back_b", "x", "y", "left_trigger", "left_bumper",
    "right_trigger", "right_bumper", "dpad", "dpad_up", "dpad_right",
    "dpad_down", "dpad_left", "left_stick", "right_stick",
}


def main() -> None:
    button_scene = PS3_SOURCE / "resource/fonts/gen/buttons.milo_ps3"
    pad_scene = XBOX_SOURCE / "controller_config/gen/controller_config.milo_xbox"
    missing = [str(path) for path in (button_scene, pad_scene) if not path.is_file()]
    if missing:
        raise SystemExit("Local game scene missing: " + ", ".join(missing))

    sheet = DEST.parent / "buttons-sheet.png"
    texture(button_scene, "../image/icons_buttons_xbox_nomip.bmp", sheet)
    entries = buttons(sheet, DEST, CONTROLLER_GLYPHS)
    pads = []
    for index in range(4):
        slot = f"ps_{index}"
        texture(pad_scene, f"img/{slot}.png", DEST / "pads" / f"{slot}.png")
        pads.append(f'{slot} = "pads/{slot}.png"')
    manifest(DEST, "ps3", "PlayStation 3", entries, pads)
    print(f"Private PS3 reference ready: {DEST}")


if __name__ == "__main__":
    main()
