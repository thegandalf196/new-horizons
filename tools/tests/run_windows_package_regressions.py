#!/usr/bin/env python3
"""Run the complete supported Windows packaging regression set before compilation."""
import argparse
import json
from pathlib import Path
import subprocess
import unittest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    for pattern in ('test_windows*.py', 'test_*mingw*.py', 'test_git_source_snapshot.py',
                    'test_binary_privacy.py', 'test_dependency_preflight.py'):
        suite.addTests(loader.discover(str(tests), pattern=pattern))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    report = {
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'tracked_worktree_dirty': bool(subprocess.check_output(['git', 'diff', '--name-only'], text=True).strip()),
        'tests': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
        'skipped': len(result.skipped), 'pass': result.wasSuccessful(),
        'scope': 'Complete Windows package/source/PE/privacy preflight regressions; no client compilation or gameplay claim',
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)


if __name__ == '__main__':
    main()
