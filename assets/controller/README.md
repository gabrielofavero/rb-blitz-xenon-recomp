# Create a controller layout

A layout is a folder containing `scheme.toml` and at least one original button or
controller image. The game reads layouts at startup. You can change between installed
layouts in **Help & Options → Controls → Layout** without restarting.
This guide, the blank templates, and the original examples are included under
`assets/controller/` beside an installed `rb_blitz.exe`.

## Make a one-button layout

1. In an RGBA image editor, draw a button on a transparent canvas. Save it as an
   **8-bit RGBA PNG** named `buttons/a.png`. Use straight (unpremultiplied) alpha
   and sRGB colors. A 128×128 canvas is fine; the image need not match the game's
   slot size. White or light gray shapes work best because the game may tint the
   button glyphs. Check the result against the game's light and dark backgrounds.
2. Create a folder whose name is a unique, lower-case layout ID, such as
   `my-layout`. Put this `scheme.toml` in it:

   ```toml
   schema_version = 1
   id = "my-layout"
   name = "My Layout"
   kind = "controller"

   [buttons]
   confirm_a = "buttons/a.png"
   ```

3. Find the save-data folder chosen during installation. Its path is recorded as
   `user_data_root` in the `[paths]` section of `paths.toml` beside the installed
   `rb_blitz.exe`. If no path was set, the default is the Windows
   **Documents\Rock Band Blitz** folder (which may be redirected). An explicit
   `--user_data_root` launch option also changes this location. Copy the whole
   `my-layout` folder into `<user_data_root>\controller_schemes\`. The result is
   `<user_data_root>\controller_schemes\my-layout\scheme.toml` with
   `buttons\a.png` beside it in its `buttons` subfolder.
4. Start or restart the game. It validates the folder at boot and adds **My
   Layout** to **Help & Options → Controls → Layout** if valid. Selecting it
   applies immediately. A new folder copied in while the game is running is
   discovered on the next boot. An invalid folder is skipped with a reason in
   the game log.

Your packages live in the save-data folder, outside the game installation.
Reinstalling or updating the game does not require copying them again as long
as it uses the same `user_data_root`.

## Optional validation from a source checkout

The installed game performs validation at boot. If you have a source checkout,
you can also run the host validator before copying a package:

```powershell
cmake --build --preset win-amd64-release --target rb_blitz_controller_schemes
.\scripts\controller_schemes.ps1 -Validate -Scheme .\my-layout
```

Pass an existing folder path to `-Scheme`. The validator reports source and
fitted sizes for each declared image.

The tracked [one-button example](examples/example-original/scheme.toml) includes
an original, procedurally drawn `buttons/a.png`. You can validate it with:

```powershell
.\scripts\controller_schemes.ps1 -Validate -Scheme .\assets\controller\examples\example-original
```

These examples are included under `assets/controller/examples/` in an installed
build. Copy an example into your `controller_schemes` folder and rename its
folder, `id`, and `name` before using it as the start of your own layout. Draw
your own art; do not copy pixels from the local game-art reference exports.
The separate [one-pad example](examples/example-pad/scheme.toml) replaces only
`xbox_0`; the other seven pad textures retain their base-game art.
Its source is a non-square 256×192 PNG. Check its fit result with:

```powershell
.\scripts\controller_schemes.ps1 -Validate -Scheme .\assets\controller\examples\example-pad
```

## Add or omit images

Every image is optional except that a package needs at least one button or pad
replacement. A path is used only when its slot ID appears in `scheme.toml`:

```toml
[buttons]
confirm_a = "buttons/a.png"
back_b = "buttons/b.png"

