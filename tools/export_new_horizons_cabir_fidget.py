#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export a private 2x2 fidget review with one scale and planted-foot alignment.

All source pixels and alpha are retained. Alpha>=128 identifies alignment
landmarks only; it does not mask, clean, repaint or normalize individual frames.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bounds(image, threshold=1):
    return image.getchannel('A').point(lambda a: 255 if a >= threshold else 0).getbbox()


def pivot(image):
    box = bounds(image, 128)
    if box is None:
        raise ValueError('No meaningful alpha>=128 body')
    alpha = image.getchannel('A').load()
    xs = [x for y in range(max(box[1], box[3] - 5), box[3])
          for x in range(box[0], box[2]) if alpha[x, y] >= 128]
    return ((min(xs) + max(xs) + 1) / 2, box[3]), box


def export(source, reference, output):
    if output.exists():
        raise FileExistsError('Output must be a new private directory')
    source_hash, reference_hash = sha(source), sha(reference)
    atlas, ref = Image.open(source).convert('RGBA'), Image.open(reference).convert('RGBA')
    if atlas.width % 2 or atlas.height % 2:
        raise ValueError('Expected evenly divisible 2x2 sheet')
    cells = [atlas.crop((i % 2 * atlas.width // 2, i // 2 * atlas.height // 2,
                         (i % 2 + 1) * atlas.width // 2, (i // 2 + 1) * atlas.height // 2))
             for i in range(4)]
    landmarks = [pivot(cell) for cell in cells]
    scale = 58 / max(box[3] - box[1] for _, box in landmarks)
    target, _ = pivot(ref)
    frames, records = [], []
    for index, (cell, (contact, core)) in enumerate(zip(cells, landmarks)):
        size = tuple(round(n * scale) for n in cell.size)
        xy = tuple(round(target[d] - contact[d] * scale) for d in range(2))
        if min(xy) < 0 or any(xy[d] + size[d] > ref.size[d] for d in range(2)):
            raise ValueError('Whole-cell export would clip source alpha')
        frame = Image.new('RGBA', ref.size)
        frame.alpha_composite(cell.resize(size, Image.Resampling.LANCZOS), xy)
        frames.append(frame)
        raw = bounds(cell)
        records.append({'index': index, 'sourceCell': [index % 2, index // 2],
                        'sourceRawAlphaBounds': raw, 'sourceAlpha128Bounds': core,
                        'sourceFootPivot': contact, 'placement': xy,
                        'outputRawAlphaBounds': bounds(frame),
                        'outputAlpha128Bounds': bounds(frame, 128),
                        'actualPivot': [xy[d] + contact[d] * scale for d in range(2)],
                        'rawAlphaTouchesCellEdge': raw[0] == 0 or raw[1] == 0 or
                        raw[2] == cell.width or raw[3] == cell.height})
    output.mkdir(parents=True)
    for i, frame in enumerate(frames):
        frame.save(output / f'frame-{i:02}.png')
    # Fixed common view includes all retained alpha, plus the reference.
    boxes = [bounds(f) for f in frames + [ref]]
    crop = (max(0, min(b[0] for b in boxes) - 5), max(0, min(b[1] for b in boxes) - 5),
            min(ref.width, max(b[2] for b in boxes) + 5), min(ref.height, max(b[3] for b in boxes) + 5))
    w, h = crop[2] - crop[0], crop[3] - crop[1]
    matte = (62, 55, 52, 255)
    sheet = Image.new('RGBA', (w * 5, h + 20), matte)
    draw = ImageDraw.Draw(sheet)
    for i, frame in enumerate([ref] + frames):
        sheet.alpha_composite(frame.crop(crop), (i * w, 20))
        draw.text((i * w + 3, 3), 'reference' if i == 0 else f'fidget {i-1}', fill='white')
    sheet.save(output / 'comparison-native.png')
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.Resampling.NEAREST).save(output / 'comparison-4x.png')
    gif = []
    for frame in frames:
        tile = Image.new('RGBA', (w, h), matte)
        tile.alpha_composite(frame.crop(crop))
        gif.append(tile.convert('RGB'))
    gif[0].save(output / 'preview-native.gif', save_all=True, append_images=gif[1:],
                duration=160, loop=0, disposal=2, optimize=False)
    enlarged = [frame.resize((w * 4, h * 4), Image.Resampling.NEAREST) for frame in gif]
    enlarged[0].save(output / 'preview-4x.gif', save_all=True, append_images=enlarged[1:],
                      duration=160, loop=0, disposal=2, optimize=False)
    manifest = {'status': 'Private review only; not installed or accepted',
                'source': str(source), 'sourceSha256': source_hash,
                'reference': str(reference), 'referenceSha256': reference_hash,
                'sourceSize': atlas.size, 'cellSize': cells[0].size,
                'sharedScale': scale, 'maximumMeaningfulBodyHeight': 58,
                'targetFootPivot': target, 'canvas': ref.size, 'reviewCrop': crop,
                'alphaEdited': False, 'frameHeightNormalization': False, 'frames': records}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if sha(source) != source_hash or sha(reference) != reference_hash:
        raise RuntimeError('Input bytes changed during export')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    export(args.input, args.reference, args.output_dir)
