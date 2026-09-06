#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Start ONE authorized frozen perf trial; no host display or build operations.

Caller prepares a fresh managed profile, guard and frozen snapshot and supplies
explicit paths. Client is bounded to600s. Returns after normal menu loading and
warmup, leaving the owned client alive for sole Tester ROI measurements/quit.
Coordinates assume the declared1280x800 private display /1280x720 window.
"""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import subprocess
import sys
import time

# Static Python supervisor, argv-only child execution; no selected path is eval'd.
SUPERVISOR_CODE = '''import json, os, subprocess, sys, time
from pathlib import Path
status = Path(sys.argv[1])
try:
    result = {"exit_code": subprocess.run(sys.argv[2:]).returncode}
except Exception as exc:
    result = {"exit_code": None, "error": str(exc)}
result["finished_wall_ns"] = time.time_ns()
temporary = status.with_name(status.name + ".tmp")
with temporary.open("x") as output:
    json.dump(result, output)
    output.write("\\n")
os.replace(temporary, status)
'''

p = argparse.ArgumentParser(description=__doc__)
for key in ('snapshot', 'profile', 'evidence', 'guard', 'assets'):
    p.add_argument('--' + key, required=True, type=Path)
p.add_argument('--trial', required=True, type=int)
p.add_argument('--trace', required=True, choices=('OFF', 'ON'))
a = p.parse_args()
root = Path.cwd()
helper = root / 'tools/tests/nh-private-input.py'
def inputs(*args):
    subprocess.run(['python3', str(helper), '--guard', str(a.guard), *map(str, args)], check=True)
inputs()  # Verify the separately created owned Xvfb before any connection/input.
e = a.evidence.resolve(); profile = a.profile.resolve(); snapshot = a.snapshot.resolve()
identity_file = e / f'client{a.trial}.json'
status_file = e / f'run{a.trial}-exit.json'
if identity_file.exists() or status_file.exists() or status_file.with_name(status_file.name + '.tmp').exists():
    raise SystemExit('Trial/status already exists; refusing duplicate launch')
env = os.environ.copy()
env.update(DISPLAY=':191', SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='dummy')
env.pop('NH_PERF_TRACE', None)
env.pop('NH_PERF_TRACE_FILE', None)
if a.trace == 'ON':
    env['NH_PERF_TRACE'] = '1'
    env['NH_PERF_TRACE_FILE'] = str(e / f'run{a.trial}-trace.jsonl')
    env['NH_PERF_TRACE_MAX_RECORDS'] = '250000'
command = ['timeout', '600', str(root / 'tools/new-horizons-launch.sh'), '--assets', str(a.assets.resolve()),
           '--client', str(snapshot / 'vcmiclient'), '--resources', str(snapshot), '--profile', str(profile)]
with (e / f'run{a.trial}-console.log').open('x') as output:
    supervisor = subprocess.Popen([sys.executable, '-c', SUPERVISOR_CODE, str(status_file), *command],
                                  stdout=output, stderr=subprocess.STDOUT,
                                  stdin=subprocess.DEVNULL, env=env, start_new_session=True)
record = {'trial': a.trial, 'trace': a.trace, 'supervisor': supervisor.pid,
          'wall_start_ns': time.time_ns(), 'mono_start_ns': time.monotonic_ns(),
          'exit_status_file': status_file.name}
try:
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        matches = []
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                cmd = (proc / 'cmdline').read_bytes().split(b'\0')[0].decode()
                if cmd.startswith(str(profile) + '/runtime.') and cmd.endswith('/vcmiclient'):
                    matches.append(proc)
            except (OSError, UnicodeError):
                pass
        if len(matches) == 1:
            process = matches[0]
            break
        if supervisor.poll() is not None:
            status = json.loads(status_file.read_text()) if status_file.is_file() else {'error': 'missing launch status'}
            raise RuntimeError(f'Client launch ended before discovery: {status}')
        time.sleep(.2)
    else:
        raise RuntimeError('No unique owned client appeared within20s')
    fields = (process / 'stat').read_text().rsplit(')', 1)[1].split()
    record.update(pid=int(process.name), start_ticks=fields[19], window='0x200009')
    identity_file.write_text(json.dumps(record, indent=2) + '\n')
    time.sleep(3)
    inputs()
    x = c.CDLL('libX11.so.6'); x.XOpenDisplay.restype = c.c_void_p
    x.XOpenDisplay.argtypes = [c.c_char_p]
    x.XMoveWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int]
    x.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
    x.XFlush.argtypes = [c.c_void_p]; x.XCloseDisplay.argtypes = [c.c_void_p]
    display = x.XOpenDisplay(b':191')
    if not display:
        raise RuntimeError('Owned private display unavailable')
    try:
        x.XMoveWindow(display, 0x200009, 0, 0)
        x.XSetInputFocus(display, 0x200009, 1, 0)
        x.XFlush(display)
    finally:
        x.XCloseDisplay(display)
    inputs('click',640,399,'wait',.4,'click',885,247,'wait',.4,'click',887,126,'wait',.4,
           'click',475,220,'click',751,615,'wait',5,'click',1120,212,'wait',.5)
    print(json.dumps(record))
except Exception as exc:
    # No silent retry/fresh state. The bounded supervisor remains the safety net;
    # Tester must inspect and stop normally on a setup failure.
    record['setup_error'] = str(exc)
    (e / f'run{a.trial}-setup-failure.json').write_text(json.dumps(record, indent=2)+'\n')
    raise
