# Controller scheme v2 format

Each non-template art folder has a `scheme.toml`. The runtime and validator accept
these manifests alongside version 1 user packages. The build stages the
authored controller folders and the internal keyboard art. Each layout's
editable `pad.svg` and source `pad.png` live under
`assets/controller/masters/<layout-id>/` and are not listed in any manifest.

```toml
schema_version = 2
id = "xbox-series"             # matches folder name
name = "Xbox Series"
kind = "controller"            # or "keyboard-mouse"

[[input]]
id = "a_button"               # stable art ID; unique within this layout
file = "buttons/a_button.png" # relative RGBA PNG
source = "pad:a"               # normalized Xbox-position source for Gameplay Mapping
slot = "a_button"             # optional legacy atlas destination

[[input]]
id = "share"
file = "buttons/share.png"
source = "sdl:button:misc1"    # SDL's distinct Share button source
```

Controller v2 manifests may also use the v1 optional `[controller]`,
`[controller.slots]`, and `[icons]` PNG fields for pad diagrams and the whole
extra-icon sheet. Their fit, precedence, and omitted-slot fallback rules stay
the same. Xbox Series declares `pads/pad_0.png` through `pads/pad_3.png` for
`xbox_0` through `xbox_3`. Run `python scripts/controllers/generate_xbox_series_pads.py`
to regenerate them from the authoring
[`pad.png`](../../assets/controller/masters/xbox-series/pad.png),
[`ps3-arrow.svg`](../../assets/controller/masters/ps3-arrow.svg), and the four
[`pad_N-arrows.svg`](../../assets/controller/masters/xbox-series/pad_0-arrows.svg)
route masters. The generator requires Pillow, NumPy, and OpenCV. Each route SVG
keeps its original Xbox 360 texture's outer text anchors fixed; the generator
joins the arrow shapes before applying the PS3 color profile and checks branch
clearance and continuous arrow pixels. PlayStation 5 declares its four generated
`pads/pad_N.png` files for `ps_0` through `ps_3`. Run
`python scripts/controllers/generate_playstation_5_pads.py` to rebuild them from its
[`pad.png`](../../assets/controller/masters/playstation-5/pad.png) and
[`pad_N-arrows.svg`](../../assets/controller/masters/playstation-5/pad_0-arrows.svg)
masters. Its fixed text rails follow the corresponding PS3 reference pads.
PlayStation 4 declares the same four `ps_0` through `ps_3` slots. Run
`python scripts/controllers/generate_playstation_4_pads.py` to rebuild them from its
[`pad.png`](../../assets/controller/masters/playstation-4/pad.png) and
[`pad_N-arrows.svg`](../../assets/controller/masters/playstation-4/pad_0-arrows.svg)
masters, using the corresponding PS3 text rails.
Steam Deck declares four generated diagrams for `xbox_0` through `xbox_3`.
Run `python scripts/controllers/generate_steam_deck_pads.py` to rebuild them from its
[`pad.png`](../../assets/controller/masters/steam-deck/pad.png) and
[`pad_N-arrows.svg`](../../assets/controller/masters/steam-deck/pad_0-arrows.svg)
masters. Its front view shows one shoulder silhouette per side; pad 2 uses
the top rim for trigger callouts and outer shoulder edges for bumper callouts.
Switch Pro and Switch 2 Pro each declare four generated diagrams for
`xbox_0` through `xbox_3`. Run
`python scripts/controllers/generate_switch_pro_pads.py` to rebuild them from
their body PNGs and editable SVG routes. Switch Joy-Con and Switch 2 Joy-Con
also declare four generated diagrams for those slots. Run
`python scripts/controllers/generate_switch_joycon_pads.py` to rebuild them
from their body PNGs and editable SVG routes.
Meta Quest 2 and 3 declare four generated diagrams each for `xbox_0` through
`xbox_3`. Run `python scripts/controllers/generate_meta_quest_pads.py` to rebuild
them from their master PNGs and editable SVG routes. Their callouts use the
Xbox text rails while pointing to the Quest Touch controls.
The files in `masters/` are authoring sources; manifests reference finished
images in their layout folders.

