"""Admit bounded Windows build experiments and record their exact controls."""
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import json
import os
from pathlib import Path
import re
import subprocess


BRANCH = 'refs/heads/performance/quick-combat-and-ci'
FIXED_CONTROLS = {'preflight_only': False, 'use_precompiled_headers': True,
                  'pch_off_cache_seed': '', 'repack_run_id': '', 'release_channel': ''}
BOOLEAN_CONTROLS = ('use_unity_build', 'skip_pch_cache_save', 'use_hot_header_pch')
# Each tuple is (Unity, jobs, skip cache save, hot-header PCH).
PROFILES = {
    (False, 4, False, False): 'control-4',
    (True, 4, False, False): 'unity-4',
    (False, 6, False, False): 'control-6',
    (True, 6, False, False): 'unity-6',
    (False, 4, True, False): 'control-4-no-cache-save',
    (True, 4, True, False): 'unity-4-no-cache-save',
    (False, 6, True, False): 'control-6-no-cache-save',
    (True, 6, True, False): 'unity-6-no-cache-save',
    (False, 4, False, True): 'control-4-hot-pch',
    (True, 4, False, True): 'unity-4-hot-pch',
    (False, 6, False, True): 'control-6-hot-pch',
    (True, 6, False, True): 'unity-6-hot-pch',
    (False, 4, True, True): 'control-4-no-cache-save-hot-pch',
    (True, 4, True, True): 'unity-4-no-cache-save-hot-pch',
    (False, 6, True, True): 'control-6-no-cache-save-hot-pch',
    (True, 6, True, True): 'unity-6-no-cache-save-hot-pch',
}


def normalize_controls(raw):
    if type(raw) is not dict:
        raise ValueError('Performance preview requires an input object')
    controls = dict(raw)
    # GitHub sends numeric dispatch inputs as strings and can omit empty strings.
    if type(controls.get('build_jobs')) is str and controls['build_jobs'] in {'4', '6'}:
        controls['build_jobs'] = int(controls['build_jobs'])
    for name in ('pch_off_cache_seed', 'repack_run_id', 'release_channel'):
        controls.setdefault(name, '')
    expected_names = FIXED_CONTROLS.keys() | set(BOOLEAN_CONTROLS) | {'build_jobs'}
    if controls.keys() != expected_names:
        raise ValueError('Performance preview has missing or unrecognized controls')
    for name, expected in FIXED_CONTROLS.items():
        if type(controls[name]) is not type(expected) or controls[name] != expected:
            raise ValueError(f'Performance preview requires {name}={expected!r}')
    for name in BOOLEAN_CONTROLS:
        if type(controls[name]) is not bool:
            raise ValueError(f'Performance preview requires a boolean {name}')
    if type(controls['build_jobs']) is not int or controls['build_jobs'] not in {4, 6}:
        raise ValueError('Performance preview requires build_jobs=4 or 6')
    key = (controls['use_unity_build'], controls['build_jobs'],
           controls['skip_pch_cache_save'], controls['use_hot_header_pch'])
    return controls, PROFILES[key]


def validate_revision():
    if os.environ.get('GITHUB_REF') != BRANCH or os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch':
        raise ValueError('Performance preview requires the manual performance branch')
    revision = os.environ['GITHUB_SHA']
    if not re.fullmatch('[0-9a-f]{40}', revision):
        raise ValueError('Invalid source revision')
    checkout = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True, timeout=30).strip()
    if checkout != revision:
        raise ValueError('Checkout differs from the dispatched source revision')
    return revision


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        controls, profile = normalize_controls(json.loads(os.environ['TRIAL_INPUTS']))
        revision = validate_revision()
    except (ValueError, KeyError) as error:
        parser.exit(1, str(error) + '\n')
    report = {'format': 1, 'sourceRevision': revision, 'profile': profile, 'controls': controls}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
        stream.write('## Matched Windows preview trial\n')
        stream.write(f'Source: `{revision}`\n\nProfile: `{profile}`\n\n')
        stream.write(f'Controls: `{json.dumps(controls, sort_keys=True)}`\n\n')
        stream.write('Preview artifacts only. Release, tag and updater publication are separate.\n')


if __name__ == '__main__':
    main()
