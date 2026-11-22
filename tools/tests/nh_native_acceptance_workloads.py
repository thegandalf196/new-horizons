#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run one opt-in native encounter/world workload under the reviewed quiet guard.

Imports the ordinary benchmark's exact-child/isolation/pinning guard. Original
resources remain external. No compilation, graphical launch or promotion.
"""
import argparse
import json
import math
import os
from pathlib import Path
import statistics
import nh_ordinary_decision_benchmark as guard
import nh_native_work_properties as work_properties

ENCOUNTER = 'DISABLED_NativeEncounterBenchmarkTest.'
WORLD = 'DISABLED_NativeWorldBenchmarkTest.'
WORKLOADS = {
    'nohero': (ENCOUNTER + 'NoHeroFourteenStack', 'encounterUs'),
    'hero': (ENCOUNTER + 'HeroOptionsFourteenStack', 'encounterUs'),
    'castle': (ENCOUNTER + 'CastleEncounter', 'encounterUs'),
    'necropolis': (ENCOUNTER + 'NecropolisEncounter', 'encounterUs'),
    'rampart': (ENCOUNTER + 'RampartEncounter', 'encounterUs'),
    'conflux-aenain-gnolls': (ENCOUNTER + 'ConfluxAenainGnolls', 'encounterUs'),
    'sorcery': (ENCOUNTER + 'SorceryEncounter', 'encounterUs'),
    'mcastle': (ENCOUNTER + 'MirrorCastle', 'encounterUs'),
    'mrampart': (ENCOUNTER + 'MirrorRampart', 'encounterUs'),
    'mnecro': (ENCOUNTER + 'MirrorNecropolis', 'encounterUs'),
    'mcastle-hero': (ENCOUNTER + 'MirrorCastleHeroOptions', 'encounterUs'),
    'mrampart-hero': (ENCOUNTER + 'MirrorRampartHeroOptions', 'encounterUs'),
    'mnecro-hero': (ENCOUNTER + 'MirrorNecropolisHeroOptions', 'encounterUs'),
    'small': (WORLD + 'SmallMapInitialization', 'initializationUs'),
    'large': (WORLD + 'LargeMapInitialization', 'initializationUs'),
    'day8-memory': (WORLD + 'DayEightCurrentFormatMemoryRoundTrip', 'snapshotReadUs'),
}
# Keep every recorded nonclock semantic/work witness. Operation-count reductions
# are reported separately from required action/state/RNG agreement.
CLOCKS = {'encounterUs', 'callbackUs', 'callbackUsSeries', 'initializationUs', 'snapshotReadUs', 'snapshotWriteUs'}
# Exact work fields and validated controls are classified by the shared pure schema helper.


def read_result(path, expected):
    return guard.executed_properties(path, expected)


def read_diagnostics(log, world, volume):
    rows = [line[line.index('NH_LOAD_PROFILE '):] for line in log.splitlines() if 'NH_LOAD_PROFILE ' in line]
    if world:
        headers = [row for row in rows if ' start_ns=' in row]
        if not headers or any(' valid=1 dropped=0 ' not in row for row in headers):
            raise ValueError('Missing or invalid initialization profile groups')
        if any('overflow' in row for row in rows):
            raise ValueError('Initialization profile report budget exhausted')
        if not any('metric=hero-capture ' in row for row in rows):
            raise ValueError('HeroCapture workload not observed')
        if any('json_volume_pass=' + ('1' if volume else '0') not in row for row in headers):
            raise ValueError('Unexpected profiling volume mode')
    hero = [line[line.index('NH_HERO_ACTION_TYPE_TIMING '):] for line in log.splitlines()
            if 'NH_HERO_ACTION_TYPE_TIMING ' in line]
    if any('overflow=1' in row for row in hero):
        raise ValueError('Per-type hero candidate budget exhausted')
    aggregates = [line[line.index('NH_HERO_ACTION_TIMING '):] for line in log.splitlines()
                  if 'NH_HERO_ACTION_TIMING ' in line]
    if any('records omitted' in row for row in aggregates):
        raise ValueError('Hero attempt record budget exhausted')
    return {'initializationRows': rows, 'heroActionAggregateRows': aggregates, 'heroCandidateTypeRows': hero,
            'heroTypeRowsPresent': bool(hero)}


def prepare_encounter_preset(environment, selector):
    """Activate shipping NH only in this fresh encounter's isolated test profile."""
    if not selector.startswith(ENCOUNTER):
        return
    config = Path(environment['XDG_CONFIG_HOME']) / 'vcmi'
    config.mkdir(mode=0o700)
    with (config / 'testModSettings.json').open('x') as output:
        json.dump({'activePreset': 'default', 'presets': {'default': {
            'mods': ['core', 'vcmi', 'vcmi-test', 'new-horizons'], 'settings': {}}}}, output)


