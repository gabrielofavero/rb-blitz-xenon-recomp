"""Split Vector Source.svg into individual controller and keyboard master SVGs.

These editable vector masters are source art. Runtime controller schemes still
require PNGs; keyboard art has no runtime scheme format yet.

Vector Source.svg is from https://github.com/haaldor/Xelu_prompts_SVG, the SVG
edit of Xelu's Free Prompts Pack; both are CC0 1.0. Credits, the Steam Deck
COCOGOOSE font caveat, and the checked dates are in masters/README.md.
"""

from copy import deepcopy
from pathlib import Path
import argparse
import re
import shutil

from lxml import etree


ROOT = Path(__file__).resolve().parents[1]
MASTERS = ROOT / "assets" / "controller" / "masters"
SOURCE = MASTERS / "Vector Source.svg"
SVG = "http://www.w3.org/2000/svg"
INKSCAPE = "http://www.inkscape.org/namespaces/inkscape"

# Names follow the visible controls. Numbered variants retain source objects
# whose exact runtime meaning is not yet established.
EXPORTS = {
    "xbox-series": {
        "Xbox_Series": [
            "right_bumper", "left_bumper", "right_trigger", "left_trigger",
            "dpad_down", "dpad_left", "dpad_up", "dpad_right", "dpad",
            "share_button", "menu_button", "view_button", "right_stick",
            "left_stick", "right_stick_click", "left_stick_click",
            "x_button", "y_button", "b_button", "a_button", "pad",
        ],
    },
    "playstation-4": {
        "Playstation_4": [
            "dpad_left", "dpad_down", "dpad_right", "dpad_up", "dpad",
            "touchpad", "share_button", "options_button", "left_bumper",
            "right_bumper", "left_trigger", "right_trigger",
            "left_trigger_side", "right_trigger_side", "right_stick",
            "left_stick", "cross_button", "square_button", "triangle_button",
            "circle_button", "pad",
        ],
    },
    "playstation-5": {
        "Playstation_5": [
            "touchpad", "dpad_down", "dpad_left", "dpad_up", "dpad_right",
            "dpad", "create_button", "options_button",
            "create_button_indicator", "options_button_indicator",
            "right_stick", "left_stick", "right_stick_click", "left_stick_click",
            "mute_button", "left_trigger", "right_trigger", "right_bumper",
            "left_bumper", "cross_button", "square_button", "triangle_button",
            "circle_button", "pad",
        ],
    },
    "nintendo-switch": {
        "Nintendo_Switch": [
            "joycon_right_side", "joycon_left_side", "joycon_pair",
            "joycon_pair_rotated", "zl_button", "zr_button", "r_button",
            "l_button", "dpad_left", "dpad_down", "dpad_right", "dpad_up",
            "dpad", "dpad_plain", "minus_button", "capture_button",
            "home_button", "right_stick", "left_stick", "arrow_down",
            "arrow_left", "arrow_up", "arrow_right", "x_button", "y_button",
            "b_button", "a_button",
        ],
    },
    "meta-quest": {
        "Oculus_Touch": [
            "touch_right_controller", "touch_left_controller", "right_grip_plain",
            "right_grip", "left_grip", "right_trigger_plain", "left_trigger",
            "right_trigger", "right_stick", "left_stick", "x_button",
            "y_button", "b_button", "a_button",
        ],
        "Oculus_Remote": [
            "remote", "remote_select_bar", "remote_back_button",
            "remote_plus_button", "remote_minus_button", "remote_rotate_clockwise",
            "remote_rotate_counterclockwise", "remote_ring_left",
            "remote_ring_down", "remote_ring_right", "remote_ring_up",
            "remote_select_button", "remote_ring_button", "remote_center_button",
        ],
    },
    "steam-controller": {
        "Steam_Controller": [
            "gesture_rotate", "gesture_horizontal", "gesture_tilt", "gesture_motion",
            "left_trigger", "right_trigger", "left_bumper", "right_bumper",
            "right_grip", "left_grip", "trackpad_ring", "steam_button",
            "previous_arrow", "next_arrow", "x_button", "y_button",
            "b_button", "a_button", *[f"trackpad_variant_{n:02d}" for n in range(1, 13)],
        ],
    },
    "steam-deck": {
        "Steam_Deck": [
            "steam_deck_logo", "l5_button", "r5_button", "l4_button",
            "r4_button", "l2_button", "r2_button", "l1_button", "r1_button",
            "power_button", "minus_button", "plus_button", "view_button",
            "menu_button", "quick_access_button", "steam_button",
            "right_trackpad", "left_trackpad", "right_stick", "left_stick",
            "right_stick_click", "left_stick_click", "x_button", "y_button",
            "b_button", "a_button", "dpad_down", "dpad_left", "dpad_up",
            "dpad_right", "dpad",
        ],
    },
    "keyboard-dark": {
        "Keyboard_Dark": [
            "mouse_side_button_lower", "mouse_side_button_upper",
            "mouse_wheel_vertical", "mouse_wheel_down", "mouse_wheel_up",
            "mouse_wheel", "mouse_right_button", "mouse_left_button", "mouse",
            "numpad_plus", "numpad_enter", "enter_return_arrow", "enter_wide",
            "space", "shift_wide", "backspace", "backspace_arrow",
            "caps_lock", "tab", "alt", "ctrl", "shift", "windows",
            "num_lock", "escape", "asterisk", "less_than", "greater_than",
            "right_bracket", "left_bracket", "double_quote", "semicolon",
            "question_mark", "backslash", "plus", "minus", "print_screen",
            "page_up", "page_down", "home", "end", "delete", "insert",
            "tilde", "command", "arrow_left", "arrow_down", "arrow_right",
            "arrow_up", *[f"digit_{n}" for n in range(9, -1, -1)],
            *[f"key_{letter.lower()}" for letter in "MNBVCXZLKJHGFDSAPOIUYTREWQ"],
        ],
    },
}

