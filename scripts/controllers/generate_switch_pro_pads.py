"""Render Switch 1 and Switch 2 Pro pad diagrams from editable SVG routes.

Run: python scripts/controllers/generate_switch_pro_pads.py
Edit masters/nintendo-switch[-2]-pro-controller/pad_N-arrows.svg for callouts.
"""

from math import dist

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


LAYOUTS = {
    "nintendo-switch-pro-controller": (100, 211, 924, 815),
    "nintendo-switch-2-pro-controller": (100, 207, 924, 816),
}
ROUTE_COUNTS = (7, 6, 5, 6)
# Route ids name Xbox positions, while the printed Switch faces are swapped.
# These tip regions cover Y (west), B (south), and A (east), respectively.
FACE_TIP_REGIONS = {
    "x_button": (620, 385, 680, 440),
    "a_button": (675, 450, 730, 500),
    "b_button": (745, 390, 795, 440),
}
FACE_CENTERS = {
    "nintendo-switch-pro-controller": {
        "x_button": (648, 416), "a_button": (703, 463),
        "b_button": (759, 416),
    },
    "nintendo-switch-2-pro-controller": {
        "x_button": (640, 411), "a_button": (697, 462),
        "b_button": (754, 411),
    },
}
# The top arrows need a full shaft before the reusable 34 px head. Side arrows
# must terminate on the visible shoulder edge, not in the transparent margin.
SHOULDER_TIP_REGIONS = {
    "left_shoulder_pair": (245, 285, 280, 315),
    "right_shoulder_pair": (744, 285, 779, 315),
    "left_trigger": (245, 285, 280, 315),
    "right_trigger": (744, 285, 779, 315),
    "left_bumper": (225, 315, 250, 345),
    "right_bumper": (774, 315, 800, 345),
}
TOP_SHOULDER_ROUTES = frozenset((
    "left_shoulder_pair", "right_shoulder_pair", "left_trigger", "right_trigger"))


def generate_pad(layout_id: str, pad_index: int) -> None:
    if layout_id not in LAYOUTS:
        raise ValueError(f"Unknown Switch Pro layout: {layout_id}")
    if pad_index not in range(4):
        raise ValueError(f"Unknown Switch Pro pad index: {pad_index}")

    routes, rails, width = read_layout(
        pad_index, layout_id, expected_routes=ROUTE_COUNTS[pad_index])
    for name, (left, top, right, bottom) in FACE_TIP_REGIONS.items():
        if name in routes:
            x, y = routes[name][-1]
            if not (left <= x <= right and top <= y <= bottom):
                raise ValueError(f"{layout_id} pad {pad_index}: {name} points "
                                 f"outside its Switch face region")
            cx, cy = FACE_CENTERS[layout_id][name]
            center_distance = dist((x, y), (cx, cy))
            if center_distance > 29:
                raise ValueError(f"{layout_id} pad {pad_index}: {name} "
                                 "arrowhead stops short of its face button")
            previous = routes[name][-2]
            toward_center = ((x - previous[0]) * (cx - x) +
                             (y - previous[1]) * (cy - y))
            if toward_center < 0.5 * dist(previous, (x, y)) * center_distance:
                raise ValueError(f"{layout_id} pad {pad_index}: {name} "
                                 "arrowhead points away from its face button")
    for name, (left, top, right, bottom) in SHOULDER_TIP_REGIONS.items():
        if name in routes:
            points = routes[name]
            x, y = points[-1]
            if not (left <= x <= right and top <= y <= bottom):
                raise ValueError(f"{layout_id} pad {pad_index}: {name} misses "
                                 "the visible shoulder")
            if name in TOP_SHOULDER_ROUTES and dist(points[-2], points[-1]) < 50:
                raise ValueError(f"{layout_id} pad {pad_index}: {name} shaft "
                                 "is too short for a complete arrowhead")
    validate_routes(routes)
    if pad_index in (0, 3):
        rail_id = "middle-right" if pad_index == 0 else "top-right"
        rail_y = routes["x_button"][0][1]
        inner_ends = [x_start for x_start, _, y in rails if y == rail_y]
        if inner_ends != [routes["x_button"][0][0]]:
            raise ValueError(f"{layout_id} pad {pad_index}: {rail_id} "
                             "has a stub before the Y arrow junction")
    source_path = ROOT / f"assets/controller/masters/{layout_id}/pad.png"
    with Image.open(source_path) as source:
        body = load_transparent_body(source)
    left, top, right, bottom = LAYOUTS[layout_id]
    art = Image.new("RGBA", (SIZE, SIZE))
    art.alpha_composite(body.resize((right - left, bottom - top),
                                    Image.Resampling.LANCZOS), (left, top))

    callouts = render_callouts(routes, rails, width)
    for name, points in routes.items():
        validate_complete(name, callouts, points)
    alpha = callouts.getchannel("A")
    for x_start, x_end, y in rails:
        if any(not alpha.getpixel((x, y)) for x in range(x_start, x_end + 1)):
            raise ValueError(f"Horizontal arrow rail has a gap at y={y}")
    art.alpha_composite(callouts)

    dest = ROOT / f"assets/controller/{layout_id}/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    for layout_id in LAYOUTS:
        for pad_index in range(4):
            generate_pad(layout_id, pad_index)


if __name__ == "__main__":
    main()
