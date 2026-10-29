# Controller and keyboard vector masters

`Vector Source.svg` is the source sheet. Run
`python scripts/export_controller_vector_masters.py` from the repository root to
split its platform groups into individual SVG files. The script preserves each
source object's path IDs and copies referenced gradients, clip paths, and
embedded font definitions. Filenames identify controls where the artwork makes
that clear; numbered `trackpad_variant_*` files keep additional Steam Controller
drawings without guessing their final slot meaning.

To regenerate just the dark keyboard assets, run
`python scripts/export_controller_vector_masters.py --preset keyboard-dark`.

The eight preset folders contain vector source art only. `keyboard-dark` contains
all 85 objects from the source's dark keyboard layer, including mouse drawings,
special keys, digits, and letters. The light keyboard layer is not exported.
The runtime controller
scheme format in [`../README.md`](../README.md) accepts PNGs and a `scheme.toml`;
rasterizing and packaging these masters is a later conversion step. Nintendo
Switch has Joy-Con diagrams rather than a full controller diagram in the source.
`meta-quest` includes both Oculus Touch and Oculus Remote source objects.
Keyboard and mouse art currently has no runtime scheme package format.

The six audited controller folders keep direct slot art as `<id>.svg` and
candidate copies as `<id>_adapt.svg`. The latter still need any art edits listed
in each folder's `AUDIT.md`; source drawings not used as direct slot files are
in `unused/`. For these
layouts, `next_arrow` represents Start and `previous_arrow` represents Select.
The exporter recognizes this structure when regenerating those six folders.

## Credits and licence

The art is not ours. `Vector Source.svg` is the
[haaldor/Xelu_prompts_SVG](https://github.com/haaldor/Xelu_prompts_SVG) edit of
Xelu's *Free Prompts Pack*: Haaldor extracted and arranged the vector graphics,
and the prompts themselves are by Nicolae (Xelu) Berbece, Those Awesome Guys.
The original pack is published under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) and the SVG edit
carries the same licence (both checked 2026-10-28), so redistribution and
commercial use are allowed without attribution; the credit is given anyway.

That licence does not cover everything in the file. The Steam Deck prompts are
set in the **COCOGOOSE** font, which Haaldor's README describes as free for
personal use only. The export copies the source's embedded font definitions into
every master that still contains text, so before redistributing a Steam Deck
master, or art derived from it, replace that font with one the project may ship.
