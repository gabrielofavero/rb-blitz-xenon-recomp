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
ROUTE_COUNTS = {
    "meta-quest-2": (8, 7, 5, 6),
    "meta-quest-3": (8, 7, 5, 6),
}
# The fixed Xbox rails put X on the right, but Quest X is on the left.
# Pads 0 and 1 draw that route beneath the crossing lower callouts.
ARROW_GROUPS = (4, 3, 5, 5)
CONTROL_CENTERS = {
    "meta-quest-2": {
        "left_trigger": (410, 575), "right_trigger": (614, 575),
        "left_grip": (298, 596), "right_grip": (724, 598),
        "left_stick": (319, 474),
        "right_stick": (704, 474),
        "x_button": (327, 511), "a_button": (695, 511),
        "b_button": (660, 507),
    },
    "meta-quest-3": {
        "left_trigger": (440, 430), "right_trigger": (580, 430),
        "left_grip": (265, 525), "right_grip": (760, 525),
        "left_stick": (308, 325),
        "right_stick": (713, 325),
        "x_button": (296, 382), "a_button": (727, 382),
        "b_button": (670, 354),
    },
}
# Grip tips target the adjacent space or the upper edge of each button.
GRIP_TIP_TARGETS = {
    "meta-quest-2": {
        0: {"left_grip": (337, 605), "right_grip": (687, 605)},
        1: {"left_grip": (337, 605), "right_grip": (687, 605)},
        2: {"left_grip": (270, 578), "right_grip": (750, 578)},
        3: {"left_grip": (337, 605)},
    },
    "meta-quest-3": {
        0: {"left_grip": (365, 518), "right_grip": (659, 518)},
        1: {"left_grip": (365, 518), "right_grip": (659, 518)},
        2: {"left_grip": (235, 525), "right_grip": (790, 525)},
        3: {"left_grip": (365, 518)},
    },
}


def generate_pad(layout_id: str, pad_index: int) -> None:
    if layout_id not in BODY_BOXES or pad_index not in range(4):
        raise ValueError(f"Unknown Meta Quest layout/pad: {layout_id}/{pad_index}")
    layered_crossings = pad_index in (0, 1)
    routes, rails, width = read_layout(
        pad_index, layout_id, expected_routes=ROUTE_COUNTS[layout_id][pad_index],
        expected_rails=4 if pad_index == 1 else None)
    allowed_joins = set()
    if pad_index in (0, 1, 3):
        top_routes = tuple(name for name in
                           ("left_trigger", "left_grip", "right_trigger", "right_grip")
                           if name in routes)
        allowed_joins.update(
            frozenset((first, second)) for index, first in enumerate(top_routes)
            for second in top_routes[index + 1:])
    if layered_crossings:
        allowed_joins.add(frozenset(("right_stick", "x_button")))
        if pad_index == 0:
            allowed_joins.add(frozenset(("a_button", "x_button")))
            allowed_joins.add(frozenset(("right_stick", "a_button")))
    validate_routes(routes, frozenset(allowed_joins))

    target_centers = CONTROL_CENTERS[layout_id].copy()
    target_centers.update(GRIP_TIP_TARGETS[layout_id][pad_index])
    for name, center in target_centers.items():
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

    if layered_crossings:
        # The X line crosses two lower callouts. Draw its own rim first, then
        # place the other arrows over it so their black rims form clear bridges.
        x_rail_y = routes["x_button"][0][1]
        x_rails = tuple(rail for rail in rails if rail[2] == x_rail_y)
        upper_rails = tuple(rail for rail in rails if rail[2] != x_rail_y)
        if len(x_rails) != 1:
            raise ValueError(f"{layout_id} pad {pad_index} needs one X text rail")
        x_layer = render_callouts(
            {"x_button": routes["x_button"]}, x_rails, width,
            expected_groups=1)
        upper_routes = {name: points for name, points in routes.items()
                        if name != "x_button"}
        upper_layer = render_callouts(
            upper_routes, upper_rails, width,
            expected_groups=2 if pad_index == 1 else 3)
        validate_complete("x_button", x_layer, routes["x_button"])
        for name, points in upper_routes.items():
            validate_complete(name, upper_layer, points)
        callouts = x_layer.copy()
        callouts.alpha_composite(upper_layer)
    else:
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
