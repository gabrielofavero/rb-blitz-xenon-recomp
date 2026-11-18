"""Configured SDK runtime modules and dependency sources shared by native staging."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
MODULES = json.loads((ROOT / "installer/config/runtime-modules.json").read_text())

def cmake_cache(build: Path) -> dict[str, str]:
    path = build / "CMakeCache.txt"
    return dict(re.findall(r"(?m)^([^/#:=\r\n][^:=\r\n]*):[^=\r\n]*=([^\r\n]*)$", path.read_text())) if path.is_file() else {}

def flag(cache: dict[str, str], *names: str, default: bool = True) -> bool:
    value = next((cache[name] for name in names if name in cache), "ON" if default else "OFF").strip().upper()
    return value not in {"", "0", "FALSE", "OFF", "NO", "N", "IGNORE", "NOTFOUND"} and not value.endswith("-NOTFOUND")

def enabled(build: Path, feature: str) -> bool:
    cache = cmake_cache(build)
    names = {"vulkan": ("REXGLUE_USE_VULKAN", "REXGLUE_GPU_VULKAN"), "tracy": ("REXGLUE_ENABLE_TRACY",),
             "d3d12": ("REXGLUE_USE_D3D12",)}
    return flag(cache, *names[feature])

def runtime_targets(build: Path, platform: str) -> list[str]:
    names = list(MODULES["core"])
    for feature, values in MODULES["conditional"].items():
        if feature == "windows":
            if platform == "windows": names += values
        elif feature == "d3d12":
            if platform == "windows" and enabled(build, feature): names += values
        elif enabled(build, feature): names += values
    return names

def target_filename(name: str, platform: str) -> str:
    if not name.startswith("rex") and name != "TracyClient" and name != "SDL3": return name
    if name == "SDL3" and platform == "linux": return "libSDL3.so.0"
    return name + ".dll" if platform == "windows" else "lib" + ("SDL3.0" if name == "SDL3" else name) + (".dylib" if platform == "macos" else ".so")

def manifest_entries(config: dict, build: Path) -> list[dict]:
    return [entry for entry in config["stageManifest"] if not entry.get("when") or enabled(build, entry["when"])]

def vulkan_source(build: Path, relative: str) -> Path:
    cache = cmake_cache(build)
    root = Path(cache.get("REXGLUE_VULKAN_SOURCE_ROOT", str(build / "_deps/rex-vulkan"))).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"missing prepared Vulkan license: {relative}; preserve REXGLUE_VULKAN_SOURCE_ROOT")
    return path

def molten_dependency_license(build: Path, name: str) -> Path:
    cache = cmake_cache(build)
    roots = [Path(cache.get("CPM_SOURCE_CACHE", str(build / "_deps"))), build / "_deps"]
    roots += [Path(value) for key, value in cache.items() if key.lower() == name.lower().replace('-', '_') + "_source_dir"]
    matches = set()
    for root in roots:
        if not root.is_dir(): continue
        for candidate in root.rglob("LICENSE*"):
            parents = [parent for parent in (candidate.parent, candidate.parent.parent)
                       if name.replace("-", "").lower() in parent.name.replace("-", "").lower()]
            if not parents: continue
            if candidate.is_file() and candidate.resolve().is_relative_to(root.resolve()): matches.add(candidate.resolve())
    if not matches: raise ValueError(f"missing pinned MoltenVK {name} license; retain its configured source cache")
    groups = {hashlib.sha256(path.read_bytes()).hexdigest() for path in matches}
    if len(groups) != 1: raise ValueError(f"ambiguous MoltenVK {name} license sources; use a clean dependency source cache")
    return sorted(matches)[0]
