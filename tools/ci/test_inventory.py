#!/usr/bin/env python3
"""Classify lexical test sources separately from supplied CTest discovery.

No configure, compilation, discovery or test process is launched. Optional
--ctest-json consumes a previously generated `ctest --show-only=json-v1` file;
its names describe registration, never execution or gameplay acceptance.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
DECLARATION = re.compile(r'\b(TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
SOURCE = re.compile(r'(?:\.\./)?(?:[\w.-]+/)*[\w.-]+\.cpp')
CLASSIFICATIONS = {'default', 'conditional', 'managed', 'manual', 'exporter', 'retired'}


def cmake_references(root, files):
    """Track explicit source tokens and enclosing conditions, not CMake execution."""
    references = {}
    for filename in files:
        cmake = root / filename
        conditions = []
        for number, line in enumerate(cmake.read_text().splitlines(), 1):
            line = line.split('#', 1)[0].strip()
            if line.startswith('endif('):
                if conditions:
                    conditions.pop()
            elif line.startswith(('elseif(', 'else(')):
                if conditions:
                    conditions[-1] = line
            elif line.startswith('if('):
                conditions.append(line)
            for token in SOURCE.findall(line):
                source = (cmake.parent / token).resolve()
                if source.is_relative_to(root.resolve()) and source.is_file():
                    name = source.relative_to(root.resolve()).as_posix()
                    references.setdefault(name, []).append({
                        'cmake': cmake.relative_to(root).as_posix(),
                        'line': number, 'conditions': list(conditions)})
    return references


def classify(name, text, references, policy):
    if name in policy['unregistered']:
        return policy['unregistered'][name]
    if name.endswith('.py'):
        return {'classification': 'conditional' if name.startswith('tools/tests/') else 'manual',
                'reason': policy['rootPolicies'][next(p for p in policy['roots'] if name.startswith(p + '/'))]}
    if name not in references:
        return {'classification': 'manual', 'reason': 'Support source without lexical test declaration or standalone entry; not independently scheduled.'}
    if 'NH_EXPORT_' in text:
        return {'classification': 'exporter', 'reason': 'Explicit environment-gated fixture exporter; writes require separate reviewed authority.'}
    if any('ENABLE_NEW_HORIZONS_MANAGED_TESTS' in c for r in references[name] for c in r['conditions']):
        return {'classification': 'managed', 'reason': 'Explicit managed-profile build condition; sentinel/profile absence must not count as acceptance.'}
    if any(r['conditions'] for r in references[name]):
        return {'classification': 'conditional', 'reason': 'Explicit CMake feature/backend condition plus native profile/data requirements where applicable.'}
    return {'classification': 'default', 'reason': 'Explicit ordinary CMake source reference only; no execution inferred. Native fixtures still require isolated TEST data.'}


def inventory(root, policy):
    references = cmake_references(root, policy['cmakeFiles'])
    records, errors, seen = [], [], set()
    for directory in policy['roots']:
        for path in sorted((root / directory).rglob('*')):
            if path.suffix not in {'.cpp', '.py'} or not path.is_file():
                continue
            if any(part in policy['excludedDirectories'] for part in path.parts):
                continue
            name = path.relative_to(root).as_posix()
            text = path.read_text()
            declarations = ([{'macro': macro, 'name': suite + '.' + case}
                             for macro, suite, case in DECLARATION.findall(text)]
                            if path.suffix == '.cpp' else [])
            entry = bool(re.search(r'\bint\s+main\s*\(', text))
            seen.add(name)
            if path.suffix == '.cpp' and (declarations or entry):
                if name not in references and name not in policy['unregistered']:
                    errors.append('Unclassified unregistered entry: ' + name)
                if name in references and name in policy['unregistered']:
                    errors.append('Stale unregistered disposition: ' + name)
            row = classify(name, text, references, policy)
            records.append(dict(path=name, **row, lexicalDeclarations=declarations,
                                standaloneEntry=entry, cmakeReferences=references.get(name, [])))
    for name, row in policy['unregistered'].items():
        if name not in seen or row['classification'] not in CLASSIFICATIONS or not row['reason']:
            errors.append('Invalid/stale disposition: ' + name)
    return records, errors


def discovered_tests(path, required):
    data = json.loads(path.read_text())
    names = [test['name'] for test in data['tests']]
    missing = sorted(set(required) - set(names))
    return {'source': str(path), 'names': names, 'count': len(names),
            'missingPureTargets': missing, 'executionEstablished': False}


def pure_contract(root, policy):
    errors = []
    cmake = (root / 'test/CMakeLists.txt').read_text()
    groups = []
    for body in re.findall(r'set_tests_properties\(([^)]*)\)', cmake, re.S):
        label = re.search(r'PROPERTIES\s+LABELS\s+"([^"]+)"', body)
        if label:
            groups.append((body[:label.start()], label.group(1)))
    labeled = {name for targets, labels in groups if policy['pureCTestLabel'] in labels.split(';')
               for name in targets.split()}
    required = set(policy['pureTargets'])
    if labeled != required:
        errors.append('Pure CMake label membership differs from reviewed targets')
    presets = json.loads((root / 'CMakePresets.json').read_text())
    build = next(p for p in presets['buildPresets'] if p['name'] == 'new-horizons-linux-test')
    test = next(p for p in presets['testPresets'] if p['name'] == 'new-horizons-linux-test')
    if set(build['targets']) != required:
        errors.append('Pure build preset must contain exactly reviewed standalone targets')
    if test['filter']['include']['label'] != '^nh-pure$' or test['execution']['noTestsAction'] != 'error':
        errors.append('Pure test preset must select reviewed label and refuse empty discovery')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--policy', type=Path, default=Path(__file__).with_suffix('.json'))
    parser.add_argument('--ctest-json', type=Path)
    parser.add_argument('--details', action='store_true', help='Print every source and lexical declaration')
    args = parser.parse_args()
    policy = json.loads(args.policy.read_text())
    records, errors = inventory(args.root.resolve(), policy)
    errors.extend(pure_contract(args.root.resolve(), policy))
    result = {'scope': 'lexical inventory, not configured discovery or execution',
              'sources': len(records), 'lexicalDeclarations': sum(len(r['lexicalDeclarations']) for r in records),
              'classifications': {c: sum(r['classification'] == c for r in records) for c in sorted(CLASSIFICATIONS)},
              'unregisteredDispositions': policy['unregistered'], 'errors': errors}
    if args.details:
        result['records'] = records
    if args.ctest_json:
        result['configuredDiscovery'] = discovered_tests(args.ctest_json, policy['pureTargets'])
        errors.extend('Missing configured pure target: ' + n
                      for n in result['configuredDiscovery']['missingPureTargets'])
    print(json.dumps(result, indent=2))
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
