#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused guard for conditional Speed-artifact Initiative overlays."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_new_horizons_content import load


EXPECTED = {
    'core:ringOfTheWayfarer': 1,
    'core:necklaceOfSwiftness': 1,
    'core:capeOfVelocity': 2,
}


class NewHorizonsSpeedArtifactTest(unittest.TestCase):
    def test_matching_initiative_is_limited_to_explicit_creature_initiative(self):
        overlay = load('Mods/new-horizons/Content/config/artifacts/speedAndInitiative.json')
        core = load('config/artifacts.json')

        self.assertEqual(set(overlay), set(EXPECTED))
        for artifact_id, value in EXPECTED.items():
            with self.subTest(artifact=artifact_id):
                core_id = artifact_id.split(':', 1)[1]
                core_speed = core[core_id]['bonuses']['speed']
                self.assertEqual(core_speed['type'], 'STACKS_SPEED')
                self.assertEqual(core_speed['val'], value)
                self.assertEqual(core_speed['valueType'], 'BASE_NUMBER')

                patch = overlay[artifact_id]
                self.assertEqual(set(patch['bonuses']), {'initiative'})
                initiative = patch['bonuses']['initiative']
                self.assertEqual(initiative['type'], 'STACKS_INITIATIVE_BASE')
                self.assertEqual(initiative['val'], value)
                self.assertEqual(initiative['valueType'], 'BASE_NUMBER')
                self.assertEqual(initiative['limiters'], [{
                    'type': 'HAS_ANOTHER_BONUS_LIMITER',
                    'bonusType': 'STACKS_INITIATIVE_BASE',
                    'bonusSourceType': 'CREATURE_ABILITY',
                }])

                description = patch['text']['description']
                self.assertIn(f'+{value} Speed', description)
                self.assertIn(f'+{value} Initiative', description)
                self.assertIn('every creature stack', description)


if __name__ == '__main__':
    unittest.main()
