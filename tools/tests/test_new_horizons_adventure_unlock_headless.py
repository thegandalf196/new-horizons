#!/usr/bin/env python3
"""Guard the adventure-spell unlock UI refresh from the engine-less headless client."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / 'client' / 'NetPacksClient.cpp').read_text(encoding='utf-8')


class AdventureSpellUnlockHeadlessTests(unittest.TestCase):
    def test_headless_returns_before_window_refresh(self):
        match = re.search(
            r'void ApplyClientNetPackVisitor::visitSetNewHorizonsAdventureSpellUnlock'
            r'\(SetNewHorizonsAdventureSpellUnlock & pack\)\s*\{(?P<body>.*?)\n\}',
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(match, 'adventure spell unlock visitor was not found')
        body = match.group('body')
        guard = re.search(
            r'if\s*\(\s*settings\["session"\]\["headless"\]\.Bool\(\)\s*\)\s*return\s*;',
            body,
        )
        self.assertIsNotNone(guard, 'headless mode must return before using ENGINE')
        first_engine_access = body.find('ENGINE->windows()')
        self.assertGreaterEqual(first_engine_access, 0)
        self.assertLess(guard.start(), first_engine_access)
        self.assertEqual(body.count('win->updateSpells(pack.townId)'), 2)


if __name__ == '__main__':
    unittest.main()
