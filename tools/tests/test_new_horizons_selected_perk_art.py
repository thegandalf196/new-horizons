#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Selected semantic art, exact baseline preservation and container-only images."""
import hashlib
import json
from pathlib import Path
import re
import unittest

if __package__:
    from .nhart_test_resources import verified_index, read_resource, open_image
else:
    from nhart_test_resources import verified_index, read_resource, open_image

ROOT = Path(__file__).resolve().parents[2]


class SelectedPerkArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.selection = json.loads((ROOT / 'assets/new-horizons/perk-art-selections.json').read_text())
        cls.manifest = json.loads((ROOT / 'assets/new-horizons/runtime-art-manifest.json').read_text())

    def test_exact_semantic_bindings_and_native_export_hashes(self):
        rows = self.selection['entries']
        self.assertEqual(len(rows), 94)
        self.assertEqual(len({row['id'] for row in rows}), 94)
        bindings = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}',
                                  (ROOT / 'client/windows/NewHorizonsPerkIcons.h').read_text()))
        for row in rows:
            self.assertEqual(bindings[row['id']], row['icon44'])
            for size, key in ((44, 'native44Sha256'), (32, 'native32Sha256')):
                name = 'SPRITES/' + row['icon44'] + ('' if size == 44 else '_32') + '.png'
                self.assertEqual(hashlib.sha256(read_resource(name)).hexdigest(), row[key])
                self.assertEqual(open_image(name).size, (size, size))
            self.assertEqual(row['approval'], 'Provisional; no Final approval')

    def test_descriptor_states_and_exact_baseline_preservation(self):
        added = set()
        for row in self.selection['entries']:
            added.update(row['resources'])
            stem = row['icon44']
            descriptor = json.loads(read_resource('SPRITES/' + stem + '.json'))
            expected = [{'group': 0, 'frame': index, 'file': stem + '_' + state + '.png'}
                        for index, state in enumerate(('normal', 'pressed', 'disabled', 'highlighted'))]
            self.assertEqual(descriptor, {'images': expected})
            for frame in expected:
                self.assertEqual(open_image('SPRITES/' + frame['file']).size, (44, 44))
            self.assertEqual(open_image('SPRITES/' + stem + '_normal.png').convert('RGBA').tobytes(),
                             open_image('SPRITES/' + stem + '.png').convert('RGBA').tobytes())
        self.assertEqual(len(added), 658)
        self.assertTrue(added <= verified_index().keys())
        # Later explicitly selected spell families are not part of this batch's
        # historical baseline. Keep its exact provenance hash, not a looser count.
        spell_record = ROOT / 'assets/new-horizons/spell-art-selections.json'
        later = set()
        if spell_record.exists():
            spells = json.loads(spell_record.read_text())
            later = {'SPRITES/' + filename for row in spells['entries'] for filename in row['exports']}
            self.assertEqual(len(later), 51)
            self.assertFalse(later & added)
        baseline = sorted((row for row in self.manifest['entries'] if row['resource'] not in added | later),
                          key=lambda row: row['resource'].encode())
        self.assertEqual(len(baseline), 3137)
        self.assertEqual(hashlib.sha256(json.dumps(baseline, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
                         self.selection['baselineEntriesSha256'])
        self.assertEqual(len(self.manifest['entries']), 3795 + len(later))

    def test_public_provenance_has_no_private_paths_or_loose_runtime_images(self):
        text = json.dumps(self.selection)
        self.assertNotRegex(text, r'(?i)/home/|/users/|[a-z]:[\\/]|\\\\')
        for row in self.selection['entries']:
            for name in row['resources']:
                relative = name.removeprefix('SPRITES/')
                self.assertFalse((ROOT / 'Mods/new-horizons/Images' / relative).exists())


if __name__ == '__main__':
    unittest.main()
