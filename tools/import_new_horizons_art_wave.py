#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare an external additive perk/status/Mass-spell art wave, preserving earlier selections."""
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
import import_new_horizons_perk_art as perks
import import_new_horizons_spell_art as spells

LEDGER = 'assets/new-horizons/runtime-art-additions.json'
HEADER = 'client/windows/NewHorizonsPerkIcons.h'
INVENTORY = 'docs/NH_UI_ASSET_INVENTORY.csv'
PERK_RECORD = 'assets/new-horizons/perk-art-selections.json'
STATUS_NAME = 'NH_status_arcane_breach-30.png'
MASS_CONFIG = 'Mods/new-horizons/Content/config/spells/massVariants.json'
MASS_SPELLS = {'massBless': 'mass-bless', 'massCurse': 'mass-curse',
               'massSorrow': 'mass-sorrow', 'massSlow': 'mass-slow'}
SPELL_RECORD = 'assets/new-horizons/spell-art-selections.json'


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


def history_bytes(path):
    if path.is_symlink():
        raise ValueError('Baseline history must not be a symlink')
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return None


def require_virgin_perk_header(header):
    # This is the maintained literal map format, not a general C++ parser.
    # Recognize its complete initializer or refuse; zero regex hits must never
    # be mistaken for evidence of an empty/neutral selection map.
    header = re.sub(r'/\*.*?\*/|//[^\n]*', '', header, flags=re.DOTALL)
    maps = re.findall(r'static\s+const\s+std::map\s*<\s*std::string\s*,\s*std::string\s*>'
                      r'\s+icons\s*=\s*\{(.*?)\}\s*;', header, flags=re.DOTALL)
    entry = r'\{\s*"(new-horizons:[A-Za-z0-9_.-]+)"\s*,\s*"(NH_perk_[a-z0-9_]+)"\s*\}'
    if len(maps) != 1 or not re.fullmatch(r'\s*(?:' + entry + r'(?:\s*,\s*' + entry + r')*\s*,?)?\s*', maps[0]):
        raise ValueError('Cannot recognize complete baseline perk icons map for virgin bootstrap')
    if any(icon != 'NH_perk_neutral' for identity, icon in re.findall(entry, maps[0])):
        raise ValueError('Unrecorded selected perk history prevents virgin bootstrap')


def baseline_history(repo, archive_hash, entry_count, expected_sha256, bootstrap):
    if (expected_sha256 is not None) == bool(bootstrap):
        raise ValueError('Choose expected baseline ledger SHA256 or explicit no-ledger bootstrap')
    snapshots = {name: history_bytes(repo / name) for name in (LEDGER, PERK_RECORD)}
    initial = None
    if snapshots[PERK_RECORD] is not None:
        initial = json.loads(snapshots[PERK_RECORD])
        if (not isinstance(initial, dict) or not isinstance(initial.get('entries'), list)
                or type(initial.get('entryCount')) is not int or initial['entryCount'] < 0
                or not isinstance(initial.get('newArchiveSha256'), str)
                or not re.fullmatch('[0-9a-f]{64}', initial['newArchiveSha256'])):
            raise ValueError('Malformed initial selection proof')
    if bootstrap:
        if snapshots[LEDGER] is not None:
            raise ValueError('No-ledger bootstrap requires an absent additive ledger')
        if initial is not None:
            if initial['newArchiveSha256'] != archive_hash or initial['entryCount'] != entry_count:
                raise ValueError('Baseline advanced beyond initial selection proof; additive ledger required')
        else:
            header = (repo / HEADER).read_text(encoding='utf-8')
            require_virgin_perk_header(header)
        ledger = {'format': 1, 'waves': []}
    else:
        if not isinstance(expected_sha256, str) or not re.fullmatch('[0-9a-f]{64}', expected_sha256):
            raise ValueError('Invalid expected baseline ledger SHA256')
        raw = snapshots[LEDGER]
        if raw is None or hashlib.sha256(raw).hexdigest() != expected_sha256:
            raise ValueError('Missing or changed pinned baseline additive ledger')
        ledger = json.loads(raw)
        if (not isinstance(ledger, dict) or type(ledger.get('format')) is not int
                or ledger['format'] != 1 or not isinstance(ledger.get('waves'), list) or not ledger['waves']):
            raise ValueError('Malformed baseline additive ledger')
        if ledger['waves']:
            latest = ledger['waves'][-1]
            if (not isinstance(latest, dict) or latest.get('newArchiveSha256') != archive_hash
                    or type(latest.get('entryCount')) is not int or latest['entryCount'] != entry_count):
                raise ValueError('Additive ledger does not anchor the current baseline')
    return ledger, snapshots


