"""Retain Windows compile timings and compiler-cache evidence without changing the build."""
# SPDX-License-Identifier: GPL-2.0-or-later

from datetime import datetime, timezone
import hashlib
import json
import ntpath
import os
from pathlib import Path
import shutil
import subprocess
import time


CACHE_COUNTERS = (
    'compile_requests', 'requests_unsupported_compiler', 'requests_not_compile',
    'requests_not_cacheable', 'requests_executed', 'non_cacheable_compilations',
    'compilations', 'compile_fails', 'cache_writes', 'cache_write_errors',
)
BUILD_SETTINGS = (
    'CMAKE_BUILD_TYPE', 'CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER',
    'CMAKE_CXX_COMPILER_VERSION', 'CMAKE_CXX_FLAGS', 'CMAKE_CXX_FLAGS_RELEASE',
    'CMAKE_CXX_FLAGS_RELWITHDEBINFO', 'ENABLE_PCH', 'ENABLE_NH_COMPACT_FORECAST',
)


def cache_delta(before, after):
    """Compare sccache's per-language counters and individual bypass reasons."""
    first, last = before.get('stats', {}), after.get('stats', {})
    if not first or not last:
        return {'available': False}
    result = {'available': True}
    for name in CACHE_COUNTERS:
        result[name] = last.get(name, 0) - first.get(name, 0)
    for name in ('cache_hits', 'cache_misses', 'cache_errors'):
        initial, final = first.get(name, {}), last.get(name, {})
        for category, suffix in (('counts', ''), ('adv_counts', '_by_compiler')):
            start, end = initial.get(category, {}), final.get(category, {})
            result[name + suffix] = {key: end.get(key, 0) - start.get(key, 0)
                                     for key in sorted(start.keys() | end.keys())}
    initial, final = first.get('not_cached', {}), last.get('not_cached', {})
    result['not_cached'] = {key: final.get(key, 0) - initial.get(key, 0)
                            for key in sorted(initial.keys() | final.keys())}
    result['counters_reset'] = any(value < 0 for value in result.values() if isinstance(value, int))
    result['counters_reset'] |= any(value < 0 for counts in result.values() if isinstance(counts, dict)
                                    for value in counts.values())
    return result


def ninja_actions(content, previous):
    """Read Ninja v5-v7 completed actions; their elapsed spans can overlap."""
    old = set(previous.splitlines())
    rows, invalid = [], 0
    header = content.splitlines()[0] if content else ''
    if content and header not in ('# ninja log v5', '# ninja log v6', '# ninja log v7'):
        return {'available': False, 'reason': 'Unsupported Ninja log version'}
    for line in content.splitlines()[1:]:
        if line in old:
            continue
        fields = line.split('\t')
        try:
            start, end = map(int, fields[:2])
            if len(fields) != 5 or end < start or start < 0:
                raise ValueError('Invalid Ninja action')
        except ValueError:
            invalid += 1
            continue
        rows.append({'startMilliseconds': start, 'endMilliseconds': end,
                     'durationMilliseconds': end - start, 'output': fields[3],
                     'commandHash': fields[4]})
    rows.sort(key=lambda row: row['durationMilliseconds'], reverse=True)
    return {'available': bool(content), 'version': int(header[-1]) if content else None,
            'newCompletedOutputCount': len(rows),
            'invalidRows': invalid, 'longestCompletedOutputs': rows[:30],
            'note': 'One command can produce several outputs. Durations overlap across concurrent commands.'}


def configured_settings(build_root):
    path = build_root / 'CMakeCache.txt'
    result = {}
    if path.is_file():
        for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
            if '=' not in line or ':' not in line.split('=', 1)[0]:
                continue
            key, value = line.split('=', 1)
            name = key.split(':', 1)[0]
            if name in BUILD_SETTINGS:
                result[name] = ntpath.basename(value) if name.endswith('_COMPILER') else value
    return result


