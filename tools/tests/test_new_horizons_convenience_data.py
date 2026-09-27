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
    def test_adaptive_layout_owns_two_namespaced_existing_actions_per_visible_branch(self):
        fragment = load('Mods/new-horizons/Content/config/widgets/nhConvenience.json')
        self.assertEqual(set(fragment), {'items'})
        self.assertEqual(fragment['items'], [])

        layout = load('Mods/new-horizons/Content/config/widgets/adventureMap.json')
        containers = [container for container in layout['items']
                      if any(item.get('name') == 'nhButtonQuickSave'
                             for item in container.get('items', []))]
        self.assertEqual([container['name'] for container in containers],
                         ['buttonsContainer4', 'buttonsContainer5'])
        self.assertEqual(containers[0]['exists'], {'heightMin': 664, 'heightMax': 899})
        self.assertEqual(containers[1]['exists'], {'heightMin': 899})
        expected_images = (
            ['NH_qsave_32', 'NH_qload_32'],
            ['NH_qsave_64x32', 'NH_qload_64x32'],
        )
        expected_areas = (
            [
                {'top': 160, 'left': 0, 'width': 32, 'height': 32},
                {'top': 160, 'left': 32, 'width': 32, 'height': 32},
            ],
            [
                {'top': 64, 'left': 0, 'width': 64, 'height': 32},
                {'top': 96, 'left': 0, 'width': 64, 'height': 32},
            ],
        )
        for index, container in enumerate(containers):
            self.assertEqual(container['hideWhen'], 'worldViewMode')
            buttons = [item for item in container['items']
                       if item.get('name') in {'nhButtonQuickSave', 'nhButtonQuickLoad'}]
            self.assertEqual(len(buttons), 2)
            self.assertEqual([button['hotkey'] for button in buttons],
                             ['adventureQuickSave', 'adventureQuickLoad'])
            self.assertEqual([button['image'] for button in buttons], expected_images[index])
            self.assertEqual([button['area'] for button in buttons], expected_areas[index])
            self.assertEqual([button['help'] for button in buttons],
                             ['vcmi.adventureMap.quickSave', 'vcmi.adventureMap.quickLoad'])
            self.assertTrue(all(not button['playerColored'] for button in buttons))

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
            # The presentation preview keeps its historical identity while
            # inheriting the active canonical settings (currently module 0.14.0).
            self.assertEqual(metadata['settings'], json.loads(before)['settings'])
            self.assertNotEqual(output.read_bytes(), before)
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
        layout = load('Mods/new-horizons/Content/config/widgets/adventureMap.json')
        buttons = [item for container in layout['items'] for item in container.get('items', [])
                   if item.get('name') in {'nhButtonQuickSave', 'nhButtonQuickLoad'}]
        self.assertEqual(len(buttons), 4)
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
