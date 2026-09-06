#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute one authorized prepared paired trial on the owned private Xvfb.

Caller freezes snapshot/profile/manifest and first visually validates coordinates
and expected hero/resource crop fingerprints. No fallback, no build, no silent
retry. A mismatch stops before quitting for sole Tester inspection;600s launch
supervisor remains the safety bound. Raw image evidence stays local.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--evidence', type=Path, required=True)
p.add_argument('--snapshot', type=Path, required=True)
p.add_argument('--trial', type=int, required=True)
a = p.parse_args(); e = a.evidence.resolve(); n = a.trial
guard = e / 'display-guard.json'
manifest = json.loads((e / 'manifest.json').read_text())
profile = Path((e / 'profile-root.txt').read_text().strip()) / f'profile-{n}'
mode = manifest['modes'][n-1]
def run(*args):
    subprocess.run(list(map(str, args)), check=True)
def inputs(*args):
    run('python3','tools/tests/nh-private-input.py','--guard',guard,*args)
def probe(kind, roi, click):
    run('python3','tools/tests/nh-perf-pixel-probe.py','--guard',guard,'--client',e/f'client{n}.json',
        '--output',e/f'run{n}-{kind}.json','--roi',*roi,'--click',*click)
run('python3','tools/tests/nh-perf-launch-trial.py','--snapshot',a.snapshot,'--profile',profile,
    '--evidence',e,'--guard',guard,'--assets','..','--trial',n,'--trace',mode)
probe('ui',(350,180,32,32),(936,602))
inputs('key','e','wait',.3)
probe('day',(1095,699,165,14),(600,410))
inputs('wait',1,'click',544,369,'wait',.3)
probe('move',(526,360,8,8),(544,369))
inputs('wait',.5,'click',1120,212)
# Not in timed polling. Fingerprints were approved from the first inspected pair.
inputs()  # Fresh owned-Xvfb identity/socket check immediately before capture.
run('env','DISPLAY=:191','import','-window','root',e/'current.png')
fingerprints = {}
for label, crop in [('hero','600x550+310+70'),('resources','780x18+490+697')]:
    raw = subprocess.check_output(['convert',str(e/'current.png'),'-crop',crop,'+repage','rgba:-'])
    fingerprints[label] = hashlib.sha256(raw).hexdigest()
result = {'trial':n,'mode':mode,'fingerprints':fingerprints,'wall_checked_ns':time.time_ns()}
(e/f'run{n}-correctness.json').write_text(json.dumps(result,indent=2)+'\n')
if fingerprints != manifest['expected_fingerprints']:
    raise SystemExit('State crop mismatch: inspect current.png; no silent retry')
inputs('click',936,602,'key','o','click',770,565,'click',600,410,'wait',1)
client = json.loads((e/f'client{n}.json').read_text())
status_file = e / f'run{n}-exit.json'
deadline = time.monotonic() + 5
while time.monotonic() < deadline and (Path('/proc',str(client['pid'])).exists() or not status_file.is_file()):
    time.sleep(.1)
if Path('/proc',str(client['pid'])).exists() or not status_file.is_file():
    raise SystemExit('Client disappearance or exact exit status missing: inspect before next trial')
status = json.loads(status_file.read_text())
result['supervisor_result'] = status
(e/f'run{n}-correctness.json').write_text(json.dumps(result,indent=2)+'\n')
if status.get('exit_code') != 0:
    raise SystemExit(f'Nonzero/failed client supervisor exit: {status}; preserve this trial')
(e/f'run{n}-client.log').write_bytes((profile/'cache/vcmi/VCMI_Client_log.txt').read_bytes())
result['exit_zero_confirmed_ns'] = time.time_ns()
if mode == 'ON':
    lines = (e/f'run{n}-trace.jsonl').read_text().splitlines()
    result['trace_metadata'] = json.loads(lines[0]); result['trace_records'] = len(lines)-1
    if result['trace_metadata']['dropped'] or result['trace_metadata']['metadata_evicted']:
        raise SystemExit('Trace loss: preserve failed trial and inspect; no silent retry')
(e/f'run{n}-correctness.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
