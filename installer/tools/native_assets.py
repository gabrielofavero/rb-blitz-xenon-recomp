"""Enumerate only controller files accepted by the game's own native validator."""
from pathlib import Path
import hashlib
import subprocess


def controller_entries(root: Path, validator: Path, destination: str) -> list[dict]:
    source_root = root / "assets/controller"
    entries = []
    retail_hashes = {hashlib.sha256(path.read_bytes()).digest()
                     for name in ("template-360", "template-ps3")
                     for path in (source_root / name).rglob("*.png") if path.is_file()}
    for folder in sorted(source_root.iterdir()):
        if (not folder.is_dir() or folder.name.startswith("template") or
                not (folder / "scheme.toml").is_file()):
            continue
        if folder.is_symlink():
            raise ValueError(f"linked controller folder: {folder}")
        internal = folder.name == "keyboard-mouse"
        mode = "--list-internal-keyboard" if internal else "--list-declared"
        files = subprocess.run([str(validator), mode, str(folder)], check=True,
                               capture_output=True, text=True).stdout.splitlines()
        if "scheme.toml" not in files or len(files) != len(set(files)):
            raise ValueError(f"invalid controller inventory: {folder}")
        target = "keyboard-mouse" if internal else f"schemes/{folder.name}"
        for name in files:
            path = Path(name)
            if path.is_absolute() or ".." in path.parts or path.suffix not in (".png", ".toml"):
                raise ValueError(f"unsafe declared controller file: {name}")
            source = folder / path
            if source.is_symlink() or not source.resolve().is_relative_to(folder.resolve()):
                raise ValueError(f"linked controller asset: {source}")
            if path.suffix == ".png" and hashlib.sha256(source.read_bytes()).digest() in retail_hashes:
                raise ValueError(f"controller asset is an exact copy of local retail art: {source}")
            entries.append({"from": source.relative_to(root).as_posix(),
                            "to": f"{destination}/{target}/{path.as_posix()}"})
    for name in ("Attributions.txt", "README.md"):
        entries.append({"from": f"assets/controller/{name}", "to": f"{destination}/{name}"})
    return entries
