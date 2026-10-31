"""Trim transparent margins from authored controller and keyboard/mouse PNGs.

The game fits the entire PNG canvas into its button slot. The Xbox 360 reference
exports are already tight crops, so keeping source margins makes custom art
smaller in the same slot. The internal keyboard/mouse icons use the same image
path. This trims only fully transparent outer pixels; it does not resample or
change any visible pixels.
"""

from pathlib import Path
import tomllib

from PIL import Image


ROOT = Path(__file__).resolve().parents[2] / "assets" / "controller"


def trim(path: Path, destination: Path) -> bool:
    with Image.open(path) as source:
        image = source.convert("RGBA")
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError(f"empty button image: {path}")
    if destination != path and destination.exists():
        raise ValueError(f"destination already exists: {destination}")
    changed = bounds != (0, 0, *image.size) or destination != path
    if changed:
        image.crop(bounds).save(destination)
        if destination != path:
            path.unlink()
    return changed


def main() -> None:
    total = 0
    for folder in sorted(ROOT.iterdir()):
        manifest = folder / "scheme.toml"
        internal_keyboard = folder.name == "keyboard-mouse"
        images = folder if internal_keyboard else folder / "buttons"
        if not manifest.is_file() or not images.is_dir():
            continue
        if folder.name.startswith("template-"):
            continue

        original = manifest.read_bytes()
        text = original.decode("utf-8")
        data = tomllib.loads(text)
        declared = set()
        for entry in data.get("input", []):
            relative = entry.get("file", "")
            if internal_keyboard and Path(relative).parent != Path("."):
                continue
            if not internal_keyboard and not relative.startswith("buttons/"):
                continue
            path = folder / relative
            if not path.is_file():
                raise FileNotFoundError(path)
            declared.add(path)
            replacement = f'{entry["id"]}.png' if internal_keyboard else f'buttons/{entry["id"]}.png'
            if relative != replacement:
                old_line = f'file = "{relative}"'
                new_line = f'file = "{replacement}"'
                if text.count(old_line) != 1:
                    raise ValueError(f"ambiguous manifest path: {manifest}: {relative}")
                text = text.replace(old_line, new_line, 1)
            total += trim(path, folder / replacement)

        for path in sorted(images.glob("*.png")):
            if path not in declared and not any(
                path.name == f'{entry["id"]}.png' for entry in data.get("input", [])
            ):
                total += trim(path, path)

        if text != original.decode("utf-8"):
            manifest.write_bytes(text.encode("utf-8"))
        print(folder.name)
    print(f"Updated {total} input PNGs")


if __name__ == "__main__":
    main()
