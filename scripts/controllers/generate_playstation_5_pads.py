"""Render PlayStation 5 controller diagrams with PS3 text rail positions.

Run: python scripts/controllers/generate_playstation_5_pads.py
Edit masters/playstation-5/pad_N-arrows.svg to adjust the callouts.
"""

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


SOURCE = ROOT / "assets/controller/masters/playstation-5/pad.png"
# Preserve the master's 1322:829 aspect ratio. Its visible silhouette lands at
# approximately x=144..879, y=293..723; the PS3 text rails stay in place.
BODY = (95, 248, 928, 770)


def generate_pad(pad_index: int) -> None:
    if pad_index not in range(4):
        raise ValueError(f"Unknown PlayStation 5 pad index: {pad_index}")
    routes, rails, width = read_layout(pad_index, "playstation-5")
    validate_routes(routes)
    with Image.open(SOURCE) as source:
        body = load_transparent_body(source)
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
