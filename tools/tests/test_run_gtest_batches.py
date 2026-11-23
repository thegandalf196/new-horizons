"""Batched GoogleTest runs survive aborting cases and report every case once."""
# SPDX-License-Identifier: GPL-2.0-or-later

import contextlib
import io
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import run_gtest_batches as runner

# A stand-in for a GoogleTest binary: lists its cases and runs those matching the
# filter. Alpha.Crash aborts the process and Beta.Slow fails.
FAKE = '''#!{python}
import os, sys
cases = ["Alpha.One", "Alpha.Crash", "Alpha.Two", "Beta.Slow", "Beta.Skip", "Gamma/P.Case/0", "Delta.DISABLED_Off"]
if "--gtest_list_tests" in sys.argv:
    print("Running main() from fake")
    for suite in ("Alpha", "Beta", "Gamma/P", "Delta"):
        print(suite + ".")
        for case in cases:
            if case.startswith(suite + "."):
                print("  " + case.split(".", 1)[1] + ("  # GetParam() = (1, 2)" if "/" in case else ""))
    sys.exit(0)
pattern = next(a for a in sys.argv if a.startswith("--gtest_filter=")).split("=", 1)[1].split(":")
selected = [c for c in cases if not c.split(".", 1)[1].startswith("DISABLED_")
            and any(c == p or (p.endswith(".*") and c.startswith(p[:-1])) for p in pattern)]
failed = False
for case in selected:
    print("[ RUN      ] " + case, flush=True)
    if case == "Alpha.Crash":
        print("xstring(1348) : Assertion failed: string_view subscript out of range", flush=True)
        os._exit(3)
    if case == "Beta.Slow":
        failed = True
        print("[  FAILED  ] " + case + " (5 ms)")
    elif case == "Beta.Skip":
        print("[  SKIPPED ] " + case + " (0 ms)")
    else:
        suffix = ", where GetParam() = (1, 2)" if "/" in case else ""
        print("[       OK ] " + case + suffix + " (1 ms)")
sys.exit(1 if failed else 0)
'''


class RunGtestBatchesTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.binary = self.root / 'fake_gtest'
        self.binary.write_text(FAKE.format(python=sys.executable))
        self.binary.chmod(self.binary.stat().st_mode | stat.S_IXUSR)

    def tearDown(self):
        self.directory.cleanup()

    def run_main(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = runner.main([str(self.binary), '--cwd', str(self.root),
                                '--log', str(self.root / 'log.txt'), '--title', 'Fake'])
        return code, output.getvalue()

    def test_aborting_case_is_reported_and_the_rest_of_its_suite_still_runs(self):
        code, summary = self.run_main()
        self.assertEqual(code, 1)
        self.assertIn('ok: 3, failed: 1, skipped: 1, crashed: 1, timeout: 0, missing: 0', summary)
        self.assertIn('- CRASHED: `Alpha.Crash`', summary)
        self.assertIn('- FAILED: `Beta.Slow`', summary)
        self.assertIn('string_view subscript out of range', summary)
        self.assertNotIn('DISABLED', summary)
        self.assertIn('[       OK ] Alpha.Two', (self.root / 'log.txt').read_text())

    def test_listing_strips_parameter_comments_and_disabled_cases(self):
        suites = runner.list_tests(str(self.binary), self.root)
        self.assertEqual(suites['Gamma/P'], ['Case/0'])
        self.assertNotIn('Delta', suites)

    def test_long_filters_are_split(self):
        patterns = [f'Suite{index:05}.*' for index in range(2000)]
        groups = list(runner.batches(patterns))
        self.assertGreater(len(groups), 1)
        self.assertEqual(sum(groups, []), patterns)
        self.assertTrue(all(len(':'.join(group)) <= runner.MAX_FILTER for group in groups))

    def test_partially_run_suites_resume_case_by_case(self):
        suites = {'A': ['x', 'y', 'z'], 'B': ['w']}
        results = {'A.x': 'OK', 'A.y': 'CRASHED'}
        self.assertEqual(runner.remaining(suites, ['A', 'B'], results), ['A.z', 'B.*'])


if __name__ == '__main__':
    unittest.main()
