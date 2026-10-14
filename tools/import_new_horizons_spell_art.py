#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare a package-only spell-art delta outside the checkout; never install it."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import shutil

from PIL import Image
import nhart

CONFIG = 'Mods/new-horizons/Content/config/spells/newHorizons.json'
ARCHIVE = 'Mods/new-horizons/NewHorizons.nhart'
MANIFEST = 'assets/new-horizons/runtime-art-manifest.json'
ROLES = {'iconBook': 44, 'iconScroll': 32, 'iconEffect': 30,
         'iconImmune': 30, 'iconScenarioBonus': 32}
SPELLS = {'phantomArmy', 'timeStop', 'spellLock', 'focusMagic', 'disintegrate',
          'handOfFate', 'realityWarp', 'polymorph', 'shieldOfChaos', 'pandemonium',
          'naturesWrath', 'elementalConvergence', 'masterChainLightning',
          'transfigureMatter', 'crusade', 'confusion', 'puppetMaster'}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_jsonc(text):
    string = r'"(?:\\.|[^"\\])*"'
    for other in (r'//[^\n]*|/\*[\s\S]*?\*/', r',(?=\s*[}\]])'):
        text = re.sub(string + '|' + other,
                      lambda match: match[0] if match[0].startswith('"') else '', text)
    return json.loads(text)


