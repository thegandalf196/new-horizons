#!/usr/bin/env python3
"""CC0 original low-resolution painted studies. Pillow only; no reference input.
Explicit material planes/brush marks, no noise, image model, font or texture input.
SVG groups and embedded-free paths are editable; JSON records runtime proposals.
"""
from pathlib import Path
import hashlib, json
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parent
class Paint:
 def __init__(self,w,h):
  self.im=Image.new('RGBA',(w,h)); self.d=ImageDraw.Draw(self.im);self.svg=[];self.t=(0,0,1,1)
 def pt(self,p):
  x,y,sx,sy=self.t;return (round(x+p[0]*sx),round(y+p[1]*sy))
 def poly(self,pts,c):
  pts=[self.pt(p) for p in pts];self.d.polygon(pts,fill=c);self.svg.append('<polygon points="'+' '.join(f'{x},{y}' for x,y in pts)+f'" fill="{c}"/>')
 def line(self,pts,c,width=1):
  pts=[self.pt(p) for p in pts];self.d.line(pts,fill=c,width=width);self.svg.append('<polyline points="'+' '.join(f'{x},{y}' for x,y in pts)+f'" fill="none" stroke="{c}" stroke-width="{width}"/>')
 def rect(self,box,c):
  a,b,cx,dy=box;self.poly([(a,b),(cx,b),(cx,dy),(a,dy)],c)
 def group(self,name,fn,x=0,y=0,sx=1,sy=None):
  old=self.t;ox,oy,osx,osy=old;self.t=(ox+x*osx,oy+y*osy,sx*osx,(sx if sy is None else sy)*osy);self.svg.append(f'<g id="{name}">');fn(self);self.svg.append('</g>');self.t=old
 def save(self,path):
  self.im.save(path.with_suffix('.png'));w,h=self.im.size
  path.with_suffix('.svg').write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" shape-rendering="crispEdges"><title>Original painted Sorcery study; CC0; unapproved</title>'+''.join(self.svg)+'</svg>\n')

def book(p):
 p.poly([(1,23),(31,12),(55,22),(31,38),(3,32)],'#211b1b')
 p.poly([(3,22),(30,13),(52,21),(31,34),(4,29)],'#6d322d')
 p.poly([(5,24),(29,17),(49,22),(30,31),(6,28)],'#b29b6b')
 p.poly([(5,19),(24,13),(31,17),(37,12),(54,15),(50,25),(31,32),(25,27),(5,28)],'#4b352a')
 p.poly([(6,18),(23,12),(30,16),(29,29),(24,25),(6,26)],'#c5b281')
 p.poly([(7,18),(22,13),(27,16),(12,21),(7,24)],'#e4d3a1')
 p.poly([(31,17),(37,12),(53,14),(48,25),(32,30)],'#d7c18f')
 p.poly([(32,20),(39,15),(49,15),(45,18),(34,24)],'#ecd9a9')
 p.poly([(29,17),(31,19),(32,29),(29,30)],'#73533b')
 for y in (19,22,25):
  p.line([(11,y),(20,y-3),(24,y-2)],'#8b7552')
  p.line([(35,y+1),(44,y-3)],'#9c855d')
 p.line([(5,30),(29,34),(49,24)],'#b79154')
 p.rect((12,30,14,32),'#d6b675')

def candle(p):
 p.poly([(2,47),(10,43),(12,28),(8,25),(8,23),(18,23),(18,26),(14,29),(15,43),(23,47),(20,50),(4,50)],'#463021')
 p.poly([(4,46),(11,44),(12,27),(14,27),(14,44),(21,47),(17,48),(6,48)],'#ad854b')
 p.line([(8,46),(12,45),(13,31)],'#d4b578')
 p.poly([(9,24),(9,8),(17,8),(17,25),(13,27)],'#bca16b')
 p.poly([(10,8),(13,7),(14,23),(11,24)],'#e3d09a')
 p.poly([(14,9),(17,8),(16,18),(15,16)],'#8c7449')
 p.line([(10,11),(11,14),(11,18)],'#f1dd9d')
 p.poly([(12,8),(10,5),(12,1),(14,-3),(17,3),(16,7),(14,9)],'#ba7a35')
 p.poly([(12,6),(12,3),(14,0),(15,5),(14,8)],'#f2d99b')
 p.line([(13,8),(13,10)],'#382b20')

