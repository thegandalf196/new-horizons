#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One final bounded formation-only A/B trial; no same-thread engine experiment.

Requires frozen snapshot, prepared profiles/manifest and independently inspected
initial/changed/restored hero-state fingerprints. Trace ON in both variants.
Only two real formation changes. Abort on any observation/state/route/exit mismatch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

if not __debug__:
    raise SystemExit('Python optimization disables correctness assertions; refusing this test')

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--evidence',required=True,type=Path)
p.add_argument('--snapshot',required=True,type=Path)
p.add_argument('--trial',required=True,type=int)
a=p.parse_args(); e=a.evidence.resolve(); n=a.trial
guard=e/'display-guard.json'; m=json.loads((e/'manifest.json').read_text())
mode=m['modes'][n-1]; profile=Path((e/'profile-root.txt').read_text().strip())/f'profile-{n}'
def run(*args):
    subprocess.run(list(map(str,args)),check=True)
def inputs(*args):
    run('python3','tools/tests/nh-private-input.py','--guard',guard,*args)
inputs()
env=os.environ.copy();env.pop('NH_PERF_QUEUED_SET_FORMATION',None)
if mode=='typed':env['NH_PERF_QUEUED_SET_FORMATION']='1'
subprocess.run(['python3','tools/tests/nh-perf-launch-trial.py','--snapshot',str(a.snapshot),
    '--profile',str(profile),'--evidence',str(e),'--guard',str(guard),'--assets','..',
    '--trial',str(n),'--trace','ON'],env=env,check=True)
result={'trial':n,'mode':mode,'state_fingerprints':[]}
for toggle,y in [(1,605),(2,568)]:
    run('python3','tools/tests/nh-perf-pixel-probe.py','--guard',guard,'--client',e/f'client{n}.json',
        '--output',e/f'run{n}-toggle{toggle}.json','--roi',787,552,48,68,'--click',810,y)
    inputs('wait',.5,'click',936,602,'click',1120,212,'wait',.3)
    inputs()  # Revalidate owned private display immediately before external capture.
    run('env','DISPLAY=:191','import','-window','root',e/'current.png')
    raw=subprocess.check_output(['convert',str(e/'current.png'),'-crop','600x550+310+70','+repage','rgba:-'])
    fingerprint=hashlib.sha256(raw).hexdigest();result['state_fingerprints'].append(fingerprint)
    (e/f'run{n}-correctness.json').write_text(json.dumps(result,indent=2)+'\n')
    if fingerprint!=m['changed_then_restored_fingerprints'][toggle-1]:
        raise SystemExit('Reopened hero state mismatch: inspect; no silent retry')
inputs('click',936,602,'key','o','click',770,565,'click',600,410,'wait',1)
client=json.loads((e/f'client{n}.json').read_text());status_file=e/f'run{n}-exit.json'
deadline=time.monotonic()+5
while time.monotonic()<deadline and (Path('/proc',str(client['pid'])).exists() or not status_file.is_file()):time.sleep(.1)
if Path('/proc',str(client['pid'])).exists() or not status_file.is_file():raise SystemExit('Exact exit evidence missing')
result['exit']=json.loads(status_file.read_text());assert result['exit'].get('exit_code')==0,result
(e/f'run{n}-client.log').write_bytes((profile/'cache/vcmi/VCMI_Client_log.txt').read_bytes())
trace=[json.loads(line) for line in (e/f'run{n}-trace.jsonl').read_text().splitlines()]
result['trace_metadata']=trace[0];result['trace_records']=len(trace)-1
assert not trace[0]['dropped'] and not trace[0]['metadata_evicted'],'Lost trace records'
commands=[v for v in trace if v.get('stage')=='command_submit' and v.get('packet_type')=='12SetFormation']
applied=[v for v in trace if v.get('stage')=='client_state_applied' and v.get('packet_type')=='15ChangeFormation']
routes=[v for v in trace if v.get('stage')=='formation_typed_queued']
assert len(commands)==len(applied)==2,'Expected exactly two real formation changes'
assert len(routes)==(2 if mode=='typed' else 0),'Actual typed route mismatch'
ids={(v['request_id'],v['player']) for v in commands};assert len(ids)==2
if mode=='typed':assert {(v['request_id'],v['player']) for v in routes}==ids
acks=[v for v in trace if v.get('stage')=='ack' and (v.get('request_id'),v.get('player')) in ids]
assert len(acks)==2 and all(v.get('detail')==1 for v in acks),'Missing/failed formation ACK'
result.update(requests=sorted(ids),typed_route_count=len(routes),state_apply_count=len(applied),success_ack_count=len(acks))
(e/f'run{n}-correctness.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
