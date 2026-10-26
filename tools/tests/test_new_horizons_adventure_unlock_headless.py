#!/usr/bin/env python3
"""Guard Mage Guild UI refreshes from the engine-less headless client."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'client' / 'NetPacksClient.cpp').read_text(encoding='utf-8')


class AdventureSpellUnlockHeadlessTests(unittest.TestCase):
    def test_headless_returns_before_window_refresh(self):
        for pack_type, town_field, refresh_count in (
            ('SetNewHorizonsAdventureSpellUnlock', 'townId', 2),
            ('SetNewHorizonsSageGuildVisit', 'town', 1),
            ('SetResearchedSpells', 'tid', 1),
        ):
            with self.subTest(pack_type=pack_type):
                match = re.search(
                    rf'void ApplyClientNetPackVisitor::visit{pack_type}'
                    rf'\({pack_type} & pack\)\s*\{{(?P<body>.*?)\n\}}',
                    SOURCE,
                    re.DOTALL,
                )
                self.assertIsNotNone(match, f'{pack_type} visitor was not found')
                body = match.group('body')
                guard = re.search(
                    r'if\s*\(\s*settings\["session"\]\["headless"\]\.Bool\(\)\s*\)\s*return\s*;',
                    body,
                )
                self.assertIsNotNone(guard, 'headless mode must return before using ENGINE')
                first_engine_access = body.find('ENGINE->')
                self.assertGreaterEqual(first_engine_access, 0)
                self.assertLess(guard.end(), first_engine_access)
                self.assertEqual(body.count(f'win->updateSpells(pack.{town_field})'), refresh_count)


if __name__ == '__main__':
    unittest.main()