def dividers(p):
 # Old measuring dividers, not polished orb/modern compass logo.
 p.poly([(17,1),(23,3),(23,8),(20,11),(32,41),(28,38),(18,17),(9,38),(5,42),(13,12),(14,7)],'#332921')
 p.poly([(17,3),(21,4),(20,9),(18,11),(8,36),(6,39),(15,9)],'#b39357')
 p.poly([(19,13),(22,13),(31,37),(29,37)],'#88683d')
 p.line([(17,5),(17,9),(9,31)],'#d2b276')
 p.line([(21,17),(27,32)],'#c5a46b')
 p.rect((17,5,19,7),'#ead39b')
 p.line([(11,26),(21,29),(25,27)],'#76613f')

def hand(p):
 # Open expressive palm, three bent fingers and raised index; warm anatomical planes.
 p.poly([(9,65),(12,45),(7,35),(5,22),(8,19),(11,23),(15,32),(16,24),(17,7),(20,4),(23,7),(23,25),(26,13),(29,10),(32,12),(30,29),(35,22),(38,22),(40,25),(34,39),(29,48),(29,65)],'#4a302a')
 p.poly([(12,62),(15,44),(10,34),(7,23),(9,22),(17,37),(19,25),(19,8),(21,7),(21,29),(24,32),(28,14),(30,13),(27,33),(29,37),(36,25),(38,25),(31,40),(26,46),(26,62)],'#b38060')
 p.poly([(15,43),(17,36),(22,31),(27,34),(29,39),(24,46),(23,60),(16,60)],'#cda27b')
 p.poly([(17,40),(20,35),(24,35),(25,38),(22,43),(18,46)],'#dfb992')
 p.line([(20,10),(20,23)],'#e3c39a')
 p.line([(28,16),(26,28)],'#ddba8d')
 p.line([(9,24),(12,31)],'#c79c78')
 p.line([(17,39),(19,41),(23,39)],'#8e5e48')
 p.line([(23,43),(22,49)],'#916047')
 p.poly([(6,57),(13,52),(25,54),(31,58),(35,77),(2,77)],'#292c3c')
 p.poly([(8,58),(12,55),(15,61),(13,76),(4,76)],'#555468')
 p.poly([(17,58),(23,57),(28,63),(30,76),(21,76)],'#414557')
 p.poly([(6,58),(13,53),(25,55),(31,59),(30,63),(23,59),(13,57),(7,62)],'#a88751')
 p.line([(8,59),(13,55),(24,57),(29,60)],'#d3b57e')

def folio(p):
 p.poly([(2,10),(14,5),(52,9),(57,17),(52,23),(49,58),(8,57),(5,50)],'#493728')
 p.poly([(5,11),(14,7),(51,11),(53,16),(49,20),(47,54),(9,54),(7,48)],'#bca06b')
 p.poly([(9,12),(20,10),(48,13),(45,48),(12,49)],'#d6bf89')
 p.poly([(13,12),(20,11),(23,47),(13,48)],'#e3cc99')
 p.poly([(42,15),(48,13),(44,49),(37,50)],'#b89a64')
 # Painted cosmology: an egg-shaped vault over a sea, rising stair and wandering stars.
 p.poly([(15,39),(14,26),(19,18),(27,15),(36,17),(41,24),(41,40)],'#665968')
 p.poly([(17,36),(16,26),(20,20),(27,17),(31,18),(24,23),(23,35)],'#878390')
 p.poly([(17,35),(25,32),(31,34),(39,30),(40,39),(16,40)],'#8a9d9b')
 p.poly([(17,38),(24,35),(29,37),(35,34),(40,36),(40,41),(17,41)],'#506d74')
 p.line([(19,38),(24,37),(28,38)],'#bfd0b9')
 p.poly([(25,43),(25,38),(28,38),(28,34),(31,34),(31,30),(34,30),(34,26),(37,26),(37,43)],'#d6c392')
 p.line([(26,38),(29,38),(29,34),(32,34),(32,30),(35,30)],'#f0dbad')
 for x,y in [(21,25),(29,21),(35,24)]:
  p.line([(x-2,y),(x+2,y)],'#ecce83');p.line([(x,y-2),(x,y+2)],'#ecce83')
 p.line([(12,15),(10,45),(13,47),(40,48)],'#9c7448')
 p.poly([(8,50),(47,52),(49,56),(11,56),(8,54)],'#8d7048')
 p.line([(11,51),(45,53)],'#e5cd93')

