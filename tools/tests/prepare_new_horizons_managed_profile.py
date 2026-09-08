#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose pinned, private managed70 TEST data. Never build or launch an executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = 'tools/tests/fixtures/new-horizons-managed-inputs.json'
MANIFEST_SHA256 = 'a59d2f17784357ed98e1508dee3db96b3386742709bce6956bbc8b0681027bce'
SPELL = 'tools/tests/fixtures/new-horizons-magic-missile.json'
SPELL_SHA256 = '4034a8677f6d2da986adcd066b9cca99d95e7b73a27b8e39363985726868e023'
CATEGORY_DESCRIPTION = (
    ' Private partial-Conflux creature classification preview: Pixies/Sprites Core,'
    ' five elemental pairs Elite, Phoenix and provisionally Firebird Champion.'
    ' Existing recruitment, upgrades, statistics, capacity and AI values are unchanged;'
    ' other creatures are deliberately unmapped. Not a complete roster redesign.')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key: ' + key)
        result[key] = value
    return result


def decode(data):
    return json.loads(data, object_pairs_hook=unique_object)


def encode(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def checked_source(root, relative, expected_hash, expected_size=None):
    name = PurePosixPath(relative)
    require(not name.is_absolute() and '..' not in name.parts and '\\' not in relative,
            'Unsafe source path')
    path = root
    for part in name.parts:
        path = path / part
        require(not path.is_symlink(), 'Source symlink rejected: ' + relative)
    require(path.is_file(), 'Missing pinned source: ' + relative)
    # Bound reads; retain the checked bytes rather than rereading mutable inputs.
    with path.open('rb') as stream:
        require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), 'Nonregular source')
        data = stream.read((expected_size + 1) if expected_size is not None else 2_000_000)
    if expected_size is not None:
        require(len(data) == expected_size, 'Source size changed: ' + relative)
    require(digest(data) == expected_hash, 'Pinned source changed: ' + relative)
    require(not data.startswith((b'\x7fELF', b'MZ')), 'Executable input rejected')
    return data


def checked_output(root, output, purchaser_data):
    require(output.is_absolute() and purchaser_data.is_absolute(), 'Use absolute paths')
    require('..' not in output.parts, 'Output traversal rejected')
    require(output.is_relative_to(root / 'build') and output != root / 'build',
            'Output must be a new directory below this checkout build/')
    for path in [output, *output.parents]:
        require(not path.is_symlink(), 'Output symlink rejected')
    require(not output.exists(), 'Never reuse or overwrite an output directory')
    require(purchaser_data.is_dir(), 'Purchaser Data directory is missing')
    data = purchaser_data.resolve(strict=True)
    require(not output.is_relative_to(data) and not data.is_relative_to(output),
            'Output and purchaser Data must not overlap')
    return data


def load_inputs(root):
    manifest_bytes = checked_source(root, MANIFEST, MANIFEST_SHA256)
    manifest = decode(manifest_bytes)
    player, tests = manifest['player_resources'], manifest['test_resources']
    require(len(player) == len(set(player)) == 945 and len(tests) == len(set(tests)) == 18,
            'Unexpected pinned resource keyset')
    require(not set(player) & set(tests) and set(manifest['files']) == set(player) | set(tests),
            'Unexpected input membership')
    require(all(n.startswith(('config/', 'scripts/', 'Mods/vcmi/', 'Mods/new-horizons/'))
                for n in player), 'Unexpected player resource scope')
    require(all(n.startswith('test/testdata/vcmi-test/') for n in tests),
            'Unexpected test resource scope')
    bodies = {name: checked_source(root, name, item['sha256'], item['size'])
              for name, item in manifest['files'].items()}
    spell = checked_source(root, SPELL, SPELL_SHA256)
    return manifest, bodies, spell


