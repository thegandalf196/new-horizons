#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Opt-in matched ordinary native callback benchmark; purchaser assets stay external.

Requires a reviewed manifest SHA and an unexpired exclusive quiet lease. Never
builds, launches graphics, changes the playable pointer or supplies game assets.
Use an absolute verified interpreter. See the adjacent benchmark contract.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import resource
import statistics
import subprocess
import time
import xml.etree.ElementTree as ET

FILTER = 'NativeRosterDecisionProfileTest.FourteenStackOrdinaryFirstDecision'
PARITY = ('effectiveInputs', 'nativeStateBefore', 'nativeStateAfter', 'selectedActionHex',
          'gameRngBefore', 'gameRngAfter', 'aiRngBefore', 'aiRngAfter', 'seed',
          'horizon', 'searchPolicy', 'initialRound', 'finalRound', 'unitCount',
          'creatureCount', 'heroActionCount', 'unitActionCount')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def pinned_files(pins):
    if not pins:
        raise ValueError('Empty candidate/source/resource/provider pin inventory')
    for name, expected in pins.items():
        path = Path(name)
        if not path.is_absolute() or not path.is_file() or digest(path) != expected:
            raise ValueError('Pinned file missing or changed: ' + name)


def quiet_lease(plan):
    path = Path(plan['quietLease']['path'])
    if digest(path) != plan['quietLease']['sha256']:
        raise ValueError('Quiet lease changed')
    lease = json.loads(path.read_text())
    if lease['state'] != 'QUIET_EXCLUSIVE' or lease['expiresUnix'] <= time.time():
        raise ValueError('No current exclusive quiet lease')
    if lease['buildsAllowed'] or lease['otherMeasurementsAllowed']:
        raise ValueError('Quiet lease permits competing work')


def cap_memory():
    resource.setrlimit(resource.RLIMIT_AS, (8589934592, 8589934592))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (67108864, 67108864))


def child_identity(pid):
    proc = Path('/proc') / str(pid)
    return {'pid': pid, 'startTicks': (proc / 'stat').read_text().rsplit(')', 1)[1].split()[19],
            'exe': str((proc / 'exe').resolve(strict=True))}


def invoke(candidate, argv, output, plan, diagnostics):
    """Observe the exact owned child before accepting results; always wait/reap."""
    quiet_lease(plan)
    pinned_files(candidate['pins'])
    env = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8'}
    env.update(candidate['environment'])
    for key in ('HOME', 'XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME',
                'XDG_RUNTIME_DIR', 'XDG_DATA_DIRS', 'TMPDIR', 'LD_LIBRARY_PATH'):
        if not env.get(key) or not Path(env[key].split(':')[0]).is_absolute():
            raise ValueError('Missing isolated environment: ' + key)
    env.update(SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_VIDEODRIVER='dummy', SDL_VIDEO_DRIVER='dummy',
               GTEST_SHUFFLE='0', GTEST_REPEAT='1', GTEST_RANDOM_SEED='1',
               NH_ORDINARY_DIAGNOSTICS='1' if diagnostics else '0',
               NH_RENDER_LOAD_TIMINGS='1' if diagnostics else '0',
               NH_RENDER_LOAD_TIMING_LIMIT='1024')
    forbidden = ('DISPLAY', 'WAYLAND_DISPLAY', 'LD_PRELOAD', 'LD_AUDIT')
    if any(key in env for key in forbidden):
        raise ValueError('Host display or injected runtime environment refused')
    if any(key.startswith(('NH_AI_', 'NH_TEST_')) for key in env):
        raise ValueError('Experimental policy overrides refused')
    binary = str(Path(candidate['binary']).resolve(strict=True))
    if binary not in candidate['pins']:
        raise ValueError('Executable is not pinned')
    with (output / 'child.log').open('x') as log:
        child = subprocess.Popen([binary, *argv], cwd=candidate['cwd'], env=env,
                                 stdout=log, stderr=subprocess.STDOUT,
                                 preexec_fn=cap_memory)
        identity, observed, timed_out = None, False, False
        try:
            deadline = time.monotonic() + 5
            while child.poll() is None and time.monotonic() < deadline:
                try:
                    identity = child_identity(child.pid)
                    actual = dict(row.split('=', 1) for row in
                                  (Path('/proc') / str(child.pid) / 'environ').read_text().split('\0')
                                  if '=' in row)
                    observed = identity['exe'] == binary and all(actual.get(k) == v for k, v in env.items())
                    observed &= not any(k in actual for k in forbidden)
                    limits = (Path('/proc') / str(child.pid) / 'limits').read_text().splitlines()
                    address = next(line.split() for line in limits if line.startswith('Max address space'))
                    observed &= address[3:5] == ['8589934592', '8589934592']
                    if observed:
                        break
                except (FileNotFoundError, ProcessLookupError):
                    pass
                time.sleep(0.001)
            if not observed:
                raise RuntimeError('Actual child executable/environment not witnessed')
            try:
                child.wait(timeout=120)
            except subprocess.TimeoutExpired:
                timed_out = True
                raise RuntimeError('Bounded native workload timed out')
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            (output / 'owner.json').write_text(json.dumps({
                'identity': identity, 'environmentVerified': observed,
                'returncode': child.returncode, 'timedOut': timed_out,
                'reaped': True, 'memoryCapBytes': 8589934592}, indent=2) + '\n')
    pinned_files(candidate['pins'])
    quiet_lease(plan)
    if child.returncode:
        raise RuntimeError('Native child failed: ' + str(child.returncode))


