"""Shared helpers for private, gitignored controller-art reference exports."""

from pathlib import Path
import subprocess
import sys
import tomllib

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
TEXTURE_TOOL = ROOT / "scripts" / "assets" / "hmx_milo.py"
SLOTS = tomllib.loads((ROOT / "assets/controller/slots.toml").read_text(encoding="utf-8"))["slot"]


def texture(scene: Path, asset: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(TEXTURE_TOOL), "texture", str(scene),
                    "--asset", asset, "--out", str(target)], check=True)


def source_bounds(sheet: Path) -> dict[str, tuple[int, ...]]:
    """Return each cell's visible source rectangle in sheet coordinates."""
    result = {}
    with Image.open(sheet) as atlas:
        alpha = atlas.convert("RGBA").getchannel("A")
        for slot in SLOTS:
            x, y = slot["column"] * 85, slot["row"] * 85
            bounds = alpha.crop((x, y, min(x + 85, alpha.width),
                                 min(y + 85, alpha.height))).getbbox()
            result[slot["id"]] = (() if bounds is None else
                                  (x + bounds[0], y + bounds[1],
                                   bounds[2] - bounds[0], bounds[3] - bounds[1]))
    return result


def check_source_bounds(sheet: Path, map_file: Path) -> None:
    """Refuse a source sheet whose visible areas differ from the reviewed map."""
    data = tomllib.loads(map_file.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or data.get("sheet_size") != [512, 512]:
        raise ValueError("PS3 source area map has an unsupported schema or sheet size")
    if len(data["slot"]) != len({entry["id"] for entry in data["slot"]}):
        raise ValueError("PS3 source area map has duplicate slot IDs")
    expected = {entry["id"]: tuple(entry["rect"]) for entry in data["slot"]}
    actual = source_bounds(sheet)
    if expected != actual:
        differences = sorted(slot_id for slot_id in expected.keys() | actual.keys()
                             if expected.get(slot_id) != actual.get(slot_id))
        raise ValueError(f"PS3 source area map differs for: {', '.join(differences)}")


def crop_slot(atlas: Image.Image, slot: dict) -> Image.Image:
    """Trim an atlas cell to its visible art, preserving a blank slot's size."""
    x, y = slot["column"] * 85, slot["row"] * 85
    cell = atlas.crop((x, y, min(x + 85, atlas.width),
                       min(y + 85, atlas.height))).convert("RGBA")
    bounds = cell.getchannel("A").getbbox()
    if bounds:
        return cell.crop(bounds)
    rect_x, rect_y, width, height = slot["rect"]
    return cell.crop((rect_x - x, rect_y - y,
                      rect_x - x + width, rect_y - y + height))


def buttons(sheet: Path, package: Path, selected: set[str] | None = None) -> list[str]:
    declarations = []
    with Image.open(sheet) as atlas:
        for slot in SLOTS:
            slot_id = slot["id"]
            if selected is not None and slot_id not in selected:
                continue
            # The font's rectangle can omit visible edge pixels, while other
            # cells contain large alignment margins. Export the visible art
            # from the complete grid cell; the runtime slot rectangle remains
            # the destination used when a package is fitted back into the atlas.
            art = crop_slot(atlas, slot)
            target = package / "buttons" / f"{slot_id}.png"
            target.parent.mkdir(parents=True, exist_ok=True)
            art.save(target)
            declarations.append(f'{slot_id} = "buttons/{slot_id}.png"')
    return declarations


def cells(sheet: Path, destination: Path) -> None:
    """Export each complete grid cell so source alignment remains visible."""
    destination.mkdir(parents=True, exist_ok=True)
    with Image.open(sheet) as atlas:
        for slot in SLOTS:
            x, y = slot["column"] * 85, slot["row"] * 85
            atlas.crop((x, y, min(x + 85, atlas.width),
                        min(y + 85, atlas.height))).save(destination / f'{slot["id"]}.png')


def contact_sheets(destination: Path, package: Path, sheet: Path | None = None,
                   include_pads: bool = True) -> None:
    """Label private source art without adding game pixels to tracked files."""
    try:
        font = ImageFont.truetype("arial.ttf", 17)
        small = ImageFont.truetype("arial.ttf", 14)
    except OSError:
        font = ImageFont.load_default()
        small = font

    def canvas(columns: int, rows: int, cell_width: int, cell_height: int) -> Image.Image:
        return Image.new("RGB", (columns * cell_width, rows * cell_height), (25, 30, 39))

    def panel(draw: ImageDraw.ImageDraw, x: int, y: int, width: int, height: int) -> None:
        draw.rounded_rectangle((x + 5, y + 5, x + width - 5, y + height - 5),
                               radius=9, fill=(41, 48, 60))

    glyphs = canvas(6, 6, 220, 145)
    draw = ImageDraw.Draw(glyphs)
    atlas = Image.open(sheet) if sheet else None
    for index, slot in enumerate(SLOTS):
        x, y = index % 6 * 220, index // 6 * 145
        panel(draw, x, y, 220, 145)
        slot_id = slot["id"]
        if atlas is not None:
            art = crop_slot(atlas, slot)
        else:
            with Image.open(package / "buttons" / f"{slot_id}.png") as source:
                art = source.convert("RGBA")
        if art.getbbox():
            glyphs.paste(art, (x + (220 - art.width) // 2, y + 10), art)
        else:
            draw.text((x + 20, y + 44), "transparent", font=font,
                      fill=(228, 176, 101))
        draw.text((x + 12, y + 99), slot_id, font=font, fill="white")
        draw.text((x + 12, y + 122), "glyph " + ", ".join(slot["glyphs"]),
                  font=small, fill=(185, 196, 210))
    glyphs.save(destination / "button-slots-contact.png")

    if atlas is not None:
        atlas.close()
    if not include_pads:
        return

    pads = canvas(4, 2, 320, 340)
    draw = ImageDraw.Draw(pads)
    for index, family in enumerate(("xbox", "ps")):
        for preset in range(4):
            x, y = preset * 320, index * 340
            panel(draw, x, y, 320, 340)
            slot_id = f"{family}_{preset}"
            with Image.open(package / "pads" / f"{slot_id}.png") as source:
                art = source.convert("RGBA")
            art.thumbnail((290, 290), Image.Resampling.LANCZOS)
            pads.paste(art, (x + (320 - art.width) // 2, y + 13), art)
            draw.text((x + 12, y + 306), slot_id, font=font, fill="white")
    pads.save(destination / "pad-slots-contact.png")


def manifest(package: Path, scheme_id: str, name: str,
             button_entries: list[str], pad_entries: list[str]) -> None:
    contents = (f'schema_version = 1\nid = "{scheme_id}"\nname = "{name}"\n'
                'kind = "controller"\n\n[buttons]\n' + "\n".join(button_entries) +
                '\n\n[controller.slots]\n' + "\n".join(pad_entries) + "\n")
    (package / "scheme.toml").write_text(contents, encoding="utf-8", newline="\n")
