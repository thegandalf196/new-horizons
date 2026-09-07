#!/usr/bin/env python3
"""Independent convenience-art structural checks; not GUI/quality acceptance.
SPDX-License-Identifier: CC0-1.0
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import Image

TRAITS = ('undead', 'flying', 'shooter', 'siege', 'noRetaliation',
          'unlimitedRetaliations', 'breath', 'adjacent', 'resistance', 'regeneration')
STATES = ('normal', 'pressed', 'disabled', 'highlighted')

def check_image(image, size, button=False):
    assert image.mode == 'RGBA', 'RGBA required'
    assert image.size == (size, size), 'wrong dimensions'
    alpha = image.getchannel('A')
    low, high = alpha.getextrema()
    assert high == 255 and low < 255, 'blank/translucent-only/opaque output'
    pixels = image.load()
    assert len({pixels[x,y][:3] for x in range(size) for y in range(size) if pixels[x,y][3] > 128}) > 12, 'blank or flat output'
    # Buttons intentionally reach the edge. Trait illustrations need clear space;
    # ignore only near-invisible Lanczos fringes below 32/255 alpha.
    if not button:
        edge = [alpha.getpixel((x, y)) for x in range(size) for y in (0, size-1)]
        edge += [alpha.getpixel((x, y)) for y in range(size) for x in (0, size-1)]
        assert max(edge) < 32, 'visible trait edge contact'

def check_animation(data, name):
    assert data == {'images': [{'group': 0, 'frame': index,
                    'file': f'NH_{name}_24_{state}.png'}
                    for index, state in enumerate(STATES)]}, 'wrong frame order/references'

def check_svg(text, size):
    root = ET.fromstring(text)
    assert root.attrib['width'] == str(size) and root.attrib['height'] == str(size)
    assert root.attrib['viewBox'] == '0 0 64 64'
    assert 'CC0-1.0' in text
    assert all(node.tag.split('}')[-1] in ('svg', 'polygon', 'polyline', 'circle') for node in root.iter())
    assert not any(word in text for word in ('href', '<!ENTITY', '<!DOCTYPE', '<image', '<text', 'url(')), 'external/non-geometric payload'

def rejects(operation):
    try:
        operation()
    except (AssertionError, ET.ParseError):
        return
    raise AssertionError('negative control accepted')

def audit(root, baseline):
    hashes = {}
    images = root / 'Mods/new-horizons/Images'
    sources = root / 'assets/new-horizons/svg'
    stems = [(f'NH_status_{trait}_50', 50, False) for trait in TRAITS]
    stems += [(f'NH_{name}_24_{state}', 24, True) for name in ('qsave', 'qload') for state in STATES]
    for stem, size, button in stems:
        image = Image.open(images / (stem + '.png'))
        check_image(image, size, button)
        check_svg((sources / (stem + '.svg')).read_text(), size)
        for p in (images / (stem + '.png'), sources / (stem + '.svg')):
            hashes[str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()
    assert len({hashes[str((images/(stem+'.png')).relative_to(root))] for stem, _, _ in stems}) == 18, 'duplicate imagery/state'
    for name in ('qsave', 'qload'):
        p = images / f'NH_{name}_24.json'
        check_animation(json.loads(p.read_text()), name)
        hashes[str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()
    for p, expected in baseline.items():
        assert p not in hashes, 'name collides with prior artwork'
        assert hashlib.sha256((root/p).read_bytes()).hexdigest() == expected, f'prior file changed: {p}'
    valid = Image.open(images/'NH_status_undead_50.png').copy()
    rejects(lambda: check_image(Image.new('RGBA', (50,50)), 50))
    rejects(lambda: check_image(valid.resize((49,50)), 50))
    opaque = valid.copy(); opaque.putalpha(255)
    rejects(lambda: check_image(opaque, 50))
    edge = valid.copy(); edge.putpixel((0,25), (255,255,255,255))
    rejects(lambda: check_image(edge, 50))
    animation = json.loads((images/'NH_qsave_24.json').read_text())
    animation['images'][0]['file'], animation['images'][2]['file'] = animation['images'][2]['file'], animation['images'][0]['file']
    rejects(lambda: check_animation(animation, 'qsave'))
    svg = (sources/'NH_status_undead_50.svg').read_text()
    rejects(lambda: check_svg(svg.replace('</svg>', '<image href="external.png"/></svg>'), 50))
    return {'scope': 'Offline structure and negative controls only; not compiled hook, GUI, quality or Windows acceptance',
            'outputs': hashes, 'prior_unchanged': len(baseline), 'negative_controls_rejected': 6}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.root.resolve(), json.loads(args.baseline.read_text()))
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS:38 outputs, prior hashes unchanged, six negative controls rejected')

if __name__ == '__main__':
    main()
