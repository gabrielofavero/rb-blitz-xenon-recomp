"""Render five unusually shaped keyboard and mouse calibration samples.

    python scripts/controllers/render_keyboard_calibration_samples.py

Outputs pressed and released PNGs to assets/controller/keyboard-mouse/calibration/.
The source PNGs and scheme are unchanged. Requires Pillow and Playwright Chromium;
see render_button_template.py.
"""

from __future__ import annotations

from pathlib import Path
import tomllib

from PIL import Image, ImageDraw
from playwright.sync_api import sync_playwright

from render_button_template import ROOT, compose_svg, render_png_with_page


SAMPLES = ("enter_alt", "plus_tall", "space", "mouse_wheel_up", "mouse_left")


def write_contact_sheet(destination: Path) -> None:
    sheet = Image.new("RGB", (5 * 270, 2 * 290), (38, 38, 38))
    draw = ImageDraw.Draw(sheet)
    for column, sample in enumerate(SAMPLES):
        for row, state in enumerate(("pressed", "released")):
            with Image.open(destination / f"{sample}-{state}.png") as image:
                button = image.convert("RGBA")
            x, y = column * 270 + 7, row * 290 + 7
            sheet.paste(button, (x, y), button.getchannel("A"))
            draw.text((x + 8, y + 260), f"{sample} {state}", fill="white")
    output = ROOT / "out/controller-button-prototype/keyboard-samples-contact.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)
    print(output.relative_to(ROOT))


def main() -> None:
    layout = ROOT / "assets/controller/keyboard-mouse"
    scheme = tomllib.loads((layout / "scheme.toml").read_text(encoding="utf-8"))
    sources = {entry["id"]: layout / entry["file"] for entry in scheme["input"]}
    destination = layout / "calibration"

    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(viewport={"width": 256, "height": 256}, device_scale_factor=4)
            for sample in SAMPLES:
                artwork = sources[sample]
                if artwork.parent != layout or not artwork.is_file():
                    raise ValueError(f"Expected a keyboard or mouse PNG: {artwork}")
                for state in ("pressed", "released"):
                    master = ROOT / f"assets/controller/masters/button-{state}-template.svg"
                    output = destination / f"{sample}-{state}.png"
                    render_png_with_page(compose_svg(master, artwork, state), output, 256, page)
                    print(output.relative_to(ROOT))
        finally:
            browser.close()
    write_contact_sheet(destination)


if __name__ == "__main__":
    main()
