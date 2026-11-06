"""Render pressed and released Xbox Series calibration PNGs.

    python scripts/controllers/render_xbox_series_calibration.py

The scheme's button images are the inputs. Outputs are written to
assets/controller/xbox-series/calibration/ and do not replace those inputs.
Requires Pillow and Playwright Chromium; see render_button_template.py.
"""

from __future__ import annotations

import tomllib

from playwright.sync_api import sync_playwright

from render_button_template import ROOT, compose_svg, render_png_with_page


def main() -> None:
    layout = ROOT / "assets/controller/xbox-series"
    scheme = tomllib.loads((layout / "scheme.toml").read_text(encoding="utf-8"))
    artwork_paths = [layout / entry["file"] for entry in scheme["input"]]
    if len(artwork_paths) != len(set(artwork_paths)):
        raise ValueError("Xbox Series scheme refers to a button image more than once")
    for artwork in artwork_paths:
        if artwork.parent != layout / "buttons" or not artwork.is_file():
            raise ValueError(f"Expected an Xbox Series button PNG: {artwork}")

    destination = layout / "calibration"
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(viewport={"width": 256, "height": 256}, device_scale_factor=4)
            for artwork in artwork_paths:
                for state in ("pressed", "released"):
                    master = ROOT / f"assets/controller/masters/button-{state}-template.svg"
                    output = destination / f"{artwork.stem}-{state}.png"
                    render_png_with_page(compose_svg(master, artwork, state), output, 256, page)
                    print(output.relative_to(ROOT))
        finally:
            browser.close()


if __name__ == "__main__":
    main()