# Most source icons occupy the same 100 by 100 drawing cell. The three full
# controller diagrams and the tall Steam gesture need their own canvas.
SPECIAL_VIEWBOX = {
    "Xbox_Series-69": (-290, -201, 681, 449),
    "Playstation_4-97": (-273, -121, 649, 392),
    "Playstation_5-59": (-286, -141, 679, 401),
    "Playstation_5-99": (0, 16, 100, 40),
    "Steam_Controller-6": (0, 0, 100, 215),
}


def local_references(element: etree._Element) -> set[str]:
    """Find SVG paint, clip, and linked-definition references in a subtree."""
    references = set()
    for node in element.iter():
        for key, value in node.attrib.items():
            references.update(re.findall(r"url\(#([^)]*)\)", value))
            if key.endswith("}href") and value.startswith("#"):
                references.add(value[1:])
    return references


def add_source_definitions(root: etree._Element, source_ids: dict[str, etree._Element],
                           fonts: list[etree._Element]) -> None:
    """Copy referenced paint definitions and embedded fonts used by text."""
    known = {node.get("id") for node in root.iter() if node.get("id")}
    definitions = etree.Element(f"{{{SVG}}}defs")
    if any(node.tag == f"{{{SVG}}}text" for node in root.iter()):
        for font in fonts:
            definitions.append(deepcopy(font))
    pending = local_references(root) - known
    while pending:
        reference = min(pending)
        pending.remove(reference)
        if reference in known:
            continue
        source = source_ids.get(reference)
        if source is None or not any(parent.tag == f"{{{SVG}}}defs" for parent in source.iterancestors()):
            raise ValueError(f"Missing source definition: {reference}")
        copied = deepcopy(source)
        definitions.append(copied)
        known.update(node.get("id") for node in copied.iter() if node.get("id"))
        pending.update(local_references(copied) - known)
    if len(definitions):
        root.insert(0, definitions)


def write_svg(folder: Path, name: str, child: etree._Element,
              box: tuple[int, int, int, int], source_ids: dict[str, etree._Element],
              fonts: list[etree._Element]) -> None:
    x, y, width, height = box
    root = etree.Element(f"{{{SVG}}}svg", nsmap={None: SVG, "inkscape": INKSCAPE})
    root.set("version", "1.1")
    root.set("width", str(width))
    root.set("height", str(height))
    root.set("viewBox", f"{x} {y} {width} {height}")
    child.set(f"{{{INKSCAPE}}}label", name)
    root.append(child)
    add_source_definitions(root, source_ids, fonts)
    (folder / f"{name}.svg").write_bytes(
        etree.tostring(root, encoding="UTF-8", xml_declaration=True))


