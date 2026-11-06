"""Render an Xbox Series button in the pressed/released bezel masters.

    python scripts/controllers/render_button_template.py --state pressed
    python scripts/controllers/render_button_template.py --state released

Every button uses its source PNG intact, including A/B/X/Y. The script tilts
and scales that image, then adds a black shadow from its alpha silhouette.
Outputs default to
out/controller-button-prototype/ and do not modify the source art.

Requires Pillow and Playwright Chromium:
    python -m pip install Pillow playwright
    python -m playwright install chromium
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
from io import BytesIO
import os
from pathlib import Path
import tempfile
import time
import xml.etree.ElementTree as ET

from PIL import Image
from playwright.sync_api import Page, sync_playwright


ROOT = Path(__file__).resolve().parents[2]
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)


@dataclass(frozen=True)
class Placement:
    center_x: int
    center_y: int
    body_width: int
    body_height: int
    shadow_offset: int


PLACEMENTS = {
    "pressed": Placement(132, 130, 150, 118, 3),
    "released": Placement(128, 114, 150, 118, 12),
}
BUTTON_ANGLE = 15


def png_bytes(image: Image.Image) -> bytes:
    output = BytesIO()
    image.save(output, format="PNG")
    return output.getvalue()


def prepare_button(artwork: Path) -> tuple[bytes, bytes]:
    """Return an unmodified button and its black alpha silhouette."""
    with Image.open(artwork) as source:
        original = source.convert("RGBA")
    shadow = Image.new("RGBA", original.size, (0, 0, 0, 0))
    shadow.putalpha(original.getchannel("A"))
    return artwork.read_bytes(), png_bytes(shadow)


def add_image(parent: ET.Element, name: str, data: bytes, center_x: int,
              center_y: int, width: int, height: int, angle: int) -> None:
    uri = "data:image/png;base64," + base64.b64encode(data).decode("ascii")
    ET.SubElement(parent, f"{{{SVG_NS}}}image", {
        "id": name,
        "x": str(center_x - width / 2),
        "y": str(center_y - height / 2),
        "width": str(width),
        "height": str(height),
        "transform": f"rotate({angle} {center_x} {center_y})",
        "preserveAspectRatio": "none",
        "href": uri,
    })


def compose_svg(master: Path, artwork: Path, state: str) -> bytes:
    placement = PLACEMENTS[state]
    root = ET.parse(master).getroot()
    if root.find(".//*[@id='button-face']") is not None:
        raise ValueError(f"{master}: template still contains a button face")
    neutral, shadow = prepare_button(artwork)
    with Image.open(artwork) as source:
        scale = min(placement.body_width / source.width,
                    placement.body_height / source.height)
        body_width = round(source.width * scale)
        body_height = round(source.height * scale)
    shadow_group = ET.SubElement(root, f"{{{SVG_NS}}}g", {
        "id": "scripted-shadow",
        "transform": f"translate(0 {placement.shadow_offset})",
    })
    add_image(shadow_group, "shadow-shape", shadow,
              placement.center_x, placement.center_y,
              body_width, body_height, BUTTON_ANGLE)
    add_image(root, "button-body", neutral,
              placement.center_x, placement.center_y,
              body_width, body_height, BUTTON_ANGLE)
    accent = root.find(f"{{{SVG_NS}}}g[@id='white-impact-accent']")
    if accent is not None:
        root.remove(accent)
        root.append(accent)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def render_png_with_page(svg: bytes, output: Path, size: int, page: Page) -> None:
    source_url = "data:image/svg+xml;base64," + base64.b64encode(svg).decode("ascii")
    page.goto(source_url)
    screenshot = page.screenshot(omit_background=True)
    with Image.open(BytesIO(screenshot)) as image:
        result = image.convert("RGBA").resize((size, size), Image.Resampling.BOX)

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".png", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        result.save(temporary_path, format="PNG", optimize=True)
        for attempt in range(5):
            try:
                os.replace(temporary_path, output)
                break
            except OSError:
                if attempt == 4:
                    raise
                time.sleep(.2)
    finally:
        temporary_path.unlink(missing_ok=True)


def render_png(svg: bytes, output: Path, size: int) -> None:
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(viewport={"width": 256, "height": 256}, device_scale_factor=4)
            render_png_with_page(svg, output, size, page)
        finally:
            browser.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", choices=PLACEMENTS, default="pressed")
    parser.add_argument("--button", type=Path,
                        default=ROOT / "assets/controller/xbox-series/buttons/a_button.png")
    parser.add_argument("--output", type=Path, help="Destination PNG")
    parser.add_argument("--size", type=int, default=256, help="Output width and height")
    args = parser.parse_args()
    if args.size <= 0:
        parser.error("--size must be positive")

    master = ROOT / f"assets/controller/masters/button-{args.state}-template.svg"
    output = args.output or ROOT / f"out/controller-button-prototype/{args.button.stem}-{args.state}.png"
    render_png(compose_svg(master, args.button, args.state), output, args.size)
    print(output)


if __name__ == "__main__":
    main()