def cloth(p):
 p.poly([(0,15),(16,4),(34,10),(52,0),(74,10),(98,7),(124,17),(158,6),(159,34),(0,34)],'#292b36')
 p.poly([(1,18),(18,8),(29,12),(17,16),(9,30),(1,29)],'#555064')
 p.poly([(25,34),(39,14),(51,4),(60,8),(48,19),(45,34)],'#494657')
 p.poly([(59,34),(72,14),(86,11),(78,20),(79,34)],'#626071')
 p.poly([(92,34),(116,19),(128,20),(110,34)],'#44424d')
 p.line([(4,24),(16,14),(22,13)],'#777080')
 p.line([(42,23),(48,15),(52,12)],'#777080')
 p.line([(69,29),(74,20),(79,16)],'#8b8090')

def scene(p,which):
 # Nominal 160x68, no reference composition or raster is consumed.
 p.rect((2,2,157,65),'#342c2a')
 p.poly([(3,3),(86,3),(104,44),(3,48)],'#554133')
 p.poly([(6,4),(68,4),(47,42),(4,44)],'#6e5339')
 if which=='study':
  # Receding shelves, near candle, open text, dividers; asymmetric still life.
  p.poly([(99,3),(156,3),(156,48),(115,44)],'#29272a')
  for x,y,w in [(112,11,7),(122,7,9),(135,13,6),(144,8,8)]:
   p.poly([(x,y),(x+w,y-1),(x+w,40),(x,42)],'#594137');p.line([(x+2,y+3),(x+2,37)],'#866446')
  p.line([(108,43),(156,45)],'#b49358')
  p.group('folded-indigo-cloth',cloth,0,35)
  p.group('open-worn-book',book,38,17,1.35)
  p.group('wax-and-aged-candlestick',candle,12,6,1)
  p.group('measuring-dividers',dividers,103,18,1)
  p.poly([(87,58),(115,54),(130,57),(105,63)],'#876c46')
  p.line([(90,58),(111,56),(121,58)],'#cbb07d')
 elif which=='conjurer':
  # Theatre curtains frame a working hand and a levitating folded leaf, not a portrait clone.
  p.poly([(3,3),(37,3),(27,20),(18,27),(7,61),(3,64)],'#4b3037')
  p.poly([(7,3),(19,3),(16,21),(10,32),(7,53)],'#795055')
  p.poly([(26,3),(34,3),(22,26),(17,32)],'#95635d')
  p.poly([(120,3),(157,3),(157,64),(148,52),(138,21)],'#33313f')
  p.poly([(125,4),(137,4),(141,29),(152,53),(145,43),(132,27)],'#5b586b')
  p.group('expressive-conjuring-hand',hand,48,-2,.9)
  p.poly([(93,23),(108,16),(119,23),(109,31),(96,30)],'#8c755c')
  p.poly([(93,22),(106,16),(109,26),(97,29)],'#dec89b')
  p.poly([(106,16),(119,22),(109,26)],'#eee0b5')
  p.line([(96,24),(102,21),(106,23)],'#977f5c')
  p.line([(87,32),(92,35),(100,35),(106,33)],'#909795')
  p.line([(87,35),(95,39),(105,38)],'#666e79')
  p.group('stage-velvet',cloth,0,54,1,.45)
 else:
  p.group('deep-blue-folds',cloth,0,33,1,1)
  p.group('painted-cosmological-folio',folio,41,-5,1.25)
  p.poly([(12,47),(32,44),(37,49),(16,53)],'#9b7b50')
  p.poly([(15,46),(33,45),(34,48),(17,51)],'#d8bb84')
  p.line([(111,54),(137,26),(142,21)],'#bca478',2)
  p.poly([(137,28),(137,19),(144,11),(149,10),(148,17),(140,27)],'#aeb5ab')
  p.line([(139,24),(146,13)],'#e1d9bd')
  p.poly([(121,56),(123,49),(129,48),(134,54),(132,59),(122,60)],'#272c32')
  p.line([(125,50),(129,50),(132,54)],'#758486')
 # Weathered narrow frame, intentionally broken highlights, no immaculate bevel.
 p.line([(2,65),(2,2),(157,2)],'#ad8a51')
 p.line([(3,64),(156,64),(156,4)],'#705134')
 p.line([(5,4),(30,4)],'#d5b477');p.line([(40,4),(73,4)],'#b79c67');p.line([(102,4),(145,4)],'#c4a16a')
 p.line([(4,8),(4,27)],'#8a6a43')
 for x in (7,151):
  p.line([(x,7),(x+2,9),(x,11),(x-2,9),(x,7)],'#b8955f')

