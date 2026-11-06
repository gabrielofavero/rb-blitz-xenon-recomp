"""Render the three keyboard media placeholders and their calibration images.

    python scripts/controllers/render_keyboard_media.py

Requires Pillow and Playwright Chromium; see render_button_template.py.
"""

from __future__ import annotations

import base64
from io import BytesIO
from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image
from playwright.sync_api import sync_playwright

from render_button_template import ROOT, compose_svg, render_png_with_page


SVG_NS = "http://www.w3.org/2000/svg"
MEDIA = ("media_next", "media_previous", "media_play_pause")


def media_svg(master: Path, selected: str) -> bytes:
    root = ET.parse(master).getroot()
    for group in list(root):
        name = group.get("id", "")
        if name.startswith("media-") and name != selected.replace("_", "-"):
            root.remove(group)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def main() -> None:
    layout = ROOT / "assets/controller/keyboard-mouse"
    master = ROOT / "assets/controller/masters/keyboard-media.svg"
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(viewport={"width": 256, "height": 256}, device_scale_factor=4)
            for name in MEDIA:
                svg = media_svg(master, name)
                page.goto("data:image/svg+xml;base64," + base64.b64encode(svg).decode("ascii"))
                with Image.open(BytesIO(page.screenshot(omit_background=True))) as screenshot:
                    icon = screenshot.convert("RGBA").crop((0, 0, 76 * 4, 72 * 4))
                    icon.resize((76, 72), Image.Resampling.BOX).save(layout / f"{name}.png")
                for state in ("pressed", "released"):
                    bezel = ROOT / f"assets/controller/masters/button-{state}-template.svg"
                    output = layout / "calibration" / f"{name}-{state}.png"
                    render_png_with_page(compose_svg(bezel, layout / f"{name}.png", state),
                                         output, 256, page)
                    print(output.relative_to(ROOT))
        finally:
            browser.close()


if __name__ == "__main__":
    main()
