"""Render Steam Deck pad diagrams from editable callout routes.

Run: python scripts/generate_steam_deck_pads.py
Edit masters/steam-deck/pad_N-arrows.svg to adjust the callouts.
"""

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


SOURCE = ROOT / "assets/controller/masters/steam-deck/pad.png"
# Crop the transparent export margin to the prior 1132:458 framing so the
# existing arrow tips still land on their controls.
SOURCE_CROP = (70, 62, 1202, 520)
BODY = (132, 320, 892, 627)
ROUTE_COUNTS = (7, 6, 5, 6)
LEFT_SHAFT_JOIN = frozenset(("dpad", "left_stick"))
PAD_0_JOINS = frozenset((
    LEFT_SHAFT_JOIN,
    frozenset(("right_stick", "a_button")),
))
PAD_3_JOINS = frozenset((LEFT_SHAFT_JOIN,))


def generate_pad(pad_index: int) -> None:
    if pad_index not in range(4):
        raise ValueError(f"Unknown Steam Deck pad index: {pad_index}")
    routes, rails, width = read_layout(
        pad_index, "steam-deck", expected_routes=ROUTE_COUNTS[pad_index])
    # Pads 0 and 3 join the left callouts; pad 0 also joins the right ones.
    allowed_joins = (PAD_0_JOINS if pad_index == 0 else
                     PAD_3_JOINS if pad_index == 3 else frozenset())
    validate_routes(routes, allowed_joins)
    with Image.open(SOURCE) as source:
        body = load_transparent_body(source).crop(SOURCE_CROP)
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
    dest = ROOT / f"assets/controller/steam-deck/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    for pad_index in range(4):
        generate_pad(pad_index)


if __name__ == "__main__":
    main()