`[[input]]` binds an image to a physical source. A source may have more than
one image entry (different shapes or legends); the entry without `variant` or
`modifiers`, when present, is the default. `variant` distinguishes alternate
art without pretending it is a new physical input. `modifiers = ["shift"]`
means that the glyph represents a
shifted key legend; the source remains the key's physical scancode. The current
  resolver chooses the unmodified, variant-free entry by default; callers can
  request a variant or the `shift` modifier explicitly. All
keyboard and mouse entries are custom: no controller-to-keyboard translation or
Xbox atlas slot is implied.

The standard controller `pad:` values are the existing remap vocabulary:
`a`, `b`, `x`, `y`, `left_shoulder`, `right_shoulder`, `left_trigger`,
`right_trigger`, `back`, `start`, `left_thumb`, `right_thumb`, four `dpad_*`
directions, and the eight `lstick_*` / `rstick_*` directions. These mean
Xbox 360 **positions**, not the printed letters on Nintendo face buttons.
`dpad` and neutral stick images have no single press source, so they use
`components = [...]` instead of `source`. Their directional images carry the
individual `pad:` sources. `slot` is the existing 512×512 atlas destination;
its name does not determine the physical source.

The new `sdl:button:*` values name normalized SDL gamepad buttons that the XInput
guest packet cannot represent independently: `misc1`, `misc2`, `touchpad`, and the four
`left_paddle1`, `left_paddle2`, `right_paddle1`, `right_paddle2` buttons. The
sampler captures them before any XInput projection. The
[SDK SDL driver](../../rexglue-sdk/src/input/sdl/sdl_input_driver.cpp)
keeps them separate from guest XInput bits, while the binding parser, capture UI,
persistence, and icon resolver accept the distinct source names.
SDL's [button enum](https://wiki.libsdl.org/SDL3/SDL_GamepadButton) establishes
the common meanings, while the [device mapping database](https://github.com/libsdl-org/SDL/blob/main/src/joystick/SDL_gamepad_db.h)
shows that availability varies by device, OS, and driver. The runtime checks
the connected device's mapping and button availability before capture. A manifest is an expected SDL mapping,
not a promise that every connection path exposes that button.

For PlayStation, the Share/Create button is the Xbox Back equivalent in the
standard SDL mapping. The touchpad-click art that came in the numbered Back
file has its own `sdl:button:touchpad` source. SDL identifies the PS5 microphone
button as `misc1`, so its art is declared too; the current SDK still suppresses
that input through its Guide mapping. Nintendo face art is similarly
mapped by button **position**: the bottom printed B is `pad:a`.

For the Steam Deck's direct HID path, SDL's
[device driver](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_steamdeck.c)
and [mapping database](https://github.com/libsdl-org/SDL/blob/main/src/joystick/SDL_gamepad_db.h)
map Quick Access to `misc1`, L4/L5/R4/R5 to the four paddle buttons, the left
trackpad click to `touchpad`, and the right trackpad click to `misc2`. A Steam
Input virtual controller may expose fewer of these, so the runtime must check
what SDL reports for the connected device. The `+`/`−` PNGs are not declared:
the Deck has Menu and View buttons, already represented by `pad:start` and
`pad:back`; assigning these PNGs extra sources would invent physical buttons.

Keyboard `key:` names follow SDL scancode names, and `mouse:` names use the
current mouse source grammar. The v2 binding parser must accept SDL names with
spaces, such as `key:Left Shift` and `key:KP Enter`. Shifted
punctuation entries use the underlying scancode plus `modifiers`. Decorative
whole-mouse art and OS-reserved keys/buttons are omitted. Guide, Home, Steam,
and Power are not exposed for gameplay remapping. Their PNGs can remain in the
art folder for future work. The packer copies **declared files only**. Version 1
keeps its 64-file limit; version 2 accepts up to 128 declared images, including
the keyboard folder's 92 images (95 PNGs on disk).

The loader validates unique IDs, known source grammar, safe relative
PNG paths, each file's existence and format, and the explicit schema version.
It rejects ambiguous duplicate defaults for one source unless a `variant`
or `modifiers` value distinguishes them. Version 1 packages remain a separate
accepted format for users who only replace legacy atlas slots. Controller v2
packages may be installed by users; the `keyboard-mouse` id and kind are reserved
for the application's internal art and never enter the controller selector.
