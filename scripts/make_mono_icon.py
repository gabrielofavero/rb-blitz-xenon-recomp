#!/usr/bin/env python3
"""Make a black-and-white ("mono") copy of an app icon.

The alt icon is the repository's own colour art desaturated to greyscale, keeping
the alpha channel untouched. It is derived from `assets/icon/blitz.ico` and
`assets/icon/blitz.png`, which are the app's own artwork, so unlike the installer's
side image it is committed: the launcher ships the mono `.ico` as its exe icon, so a
launcher window and a game window are told apart in the taskbar and Alt-Tab list
without a second piece of art to license.

Only the colour is removed: the alpha channel, the frame sizes of an `.ico` and the
image dimensions of a `.png` all come through unchanged.

    python scripts\\make_mono_icon.py assets\\icon\\blitz.ico assets\\icon\\blitz-mono.ico
    python scripts\\make_mono_icon.py assets\\icon\\blitz.png assets\\icon\\blitz-mono.png

Requires Pillow (`pip install Pillow`).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError:  # pragma: no cover - a missing dependency is a user error
    sys.exit("Pillow is required: pip install Pillow")


def grey_with_alpha(image: "Image.Image") -> "Image.Image":
    """Return `image` with colour replaced by luminance and alpha preserved.

    Luminance comes from `ImageOps.grayscale`, and the three colour channels are
    set to it, so the result is a true greyscale image rather than one that merely
    looks grey. Alpha is taken from the source untouched, which is what keeps the
    icon's soft edges and transparent background.
    """
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    grey = ImageOps.grayscale(rgba)
    return Image.merge("RGBA", (grey, grey, grey, alpha))


def convert_ico(source: Path, target: Path) -> list[tuple[int, int]]:
    """Write a mono copy of `source`, keeping every frame size it contains."""
    icon = Image.open(source)
    sizes = sorted(icon.info.get("sizes", {icon.size}))
    frames = []
    for size in sizes:
        icon.size = size
        icon.load()
        frames.append(grey_with_alpha(icon))
    # The base image must be the largest frame: Pillow's ICO writer derives the
    # file's maximum size from it and drops any requested size bigger than the
    # base. The remaining frames ride along in `append_images`, so each size keeps
    # its own pixels instead of being resized from the 256 one.
    frames.sort(key=lambda frame: frame.size[0] * frame.size[1], reverse=True)
    base, *rest = frames
    base.save(target, format="ICO", sizes=[frame.size for frame in frames], append_images=rest)
    return sorted(frame.size for frame in frames)


def convert_image(source: Path, target: Path) -> list[tuple[int, int]]:
    """Write a mono copy of a non-ICO image (a `.png` badge, typically)."""
    image = Image.open(source)
    mono = grey_with_alpha(image)
    mono.save(target, format="PNG")
    return [mono.size]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Make a black-and-white copy of an icon.")
    parser.add_argument("source", type=Path, help="the colour icon (.ico or .png)")
    parser.add_argument("target", type=Path, help="where to write the mono icon")
    args = parser.parse_args(argv)

    if not args.source.is_file():
        parser.error(f"source not found: {args.source}")

    suffix = args.target.suffix.lower()
    if suffix == ".ico":
        sizes = convert_ico(args.source, args.target)
    elif suffix == ".png":
        sizes = convert_image(args.source, args.target)
    else:
        parser.error(f"unsupported target extension: {args.target.suffix} (use .ico or .png)")

    args.target.parent.mkdir(parents=True, exist_ok=True)
    written = ", ".join(f"{w}x{h}" for w, h in sizes)
    print(f"{args.source} -> {args.target} ({written})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
