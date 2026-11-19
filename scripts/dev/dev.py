"""Native Unix development commands, dispatched by dev.sh (Python 3.11+)."""
from __future__ import annotations

import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[2]
HELP = """Usage: bash ./dev.sh [--platform linux|mac|ios|windows] ACTION [arguments]
Or load commands in Bash/zsh: source ./dev.sh

  build-game [--configure] [build arguments]
  run-game [--mobile] [--touch] [--window] [--normal] [--no-dlc] [engine arguments]
  run-game-quick                 Windowed, empty DLC, skip offline dialogs
  build-installer [packager arguments]
  dev-help

Actions: build, run (or run-game), run-normal, run-window, run-mobile,
         run-no-dlc (or no-dlc), quick, build-installer (or installer), help

Mobile runs use mobile settings and mouse-as-touch in a portrait window.
Settings are copied under out/dev-mobile; saved settings are never edited.
--touch starts with touch prompts and treats mouse activity as touch input.
DLC follows native paths.toml, with the legacy profile as a fallback.
Extra engine arguments follow defaults and retain spaces and punctuation.

Linux/macOS use matching amd64/arm64 release presets. Mac packaging uses the
existing native .app/.pkg builder; Linux packaging requires its nFPM arguments.
iOS is selected explicitly on a Mac: bash ./dev.sh --platform ios build
  build / build-installer       Existing signed/unsigned IPA builder arguments
  run --device ID [--bundle-id ID] [engine arguments]
iOS launches an already installed app through Xcode devicectl. Desktop window
and mobile preview tags do not apply to iOS; use its on-device display settings.
Unix quick runs leave title-screen input to the player.
"""


def preset(target: str, machine: str | None = None) -> str:
    if target == "ios":
        return "ios-arm64-release"
    machine = (machine or platform.machine()).lower()
    arch = {"x86_64": "amd64", "amd64": "amd64", "aarch64": "arm64", "arm64": "arm64"}.get(machine)
    if not arch:
        raise ValueError(f"Unsupported development architecture: {machine}")
    return f"{target}-{arch}-release"


def native_config(target: str) -> Path:
    if target == "mac":
        return Path.home() / "Library/Application Support/rb_blitz"
    configured = Path(os.environ.get("XDG_CONFIG_HOME", ""))
    base = configured if configured.is_absolute() else Path.home() / ".config"
    return base / "rb_blitz"


def portable(build: Path) -> bool:
    return any((build / marker).is_file() for marker in
               ("rb_blitz_controller_settings.portable", "rb_blitz_launcher.portable"))


def take_option(args: list[str], names: tuple[str, ...], default: str = "") -> tuple[str, list[str]]:
    """Remove repeated value options; the final explicit value wins."""
    result: list[str] = []
    value = default
    index = 0
    while index < len(args):
        arg = args[index]
        if arg in names:
            index += 1
            if index == len(args) or args[index].startswith("--"):
                raise ValueError(f"{arg} requires a value")
            value = args[index]
        elif any(arg.startswith(name + "=") for name in names):
            value = arg.split("=", 1)[1]
        else:
            result.append(arg)
        index += 1
    return value, result


def profile_path(target: str, build: Path, explicit: str = "") -> Path:
    value = explicit or os.environ.get("RBBLITZ_CONTROLLER_SETTINGS_PROFILE", "")
    if value:
        path = Path(value)
        return path if path.is_absolute() else build / path
    config = native_config(target)
    pointer = config / "settings_dir.txt"
    if pointer.is_file():
        chosen = pointer.read_text(encoding="utf-8").strip()
        if chosen:
            return Path(chosen) / "launcher.toml"
    return (build if portable(build) else config) / "launcher.toml"


