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
    def test_adaptive_hero_lists_reserve_native_overflow_controls(self):
        layout = load('Mods/new-horizons/Content/config/widgets/adventureMap.json')
        containers = [container for container in layout['items']
                      if any(item.get('type') == 'adventureMapHeroList'
                             for item in container.get('items', []))]
        branches = (
            ('listContainerSmall', {'heightMax': 664}, 196, 193, 5),
            ('nhListContainer665', {'heightMin': 664, 'heightMax': 665}, 259, 257, 7),
            ('listContainerLarge', {'heightMin': 665}, 260, 257, 7),
        )
        self.assertEqual([container['name'] for container in containers],
                         [branch[0] for branch in branches])
        for container, (name, gates, panel_height, height, count) in zip(containers, branches):
            with self.subTest(branch=name):
                self.assertEqual(container['exists'], gates)
                self.assertEqual(container['hideWhen'], 'worldViewMode')
                self.assertEqual(container['area'], {
                    'top': 196, 'right': 0, 'width': 193, 'height': panel_height,
                })
                heroes = [item for item in container['items']
                          if item.get('type') == 'adventureMapHeroList']
                self.assertEqual(len(heroes), 1)
                hero = heroes[0]
                self.assertEqual(set(hero), {
                    'type', 'name', 'area', 'scrollUp', 'scrollDown',
                    'item', 'itemsOffset', 'itemsCount',
                })
                self.assertEqual(hero['name'], 'heroList')
                self.assertEqual(hero['area'], {
                    'top': 0, 'right': 125, 'width': 68, 'height': height,
                })
                self.assertEqual(hero['item'], {
                    'top': 16, 'left': 3, 'width': 62, 'height': 32,
                })
                self.assertEqual(hero['itemsOffset'], {'x': 0, 'y': 32})
                self.assertEqual(hero['itemsCount'], count)
                for direction, edge, image, help_id in (
                    ('Up', 'top', 'IAM012.DEF', 'core.help.303'),
                    ('Down', 'bottom', 'IAM013.DEF', 'core.help.304'),
                ):
                    self.assertEqual(hero['scroll' + direction], {
                        'type': 'adventureMapButton', 'name': 'heroListScroll' + direction,
                        'image': image, 'help': help_id,
                        'area': {edge: 0, 'left': 2, 'width': 64, 'height': 16},
                    })
                # Protect space for actual native controls, not scaled portraits
                # or arrows laid over the first/last hero. This is data coverage;
                # real disabled states and scrolling still require the renderer.
                self.assertGreaterEqual(hero['item']['top'], hero['scrollUp']['area']['height'])
                last_bottom = (hero['item']['top'] + (count - 1) * hero['itemsOffset']['y']
                               + hero['item']['height'])
                self.assertLessEqual(last_bottom, height - hero['scrollDown']['area']['height'])
                self.assertEqual(height - hero['scrollDown']['area']['height'] - last_bottom, 1)

    def test_adaptive_layout_owns_two_namespaced_existing_actions_per_visible_branch(self):
        fragment = load('Mods/new-horizons/Content/config/widgets/nhConvenience.json')
        self.assertEqual(set(fragment), {'items'})
        self.assertEqual(len(fragment['items']), 3)

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

    def test_reporter_does_not_cover_end_turn_or_other_native_controls(self):
        layout = load('Mods/new-horizons/Content/config/widgets/adventureMap.json')
        fragment = load('Mods/new-horizons/Content/config/widgets/nhConvenience.json')

        def visible(item, height):
            gates = item.get('exists', {})
            return (height > gates.get('heightMin', -1)
                    and height <= gates.get('heightMax', 10000))

        def bounds(area, parent):
            x, y, w, h = parent
            width = area.get('width', w - area.get('left', 0) - area.get('right', 0))
            height = area.get('height', h - area.get('top', 0) - area.get('bottom', 0))
            return (x + area.get('left', w - area.get('right', 0) - width),
                    y + area.get('top', h - area.get('bottom', 0) - height), width, height)

        def controls(items, parent, height):
            result = []
            for item in items:
                if not visible(item, height):
                    continue
                rect = bounds(item.get('area', {}), parent)
                if item['type'] == 'adventureMapButton':
                    result.append((item, rect))
                elif item['type'] == 'adventureMapContainer':
                    result.extend(controls(item['items'], rect, height))
            return result

        for height in range(600, 1201):
            with self.subTest(height=height):
                base = controls(layout['items'], (0, 0, 1280, height), height)
                reporters = controls(fragment['items'], (0, 0, 1280, height), height)
                self.assertEqual(len(reporters), int(height > 631))
                end_turn = [(item, rect) for item, rect in base if item.get('name') == 'buttonEndTurn']
                self.assertEqual(len(end_turn), 1)
                self.assertEqual(end_turn[0][0]['hotkey'], 'adventureEndTurn')
                for item, (x, y, w, h) in reporters:
                    self.assertEqual(item['callback'], 'reportBug')
                    self.assertNotIn('hotkey', item)
                    self.assertLessEqual(y + h, height - 210)  # Do not cover the info panel either.
                    for other, (ox, oy, ow, oh) in base:
                        overlaps = x < ox + ow and ox < x + w and y < oy + oh and oy < y + h
                        self.assertFalse(overlaps, (height, other.get('name')))

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
            # inheriting the live edition's settings and packaged art mounts.
            self.assertEqual(metadata['settings'], json.loads(before)['settings'])
            self.assertNotEqual(output.read_bytes(), before)
            self.assertEqual(metadata['bonuses'], load('config/newHorizonsConvenienceBonuses.json'))
            self.assertEqual(metadata['filesystem'], json.loads(before)['filesystem'])
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
