#!/usr/bin/env python3
"""Original CC0 Sorcery studies. Offline only; Inkscape CLI + Pillow, no art inputs.
SVGs are editable delivery sources. --render-only preserves manual SVG edits.
"""
import argparse, hashlib, json, math, subprocess
from pathlib import Path
from PIL import Image, ImageDraw
ROOT = Path(__file__).resolve().parent
DEFS = '''<defs>
 <linearGradient id="brass" x1="0" y1="0" x2=".8" y2="1" gradientUnits="objectBoundingBox"><stop stop-color="#fff0ac"/><stop offset=".18" stop-color="#bc9450"/><stop offset=".38" stop-color="#665036"/><stop offset=".52" stop-color="#dfb76b"/><stop offset=".72" stop-color="#8a612f"/><stop offset="1" stop-color="#392b26"/></linearGradient>
 <linearGradient id="cloth"><stop stop-color="#121522"/><stop offset=".18" stop-color="#323952"/><stop offset=".3" stop-color="#535976"/><stop offset=".43" stop-color="#22283d"/><stop offset=".63" stop-color="#424763"/><stop offset=".8" stop-color="#191c30"/><stop offset="1" stop-color="#303550"/></linearGradient>
 <radialGradient id="glass" cx=".32" cy=".24" r=".78"><stop stop-color="#e2f0df"/><stop offset=".16" stop-color="#98c3c9"/><stop offset=".38" stop-color="#507d98"/><stop offset=".65" stop-color="#283952"/><stop offset=".86" stop-color="#121b31"/><stop offset="1" stop-color="#677d9a"/></radialGradient>
 <radialGradient id="field" cx=".42" cy=".35" r=".85"><stop stop-color="#554b44"/><stop offset=".55" stop-color="#302e35"/><stop offset="1" stop-color="#171821"/></radialGradient>
 <linearGradient id="silver" x2=".5" y2="1"><stop stop-color="#f1f0d9"/><stop offset=".4" stop-color="#95b8c4"/><stop offset=".6" stop-color="#4c647f"/><stop offset="1" stop-color="#263044"/></linearGradient>
 </defs>'''
def path(d, fill='none', stroke=None, width=1, extra=''):
 return f'<path d="{d}" fill="{fill}" '+(f'stroke="{stroke}" stroke-width="{width}" ' if stroke else '')+extra+'/>'
def ellipse(cx,cy,rx,ry,fill,stroke=None,width=1,extra=''):
 return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{fill}" '+(f'stroke="{stroke}" stroke-width="{width}" ' if stroke else '')+extra+'/>'
def instrument(direction, rank=3, small=False):
 # Nominal 64x64; all curves are newly authored. Upper-left material lighting.
 s=ellipse(33,54,22,4,'#090f19',extra='opacity=".65"')
 s+=path('M13 54 Q20 43 20 36 L26 39 Q23 47 26 51 L43 51 Q40 45 40 38 L45 35 Q44 47 53 55 Q32 61 13 54Z','url(#brass)','#201d23',1.3)
 s+=path('M18 54 Q33 57 48 54 M23 48 L22 52 M42 47 L46 53','none','#f0d190',.8)
 if direction=='meridian':
  s+=ellipse(33,29,24,25,'none','#181b23',4,extra='transform="rotate(-26 33 29)"')
  s+=ellipse(33,29,23,24,'none','url(#brass)',3,extra='transform="rotate(-26 33 29)"')
  s+=ellipse(32,29,18,19,'url(#glass)','#172235',1)
  s+=path('M19 26 Q26 16 38 20 Q30 19 24 28 Q21 32 20 38 Q16 34 19 26Z','#a3cbd2',extra='opacity=".22"')
  s+=path('M22 39 Q29 42 43 32 M24 42 Q32 44 40 38','none','#7196b2',.8)
  s+=ellipse(26,18,4,2,'#e2ecdd',extra='transform="rotate(-35 26 18)" opacity=".8"')
  s+=path('M11 19 C11 34 43 48 54 36 L54 40 C41 50 10 36 10 22Z','url(#brass)','#362e2a',.7)
  s+=path('M11 20 C16 33 42 43 54 36','none','#e9cf8d',1)
  if not small:
   for x,y in [(17,29),(22,33),(28,36),(35,39),(42,40),(48,39)]:
    s+=path(f'M{x} {y} l-1 2','none','#3d302a',.8)
  s+=ellipse(22,7,3,2,'url(#brass)','#25212a',.6)
 else:
  s+=path('M12 17 L35 3 L54 19 L48 46 L26 53 L9 35Z M19 20 L15 33 L28 45 L42 40 L46 22 L34 12Z','url(#brass)','#24202a',1.4,extra='fill-rule="evenodd"')
  s+=path('M12 17 L34 6 L51 20 M10 34 L26 51 L47 44 M20 20 L34 13 L45 23','none','#e4ca88',1)
  s+=ellipse(32,28,12,17,'url(#glass)','#c9be8d',1,extra='transform="rotate(23 32 28)"')
  s+=path('M27 15 Q18 29 26 39 M31 18 Q25 28 29 33','none','url(#silver)',2)
  s+=path('M17 19 L22 23 M44 16 L41 20 M43 41 L40 37','none','#93aabc',1.5)
  if not small:
   s+=path('M15 18 L18 17 M20 15 L23 13 M25 12 L28 10 M48 26 L47 30 M46 34 L45 37','none','#675138',1)
 # Rank advances physical fittings as well as countable ivory studs.
 if rank>=2: s+=path('M7 46 L10 37 L13 46 L10 51Z','url(#silver)','#172031',.7)
 if rank>=3: s+=path('M51 12 L54 5 L57 12 L54 18Z','url(#silver)','#172031',.7)
 return s