def bookmark(p,which,selected):
 # Different cloth drape and edge position states, not brightness alone.
 dx=0 if selected else 6;p.t=(dx,0,1,1)
 p.poly([(5,13),(62,13),(61,48),(14,53),(5,47)],'#17191c')
 p.poly([(3,5),(71,7),(60,25),(70,44),(4,46)],'#494456' if selected else '#292c38')
 p.poly([(6,8),(17,9),(15,41),(6,44)],'#7a6c79' if selected else '#484351')
 p.poly([(18,10),(24,10),(21,30),(24,41),(17,43)],'#373644')
 p.poly([(26,10),(55,10),(48,18),(28,22)],'#625b6a' if selected else '#393846')
 p.line([(5,7),(68,9),(58,25),(66,42),(5,44)],'#b59b64' if selected else '#7d704f')
 for x in range(9,55,5):p.line([(x,11),(x+1,11)],'#c8ad78' if selected else '#8f805f')
 # Hand-sized embroidered emblems rather than miniature pasted scene.
 if which=='study':
  p.poly([(27,29),(28,18),(38,21),(48,18),(49,29),(38,33)],'#bba371')
  p.line([(30,21),(35,23),(35,28)],'#655540');p.line([(39,23),(46,21)],'#655540');p.line([(38,22),(38,31)],'#6f5b42')
 elif which=='conjurer':
  p.poly([(34,35),(33,27),(29,22),(30,19),(35,23),(36,14),(39,13),(39,23),(43,17),(45,18),(42,29),(40,35)],'#c3aa79')
  p.line([(36,26),(39,28),(38,33)],'#78603f')
 else:
  p.poly([(27,34),(29,16),(48,18),(47,35)],'#b59a68')
  p.poly([(31,31),(32,23),(36,19),(40,20),(44,25),(43,32)],'#4b5062')
  p.line([(33,31),(36,29),(39,31),(43,29)],'#a7b3ad')
  p.line([(38,23),(38,27)],'#e3c88f');p.line([(36,25),(40,25)],'#e3c88f')
 p.t=(0,0,1,1)

