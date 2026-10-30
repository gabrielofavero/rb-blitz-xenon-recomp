"""Render PlayStation 4 pad diagrams from their editable arrow masters.

Run: python scripts/generate_playstation_4_pads.py
Edit masters/playstation-4/pad_N-arrows.svg to adjust the callouts.
"""

from PIL import Image

from generate_controller_pad_0 import (
    ROOT, SIZE, load_transparent_body, read_layout, render_callouts,
    validate_complete, validate_routes,
)


SOURCE = ROOT / "assets/controller/masters/playstation-4/pad.png"
# Keep the 1264:810 source aspect ratio and align its visible silhouette with
# the PS3 reference pads while retaining their fixed text rails.
BODY = (95, 248, 928, 782)


def generate_pad(pad_index: int) -> None:
    if pad_index not in range(4):
        raise ValueError(f"Unknown PlayStation 4 pad index: {pad_index}")
    routes, rails, width = read_layout(pad_index, "playstation-4")
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
    dest = ROOT / f"assets/controller/playstation-4/pads/pad_{pad_index}.png"
    dest.parent.mkdir(parents=True, exist_ok=True)
    art.save(dest)
    print(f"Wrote {dest.relative_to(ROOT)}")


def main() -> None:
    for pad_index in range(4):
        generate_pad(pad_index)


if __name__ == "__main__":
    main()
