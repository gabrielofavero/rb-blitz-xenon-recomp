"""Reduce a P3 catalogue to portable recipes and P2 source locators, never shader bytes.

Run against the owner's installed assets. The resulting JSON is embedded at build
time; extraction at runtime still reads and hashes that installation's own bytes.
"""
import argparse
import json
from pathlib import Path
import shader_asset_resolver as resolver
import shader_vertex_link
from shader_xex_source import load_supported_image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalogue', type=Path, required=True)
    p.add_argument('--game-root', type=Path, required=True)
    p.add_argument('--helper', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    catalogue = json.loads(a.catalogue.read_text())
    assets, _ = resolver.installed_assets(a.game_root)
    hash_codes = lambda codes: resolver.sdk_hashes(codes, a.helper)
    local = resolver.Resolver(assets, hash_codes)
    image, source = load_supported_image(a.game_root, root)
    layouts = json.loads((root / 'config/shader_vertex_layouts.json').read_text())
    local.add_vertex_sources(image, layouts, source)
    programs = {(asset, p.offset): p for asset, p in local.programs}
    paths, identities = {}, set()
    for backend, info in catalogue['backends'].items():
        for path, info in info['paths'].items():
            if not info['captured']:
                raise ValueError('uncaptured rendering path')
            rows = info['recipes']
            # Preserve only normalized renderer state, excluding provenance paths.
            paths[backend + '/' + path] = [
                {'hash': r['description_hash'], 'raw': r['raw']} for r in rows]
            for row in rows:
                for stage in ('vertex', 'pixel'):
                    shader = row.get(stage)
                    if shader and int(shader['hash'], 16):
                        identities.add((stage, shader['hash']))
    index = {(r['stage'], r['hash']): r for r in local.index()['resolved']}
    sources = []
    for stage, identity in sorted(identities):
        row = index[stage, identity]['sources'][0].copy()
        row.update(stage=stage, hash=identity)
        if 'vertex_layout' in row:
            row['fetches'] = programs[row['asset'], row['offset']].fetches
        sources.append(row)
    output = {'schema': 1, 'title_id': '5841122D', 'layouts': layouts,
              'assets': {name: {'bytes': len(data)}
                         for name, (data, _) in assets.items()},
              'sources': sources, 'paths': paths}
    # No developer paths or preserved shader/native blobs travel with this file.
    with a.output.open('w', encoding='utf-8', newline='') as stream:
        stream.write(json.dumps(output, indent=2).replace('\n', '\r\n') + '\r\n')


if __name__ == '__main__':
    main()
