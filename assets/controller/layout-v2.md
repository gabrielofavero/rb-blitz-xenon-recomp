# Proposed authored layout manifest (version 2)

Each non-template art folder has a `scheme.toml`. This is an authoring contract
for the later runtime implementation. The current version 1 loader rejects these
manifests; the art folders are not installed as schemes today. `pads/pad.svg` is
temporary reference art and is deliberately not listed in any manifest.

```toml
schema_version = 2
id = "xbox-series"             # matches folder name
name = "Xbox Series"
kind = "controller"            # or "keyboard-mouse"

[[input]]
id = "a_button"               # stable art ID; unique within this layout
file = "buttons/1 - A.png"    # relative RGBA PNG
source = "pad:a"               # normalized Xbox-position source for Gameplay Mapping
slot = "a_button"             # optional legacy atlas destination

[[input]]
id = "share"
file = "buttons/C1 - Share.png"
source = "sdl:button:misc1"    # SDL's distinct Share button source
```

Controller v2 manifests may also use the v1 optional `[controller]`,
`[controller.slots]`, and `[icons]` PNG fields for pad diagrams and the whole
extra-icon sheet. Their fit, precedence, and omitted-slot fallback rules stay
the same. The current authored folders declare no pad PNGs; `pads/pad.svg` is
temporary and cannot be used in these fields.

`[[input]]` binds an image to a physical source. A source may have more than
one image entry (different shapes or legends); the entry without `variant` or
`modifiers`, when present, is the default. `variant` distinguishes alternate
art without pretending it is a new physical input. `modifiers = ["shift"]`
means that the glyph represents a
shifted key legend; the source remains the key's physical scancode. A future
resolver can select a variant or modifier-specific legend explicitly. All
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
future sampler must capture them before the SDK folds some into ordinary XInput
bits, and the binding parser, capture UI, persistence, and icon resolver must
accept them. The existing [SDK SDL driver](../../rexglue-sdk/src/input/sdl/sdl_input_driver.cpp)
folds `misc1` into Guide and paddles into face buttons, and ignores `misc2`;
it therefore does not yet deliver the distinct sources these manifests name.
SDL's [button enum](https://wiki.libsdl.org/SDL3/SDL_GamepadButton) establishes
the common meanings, while the [device mapping database](https://github.com/libsdl-org/SDL/blob/main/src/joystick/SDL_gamepad_db.h)
shows that availability varies by device, OS, and driver. The runtime should
check the connected device's mapping and button availability, then use input
capture to confirm the actual source. A manifest is an expected SDL mapping,
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
spaces, such as `key:Left Shift` and `key:KP Enter`; the current parser rejects
these even though the key sampler can resolve them. Shifted
punctuation entries use the underlying scancode plus `modifiers`. Decorative
whole-mouse art and OS-reserved keys/buttons are omitted. Guide, Home, Steam,
and Power are not exposed for gameplay remapping. Their PNGs can remain in the
art folder for future work, but a v2 packer should copy **declared files only**
and should raise its version 1 limit of 64 files for the keyboard folder's 92
declared images (95 PNGs on disk).

The later loader should validate unique IDs, known source grammar, safe relative
PNG paths, each file's existence and format, and the explicit schema version.
It should reject ambiguous duplicate defaults for one source unless a `variant`
or `modifiers` value distinguishes them. Version 1 packages remain a separate
accepted format for users who only replace legacy atlas slots. Controller v2
packages may be installed by users; the `keyboard-mouse` id and kind are reserved
for the application's internal art and never enter the controller selector.
