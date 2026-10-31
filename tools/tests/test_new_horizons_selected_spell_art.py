"""Validate the selected seventeen-family package without private art inputs."""
import csv
import hashlib
import io
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import import_new_horizons_spell_art as importer
from .nhart_test_resources import (open_image, read_resource, verified_index,
                                  later_art_resources, later_art_waves,
                                  entries_before_wave, resource_before_wave)
from .nhart_config_revisions import config_at_revision


class SelectedSpellArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.selection = json.loads((ROOT / 'assets/new-horizons/spell-art-selections.json').read_text())
        cls.manifest = json.loads((ROOT / importer.MANIFEST).read_text())
        cls.config = importer.parse_jsonc((ROOT / importer.CONFIG).read_text())
        cls.configProofs = json.loads((ROOT / 'assets/new-horizons/runtime-art-config-revisions.json').read_text())

    def test_exact_semantic_roster_and_five_native_roles(self):
        entries = self.selection['entries']
        self.assertEqual({row['id'] for row in entries}, {'new-horizons:' + key for key in importer.SPELLS})
        self.assertEqual(len(entries), 17)
        exports = set()
        for row in entries:
            key = row['id'].split(':')[1]
            self.assertEqual(self.config[key]['graphics'], row['bindings'])
            self.assertEqual(set(row['bindings']), set(importer.ROLES))
            self.assertEqual(len(row['exports']), 3)
            for role, size in importer.ROLES.items():
                filename = row['bindings'][role]
                exports.add(filename)
                payload = read_resource('SPRITES/' + filename)
                self.assertEqual(hashlib.sha256(payload).hexdigest(), row['exports'][filename]['sha256'])
                image = open_image('SPRITES/' + filename)
                self.assertEqual(image.mode, 'RGBA'); self.assertEqual(image.size, (size, size))
                self.assertEqual(image.getchannel('A').getextrema(), (0, 255))
                self.assertFalse((ROOT / 'Mods/new-horizons/Images' / filename).exists())
        self.assertEqual(len(exports), 51)

    def test_baseline_payload_and_provenance_preservation(self):
        new = {'SPRITES/' + filename for row in self.selection['entries'] for filename in row['exports']}
        later = later_art_resources()
        self.assertFalse(later & new)
        waves = later_art_waves()
        historical = entries_before_wave(self.manifest['entries'], waves, 0)
        baseline = sorted((row for row in historical if row['resource'] not in new | later),
                          key=lambda row: row['resource'].encode())
        self.assertEqual(len(baseline), 3795)
        self.assertEqual(len(verified_index()), self.selection['entryCount'] + len(later))
        digest = hashlib.sha256(json.dumps(baseline, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(digest, self.selection['baselineEntriesSha256'])
        for row in baseline:
            self.assertEqual(hashlib.sha256(resource_before_wave(row['resource'], waves, 0)).hexdigest(), row['sha256'])
        if waves:
            self.assertEqual(waves[0]['baselineArchiveSha256'], self.selection['newArchiveSha256'])
        else:
            self.assertEqual(importer.digest(ROOT / importer.ARCHIVE), self.selection['newArchiveSha256'])

    def test_graphics_only_and_existing_scenario_byte_reuse(self):
        # Validate the current accepted gameplay capture, then inspect the art-import revision.
        historical = config_at_revision(self.config, importer.CONFIG, self.configProofs, 0)
        for wave in reversed(later_art_waves()):
            for status in wave['statuses']:
                historical[status['id'].split(':')[1]]['graphics'].update(status['previousBindings'])
        self.assertEqual(importer.non_graphics_digest(historical), self.selection['nonGraphicsConfigSha256'])
        for key in ('holyWrath', 'regeneration'):
            filename = 'NH_' + key + '_32.png'
            self.assertEqual(self.config[key]['graphics']['iconScenarioBonus'], filename)
            self.assertEqual(open_image('SPRITES/' + filename).size, (32, 32))
        rows = list(csv.DictReader(io.StringIO((ROOT / 'docs/NH_UI_ASSET_INVENTORY.csv').read_text())))
        for selection in self.selection['entries']:
            matches = [row for row in rows if row['Item / ID'].endswith('(' + selection['id'] + ')')]
            self.assertEqual(len(matches), 1); self.assertEqual(matches[0]['Art'], 'Provisional')

    def test_sanitized_provenance_and_no_final_claim(self):
        text = json.dumps(self.selection)
        self.assertNotIn('/home/', text); self.assertNotIn('Downloads/', text)
        for row in self.selection['entries']:
            for role in ('master', 'prompt', 'preview', 'handoff'):
                self.assertRegex(row[role + 'Sha256'], r'^[0-9a-f]{64}$')
            self.assertIn('not Final', row['approval'])
        self.assertNotIn('superseded-convergence-clipped', text)


if __name__ == '__main__':
    unittest.main()
