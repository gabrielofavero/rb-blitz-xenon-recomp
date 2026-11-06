"""Export the individual PS3 button glyphs from the editable SVG master.

Requires Pillow and Playwright with Chromium installed. Run from any directory:
    python scripts/controllers/export_playstation_3_buttons.py
"""

from __future__ import annotations

import base64
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image
from playwright.sync_api import sync_playwright


ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / "assets/controller/masters/playstation-3/ps3-layout-recriated.svg"
OUTPUT = ROOT / "assets/controller/playstation-3/buttons"
SVG = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG)

# The SVG IDs describe the printed symbols. The filenames follow the game's
# Xbox-position button slots, so Cross is A, Circle is B, and so on.
GLYPHS = {
    "cross": "a_button", "circle": "b_button",
    "square": "x_button", "triangle": "y_button",
    "l2": "left_trigger", "l1": "left_bumper",
    "r2": "right_trigger", "r1": "right_bumper",
    "start": "start_button", "select": "back_button",
    "dpad": "dpad", "dpad-up": "dpad_up",
    "dpad-right": "dpad_right", "dpad-down": "dpad_down",
    "dpad-left": "dpad_left",
    "ls": "left_stick", "rs": "right_stick",
    "ls-up": "left_stick_up", "ls-right": "left_stick_right",
    "ls-down": "left_stick_down", "ls-left": "left_stick_left",
    "rs-up": "right_stick_up", "rs-right": "right_stick_right",
    "rs-down": "right_stick_down", "rs-left": "right_stick_left",
}


def isolated_svg(defs: ET.Element, glyph: ET.Element) -> bytes:
    root = ET.Element(f"{{{SVG}}}svg", {
        "width": "160", "height": "160", "viewBox": "-80 -80 160 160",
    })
    root.append(defs)
    root.append(glyph)
    # The sheet's translate() places the glyph in its grid cell. Each export
    # uses the glyph's own origin instead.
    glyph.attrib.pop("transform", None)
    return ET.tostring(root, encoding="utf-8")


def main() -> None:
    tree = ET.parse(MASTER)
    root = tree.getroot()
    defs = root.find(f"{{{SVG}}}defs")
    if defs is None:
        raise ValueError(f"{MASTER}: missing SVG definitions")
    glyphs = {child.get("id"): child for child in root if child.tag == f"{{{SVG}}}g"}
    if set(glyphs) != set(GLYPHS):
        raise ValueError(f"SVG glyphs differ from exporter: {set(glyphs) ^ set(GLYPHS)}")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(
                viewport={"width": 160, "height": 160}, device_scale_factor=2,
            )
            for svg_id, filename in GLYPHS.items():
                svg = isolated_svg(defs, glyphs[svg_id])
                page.goto("data:image/svg+xml;base64," + base64.b64encode(svg).decode())
                screenshot = page.screenshot(omit_background=True)
                with Image.open(BytesIO(screenshot)) as image:
                    rgba = image.convert("RGBA").resize((160, 160), Image.Resampling.LANCZOS)
                    bounds = rgba.getchannel("A").getbbox()
                    if bounds is None:
                        raise ValueError(f"{svg_id}: empty rendered image")
                    rgba.crop(bounds).save(OUTPUT / f"{filename}.png", optimize=True)
        finally:
            browser.close()
    print(f"Exported {len(GLYPHS)} glyphs to {OUTPUT}")


if __name__ == "__main__":
    main()
