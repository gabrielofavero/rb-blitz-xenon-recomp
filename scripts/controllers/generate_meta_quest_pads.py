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
    "meta-quest-2": (100, 277, 924, 765),
    "meta-quest-3": (100, 240, 924, 757),
}
ROUTE_COUNTS = (7, 6, 5, 6)
# The fixed Xbox rails put X on the right, but Quest X is on the left.
# Its route joins the right-side callouts on pads 0 and 1.
LEFT_SHAFT_JOIN = frozenset(("left_stick_alt", "left_stick"))
RIGHT_RAIL_JOINS = (
    frozenset(("right_stick", "x_button")),
    frozenset(("a_button", "x_button")),
)
ARROW_GROUPS = (4, 3, 5, 5)
CONTROL_CENTERS = {
    "meta-quest-2": {
        "left_shoulder_pair": (410, 575), "right_shoulder_pair": (614, 575),
        "left_trigger": (410, 575), "right_trigger": (614, 575),
        "left_grip": (280, 590), "right_grip": (744, 590),
        "left_stick": (319, 474), "left_stick_alt": (319, 474),
        "right_stick": (704, 474),
        "x_button": (327, 511), "a_button": (695, 511),
        "b_button": (660, 507),
    },
    "meta-quest-3": {
        "left_shoulder_pair": (440, 430), "right_shoulder_pair": (580, 430),
        "left_trigger": (440, 430), "right_trigger": (580, 430),
        "left_grip": (265, 525), "right_grip": (760, 525),
        "left_stick": (308, 325), "left_stick_alt": (308, 325),
        "right_stick": (713, 325),
        "x_button": (296, 382), "a_button": (727, 382),
        "b_button": (670, 354),
    },
}


def generate_pad(layout_id: str, pad_index: int) -> None:
    if layout_id not in BODY_BOXES or pad_index not in range(4):
        raise ValueError(f"Unknown Meta Quest layout/pad: {layout_id}/{pad_index}")
    routes, rails, width = read_layout(
        pad_index, layout_id, expected_routes=ROUTE_COUNTS[pad_index])
    allowed_joins = set((LEFT_SHAFT_JOIN,)) if pad_index in (0, 1, 3) else set()
    if pad_index in (0, 1):
        allowed_joins.add(RIGHT_RAIL_JOINS[0])
    if pad_index == 0:
        allowed_joins.add(RIGHT_RAIL_JOINS[1])
    validate_routes(routes, frozenset(allowed_joins))

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

    callouts = render_callouts(routes, rails, width,
                               expected_groups=ARROW_GROUPS[pad_index])
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