def workload_sample(plan, prepared, output, selector, clock, args):
    """Own one isolated listing/body sample; parity and aggregation stay in main."""
    candidate = dict(prepared)
    candidate['environment'] = dict(candidate['environment'])
    output.mkdir(mode=0o700)
    for key, name in (('HOME', 'home'), ('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'),
                      ('XDG_CACHE_HOME', 'cache'), ('XDG_RUNTIME_DIR', 'runtime'), ('TMPDIR', 'tmp')):
        target = output / name
        target.mkdir(mode=0o700)
        candidate['environment'][key] = str(target)
    prepare_encounter_preset(candidate['environment'], selector)
    candidate['environment']['NH_BENCHMARK_POLICY'] = args.policy
    candidate['environment']['NH_MAPLOAD_PROFILE'] = '1' if args.diagnostics else '0'
    candidate['environment']['NH_MAPLOAD_PROFILE_BYTES'] = '1' if args.volume else '0'
    listing = output / 'listing'
    listing.mkdir(mode=0o700)
    base = ['--gtest_also_run_disabled_tests', '--gtest_filter=' + selector,
            '--gtest_shuffle=0', '--gtest_repeat=1', '--gtest_random_seed=1']
    guard.invoke(candidate, ['--gtest_list_tests'], listing, plan, args.diagnostics)
    membership = guard.discovery_membership((listing / 'child.log').read_text(), candidate, selector, args.gate)
    guard.invoke(candidate, base + ['--gtest_output=xml:' + str(output / 'native.xml')],
                 output, plan, args.diagnostics)
    props = read_result(output / 'native.xml', selector)
    if clock not in props or int(props[clock]) <= 0:
        raise ValueError('Missing positive elapsed interval')
    row = {'properties': props, 'discovery': membership}
    if args.diagnostics:
        native_log = output / 'cache' / 'vcmi' / 'VCMI_Test_log.txt'
        if not native_log.is_file() or native_log.is_symlink():
            raise ValueError('Ordinary native file logger evidence missing')
        row['diagnostics'] = read_diagnostics(native_log.read_text(), selector.startswith(WORLD), args.volume)
    return row



def workload_witness(props, policy):
    work, controls = work_properties.property_classes(props, policy)
    witness = {key: value for key, value in props.items()
               if key not in CLOCKS and key not in work and key not in controls and key != 'snapshotBytes'}
    return witness, controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--workload', required=True, choices=WORKLOADS)
    parser.add_argument('--execute', required=True, action='store_true')
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--gate', action='store_true')
    parser.add_argument('--max-regression-percent', type=float)
    parser.add_argument('--volume', action='store_true')
    parser.add_argument('--policy', choices=['Full', 'Fast'], default='Full')
    parser.add_argument('--compare-choices', action='store_true')
    args = parser.parse_args()
    os.umask(0o077)
    if args.volume and not args.diagnostics:
        raise ValueError('Volume inspection requires a separate diagnostic pass')
    if guard.digest(args.manifest) != args.manifest_sha256:
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
    selector, clock = WORKLOADS[args.workload]
    for candidate in plan['candidates'].values():
        guard.expected_discovery_cases(candidate, selector, args.gate)
    if not args.output.is_absolute() or args.output.exists():
        raise ValueError('Fresh absolute private output required')
    args.output.mkdir(mode=0o700)
    if args.compare_choices and (args.gate or not selector.startswith(ENCOUNTER) or args.policy != 'Fast'):
        raise ValueError('Choice reports require an explicit Fast encounter without an exact-parity timing gate')
    samples, reference = [], None
    for pair in range(plan['pairs']):
        for arm in (('baseline', 'candidate') if pair % 2 == 0 else ('candidate', 'baseline')):
            row = workload_sample(plan, plan['candidates'][arm],
                                  args.output / (str(pair) + '-' + arm), selector, clock, args)
            props = row['properties']
            witness, row['diagnosticControls'] = workload_witness(props, args.policy)
            if reference is None:
                reference = witness
            differences = [key for key in reference.keys() | witness.keys() if witness.get(key) != reference.get(key)]
            row.update(pair=pair, arm=arm, semanticDifferences=differences)
            samples.append(row)
            (args.output / 'samples.json').write_text(json.dumps(samples, indent=2) + '\n')
            input_keys = ('initialEvidence', 'seed', 'policy', 'scope')
            if differences and (not args.compare_choices or any(key in differences for key in input_keys)):
                raise ValueError('Exact semantic parity failed: ' + ', '.join(differences))
    summary = {'exactSemanticParity': all(not row['semanticDifferences'] for row in samples), 'workload': args.workload, 'diagnostics': args.diagnostics,
               'volumeInspection': args.volume, 'policy': args.policy,
               'choiceReportOnly': args.compare_choices, 'decisionQualityAccepted': False, 'quietTimingSamples': not args.diagnostics,
               'discoveryMembershipVerified': all(row['discovery']['verified'] for row in samples),
               'candidateGameplayAccepted': False,
               'scope': 'Native fixture only; no rendered, disk-save, lobby or playable-delivery acceptance'}
    if args.compare_choices:
        summary['choiceComparison'] = [
            {'pair': row['pair'], 'arm': row['arm'],
             'outcome': json.loads(row['properties']['outcome']),
             'actions': json.loads(row['properties']['submittedActions']),
             'rounds': int(row['properties']['rounds']),
             'callbacks': int(row['properties']['callbacks']),
             'changedWitnesses': row['semanticDifferences']} for row in samples]
    for arm in ('baseline', 'candidate'):
        values = [int(row['properties'][clock]) for row in samples if row['arm'] == arm]
        summary[arm] = {'samples': len(values), 'metric': clock, 'medianUs': statistics.median(values),
                        'minUs': min(values), 'maxUs': max(values)}
    if args.gate:
        summary['regressionGate'] = guard.relative_regression_gate(
            summary['baseline']['medianUs'], summary['candidate']['medianUs'], margin)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if args.gate and not summary['regressionGate']['passed']:
        raise SystemExit(2)



if __name__ == '__main__':
    main()
