"""Render Meta Quest 2 and 3 pad diagrams from editable SVG arrow routes.

Run: python scripts/controllers/generate_meta_quest_pads.py
Edit assets/controller/masters/meta-quest-{2,3}/pad_N-arrows.svg to move callouts.
"""

from math import dist

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


BODY_BOXES = {
    "meta-quest-2": (100, 285, 924, 745),
    "meta-quest-3": (100, 250, 924, 735),
}
ROUTE_COUNTS = (7, 6, 5, 6)
LEFT_SHAFT_JOIN = frozenset(("dpad", "left_stick"))
CONTROL_CENTERS = {
    "meta-quest-2": {
        "left_stick": (319, 468), "right_stick": (704, 468),
        "x_button": (327, 504), "a_button": (695, 504),
        "b_button": (660, 500),
    },
    "meta-quest-3": {
        "left_stick": (308, 330), "right_stick": (713, 330),
        "x_button": (296, 383), "a_button": (727, 383),
        "b_button": (670, 357),
    },
}


def generate_pad(layout_id: str, pad_index: int) -> None:
    if layout_id not in BODY_BOXES or pad_index not in range(4):
        raise ValueError(f"Unknown Meta Quest layout/pad: {layout_id}/{pad_index}")
    routes, rails, width = read_layout(
        pad_index, layout_id, expected_routes=ROUTE_COUNTS[pad_index])
    allowed_joins = (frozenset((LEFT_SHAFT_JOIN,)) if pad_index in (0, 1, 3)
                     else frozenset())
    validate_routes(routes, allowed_joins)

    for name, center in CONTROL_CENTERS[layout_id].items():
        if name in routes and dist(routes[name][-1], center) > 25:
            raise ValueError(f"{layout_id} pad {pad_index}: {name} misses its control")
    for name, points in routes.items():
        if not all(0 <= x < SIZE and 0 <= y < SIZE for x, y in points):
            raise ValueError(f"{layout_id} pad {pad_index}: {name} leaves the pad canvas")

    source_path = ROOT / f"assets/controller/masters/{layout_id}/pad.png"
    with Image.open(source_path) as source:
        body = load_transparent_body(source)
        body = body.crop(body.getchannel("A").getbbox())
    left, top, right, bottom = BODY_BOXES[layout_id]
    art = Image.new("RGBA", (SIZE, SIZE))
    art.alpha_composite(body.resize(
        (right - left, bottom - top), Image.Resampling.LANCZOS), (left, top))

    callouts = render_callouts(routes, rails, width)
    for name, points in routes.items():
        validate_complete(name, callouts, points)
    alpha = callouts.getchannel("A")
    for x_start, x_end, y in rails:
        if any(not alpha.getpixel((x, y)) for x in range(x_start, x_end + 1)):
            raise ValueError(f"{layout_id} pad {pad_index}: text rail y={y} has a gap")
    art.alpha_composite(callouts)

    dest = ROOT / f"assets/controller/{layout_id}/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    for layout_id in BODY_BOXES:
        for pad_index in range(4):
            generate_pad(layout_id, pad_index)


if __name__ == "__main__":
    main()
