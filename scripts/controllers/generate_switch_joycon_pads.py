"""Render Switch 1 and Switch 2 Joy-Con pads from editable SVG callouts.

Run: python scripts/controllers/generate_switch_joycon_pads.py
Edit masters/nintendo-switch[-2]-joycon/pad_N-arrows.svg to adjust routes.
"""

from math import dist

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


# Fit each source at its original aspect ratio while keeping the Xbox text
# rails outside the two Joy-Con silhouettes.
LAYOUTS = {
    "nintendo-switch-joycon": (100, 200, 924, 854),
    "nintendo-switch-2-joycon": (100, 195, 924, 855),
}
ROUTE_COUNTS = (7, 6, 5, 6)
LEFT_SHAFT_JOIN = frozenset(("dpad", "left_stick"))
RIGHT_SHAFT_JOIN = frozenset(("right_stick", "a_button"))
# Route ids use Xbox positions: X reaches printed Y, A reaches B, B reaches A.
FACE_CENTERS = {
    "nintendo-switch-joycon": {
        "x_button": (737, 400), "a_button": (778, 439),
        "b_button": (820, 400),
    },
    "nintendo-switch-2-joycon": {
        "x_button": (735, 391), "a_button": (778, 431),
        "b_button": (818, 391),
    },
}
SHOULDER_TIP_REGIONS = {
    "left_shoulder_pair": (192, 275, 208, 292),
    "right_shoulder_pair": (816, 275, 832, 292),
    "left_trigger": (192, 275, 208, 292),
    "right_trigger": (816, 275, 832, 292),
    "left_bumper": (160, 315, 175, 335),
    "right_bumper": (849, 315, 864, 335),
}


