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


class ArtAdditionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.waves = later_art_waves()
        cls.manifest = json.loads((ROOT / importer.spells.MANIFEST).read_text())
        cls.header = (ROOT / importer.HEADER).read_text()
        cls.config = importer.spells.parse_jsonc((ROOT / importer.spells.CONFIG).read_text())

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
            declared = {name for row in wave['perks'] + wave['statuses'] for name in row['resources']}
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
            self.assertEqual(hashlib.sha256(header.encode()).hexdigest(), wave['baselineHeaderSha256'])
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
                self.assertEqual(importer.non_status_config_digest(config, [reused]), reused['nonGraphicsConfigSha256'])
                self.assertEqual(config[reused['id'].split(':')[1]]['graphics']['iconScenarioBonus'], 'NH_regeneration_32.png')
                self.assertEqual(open_image('SPRITES/NH_regeneration_32.png').size, (32, 32))

    def test_sanitized_provisional_records_and_no_loose_images(self):
        text = json.dumps(self.waves)
        self.assertNotRegex(text, r'(?i)/home/|/users/|[a-z]:[\\/]|\\\\')
        inventory = list(csv.DictReader(io.StringIO((ROOT / importer.INVENTORY).read_text())))
        for wave in self.waves:
            for row in wave['perks'] + wave['statuses']:
                self.assertIn('not Final' if row in wave['statuses'] else 'no Final', row['approval'])
                matches = [entry for entry in inventory if entry['Item / ID'].endswith('(' + row['id'] + ')')]
                self.assertEqual(len(matches), 1); self.assertEqual(matches[0]['Art'], 'Provisional')
            for name in wave['resources']:
                self.assertFalse((ROOT / 'Mods/new-horizons/Images' / name.removeprefix('SPRITES/')).exists())


if __name__ == '__main__':
    unittest.main()
