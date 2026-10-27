"""Reproduce P2's guest BC3 fingerprints from the owner's local 360 scenes.

Reads scene bytes in memory; writes no game art. The tiled FNV values must
match src/assets/texture_override.cpp and P1's guest trace.
"""
from pathlib import Path
import sys

import hmx_milo


EXPECTED = {
    '../image/icons_buttons_xbox_nomip.bmp': 0x3F044908A62A89AD,
    '../image/blitz_icons_xbox_nomip.bmp': 0x1163F9F7F2FFF53D,
    'img/xbox_0.png': 0x1607043F166B232D,
    'img/xbox_1.png': 0x06FFB32889EA3A64,
    'img/xbox_2.png': 0x54855E715F650967,
    'img/xbox_3.png': 0x66AE0BAD190B484D,
    'img/ps_0.png': 0x4747E8979C00EEFB,
    'img/ps_1.png': 0xC6948849BBEEB180,
    'img/ps_2.png': 0xF2158119E803554E,
    'img/ps_3.png': 0x86AAF3CCA6CE2365,
}


def tiled_offset(x, y, pitch):
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << 11
    micro = ((x & 7) + ((y & 14) << 2)) << 4
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4)
    return (((offset & ~511) << 3) + ((y & 16) << 7) +
            ((offset & 448) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +
            (offset & 63))


def guest_hash(linear, width, height):
    pitch = width // 4
    tiled = bytearray(len(linear))
    for y in range(height // 4):
        for x in range(pitch):
            source = (y * pitch + x) * 16
            target = tiled_offset(x, y, pitch)
            tiled[target:target + 16] = linear[source:source + 16]
    digest = 14695981039346656037
    for byte in tiled:
        digest = ((digest ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return digest


def main():
    base = Path(__file__).resolve().parents[1] / 'assets/game/360/ui'
    scenes = [base / 'resource/fonts/gen/buttons.milo_xbox',
              base / 'resource/fonts/gen/blitz_icons.milo_xbox',
              base / 'controller_config/gen/controller_config.milo_xbox']
    seen = set()
    for scene in scenes:
        milo = hmx_milo.load(scene)
        stream = milo.stream()
        for texture in milo.scene_textures():
            name = texture['path']
            if name not in EXPECTED:
                continue
            size = texture['payload_size']
            linear = stream[texture['payload_at']:texture['payload_at'] + size]
            actual = guest_hash(linear, texture['width'], texture['height'])
            print(f'{name}: {actual:016X}')
            if actual != EXPECTED[name]:
                print(f'expected {EXPECTED[name]:016X}', file=sys.stderr)
                return 1
            seen.add(name)
    missing = set(EXPECTED) - seen
    if missing:
        print(f'missing texture(s): {sorted(missing)}', file=sys.stderr)
        return 1
    print('button, extra icons, and eight pad fingerprints match')
    return 0


if __name__ == '__main__':
    sys.exit(main())