def organized_layout(folder: Path) -> tuple[dict[str, str], dict[str, str]] | None:
    """Read the slot names and adaptation sources from an organized audit."""
    audit = folder / "AUDIT.md"
    if not (folder / "unused").is_dir() or not audit.is_file():
        return None
    direct = {}
    adaptations = {}
    rows = re.findall(r"^\| `([^`]+)` \| (Direct|Adapt|Missing) \| (.*) \|$",
                      audit.read_text(encoding="utf-8"), re.M)
    for slot, status, field in rows:
        if status == "Direct":
            renamed = re.search(r"renamed from `([^`]+)\.svg`", field)
            source = renamed.group(1) if renamed else slot
            if source in direct:
                raise ValueError(f"{audit}: duplicate direct source {source}")
            direct[source] = slot
        elif status == "Adapt":
            copied = re.search(r"copy of \[[^]]+\]\(([^)]+)\.svg\)", field)
            if not copied:
                raise ValueError(f"{audit}: no source for {slot}_adapt")
            adaptations[slot] = Path(copied.group(1)).stem
    if not rows or not direct:
        raise ValueError(f"{audit}: no organized slot rows")
    return direct, adaptations


def export(presets: list[str] | None = None) -> int:
    document = etree.parse(str(SOURCE))
    source_ids = {node.get("id"): node for node in document.getroot().iter() if node.get("id")}
    fonts = list(document.getroot().find(f"{{{SVG}}}defs").iter(f"{{{SVG}}}font"))
    layers = {
        layer.get(f"{{{INKSCAPE}}}label"): layer
        for layer in document.getroot().iter(f"{{{SVG}}}g")
        if layer.get(f"{{{INKSCAPE}}}label")
    }
    count = 0
    for preset in presets or EXPORTS:
        groups = EXPORTS[preset]
        folder = MASTERS / preset
        folder.mkdir(parents=True, exist_ok=True)
        organized = organized_layout(folder)
        direct, adaptations = organized if organized else ({}, {})

        def emit(name: str, child: etree._Element, box: tuple[int, int, int, int]) -> None:
            destination = folder if not organized or name in direct else folder / "unused"
            write_svg(destination, direct.get(name, name), child, box, source_ids, fonts)

        for label, names in groups.items():
            objects = list(layers[label])
            if len(objects) != len(names):
                raise ValueError(f"{label}: expected {len(names)} objects, found {len(objects)}")
            for source_object, name in zip(objects, names, strict=True):
                child = deepcopy(source_object)
                source_id = child.get("id")
                # Translation and scale place the object on the source sheet.
                # Its own drawing coordinates make an independent master.
                child.attrib.pop("transform", None)
                if source_id == "Playstation_5-99":
                    # The source object combines a mute button drawing and
                    # its crossed-microphone symbol in one group.
                    mute = deepcopy(child)
                    for path in list(mute.iter(f"{{{SVG}}}path")):
                        if path.get("id") != "path162-1":
                            path.getparent().remove(path)
                    for path in list(child.iter(f"{{{SVG}}}path")):
                        if path.get("id") == "path162-1":
                            path.getparent().remove(path)
                    emit("mute_icon", mute, (30, 50, 40, 43))
                    count += 1
                emit(name, child, SPECIAL_VIEWBOX.get(source_id, (0, 0, 100, 100)))
                count += 1
        for slot, source in adaptations.items():
            source_file = ((folder / f"{direct[source]}.svg") if source in direct else
                           (folder / "unused" / f"{source}.svg"))
            shutil.copyfile(source_file, folder / f"{slot}_adapt.svg")
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preset", action="append", choices=EXPORTS,
                        help="Export only this preset; may be repeated")
    args = parser.parse_args()
    print(f"Exported {export(args.preset)} SVG masters from {SOURCE}")