def set_scalars(text: str, section: str, values: dict[str, str]) -> str:
    """Edit only scalar lines in one table, retaining comments and other tables."""
    table = re.search(r"(?ms)^\[" + re.escape(section) + r"\][^\r\n]*\r?\n(.*?)(?=^\[|\Z)", text)
    body = table[1] if table else ""
    for key, value in values.items():
        pattern = r"(?m)^[ \t]*(?:" + re.escape(key) + "|'" + re.escape(key) + "'|\"" + re.escape(key) + r"\")[ \t]*=[^\r\n]*"
        rendered = f"'{key}'" if "." in key else key
        line = f"{rendered} = {value}"
        if re.search(pattern, body):
            body = re.sub(pattern, lambda _: line, body)
        else:
            body = body.rstrip() + "\n" + line + "\n"
    replacement = f"[{section}]\n{body}"
    return text[:table.start()] + replacement + text[table.end():] if table else text.rstrip() + "\n\n" + replacement


def mobile_profile(source: Path) -> Path:
    settings_name = "settings.toml" if source.name == "launcher.toml" else source.stem + ".game-settings.toml"
    settings = source.with_name(settings_name)
    text = settings.read_text(encoding="utf-8") if settings.is_file() else "schema_version = 1\n"
    tomllib.loads(text)  # Refuse malformed input before creating a session.
    text = set_scalars(text, "graphics", {"interface_orientation": "'portrait'",
                                          "gameplay_orientation": "'portrait'"})
    text = set_scalars(text, "provenance", {f"graphics.{key}": "'manual'" for key in
                                          ("interface_orientation", "gameplay_orientation")})
    parent = ROOT / "out/dev-mobile"
    parent.mkdir(parents=True, exist_ok=True)
    session = Path(tempfile.mkdtemp(dir=parent))
    if source.is_file():
        shutil.copyfile(source, session / "launcher.toml")
    (session / "settings.toml").write_text(text, encoding="utf-8")
    print(f"Mobile preview; settings copy: {session}", flush=True)
    return session / "launcher.toml"


def dlc_arguments(target: str, build: Path, profile: Path, args: list[str]) -> list[str]:
    if any(re.match(r"--(?:blitz\.runtime\.)?dlc_(?:root|library)(?:=|$)", arg) for arg in args):
        return []
    if any(os.environ.get(name) for name in ("REX_DLC_ROOT", "REX_DLC_LIBRARY",
           "REX_BLITZ_RUNTIME_DLC_ROOT", "REX_BLITZ_RUNTIME_DLC_LIBRARY")):
        return []
    path_file = (build if portable(build) else native_config(target)) / "paths.toml"
    if path_file.is_file():
        # The runtime owns the native path file and validates it itself.
        try:
            if tomllib.loads(path_file.read_text(encoding="utf-8")).get("paths", {}).get("dlc_root"):
                return []
        except (ValueError, OSError) as error:
            print(f"Cannot read {path_file}: {error}", file=sys.stderr)
            return []
    if profile.is_file():
        doc = tomllib.loads(profile.read_text(encoding="utf-8"))
        value = doc.get("settings", {}).get("dlc_root") or doc.get("launch", {}).get("dlc_dir")
        if value:
            root = Path(value)
            if not root.is_absolute():
                root = profile.parent / root
            if root.is_dir():
                return [f"--blitz.runtime.dlc_root={root}"]
            print(f"Configured DLC folder does not exist: {root}", file=sys.stderr)
    return []


def execute(command: list[str], cwd: Path = ROOT) -> int:
    return subprocess.call(command, cwd=cwd)


def build_game(target: str, args: list[str]) -> int:
    if target == "ios":
        return execute(["sh", str(ROOT / "scripts/ios/build.sh"), *args])
    configure = any(arg in ("--configure", "-Configure") for arg in args)
    args = [arg for arg in args if arg not in ("--configure", "-Configure")]
    name = preset(target)
    build = ROOT / "out/build" / name
    if configure or not (build / "CMakeCache.txt").is_file():
        result = execute(["cmake", "--preset", name])
        if result:
            return result
    result = execute(["cmake", "--build", "--preset", name, *args])
    if result == 0 and target == "mac":
        result = execute([sys.executable, str(ROOT / "scripts/macos/stage_direct.py"), "--build-dir", str(build)])
    return result