def compose(bodies, spell_bytes):
    result = dict(bodies)
    module = decode(bodies['Mods/new-horizons/mod.json'])
    require(module['version'] == '0.5.1' and 'spells' not in module
            and not module['settings'].get('creatures'), 'Unexpected default module')
    rules = module['settings']['magic']['newHorizons']
    require(rules['rulesetVersion'] == 1 and len(rules['spells']) == 69
            and 'new-horizons:magicMissile' not in rules['spells'], 'Unexpected baseline roster')
    categories = decode(bodies['config/newHorizonsCreatureCategories.json'])
    texts = decode(bodies['config/newHorizonsCreatureCategoryTexts.json'])
    require(not module['translations'].keys() & texts.keys(), 'Translation collision')
    spells = decode(spell_bytes)
    require(set(spells) == {'magicMissile'}, 'Unexpected fixture spell keyset')
    missile = spells['magicMissile']
    require(missile['level'] == 1 and missile['school'] == {'new-horizons:sorcery': True},
            'Unexpected fixture school/level')
    require(all(missile['levels'][rank]['cost'] == 5
                for rank in ('none', 'basic', 'advanced', 'expert')), 'Unexpected costs')
    module['settings']['creatures'] = {'newHorizonsCategories': categories}
    module['translations'].update(texts)
    module['version'] = '0.7.0'
    module['description'] += CATEGORY_DESCRIPTION + ' PRIVATE MANAGED70 NATIVE VALIDATION ONLY; not a public/default activation.'
    module['spells'] = spells
    rules['rulesetVersion'] = 2
    rules['spells']['new-horizons:magicMissile'] = {
        'schools': ['new-horizons:sorcery'], 'level': 1, 'costs': [5, 5, 5, 5],
        'directDamage': {'base': 20, 'powerCoefficient': 20}}
    result['Mods/new-horizons/mod.json'] = encode(module)
    wrapper = bodies['config/schemas/gameSettings.json']
    old = b'"newHorizons": { "$ref": "newHorizonsMagic.json" }'
    require(wrapper.count(old) == 1, 'Unexpected schema wrapper seam')
    result['config/schemas/gameSettings.json'] = wrapper.replace(
        old, b'"newHorizons": { "anyOf": [{ "$ref": "newHorizonsMagic.json" }, { "$ref": "newHorizonsMagicV2.json" }] }')
    require({n for n in result if result[n] != bodies[n]} ==
            {'Mods/new-horizons/mod.json', 'config/schemas/gameSettings.json'},
            'Unexpected resource transformation')
    return result


def prepare(root, output, purchaser_data):
    data = checked_output(root, output, purchaser_data)
    manifest, original, spell = load_inputs(root)
    bodies = compose(original, spell)
    # All input validation precedes creation. A partial failure is retained, never retried in place.
    output.parent.mkdir(parents=True, exist_ok=True)
    checked_output(root, output, purchaser_data)
    output.mkdir(mode=0o700)
    records = {}
    for name, body in bodies.items():
        destination = ('Mods/vcmi-test/' + name.removeprefix('test/testdata/vcmi-test/')
                       if name in manifest['test_resources'] else name)
        relative = 'runner/' + destination
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(body)
        records[relative] = {'sha256': digest(body), 'size': len(body)}
    (output / 'runner/Data').symlink_to(data, target_is_directory=True)
    folder = output / 'config/vcmi'
    folder.mkdir(parents=True)
    preset = {'activePreset': 'managed70-tests', 'presets': {'managed70-tests': {
        'mods': ['core', 'vcmi', 'vcmi-test', 'new-horizons'], 'settings': {}}}, 'validatedMods': {}}
    (folder / 'testModSettings.json').write_bytes(encode(preset))
    (folder / 'settings.json').write_bytes(encode({'video': {'performanceOverlay': {}, 'resolution': {}}}))
    for path in folder.iterdir():
        body = path.read_bytes()
        records[str(path.relative_to(output))] = {'sha256': digest(body), 'size': len(body)}
    records['runner/Data'] = {'symlink': str(data)}
    identity = {'scope': 'PRIVATE managed70 test DATA only; no executable, compiler, native or GUI execution',
                'input_resource_commit': manifest['source_commit'], 'input_manifest_sha256': MANIFEST_SHA256,
                'composer_sha256': digest(Path(__file__).read_bytes()),
                'fixture_sha256': SPELL_SHA256, 'player_resource_count': 945, 'test_resource_count': 18,
                'files': records, 'purchaser_data_link': str(data),
                'bootstrap': 'Default vcmitest TEST bootstrap forcibly activates vcmi-test. This is not a player profile or normal client bootstrap.',
                'required_environment': {'NH_REQUIRE_MANAGED_MISSILE_PROFILE': '1', 'NH_REQUIRE_MASTERY_TEXTS': '1'}}
    (output / 'PROFILE-IDENTITY.json').write_bytes(encode(identity))
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--data', required=True, type=Path)
    parser.add_argument('--permit-private-test-import', required=True, action='store_true')
    args = parser.parse_args()
    try:
        result = prepare(ROOT, args.out, args.data)
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))
    print(json.dumps({'output': str(args.out), 'player_resources': result['player_resource_count'],
                      'scope': result['scope']}, indent=2))


if __name__ == '__main__':
    main()
