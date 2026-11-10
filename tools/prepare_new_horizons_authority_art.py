#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare an external additive bundle from the selected owner-supplied sheet.

No image generation, chroma key, alpha thresholding, repository installation or
Final approval. Full verified baseline inputs are recovered before NHART packing.
"""
import argparse
from collections import deque
import hashlib
import io
import json
from pathlib import Path
import shutil

from PIL import Image, ImageDraw, ImageEnhance

import nhart

SHEET_SHA256 = '4e8df0ef4074697155d01484e6752de4b4c2009e26be4cde3e6a6461fd374bc8'
IDENTITIES = (
    ('authority', 'Authority'),
    ('offensiveDrills', 'Offensive Drills'),
    ('defensiveDrills', 'Defensive Drills'),
    ('conditioning', 'Conditioning'),
    ('battleReadiness', 'Battle Readiness'),
    ('officerCorps', 'Officer Corps'),
    ('maneuverTraining', 'Maneuver Training'),
    ('counterattackTraining', 'Counterattack Training'),
    ('protectiveTraining', 'Protective Training'),
    ('veteranCorps', 'Veteran Corps'),
    ('grandMarshal', 'Grand Marshal'),
)
STEMS = ('authority', 'offensive_drills', 'defensive_drills', 'conditioning',
         'battle_readiness', 'officer_corps', 'maneuver_training',
         'counterattack_training', 'protective_training', 'veteran_corps', 'grand_marshal')
# Reviewed subject envelopes include twelve source pixels beyond the substantial
# silhouette. The ownership mask, not these overlapping envelopes, separates art.
# Distant disconnected alpha specks outside them remain in the private masters.
SUBJECT_ENVELOPES = (
    (10, 2, 381, 369), (381, 27, 719, 362), (739, 0, 1063, 370), (1082, 11, 1448, 369),
    (0, 356, 368, 719), (368, 357, 717, 719), (719, 358, 1070, 718), (1072, 372, 1445, 724),
    (0, 718, 369, 1080), (354, 711, 718, 1079), (717, 719, 1087, 1086),
)
RANKS = ('basic', 'advanced', 'expert')
STATES = ('normal', 'pressed', 'disabled', 'highlighted')
SLOTS = {
    'small': ('secondarySkillSmallBlank.png', (32, 32), (2, 2, 30, 26), (8, 27, 24, 32)),
    'medium': ('secondarySkillBlank.png', (44, 44), (3, 3, 41, 36), (12, 38, 32, 44)),
    'large': ('secondarySkillLargeBlank.png', (82, 93), (5, 5, 77, 80), (24, 81, 58, 93)),
    'scenarioBonus': ('bonusBlank.png', (58, 64), (3, 3, 55, 54), None),
}
TEMPLATE_HASHES = {
    'secondarySkillSmallBlank.png': 'eed48ae89b037457aa783baa5e9dd3ed820a95b6a7a991a7840ce34c3d49d197',
    'secondarySkillBlank.png': '77203d8fcc0258dd0919ded950414c31cd34f04d78ade1bf15a5d06f67644333',
    'secondarySkillLargeBlank.png': '4fe1e46fbfae83eb327ecfab0e9c33c623b439ef0d862a9a1c60031f567d114b',
    'bonusBlank.png': '12b3f4bb28b71fd0a72eecbf1bf4d3fd3388371aa15c95b4bc04fa4249eb0ce5',
}
APPROVAL = 'Provisional; no Final approval; in-game rendered acceptance pending'
SELECTION = 'owner-selected ChatGPT-generated Authority sheet for New Horizons distribution; visual integration authorized 2026-11-09'
ORIGIN = 'owner-supplied RGBA sheet; owner-confirmed ChatGPT-generated; technical export, not generation by this agent'
RIGHTS = ('Owner confirms the supplied sheet was generated with ChatGPT and selects it for New Horizons distribution. '
          'No new ownership, CC0, generation by this agent or Final visual approval claimed. '
          'Native templates retain upstream Various / CC BY-SA 4.0 declaration and mixed Heroes III provenance; '
          'no blanket underlying-pixel clearance asserted. Third-party rights and license obligations are not waived.')
FAMILY = 'builtin-skills-perks-orders-and-ui'
ARCHIVE = 'Mods/new-horizons/NewHorizons.nhart'
MANIFEST = 'assets/new-horizons/runtime-art-manifest.json'
LEDGER = 'assets/new-horizons/runtime-art-additions.json'
HEADER = 'client/windows/NewHorizonsPerkIcons.h'
RECORD = 'assets/new-horizons/authority-art-selections.json'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical_digest(value):
    return digest(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def binding_block():
    return ''.join('\t\t{"new-horizons:authority.' + identity + '", "NH_perk_authority_' + stem + '"},\n'
                   for (identity, _), stem in zip(IDENTITIES[1:], STEMS[1:]))


def split_sheet(sheet):
    """Partition alpha support by connected subjects, preserving every alpha value.

    Alpha32 is used ONLY to find stable ownership seeds, never to cut out art.
    Multi-source distance propagation assigns the faint edges and detached pixels
    to nonrectangular, disjoint regions. No original nonzero-alpha pixel is lost.
    """
    width, height = sheet.size
    alpha = sheet.getchannel('A').tobytes()
    active = bytearray(value >= 32 for value in alpha)
    owners = bytearray(len(alpha))
    seeds = deque()
    components = []
    for start in range(len(active)):
        if not active[start]:
            continue
        pending = deque([start])
        active[start] = 0
        points = []
        while pending:
            index = pending.popleft()
            points.append(index)
            x, y = index % width, index // width
            for yy in range(max(0, y - 1), min(height, y + 2)):
                for xx in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = yy * width + xx
                    if active[neighbor]:
                        active[neighbor] = 0
                        pending.append(neighbor)
        if len(points) < 100:
            continue
        cx = sum(index % width for index in points) / len(points)
        cy = sum(index // width for index in points) / len(points)
        owner = min(range(11), key=lambda i: (cx - ((i % 4) + .5) * width / 4) ** 2
                    + (cy - ((i // 4) + .5) * height / 3) ** 2) + 1
        components.append({'owner': owner, 'pixels': len(points)})
        for index in points:
            owners[index] = owner
            seeds.append(index)
    if set(owners) != set(range(12)):
        raise ValueError('Expected eleven distinct subject seed sets')
    while seeds:
        index = seeds.popleft()
        x, y = index % width, index // width
        neighbors = []
        if x:
            neighbors.append(index - 1)
        if x + 1 < width:
            neighbors.append(index + 1)
        if y:
            neighbors.append(index - width)
        if y + 1 < height:
            neighbors.append(index + width)
        for neighbor in neighbors:
            if not owners[neighbor]:
                owners[neighbor] = owners[index]
                seeds.append(neighbor)
    foregrounds, records, full_partitions = [], [], []
    total_mass = 0
    for owner, (identity, name) in enumerate(IDENTITIES, 1):
        retained = bytes(value if label == owner else 0 for value, label in zip(alpha, owners))
        layer = sheet.copy()
        layer.putalpha(Image.frombytes('L', sheet.size, retained))
        bounds = layer.getchannel('A').getbbox()
        full_partitions.append(layer.crop(bounds))
        envelope = SUBJECT_ENVELOPES[owner - 1]
        outside = Image.frombytes('L', sheet.size, retained)
        ImageDraw.Draw(outside).rectangle((envelope[0], envelope[1], envelope[2] - 1, envelope[3] - 1), fill=0)
        excluded_maximum = outside.getextrema()[1]
        if excluded_maximum >= 32:
            raise ValueError('Reviewed envelope clips substantial subject pixels')
        foreground = layer.crop(envelope)
        foregrounds.append(foreground)
        total_mass += sum(retained)
        records.append({'id': identity, 'name': name, 'sheetOrder': owner,
                        'alphaBounds': list(envelope), 'fullPartitionAlphaBounds': list(bounds),
                        'alphaMass': sum(retained), 'excludedAlphaMass': sum(outside.tobytes()),
                        'excludedAlphaMaximum': excluded_maximum,
                        'nonzeroAlphaPixels': sum(value > 0 for value in retained),
                        'partialAlphaPixels': sum(0 < value < 255 for value in retained)})
    if total_mass != sum(alpha):
        raise ValueError('Sheet alpha was lost or duplicated')
    return foregrounds, records, components, full_partitions


def fit_foreground(foreground, size, rectangle):
    left, top, right, bottom = rectangle
    scale = min((right - left) / foreground.width, (bottom - top) / foreground.height)
    dimensions = (max(1, round(foreground.width * scale)), max(1, round(foreground.height * scale)))
    reduced = foreground.convert('RGBa').resize(dimensions, Image.Resampling.LANCZOS).convert('RGBA')
    layer = Image.new('RGBA', size)
    layer.paste(reduced, (left + (right - left - dimensions[0]) // 2,
                          top + (bottom - top - dimensions[1]) // 2))
    return layer


def button_states(normal):
    return (normal, ImageEnhance.Brightness(normal).enhance(.82),
            ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(.15)).enhance(.65),
            ImageEnhance.Brightness(normal).enhance(1.13))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def recover_baseline(repo, root, verified):
    """Recover ALL manifest inputs; reject duplicate source paths with other bytes."""
    index = {row['resource']: row for row in verified['entries']}
    with (repo / ARCHIVE).open('rb') as stream:
        for row in verified['manifest']['entries']:
            metadata = index[row['resource']]
            target = root / row['source']
            stream.seek(metadata['offset'])
            data = stream.read(metadata['size'])
            if digest(data) != row['sha256']:
                raise ValueError('Baseline payload changed during recovery')
            if target.exists() and target.read_bytes() != data:
                raise ValueError('Conflicting baseline source staging identities')
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)


def export_resources(root, templates, foregrounds, baseline):
    outputs, skill_rows, perk_rows = {}, [], []
    for rank in RANKS:
        images = {}
        for role, (filename, size, rectangle, marker_box) in SLOTS.items():
            source = templates / filename
            if digest(source.read_bytes()) != TEMPLATE_HASHES[filename]:
                raise ValueError('Pinned native template changed')
            base = Image.open(source).convert('RGBA')
            if base.size != size:
                raise ValueError('Native template size mismatch')
            art_layer = fit_foreground(foregrounds[0], size, rectangle)
            result = Image.alpha_composite(base, art_layer)
            marker_role = 'medium' if role == 'scenarioBonus' else role
            marker_name = f'SPRITES/NH_battlecraft_{rank}_{marker_role}.png'
            marker_source = Image.open(baseline(marker_name)).convert('RGBA')
            box = marker_box or SLOTS['medium'][3]
            marker = marker_source.crop(box)
            position = box[:2] if marker_box else ((size[0] - marker.width) // 2, 56)
            result.paste(marker, position)
            name = f'NH_authority_{rank}_{role}.png'
            outputs[name] = result
            images[role] = name
        skill_rows.append({'rank': rank, 'images': images})
    for (identity, name), stem, foreground in zip(IDENTITIES[1:], STEMS[1:], foregrounds[1:]):
        resource_stem = 'NH_perk_authority_' + stem
        normal = fit_foreground(foreground, (44, 44), (2, 2, 42, 42))
        outputs[resource_stem + '.png'] = normal
        outputs[resource_stem + '_32.png'] = fit_foreground(foreground, (32, 32), (2, 2, 30, 30))
        for state, image in zip(STATES, button_states(normal)):
            outputs[resource_stem + '_' + state + '.png'] = image
        outputs[resource_stem + '.json'] = {'images': [
            {'group': 0, 'frame': index, 'file': resource_stem + '_' + state + '.png'}
            for index, state in enumerate(STATES)]}
        perk_rows.append({'id': 'new-horizons:authority.' + identity, 'name': name, 'icon44': resource_stem})
    rows = []
    for filename, payload in sorted(outputs.items()):
        source = 'Mods/new-horizons/Images/' + filename
        target = root / source
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(payload, Image.Image):
            # Strip authoring metadata and hidden RGB from fully transparent pixels.
            clean = Image.new('RGBA', payload.size)
            clean.putdata([pixel if pixel[3] else (0, 0, 0, 0) for pixel in payload.get_flattened_data()])
            clean.save(target, format='PNG')
        else:
            write_json(target, payload)
        origin = ORIGIN
        if filename.startswith('NH_authority_'):
            origin += '; native modder-tools-pack mixed-origin leather and selected Battlecraft rank-marker composites; inherited terms retained'
        rows.append({'resource': 'SPRITES/' + filename, 'source': source,
                     'size': target.stat().st_size, 'sha256': digest(target.read_bytes()),
                     'family': FAMILY, 'origin': origin, 'selection': SELECTION, 'approval': APPROVAL})
    return rows, skill_rows, perk_rows


def prepare(repo, sheet_path, templates, output):
    repo = repo.resolve(strict=True)
    output = output.resolve()
    if output.exists() or output.is_relative_to(repo) or not output.parent.is_dir():
        raise ValueError('Output must be a fresh external directory with an existing parent')
    if digest(sheet_path.read_bytes()) != SHEET_SHA256:
        raise ValueError('This importer accepts only the reviewed owner-supplied sheet')
    sheet = Image.open(sheet_path)
    if sheet.mode != 'RGBA' or sheet.size != (1448, 1086):
        raise ValueError('Expected the original RGBA1448x1086 sheet')
    frozen = {name: digest((repo / name).read_bytes()) for name in (ARCHIVE, MANIFEST, LEDGER, HEADER)}
    baseline = nhart.verify(repo / ARCHIVE, repo / MANIFEST)
    ledger = json.loads((repo / LEDGER).read_text())
    if ledger['waves'][-1]['newArchiveSha256'] != frozen[ARCHIVE]:
        raise ValueError('Existing additions ledger does not anchor the baseline pack')
    output.mkdir()
    recovery = output / 'preserved-inputs'
    recovery.mkdir()
    shutil.copy2(sheet_path, recovery / 'owner-sheet.png')
    for name in (ARCHIVE, MANIFEST, LEDGER, HEADER):
        target = recovery / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / name, target)
    root = output / 'complete-inputs'
    recover_baseline(repo, root, baseline)
    foregrounds, extraction, components, partitions = split_sheet(sheet)
    for stem, foreground, partition in zip(STEMS, foregrounds, partitions):
        foreground.save(recovery / (stem + '-foreground.png'))
        partition.save(recovery / (stem + '-full-partition.png'))
    by_resource = {row['resource']: row for row in baseline['manifest']['entries']}

    def original(name):
        return root / by_resource[name]['source']

    added, skills, perks = export_resources(root, templates, foregrounds, original)
    old_identities = {nhart.resource_identity(name) for name in by_resource}
    if any(nhart.resource_identity(row['resource']) in old_identities for row in added):
        raise ValueError('Authority resources collide with a baseline identity')
    manifest = json.loads((repo / MANIFEST).read_text())
    manifest['entries'].extend(added)
    manifest['requiredFamilyCounts'][FAMILY] += len(added)
    bundle = output / 'bundle'
    write_json(bundle / MANIFEST, manifest)
    archive = bundle / ARCHIVE
    archive.parent.mkdir(parents=True, exist_ok=True)
    nhart.pack(manifest, root, archive)
    verified = nhart.verify(archive, bundle / MANIFEST)
    resulting = {row['resource']: row for row in verified['manifest']['entries']}
    if any(resulting[row['resource']] != row for row in baseline['manifest']['entries']):
        raise ValueError('Unaffected baseline payload or provenance changed')
    record = {'format': 1, 'sourceSheetSha256': SHEET_SHA256, 'sourceSize': list(sheet.size),
              'sourceMode': sheet.mode, 'sourceMetadataKeys': sorted(sheet.info),
              'origin': ORIGIN, 'selection': SELECTION, 'approval': APPROVAL,
              'rights': RIGHTS,
              'extraction': 'alpha32 connected ownership seeds (minimum100 pixels), four-neighbor distance partition; reviewed source envelopes exclude only distant alpha-below32 specks retained in private full partitions; alpha unchanged inside envelopes; premultiplied RGBa LANCZOS contain; no chroma key',
              'subjects': extraction, 'templates': TEMPLATE_HASHES,
              'slots': {role: {'nativeSize': list(size), 'template': filename,
                               'foregroundRectangle': list(rectangle),
                               'rankMarkerSourceRole': 'medium' if role == 'scenarioBonus' else role,
                               'rankMarkerSourceRectangle': list(marker_box or SLOTS['medium'][3]),
                               'rankMarkerPosition': list(marker_box[:2]) if marker_box else [19, 56]}
                        for role, (filename, size, rectangle, marker_box) in SLOTS.items()},
              'rankMarkerSources': {f'SPRITES/NH_battlecraft_{rank}_{role}.png':
                                    by_resource[f'SPRITES/NH_battlecraft_{rank}_{role}.png']['sha256']
                                    for rank in RANKS for role in ('small', 'medium', 'large')},
              'templateRepository': 'vcmi-mods/modder-tools-pack',
              'templateRevision': 'e21c90192cb2faf2c4d41347141248e19283a5ce',
              'ranks': skills, 'perks': perks, 'resources': [row['resource'] for row in added],
              'sourceEntries': added}
    write_json(bundle / RECORD, record)
    wave = {'baselineArchiveSha256': frozen[ARCHIVE], 'baselineManifestSha256': frozen[MANIFEST],
            'baselineEntriesSha256': canonical_digest(baseline['manifest']['entries']),
            'baselineHeaderSha256': frozen[HEADER], 'baselineCount': len(by_resource),
            'entryCount': len(manifest['entries']), 'newArchiveSha256': digest(archive.read_bytes()),
            'resources': record['resources'], 'perks': [], 'statuses': [], 'reusedBindings': [],
            'configRevisions': ledger['waves'][-1]['configRevisions'],
            'nonStatusConfigSha256': ledger['waves'][-1]['nonStatusConfigSha256'],
            'suppliedAuthority': [{'resources': record['resources'], 'selectionRecord': RECORD,
                                   'selectionRecordSha256': digest((bundle / RECORD).read_bytes()),
                                   'origin': ORIGIN, 'selection': SELECTION, 'approval': APPROVAL}]}
    ledger['waves'].append(wave)
    write_json(bundle / LEDGER, ledger)
    write_json(output / 'extraction-evidence.json', {'components': components, 'subjects': extraction})
    if any(digest((repo / name).read_bytes()) != sha for name, sha in frozen.items()):
        raise ValueError('Repository baseline changed while preparing the bundle')
    ready = {'baseline': frozen, 'baselinePreserved': len(by_resource), 'addedResources': len(added),
             'entryCount': len(manifest['entries']), 'files': {name: digest((bundle / name).read_bytes())
                 for name in (ARCHIVE, MANIFEST, LEDGER, RECORD)}}
    write_json(output / 'READY.json', ready)
    return ready


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--sheet', type=Path, required=True)
    parser.add_argument('--templates', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.repo, args.sheet, args.templates, args.output)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
