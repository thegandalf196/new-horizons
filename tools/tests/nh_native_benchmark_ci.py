#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Portable opt-in native performance gate; no original assets or CI runner supplied.

Nonzero on missing inputs, failed native work/parity, or a matched regression.
Requires an existing controlled quiet host with privately mounted purchaser data.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import nh_ordinary_decision_benchmark as guard
import nh_native_acceptance_workloads as workloads


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--workload', action='append', required=True,
                        choices=['ordinary', *workloads.WORKLOADS])
    parser.add_argument('--max-regression-percent', type=float, required=True)
    parser.add_argument('--execute', required=True, action='store_true')
    args = parser.parse_args()
    if not args.output.is_absolute() or args.output.exists():
        raise ValueError('Fresh absolute private CI output required')
    args.output.mkdir(mode=0o700)
    result = {'status': 'NOT_RUN', 'workloads': []}
    try:
        if guard.digest(args.manifest) != args.manifest_sha256:
            raise ValueError('Reviewed manifest changed')
        plan = json.loads(args.manifest.read_text())
        guard.quiet_lease(plan)
        guard.pinned_files(plan['harnessPins'])
        required = [Path(__file__).resolve(), Path(guard.__file__).resolve(), Path(workloads.__file__).resolve(),
                    Path(workloads.work_properties.__file__).resolve(),
                    Path(sys.executable).resolve(strict=True)]
        if any(str(path) not in plan['harnessPins'] for path in required):
            raise ValueError('CI entrypoint, runners, property schema and interpreter must be reviewed/pinned')
        for candidate in plan['candidates'].values():
            guard.pinned_files(candidate['pins'])
            if not Path(candidate['cwd']).is_dir():
                raise ValueError('Resource working directory unavailable')
        for index, workload in enumerate(args.workload):
            guard.quiet_lease(plan)
            guard.pinned_files(plan['harnessPins'])
            script = Path(guard.__file__) if workload == 'ordinary' else Path(workloads.__file__)
            command = [str(Path(sys.executable).resolve(strict=True)), str(script),
                       '--manifest', str(args.manifest), '--manifest-sha256', args.manifest_sha256,
                       '--output', str(args.output / (str(index) + '-' + workload)), '--execute',
                       '--gate', '--max-regression-percent', str(args.max_regression_percent)]
            if workload != 'ordinary':
                command += ['--workload', workload]
            # Each runner owns and reaps its child. Do not externally time out
            # its process and strand that ownership transaction.
            with (args.output / (str(index) + '-runner.log')).open('x') as log:
                completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
            result['workloads'].append({'workload': workload, 'returncode': completed.returncode})
            result['status'] = 'FAILED'
            if completed.returncode:
                return 2
        guard.pinned_files(plan['harnessPins'])
        result['status'] = 'PASSED'
        return 0
    except (OSError, ValueError, KeyError) as error:
        result['error'] = str(error)
        return 2
    finally:
        (args.output / 'ci-result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
