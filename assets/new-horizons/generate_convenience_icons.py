#!/usr/bin/env python3
"""Independent original NH convenience artwork. SPDX-License-Identifier: CC0-1.0.
Pillow only; no input artwork, fonts, Extras files or held mastery generator.
Run separately from generate_icons.py. --output-root permits isolated reproduction.
"""
from pathlib import Path
import argparse
import json
from PIL import Image, ImageDraw, ImageColor

class Paint:
    def __init__(self, size, disabled=False):
        self.size, self.disabled, self.offset = size, disabled, 0
        self.scale = size * 4 / 64
        self.image = Image.new('RGBA', (size * 4, size * 4))
        self.draw = ImageDraw.Draw(self.image)
        self.svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 64 64">',
                    '<!-- Original New Horizons layered vector artwork; CC0-1.0. No external artwork or fonts. -->']
    def color(self, color):
        if color is None:
            return None
        if not self.disabled:
            return color
        r, g, b = ImageColor.getrgb(color)
        v = round((r * 0.30 + g * 0.59 + b * 0.11) * 0.65 + 20)
        return '#%02x%02x%02x' % (v, v, v)
    def points(self, points):
        return [(x + self.offset, y + self.offset) for x, y in points]
    def poly(self, points, fill, edge=None, width=1):
        points = self.points(points)
        fill, edge = self.color(fill), self.color(edge)
        pixels = [(round(x * self.scale), round(y * self.scale)) for x, y in points]
        self.draw.polygon(pixels, fill=fill)
        if edge:
            self.draw.line(pixels + pixels[:1], fill=edge, width=max(1, round(width * self.scale)), joint='curve')
        coords = ' '.join(f'{x:g},{y:g}' for x, y in points)
        self.svg.append(f'<polygon points="{coords}" fill="{fill or "none"}" stroke="{edge or "none"}" stroke-width="{width:g}" stroke-linejoin="round"/>')
    def line(self, points, color, width=2):
        points = self.points(points)
        color = self.color(color)
        self.draw.line([(round(x * self.scale), round(y * self.scale)) for x, y in points], fill=color, width=max(1, round(width * self.scale)), joint='curve')
        coords = ' '.join(f'{x:g},{y:g}' for x, y in points)
        self.svg.append(f'<polyline points="{coords}" fill="none" stroke="{color}" stroke-width="{width:g}" stroke-linejoin="round"/>')
    def circle(self, x, y, radius, fill, edge=None, width=1):
        x, y = x + self.offset, y + self.offset
        fill, edge = self.color(fill), self.color(edge)
        box = tuple(round(v * self.scale) for v in (x-radius, y-radius, x+radius, y+radius))
        self.draw.ellipse(box, fill=fill, outline=edge, width=max(1, round(width * self.scale)))
        self.svg.append(f'<circle cx="{x:g}" cy="{y:g}" r="{radius:g}" fill="{fill or "none"}" stroke="{edge or "none"}" stroke-width="{width:g}"/>')
    def save(self, root, name):
        svg = root / 'assets/new-horizons/svg' / (name + '.svg')
        png = root / 'Mods/new-horizons/Images' / (name + '.png')
        svg.parent.mkdir(parents=True, exist_ok=True)
        png.parent.mkdir(parents=True, exist_ok=True)
        svg.write_text('\n'.join(self.svg + ['</svg>']) + '\n', encoding='utf-8')
        self.image.resize((self.size, self.size), Image.Resampling.LANCZOS).save(png)

BONE, LIGHT, SHADOW, DARK = '#d9c69a', '#fff0c6', '#88734f', '#232c32'

def undead(p):
    p.poly([(14,21),(18,11),(29,6),(42,9),(50,20),(48,35),(42,42),(41,54),(24,55),(21,43),(14,36)], SHADOW, DARK, 2)
    p.poly([(16,21),(20,13),(29,8),(40,11),(47,21),(44,37),(37,43),(27,43),(19,37)], BONE)
    p.poly([(20,15),(29,9),(40,12),(44,19),(33,16),(24,20)], LIGHT)
    p.poly([(37,21),(46,22),(44,36),(38,41),(35,36)], '#ae9568')
    p.poly([(18,26),(24,23),(29,27),(27,34),(20,34)], DARK)
    p.poly([(35,27),(42,23),(46,27),(43,34),(36,34)], DARK)
    p.line([(18,25),(25,22),(29,25)], LIGHT, 2)
    p.line([(35,25),(42,21),(46,24)], LIGHT, 2)
    p.poly([(32,32),(28,40),(35,40)], '#3b3b33')
    p.poly([(24,43),(39,43),(39,51),(26,52)], BONE, DARK)
    for x in (27,31,35):
        p.line([(x,44),(x,50)], SHADOW, 1)
    p.line([(25,53),(39,52)], LIGHT, 1)

