#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export bounded subtitle patches from image-tool masters, not original backgrounds.

Reference images and full edited masters remain private. This performs only
deterministic raster export/composition; lettering and backdrop repairs must
already exist in the supplied generated masters.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
VARIANTS = (
    ('complete_main', 'H3bitmap', 'GAMSELBK', (8, 120, 432, 65)),
    ('complete_scenario0', 'H3bitmap', 'GAMSELB0', (10, 92, 385, 57)),
    ('complete_scenario1', 'H3bitmap', 'GAMSELB1', (10, 92, 385, 57)),
    ('complete_loading', 'H3bitmap', 'LOADBAR', (140, 116, 466, 70)),
    ('ab_main', 'H3ab_bmp', 'GAMSELBK', (12, 106, 458, 46)),
    ('ab_scenario0', 'H3ab_bmp', 'GAMSELB0', (18, 82, 370, 40)),
    ('ab_scenario1', 'H3ab_bmp', 'GAMSELB1', (18, 82, 370, 40)),
    ('ab_loading', 'H3ab_bmp', 'LOADBAR', (10, 112, 460, 47)),
)


def export_patch(master, bounds):
    x, y, width, height = bounds
    resized = master.convert('RGB').resize((800, 600), Image.Resampling.LANCZOS)
    patch = resized.crop((x, y, x + width, y + height)).convert('RGBA')
    # A narrow export feather joins the generated repair to the purchaser's
    # untouched background. It does not alter the lettering or paint new art.
    alpha = Image.new('L', patch.size)
    alpha.putdata([min(255, min(px, py, width - 1 - px, height - 1 - py) * 85)
                   for py in range(height) for px in range(width)])
    patch.putalpha(alpha)
    return patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--masters-dir', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--references-dir', type=Path, required=True)
    parser.add_argument('--previews-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--mapping-output', type=Path, required=True)
    parser.add_argument('--provenance-output', type=Path, required=True)
    args = parser.parse_args()
    # Reuse the bounded read-only archive reader; no archive is changed.
    spec = importlib.util.spec_from_file_location('archive_reader', ROOT / 'tools/review-new-horizons-hero-biographies.py')
    archive_reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(archive_reader)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.previews_dir.mkdir(parents=True, exist_ok=True)
    replacements, provenance = [], []
    for variant, archive, resource, bounds in VARIANTS:
        master_path = args.masters_dir / f'{archive}-{resource}.png'
        payload = archive_reader.read_lod_entry(args.data_dir / f'{archive}.lod', resource + '.PCX')
        if payload is None:
            raise ValueError(f'Missing reference payload: {archive}/{resource}')
        with Image.open(master_path) as master:
            if abs(master.width / master.height - 4 / 3) > 0.01:
                raise ValueError(f'Unexpected master aspect ratio: {master_path.name}')
            original_size = list(master.size)
            patch = export_patch(master, bounds)
        image_name = 'NH_menu_' + variant
        output_path = args.output_dir / (image_name + '.png')
        patch.save(output_path, interlace=False)
        x, y, width, height = bounds
        replacements.append({'resource': resource, 'crc32': zlib.crc32(payload),
                             'image': image_name, 'x': x, 'y': y,
                             'width': width, 'height': height})
        provenance.append({'variant': variant, 'reference_archive': archive + '.lod',
                           'reference_resource': resource + '.PCX',
                           'reference_sha256': hashlib.sha256(payload).hexdigest(),
                           'generated_master': master_path.name, 'master_size': original_size,
                           'master_sha256': hashlib.sha256(master_path.read_bytes()).hexdigest(),
                           'patch_sha256': hashlib.sha256(output_path.read_bytes()).hexdigest(),
                           'bounds': list(bounds), 'approval': 'Provisional'})
        with Image.open(args.references_dir / f'{archive}-{resource}.png') as reference:
            preview = reference.convert('RGBA')
            preview.alpha_composite(patch, (x, y))
            preview.convert('RGB').save(args.previews_dir / (variant + '.png'))
        print(variant, patch.size)
    args.mapping_output.write_text(json.dumps({'replacements': replacements}, indent='\t') + '\n', encoding='utf-8')
    args.provenance_output.parent.mkdir(parents=True, exist_ok=True)
    args.provenance_output.write_text(json.dumps({'workflow': 'HoMM3 art skill; built-in image tool; reference-based text edit',
        'rights': 'Reference-based title patches. Original game artwork and trademarks retain their original rights. No rights-cleared or CC0 claim.',
        'full_masters': 'Retained privately under ignored output/homm3/menu-titles-v1/masters; not distributed.',
        'variants': provenance}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
