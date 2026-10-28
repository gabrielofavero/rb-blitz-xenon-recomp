"""Generate small original PNG examples for the controller authoring guide."""

from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1] / "assets/controller/examples"


def save(package: str, manifest: str, image: Image.Image, filename: str) -> None:
    folder = ROOT / package
    target = folder / filename
    target.parent.mkdir(parents=True, exist_ok=True)
    image.save(target)
    (folder / "scheme.toml").write_text(manifest, encoding="utf-8", newline="\n")


def main() -> None:
    glyph = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glyph)
    draw.rounded_rectangle((10, 10, 117, 117), radius=28,
                           outline=(235, 243, 255, 255), width=9)
    draw.polygon(((64, 29), (36, 93), (46, 93), (64, 48), (82, 93), (92, 93)),
                 fill=(235, 243, 255, 255))
    draw.rectangle((47, 73, 81, 81), fill=(235, 243, 255, 255))
    save("example-original",
         'schema_version = 1\nid = "example-original"\nname = "Example Original"\n'
         'kind = "controller"\n\n[buttons]\nconfirm_a = "buttons/a.png"\n',
         glyph, "buttons/a.png")

    pad = Image.new("RGBA", (256, 192), (0, 0, 0, 0))
    draw = ImageDraw.Draw(pad)
    draw.rounded_rectangle((20, 38, 235, 157), radius=48,
                           outline=(235, 243, 255, 255), width=9)
    draw.ellipse((72, 84, 101, 113), fill=(235, 243, 255, 255))
    for x, y in ((177, 73), (195, 91), (159, 91), (177, 109)):
        draw.ellipse((x - 7, y - 7, x + 7, y + 7), fill=(235, 243, 255, 255))
    save("example-pad",
         'schema_version = 1\nid = "example-pad"\nname = "Example Pad"\n'
         'kind = "controller"\n\n[controller.slots]\nxbox_0 = "pads/xbox_0.png"\n',
         pad, "pads/xbox_0.png")


if __name__ == "__main__":
    main()
