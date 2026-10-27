"""Generate a local, gitignored controller-art reference from the Xbox 360 dump.

Run from anywhere: python scripts/export_template_360.py
The decoded retail PNGs stay in assets/controller/template-360/ and are never
packaged. The manifest uses a custom reference id because `retail` is reserved
for the built-in base-game choice, which has no package folder.
"""

from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "game" / "360" / "ui"
DEST = ROOT / "assets" / "controller" / "template-360"
TOOL = ROOT / "scripts" / "hmx_milo.py"

SCHEME_TOML = """schema_version = 1
id = "xbox-360-reference"
name = "Xbox 360 Reference"
kind = "controller"

[buttons]
file = "buttons.png"

[controller]
file = "pad.png"
slots = ["pads/pad_0.png", "pads/pad_1.png", "pads/pad_2.png", "pads/pad_3.png"]

[icons]
file = "icons.png"
"""


def export(scene, asset, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(TOOL), "texture", str(scene),
                    "--asset", asset, "--out", str(target)], check=True)


def main():
    fonts = SOURCE / "resource" / "fonts" / "gen"
    pad_scene = SOURCE / "controller_config" / "gen" / "controller_config.milo_xbox"
    required = [fonts / "buttons.milo_xbox", fonts / "blitz_icons.milo_xbox", pad_scene]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("Xbox 360 scene missing: " + ", ".join(missing))

    export(required[0], "../image/icons_buttons_xbox_nomip.bmp", DEST / "buttons.png")
    export(required[1], "../image/blitz_icons_xbox_nomip.bmp", DEST / "icons.png")
    for index in range(4):
        export(pad_scene, f"img/xbox_{index}.png", DEST / "pads" / f"pad_{index}.png")
    shutil.copyfile(DEST / "pads" / "pad_0.png", DEST / "pad.png")
    (DEST / "scheme.toml").write_text(SCHEME_TOML, encoding="utf-8", newline="\n")
    print(f"Xbox 360 reference ready: {DEST}")


if __name__ == "__main__":
    main()
