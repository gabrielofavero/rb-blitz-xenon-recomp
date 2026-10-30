# Controller art masters

Each controller has a `<layout-id>/` folder here, matching its layout folder
under `assets/controller/`. Its `pad.svg` is editable source art and `pad.png`
is the raster body used while composing annotated pad diagrams. These files are
authoring sources; runtime schemes use only the PNGs declared in their layout
folders.

The existing vector export helper is `scripts/export_controller_vector_masters.py`.
It identifies its upstream source as the CC0 Xelu prompts SVG adaptation by
haaldor and notes the Steam Deck COCOGOOSE font caveat. The pad SVGs were moved
here unchanged from their layout folders.

[`ps3-arrow.svg`](ps3-arrow.svg) is the reusable arrowhead and color reference.
The head's tip is `(0, 0)` and its shaft enters from the left. Its dimensions,
13-pixel cyan shaft, 3-pixel black rim, and cyan color stops were measured from
the local PS3 arrow artwork. The Xbox Series pad generator draws 14-pixel
shafts for pads 0–2 and a 13-pixel shaft for pad 3 to match their Xbox 360
text rails. Edit `cyan-head` to change the
head silhouette; edit `raster-depth-profile` to change the output PNG's seven
cyan shades.

The Xbox Series folder has a separate `pad_N-arrows.svg` route master for each
annotated pad.
Its `routes` polylines start at text rails and end at input edges. The
`text-rails` lines carry the fixed game text endpoints, measured for each pad
from `template-360/pads/pad_N.png`. The arrow tips follow the Xbox Series
controls, so their angles differ where its pad art differs from the Xbox 360
controller. Pad 1 shares a bottom rail across three arrows; pad 2 calls out
the bumpers from side rails; pad 3 calls out X, B, and A on the right. Pad 3's
top-right rail starts farther right than the original so it clears the Series
controller's Y button; its outer end still lines up with the game text.
Pads 0–2 route X through the narrow space between B and A, then use a short
horizontal segment into X, following the Xbox 360 callout shape.

The generator at `scripts/generate_xbox_series_pads.py` reads the Xbox Series
master PNG, its arrow SVGs, and the shared PS3 arrow SVG. It
rounds only the corners, joins the shapes at four times output size, and applies
one exterior rim and the PS3 cyan profile before writing the Xbox Series
`pads/pad_0.png` through `pads/pad_3.png`. The older
`scripts/generate_controller_pad_0.py` entry point still rebuilds only pad 0.
This keeps intersections free of internal borders and
preserves smooth edges at the game's output resolution.