def skill(p,which,small):
 w,h=p.im.size
 p.rect((2,2,w-3,h-3),'#4a392c');p.line([(3,h-4),(3,3),(w-4,3)],'#9c7c4c')
 # Native32 is a separate crop/arrangement, no downsampling of the large scene.
 if small:
  if which=='study':
   p.group('native-small-open-book',book,3,8,.47)
   p.group('native-small-candle',candle,5,3,.36)
  elif which=='conjurer':p.group('native-small-hand',hand,8,3,.33)
  else:p.group('native-small-painted-leaf',folio,6,3,.38)
 else:
  p.group('skill-cloth',cloth,3,50,.47,.9)
  if which=='study':
   p.group('skill-open-book',book,7,31,1.1)
   p.group('skill-candle',candle,8,9,1)
   p.group('skill-dividers',dividers,48,18,.8)
  elif which=='conjurer':
   p.group('skill-conjuring-hand',hand,16,4,1)
   p.poly([(52,28),(63,20),(72,25),(64,34)],'#d9c69a')
   p.line([(48,35),(55,39),(64,38)],'#9daba6')
  else:p.group('skill-painted-folio',folio,7,8,1.17)

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 records=[]
 for which,title in [('study','MAGICIANS STUDY'),('conjurer','CONJURER'),('manuscript','CELESTIAL MANUSCRIPT')]:
  out=ROOT/which;out.mkdir(exist_ok=True)
  items=[('header',160,96),('bookmark_selected',80,60),('bookmark_unselected',80,60),('skill_small',32,32),('skill_large',82,93)]
  for name,w,h in items:
   p=Paint(w,h)
   if name=='header':p.group('original-miniature',lambda p:scene(p,which),2,29,.97,.91)
   elif name.startswith('bookmark'):bookmark(p,which,name.endswith('_selected'))
   else:skill(p,which,w==32)
   # All exported art is inset by construction, not repaired by alpha erasure.
   alpha=p.im.getchannel('A');assert alpha.getbbox() and alpha.getextrema()==(0,255)
   assert not any(alpha.crop(b).getbbox() for b in [(0,0,w,1),(0,h-1,w,h),(0,0,1,h),(w-1,0,w,h)]),(which,name,'edge')
   if name=='header':assert not alpha.crop((0,0,w,28)).getbbox()
   p.save(out/name)
   records.append({'study':which,'asset':name,'size':[w,h],'png_sha256':sha(out/(name+'.png')),'svg_sha256':sha(out/(name+'.svg')),'status':'unapproved concept sample; skill rank not yet assigned'})
  sheet=Image.new('RGB',(520,225),'#756c5c');d=ImageDraw.Draw(sheet)
  d.text((8,8),title+' / V2 STUDY - NOT APPROVED',fill='#ffffff')
  for name,x,y in [('header',8,29),('bookmark_selected',183,51),('bookmark_unselected',273,51),('skill_small',183,146),('skill_large',425,113)]:
   im=Image.open(out/(name+'.png'));sheet.paste(im,(x,y),im)
  for i,state in enumerate(('selected','unselected')):
   im=Image.open(out/('bookmark_'+state+'.png')).resize((68,51),Image.Resampling.LANCZOS);sheet.paste(im,(252+i*78,144),im)
  for text,x,y in [('header 160x96 / clear top28',8,128),('native 80x60 states',183,34),('32px',183,130),('compact 68x51',252,128),('82x93',425,96),('Actual pixels; hypothesis, not an approved style',8,207)]:d.text((x,y),text,fill='white')
  sheet.save(out/'native-review.png');sheet.resize((1040,450),Image.Resampling.NEAREST).save(out/'nearest-2x-review.png')
 (ROOT/'manifest.json').write_text(json.dumps({'status':'three unapproved studies; not full family or import authorization','tool':'Pillow native-pixel polygons/brush lines; editable SVG equivalents','exporter_sha256':sha(Path(__file__)),'assets':records},indent=2)+'\n')
 print('15 new study assets: size/RGBA/clear rim/header margin PASS; original A/B untouched')
if __name__=='__main__':main()