def make_svg(direction,kind,w,h,rank=3,state='normal'):
 s=DEFS
 if kind=='header':
  s+='<g transform="translate(0 28)">'
  s+='<rect x="1" y="1" width="158" height="66" fill="url(#field)" stroke="#392a24"/>'
  # Low folded velvet foreground and recessed brass measuring panel, not an original scene.
  s+=path('M3 51 Q23 37 47 49 T98 51 T157 43 L157 65 L3 65Z','url(#cloth)')
  s+=path('M5 55 Q27 45 47 55 M91 58 Q127 47 152 54','none','#66667c',1,extra='opacity=".5"')
  s+=path('M8 12 L52 12 L52 39 L8 39Z','none','#857052',.7)
  for i in range(9): s+=path(f'M{12+i*4} 13 v{5 if i%2==0 else 3}','none','#b99b68',.6)
  s+=path('M13 31 Q24 18 35 28 T48 23 M113 20 Q127 10 146 22 M115 25 Q130 17 145 26','none','#98866c',.7)
  s+='<g transform="translate(52 -1) scale(1.05)">'+instrument(direction)+'</g>'
  s+=path('M2 66 V2 H158 M4 64 V4 H156 M158 3 V66 H3','none','url(#brass)',1)
  for x in (5,153):
   for y in (5,61): s+=ellipse(x,y,1,1,'#e3c387')
  s+='</g>'
 elif kind=='bookmark':
  selected=state=='selected'; dx=0 if selected else 5
  s+=f'<g transform="translate({dx} 0)">'
  s+=path('M5 13 L65 13 L60 49 L13 54 L6 47Z','#090e18',extra='opacity=".65"')
  s+=path('M3 5 Q33 8 73 5 L62 26 L72 46 Q37 42 3 47Z','url(#cloth)','#171821',1)
  s+=path('M5 7 Q37 10 70 7 L59 26 L69 44 Q36 40 5 45','none','url(#brass)',1)
  s+=path('M12 9 Q16 23 11 42 M21 10 Q25 23 20 41','none','#77758d',.6,extra='opacity=".35"')
  s+='<g transform="translate(19 3) scale(.65)">'+instrument(direction,2,True)+'</g>'
  if not selected:s+=path('M3 5 Q33 8 73 5 L62 26 L72 46 Q37 42 3 47Z','#101424',extra='opacity=".24"')
  else:s+=path('M4 6 Q35 9 71 6','none','#dfcda1',1)
  s+='</g>'
 else:
  is_skill=kind=='skill'; small=w==32
  s+=f'<g transform="scale({w/64} {h/64})">'
  if is_skill:
   s+='<rect x="4" y="4" width="56" height="56" fill="url(#field)" stroke="url(#brass)" stroke-width="1.5"/>'
   s+=path('M4 48 Q20 37 31 47 T60 46 L60 60 L4 60Z','url(#cloth)')
  else:
   s+=ellipse(32,33,30,30,'#131725','#493c30',1)
   s+=ellipse(32,31,29,29,'url(#field)','url(#brass)',2)
  shift=1.5 if state=='pressed' else 0
  s+=f'<g transform="translate(0 {shift})'+(' scale(.94 .88)' if is_skill else '')+'">'+instrument(direction,rank,small)+'</g>'
  if is_skill:
   # Deliberately reserved rank rail; minimum 2 native pixels per ivory stud.
   s+=path('M15 56 H49 L47 62 H17Z','#171a26','#8d7449',.7)
   for i in range(rank):
    x=32+(i-(rank-1)/2)*10
    s+=f'<rect x="{x-2.5}" y="57" width="5" height="4" fill="#e1d5ae"/>'
  if state=='highlighted':s+=ellipse(32,31,29,29,'none','#eee0b5',1.5)
  if state=='pressed':s+=path('M9 14 Q30 -1 52 15','none','#101422',3)
  if state=='disabled':s+=ellipse(32,32,28,28,'#252835',extra='opacity=".57"')
  s+='</g>'
 return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}"><title>Original Sorcery {direction} {kind} {state} rank {rank}; CC0 draft</title>{s}</svg>\n'

