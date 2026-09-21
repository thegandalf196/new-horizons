#!/usr/bin/env python3
"""CC0: native-size quality proof from actual render. No proprietary inputs.
Straight RGBA output, premultiplied-alpha filtering; no stretching or sharpening.
"""
from pathlib import Path
import hashlib,json
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parent;OUT=ROOT/'output'
raw=Image.open(OUT/'master.png').convert('RGBA');assert raw.size==(768,768)
# Blender raw PNG contains scene/file metadata. Keep it private; publish only a
# fresh image with identical RGBA pixels and no inherited text/path metadata.
im=Image.new('RGBA',raw.size);im.paste(raw);im.save(OUT/'master-review.png')
for size in (44,32):
 small=im.convert('RGBa').resize((size,size),Image.Resampling.LANCZOS).convert('RGBA')
 small.save(OUT/f'native-{size}.png')
slot=Image.new('RGBA',(82,93));scaled=im.convert('RGBa').resize((82,82),Image.Resampling.LANCZOS).convert('RGBA');slot.paste(scaled,(0,5));slot.save(OUT/'slot-82x93.png')
sheet=Image.new('RGB',(320,156),'#706351');d=ImageDraw.Draw(sheet)
d.text((8,8),'V3 RENDER QUALITY PROOF / NOT APPROVED',fill='white')
for name,x,y in [('native-44',12,49),('native-32',79,49),('slot-82x93',158,38)]:
 a=Image.open(OUT/(name+'.png'));sheet.paste(a,(x,y),a);d.text((x,23),name.replace('native-','').replace('slot-',''),fill='white')
d.text((8,140),'Actual-size views; see separate 768 master',fill='white');sheet.save(OUT/'native-review.png')
files=[ROOT/'scene.py',ROOT/'reduce.py']+sorted(p for p in OUT.iterdir() if p.suffix in ('.png','.blend','.txt'))
manifest={'status':'ONE unapproved original rendered still-life quality proof; not final family; raw master/blend/settings PRIVATE pending review','reduction':'Pillow Lanczos on premultiplied RGBa, convert to straight RGBA; no stretch/sharpen','slot':'82x82 aspect-preserving render centred vertically in82x93 transparent canvas','files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Native44/32 and aspect-preserving82x93 export PASS; no art acceptance inferred')