def properties(path):
    root = ET.parse(path).getroot()
    cases = list(root.iter('testcase'))
    if len(cases) != 1 or list(root.iter('failure')) or list(root.iter('skipped')) or list(root.iter('error')):
        raise ValueError('Expected exactly one executed successful case')
    if cases[0].get('classname') + '.' + cases[0].get('name') != FILTER:
        raise ValueError('Wrong native case')
    result = {row.attrib['name']: row.attrib['value'] for row in cases[0].iter('property')}
    if any(key not in result for key in (*PARITY, 'firstCallbackUs', 'diagnostics')):
        raise ValueError('Missing semantic witnesses')
    if int(result['firstCallbackUs']) <= 0:
        raise ValueError('Invalid callback elapsed time')
    for prefix in ('gameRng', 'aiRng'):
        if result[prefix + 'Before'] != result[prefix + 'After']:
            raise ValueError('Callback mutated RNG: ' + prefix)
    if result['nativeStateBefore'] != result['nativeStateAfter']:
        raise ValueError('Callback mutated native state')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--execute', action='store_true', required=True)
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--gate', action='store_true')
    parser.add_argument('--max-regression-percent', type=float)
    args = parser.parse_args()
    os.umask(0o077)
    if digest(args.manifest) != args.manifest_sha256:
        raise ValueError('Reviewed manifest changed')
    plan = json.loads(args.manifest.read_text())
    if args.gate:
        if args.diagnostics or not 3 <= plan['pairs'] <= 5:
            raise ValueError('Timing gate requires three to five quiet matched pairs')
        margin = args.max_regression_percent
        if margin is None or not math.isfinite(margin) or not 0 <= margin <= 100:
            raise ValueError('Timing gate requires an explicit finite margin from zero to 100 percent')

    if not 1 <= plan['pairs'] <= 5 or set(plan['candidates']) != {'baseline', 'candidate'}:
        raise ValueError('Expected two candidates and one to five pairs')
    if not args.output.is_absolute() or args.output.exists():
        raise ValueError('Output must be a fresh absolute private directory')
    args.output.mkdir(mode=0o700)
    samples, reference = [], None
    for pair in range(plan['pairs']):
        for arm in (('baseline', 'candidate') if pair % 2 == 0 else ('candidate', 'baseline')):
            candidate = dict(plan['candidates'][arm])
            output = args.output / (str(pair) + '-' + arm)
            output.mkdir(mode=0o700)
            # Every invocation receives a fresh disposable profile beneath its output.
            candidate['environment'] = dict(candidate['environment'])
            for key, name in (('HOME', 'home'), ('XDG_DATA_HOME', 'data'),
                              ('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'),
                              ('XDG_RUNTIME_DIR', 'runtime'), ('TMPDIR', 'tmp')):
                target = output / name
                target.mkdir(mode=0o700)
                candidate['environment'][key] = str(target)
            argv = ['--gtest_filter=' + FILTER, '--gtest_shuffle=0', '--gtest_repeat=1',
                    '--gtest_random_seed=1', '--gtest_output=xml:' + str(output / 'native.xml')]
            listing = output / 'listing'
            listing.mkdir(mode=0o700)
            invoke(candidate, ['--gtest_filter=' + FILTER, '--gtest_list_tests'],
                   listing, plan, args.diagnostics)
            names, suite = [], None
            for line in (listing / 'child.log').read_text().splitlines():
                clean = line.split('#', 1)[0].rstrip()
                if clean and not clean[0].isspace() and clean.endswith('.'):
                    suite = clean[:-1]
                elif clean.startswith('  ') and suite:
                    names.append(suite + '.' + clean.strip())
            if names != [FILTER]:
                raise ValueError('Exact nonempty discovery failed')
            invoke(candidate, argv, output, plan, args.diagnostics)
            row = properties(output / 'native.xml')
            if row['diagnostics'] != ('1' if args.diagnostics else '0'):
                raise ValueError('Diagnostic mode witness disagrees')
            witness = {key: row[key] for key in PARITY}
            if reference is None:
                reference = witness
            differences = [key for key in PARITY if witness[key] != reference[key]]
            sample = {'pair': pair, 'arm': arm, 'properties': row, 'differences': differences}
            samples.append(sample)
            (args.output / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
            if differences:
                raise ValueError('Functional parity failed: ' + ', '.join(differences))
    summary = {'functionalParity': True, 'diagnostics': args.diagnostics,
               'quietTimingSamples': not args.diagnostics, 'samplesPerArm': plan['pairs'],
               'scope': 'One no-hero ordinary callback; not whole-battle/rendered acceptance'}
    for arm in ('baseline', 'candidate'):
        times = [int(row['properties']['firstCallbackUs']) for row in samples if row['arm'] == arm]
        summary[arm] = {'medianUs': statistics.median(times), 'minUs': min(times), 'maxUs': max(times)}
    if args.gate:
        baseline = summary['baseline']['medianUs']
        candidate = summary['candidate']['medianUs']
        summary['regressionGate'] = {
            'maxRegressionPercent': margin,
            'observedPercent': 100 * (candidate / baseline - 1),
            'passed': candidate <= baseline * (1 + margin / 100),
            'basis': 'Matched medians after exact semantic parity; hardware/noise qualification is external'}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if args.gate and not summary['regressionGate']['passed']:
        raise SystemExit(2)



if __name__ == '__main__':
    main()
