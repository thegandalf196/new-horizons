"""Explicit additive-wave proof; no global current-package/perk-count freeze."""
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import import_new_horizons_art_wave as importer
from .nhart_test_resources import later_art_waves, later_art_resources, read_resource, open_image, verified_index


def config_at_wave(config, waves, index, config_path):
    """Undo later graphics edits only; historical nongraphics bytes stay checked."""
    historical = json.loads(json.dumps(config))
    for later in reversed(waves[index + 1:]):
        # Producer applies reused roles before new spell roles within a wave.
        edits = later.get('reusedBindings', []) + later.get('spells', [])
        for row in reversed(edits):
            if row['config'] == config_path:
                historical[row['id'].split(':')[1]]['graphics'].update(row['previousBindings'])
    return historical


def perk_header_history_forms(header):
    """Retain exact hashes across the approved inline-map/table extraction."""
    if 'newHorizonsPerkIconBindings' not in header:
        return (header,)
    # Undo only the known syntactic extraction. Binding bytes/order and the
    # fallback/lookup body remain untouched, so their edits still break history.
    function = 'inline const std::string & newHorizonsPerkIcon(const std::string & perkId)\n{\n'
    replacements = (
        ('#include <iterator>\n', ''),
        ('#include <utility>\n', ''),
        ('// Constant binding data; the runtime map remains lazy below.\n'
         'inline constexpr std::pair<const char *, const char *> newHorizonsPerkIconBindings[] = {\n',
         function + '\tstatic const std::map<std::string, std::string> icons = {\n'),
        ('};\n\n' + function + '\tstatic const std::map<std::string, std::string> icons(\n'
         '\t\tstd::begin(newHorizonsPerkIconBindings), std::end(newHorizonsPerkIconBindings));\n',
         '\t};\n'),
    )
    historical = header
    for current, previous in replacements:
        if historical.count(current) != 1:
            raise ValueError('Unrecognized perk icon table extraction')
        historical = historical.replace(current, previous, 1)
    return header, historical


class PerkHeaderHistoryTest(unittest.TestCase):
    inline = '''#pragma once

#include <map>
#include <string>

inline const std::string & newHorizonsPerkIcon(const std::string & perkId)
{
	static const std::map<std::string, std::string> icons = {
		{"new-horizons:fixture.first", "NH_first"},
		{"new-horizons:fixture.second", "NH_second"},
	};
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}
'''
    table = '''#pragma once

#include <iterator>
#include <map>
#include <string>
#include <utility>

// Constant binding data; the runtime map remains lazy below.
inline constexpr std::pair<const char *, const char *> newHorizonsPerkIconBindings[] = {
		{"new-horizons:fixture.first", "NH_first"},
		{"new-horizons:fixture.second", "NH_second"},
};

inline const std::string & newHorizonsPerkIcon(const std::string & perkId)
{
	static const std::map<std::string, std::string> icons(
		std::begin(newHorizonsPerkIconBindings), std::end(newHorizonsPerkIconBindings));
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}
'''

    def test_both_recorded_layouts_retain_their_exact_bytes(self):
        self.assertEqual(perk_header_history_forms(self.inline), (self.inline,))
        self.assertEqual(perk_header_history_forms(self.table), (self.table, self.inline))

    def test_bindings_order_fallback_and_lookup_changes_still_break_history(self):
        first = '\t\t{"new-horizons:fixture.first", "NH_first"},\n'
        second = '\t\t{"new-horizons:fixture.second", "NH_second"},\n'
        recorded = {hashlib.sha256(text.encode()).hexdigest() for text in (self.inline, self.table)}
        mutations = (self.table.replace('NH_first', 'NH_changed'),
                     self.table.replace(first + second, second + first),
                     self.table.replace('NH_perk_neutral', 'NH_changed_fallback'),
                     self.table.replace('found == icons.end()', 'found != icons.end()'),
                     '// unrelated change\n' + self.table)
        for changed in mutations:
            with self.subTest(header=changed):
                actual = {hashlib.sha256(text.encode()).hexdigest()
                          for text in perk_header_history_forms(changed)}
                self.assertTrue(recorded.isdisjoint(actual))

    def test_unknown_or_duplicate_extraction_syntax_is_rejected(self):
        for changed in (self.table.replace('std::begin(', 'std::cbegin('),
                        '#include <iterator>\n' + self.table):
            with self.subTest(header=changed):
                with self.assertRaisesRegex(ValueError, 'Unrecognized perk icon table extraction'):
                    perk_header_history_forms(changed)