[controller.slots]
xbox_0 = "pads/xbox_0.png"
```

This replaces two button glyphs and one of eight controller diagram textures.
The remaining button slots and seven pad textures keep the corresponding art
from the player's own base game. A transparent pixel in a **declared** button
slot clears the old pixel in that slot; omission is how to preserve the old art.
The pad IDs are `xbox_0` through `xbox_3` and `ps_0` through `ps_3`. Each pad
has a 1024×1024 destination. The pad [placement guide](template/pad.png) marks
a central safe area, but is a guide only and must not be shipped as finished art.

For one image on **all eight** pads, add:

```toml
[controller]
file = "pads/shared.png"
```

An entry under `[controller.slots]` takes precedence over the shared image for
that exact pad. You may also supply a complete 1024×512 extra-icon sheet with
`[icons]` and `file = "icons.png"`; it does not replace individual button slots
and cannot be the package's only image. The extra sheet has no individual-slot
contract yet. Keyboard and mouse images are built into the application and
cannot be packaged or selected as a controller layout.

## Canvas and slot sizes

The [blank button grid](template/buttons.png) is a placement reference with no
game art. Save **separate** images for the slots you want to change; do not put
the full grid PNG into your package. The exact rectangles, including position,
are in [slots.toml](slots.toml). These are the measured destination sizes:

| Slot IDs | Destination size |
| --- | --- |
| `confirm_a` | 81×81 |
| `back_b`, `x`, `y` | 80×80 |
| `glyph_0067`, `glyph_0063` | 21×21 |
| `left_trigger` | 63×63 |
| `right_trigger` | 64×64 |
| `left_bumper`, `right_bumper` | 82×82 |
| `next_arrow`, `previous_arrow` | 83×83 |
| `dpad`, `glyph_0031`–`glyph_0035`, `dpad_up`, `dpad_right`, `dpad_down`, `dpad_left`, `left_stick`, `right_stick`, `glyph_0036`–`glyph_0039`, `glyph_005e`, `glyph_0026`, `glyph_0028`, `glyph_002a` | 81×81 |

Names beginning `glyph_` are stable character-based IDs where the action is
not yet identified. The font sheet has no standalone Start or Back picture.

The loader scales the **whole source canvas**, including transparent margins,
uniformly by `min(destination width/source width, destination height/source
height)`, rounds each resulting dimension to fit, and centers it on a
transparent destination canvas. It does not crop or stretch. For a 32×32
destination, 128×128 becomes 32×32; 64×50 becomes 32×25 with transparent space
above and below. `confirm_a` is actually 81×81, so a 128×128 square fits to
81×81. Trim accidental transparent margins from the source if the visible art
looks too small. Pads use the same rule against 1024×1024.

Use 1–2048 pixels on each source axis and at most 8 MB per PNG. A layout may
contain at most 64 files, including its manifest. Paths must be relative `.png`
paths inside the layout folder, with no `..`, absolute path, symlink, duplicate
path, missing file, or unknown slot ID. The manifest must be under 64 KiB.
`schema_version` is the integer `1`, `kind` is `"controller"`, the ID matches
its folder and follows `[a-z0-9][a-z0-9-]{0,31}`, and the displayed name is
1–20 UTF-8 characters. Only `retail` (base-game art), `keyboard` (built-in
input images), and `ps3` (installer import) are reserved. Any other valid ID
can be used for a user layout, including an ID used by an installed authored
layout. In that case, the user's package takes precedence at the next boot.
Keep only declared files in the folder. The validator reports extras; they are
not used by the game.

## Local references and packaging

If you own local game extracts, `python scripts/export_template_360.py` and
`python scripts/export_template_ps3.py` export separate glyph and pad PNGs into
gitignored `assets/controller/template-360/` and `template-ps3/` folders. They
are private references, not authoring assets or distributable packages. The
blank guides and `slots.toml` are enough to create original art without them.

Any owner-authored layout with a non-reserved ID placed in
`assets/controller/schemes/<id>/` is
validated and included by the installer build, without registering its ID in
code. The example packages live under `assets/controller/examples/` so they
are not installed. Check and copy all authored layouts to the build tree with:

```powershell
.\scripts\controller_schemes.ps1 -Validate -Pack
```

Only original art or art you have permission to redistribute belongs in this
tracked directory or a release. Do not commit decoded game images, filled
templates copied from them, or composites containing game pixels. The base
game supplies omitted slots at runtime; a layout package never needs them.
