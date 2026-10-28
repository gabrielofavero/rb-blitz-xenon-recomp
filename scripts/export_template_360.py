"""Export private Xbox 360 reference images; never commit or package this output.

Run from anywhere: python scripts/export_template_360.py
"""

from export_controller_reference import ROOT, buttons, manifest, texture


SOURCE = ROOT / "assets/game/360/ui"
DEST = ROOT / "assets/controller/template-360/xbox-360-reference"


def main() -> None:
    button_scene = SOURCE / "resource/fonts/gen/buttons.milo_xbox"
    pad_scene = SOURCE / "controller_config/gen/controller_config.milo_xbox"
    missing = [str(path) for path in (button_scene, pad_scene) if not path.is_file()]
    if missing:
        raise SystemExit("Xbox 360 scene missing: " + ", ".join(missing))

    sheet = DEST.parent / "buttons-sheet.png"
    texture(button_scene, "../image/icons_buttons_xbox_nomip.bmp", sheet)
    entries = buttons(sheet, DEST)
    pads = []
    for family in ("xbox", "ps"):
        for index in range(4):
            slot = f"{family}_{index}"
            texture(pad_scene, f"img/{slot}.png", DEST / "pads" / f"{slot}.png")
            pads.append(f'{slot} = "pads/{slot}.png"')
    manifest(DEST, "xbox-360-reference", "Xbox 360 Reference", entries, pads)
    print(f"Private Xbox 360 reference ready: {DEST}")


if __name__ == "__main__":
    main()