class BuildDiagnostics:
    def __init__(self, args):
        self.directory = Path(args.diagnostics_dir)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.build_root = Path(args.build_root)
        self.sccache = args.sccache
        self.started = time.perf_counter()
        self.report = {
            'format': 1, 'startedUTC': datetime.now(timezone.utc).isoformat(),
            'sourceRevision': os.environ.get('GITHUB_SHA', ''),
            'workflowRun': os.environ.get('GITHUB_RUN_ID', ''),
            'workflowAttempt': os.environ.get('GITHUB_RUN_ATTEMPT', ''),
            'runnerImageOS': os.environ.get('ImageOS', ''),
            'runnerImageVersion': os.environ.get('ImageVersion', ''),
            'cacheKeyRequested': args.cache_key_requested,
            'cacheKeyMatched': args.cache_key_matched,
            'jobs': args.jobs, 'settings': configured_settings(self.build_root),
            'stages': [], 'diagnosticErrors': [],
        }
        log = self.build_root / '.ninja_log'
        self.previous_ninja = log.read_text(encoding='utf-8', errors='replace') if log.is_file() else ''
        self.before = self.cache_snapshot('before')

    def cache_snapshot(self, when):
        decoded = {}
        for kind, arguments in (
            ('json', ['--show-stats', '--stats-format=json']),
            ('text', ['--show-stats']),
        ):
            try:
                result = subprocess.run([self.sccache, *arguments], check=False,
                                        capture_output=True, text=True, encoding='utf-8',
                                        errors='replace', timeout=30)
                extension = 'json' if kind == 'json' else 'txt'
                (self.directory / f'sccache-{when}.{extension}').write_text(result.stdout, encoding='utf-8')
                (self.directory / f'sccache-{when}-{kind}.stderr.txt').write_text(result.stderr, encoding='utf-8')
                if result.returncode:
                    raise ValueError(f'sccache {kind} snapshot returned {result.returncode}')
                if kind == 'json':
                    snapshot = json.loads(result.stdout)
                    if not isinstance(snapshot, dict):
                        raise ValueError('sccache statistics are not an object')
                    decoded = snapshot
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                self.report['diagnosticErrors'].append(f'{when} {kind}: {error}')
        return decoded

    def run(self, command):
        began = time.perf_counter()
        portable_command = [ntpath.basename(command[0]), *command[1:]]
        portable_command[portable_command.index('--build') + 1] = '<build-root>'
        stage = {'command': portable_command, 'startedUTC': datetime.now(timezone.utc).isoformat()}
        self.report['stages'].append(stage)
        try:
            result = subprocess.run(command, check=False)
            stage['exitCode'] = result.returncode
            return result.returncode
        finally:
            stage['wallSeconds'] = time.perf_counter() - began

    def finish(self):
        # Record compile wall time before diagnostic subprocesses and copying.
        self.report['wallSeconds'] = sum(stage['wallSeconds'] for stage in self.report['stages'])
        self.report['finishedUTC'] = datetime.now(timezone.utc).isoformat()
        after = self.cache_snapshot('after')
        self.report['cacheDelta'] = cache_delta(self.before, after)
        self.report['sccacheVersion'] = after.get('version', self.before.get('version', ''))
        log = self.build_root / '.ninja_log'
        if log.is_file():
            target = self.directory / '.ninja_log'
            if log.resolve() != target.resolve():
                shutil.copyfile(log, target)
            self.report['ninjaLogSHA256'] = hashlib.sha256(log.read_bytes()).hexdigest()
            self.report['ninja'] = ninja_actions(log.read_text(encoding='utf-8', errors='replace'), self.previous_ninja)
        else:
            self.report['ninja'] = {'available': False, 'reason': 'Ninja produced no log'}
        self.report['elapsedIncludingDiagnosticsSeconds'] = time.perf_counter() - self.started
        (self.directory / 'build-diagnostics.json').write_text(json.dumps(self.report, indent=2) + '\n', encoding='utf-8')
