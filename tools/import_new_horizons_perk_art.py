#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare an external provisional perk-art bundle; never mutate repository inputs."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shutil

from PIL import Image, ImageEnhance

import nhart


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def import_art(repo, plan_path, staging, recovery, output):
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    entries = plan['entries']
    if plan['blocked'] or len(entries) != plan['verifiedCount'] or len({row['id'] for row in entries}) != len(entries):
        raise ValueError('Incomplete or duplicate semantic selection')
    for directory in (staging, recovery, output):
        if directory.exists() or directory.is_symlink() or directory.resolve().is_relative_to(repo.resolve()):
            raise ValueError('Private staging and recovery must be fresh and outside checkout')
    directories = [directory.resolve() for directory in (staging, recovery, output)]
    if any(left.is_relative_to(right) for index, left in enumerate(directories)
           for other, right in enumerate(directories) if index != other):
        raise ValueError('Output, staging and recovery directories must not overlap')
    archive = repo / 'Mods/new-horizons/NewHorizons.nhart'
    manifest_path = repo / 'assets/new-horizons/runtime-art-manifest.json'
    if digest(archive) != plan['baselineArchiveSha256'] or digest(manifest_path) != plan['baselineManifestSha256']:
        raise ValueError('Baseline changed; select and review a fresh delta')
    baseline = nhart.verify(archive, manifest_path)
    registry = json.loads((repo / 'config/newHorizonsPerks.json').read_text(encoding='utf-8'))
    roster = {perk['id'] for skill in registry['skills'].values() for perk in skill['perks']}
    header_path = repo / 'client/windows/NewHorizonsPerkIcons.h'
    header = header_path.read_text(encoding='utf-8')
    lookup = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', header))
    existing_resources = {nhart.resource_identity(row['resource']) for row in baseline['manifest']['entries']}
    new_resources = set()
    native = {}
    for row in entries:
        stem = row['icon44']
        if not re.fullmatch(r'NH_perk_[a-z0-9_]+', stem):
            raise ValueError('Non-portable perk resource stem')
        expected = ['SPRITES/' + stem + suffix for suffix in (
            '.png', '_32.png', '.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')]
        if set(row['resources']) != set(expected) or len(row['resources']) != 7:
            raise ValueError('Unexpected declared perk resource leaves')
        for resource in expected:
            identity = nhart.resource_identity(resource)
            if identity in existing_resources or identity in new_resources:
                raise ValueError('Selected resource identity collides')
            new_resources.add(identity)
        if row['id'] not in roster or lookup.get(row['id'], 'NH_perk_neutral') != 'NH_perk_neutral':
            raise ValueError('Selection is not an exact current neutral perk')
        if digest(Path(row['sourceManifest'])) != row['reviewManifestSha256'] or digest(Path(row['review'])) != row['reviewEvidenceSha256']:
            raise ValueError('Artist evidence changed')
        for item in row['inputs']:
            source = Path(item['frozen_copy'])
            if digest(source) != item['sha256']:
                raise ValueError('Pinned native input changed')
            with Image.open(source) as image:
                if image.size != (item['size'], item['size']):
                    raise ValueError('Incorrect native dimensions')
            native[row['id'], item['size']] = source
    recovery.mkdir(parents=True)
    shutil.copyfile(archive, recovery / 'baseline.nhart')
    shutil.copyfile(manifest_path, recovery / 'baseline-manifest.json')
    staging.mkdir(parents=True)
    rows_by_resource = {row['resource']: row for row in baseline['entries']}
    with archive.open('rb') as stream:
        for entry in baseline['manifest']['entries']:
            metadata = rows_by_resource[entry['resource']]
            target = staging / entry['source']
            target.parent.mkdir(parents=True, exist_ok=True)
            stream.seek(metadata['offset'])
            target.write_bytes(stream.read(metadata['size']))
            if digest(target) != entry['sha256']:
                raise ValueError('Baseline extraction changed selected bytes')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    for row in entries:
        stem = row['icon44']
        source_directory = staging / 'Mods/new-horizons/Images'
        source_directory.mkdir(parents=True, exist_ok=True)
        with Image.open(native[row['id'], 44]) as image:
            normal = image.convert('RGBA')
        states = {'normal': normal,
                  'pressed': ImageEnhance.Brightness(normal).enhance(0.82),
                  'disabled': ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
                  'highlighted': ImageEnhance.Brightness(normal).enhance(1.13)}
        shutil.copyfile(native[row['id'], 44], source_directory / (stem + '.png'))
        shutil.copyfile(native[row['id'], 32], source_directory / (stem + '_32.png'))
        frames = []
        for index, (state, image) in enumerate(states.items()):
            filename = stem + '_' + state + '.png'
            image.save(source_directory / filename, format='PNG')
            frames.append({'group': 0, 'frame': index, 'file': filename})
        (source_directory / (stem + '.json')).write_text(json.dumps({'images': frames}, indent=2) + '\n', encoding='utf-8')
        for resource in row['resources']:
            filename = resource.removeprefix('SPRITES/')
            path = source_directory / filename
            manifest['entries'].append({'resource': resource, 'source': 'Mods/new-horizons/Images/' + filename,
                                       'size': path.stat().st_size, 'sha256': digest(path),
                                       'family': 'builtin-skills-perks-orders-and-ui', 'origin': row['origin'],
                                       'selection': row['selection'], 'approval': row['approval']})
    counts = {family: sum(row['family'] == family for row in manifest['entries'])
              for family in manifest['requiredFamilies']}
    manifest['requiredFamilyCounts'] = counts
    baseline_count = len(baseline['manifest']['entries'])
    public_entries = manifest['entries'][:baseline_count] + sorted(
        manifest['entries'][baseline_count:], key=lambda row: row['resource'].encode())
    manifest = nhart.validate_manifest(manifest)
    candidate = staging / 'candidate.nhart'
    nhart.pack(manifest, staging, candidate)
    verified = nhart.verify(candidate, manifest)
    resulting = {row['resource']: row for row in verified['manifest']['entries']}
    if any(resulting[row['resource']] != row for row in baseline['manifest']['entries']):
        raise ValueError('Unaffected baseline payload/provenance changed')
    additions = ''.join('\t\t{"' + row['id'] + '", "' + row['icon44'] + '"},\n' for row in sorted(entries, key=lambda row: row['id']))
    header = header.replace('\tstatic const std::map<std::string, std::string> icons = {\n',
                            '\tstatic const std::map<std::string, std::string> icons = {\n' + additions, 1)
    inventory_path = repo / 'docs/NH_UI_ASSET_INVENTORY.csv'
    inventory = list(csv.DictReader(io.StringIO(inventory_path.read_text(encoding='utf-8'))))
    selection = {row['id']: row for row in entries}
    updated = set()
    for row in inventory:
        identity = re.search(r'\((new-horizons:[^)]+)\)$', row['Item / ID'])
        if identity and identity.group(1) in selection:
            perk_id = identity.group(1)
            row['Art'] = 'Provisional'
            row['Live binding / source'] = 'client/windows/NewHorizonsPerkIcons.h; ' + selection[perk_id]['icon44'] + '; selected NHART package'
            row['Remaining work / evidence'] = 'Selected original draft with verified native44/32, preserved master/prompt/review provenance; four-state44 export. User permits provisional runtime use, not Final approval. Package-only rendered acceptance pending.'
            updated.add(perk_id)
    if updated != set(selection):
        raise ValueError('Missing semantic asset-inventory row')
    csv_text = io.StringIO()
    writer = csv.DictWriter(csv_text, fieldnames=list(inventory[0]), lineterminator='\n', quoting=csv.QUOTE_ALL)
    csv_text.write(inventory_path.read_text(encoding='utf-8').splitlines()[0] + '\n')
    writer.writerows(inventory)
    provenance = {key: plan[key] for key in ('handoffSha256', 'additiveHandoffSha256', 'baselineArchiveSha256', 'baselineManifestSha256')}
    provenance['entries'] = [{key: value for key, value in row.items() if key not in ('inputs', 'sourceManifest', 'review', 'versionsPreserved')} for row in entries]
    provenance['newArchiveSha256'] = digest(candidate)
    provenance['entryCount'] = len(manifest['entries'])
    provenance['baselineEntriesSha256'] = hashlib.sha256(json.dumps(
        baseline['manifest']['entries'], sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    # These are unpublished artifacts. A failure may leave a partial output
    # directory, but never changes repository inputs or emits a ready receipt.
    output.mkdir(parents=True)
    output_archive = output / 'Mods/new-horizons/NewHorizons.nhart'
    output_archive.parent.mkdir(parents=True)
    shutil.copyfile(candidate, output_archive)
    ordered_manifest = {key: manifest[key] for key in ('format', 'entries', 'requiredFamilies', 'requiredFamilyCounts')}
    ordered_manifest['entries'] = public_entries
    outputs = {'assets/new-horizons/runtime-art-manifest.json': json.dumps(ordered_manifest, indent=2) + '\n',
               'assets/new-horizons/perk-art-selections.json': json.dumps(provenance, indent=2) + '\n',
               'client/windows/NewHorizonsPerkIcons.h': header,
               'docs/NH_UI_ASSET_INVENTORY.csv': csv_text.getvalue()}
    for name, content in outputs.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
    nhart.verify(output_archive, output / 'assets/new-horizons/runtime-art-manifest.json')
    result = {'perkCount': len(entries), 'entryCount': len(manifest['entries']), 'sha256': digest(output_archive),
              'baselinePreserved': len(baseline['manifest']['entries']),
              'files': {name: digest(output / name) for name in [
                  'Mods/new-horizons/NewHorizons.nhart', *outputs]}}
    (output / 'READY.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--input-plan', type=Path, required=True)
    parser.add_argument('--staging-root', type=Path, required=True)
    parser.add_argument('--recovery-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True, help='Fresh external artifact bundle; never installs')
    args = parser.parse_args()
    try:
        result = import_art(args.repo, args.input_plan, args.staging_root, args.recovery_root, args.output_root)
    except OSError:
        parser.exit(1, 'Private input/output filesystem operation failed; no input paths disclosed\n')
    except (ValueError, KeyError, nhart.NHArtError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
