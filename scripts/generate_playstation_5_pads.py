"""Render PlayStation 5 controller diagrams with PS3 text rail positions.

Run: python scripts/generate_playstation_5_pads.py
Edit masters/playstation-5/pad_N-arrows.svg to adjust the callouts.
"""

from PIL import Image
import cv2
import numpy as np

from generate_controller_pad_0 import (
    ROOT, SIZE, read_layout, render_callouts,
    validate_complete, validate_routes,
)


SOURCE = ROOT / "assets/controller/masters/playstation-5/pad.png"
# Preserve the master's 1322:828 aspect ratio. Its visible silhouette lands at
# approximately x=144..879, y=293..723; the PS3 text rails stay in place.
BODY = (95, 248, 928, 770)


def remove_white_background(source: Image.Image) -> Image.Image:
    """Keep the controller silhouette and thumbsticks, excluding its broad shadow."""
    rgb = np.asarray(source.convert("RGB"), dtype=np.uint8)
    candidate = (rgb.min(axis=2) < 145).astype(np.uint8)
    contours, _ = cv2.findContours(candidate, cv2.RETR_EXTERNAL,
                                   cv2.CHAIN_APPROX_SIMPLE)
    silhouette = np.zeros(candidate.shape, dtype=np.uint8)
    for contour in contours:
        if cv2.contourArea(contour) > 1000:
            cv2.drawContours(silhouette, [contour], -1, 255, cv2.FILLED)
    if not silhouette.any():
        raise ValueError("PS5 pad silhouette could not be found")
    silhouette = cv2.erode(silhouette, np.ones((3, 3), dtype=np.uint8),
                           iterations=2)
    inner = cv2.erode(silhouette, np.ones((3, 3), dtype=np.uint8),
                       iterations=2)
    edge = (silhouette != 0) & (inner == 0)
    rgb = rgb.copy()
    rgb[edge] = np.minimum(rgb[edge], 100)
    rgba = np.dstack((rgb, silhouette))
    return Image.fromarray(rgba, "RGBA")


def generate_pad(pad_index: int) -> None:
    if pad_index not in range(4):
        raise ValueError(f"Unknown PlayStation 5 pad index: {pad_index}")
    routes, rails, width = read_layout(pad_index, "playstation-5")
    validate_routes(routes)
    with Image.open(SOURCE) as source:
        body = remove_white_background(source)
    art = Image.new("RGBA", (SIZE, SIZE))
    fitted = body.resize((BODY[2] - BODY[0], BODY[3] - BODY[1]),
                         Image.Resampling.LANCZOS)
    art.alpha_composite(fitted, BODY[:2])
    callouts = render_callouts(routes, rails, width)
    for name, points in routes.items():
        validate_complete(name, callouts, points)
    alpha = callouts.getchannel("A")
    for x_start, x_end, y in rails:
        if any(not alpha.getpixel((x, y)) for x in range(x_start, x_end + 1)):
            raise ValueError(f"Horizontal arrow rail has a gap at y={y}")
    art.alpha_composite(callouts)
    dest = ROOT / f"assets/controller/playstation-5/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    for pad_index in range(4):
        generate_pad(pad_index)


if __name__ == "__main__":
    main()