def specs():
 yield 'NH_sorcery_header','header',160,96,3,'normal'
 for state in ('selected','unselected'):yield 'NH_sorcery_bookmark_'+state,'bookmark',80,60,3,state
 for rank,n in [('basic',1),('advanced',2),('expert',3)]:
  for size,(w,h) in {'small':(32,32),'medium':(44,44),'large':(82,93),'scenarioBonus':(58,64)}.items():yield f'NH_sorceryMagic_{rank}_{size}','skill',w,h,n,'normal'
 yield 'NH_sorcery_icon','emblem',64,64,3,'normal'
 for state in ('normal','pressed','highlighted','disabled'):yield 'NH_sorcery_'+state,'button',64,64,3,state

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--render-only',action='store_true');args=ap.parse_args()
 records=[]
 for direction in ('meridian','aperture'):
  folder=ROOT/direction; (folder/'svg').mkdir(parents=True,exist_ok=True);(folder/'png').mkdir(exist_ok=True)
  for name,kind,w,h,rank,state in specs():
   svg=folder/'svg'/f'{name}.svg'; png=folder/'png'/f'{name}.png'
   if not args.render_only:svg.write_text(make_svg(direction,kind,w,h,rank,state))
   subprocess.run(['inkscape',str(svg),'--export-type=png',f'--export-filename={png}','--export-overwrite'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   im=Image.open(png).convert('RGBA');im.save(png,optimize=False)
   assert im.size==(w,h) and im.getbbox()
   assert im.getchannel('A').getextrema()[0]==0
   if kind=='header':assert im.crop((0,0,w,28)).getchannel('A').getbbox() is None
   records.append(dict(direction=direction,source=str(svg.relative_to(ROOT)),draft=str(png.relative_to(ROOT)),runtime='Mods/new-horizons/Images/'+png.name,size=[w,h],kind=kind,state=state,rank=rank,source_sha256=sha(svg),png_sha256=sha(png),alpha='RGBA; top28 clear' if kind=='header' else 'RGBA; exterior transparent'))
  sheet=Image.new('RGB',(640,370),'#746b5b');d=ImageDraw.Draw(sheet)
  d.text((8,5),f'{direction.upper()} - ORIGINAL CC0 CONCEPT / NOT APPROVED',fill='#ffffff')
  placements=[('NH_sorcery_header',8,25),('NH_sorcery_bookmark_selected',182,35),('NH_sorcery_bookmark_unselected',275,35)]
  for rank,y in [('basic',142),('advanced',245),('expert',245)]:
   if rank=='expert':continue
   for name,x in [('small',8),('medium',55),('large',116),('scenarioBonus',216)]:placements.append((f'NH_sorceryMagic_{rank}_{name}',x,y))
  for name,x in [('small',305),('medium',350),('large',410),('scenarioBonus',510)]:placements.append((f'NH_sorceryMagic_expert_{name}',x,245))
  for i,state in enumerate(('icon','normal','pressed','disabled','highlighted')):placements.append(('NH_sorcery_'+state,300+i*67,142))
  for name,x,y in placements:
   im=Image.open(folder/'png'/f'{name}.png');sheet.paste(im,(x,y),im)
  for i,state in enumerate(('selected','unselected')):
   im=Image.open(folder/'png'/f'NH_sorcery_bookmark_{state}.png').resize((68,51),Image.Resampling.LANCZOS);sheet.paste(im,(390+i*85,38),im)
  for text,x,y in [('native header / bookmarks',8,124),('compact 68x51',390,96),('Basic',8,130),('Advanced',8,233),('Expert',305,233),('icon / normal / pressed / disabled / hover',300,130)]:d.text((x,y),text,fill='white')
  sheet.save(folder/'native-review.png');sheet.resize((1280,740),Image.Resampling.NEAREST).save(folder/'nearest-2x-review.png')
 (ROOT/'import-manifest.json').write_text(json.dumps({'status':'TWO UNAPPROVED CONCEPTS; import exactly ONE family only after gates','button_frame_order':['normal','pressed','disabled','highlighted'],'bookmark_frame_order':['selected','unselected'],'exporter_sha256':sha(Path(__file__)),'assets':records},indent=2)+'\n')
 print('Validated 40 isolated concept PNGs; no live writes.')
if __name__=='__main__':main()
