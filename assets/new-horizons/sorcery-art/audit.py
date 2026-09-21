#!/usr/bin/env python3
"""CC0: static draft contract checks, not artistic/in-game acceptance.
Read-only unless --report supplies an isolated output path. Negative controls
are synthetic in-memory mutations; never overwrite reviewed concept bytes.
"""
import argparse, hashlib, json
from pathlib import Path
from PIL import Image, ImageStat, ImageChops
ROOT=Path(__file__).resolve().parent

def validate_image(im,record):
 assert im.mode=='RGBA','mode'
 assert list(im.size)==record['size'],'size'
 alpha=im.getchannel('A')
 assert alpha.getextrema()==(0,255),'alpha coverage'
 assert alpha.getbbox(),'empty'
 w,h=im.size
 if record['kind']=='header':
  assert not alpha.crop((0,0,w,28)).getbbox(),'header margin'
  assert alpha.crop((0,28,w,h)).getbbox(),'missing illustration'
 else:
  # No visible pixels meet the canvas edge: margin for filter/placement safety.
  assert not any(alpha.crop(box).getbbox() for box in [(0,0,w,1),(0,h-1,w,h),(0,0,1,h),(w-1,0,w,h)]),'edge clipping'

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--report',type=Path);args=ap.parse_args()
 manifest=json.loads((ROOT/'import-manifest.json').read_text()); records=manifest['assets']; report={'scope':'Static only; no human legibility, GUI or user approval inferred','assets':len(records),'failures':[],'negative_controls':{},'state_mean_luma':{}}
 assert len(records)==40
 for r in records:
  p=ROOT/r['draft'];svg=ROOT/r['source']
  assert hashlib.sha256(p.read_bytes()).hexdigest()==r['png_sha256']
  assert hashlib.sha256(svg.read_bytes()).hexdigest()==r['source_sha256']
  text=svg.read_text()
  assert '<image' not in text and 'href=' not in text and '<script' not in text and '<text' not in text
  try:validate_image(Image.open(p),r)
  except AssertionError as e:report['failures'].append({'path':r['draft'],'reason':str(e)})
 for direction in ('meridian','aperture'):
  rr=[r for r in records if r['direction']==direction]
  assert len({r['runtime'] for r in rr})==20
  statehashes=[r['png_sha256'] for r in rr if r['kind']=='button'];assert len(set(statehashes))==4
  for r in rr:
   if r['kind'] in ('button','bookmark'):
    im=Image.open(ROOT/r['draft']); report['state_mean_luma'][direction+'/'+r['state']]=round(ImageStat.Stat(im.convert('L'),im.getchannel('A')).mean[0],3)
  for size in ('small','medium','large','scenarioBonus'):
   hashes=[r['png_sha256'] for r in rr if r['kind']=='skill' and r['draft'].endswith('_'+size+'.png')];assert len(set(hashes))==3
 r=next(r for r in records if r['kind']=='header');im=Image.open(ROOT/r['draft'])
 controls={'wrong_size':im.resize((159,96)), 'wrong_mode':im.convert('RGB'), 'blank':Image.new('RGBA',im.size), 'opaque':im.copy(), 'dirty_top_margin':im.copy()}
 controls['opaque'].putalpha(255);controls['dirty_top_margin'].putpixel((50,10),(255,0,0,255))
 for label,bad in controls.items():
  try:validate_image(bad,r)
  except AssertionError as e:report['negative_controls'][label]='rejected: '+str(e)
  else:raise AssertionError('negative control accepted: '+label)
 r=next(r for r in records if r['kind']=='button');bad=Image.open(ROOT/r['draft']);bad.putpixel((0,30),(255,255,255,255))
 try:validate_image(bad,r)
 except AssertionError as e:report['negative_controls']['clipped_edge']='rejected: '+str(e)
 else:raise AssertionError('edge negative accepted')
 report['pass']=not report['failures']
 if args.report:
  out=args.report.resolve();allowed=(ROOT,Path('build/new-horizons-linux/research/sorcery-art').resolve());assert any(out.is_relative_to(a) for a in allowed)
  out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
 if not report['pass']:raise SystemExit(1)
if __name__=='__main__':main()