class ArtWaveHistoryTest(unittest.TestCase):
    def test_mass_regeneration_proof_survives_later_mass_wave_without_loosening_digest(self):
        original = {key: {'power': 7, 'graphics': {role: key + '-' + role
                    for role in importer.spells.ROLES}}
                    for key in (*importer.MASS_SPELLS, 'massRegeneration')}
        reused = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massRegeneration',
                  'bindings': {'iconScenarioBonus': 'NH_regeneration_32.png'},
                  'previousBindings': {'iconScenarioBonus': original['massRegeneration']['graphics']['iconScenarioBonus']}}
        current = json.loads(json.dumps(original))
        current['massRegeneration']['graphics'].update(reused['bindings'])
        old_digest = importer.non_status_config_digest(current, [reused])
        records = []
        for key in importer.MASS_SPELLS:
            row = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:' + key,
                   'previousBindings': dict(current[key]['graphics']),
                   'bindings': {role: 'selected-' + key + '-' + role for role in importer.spells.ROLES}}
            current[key]['graphics'].update(row['bindings']); records.append(row)
        waves = [{'reusedBindings': [reused]}, {'spells': records}]
        self.assertNotEqual(importer.non_status_config_digest(current, [reused]), old_digest)
        historical = config_at_wave(current, waves, 0, importer.MASS_CONFIG)
        self.assertEqual(importer.non_status_config_digest(historical, [reused]), old_digest)
        self.assertEqual(current['massBless']['graphics'], records[0]['bindings'])
        current['massBless']['power'] += 1
        mutated = config_at_wave(current, waves, 0, importer.MASS_CONFIG)
        self.assertNotEqual(importer.non_status_config_digest(mutated, [reused]), old_digest)

    def test_rollback_includes_later_same_config_reused_roles_in_reverse_order(self):
        base = {'massBless': {'power': 7, 'graphics': {'iconBook': 'original'}}}
        spell = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massBless',
                 'previousBindings': {'iconBook': 'original'}}
        reused = {'config': importer.MASS_CONFIG, 'id': 'new-horizons:massBless',
                  'previousBindings': {'iconBook': 'first-spell'}}
        unrelated = {'config': 'other.json', 'id': 'new-horizons:absent',
                     'previousBindings': {'iconBook': 'must-not-apply'}}
        current = {'massBless': {'power': 7, 'graphics': {'iconBook': 'latest'}}}
        waves = [{}, {'spells': [spell]}, {'reusedBindings': [reused, unrelated]}]
        self.assertEqual(config_at_wave(current, waves, 0, importer.MASS_CONFIG), base)
        self.assertEqual(config_at_wave(current, waves, 1, importer.MASS_CONFIG),
                         {'massBless': {'power': 7, 'graphics': {'iconBook': 'first-spell'}}})


class ArtAdditionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.waves = later_art_waves()
        cls.manifest = json.loads((ROOT / importer.spells.MANIFEST).read_text())
        cls.header = (ROOT / importer.HEADER).read_text()
        cls.config = importer.spells.parse_jsonc((ROOT / importer.spells.CONFIG).read_text())

    def test_public_atlas_recipes_keep_bounded_geometry_without_authoring_prompts(self):
        families = [('active-perks-v1', [6, 6, 6, 6], [1536, 1024]),
                    ('sylvan-luck-perks-v1', [3], [2172, 724]),
                    ('sylvan-luck-perks-v2', [4], [2172, 724])]
        for family, counts, dimensions in families:
            with self.subTest(family=family):
                recipe = json.loads((ROOT / 'assets/new-horizons/art-source' / family / 'generation.json').read_text())
                self.assertFalse('promptSummary' in recipe, 'Public recipe must omit authoring summaries')
                self.assertEqual(recipe['license'], 'CC0-1.0')
                atlases = recipe.get('atlases', [recipe])
                self.assertEqual([len(atlas['cells']) for atlas in atlases], counts)
                keys = []
                for atlas in atlases:
                    self.assertFalse('prompt' in atlas, 'Public recipe must omit raw authoring prompts')
                    self.assertEqual(atlas['dimensions'], dimensions)
                    self.assertTrue(atlas.get('file', recipe.get('atlas', '')).endswith('.png'))
                    for cell in atlas['cells']:
                        self.assertTrue(cell['key'].startswith('NH_'))
                        keys.append(cell['key'])
                        if 'atlases' in recipe:
                            self.assertTrue(cell['id'])
                        self.assertEqual(len(cell['box']), 4)
                        self.assertTrue(all(type(value) is int for value in cell['box']))
                        left, top, right, bottom = cell['box']
                        self.assertTrue(0 <= left < right <= dimensions[0])
                        self.assertTrue(0 <= top < bottom <= dimensions[1])
                self.assertEqual(len(keys), len(set(keys)))

    def test_each_wave_has_exact_baseline_payload_and_provenance_proof(self):
        self.assertTrue(self.waves)
        self.assertEqual(len(later_art_resources()), sum(len(wave['resources']) for wave in self.waves))
        for index, wave in enumerate(self.waves):
            remove = {resource for later in self.waves[index:] for resource in later['resources']}
            baseline = [row for row in self.manifest['entries'] if row['resource'] not in remove]
            self.assertEqual(len(baseline), wave['baselineCount'])
            self.assertEqual(importer.entries_digest(baseline), wave['baselineEntriesSha256'])
            self.assertEqual(wave['entryCount'], wave['baselineCount'] + len(wave['resources']))
            for row in baseline:
                self.assertEqual(hashlib.sha256(read_resource(row['resource'])).hexdigest(), row['sha256'])
            if index:
                self.assertEqual(wave['baselineArchiveSha256'], self.waves[index - 1]['newArchiveSha256'])
        self.assertEqual(len(verified_index()), self.waves[-1]['entryCount'])
        self.assertEqual(importer.perks.digest(ROOT / importer.spells.ARCHIVE), self.waves[-1]['newArchiveSha256'])

    def test_exact_perk_bindings_and_native_families(self):
        lookup = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', self.header))
        roster = json.loads((ROOT / 'config/newHorizonsPerks.json').read_text())
        current_ids = {row['id'] for skill in roster['skills'].values() for row in skill['perks']}
        for wave in self.waves:
            declared = {name for row in wave['perks'] + wave['statuses'] + wave.get('spells', []) for name in row['resources']}
            self.assertEqual(declared, set(wave['resources']))
            for row in wave['perks']:
                self.assertIn(row['id'], current_ids); self.assertEqual(lookup[row['id']], row['icon44'])
                self.assertEqual(len(row['resources']), 7)
                stem = row['icon44']
                for size in (44, 32):
                    filename = stem + ('' if size == 44 else '_32') + '.png'
                    self.assertEqual(open_image('SPRITES/' + filename).size, (size, size))
                    self.assertEqual(hashlib.sha256(read_resource('SPRITES/' + filename)).hexdigest(), row['native' + str(size) + 'Sha256'])
                frames = json.loads(read_resource('SPRITES/' + stem + '.json'))['images']
                self.assertEqual(frames, [{'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                    for index, state in enumerate(('normal', 'pressed', 'disabled', 'highlighted'))])
                for frame in frames:
                    self.assertEqual(open_image('SPRITES/' + frame['file']).size, (44, 44))

    def test_header_baseline_and_status_only_graphics(self):
        for index, wave in enumerate(self.waves):
            header = self.header
            for later in self.waves[index:]:
                for row in later['perks']:
                    line = '\t\t{"' + row['id'] + '", "' + row['icon44'] + '"},\n'
                    self.assertEqual(header.count(line), 1); header = header.replace(line, '')
            self.assertIn(wave['baselineHeaderSha256'], {
                hashlib.sha256(form.encode()).hexdigest() for form in perk_header_history_forms(header)})
            historical = json.loads(json.dumps(self.config))
            for later in reversed(self.waves[index + 1:]):
                for status in later['statuses']:
                    historical[status['id'].split(':')[1]]['graphics'].update(status['previousBindings'])
            self.assertEqual(importer.non_status_config_digest(historical, wave['statuses']), wave['nonStatusConfigSha256'])
            for status in wave['statuses']:
                self.assertFalse(status['castable']); self.assertIn(status['id'], {'new-horizons:arcaneBreach', 'new-horizons:lucidity'})
                self.assertEqual(set(status['bindings']), {'iconEffect', 'iconImmune'})
                self.assertEqual(len(status['resources']), 1)
                for role, filename in status['bindings'].items():
                    self.assertEqual(self.config[status['id'].split(':')[1]]['graphics'][role], filename)
                    image = open_image('SPRITES/' + filename)
                    self.assertEqual(image.mode, 'RGBA'); self.assertEqual(image.size, (30, 30))
                    self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
                    self.assertEqual(hashlib.sha256(read_resource('SPRITES/' + filename)).hexdigest(), status['native30Sha256'])
            for reused in wave['reusedBindings']:
                config = importer.spells.parse_jsonc((ROOT / reused['config']).read_text())
                historical_config = config_at_wave(config, self.waves, index, reused['config'])
                self.assertEqual(importer.non_status_config_digest(historical_config, [reused]), reused['nonGraphicsConfigSha256'])
                self.assertEqual(historical_config[reused['id'].split(':')[1]]['graphics']['iconScenarioBonus'], 'NH_regeneration_32.png')
                self.assertEqual(open_image('SPRITES/NH_regeneration_32.png').size, (32, 32))

    def test_sanitized_provisional_records_and_no_loose_images(self):
        text = json.dumps(self.waves)
        self.assertNotRegex(text, r'(?i)/home/|/users/|[a-z]:[\\/]|\\\\')
        inventory = list(csv.DictReader(io.StringIO((ROOT / importer.INVENTORY).read_text())))
        for wave in self.waves:
            for row in wave['perks'] + wave['statuses'] + wave.get('spells', []):
                self.assertIn('no Final' if row in wave['perks'] else 'not Final', row['approval'])
                matches = [entry for entry in inventory if entry['Item / ID'].endswith('(' + row['id'] + ')')]
                self.assertEqual(len(matches), 1); self.assertEqual(matches[0]['Art'], 'Provisional')
            for name in wave['resources']:
                self.assertFalse((ROOT / 'Mods/new-horizons/Images' / name.removeprefix('SPRITES/')).exists())

    def test_optional_mass_spell_records_have_exact_roles_payloads_and_provenance(self):
        for index, wave in enumerate(self.waves):
            if 'spells' not in wave:
                continue # Historical format1 waves retain their original shape.
            records = wave['spells']
            self.assertEqual(len(records), 4)
            self.assertEqual({row['id'] for row in records},
                             {'new-horizons:' + key for key in importer.MASS_SPELLS})
            config = importer.spells.parse_jsonc((ROOT / importer.MASS_CONFIG).read_text())
            config = config_at_wave(config, self.waves, index, importer.MASS_CONFIG)
            self.assertEqual(importer.non_status_config_digest(config, records), wave['massConfigNonGraphicsSha256'])
            for row in records:
                key = row['id'].split(':')[1]
                stem = 'NH_spell_' + importer.MASS_SPELLS[key].replace('-', '_')
                expected = {role: stem + '-' + str(size) + '.png'
                            for role, size in importer.spells.ROLES.items()}
                self.assertEqual(row['config'], importer.MASS_CONFIG)
                self.assertEqual(row['bindings'], expected)
                self.assertEqual(set(row['previousBindings']), set(expected))
                self.assertEqual(config[key]['graphics'], expected)
                self.assertEqual(set(row['exports']), set(expected.values()))
                self.assertEqual(set(row['resources']), {'SPRITES/' + name for name in expected.values()})
                for filename, metadata in row['exports'].items():
                    size = metadata['size']; self.assertIn(size, (44, 32, 30))
                    resource = 'SPRITES/' + filename
                    self.assertEqual(hashlib.sha256(read_resource(resource)).hexdigest(), metadata['sha256'])
                    image = open_image(resource)
                    self.assertEqual(image.size, (size, size)); self.assertEqual(image.mode, 'RGBA')
                    self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
                for field in ('handoffSha256', 'selectionHandoffSha256', 'masterSha256', 'promptSha256',
                              'reviewSha256', 'comparisonSha256', 'exportManifestSha256'):
                    self.assertRegex(row[field], r'^[0-9a-f]{64}$')


if __name__ == '__main__':
    unittest.main()
