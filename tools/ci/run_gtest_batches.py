#!/usr/bin/env python3
"""Run a GoogleTest binary in suite batches so that one aborting case cannot hide the rest.

A debug-CRT assertion or an access violation ends the whole process. The batch that
contained it is resumed after the case that was running, which is reported as crashed.
Writes a Markdown summary to stdout and exits non-zero on any failed or crashed case.
"""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import re
import subprocess
import sys
from pathlib import Path

RESULT = re.compile(r'^\[ +(OK|FAILED|SKIPPED) +\] (\S+?)(?:, where .*)? \(\d+ ms\)\s*$')
RUN = re.compile(r'^\[ RUN {6}\] (\S+)\s*$')
MAX_FILTER = 8000
MAX_LISTED = 200


def list_tests(binary, cwd):
    """Suite name -> case names, without disabled cases."""
    output = subprocess.run([binary, '--gtest_list_tests'], cwd=cwd, capture_output=True,
                            text=True, errors='replace', check=True).stdout
    suites, suite = {}, None
    for line in output.splitlines():
        name = line.split('#', 1)[0].strip()
        if not name or line.startswith(('Running main', 'Note:')):
            continue
        if not line.startswith(' '):
            suite = name.rstrip('.')
            suites.setdefault(suite, [])
        elif suite and not any(part.startswith('DISABLED_') for part in (*suite.split('/'), name)):
            suites[suite].append(name)
    return {suite: cases for suite, cases in suites.items() if cases}


def batches(patterns):
    """Group filter patterns so that each command line stays short."""
    group, size = [], 0
    for pattern in patterns:
        if group and size + len(pattern) + 1 > MAX_FILTER:
            yield group
            group, size = [], 0
        group.append(pattern)
        size += len(pattern) + 1
    if group:
        yield group


def parse(output, results):
    """Record case results; return the last case that started without a result."""
    running = None
    for line in output.splitlines():
        started = RUN.match(line)
        if started:
            running = started.group(1)
            continue
        finished = RESULT.match(line)
        if finished:
            results[finished.group(2)] = finished.group(1)
            if finished.group(2) == running:
                running = None
    return running


def remaining(suites, pattern_suites, results):
    """Patterns for the cases of these suites that have no result yet."""
    patterns = []
    for suite in pattern_suites:
        cases = [f'{suite}.{case}' for case in suites[suite]]
        left = [case for case in cases if case not in results]
        if len(left) == len(cases):
            patterns.append(suite + '.*')
        else:
            patterns.extend(left)
    return patterns


def run(args):
    suites = list_tests(args.binary, args.cwd)
    results, queue = {}, [suite + '.*' for suite in suites]
    with open(args.log, 'w', encoding='utf-8', errors='replace') as log:
        while queue:
            group = next(batches(queue))
            queue = queue[len(group):]
            command = [args.binary, '--gtest_filter=' + ':'.join(group)]
            try:
                process = subprocess.run(command, cwd=args.cwd, capture_output=True, text=True,
                                         errors='replace', timeout=args.timeout)
                output, code = process.stdout + process.stderr, process.returncode
            except subprocess.TimeoutExpired as error:
                output = (error.stdout or '') + (error.stderr or '')
                output = output.decode(errors='replace') if isinstance(output, bytes) else output
                code = None
            log.write(output)
            crashed = parse(output, results)
            if code in (0, 1) and crashed is None:
                continue
            if crashed is None:
                print(f'Batch ended with exit code {code} outside a case:\n' + '\n'.join(output.splitlines()[-10:]))
                break
            results[crashed] = 'CRASHED' if code is not None else 'TIMEOUT'
            print(f'`{crashed}` ended the process (exit code {code}):\n```\n'
                  + '\n'.join(output.splitlines()[-10:]) + '\n```')
            group_suites = sorted({pattern.split('.', 1)[0] for pattern in group})
            queue = remaining(suites, group_suites, results) + queue
    return suites, results


def summarize(suites, results, title):
    expected = [f'{suite}.{case}' for suite, cases in suites.items() for case in cases]
    counts = {'OK': 0, 'FAILED': 0, 'SKIPPED': 0, 'CRASHED': 0, 'TIMEOUT': 0, 'MISSING': 0}
    problems = []
    for name in expected:
        status = results.get(name, 'MISSING')
        counts[status] += 1
        if status not in ('OK', 'SKIPPED'):
            problems.append(f'- {status}: `{name}`')
    lines = [f'## {title}', ', '.join(f'{key.lower()}: {value}' for key, value in counts.items())]
    lines += problems[:MAX_LISTED]
    if len(problems) > MAX_LISTED:
        lines.append(f'- and {len(problems) - MAX_LISTED} more')
    return '\n'.join(lines), not problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', help='GoogleTest executable')
    parser.add_argument('--cwd', type=Path, default=None, help='Working directory for every run')
    parser.add_argument('--log', type=Path, required=True, help='Combined output of all runs')
    parser.add_argument('--timeout', type=int, default=1200, help='Seconds allowed for one batch')
    parser.add_argument('--title', default='GoogleTest results')
    args = parser.parse_args(argv)
    args.binary = str(Path(args.binary).resolve())
    suites, results = run(args)
    summary, passed = summarize(suites, results, args.title)
    print(summary)
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