def non_graphics_digest(config):
    config = json.loads(json.dumps(config))
    for key in SPELLS:
        config[key].pop('graphics')
    for key in ('holyWrath', 'regeneration'):
        config[key]['graphics'].pop('iconScenarioBonus')
    return hashlib.sha256(json.dumps(config, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def replace_graphics(text, key, bindings):
    pattern = r'(^\t"' + re.escape(key) + r'"\s*:\s*\{[\s\S]*?"graphics"\s*:\s*\{)([^}]*)(\})'
    def replace(match):
        body = match[2]
        for role, filename in bindings.items():
            body, count = re.subn(r'("' + role + r'"\s*:\s*)"[^"\n]*"',
                                 lambda item: item[1] + json.dumps(filename), body)
            if count != 1:
                raise ValueError('Missing or duplicate graphics role')
        return match[1] + body + match[3]
    result, count = re.subn(pattern, replace, text, flags=re.MULTILINE)
    if count != 1:
        raise ValueError('Missing or duplicate spell key')
    return result


def prepare(repo, handoffs, staging, recovery, output):
    roots = [path.resolve() for path in (staging, recovery, output)]
    if any(path.exists() or path.is_symlink() or path.resolve().is_relative_to(repo.resolve())
           for path in (staging, recovery, output)) or any(
               left.is_relative_to(right) for i, left in enumerate(roots)
               for j, right in enumerate(roots) if i != j):
        raise ValueError('Fresh, nonoverlapping external directories required')
    baseline = nhart.verify(repo / ARCHIVE, repo / MANIFEST)
    manifest = json.loads((repo / MANIFEST).read_text())
    before_text = (repo / CONFIG).read_text()
    before = parse_jsonc(before_text)
    text = before_text
    selections, native, identities = [], {}, {nhart.resource_identity(row['resource']) for row in manifest['entries']}
    for handoff in handoffs:
        evidence = json.loads(handoff.read_text())
        for asset in evidence.get('artifacts', evidence.get('assets', [])):
            key = asset['spell'].removeprefix('new-horizons:')
            if asset['spell'] != 'new-horizons:' + key or key not in SPELLS or key in native or key not in before:
                raise ValueError('Unexpected or duplicate selected spell')
            bindings, exports = {}, {}
            for role, size in ROLES.items():
                relative = asset['proposedRoles'][role]
                source = (handoff.parent / relative).resolve()
                if not source.is_relative_to(handoff.parent.resolve()) or digest(source) != evidence['sha256'][relative]:
                    raise ValueError('Pinned native export changed or escaped handoff')
                filename = source.name
                if not re.fullmatch(r'NH_spell_[a-z_]+-(44|32|30)\.png', filename):
                    raise ValueError('Nonportable resource filename')
                with Image.open(source) as image:
                    if image.mode != 'RGBA' or image.size != (size, size) or image.getchannel('A').getextrema() != (0, 255):
                        raise ValueError('Incorrect native size or transparent RGBA export')
                bindings[role] = filename
                exports[filename] = {'sha256': digest(source), 'size': size}
                native.setdefault(key, {})[filename] = source
            if len(exports) != 3:
                raise ValueError('Each selected family must have exactly three native exports')
            references = {}
            for role in ('master', 'prompt', 'preview'):
                source = handoff.parent / asset[role]
                if not source.resolve().is_relative_to(handoff.parent.resolve()) or digest(source) != evidence['sha256'][asset[role]]:
                    raise ValueError('Artist provenance changed')
                references[role + 'Sha256'] = digest(source)
            selections.append({'id': asset['spell'], 'bindings': bindings, 'exports': exports,
                               'handoffSha256': digest(handoff), **references,
                               'origin': 'original-generated-purpose-made-spell-symbol',
                               'selection': 'user-authorized-provisional-runtime-2026-10-14',
                               'approval': 'Provisional; not Final; rendered acceptance pending'})
            text = replace_graphics(text, key, bindings)
    if set(native) != SPELLS:
        raise ValueError('Incomplete seventeen-spell selection')
    reused = {key: {'iconScenarioBonus': 'NH_' + key + '_32.png'} for key in ('holyWrath', 'regeneration')}
    for key, bindings in reused.items():
        if 'SPRITES/' + bindings['iconScenarioBonus'] not in {row['resource'] for row in manifest['entries']}:
            raise ValueError('Missing selected scenario resource')
        text = replace_graphics(text, key, bindings)
    after = parse_jsonc(text)
    if non_graphics_digest(before) != non_graphics_digest(after):
        raise ValueError('Non-selected configuration changed')
    for key in before:
        left, right = dict(before[key]), dict(after[key])
        if key in SPELLS or key in reused:
            left.pop('graphics'); right.pop('graphics')
        if left != right:
            raise ValueError('Non-graphics configuration changed')
    recovery.mkdir(parents=True)
    for name in (ARCHIVE, MANIFEST, CONFIG):
        target = recovery / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / name, target)
    staging.mkdir(parents=True)
    index = {row['resource']: row for row in baseline['entries']}
    with (repo / ARCHIVE).open('rb') as stream:
        for row in manifest['entries']:
            target = staging / row['source']
            target.parent.mkdir(parents=True, exist_ok=True)
            stream.seek(index[row['resource']]['offset'])
            target.write_bytes(stream.read(row['size']))
            if digest(target) != row['sha256']:
                raise ValueError('Baseline extraction changed bytes')
    original = list(manifest['entries'])
    for selection in selections:
        for filename, metadata in selection['exports'].items():
            resource = 'SPRITES/' + filename
            identity = nhart.resource_identity(resource)
            if identity in identities:
                raise ValueError('Selected resource collision')
            identities.add(identity)
            target = staging / 'Mods/new-horizons/Images' / filename
            shutil.copyfile(native[selection['id'].split(':')[1]][filename], target)
            manifest['entries'].append({'resource': resource, 'source': 'Mods/new-horizons/Images/' + filename,
                'size': target.stat().st_size, 'sha256': metadata['sha256'], 'family': 'builtin-skills-perks-orders-and-ui',
                **{key: selection[key] for key in ('origin', 'selection', 'approval')}})
    manifest['entries'] = original + sorted(manifest['entries'][len(original):], key=lambda row: row['resource'].encode())
    manifest['requiredFamilyCounts'] = {family: sum(row['family'] == family for row in manifest['entries'])
                                       for family in manifest['requiredFamilies']}
    candidate = staging / 'candidate.nhart'
    nhart.pack(manifest, staging, candidate)
    verified = nhart.verify(candidate, manifest)
    resulting = {row['resource']: row for row in verified['manifest']['entries']}
    if any(resulting[row['resource']] != row for row in original):
        raise ValueError('Baseline provenance or payload changed')
    inventory_path = repo / 'docs/NH_UI_ASSET_INVENTORY.csv'
    inventory = list(csv.DictReader(io.StringIO(inventory_path.read_text())))
    selected = {row['id']: row for row in selections}
    seen = set()
    for row in inventory:
        match = re.search(r'\((new-horizons:[^)]+)\)$', row['Item / ID'])
        if match and match[1] in selected:
            seen.add(match[1]); row['Art'] = 'Provisional'
            row['Live binding / source'] = CONFIG + '; ' + '; '.join(sorted(selected[match[1]]['exports'])) + '; selected NHART'
            row['Remaining work / evidence'] += ' Purpose-made original draft now selected provisionally for all five icon roles; pinned native44/32/30 and master/prompt/preview provenance retained. Package-only rendering/contrast and user-final approval remain pending.'
        elif match and match[1] in {'new-horizons:' + key for key in reused}:
            row['Live binding / source'] += '; scenario role now reuses selected ' + reused[match[1].split(':')[1]]['iconScenarioBonus']
    for identity in sorted(set(selected) - seen):
        key = identity.split(':')[1]
        name = before[key]['name']
        inventory.append({'Area': 'Spell', 'Item / ID': name + ' (' + identity + ')',
            'Live binding / source': CONFIG + '; ' + '; '.join(sorted(selected[identity]['exports'])) + '; selected NHART',
            'Implementation': 'Provisional', 'Art': 'Provisional',
            'Remaining work / evidence': 'Purpose-made original draft selected provisionally for all five icon roles; pinned native44/32/30 and master/prompt/preview provenance retained. Graphics-only change; existing spell mechanics preserved. Package-only rendering/contrast and user-final approval remain pending.'})
    csv_text = io.StringIO()
    writer = csv.DictWriter(csv_text, fieldnames=list(inventory[0]), lineterminator='\n', quoting=csv.QUOTE_ALL)
    csv_text.write(inventory_path.read_text().splitlines()[0] + '\n'); writer.writerows(inventory)
    legacy_path = 'assets/new-horizons/art-source/spell-symbols-v2/manifest.json'
    legacy_text = (repo / legacy_path).read_text()
    # Only reconcile the two historical liveBindings leaves, not source provenance.
    for key, bindings in reused.items():
        pattern = r'("spellKey": "' + key + r'"[\s\S]*?"liveBindings": \{[^}]*"iconScenarioBonus": )"[^"]*"'
        legacy_text, count = re.subn(pattern, lambda match: match[1] + json.dumps(bindings['iconScenarioBonus']), legacy_text)
        if count != 1:
            raise ValueError('Missing legacy live-binding record')
    provenance = {'format': 1, 'baselineArchiveSha256': digest(repo / ARCHIVE),
        'baselineManifestSha256': digest(repo / MANIFEST), 'baselineConfigSha256': digest(repo / CONFIG),
        'nonGraphicsConfigSha256': non_graphics_digest(before),
        'baselineEntriesSha256': hashlib.sha256(json.dumps(sorted(original, key=lambda row: row['resource'].encode()), sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
        'baselineCount': len(original), 'entryCount': len(manifest['entries']), 'newArchiveSha256': digest(candidate),
        'entries': selections, 'reusedScenarioBindings': reused,
        'remainingRoles': ['arcaneBreach remains outside this seventeen-family selection'],
        'acceptance': 'Source/container verified; native rendered contrast and Final approval pending'}
    outputs = {CONFIG: text, MANIFEST: json.dumps(manifest, indent=2) + '\n',
        'assets/new-horizons/spell-art-selections.json': json.dumps(provenance, indent=2) + '\n',
        'docs/NH_UI_ASSET_INVENTORY.csv': csv_text.getvalue(), legacy_path: legacy_text}
    output.mkdir(parents=True)
    for name, content in outputs.items():
        path = output / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(content)
    path = output / ARCHIVE; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(candidate, path)
    nhart.verify(path, output / MANIFEST)
    result = {'spellCount': len(selections), 'entryCount': len(manifest['entries']), 'baselinePreserved': len(original),
              'sha256': digest(path), 'files': {name: digest(output / name) for name in [ARCHIVE, *outputs]}}
    (output / 'READY.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--handoff', type=Path, action='append', required=True)
    for name in ('staging', 'recovery', 'output'):
        parser.add_argument('--' + name + '-root', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.repo, args.handoff, args.staging_root, args.recovery_root, args.output_root), sort_keys=True))
    except OSError:
        parser.exit(1, 'Private input/output operation failed; repository unchanged\n')
    except (ValueError, KeyError, nhart.NHArtError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
