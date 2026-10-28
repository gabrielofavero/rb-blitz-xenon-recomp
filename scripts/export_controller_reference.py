"""Shared helpers for private, gitignored controller-art reference exports."""

from pathlib import Path
import subprocess
import sys
import tomllib

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
TEXTURE_TOOL = ROOT / "scripts" / "hmx_milo.py"
SLOTS = tomllib.loads((ROOT / "assets/controller/slots.toml").read_text(encoding="utf-8"))["slot"]


def texture(scene: Path, asset: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(TEXTURE_TOOL), "texture", str(scene),
                    "--asset", asset, "--out", str(target)], check=True)


def buttons(sheet: Path, package: Path, selected: set[str] | None = None) -> list[str]:
    declarations = []
    with Image.open(sheet) as atlas:
        for slot in SLOTS:
            slot_id = slot["id"]
            if selected is not None and slot_id not in selected:
                continue
            x, y, width, height = slot["rect"]
            target = package / "buttons" / f"{slot_id}.png"
            target.parent.mkdir(parents=True, exist_ok=True)
            atlas.crop((x, y, x + width, y + height)).convert("RGBA").save(target)
            declarations.append(f'{slot_id} = "buttons/{slot_id}.png"')
    return declarations


def manifest(package: Path, scheme_id: str, name: str,
             button_entries: list[str], pad_entries: list[str]) -> None:
    contents = (f'schema_version = 1\nid = "{scheme_id}"\nname = "{name}"\n'
                'kind = "controller"\n\n[buttons]\n' + "\n".join(button_entries) +
                '\n\n[controller.slots]\n' + "\n".join(pad_entries) + "\n")
    (package / "scheme.toml").write_text(contents, encoding="utf-8", newline="\n")
