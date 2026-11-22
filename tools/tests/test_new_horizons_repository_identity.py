#!/usr/bin/env python3
"""Local identity/branch contracts for the fork-owned Windows workflows."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
REPOSITORY = "github.repository == 'thegandalf196/new-horizons' && "
MAIN = "github.ref == 'refs/heads/main'"
# Release builds also run from version tags (see the Windows dispatch gate).
GATES = {
    'new-horizons-windows-notices.yml': REPOSITORY + MAIN,
    'new-horizons-windows-tests.yml': REPOSITORY + MAIN,
    'new-horizons-windows.yml': REPOSITORY + "(" + MAIN + " || startsWith(github.ref, 'refs/tags/v') || "
        "(github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/performance/quick-combat-and-ci'))",
}


def job_gates(source):
    """Job-level `if:` conditions, with folded (`>-`) scalars joined into one line."""
    gates = []
    lines = source.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r'^    if: (.*)$', line)
        if not match:
            continue
        if match.group(1) != '>-':
            gates.append(match.group(1))
            continue
        folded = []
        for follow in lines[index + 1:]:
            if not follow.startswith('      '):
                break
            folded.append(follow.strip())
        gates.append(re.sub(r'\(\s+', '(', ' '.join(folded)))
    return [gate for gate in gates if gate.startswith('github.repository')]


class NewHorizonsRepositoryIdentityTest(unittest.TestCase):
    def test_windows_jobs_target_only_the_renamed_repository_main(self):
        for name, expected in GATES.items():
            with self.subTest(workflow=name):
                source = (ROOT / '.github/workflows' / name).read_text()
                self.assertEqual(job_gates(source), [expected])

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

    def test_tests_workflow_never_shares_or_delays_the_release_build(self):
        source = (ROOT / '.github/workflows/new-horizons-windows-tests.yml').read_text()
        self.assertIn('  push:\n    branches: [main]\n', source)
        self.assertIn('  workflow_dispatch:', source)
        self.assertIn('  group: new-horizons-windows-tests-${{ github.ref }}\n  cancel-in-progress: true\n', source)
        self.assertNotIn('group: new-horizons-windows-x64', source)
        self.assertNotIn('new-horizons-windows.yml', source)

    def test_tests_workflow_uses_the_release_dependency_archive(self):
        release = (ROOT / '.github/workflows/new-horizons-windows.yml').read_text()
        tests = (ROOT / '.github/workflows/new-horizons-windows-tests.yml').read_text()
        pin = re.compile(r'https://github\.com/vcmi/vcmi-dependencies/releases/download/\S+'
                         r'|echo "[0-9a-f]{64}  \$archive"')
        self.assertEqual(pin.findall(tests), pin.findall(release))
        self.assertEqual(len(pin.findall(tests)), 2)


if __name__ == '__main__':
    unittest.main()