def flying(p):
    for mirror in (False, True):
        pts = [(31,41),(23,19),(5,9),(8,27),(14,30),(12,36),(20,38),(20,44),(27,48)]
        if mirror:
            pts = [(64-x,y) for x,y in pts]
        p.poly(pts, '#a0b8bf', DARK, 2)
        for x,y in ((9,16),(12,25),(18,32),(23,39)):
            line = [(x,y),(28,43)]
            if mirror:
                line = [(64-a,b) for a,b in line]
            p.line(line, '#edf0d8', 3)
    p.poly([(32,25),(37,40),(32,53),(27,40)], '#cfad67', DARK, 2)

def shooter(p):
    p.line([(12,38),(17,24),(31,19),(46,24),(52,38)], DARK, 8)
    p.line([(12,37),(18,25),(31,21),(45,25),(52,37)], '#ad7544', 5)
    p.line([(13,34),(20,24),(31,20),(44,24),(50,34)], '#e0b575', 2)
    p.line([(12,38),(31,44),(52,38)], BONE, 1)
    p.line([(31,55),(31,16)], '#5c4936', 6)
    p.line([(30,54),(30,17)], '#e5d8ab', 2)
    p.poly([(31,7),(23,21),(31,18),(39,21)], '#c4d8dc', DARK, 1)
    p.poly([(31,7),(31,18),(39,21)], '#738c98')

def siege(p):
    p.poly([(11,40),(18,34),(49,34),(55,43),(52,48),(12,48)], '#75513b', DARK, 2)
    p.line([(13,41),(51,41)], '#cea572', 3)
    for x in (18,47):
        p.circle(x,49,8,'#6a5037',DARK,2)
        p.circle(x,49,5,'#b18b4c',LIGHT,1)
        p.line([(x,44),(x,54)], DARK,1)
        p.line([(x-5,49),(x+5,49)], DARK,1)
    p.poly([(22,35),(30,20),(39,35)], '#82949a', DARK, 2)
    p.line([(22,34),(46,10)], DARK,7)
    p.line([(22,32),(46,10)], '#d2b87d',4)
    p.poly([(41,8),(55,8),(53,19),(44,22)], '#899ba0',DARK,2)
    p.line([(44,10),(53,10)], '#e2ece7',2)

def shield(p, color='#587c9b'):
    p.poly([(13,10),(32,6),(52,10),(49,38),(42,48),(32,57),(21,49),(15,38)], '#283844', DARK, 2)
    p.poly([(17,14),(32,10),(47,14),(44,37),(32,51),(21,38)], color, '#c5b078', 2)
    p.line([(19,17),(32,13),(43,16)], '#ece1b7', 2)
    p.poly([(32,15),(43,18),(41,35),(32,45)], '#324853')

def no_retaliation(p):
    shield(p)
    p.line([(45,31),(37,23),(25,23),(19,31)], '#d9ddc8', 4)
    p.poly([(16,25),(15,37),(26,32)], '#d9ddc8', DARK)
    p.line([(13,52),(52,10)], DARK,7)
    p.line([(13,51),(51,10)], '#ce7856',4)
    p.line([(14,50),(49,11)], '#f4bd86',1)

def unlimited(p):
    pts=[(8,33),(10,24),(18,19),(25,21),(40,41),(47,44),(55,40),(58,31),(54,23),(47,20),(40,23),(25,42),(17,44),(10,40),(8,33)]
    p.line(pts,DARK,9)
    p.line(pts,'#b38b4c',6)
    p.line(pts,'#f0d699',2)
    p.poly([(57,24),(48,29),(57,35)], '#c8d6d3', DARK)
    p.poly([(8,41),(17,35),(8,30)], '#c8d6d3', DARK)

def breath(p):
    for x in (24,46):
        p.poly([(x-12,23),(x,16),(x+12,23),(x+12,38),(x,45),(x-12,38)], '#41454a', '#94815d', 1)
    p.poly([(5,34),(21,28),(26,18),(35,24),(50,16),(45,29),(60,31),(46,37),(53,47),(33,42),(25,47),(18,38)], '#9f4c38', DARK,2)
    p.poly([(9,34),(27,29),(31,24),(36,30),(50,25),(43,34),(49,39),(32,36),(25,41),(22,36)], '#e29948')
    p.poly([(10,34),(29,32),(38,30),(34,35),(25,36)], '#ffe0a0')

