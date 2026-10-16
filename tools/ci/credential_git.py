# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolated credential Git transport; finite R1/R2 reader stays unchanged."""
import os
import subprocess
from git_publication_blobs import InspectionError, MAX_TEXT_BYTES


def git(root, *args, data=None, deadline=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    env.update(GIT_CONFIG_GLOBAL='/dev/null', GIT_CONFIG_SYSTEM='/dev/null',
               GIT_CONFIG_NOSYSTEM='1', GIT_OPTIONAL_LOCKS='0',
               GIT_NO_LAZY_FETCH='1', GIT_NO_REPLACE_OBJECTS='1', LC_ALL='C')
    argv = ['/usr/bin/git', '--no-lazy-fetch', '--no-replace-objects', '-c', 'core.fsmonitor=false', '-C', str(root), *args]
    if deadline is not None:
        from scanner_runtime import run, ScannerError
        try:
            code, output, _ = run(argv, data or b'', root, deadline,
                                  stdout_limit=MAX_TEXT_BYTES + 1048576,
                                  environment=env, timeout=30)
        except ScannerError:
            raise InspectionError('git-inspection-budget-or-process') from None
        if code:
            raise InspectionError('git-inspection-failed')
        return output
    try:
        result = subprocess.run(['/usr/bin/git', '--no-lazy-fetch', '--no-replace-objects', '-c', 'core.fsmonitor=false', '-C', str(root), *args],
                                input=data, capture_output=True, env=env, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        raise InspectionError('git-inspection-unavailable') from None
    if result.returncode:
        raise InspectionError('git-inspection-failed')
    return result.stdout
