# Controller art masters

Each controller has a `<layout-id>/` folder here, matching its layout folder
under `assets/controller/`. Its `pad.svg` is editable source art and `pad.png`
is the transparent raster body used while composing annotated pad diagrams.
These files are authoring sources; runtime schemes use only the PNGs declared
in their layout folders.

The existing vector export helper is `scripts/controllers/export_controller_vector_masters.py`.
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

The generator at `scripts/controllers/generate_xbox_series_pads.py` reads the Xbox Series
master PNG, its arrow SVGs, and the shared PS3 arrow SVG. It
rounds only the corners, joins the shapes at four times output size, and applies
one exterior rim and the PS3 cyan profile before writing the Xbox Series
`pads/pad_0.png` through `pads/pad_3.png`. The older
`scripts/controllers/generate_controller_pad_0.py` entry point still rebuilds only pad 0.
This keeps intersections free of internal borders and
preserves smooth edges at the game's output resolution.

The PlayStation 5 folder likewise has `pad_0-arrows.svg` through
`pad_3-arrows.svg`. Their text rails follow the corresponding PS3 pad textures,
and their arrow tips follow the PS5 controls. Run
`python scripts/controllers/generate_playstation_5_pads.py` to regenerate the four PNGs in
`assets/controller/playstation-5/pads/`. The PS5 generator shares the arrow
head, cyan profile, and route renderer with the Xbox Series generator. It
uses the transparent source PNG's alpha. The PS5 body keeps its source aspect
ratio; only the arrow tips move to its controls.

The PlayStation 4 folder follows the same route and text-rail convention. Its
`pad_0-arrows.svg` through `pad_3-arrows.svg` point to PS4 controls while keeping
the PS3 text rails. Run `python scripts/controllers/generate_playstation_4_pads.py` to
regenerate `assets/controller/playstation-4/pads/pad_0.png` through `pad_3.png`.
The generator uses the transparent source PNG's alpha, preserves the PS4 body's
aspect ratio, and uses the shared PS3 arrow renderer.

The Steam Deck folder has four `pad_N-arrows.svg` route masters and the same
PS3 text-rail outer endpoints. Run `python scripts/controllers/generate_steam_deck_pads.py` to
rebuild `assets/controller/steam-deck/pads/pad_0.png` through `pad_3.png`.
The front-view master does not distinguish each trigger from its bumper, so
pads 0, 1, and 3 use one arrow per shoulder side. Pad 2 needs separate callouts:
the trigger arrows meet the top rim and the bumper arrows meet the outer
shoulder edges. Those positions identify regions of the shared silhouette,
not separately visible controls. Pads 0 and 3 share one shaft outside the left
edge for the D-pad and left stick. Pad 0 also shares a straight right shaft for
the lower X button and right stick. The generator crops the transparent export
margin to preserve the body framing and arrow alignment; the SVG previews use
that same framing.

The Switch Pro and Switch 2 Pro folders each have four editable
`pad_N-arrows.svg` masters. Run
`python scripts/controllers/generate_switch_pro_pads.py` to rebuild their
`pads/pad_0.png` through `pad_3.png` textures. The source bodies keep their
aspect ratios, while the text rails retain the Xbox 360 label endpoints.
Pads 0, 1, and 3 use one top arrow for each visible shoulder pair. Pad 2
distinguishes triggers with downward arrows from the top rails and bumpers
with upward arrows from the side rails. Pad 3 shortens the inner end of its
upper-right rail to leave room for the Switch A button callout.
The top shoulder rails also stop before the controller to leave enough shaft
for complete arrowheads. Pad 2's upward tips meet the visible outer shoulder
edges. The generator checks both tip regions and minimum top-arrow length.
Their route IDs follow Xbox pad positions: `x_button` points to the printed
Switch Y, `a_button` to B, and `b_button` to A. The generator validates these
face-button tips so the pad art stays consistent with the scheme's `pad:`
sources and Xbox atlas slots.
The Y callouts enter the button from its right edge on pads 0–2 and from its
top edge on pad 3. Pads 0 and 3 start their right text rails exactly at the Y
branch, avoiding a short rail stub at the elbow.

The Switch Joy-Con and Switch 2 Joy-Con folders likewise have four editable
`pad_N-arrows.svg` masters. Run
`python scripts/controllers/generate_switch_joycon_pads.py` to rebuild their
`pads/pad_0.png` through `pad_3.png` textures. Each transparent paired body
keeps its aspect ratio and the callouts share the Xbox 360 text-rail endpoints.
The face routes use Xbox positions: `x_button` reaches the printed Y,
`a_button` reaches B, and `b_button` reaches A. Pads 0, 1, and 3 use one
top arrow per shoulder pair. These arrows descend onto the gray top shoulder
rim. Pad 2 uses the descending arrows for triggers and upward arrows along the
outside edges for bumpers; those tips also meet the gray rim. The generator
checks face-button destinations, top-arrow shaft lengths, continuous text rails,
the gray shoulder tip regions, and the Y rail elbow on pads 0 and 3.
Pads 0 and 3 also join the D-pad and left-stick arrows along one shaft just
outside the left Joy-Con, following the Steam Deck route style.
Pad 0 joins the right-stick and printed B callouts on a shaft outside the
right Joy-Con, entering both controls from the right. Its Y callout follows a
separate outer shaft, passes above the face buttons, and approaches Y
diagonally from the upper right.
Pad 1 routes the left-stick arrow along the outside of the left Joy-Con and
enters the stick from the side. Pad 2 brings Y around the lower left of the
right Joy-Con and enters it horizontally. Pad 3 brings Y down from above and
gives the printed A and B separate routes from the right.
