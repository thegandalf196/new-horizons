#!/usr/bin/env python3
"""CC0 review utility only. No license is assigned to supplied external images.
Hash-bound, no crop/paint/sharpening, writes ONLY ignored Artist review directory.
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from PIL import Image,ImageDraw,__version__ as pillow_version
def png_chunks(path):
 data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n';pos=8;chunks=[]
 while pos<len(data):
  assert pos+12<=len(data)
  length=struct.unpack_from('>I',data,pos)[0];end=pos+12+length;assert end<=len(data)
  kind=data[pos+4:pos+8];payload=data[pos+8:pos+8+length]
  assert zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',data,pos+8+length)[0]
  chunks.append({'type':kind.decode('ascii'),'bytes':length});pos=end
  if kind==b'IEND':break
 assert pos==len(data) and chunks[-1]['type']=='IEND'
 return chunks

ap=argparse.ArgumentParser();ap.add_argument('master',type=Path);ap.add_argument('--sha256',required=True);args=ap.parse_args()
master=args.master.resolve();before=hashlib.sha256(master.read_bytes()).hexdigest();assert before==args.sha256
raw=Image.open(master);assert raw.size==(1254,1254) and raw.mode=='RGB'
# Fresh storage explicitly excludes inherited metadata. Original context retained.
im=Image.new('RGBA',raw.size);im.paste(raw.convert('RGBA'))
out=Path('build/new-horizons-linux/research/sorcery-art/external-master-01');out.mkdir(parents=True,exist_ok=True)
for n in (44,32):
 im.convert('RGBa').resize((n,n),Image.Resampling.LANCZOS).convert('RGBA').save(out/f'native-{n}.png')
slot=Image.new('RGBA',(82,93));slot.paste(im.convert('RGBa').resize((82,82),Image.Resampling.LANCZOS).convert('RGBA'),(0,5));slot.save(out/'slot-82x93.png')
sheet=Image.new('RGB',(430,166),'#756b5c');d=ImageDraw.Draw(sheet)
d.text((8,8),'EXTERNAL MASTER 01 / TECHNICAL REVIEW ONLY',fill='white')
for name,x,y,label in [('native-44',12,47,'44x44'),('native-32',89,47,'32x32'),('slot-82x93',174,44,'82x93 slot')]:
 a=Image.open(out/(name+'.png'));sheet.paste(a,(x,y),a);d.text((x,27),label,fill='white')
d.text((8,146),'No crop / no paint / no visual or rights approval',fill='white');sheet.save(out/'native-review.png')
records={}
for p in sorted(out.glob('*.png')):
 a=Image.open(p);assert not a.info
 chunks=png_chunks(p);assert all(c['type'] in ('IHDR','IDAT','IEND') for c in chunks)
 records[p.name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'size':a.size,'mode':a.mode,'alpha_extrema':a.getchannel('A').getextrema() if a.mode=='RGBA' else None,'png_chunks':chunks}
assert hashlib.sha256(master.read_bytes()).hexdigest()==before
sidecar=master.with_suffix('.provenance.md')
report={'status':'external supplied raster technical review only; not approved or rights-cleared','input_name':master.name,'input_sha256':before,'input_size':raw.size,'input_mode':raw.mode,'input_metadata_keys':sorted(raw.info),'input_png_chunks':png_chunks(master),'metadata_caution':'Empty Pillow.info is NOT absence of PNG metadata. caBX/C2PA carrier presence is not signature or tool-history verification.','provenance_sidecar_sha256':hashlib.sha256(sidecar.read_bytes()).hexdigest() if sidecar.exists() else None,'tool':'Pillow '+pillow_version,'filter':'Lanczos on premultiplied RGBa -> straight RGBA; no crop, retouch, sharpening or color grading','slot':'82x82 unchanged square composition at(0,5) in82x93 transparent canvas; not stretched or rank-assigned','master_unchanged':True,'rights':'not inferred; producer report and applicable terms require independent review','outputs':records}
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
