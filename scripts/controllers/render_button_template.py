"""Render an Xbox Series button in the pressed/released bezel masters.

    python scripts/controllers/render_button_template.py --state pressed
    python scripts/controllers/render_button_template.py --state released

For Xbox Series A/B/X/Y, the script keeps each source glyph and draws a
simple oval face in the colors of the reference artwork. Other buttons use
their source image. Every shadow follows its button's outline. Outputs default to
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
    "released": Placement(128, 114, 138, 100, 12),
}
BODY_ANGLE = -15
GLYPH_ANGLE = 15
WHOLE_BUTTON_ANGLE = 15
FACE_STYLES = {
    "a_button": (("#319e3a", "#177e22", "#075617"), (248, 248, 244), (95, 75)),
    "b_button": (("#d9433a", "#b92224", "#73161a"), (248, 248, 244), (78, 75)),
    "x_button": (("#424244", "#29292b", "#171719"), (89, 139, 192), (116, 82)),
    "y_button": (("#424244", "#29292b", "#171719"), (225, 174, 59), (94, 75)),
}


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
    return png_bytes(original), png_bytes(shadow)


def prepare_face_glyph(artwork: Path, color: tuple[int, int, int],
                       width: int, height: int) -> bytes:
    """Extract the source letter and rasterize a sharp mask at output resolution."""
    with Image.open(artwork) as source:
        original = source.convert("RGBA")
    mask = Image.new("L", original.size)
    for y in range(original.height):
        for x in range(original.width):
            red, green, blue, source_alpha = original.getpixel((x, y))
            saturation = max(red, green, blue) - min(red, green, blue)
            if saturation > 8:
                mask.putpixel((x, y), round(source_alpha * min(1, (saturation - 8) / 65)))
    bounds = mask.getbbox()
    if bounds is None:
        raise ValueError(f"{artwork}: no colored face-button glyph")
    mask = mask.crop(bounds).resize((width * 4, height * 4), Image.Resampling.LANCZOS)
    mask = mask.point(lambda alpha: 255 if alpha >= 96 else 0)
    glyph = Image.new("RGBA", mask.size, (*color, 0))
    glyph.putalpha(mask)
    return png_bytes(glyph)


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


def add_series_face(root: ET.Element, artwork: Path, placement: Placement) -> None:
    colors, glyph_color, glyph_size = FACE_STYLES[artwork.stem]
    defs = root.find(f"{{{SVG_NS}}}defs")
    if defs is None:
        defs = ET.SubElement(root, f"{{{SVG_NS}}}defs")
    gradient = ET.SubElement(defs, f"{{{SVG_NS}}}radialGradient", {
        "id": "series-face-fill", "cx": "38%", "cy": "30%", "r": "80%",
    })
    for offset, color in zip(("0", ".58", "1"), colors):
        ET.SubElement(gradient, f"{{{SVG_NS}}}stop", {
            "offset": offset, "stop-color": color,
        })

    center_x, center_y = placement.center_x, placement.center_y
    radius_x, radius_y = placement.body_width / 2, placement.body_height / 2
    shadow = ET.SubElement(root, f"{{{SVG_NS}}}g", {
        "id": "scripted-shadow", "transform": f"translate(0 {placement.shadow_offset})",
    })
    ET.SubElement(shadow, f"{{{SVG_NS}}}ellipse", {
        "cx": str(center_x), "cy": str(center_y),
        "rx": str(radius_x), "ry": str(radius_y),
        "fill": "#050505", "transform": f"rotate({BODY_ANGLE} {center_x} {center_y})",
    })
    face = ET.SubElement(root, f"{{{SVG_NS}}}g", {
        "id": "button-body", "transform": f"rotate({BODY_ANGLE} {center_x} {center_y})",
    })
    ET.SubElement(face, f"{{{SVG_NS}}}ellipse", {
        "cx": str(center_x), "cy": str(center_y),
        "rx": str(radius_x), "ry": str(radius_y), "fill": "#09090a",
    })
    ET.SubElement(face, f"{{{SVG_NS}}}ellipse", {
        "cx": str(center_x), "cy": str(center_y),
        "rx": str(radius_x - 4), "ry": str(radius_y - 4),
        "fill": "url(#series-face-fill)",
    })
    ET.SubElement(face, f"{{{SVG_NS}}}ellipse", {
        "cx": str(center_x), "cy": str(center_y),
        "rx": str(radius_x - 5), "ry": str(radius_y - 5),
        "fill": "none", "stroke": "#fff", "stroke-width": "1",
        "stroke-opacity": ".12",
    })

    glyph_width = round(glyph_size[0] * placement.body_width / 150)
    glyph_height = round(glyph_size[1] * placement.body_height / 118)
    glyph = prepare_face_glyph(artwork, glyph_color, glyph_width, glyph_height)
    add_image(root, "button-symbol", glyph, center_x, center_y - 4,
              glyph_width, glyph_height, GLYPH_ANGLE)


def compose_svg(master: Path, artwork: Path, state: str) -> bytes:
    placement = PLACEMENTS[state]
    root = ET.parse(master).getroot()
    if root.find(".//*[@id='button-face']") is not None:
        raise ValueError(f"{master}: template still contains a button face")
    if artwork.parent.parent.name == "xbox-series" and artwork.stem in FACE_STYLES:
        add_series_face(root, artwork, placement)
        return ET.tostring(root, encoding="utf-8", xml_declaration=True)

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
              body_width, body_height, WHOLE_BUTTON_ANGLE)
    add_image(root, "button-body", neutral,
              placement.center_x, placement.center_y,
              body_width, body_height, WHOLE_BUTTON_ANGLE)
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
