#!/usr/bin/env python3
"""Original NH vector medallions; deterministic SVG + antialiased RGBA PNG exports.
SPDX-License-Identifier: CC0-1.0
Requires Pillow only. No purchaser assets or concept images are read.
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageColor

HERE = Path(__file__).resolve().parent
OUT = HERE.parent.parent / 'Mods/new-horizons/Images'
SOURCE = HERE / 'svg'
SCALE = 4

# Authored geometric motifs, not traced from Heroes artwork or concept images.
MOTIFS = {
    'light': ('#f2d875', [('line', [(32,10),(32,54)]), ('line', [(10,32),(54,32)]), ('polygon', [(32,17),(42,32),(32,47),(22,32)]), ('circle', (32,32,5))]),
    'nature': ('#98d57c', [('line', [(23,54),(38,18)]), ('polygon', [(31,34),(13,30),(12,15),(29,20)]), ('polygon', [(34,29),(38,10),(53,12),(49,28)]), ('line', [(17,48),(45,48)])]),
    'sorcery': ('#8fcefa', [('circle', (32,32,22)), ('polygon', [(32,12),(46,28),(32,53),(18,28)]), ('line', [(18,28),(46,28)]), ('line', [(32,12),(32,53)])]),
    'havoc': ('#f29a55', [('polygon', [(38,7),(19,34),(31,34),(23,58),(49,25),(35,25)]), ('line', [(10,19),(17,25)]), ('line', [(46,46),(54,50)])]),
    'shadow': ('#bd9bda', [('polygon', [(15,16),(44,11),(51,45),(32,56),(13,43)]), ('line', [(21,25),(28,28)]), ('line', [(37,28),(44,25)]), ('line', [(26,42),(38,42)])]),
    'chaos': ('#e38bbd', [('line', [(12,14),(45,16),(48,47),(16,49),(18,27),(37,25),(35,37)]), ('polygon', [(8,10),(21,12),(11,22)]), ('circle', (48,47,5))]),
    'charge': ('#efaa66', [('line', [(10,18),(47,18)]), ('line', [(10,32),(47,32)]), ('line', [(10,46),(47,46)]), ('polygon', [(42,9),(56,18),(42,27)]), ('polygon', [(42,23),(56,32),(42,41)]), ('polygon', [(42,37),(56,46),(42,55)])]),
    'holdTheLine': ('#a1cce0', [('polygon', [(16,12),(48,12),(46,40),(32,55),(18,40)]), ('line', [(11,24),(53,24)]), ('line', [(12,34),(52,34)]), ('line', [(32,13),(32,47)])]),
    'advance': ('#b4d986', [('polygon', [(12,25),(32,10),(52,25),(45,32),(32,22),(19,32)]), ('polygon', [(12,43),(32,28),(52,43),(45,50),(32,40),(19,50)]), ('line', [(12,56),(52,56)])]),
    'aggressive': ('#ec8a73', [('polygon', [(12,12),(22,14),(51,50),(46,55),(17,24)]), ('polygon', [(52,12),(42,14),(13,50),(18,55),(47,24)]), ('line', [(9,44),(24,56)]), ('line', [(40,56),(55,44)])]),
    'defensive': ('#93bce5', [('polygon', [(12,13),(52,13),(49,40),(32,56),(15,40)]), ('polygon', [(22,22),(42,22),(40,36),(32,44),(24,36)]), ('line', [(8,54),(56,54)])]),
    'spells': ('#d0b4ed', [('polygon', [(8,16),(28,12),(32,18),(36,12),(56,16),(56,49),(36,46),(32,51),(28,46),(8,49)]), ('line', [(32,18),(32,51)]), ('line', [(14,25),(24,23)]), ('line', [(40,23),(50,25)]), ('line', [(14,35),(24,33)]), ('line', [(40,33),(50,35)])]),
    'cancel': ('#d8c6a0', [('line', [(20,20),(44,44)]), ('line', [(44,20),(20,44)])]),
}

# Display glyphs for the upcoming hero screen, not evidence of implemented rules.
# No text, portrait, purchaser art or concept-image pixels are incorporated.
HERO_MOTIFS = {
    'attack': ('#efaa66', [('polygon', [(32,8),(41,21),(35,40),(29,40),(23,21)]), ('line', [(19,41),(45,41)]), ('line', [(32,41),(32,55)])]),
    'defense': ('#a1cce0', [('polygon', [(13,12),(51,12),(47,41),(32,55),(17,41)]), ('line', [(23,20),(23,37),(32,45),(41,37),(41,20)])]),
    'power': ('#e7b0ed', [('polygon', [(32,8),(38,25),(55,32),(38,39),(32,56),(25,39),(9,32),(25,25)]), ('circle', (32,32,5))]),
    'knowledge': ('#8fcefa', [('polygon', [(12,10),(46,10),(52,17),(52,54),(12,54)]), ('line', [(20,21),(42,21)]), ('line', [(20,31),(42,31)]), ('line', [(20,41),(35,41)])]),
    'mana': ('#8fcefa', [('polygon', [(24,9),(40,9),(40,22),(50,40),(47,53),(17,53),(14,40),(24,22)]), ('line', [(22,35),(42,35)]), ('line', [(24,15),(40,15)])]),
    'leadership': ('#f2d875', [('line', [(32,8),(32,23)]), ('polygon', [(32,8),(48,8),(43,17),(32,17)]), ('circle', (17,33,5)), ('circle', (32,29,5)), ('circle', (47,33,5)), ('line', [(9,53),(12,43),(22,43),(25,53)]), ('line', [(24,53),(27,39),(37,39),(40,53)]), ('line', [(39,53),(42,43),(52,43),(55,53)])]),
    'movement': ('#b4d986', [('polygon', [(17,10),(32,10),(32,34),(49,42),(51,53),(13,53),(13,41),(17,35)]), ('line', [(21,20),(28,20)]), ('line', [(21,28),(28,28)]), ('line', [(39,12),(54,12),(48,7)]), ('line', [(54,12),(48,18)])]),
    'morale': ('#efaa66', [('line', [(17,55),(17,9)]), ('polygon', [(18,11),(48,11),(41,23),(48,35),(18,35)]), ('line', [(25,25),(32,18),(39,25)]), ('line', [(32,18),(32,30)])]),
    'luck': ('#98d57c', [('polygon', [(15,10),(24,10),(21,31),(24,42),(32,47),(40,42),(43,31),(40,10),(49,10),(53,31),(48,48),(32,57),(16,48),(11,31)]), ('line', [(17,20),(23,20)]), ('line', [(41,20),(47,20)])]),
    'siege': ('#d8c6a0', [('circle', (19,49,6)), ('circle', (45,49,6)), ('line', [(12,42),(51,42)]), ('line', [(23,41),(32,21),(42,41)]), ('line', [(20,38),(44,10)]), ('polygon', [(40,9),(53,9),(49,18)]), ('line', [(13,31),(20,38)])]),
    'mastery': ('#e7b0ed', [('line', [(32,55),(32,34),(14,24),(14,15)]), ('line', [(32,34),(32,14)]), ('line', [(32,34),(50,24),(50,15)]), ('circle', (14,11,5)), ('circle', (32,10,5)), ('circle', (50,11,5))]),
    'core': ('#b4d986', [('polygon', [(11,22),(32,10),(53,22),(53,43),(32,55),(11,43)]), ('line', [(19,34),(32,25),(45,34)]), ('circle', (32,43,3))]),
    'elite': ('#8fcefa', [('polygon', [(11,22),(32,10),(53,22),(53,43),(32,55),(11,43)]), ('line', [(19,31),(32,22),(45,31)]), ('line', [(19,43),(32,34),(45,43)])]),
    'champion': ('#f2d875', [('polygon', [(10,18),(22,29),(32,10),(42,29),(54,18),(48,47),(16,47)]), ('line', [(16,54),(48,54)]), ('circle', (32,37,4))]),
    'growth': ('#b4d986', [('line', [(12,53),(12,38)]), ('line', [(26,53),(26,30)]), ('line', [(40,53),(40,22)]), ('line', [(54,53),(54,14)]), ('line', [(10,28),(50,8),(40,8)]), ('line', [(50,8),(49,18)])]),
}

class Art:
    def __init__(self, width, height):
        self.w, self.h = width, height
        self.image = Image.new('RGBA', (width*SCALE,height*SCALE))
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">', '<!-- Original New Horizons provisional vector artwork. CC0-1.0. -->']
    def polygon(self, points, fill, stroke='#c3a363', width=1):
        p=[(round(x*SCALE),round(y*SCALE)) for x,y in points]
        self.draw.polygon(p,fill=fill)
        if stroke:self.draw.line(p+[p[0]],fill=stroke,width=max(1,round(width*SCALE)),joint='curve')
        pts=' '.join(f'{x:g},{y:g}' for x,y in points)
        self.svg.append(f'<polygon points="{pts}" fill="{fill or "none"}" stroke="{stroke or "none"}" stroke-width="{width:g}" stroke-linejoin="round"/>')
    def line(self, points, color, width=3):
        self.draw.line([(round(x*SCALE),round(y*SCALE)) for x,y in points],fill=color,width=round(width*SCALE),joint='curve')
        pts=' '.join(f'{x:g},{y:g}' for x,y in points)
        self.svg.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width:g}" stroke-linejoin="round"/>')
    def circle(self,x,y,r,fill,stroke,width=1):
        self.draw.ellipse(tuple(round(v*SCALE) for v in (x-r,y-r,x+r,y+r)),fill=fill,outline=stroke,width=max(1,round(width*SCALE)))
        self.svg.append(f'<circle cx="{x:g}" cy="{y:g}" r="{r:g}" fill="{fill or "none"}" stroke="{stroke or "none"}" stroke-width="{width:g}"/>')
    def motif(self,name,x=0,y=0,size=64,disabled=False):
        color,shapes=(MOTIFS[name] if name in MOTIFS else HERO_MOTIFS[name]);color='#85827b' if disabled else color
        for kind,shape in shapes:
            if kind=='circle':
                cx,cy,r=shape;self.circle(x+cx*size/64,y+cy*size/64,r*size/64,None,color,2*size/64)
            else:
                pts=[(x+px*size/64,y+py*size/64) for px,py in shape]
                if kind=='line':self.line(pts,color,3*size/64)
                else:self.polygon(pts,'#282527' if not disabled else '#333332',color,2*size/64)
    def save(self,name):
        SOURCE.mkdir(parents=True,exist_ok=True);OUT.mkdir(parents=True,exist_ok=True)
        (SOURCE/(name+'.svg')).write_text('\n'.join(self.svg+['</svg>'])+'\n')
        raster = self.image.resize((self.w,self.h),Image.Resampling.LANCZOS)
        if name.endswith('_header'):
            # Preserve the spellbook's exact reserved top padding: antialiasing
            # must not bleed the artwork into rows 0..27.
            alpha = raster.getchannel('A')
            alpha.paste(0, (0, 0, self.w, 28))
            raster.putalpha(alpha)
        raster.save(OUT/(name+'.png'))

def animation(name,frames):
    (OUT/(name+'.json')).write_text(json.dumps({'images':[{'group':0,'frame':i,'file':f} for i,f in enumerate(frames)]},indent=2)+'\n')

for name in MOTIFS:
    art=Art(64,64);art.circle(32,32,30,'#302a25','#b49a62',2);art.motif(name,4,4,56);art.save('NH_'+name+'_icon')
    frames=[]
    for state in ('normal','pressed','disabled','highlighted'):
        art=Art(64,64);blocked=state=='disabled';rim='#726d60' if blocked else '#f4db90' if state=='highlighted' else '#b49a62'
        art.polygon([(2,2),(61,2),(61,61),(2,61)],'#201e20' if state=='pressed' else '#39312b',rim,2)
        shift=2 if state=='pressed' else 0;art.motif(name,4+shift,4+shift,54,blocked)
        output=f'NH_{name}_{state}';art.save(output);frames.append(output+'.png')
    animation('NH_'+name+'_button',frames)
    if name in ('light','nature','sorcery','havoc','shadow','chaos'):
        art=Art(160,96);art.polygon([(1,29),(158,29),(158,94),(1,94)],'#39312b','#b49a62',2)
        art.line([(8,62),(40,62)],MOTIFS[name][0],1);art.line([(120,62),(152,62)],MOTIFS[name][0],1)
        art.motif(name,50,32,60);art.save('NH_'+name+'_header')
        frames=[]
        for state in ('selected','unselected'):
            art=Art(80,60);art.polygon([(3,3),(77,3),(65,30),(77,56),(3,56)],'#51432e' if state=='selected' else '#292729','#f1d48b' if state=='selected' else '#8c7956',2)
            art.motif(name,10,5,50);output=f'NH_{name}_bookmark_{state}';art.save(output);frames.append(output+'.png')
        animation('NH_'+name+'_bookmark',frames)

entry_frames=[]
for state in ('normal','pressed','disabled','highlighted'):
    art=Art(48,36);blocked=state=='disabled'
    art.polygon([(1,1),(46,1),(46,34),(1,34)],'#211e20' if state=='pressed' else '#39312b','#f4db90' if state=='highlighted' else '#827557' if blocked else '#b49a62',1)
    shift=1 if state=='pressed' else 0
    art.motif('spells',14+shift,2+shift,32,blocked)
    art.line([(7+shift,27),(7+shift,8),(12+shift,13)],'#85827b' if blocked else '#efaa66',2)
    name='NH_hero_actions_entry_'+state;art.save(name);entry_frames.append(name+'.png')
animation('NH_hero_actions_entry',entry_frames)

art=Art(640,520)
art.polygon([(0,0),(639,0),(639,519),(0,519)],'#272329','#b99a64',3)
art.polygon([(9,9),(630,9),(630,510),(9,510)],'#342e2b','#756040',1)
for y in (94,191,334):art.line([(23,y),(617,y)],'#8d744f',1)
art.save('NH_hero_actions_back')
for name in HERO_MOTIFS:
    for size in (32, 64):
        art=Art(size,size)
        art.circle(size/2,size/2,size/2-2,'#302a25','#b49a62',1 if size == 32 else 2)
        art.motif(name, size/16, size/16, size*7/8)
        art.save(f'NH_hero_{name}_{size}')

print('Exported original SVG + RGBA PNG; provisional flat vector style, not final illustration.')