def run_game(target: str, args: list[str]) -> int:
    tags = {"--mobile", "--touch", "--window", "--windowed", "--normal", "--no-dlc", "--quick"}
    selected = set(args) & tags
    forward = [arg for arg in args if arg not in tags]
    if target == "ios":
        if selected & {"--mobile", "--window", "--windowed", "--quick"}:
            raise ValueError("Desktop window/mobile/quick tags are unavailable on iOS; use on-device display settings.")
        device, forward = take_option(forward, ("--device",), os.environ.get("RBBLITZ_IOS_DEVICE", ""))
        bundle, forward = take_option(forward, ("--bundle-id",), "io.github.gabrielofavero.rbblitz")
        if not device:
            raise ValueError("iOS launch requires --device ID or RBBLITZ_IOS_DEVICE.")
        defaults = ["--blitz.compatibility.ultimate_mode=0"] if "--normal" in selected else []
        if selected & {"--touch", "--mobile"}:
            defaults += ["--blitz.diagnostics.mouse_as_touch=true"]
        if "--no-dlc" in selected:
            defaults += ["--blitz.runtime.dlc_root=/nonexistent-rb-blitz-dlc",
                         "--blitz.runtime.dlc_library=/nonexistent-rb-blitz-dlc"]
        return execute(["xcrun", "devicectl", "device", "process", "launch", "--device", device,
                        bundle, *defaults, *forward])
    build = ROOT / "out/build" / preset(target)
    binary = build / "rb_blitz"
    if not binary.is_file():
        raise ValueError(f"Game executable is missing: {binary}. Run build-game first.")
    defaults = [f"--title.game_root={ROOT / 'game'}"]
    if selected & {"--touch", "--mobile"}:
        defaults += ["--blitz.diagnostics.mouse_as_touch=true"]
    if "--normal" in selected:
        defaults += ["--blitz.compatibility.ultimate_mode=0"]
    if selected & {"--window", "--windowed", "--mobile", "--quick"}:
        defaults += ["--ui.fullscreen=false"]
    if selected & {"--no-dlc", "--quick"}:
        empty = ROOT / "out/no-dlc"
        empty.mkdir(parents=True, exist_ok=True)
        defaults += [f"--blitz.runtime.dlc_root={empty}", f"--blitz.runtime.dlc_library={empty}"]
    if "--quick" in selected:
        defaults += ["--blitz.enhancements.enhancements_skip_offline_dialog=true"]
    profile, remaining = take_option(forward, ("--launcher_profile", "--blitz.runtime.launcher_profile"))
    source = profile_path(target, build, profile)
    defaults += dlc_arguments(target, build, source, defaults + forward)
    if "--mobile" in selected:
        defaults += ["--blitz.diagnostics.mobile_preview=true"]
        forward = [f"--blitz.runtime.launcher_profile={mobile_profile(source)}", *remaining]
    return execute([str(binary), *defaults, *forward], build)


def main(argv: list[str]) -> int:
    target, *argv = argv
    action = argv.pop(0) if argv else "help"
    if action in ("help", "--help", "-h") or "--help" in argv or "-h" in argv:
        print(HELP)
        return 0
    if target in ("mac", "ios") and platform.system() != "Darwin":
        raise ValueError(f"{target} commands require a Mac host with the native Apple tools.")
    if target == "linux" and platform.system() != "Linux":
        raise ValueError("Linux commands require a native Linux host.")
    shortcuts = {"run-normal": "--normal", "run-window": "--window", "run-mobile": "--mobile",
                 "run-no-dlc": "--no-dlc", "no-dlc": "--no-dlc", "quick": "--quick"}
    if action in shortcuts:
        argv.insert(0, shortcuts[action])
        action = "run"
    if action == "build":
        return build_game(target, argv)
    if action in ("run", "run-game"):
        if argv[:1] == ["game"]:
            argv.pop(0)
        return run_game(target, argv)
    if action in ("installer", "build-installer"):
        if target == "ios":
            return build_game(target, argv)
        if target == "mac":
            return execute(["sh", str(ROOT / "installer/macos/build.sh"), "--development", *argv])
        result = build_game(target, [])
        return result or execute([sys.executable, str(ROOT / "installer/linux/package.py"),
                                  "--build-dir", str(ROOT / "out/build" / preset(target)), *argv])
    raise ValueError(f"Unknown action: {action}. Run dev-help for commands.")


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, ValueError) as error:
        print(f"Development command failed: {error}", file=sys.stderr)
        sys.exit(1)
