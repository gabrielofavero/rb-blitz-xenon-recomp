"""Render pressed and released calibration PNGs for every populated layout.

    python scripts/controllers/render_all_controller_calibration.py
    python scripts/controllers/render_all_controller_calibration.py --exclude xbox-series

Each layout's scheme supplies the source PNGs. The renderer embeds those PNGs
intact and writes generated images to each layout's calibration/ folder.
Requires Pillow and Playwright Chromium; see render_button_template.py.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import tomllib

from playwright.sync_api import sync_playwright

from render_button_template import ROOT, compose_svg, render_png_with_page


def layout_inputs(layout: Path) -> list[tuple[str, Path]]:
    scheme = tomllib.loads((layout / "scheme.toml").read_text(encoding="utf-8"))
    inputs = scheme.get("input", [])
    names = [entry["id"] for entry in inputs]
    if len(names) != len(set(names)):
        raise ValueError(f"{layout}: duplicate input IDs")
    artwork_paths = [layout / entry["file"] for entry in inputs]
    if len(artwork_paths) != len(set(artwork_paths)):
        raise ValueError(f"{layout}: duplicate source PNGs")
    for name, artwork in zip(names, artwork_paths):
        if Path(name).name != name or name in (".", ".."):
            raise ValueError(f"{layout}: invalid input ID {name!r}")
        if (artwork.suffix.lower() != ".png" or not artwork.is_file()
                or not artwork.resolve().is_relative_to(layout.resolve())):
            raise ValueError(f"{layout}: expected a PNG within the layout: {artwork}")
    return list(zip(names, artwork_paths))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exclude", action="append", default=[],
                        help="Skip a layout already rendered (repeatable)")
    args = parser.parse_args()

    root = ROOT / "assets/controller"
    available = {path.parent.name: path.parent for path in root.glob("*/scheme.toml")}
    unknown = set(args.exclude) - set(available)
    if unknown:
        parser.error(f"Unknown layout(s): {', '.join(sorted(unknown))}")
    jobs = [(available[name], layout_inputs(available[name]))
            for name in sorted(available) if name not in args.exclude]
    jobs = [(layout, inputs) for layout, inputs in jobs if inputs]

    total = 0
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            page = browser.new_page(viewport={"width": 256, "height": 256}, device_scale_factor=4)
            for layout, inputs in jobs:
                for name, artwork in inputs:
                    for state in ("pressed", "released"):
                        master = root / f"masters/button-{state}-template.svg"
                        output = layout / "calibration" / f"{name}-{state}.png"
                        render_png_with_page(compose_svg(master, artwork, state), output, 256, page)
                count = len(inputs) * 2
                total += count
                print(f"{layout.name}: {count} PNGs", flush=True)
        finally:
            browser.close()
    print(f"Rendered {total} PNGs across {len(jobs)} layouts.")


if __name__ == "__main__":
    main()
