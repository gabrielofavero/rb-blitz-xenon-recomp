"""Generate a local, gitignored controller-art reference from the PS3 dump.

Run from anywhere: python scripts/export_template_ps3.py
Retail PNGs stay in assets/controller/template-ps3/. Its manifest matches the
planned PS3 scheme format, but this local reference is never packaged.
"""

from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "game" / "ps3" / "ui"
DEST = ROOT / "assets" / "controller" / "template-ps3"
TOOL = ROOT / "scripts" / "hmx_milo.py"

SCHEME_TOML = """schema_version = 1
id = "ps3"
name = "PlayStation 3"
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
    pad_scene = SOURCE / "controller_config" / "gen" / "controller_config.milo_ps3"
    required = [fonts / "buttons.milo_ps3", fonts / "blitz_icons.milo_ps3", pad_scene]
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("PlayStation 3 scene missing: " + ", ".join(missing))

    export(required[0], "../image/icons_buttons_xbox_nomip.bmp", DEST / "buttons.png")
    export(required[1], "../image/blitz_icons_xbox_nomip.bmp", DEST / "icons.png")
    for index in range(4):
        export(pad_scene, f"img/ps_{index}.png", DEST / "pads" / f"pad_{index}.png")
    shutil.copyfile(DEST / "pads" / "pad_0.png", DEST / "pad.png")
    (DEST / "scheme.toml").write_text(SCHEME_TOML, encoding="utf-8", newline="\n")
    print(f"PlayStation 3 reference ready: {DEST}")


if __name__ == "__main__":
    main()
