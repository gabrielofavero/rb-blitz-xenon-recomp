"""Generate local, original-art P2 fixtures under out/button-schemes/p2/.

Requires Pillow. The output is gitignored and contains no game pixels.
"""
from pathlib import Path
import argparse
import tomllib

from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path,
                        default=Path("out/button-schemes/p2/fixtures"))
    args = parser.parse_args()
    slots = tomllib.loads(Path("assets/controller/slots.toml").read_text())['slot']
    package = args.output / 'test-markers'
    (package / 'buttons').mkdir(parents=True, exist_ok=True)
    (package / 'pads').mkdir(parents=True, exist_ok=True)
    manifest = ['schema_version = 1', 'id = "test-markers"',
                'name = "Test Markers"', 'kind = "controller"', '', '[buttons]']
    for index, slot in enumerate(slots):
        name = slot['id']
        width, height = (64, 50) if index == 0 else (96, 96)
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        color = ((index * 67 + 45) % 256, (index * 131 + 70) % 256,
                 (index * 193 + 100) % 256, 255)
        draw.rectangle((3, 3, width - 4, height - 4), fill=color)
        draw.rectangle((width // 3, height // 3, width * 2 // 3,
                        height * 2 // 3), fill=(0, 0, 0, 0))
        image.save(package / 'buttons' / f'{name}.png')
        manifest.append(f'{name} = "buttons/{name}.png"')
    manifest += ['', '[controller.slots]']
    for index, name in enumerate(('xbox_0', 'xbox_1', 'xbox_2', 'xbox_3',
                                  'ps_0', 'ps_1', 'ps_2', 'ps_3')):
        image = Image.new('RGBA', (128, 96), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rectangle((8, 8, 119, 87), fill=((index * 47 + 25) % 256,
                                                   (index * 83 + 35) % 256,
                                                   (index * 119 + 45) % 256, 255))
        image.save(package / 'pads' / f'{name}.png')
        manifest.append(f'{name} = "pads/{name}.png"')
    (package / 'scheme.toml').write_text('\n'.join(manifest) + '\n')
    one_glyph = args.output / 'one-glyph'
    one_glyph.mkdir(parents=True, exist_ok=True)
    glyph_image = Image.new('RGBA', (64, 50), (0, 0, 0, 0))
    glyph_draw = ImageDraw.Draw(glyph_image)
    glyph_draw.rectangle((4, 4, 59, 45), fill=(255, 0, 220, 255))
    glyph_draw.rectangle((20, 15, 43, 34), fill=(0, 0, 0, 0))
    glyph_image.save(one_glyph / 'a.png')
    (one_glyph / 'scheme.toml').write_text(
        'schema_version = 1\nid = "one-glyph"\nname = "One Glyph"\n'
        'kind = "controller"\n[buttons]\nconfirm_a = "a.png"\n')
    one_pad = args.output / 'one-pad'
    one_pad.mkdir(parents=True, exist_ok=True)
    Image.new('RGBA', (128, 96), (255, 0, 220, 255)).save(one_pad / 'pad.png')
    (one_pad / 'scheme.toml').write_text(
        'schema_version = 1\nid = "one-pad"\nname = "One Pad"\n'
        'kind = "controller"\n[controller.slots]\nxbox_1 = "pad.png"\n')
    broken = args.output / 'invalid-missing'
    broken.mkdir(parents=True, exist_ok=True)
    (broken / 'scheme.toml').write_text(
        'schema_version = 1\nid = "invalid-missing"\nname = "Invalid"\n'
        'kind = "controller"\n[buttons]\nconfirm_a = "missing.png"\n')
    print(f'Generated {package}, {one_glyph}, {one_pad}, and {broken}')


if __name__ == '__main__':
    main()
