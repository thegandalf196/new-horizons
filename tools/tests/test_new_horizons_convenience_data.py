#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent-authored presentation data shape; not loading/GUI acceptance."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_new_horizons_content import ROOT, load


class ConvenienceDataTest(unittest.TestCase):
    def test_fragment_only_adds_two_namespaced_existing_actions(self):
        fragment = load('Mods/new-horizons/Content/config/widgets/nhConvenience.json')
        self.assertEqual(set(fragment), {'items'})
        self.assertEqual(len(fragment['items']), 1)
        container = fragment['items'][0]
        self.assertEqual(container['name'], 'nhConvenienceControls')
        self.assertEqual(container['hideWhen'], 'worldViewMode')
        self.assertEqual(container['area'], {'top': 171, 'right': 69, 'width': 56, 'height': 24})
        buttons = container['items']
        self.assertEqual(len(buttons), 2)
        for button, action, image, help_key, left in zip(
                buttons, ('adventureQuickSave', 'adventureQuickLoad'),
                ('NH_qsave_24', 'NH_qload_24'), ('quickSave', 'quickLoad'), (0, 32)):
            self.assertTrue(button['name'].startswith('nhButton'))
            self.assertEqual(button['hotkey'], action)
            self.assertEqual(button['image'], image)
            self.assertEqual(button['help'], 'vcmi.adventureMap.' + help_key)
            self.assertFalse(button['playerColored'])
            self.assertEqual(button['area'], {'top': 0, 'left': left, 'width': 24, 'height': 24})
        self.assertLessEqual(32 + 24, container['area']['width'])
        self.assertLessEqual(171 + 24, 196)  # Existing landscape lists start here.

    def test_bonus_patches_change_only_icons_and_explicit_siege_text(self):
        patches = load('config/newHorizonsConvenienceBonuses.json')
        expected = {'UNDEAD': 'undead', 'FLYING': 'flying', 'SHOOTER': 'shooter',
                    'SIEGE_WEAPON': 'siege', 'BLOCKS_RETALIATION': 'noRetaliation',
                    'UNLIMITED_RETALIATIONS': 'unlimitedRetaliations',
                    'TWO_HEX_ATTACK_BREATH': 'breath', 'ATTACKS_ALL_ADJACENT': 'adjacent',
                    'MAGIC_RESISTANCE': 'resistance', 'HP_REGENERATION': 'regeneration'}
        self.assertEqual(set(patches), {'core:' + key for key in expected})
        core = load('config/bonuses.json')
        for key, stem in expected.items():
            self.assertIn(key, core)
            patch = patches['core:' + key]
            expected_patch = {'graphics': {'icon': 'NH_status_' + stem + '_50'}}
            if key == 'SIEGE_WEAPON':
                expected_patch['description'] = '{Siege Weapon}\nThis unit has the siege-weapon trait.'
            self.assertEqual(patch, expected_patch)
            # Model the intended additive overlay; actual engine merge needs its
            # separate native gate, not an inference from this Python check.
            merged = copy.deepcopy(core[key])
            merged.setdefault('graphics', {}).update(patch['graphics'])
            for field, value in core[key].items():
                if field != 'graphics':
                    self.assertEqual(merged[field], value)
        self.assertTrue(core['UNDEAD']['creatureNature'])
        self.assertNotIn('core:SPELL_IMMUNITY', patches)
        self.assertNotIn('core:NO_RETALIATION', patches)  # Temporary inability, not Vampire/Naga trait.

    def test_preview_generator_preserves_rules_and_live_module(self):
        live = ROOT / 'Mods/new-horizons/mod.json'
        before = live.read_bytes()
        script = ROOT / 'tools/update-new-horizons-convenience.py'
        with tempfile.TemporaryDirectory(dir=ROOT / 'build') as temporary:
            output = Path(temporary) / 'mod.json'
            command = [sys.executable, str(script), '--output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            metadata = json.loads(output.read_text())
            self.assertEqual(metadata['version'], '0.5.1')
            self.assertEqual(metadata['bonuses'], load('config/newHorizonsConvenienceBonuses.json'))
            self.assertEqual(metadata['filesystem'][''], [{'type': 'dir', 'path': '/Content'}])
            self.assertEqual(metadata['filesystem']['SPRITES/'], [{'type': 'dir', 'path': '/Images'}])
            self.assertEqual(metadata['settings']['heroes']['newHorizonsMasteries'],
                             load('config/newHorizonsMasteries.json'))
            subprocess.run(command + ['--check'], check=True, capture_output=True)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run([sys.executable, str(script), '--output', str(live)],
                                          capture_output=True).returncode, 0)
        self.assertEqual(live.read_bytes(), before)

    def test_existing_help_is_reused(self):
        texts = load('Mods/vcmi/Content/config/translations/english.json')
        buttons = load('Mods/new-horizons/Content/config/widgets/nhConvenience.json')['items'][0]['items']
        # readHintText appends BOTH suffixes to a string prefix. Checking a
        # standalone translation leaf missed the actual GUI regression.
        reader = (ROOT / 'client/gui/InterfaceObjectConfigurable.cpp').read_text()
        for suffix in ('hover', 'help'):
            self.assertIn('translate( config.String(), "' + suffix + '")', reader)
            for button in buttons:
                self.assertTrue(texts[button['help'] + '.' + suffix])
                # Preserve the observed leaf-as-prefix as a negative control.
                self.assertNotIn(button['help'] + '.help.' + suffix, texts)


if __name__ == '__main__':
    unittest.main()
