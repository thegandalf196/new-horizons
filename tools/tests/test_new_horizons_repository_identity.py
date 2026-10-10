#!/usr/bin/env python3
"""Local identity/branch contracts for the two fork-owned Windows workflows."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
EXPECTED_GATE = "github.repository == 'thegandalf196/new-horizons' && github.ref == 'refs/heads/main'"


class NewHorizonsRepositoryIdentityTest(unittest.TestCase):
    def test_both_windows_jobs_target_only_the_renamed_repository_main(self):
        for name in ('new-horizons-windows.yml', 'new-horizons-windows-notices.yml'):
            with self.subTest(workflow=name):
                source = (ROOT / '.github/workflows' / name).read_text()
                gates = re.findall(r'^    if: (github\.repository.*)$', source, re.MULTILINE)
                self.assertEqual(gates, [EXPECTED_GATE])

    def test_notices_scoped_push_tracks_main_and_retains_its_path_filter(self):
        source = (ROOT / '.github/workflows/new-horizons-windows-notices.yml').read_text()
        self.assertIn('  push:\n    branches: [main]\n', source)
        self.assertIn('    paths: [.github/workflows/new-horizons-windows-notices.yml]', source)
        self.assertIn('  workflow_dispatch:', source)

    def test_main_build_remains_manually_dispatched_without_added_push(self):
        source = (ROOT / '.github/workflows/new-horizons-windows.yml').read_text()
        self.assertIn('  workflow_dispatch:', source)
        self.assertNotRegex(source, re.compile(r'^  push:', re.MULTILINE),
                            msg='Do not broaden the existing build trigger')
        self.assertIn('  cancel-in-progress: false', source)


if __name__ == '__main__':
    unittest.main()