def require_unchanged_history(repo, snapshots):
    if any(history_bytes(repo / name) != raw for name, raw in snapshots.items()):
        raise ValueError('Baseline history changed during preparation')


def mass_spell_inputs(selection, roots):
    """Read exact selected artist handoffs; no image transformation or staging."""
    if selection is None:
        return [], {}, {}
    if selection.is_symlink():
        raise ValueError('Selected Mass handoff must not be a symlink')
    reject_artist_overlap(roots, [selection.parent])
    raw = selection.read_bytes()
    hashes = {selection: hashlib.sha256(raw).hexdigest()}
    chosen = json.loads(raw)
    if type(chosen.get('count')) is not int or chosen['count'] != 4 or len(chosen['entries']) != 4:
        raise ValueError('Exactly four selected Mass spell identities required')
    records, sources, seen = [], {}, set()
    for entry in chosen['entries']:
        key = entry['id'].removeprefix('new-horizons:')
        if entry['id'] != 'new-horizons:' + key or key not in MASS_SPELLS or key in seen:
            raise ValueError('Unexpected or duplicate Mass spell identity')
        seen.add(key)
        directory = Path(entry['directory']).resolve()
        reject_artist_overlap(roots, [directory])
        handoff = Path(entry['handoff'])
        if handoff.is_symlink() or handoff.resolve() != directory / 'HANDOFF.json':
            raise ValueError('Selected Mass handoff escaped its directory')
        raw = handoff.read_bytes()
        if hashlib.sha256(raw).hexdigest() != entry['handoffSha256']:
            raise ValueError('Selected Mass handoff changed or escaped its directory')
        evidence = json.loads(raw)
        hashes[handoff] = hashlib.sha256(raw).hexdigest()
        if evidence['id'] != entry['id']:
            raise ValueError('Mass artist identity mismatch')
        exports = {row['file']: row for row in evidence['exports']}
        if len(exports) != len(evidence['exports']):
            raise ValueError('Duplicate Mass export filename')
        stem = 'NH_spell_' + MASS_SPELLS[key].replace('-', '_')
        bindings = {role: stem + '-' + str(size) + '.png' for role, size in spells.ROLES.items()}
        if entry['proposedBindings'] != bindings:
            raise ValueError('Mass roles must be the exact44/32/30 mapping')
        native = {}
        for size in (44, 32, 30):
            source = directory / (MASS_SPELLS[key] + '-' + str(size) + '.png')
            expected = exports[source.name]['sha256']
            if source.is_symlink():
                raise ValueError('Mass native symlink refused')
            raw = source.read_bytes()
            if hashlib.sha256(raw).hexdigest() != expected:
                raise ValueError('Pinned Mass native export changed')
            with Image.open(io.BytesIO(raw)) as image:
                if image.mode != 'RGBA' or image.size != (size, size) or image.getchannel('A').getextrema() != (0, 255):
                    raise ValueError('Incorrect Mass native dimensions or transparent RGBA')
            filename = stem + '-' + str(size) + '.png'
            sources[filename] = source
            native[filename] = {'sha256': expected, 'size': size}
            hashes[source] = expected
        references = {'master': directory / 'master.png', 'prompt': directory / 'PROMPT-AND-PROVENANCE.md',
                      'review': directory / 'REVIEW.md', 'comparison': directory / (MASS_SPELLS[key] + '-comparison.png'),
                      'exportManifest': directory / 'EXPORT-MANIFEST.json'}
        for path in references.values():
            if path.is_symlink() or not path.resolve().is_relative_to(directory):
                raise ValueError('Mass provenance path escaped selected directory')
        for role in ('master', 'comparison'):
            if perks.digest(references[role]) != exports[references[role].name]['sha256']:
                raise ValueError('Pinned Mass master/comparison changed')
        exported = json.loads(references['exportManifest'].read_bytes())
        manifest_exports = {row['file']: row['sha256'] for row in exported['outputs']}
        if len(manifest_exports) != len(exported['outputs']) or any(manifest_exports.get(name) != row['sha256'] for name, row in exports.items()):
            raise ValueError('Mass handoff/export manifest disagree')
        for path in [handoff, *references.values()]:
            if path.is_symlink() or not path.resolve().is_relative_to(directory):
                raise ValueError('Mass provenance path escaped selected directory')
            value = perks.digest(path)
            if path in hashes and hashes[path] != value:
                raise ValueError('Selected Mass inputs changed during validation')
            hashes[path] = value
        records.append({'id': entry['id'], 'config': MASS_CONFIG, 'bindings': bindings,
            'resources': ['SPRITES/' + name for name in native], 'exports': native,
            'handoffSha256': entry['handoffSha256'], 'selectionHandoffSha256': hashes[selection],
            **{role + 'Sha256': hashes[path] for role, path in references.items()},
            'origin': 'original-generated-purpose-made-mass-spell-symbol',
            'selection': 'user-authorized-provisional-runtime-2026-10-14',
            'approval': 'Provisional; not Final; rendered acceptance pending'})
    return records, sources, hashes


