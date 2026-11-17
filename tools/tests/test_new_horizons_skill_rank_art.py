"""Distinct active rank families and their retained selected predecessors."""
import json
from pathlib import Path
import unittest
from .nhart_test_resources import open_image, resource_names

ROOT = Path(__file__).resolve().parents[2]
SKILLS = (
    'battlecraft', 'recruitment', 'authority', 'warcasting', 'divineMandate',
    'sylvanLuck', 'shroudOfMalassa', 'demonicGating', 'bloodrage',
    'bulwarkOfTheMire', 'elementalRebirth',
)
RANKS = ('basic', 'advanced', 'expert')
SIZES = {'small': (32, 32), 'medium': (44, 44),
         'large': (82, 93), 'scenarioBonus': (58, 64)}


class SkillRankArtworkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canonical = json.loads((ROOT / 'config/newHorizonsSkills.json').read_text())
        cls.module = json.loads((ROOT / 'Mods/new-horizons/mod.json').read_text())
        cls.heroes = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroes/halon.json').read_text())

    def test_all_eleven_families_have_complete_matching_canonical_and_module_bindings(self):
        bound = set()
        for skill in SKILLS:
            for rank in RANKS:
                expected = {size: f'NH_{skill}_rankart_v2_{rank}_{size}.png' for size in SIZES}
                self.assertEqual(self.canonical[skill][rank]['images'], expected)
                self.assertEqual(self.module['skills'][skill][rank]['images'], expected)
                bound.update(expected.values())
        self.assertEqual(len(bound), 132)
        self.assertTrue({'SPRITES/' + name for name in bound} <= resource_names())

    def test_rank_art_has_native_dimensions_and_distinct_subject_areas(self):
        for skill in SKILLS:
            for role, size in SIZES.items():
                interiors = []
                for rank in RANKS:
                    name = self.canonical[skill][rank]['images'][role]
                    image = open_image('SPRITES/' + name)
                    self.assertEqual((image.size, image.mode), (size, 'RGBA'), name)
                    # Compare the upper subject region, excluding the old bottom pip row.
                    interiors.append(image.crop((size[0] // 5, size[1] // 5,
                                                 size[0] * 4 // 5, size[1] * 3 // 4)).tobytes())
                self.assertEqual(len(set(interiors)), 3, (skill, role))

    def test_historical_images_remain_selected_with_their_existing_resource_names(self):
        historical = {f'SPRITES/NH_{skill}_{rank}_{role}.png'
                      for skill in SKILLS for rank in RANKS for role in SIZES}
        self.assertEqual(len(historical), 132)
        self.assertTrue(historical <= resource_names())

    def test_skill_specialty_aliases_use_current_basic_art(self):
        aliases = []
        prefixes = tuple('NH_' + skill + '_rankart_v2_' for skill in SKILLS)
        obsolete = tuple('NH_' + skill + '_' + rank + '_' for skill in SKILLS for rank in RANKS)
        for hero, definition in self.heroes.items():
            images = definition.get('images', {})
            for role, value in images.items():
                self.assertFalse(value.startswith(obsolete), (hero, role, value))
                if value.startswith(prefixes):
                    aliases.append((hero, role))
                    size = {'specialtySmall': 'small', 'specialtyLarge': 'medium'}[role]
                    self.assertTrue(value.endswith('_basic_' + size + '.png'))
                    self.assertIn('SPRITES/' + value, resource_names())
        self.assertEqual(len(aliases), 38)
        self.assertEqual(len({hero for hero, _ in aliases}), 19)