def generate_pad(layout_id: str, pad_index: int) -> None:
    if layout_id not in LAYOUTS:
        raise ValueError(f"Unknown Joy-Con layout: {layout_id}")
    if pad_index not in range(4):
        raise ValueError(f"Unknown Joy-Con pad index: {pad_index}")

    routes, rails, width = read_layout(
        pad_index, layout_id, expected_routes=ROUTE_COUNTS[pad_index])
    allowed_joins = (frozenset((LEFT_SHAFT_JOIN,)) if pad_index in (0, 3)
                     else frozenset())
    if pad_index == 0:
        allowed_joins |= frozenset((RIGHT_SHAFT_JOIN,))
    validate_routes(routes, allowed_joins)
    if pad_index in (0, 3):
        left_rail = next((rail for rail in rails if rail[0] == 16
                          and rail[2] in (759, 760)), None)
        if left_rail is None:
            raise ValueError(f"{layout_id} pad {pad_index}: no left text rail")
        shared_start = (left_rail[1], left_rail[2])
        if (routes["dpad"][0] != shared_start or
                routes["left_stick"][0] != shared_start or
                any(points[1][0] != shared_start[0]
                    for points in (routes["dpad"], routes["left_stick"]))):
            raise ValueError(f"{layout_id} pad {pad_index}: left callouts "
                             "must share a shaft outside the Joy-Con")
    if pad_index == 0:
        right_rail = next((rail for rail in rails if rail[2] == 758
                           and rail[1] == 1009), None)
        if right_rail is None:
            raise ValueError(f"{layout_id} pad 0: no right text rail")
        shared_start = (right_rail[0], right_rail[2])
        if (routes["right_stick"][0] != shared_start or
                routes["a_button"][0] != shared_start or
                any(points[1][0] != shared_start[0]
                    for points in (routes["right_stick"], routes["a_button"]))):
            raise ValueError(f"{layout_id} pad 0: right callouts "
                             "must share a shaft beside the Joy-Con")
    for name, (cx, cy) in FACE_CENTERS[layout_id].items():
        if name not in routes:
            continue
        points = routes[name]
        x, y = points[-1]
        center_distance = dist((x, y), (cx, cy))
        if center_distance > 22:
            raise ValueError(f"{layout_id} pad {pad_index}: {name} misses "
                             "its Switch face button")
        previous_x, previous_y = points[-2]
        toward_center = ((x - previous_x) * (cx - x) +
                         (y - previous_y) * (cy - y))
        if toward_center < 0.25 * dist(points[-2], points[-1]) * center_distance:
            raise ValueError(f"{layout_id} pad {pad_index}: {name} arrowhead "
                             "points away from its Switch face button")
    if pad_index == 0:
        y_route = routes["x_button"]
        if (y_route[-1][0] <= FACE_CENTERS[layout_id]["x_button"][0]
                or y_route[-2][0] <= y_route[-1][0]
                or y_route[-2][1] >= y_route[-1][1]):
            raise ValueError(f"{layout_id} pad 0: printed Y must be "
                             "approached diagonally from the upper right")
    if pad_index == 1:
        y_route = routes["x_button"]
        if (y_route[-1][0] != FACE_CENTERS[layout_id]["x_button"][0]
                or y_route[-2][0] != y_route[-1][0]
                or y_route[-2][1] <= y_route[-1][1]):
            raise ValueError(f"{layout_id} pad {pad_index}: printed Y "
                             "must be approached vertically from below")
    if pad_index == 2:
        y_route = routes["x_button"]
        if (y_route[-2][1] != y_route[-1][1]
                or y_route[-2][0] >= y_route[-1][0]):
            raise ValueError(f"{layout_id} pad 2: printed Y must be "
                             "approached horizontally from the left")
    if pad_index == 3:
        y_route = routes["x_button"]
        if (y_route[-1][0] != FACE_CENTERS[layout_id]["x_button"][0]
                or y_route[-2][0] != y_route[-1][0]
                or y_route[-2][1] >= y_route[-1][1]):
            raise ValueError(f"{layout_id} pad 3: printed Y must be "
                             "approached vertically from above")
    for name, (x_min, y_min, x_max, y_max) in SHOULDER_TIP_REGIONS.items():
        if name not in routes:
            continue
        x, y = routes[name][-1]
        if not (x_min <= x <= x_max and y_min <= y <= y_max):
            raise ValueError(f"{layout_id} pad {pad_index}: {name} misses "
                             "the visible shoulder")
        if (name not in ("left_bumper", "right_bumper") and
                dist(*routes[name][-2:]) < 40):
            raise ValueError(f"{layout_id} pad {pad_index}: {name} has "
                             "too little shaft for a complete arrowhead")
        if name not in ("left_bumper", "right_bumper"):
            previous_y = routes[name][-2][1]
            if previous_y >= y:
                raise ValueError(f"{layout_id} pad {pad_index}: {name} "
                                 "must point downward at the shoulder rim")
    if pad_index in (0, 3):
        rail_y = routes["x_button"][0][1]
        inner_ends = [x_start for x_start, _, y in rails if y == rail_y]
        if inner_ends != [routes["x_button"][0][0]]:
            raise ValueError(f"{layout_id} pad {pad_index}: Y rail has "
                             "a stub before the branch")

    source_path = ROOT / f"assets/controller/masters/{layout_id}/pad.png"
    with Image.open(source_path) as source:
        body = load_transparent_body(source)
    left, top, right, bottom = LAYOUTS[layout_id]
    art = Image.new("RGBA", (SIZE, SIZE))
    fitted = body.resize((right - left, bottom - top), Image.Resampling.LANCZOS)
    art.alpha_composite(fitted, (left, top))
    for name in SHOULDER_TIP_REGIONS:
        if name not in routes:
            continue
        tip_x, tip_y = routes[name][-1]
        red, green, blue, alpha = fitted.getpixel((tip_x - left, tip_y - top))
        if alpha < 200 or not (55 <= min(red, green, blue)
                               and max(red, green, blue) <= 110
                               and max(red, green, blue) - min(red, green, blue) <= 12):
            raise ValueError(f"{layout_id} pad {pad_index}: {name} arrowhead "
                             "does not meet the gray shoulder rim")
    callouts = render_callouts(
        routes, rails, width,
        expected_groups=len(rails),
    )
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