def prepare(repo, handoffs, status_handoff, work, output, mass_regeneration_scenario=False,
            *, expected_ledger_sha256=None, bootstrap_no_ledger=False, mass_spell_handoff=None):
    roots = [path.resolve() for path in (work, output)]
    if any(path.exists() or path.is_symlink() or path.resolve().is_relative_to(repo.resolve())
           for path in (work, output)) or roots[0].is_relative_to(roots[1]) or roots[1].is_relative_to(roots[0]):
        raise ValueError('Fresh nonoverlapping external preparation/output directories required')
    status_handoffs = [status_handoff] if isinstance(status_handoff, Path) else (status_handoff or [])
    if mass_spell_handoff is not None and (status_handoffs or mass_regeneration_scenario):
        raise ValueError('Prepare Mass selection separately from status/scenario-reuse inputs')
    reject_artist_overlap(roots, [path.parent for path in [*handoffs, *status_handoffs]])
    baseline = nhart.verify(repo / spells.ARCHIVE, repo / spells.MANIFEST)
    archive_hash, manifest_hash = perks.digest(repo / spells.ARCHIVE), perks.digest(repo / spells.MANIFEST)
    ledger, history = baseline_history(repo, archive_hash, len(baseline['manifest']['entries']),
                                       expected_ledger_sha256, bootstrap_no_ledger)
    # Metadata is a single baseline epoch too, not just the package payload.
    metadata = {name: (repo / name).read_bytes() for name in (spells.MANIFEST, HEADER, INVENTORY)}
    metadata.update(history)
    for name in (spells.CONFIG, MASS_CONFIG, 'config/newHorizonsPerks.json', SPELL_RECORD):
        metadata[name] = history_bytes(repo / name)
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
    if not plan_rows and mass_spell_handoff is None:
        raise ValueError('No completed perk inputs')
    mass_records, mass_sources, mass_hashes = mass_spell_inputs(mass_spell_handoff, roots)
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
            'handoffSha256': handoff_hashes[0] if handoff_hashes else perks.digest(mass_spell_handoff),
            'additiveHandoffSha256': handoff_hashes[-1] if handoff_hashes else perks.digest(mass_spell_handoff)}
    plan_path = work / 'private-plan.json'; plan_path.write_text(json.dumps(plan, indent=2), encoding='utf-8')
    # This maintained importer prepares only external outputs. Its temporary
    # standalone selection record is consumed below, never replacing the94 proof.
    perk_output, stage = work / 'perk-output', work / 'staging'
    if plan_rows:
        perks.import_art(repo, plan_path, stage, work / 'recovery', perk_output)
        manifest = json.loads((perk_output / spells.MANIFEST).read_text(encoding='utf-8'))
        perk_records = json.loads((perk_output / PERK_RECORD).read_text(encoding='utf-8'))['entries']
        header_text = (perk_output / HEADER).read_text(encoding='utf-8')
        inventory_text = (perk_output / INVENTORY).read_text(encoding='utf-8')
    else:
        # A Mass-only wave has no fabricated perk handoff/standalone record.
        recovery = work / 'recovery'; recovery.mkdir()
        for name in (spells.ARCHIVE, spells.MANIFEST):
            shutil.copyfile(repo / name, recovery / Path(name).name)
        stage.mkdir()
        manifest = json.loads(metadata[spells.MANIFEST])
        index = {row['resource']: row for row in baseline['entries']}
        with (repo / spells.ARCHIVE).open('rb') as stream:
            for row in manifest['entries']:
                target = stage / row['source']; target.parent.mkdir(parents=True, exist_ok=True)
                stream.seek(index[row['resource']]['offset'])
                target.write_bytes(stream.read(row['size']))
                if perks.digest(target) != row['sha256']:
                    raise ValueError('Mass-only baseline extraction changed bytes')
        perk_records = []
        header_text = metadata[HEADER].decode('utf-8')
        inventory_text = metadata[INVENTORY].decode('utf-8')
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
    if mass_records:
        mass_text = mass_text if mass_text is not None else (repo / MASS_CONFIG).read_text(encoding='utf-8')
        before_mass = spells.parse_jsonc(mass_text)
        identities = {nhart.resource_identity(row['resource']) for row in manifest['entries']}
        for row in mass_records:
            key = row['id'].split(':')[1]
            row['previousBindings'] = {role: before_mass[key]['graphics'][role] for role in row['bindings']}
            for filename, native in row['exports'].items():
                identity = nhart.resource_identity('SPRITES/' + filename)
                if identity in identities:
                    raise ValueError('Mass spell resource collision')
                identities.add(identity)
                target = stage / 'Mods/new-horizons/Images' / filename
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(mass_sources[filename], target)
                manifest['entries'].append({'resource': 'SPRITES/' + filename,
                    'source': 'Mods/new-horizons/Images/' + filename, 'size': target.stat().st_size,
                    'sha256': native['sha256'], 'family': 'builtin-skills-perks-orders-and-ui',
                    **{key: row[key] for key in ('origin', 'selection', 'approval')}})
            mass_text = spells.replace_graphics(mass_text, key, row['bindings'])
        if non_status_config_digest(before_mass, mass_records) != non_status_config_digest(spells.parse_jsonc(mass_text), mass_records):
            raise ValueError('Mass edit changed non-owned configuration')
    manifest['requiredFamilyCounts'] = {family: sum(row['family'] == family for row in manifest['entries'])
                                       for family in manifest['requiredFamilies']}
    candidate = work / 'candidate.nhart'; nhart.pack(manifest, stage, candidate)
    verified = nhart.verify(candidate, manifest)
    resulting = {row['resource']: row for row in verified['manifest']['entries']}
    if any(resulting[row['resource']] != row for row in baseline['manifest']['entries']):
        raise ValueError('Baseline resource or provenance changed')
    resources = sorted([name for row in perk_records + status_records + mass_records for name in row['resources']])
    ledger['waves'].append({'baselineArchiveSha256': archive_hash, 'baselineManifestSha256': manifest_hash,
        'baselineEntriesSha256': entries_digest(baseline['manifest']['entries']),
        'baselineHeaderSha256': perks.digest(repo / HEADER),
        'nonStatusConfigSha256': non_status_config_digest(spells.parse_jsonc(config_text), status_records),
        'baselineCount': len(baseline['manifest']['entries']), 'entryCount': len(manifest['entries']),
        'newArchiveSha256': perks.digest(candidate), 'resources': resources,
        'perks': perk_records, 'statuses': status_records, 'reusedBindings': reused,
        'approval': 'Provisional; not Final; rendered acceptance pending'})
    if mass_records:
        ledger['waves'][-1]['spells'] = mass_records
        ledger['waves'][-1]['massConfigNonGraphicsSha256'] = non_status_config_digest(spells.parse_jsonc(mass_text), mass_records)
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
    if mass_records:
        rows = list(csv.DictReader(io.StringIO(inventory_text)))
        for record in mass_records:
            matches = [row for row in rows if row['Item / ID'].endswith('(' + record['id'] + ')')]
            if len(matches) != 1:
                raise ValueError('Exactly one Mass spell inventory row required')
            matches[0]['Art'] = 'Provisional'
            matches[0]['Live binding / source'] = MASS_CONFIG + '; ' + '; '.join(sorted(record['exports'])) + '; selected NHART'
            matches[0]['Remaining work / evidence'] += ' Purpose-made Mass identity selected provisionally for five icon roles; gameplay, animation and sound unchanged. Native rendered acceptance and Final approval pending.'
        text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=list(rows[0]), lineterminator='\n', quoting=csv.QUOTE_ALL)
        writer.writeheader(); writer.writerows(rows); inventory_text = text.getvalue()
    outputs = {spells.MANIFEST: json.dumps(manifest, indent=2) + '\n',
        LEDGER: json.dumps(ledger, indent=2) + '\n', spells.CONFIG: config_text,
        HEADER: header_text, INVENTORY: inventory_text}
    if mass_text is not None:
        outputs[MASS_CONFIG] = mass_text
    require_unchanged_history(repo, metadata)
    if any(perks.digest(path) != value for path, value in mass_hashes.items()):
        raise ValueError('Selected Mass inputs changed during preparation')
    output.mkdir(parents=True)
    for name, text in outputs.items():
        path = output / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_text(text, encoding='utf-8')
    target = output / spells.ARCHIVE; target.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(candidate, target)
    nhart.verify(target, output / spells.MANIFEST)
    if archive_hash != perks.digest(repo / spells.ARCHIVE) or manifest_hash != perks.digest(repo / spells.MANIFEST):
        raise ValueError('Baseline changed during preparation')
    require_unchanged_history(repo, metadata)
    if any(perks.digest(path) != value for path, value in mass_hashes.items()):
        raise ValueError('Selected Mass inputs changed during preparation')
    result = {'perkCount': len(plan_rows), 'statusCount': len(status_records), 'addedResources': len(resources),
        'baselinePreserved': len(baseline['manifest']['entries']), 'entryCount': len(manifest['entries']),
        'baselineLedgerSha256': expected_ledger_sha256, 'bootstrapNoLedger': bootstrap_no_ledger,
        'sha256': perks.digest(target), 'files': {name: perks.digest(output / name) for name in [spells.ARCHIVE, *outputs]}}
    if mass_records:
        result['massSpellCount'] = len(mass_records)
    ready = output / '.READY.json.pending'
    ready.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    ready.replace(output / 'READY.json')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--perk-handoff', type=Path, action='append', default=[])
    parser.add_argument('--mass-spell-handoff', type=Path)
    parser.add_argument('--status-handoff', type=Path, action='append')
    parser.add_argument('--mass-regeneration-scenario', action='store_true')
    parser.add_argument('--work-root', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    baseline = parser.add_mutually_exclusive_group(required=True)
    baseline.add_argument('--expected-baseline-ledger-sha256', dest='expected_ledger_sha256')
    baseline.add_argument('--bootstrap-no-ledger', action='store_true',
                          help='Explicit first-wave/virgin baseline; initial selection proof must still anchor it')
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.repo, args.perk_handoff, args.status_handoff, args.work_root,
                                 args.output_root, args.mass_regeneration_scenario,
                                 expected_ledger_sha256=args.expected_ledger_sha256,
                                 bootstrap_no_ledger=args.bootstrap_no_ledger,
                                 mass_spell_handoff=args.mass_spell_handoff), sort_keys=True))
    except OSError:
        parser.exit(1, 'Private input/output operation failed; repository unchanged\n')
    except (ValueError, KeyError, nhart.NHArtError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
