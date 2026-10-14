#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare an external additive perk/status-art wave, preserving earlier selections."""
import argparse
import csv
import io
import json
from pathlib import Path
import re
import shutil

from PIL import Image
import nhart
import import_new_horizons_perk_art as perks
import import_new_horizons_spell_art as spells

LEDGER = 'assets/new-horizons/runtime-art-additions.json'
HEADER = 'client/windows/NewHorizonsPerkIcons.h'
INVENTORY = 'docs/NH_UI_ASSET_INVENTORY.csv'
PERK_RECORD = 'assets/new-horizons/perk-art-selections.json'
STATUS_NAME = 'NH_status_arcane_breach-30.png'
MASS_CONFIG = 'Mods/new-horizons/Content/config/spells/massVariants.json'


def entries_digest(entries):
    import hashlib
    return hashlib.sha256(json.dumps(sorted(entries, key=lambda row: row['resource'].encode()),
                                     sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def non_status_config_digest(config, statuses):
    import hashlib
    config = json.loads(json.dumps(config))
    for status in statuses:
        for role in status['bindings']:
            config[status['id'].split(':')[1]]['graphics'].pop(role)
    return hashlib.sha256(json.dumps(config, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def reject_artist_overlap(roots, artist_roots):
    for root in roots:
        for artist_root in artist_roots:
            artist_root = artist_root.resolve()
            if root.is_relative_to(artist_root) or artist_root.is_relative_to(root):
                raise ValueError('Preparation/output overlaps protected artist inputs')


def prepare(repo, handoffs, status_handoff, work, output, mass_regeneration_scenario=False):
    roots = [path.resolve() for path in (work, output)]
    if any(path.exists() or path.is_symlink() or path.resolve().is_relative_to(repo.resolve())
           for path in (work, output)) or roots[0].is_relative_to(roots[1]) or roots[1].is_relative_to(roots[0]):
        raise ValueError('Fresh nonoverlapping external preparation/output directories required')
    status_handoffs = [status_handoff] if isinstance(status_handoff, Path) else (status_handoff or [])
    reject_artist_overlap(roots, [path.parent for path in [*handoffs, *status_handoffs]])
    baseline = nhart.verify(repo / spells.ARCHIVE, repo / spells.MANIFEST)
    archive_hash, manifest_hash = perks.digest(repo / spells.ARCHIVE), perks.digest(repo / spells.MANIFEST)
    plan_rows, seen, handoff_hashes = [], set(), []
    for handoff in handoffs:
        evidence = json.loads(handoff.read_text(encoding='utf-8'))
        if evidence['count'] != len(evidence['entries']) or not evidence['entries']:
            raise ValueError('Incomplete handoff count')
        handoff_hashes.append(perks.digest(handoff))
        for selected in evidence['entries']:
            identity = selected['id']
            if identity in seen:
                raise ValueError('Duplicate perk selection')
            seen.add(identity)
            directory = Path(selected['selected_review_directory']).resolve()
            reject_artist_overlap(roots, [directory])
            source_manifest = Path(selected['manifest'])
            review = Path(selected['review'])
            prompt = directory / 'PROMPT-AND-PROVENANCE.md'
            manifest = json.loads(source_manifest.read_text(encoding='utf-8'))
            exports = {row['file']: row for row in manifest['outputs']}
            slug = manifest['name']
            if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', slug):
                raise ValueError('Nonportable perk slug')
            stem = 'NH_perk_' + slug.replace('-', '_')
            native = []
            for size in (44, 32):
                path = Path(selected['native' + str(size)])
                if not path.resolve().is_relative_to(directory) or path.name not in exports or perks.digest(path) != exports[path.name]['sha256']:
                    raise ValueError('Pinned perk native export changed')
                with Image.open(path) as image:
                    if image.size != (size, size):
                        raise ValueError('Incorrect perk native dimensions')
                native.append({'size': size, 'sha256': perks.digest(path), 'frozen_copy': str(path)})
            master = directory / 'master.png'
            comparison = Path(selected['comparison'])
            if any(not path.resolve().is_relative_to(directory) for path in (source_manifest, review, comparison)):
                raise ValueError('Artist input escaped selected directory')
            if perks.digest(master) != exports['master.png']['sha256'] or perks.digest(comparison) != exports[comparison.name]['sha256']:
                raise ValueError('Pinned perk provenance changed')
            plan_rows.append({'id': identity, 'name': selected['name'], 'slug': slug, 'version': directory.name,
                'icon44': stem, 'image32': stem + '_32', 'inputs': native, 'sourceManifest': str(source_manifest),
                'review': str(review), 'native44Sha256': native[0]['sha256'], 'native32Sha256': native[1]['sha256'],
                'masterSha256': perks.digest(master), 'promptSha256': perks.digest(prompt),
                'comparisonSha256': perks.digest(comparison), 'reviewManifestSha256': perks.digest(source_manifest),
                'reviewEvidenceSha256': perks.digest(review), 'handoffSha256': perks.digest(handoff),
                'origin': 'independently-authored-built-in-generation-or-targeted-own-draft-edit',
                'selection': 'user-authorized-provisional-perk-drafts-2026-10-14', 'approval': 'Provisional; no Final approval',
                'resources': ['SPRITES/' + stem + suffix for suffix in (
                    '.png', '_32.png', '.json', '_normal.png', '_pressed.png', '_disabled.png', '_highlighted.png')]})
    if not plan_rows:
        raise ValueError('No completed perk inputs')
    status_records, status_sources = [], {}
    original_config = spells.parse_jsonc((repo / spells.CONFIG).read_text(encoding='utf-8'))
    for status_handoff in status_handoffs:
        status = json.loads(status_handoff.read_text(encoding='utf-8'))
        if 'assets' in status:
            if len(status['assets']) != 1 or status['assets'][0]['id'] != 'new-horizons:lucidity' or status['assets'][0]['primaryRole'] != 'effect-and-immune-30px':
                raise ValueError('Not the exact internal Lucidity status')
            asset = status['assets'][0]; directory = asset['directory']
            filename = 'NH_status_lucidity-30.png'
            normalized = {'effect': asset['id'], 'castable': False,
                'proposedStatusRoles': {'iconEffect': directory + '/' + filename, 'iconImmune': directory + '/' + filename},
                'sha256': {directory + '/' + name: digest for name, digest in asset['sha256'].items()},
                'master': directory + '/master.png', 'prompt': directory + '/PROMPT.md',
                'preview': directory + '/NH_status_lucidity-comparison.png'}
            export_manifest_path = status_handoff.parent / directory / 'NH_status_lucidity-manifest.json'
            export_manifest = json.loads(export_manifest_path.read_text(encoding='utf-8'))
            for item in export_manifest['outputs']:
                name = directory + '/' + item['file']
                if name in normalized['sha256'] and normalized['sha256'][name] != item['sha256']:
                    raise ValueError('Lucidity handoff and export manifest disagree')
                normalized['sha256'][name] = item['sha256']
            normalized['sha256'][normalized['prompt']] = perks.digest(status_handoff.parent / normalized['prompt'])
            status = normalized
        else:
            filename = STATUS_NAME
            if status['effect'] != 'new-horizons:arcaneBreach':
                raise ValueError('Not the exact internal Arcane Breach status')
        if status['effect'] in status_sources or status['castable'] is not False or set(status['proposedStatusRoles']) != {'iconEffect', 'iconImmune'}:
            raise ValueError('Invalid or duplicate internal status selection')
        names = set(status['proposedStatusRoles'].values())
        if len(names) != 1:
            raise ValueError('Status roles must share one thirty-pixel export')
        name = names.pop(); source = (status_handoff.parent / name).resolve()
        if source.name != filename or not source.is_relative_to(status_handoff.parent.resolve()):
            raise ValueError('Unexpected status export path')
        for relative, expected in status['sha256'].items():
            path = (status_handoff.parent / relative).resolve()
            if not path.is_relative_to(status_handoff.parent.resolve()) or perks.digest(path) != expected:
                raise ValueError('Pinned status provenance changed')
        with Image.open(source) as image:
            if image.mode != 'RGBA' or image.size != (30, 30) or image.getchannel('A').getextrema() != (0, 255):
                raise ValueError('Incorrect transparent native status export')
        status_sources[status['effect']] = source
        status_record = {'id': status['effect'], 'castable': False,
            'bindings': {'iconEffect': filename, 'iconImmune': filename},
            'previousBindings': {role: original_config[status['effect'].split(':')[1]]['graphics'][role]
                                 for role in ('iconEffect', 'iconImmune')},
            'resources': ['SPRITES/' + filename], 'native30Sha256': perks.digest(source),
            'handoffSha256': perks.digest(status_handoff),
            **{role + 'Sha256': status['sha256'][status[role]] for role in ('master', 'prompt', 'preview')},
            'origin': 'original-generated-purpose-made-internal-status-symbol',
            'selection': 'user-authorized-provisional-runtime-2026-10-14',
            'approval': 'Provisional; not Final; rendered acceptance pending'}
        status_records.append(status_record)
    work.mkdir(parents=True)
    plan = {'entries': plan_rows, 'blocked': [], 'verifiedCount': len(plan_rows),
            'baselineArchiveSha256': archive_hash, 'baselineManifestSha256': manifest_hash,
            'handoffSha256': handoff_hashes[0], 'additiveHandoffSha256': handoff_hashes[-1]}
    plan_path = work / 'private-plan.json'; plan_path.write_text(json.dumps(plan, indent=2), encoding='utf-8')
    # This maintained importer prepares only external outputs. Its temporary
    # standalone selection record is consumed below, never replacing the94 proof.
    perk_output, stage = work / 'perk-output', work / 'staging'
    perks.import_art(repo, plan_path, stage, work / 'recovery', perk_output)
    manifest = json.loads((perk_output / spells.MANIFEST).read_text(encoding='utf-8'))
    config_text = (repo / spells.CONFIG).read_text(encoding='utf-8')
    for status_record in status_records:
        filename = status_record['bindings']['iconEffect']
        if any(nhart.resource_identity(row['resource']) == nhart.resource_identity('SPRITES/' + filename) for row in manifest['entries']):
            raise ValueError('Status resource collision')
        target = stage / 'Mods/new-horizons/Images' / filename
        shutil.copyfile(status_sources[status_record['id']], target)
        manifest['entries'].append({'resource': 'SPRITES/' + filename,
            'source': 'Mods/new-horizons/Images/' + filename, 'size': target.stat().st_size,
            'sha256': status_record['native30Sha256'], 'family': 'builtin-skills-perks-orders-and-ui',
            **{key: status_record[key] for key in ('origin', 'selection', 'approval')}})
        config_text = spells.replace_graphics(config_text, status_record['id'].split(':')[1], status_record['bindings'])
        config_text = config_text.replace('// Existing spellLock frames are an unrelated internal placeholder; no Arcane Breach art is defined here.',
                                          '// Purpose-made internal Arcane Breach status art is selected provisionally; castable spell aliases are unchanged.')
    if non_status_config_digest(original_config, status_records) != non_status_config_digest(spells.parse_jsonc(config_text), status_records):
        raise ValueError('Status edit changed non-owned configuration')
    reused = []
    mass_text = None
    if mass_regeneration_scenario:
        filename = 'NH_regeneration_32.png'
        if 'SPRITES/' + filename not in {row['resource'] for row in manifest['entries']}:
            raise ValueError('Selected Regeneration companion missing')
        mass_text = (repo / MASS_CONFIG).read_text(encoding='utf-8')
        before = spells.parse_jsonc(mass_text)
        mass_text = spells.replace_graphics(mass_text, 'massRegeneration', {'iconScenarioBonus': filename})
        after = spells.parse_jsonc(mass_text)
        previous = before['massRegeneration']['graphics'].pop('iconScenarioBonus')
        after['massRegeneration']['graphics'].pop('iconScenarioBonus')
        if before != after:
            raise ValueError('Mass Regeneration edit changed non-owned configuration')
        reused = [{'config': MASS_CONFIG, 'id': 'new-horizons:massRegeneration',
                   'bindings': {'iconScenarioBonus': filename}, 'previousBindings': {'iconScenarioBonus': previous},
                   'nonGraphicsConfigSha256': non_status_config_digest(spells.parse_jsonc(mass_text),
                       [{'id': 'new-horizons:massRegeneration', 'bindings': {'iconScenarioBonus': filename}}])}]
    manifest['requiredFamilyCounts'] = {family: sum(row['family'] == family for row in manifest['entries'])
                                       for family in manifest['requiredFamilies']}
    candidate = work / 'candidate.nhart'; nhart.pack(manifest, stage, candidate)
    verified = nhart.verify(candidate, manifest)
    resulting = {row['resource']: row for row in verified['manifest']['entries']}
    if any(resulting[row['resource']] != row for row in baseline['manifest']['entries']):
        raise ValueError('Baseline resource or provenance changed')
    record = json.loads((perk_output / PERK_RECORD).read_text(encoding='utf-8'))
    ledger = json.loads((repo / LEDGER).read_text(encoding='utf-8')) if (repo / LEDGER).exists() else {'format': 1, 'waves': []}
    resources = sorted([name for row in record['entries'] + status_records for name in row['resources']])
    ledger['waves'].append({'baselineArchiveSha256': archive_hash, 'baselineManifestSha256': manifest_hash,
        'baselineEntriesSha256': entries_digest(baseline['manifest']['entries']),
        'baselineHeaderSha256': perks.digest(repo / HEADER),
        'nonStatusConfigSha256': non_status_config_digest(spells.parse_jsonc(config_text), status_records),
        'baselineCount': len(baseline['manifest']['entries']), 'entryCount': len(manifest['entries']),
        'newArchiveSha256': perks.digest(candidate), 'resources': resources,
        'perks': record['entries'], 'statuses': status_records, 'reusedBindings': reused,
        'approval': 'Provisional; not Final; rendered acceptance pending'})
    inventory_text = (perk_output / INVENTORY).read_text(encoding='utf-8')
    if status_records:
        rows = list(csv.DictReader(io.StringIO(inventory_text))); changed = 0
        for row in rows:
            if mass_text is not None and row['Item / ID'].endswith('(new-horizons:massRegeneration)'):
                row['Live binding / source'] += '; scenario role now reuses selected NH_regeneration_32.png'
            for status_record in status_records:
                if row['Item / ID'].endswith('(' + status_record['id'] + ')'):
                    row['Art'] = 'Provisional'; row['Live binding / source'] += '; effect/immunity now selected ' + status_record['bindings']['iconEffect']
                    row['Remaining work / evidence'] += ' Purpose-made internal30px status symbol selected provisionally; book/scroll/scenario aliases and all gameplay unchanged. Native rendered contrast and Final approval pending.'; changed += 1
        missing = [status_record for status_record in status_records if not any(row['Item / ID'].endswith('(' + status_record['id'] + ')') for row in rows)]
        for status_record in missing:
            key = status_record['id'].split(':')[1]
            rows.append({'Area': 'Spell status', 'Item / ID': original_config[key]['name'] + ' (' + status_record['id'] + ')',
                'Art': 'Provisional', 'Implementation': 'Provisional', 'Live binding / source': spells.CONFIG + '; selected ' + status_record['bindings']['iconEffect'],
                'Remaining work / evidence': 'Purpose-made internal30px effect/immunity symbol selected provisionally; non-castable status mechanics unchanged. Native rendered contrast and Final approval pending.'})
        if changed + len(missing) != len(status_records):
            raise ValueError('Duplicate internal status inventory row')
        text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=list(rows[0]), lineterminator='\n', quoting=csv.QUOTE_ALL)
        text.write(inventory_text.splitlines()[0] + '\n'); writer.writerows(rows); inventory_text = text.getvalue()
    outputs = {spells.MANIFEST: json.dumps(manifest, indent=2) + '\n',
        LEDGER: json.dumps(ledger, indent=2) + '\n', spells.CONFIG: config_text,
        HEADER: (perk_output / HEADER).read_text(encoding='utf-8'), INVENTORY: inventory_text}
    if mass_text is not None:
        outputs[MASS_CONFIG] = mass_text
    output.mkdir(parents=True)
    for name, text in outputs.items():
        path = output / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(text, encoding='utf-8')
    target = output / spells.ARCHIVE; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(candidate, target)
    nhart.verify(target, output / spells.MANIFEST)
    if archive_hash != perks.digest(repo / spells.ARCHIVE) or manifest_hash != perks.digest(repo / spells.MANIFEST):
        raise ValueError('Baseline changed during preparation')
    result = {'perkCount': len(plan_rows), 'statusCount': len(status_records), 'addedResources': len(resources),
        'baselinePreserved': len(baseline['manifest']['entries']), 'entryCount': len(manifest['entries']),
        'sha256': perks.digest(target), 'files': {name: perks.digest(output / name) for name in [spells.ARCHIVE, *outputs]}}
    (output / 'READY.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--perk-handoff', type=Path, action='append', required=True)
    parser.add_argument('--status-handoff', type=Path, action='append')
    parser.add_argument('--mass-regeneration-scenario', action='store_true')
    parser.add_argument('--work-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.repo, args.perk_handoff, args.status_handoff, args.work_root, args.output_root, args.mass_regeneration_scenario), sort_keys=True))
    except OSError:
        parser.exit(1, 'Private input/output operation failed; repository unchanged\n')
    except (ValueError, KeyError, nhart.NHArtError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