def adjacent(p):
    for dx,dy in ((0,-1),(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1)):
        p.line([(32+dx*10,32+dy*10),(32+dx*24,32+dy*24)],DARK,7)
        p.line([(32+dx*10,32+dy*10),(32+dx*24,32+dy*24)],'#c9d6d7',4)
        p.line([(32+dx*11-1,32+dy*11-1),(32+dx*23-1,32+dy*23-1)],LIGHT,1)
    p.circle(32,32,12,'#695338',DARK,2)
    p.circle(32,32,8,'#bc9b5d','#ecd39b',2)
    p.poly([(32,25),(37,32),(32,39),(27,32)],'#e0d1aa')

def resistance(p):
    shield(p,'#866ca0')
    p.poly([(50,5),(35,22),(43,22),(39,30),(53,17),(46,17),(59,5)],'#cca1ed',DARK,2)
    p.line([(35,29),(44,42),(56,44)],'#e7c9ed',3)
    p.line([(48,36),(55,37)],'#d2b1eb',2)
    p.line([(39,45),(42,53)],'#d2b1eb',2)

def regeneration(p):
    p.line([(20,53),(32,29),(43,15)],DARK,5)
    p.poly([(28,35),(14,32),(9,19),(25,22),(31,29)],'#618c54',DARK,2)
    p.poly([(33,27),(34,13),(49,8),(49,22),(41,30)],'#9dbb67',DARK,2)
    p.line([(14,24),(27,31)],'#c1d590',2)
    p.line([(39,24),(45,13)],'#e1e2a1',2)
    p.line([(14,46),(21,56),(39,57),(52,46),(53,34)],'#bf9b56',5)
    p.line([(15,46),(22,54),(39,55),(50,45)],'#f0d799',2)
    p.poly([(45,36),(54,28),(59,41)],'#d6bf81',DARK,1)

STATUS = {'undead':undead, 'flying':flying, 'shooter':shooter, 'siege':siege,
          'noRetaliation':no_retaliation, 'unlimitedRetaliations':unlimited,
          'breath':breath, 'adjacent':adjacent, 'resistance':resistance,
          'regeneration':regeneration}

def button(p, load, state):
    rim = '#ffe4a2' if state == 'highlighted' else '#b39b6c'
    p.poly([(3,1),(60,1),(63,4),(63,60),(60,63),(3,63),(1,60),(1,4)],'#342e27',rim,2)
    p.poly([(4,4),(59,4),(56,8),(8,8),(8,56),(4,60)],'#d5bd8b')
    p.poly([(59,4),(60,60),(4,60),(8,56),(56,56),(56,8)],'#161e23')
    p.poly([(9,9),(55,9),(55,55),(9,55)],'#493b2d')
    for y in (14,21,28,35,42,49):
        p.line([(10,y),(54,y)],'#554634',1)
    p.offset = 2 if state == 'pressed' else 0
    p.poly([(12,25),(35,20),(44,25),(44,51),(19,56),(12,50)],'#211e1c',DARK,2)
    p.poly([(16,28),(39,24),(40,48),(18,52)],'#dfcf9c','#805e3c',1)
    p.line([(18,44),(38,40)],'#a08859',1)
    p.line([(18,48),(38,44)],'#a08859',1)
    p.poly([(12,25),(34,20),(39,24),(17,29)],'#b57642',LIGHT,1)
    if load:
        arrow=[(39,8),(26,24),(34,24),(34,39),(44,39),(44,24),(52,24)]
    else:
        arrow=[(34,9),(44,9),(44,25),(52,25),(39,41),(26,25),(34,25)]
    p.poly(arrow,'#8fc1c6',DARK,2)
    p.line([(37,13),(37,27 if not load else 35)],'#e3f1d9',2)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output-root',type=Path,default=Path(__file__).resolve().parents[2])
    root=parser.parse_args().output_root
    for name, draw in STATUS.items():
        p=Paint(50);draw(p);p.save(root,f'NH_status_{name}_50')
    for name,load in (('qsave',False),('qload',True)):
        frames=[]
        for state in ('normal','pressed','disabled','highlighted'):
            p=Paint(24,state=='disabled');button(p,load,state)
            stem=f'NH_{name}_24_{state}';p.save(root,stem);frames.append(stem+'.png')
        path=root/'Mods/new-horizons/Images'/f'NH_{name}_24.json'
        path.write_text(json.dumps({'images':[{'group':0,'frame':i,'file':f} for i,f in enumerate(frames)]},indent=2)+'\n',encoding='utf-8')
    print('Exported 18 original SVG/PNG pairs and two button animations; no gameplay or input acceptance implied.')

if __name__ == '__main__':
    main()
