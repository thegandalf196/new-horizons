#!/usr/bin/env python3
"""CC0 V4 original illustrative Sorcery material/light refinement in Blender.
Spatial pigment fields/selective accents; NOT human painting or approved art. Run headless only in a Build-approved slot:
blender --background --factory-startup --python scene.py -- --output DIR
No external textures, fonts, proprietary inputs, downloads or game interaction.
"""
import argparse, math, sys
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
args=argparse.ArgumentParser();args.add_argument('--output',type=Path,default=ROOT/'output');args.add_argument('--samples',type=int,default=64)
opt=args.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
out=opt.output.resolve()
allowed=(ROOT,Path.cwd()/'build/new-horizons-linux/research/sorcery-art')
assert any(out.is_relative_to(p.resolve()) for p in allowed),'isolated output required'
out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)

def mat(name,color,rough=.6,metal=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*color,1);b.inputs['Roughness'].default_value=rough;b.inputs['Metallic'].default_value=metal
 return m
leather=mat('Illustrated ultramarine leather / subdued specular',(.018,.046,.21),.88)
clothmat=mat('Illustrated carmine cloth / broad pigment groups',(.24,.012,.035),.94)
ivory=mat('Ochre paper edges',(.48,.29,.115),.9)
page=mat('Illustrated parchment / warm pigment fields',(.83,.62,.29),.9)
pageedge=mat('Oxidised leaf edges',(.37,.23,.105),.85)
ink=mat('Faded sepia authored notation',(.105,.052,.022),.91)
silver=mat('Illustrated silver / slate body with selective ivory accents',(.34,.42,.51),.57,.48)
silverdark=mat('Silver recess patina',(.065,.073,.11),.76,.3)
gold=mat('Dull ochre book tooling',(.32,.19,.064),.53,.72)
thread=mat('Worn blue linen binding thread',(.14,.22,.29),.84)
wood=mat('Warm dark walnut table',(.065,.031,.017),.72)
ribbon=mat('Faded russet bookmark ribbon',(.34,.068,.025),.71)
# Broad deliberate spatial pigment groupings, not noise or a post-render filter.
# Generated coordinates keep warm/cool transitions attached to the physical form.
# These are procedural shader fields, NOT manually painted brushwork.
def pigment(m,axis,stops):
 n=m.node_tree.nodes;l=m.node_tree.links;b=n.get('Principled BSDF')
 b.inputs['Specular IOR Level'].default_value=.12
 coord=n.new('ShaderNodeTexCoord');sep=n.new('ShaderNodeSeparateXYZ');ramp=n.new('ShaderNodeValToRGB');ramp.name='Authored broad pigment grouping / '+axis
 ramp.color_ramp.interpolation='EASE'
 while len(ramp.color_ramp.elements)>1:ramp.color_ramp.elements.remove(ramp.color_ramp.elements[-1])
 for i,(position,color) in enumerate(stops):
  e=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(position)
  e.position=position;e.color=(*color,1)
 l.new(coord.outputs['Generated'],sep.inputs[0]);l.new(sep.outputs[axis],ramp.inputs[0]);l.new(ramp.outputs['Color'],b.inputs['Base Color'])
pigment(page,'X',[(0,(.30,.16,.057)),(.14,(.72,.43,.15)),(.36,(.91,.72,.37)),(.58,(.79,.54,.22)),(.82,(.88,.68,.34)),(1,(.55,.31,.11))])
pigment(leather,'Y',[(0,(.028,.055,.19)),(.24,(.022,.04,.16)),(.52,(.06,.09,.32)),(.76,(.018,.027,.12)),(1,(.045,.07,.24))])
pigment(clothmat,'X',[(0,(.12,.008,.034)),(.20,(.31,.019,.046)),(.44,(.17,.01,.03)),(.67,(.25,.014,.05)),(1,(.055,.006,.022))])
accent=mat('Selective painted-illustration silver highlight / explicit geometry',(.78,.76,.53),.65,.15)

bookroot=bpy.data.objects.new('BOOK / coherent original authored assembly',None);bpy.context.collection.objects.link(bookroot);bookroot.rotation_euler[2]=math.radians(-13)

def attach(o,m,parent=None):
 o.data.materials.append(m)
 if parent:o.parent=parent
 return o

def mesh(name,verts,faces,m,parent=None,smooth=False):
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);attach(o,m,parent)
 if smooth:
  for f in me.polygons:f.use_smooth=True
 return o

def cube(name,loc,scale,m,bevel=0,parent=None):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);attach(o,m,parent)
 if bevel:
  mod=o.modifiers.new('Real rounded material thickness','BEVEL');mod.width=bevel;mod.segments=3
  o.modifiers.new('Weighted surface normals','WEIGHTED_NORMAL')
 return o

def line(name,pts,m,r=.012,parent=None,closed=False):
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=16;cu.bevel_depth=r;cu.bevel_resolution=3;cu.use_fill_caps=True
 sp=cu.splines.new('POLY');sp.points.add(len(pts)-1)
 for q,co in zip(sp.points,pts):q.co=(*co,1)
 sp.use_cyclic_u=closed;o=bpy.data.objects.new(name,cu);bpy.context.collection.objects.link(o);attach(o,m,parent);return o

cube('Single walnut tabletop',(0,0,-.13),(200,200,.2),wood,.015)
# Broad explicitly shaped folded fabric; no imported cloth simulation/cache.
verts=[];faces=[];nx,ny=100,75
for j in range(ny):
 v=j/(ny-1);y=-1.65+3.15*v
 for i in range(nx):
  u=i/(nx-1);x=-2.25+4.5*u
  edge=math.sin(math.pi*u)**.6
  z=.015+(.075+.08*v)*math.sin(5*x+1.3*y)**2+.07*math.sin(3*y-x)**2
  z+=.20*math.exp(-((x-1.55)/.32)**2)*(1-v)+.11*math.exp(-((x+1.8)/.22)**2)*v
  verts.append((x+.1*math.sin(4*v),y+.08*math.sin(6*u),z))
for j in range(ny-1):
 for i in range(nx-1):
  a=j*nx+i;faces.append((a,a+1,a+nx+1,a+nx))
cloth=mesh('Heavy oxblood cloth / large authored folds',verts,faces,clothmat,smooth=True)
sol=cloth.modifiers.new('Visible cloth hem thickness','SOLIDIFY');sol.thickness=.018
# Open blue-leather boards and substantially thick page blocks.
for side in (-1,1):
 cover=cube(('Left' if side<0 else 'Right')+' thick blue-leather board',(side*.86,0,.265),(1.73,2.15,.11),leather,.055,bookroot)
 cover.rotation_euler[1]=side*math.radians(-2)
 # Material seams/tooling follow object edges, not a uniform icon outline.
 for yy in (-.98,.98):
  line('Hand-tooled cover inset seam',[(side*.12,yy,.328),(side*1.56,yy,.378)],gold,.009,bookroot)
 for k in range(12):
  x=side*(.22+k*.112)
  line('Discrete worn binding stitch',[(x,-1.023,.328),(x+side*.025,-1.023,.329)],thread,.007,bookroot)
 # Page surfaces are curved actual geometry, not a flat textured rectangle.
 for leaf in range(19):
  top=leaf==18;vv=[];ff=[];nu,nv=32,8
  for j in range(nv):
   v=j/(nv-1);y=-.985+1.97*v
   for i in range(nu):
    u=i/(nu-1);x=side*(.035+1.535*u)
    z=.337+leaf*.013+.19*math.sin(math.pi*u*.72)+.028*u
    z+=.022*math.sin(math.pi*v)*u+.025*u**6*math.cos(v*math.pi)
    vv.append((x,y,z))
  for j in range(nv-1):
   for i in range(nu-1):
    a=j*nu+i;face=(a,a+1,a+nu+1,a+nu);ff.append(face if side>0 else tuple(reversed(face)))
  o=mesh('Ivory curved leaf %s %02d'%(side,leaf),vv,ff,page if top else ivory,bookroot,True)
  if top:
   mod=o.modifiers.new('Paper thickness at lifted edge','SOLIDIFY');mod.thickness=.009
  # Warm irregular edge lines are separate real stacked sheets, not random noise.
  if leaf%3==0:
   pts=[vv[i] for i in range(nu)];line('Individual leaf front edge',pts,pageedge,.0025,bookroot)

def surf(side,u,y,offset=.004):
 v=(y+.985)/1.97
 return (side*(.035+1.535*u),y,.337+18*.013+.19*math.sin(math.pi*u*.72)+.028*u+.022*math.sin(math.pi*v)*u+.025*u**6*math.cos(v*math.pi)+offset)
# Faded authored abbreviated marks: no fonts or copied manuscript text.
# Sparse irregular paragraph shapes, hand-authored calligraphic curve vocabulary;
# no font/borrowed writing and no repeated uniform dash grid.
phrases=[(.12,.59),(.16,.69),(.12,.52),(.18,.73),(.13,.62),(.18,.48),(.14,.66)]
for side in (-1,1):
 for row,(start,end) in enumerate(phrases):
  y=-.65+row*.205+(0 if side<0 else .036)
  pts=[]
  for k in range(40):
   u=start+(end-start)*k/39
   bend=.009*math.sin(k*1.4+row)+(.026 if k in (4,13,25) else 0)
   pts.append(surf(side,u,y+bend,.008))
  line('Authored flowing sepia phrase',pts,ink,.004,bookroot)
 # A substantial red ornamental initial, newly drawn, not an existing letter font.
 line('Original red manuscript initial',[surf(side,.15,.74,.009),surf(side,.15,.88,.009),surf(side,.23,.85,.009),surf(side,.19,.8,.009),surf(side,.25,.74,.009)],ribbon,.009,bookroot)
# Spine valley and substantial curved binding head.
line('Deep blue spine gutter',[(0,-1.02,.37),(0,-.6,.52),(0,.6,.52),(0,1.02,.37)],leather,.047,bookroot)
# Satin bookmark drapes through foreground gutter with real thickness.
mesh('Russet cloth bookmark',[(.045,-.55,.655),(.12,-.55,.657),(.12,-1.02,.61),(.10,-1.21,.38),(.06,-1.28,.20),(-.015,-1.25,.20),(.025,-1.18,.39),(.04,-1.0,.60)],[(0,1,2,3,4,5,6,7)],ribbon,bookroot)
# Warding key: physical old silver, recognisable bow/shaft/bit instead of gem badge.
keyroot=bpy.data.objects.new('KEY / original warding key',None);bpy.context.collection.objects.link(keyroot);keyroot.parent=bookroot;keyroot.location=(.10,-.12,.84);keyroot.scale=(1.23,1.30,1);keyroot.rotation_euler=(math.radians(7),math.radians(-5),math.radians(-30))
# Irregular quatrefoil bow silhouette; restrained heroic proportions.
pts=[]
for k in range(128):
 a=2*math.pi*k/128;r=.245+.043*math.cos(4*a);pts.append((-.69+r*math.cos(a),r*math.sin(a),0))
line('Forged quatrefoil bow',pts,silver,.049,keyroot,True)
line('Subtle inset patina at bow',[(x,y,-.035) for x,y,z in pts],silverdark,.013,keyroot,True)
line('Heavy silver stem',[(-.41,0,0),(.56,0,0)],silver,.064,keyroot)
# Restricted light-side accents emphasize meaningful bow/stem, not universal rim.
line('Selected upper bow accent',[(x,y,z+.041) for x,y,z in pts[12:40]],accent,.012,keyroot)
line('Interrupted stem accent',[(-.32,-.017,.059),(-.08,-.017,.059)],accent,.010,keyroot)
line('Short bit light accent',[(.35,.07,.052),(.50,.07,.052)],accent,.009,keyroot)
for x in (-.36,-.26,.30):
 line('Turned stem collar',[(x,.064*math.cos(a*math.pi/16),.064*math.sin(a*math.pi/16)) for a in range(32)],silver,.014,keyroot,True)
# Ward teeth are distinguishable solid pieces with tiny worn corner bevels.
cube('Rectangular key-bit bridge',(.47,.14,0),(.28,.25,.095),silver,.018,keyroot)
cube('First cut key tooth',(.37,.31,0),(.065,.15,.095),silver,.012,keyroot)
cube('Second cut key tooth',(.56,.28,0),(.07,.09,.095),silver,.012,keyroot)
line('Dark chased recess on bit',[(.38,.16,.050),(.54,.16,.050),(.54,.24,.050)],silverdark,.007,keyroot)
# Small leather corner repairs: local wear where handling occurs, not uniform noise.
for side in (-1,1):
 for yy in (-1,1):
  line('Handled blue corner scuff',[(side*1.57,yy*.91,.37),(side*1.61,yy*.98,.36),(side*1.51,yy*1.02,.36)],thread,.008,bookroot)

world=bpy.data.worlds.new('Warm near-dark studio environment');bpy.context.scene.world=world;world.use_nodes=True
world.node_tree.nodes['Background'].inputs[0].default_value=(.11,.077,.053,1);world.node_tree.nodes['Background'].inputs[1].default_value=.055

def area(name,loc,power,color,size,target):
 data=bpy.data.lights.new(name,'AREA');data.energy=power;data.color=color;data.shape='DISK';data.size=size
 o=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Dominant warm theatrical light / grouped deep shadow',(-3.5,-1.8,5.0),570,(1,.71,.39),1.7,(0,0,.4))
area('Subdued violet pigment bounce, not rim',(2,2.2,3),30,(.48,.44,1),3,(0,0,.4))
camdata=bpy.data.cameras.new('Still-life camera');cam=bpy.data.objects.new('Still-life camera',camdata);bpy.context.collection.objects.link(cam)
cam.location=(3.7,-5.7,6.4);target=Vector((0,-.08,.43));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();camdata.type='ORTHO';camdata.ortho_scale=4.18
sc=bpy.context.scene;sc.camera=cam;sc.render.engine='CYCLES';sc.cycles.device='CPU';sc.cycles.samples=opt.samples;sc.cycles.seed=731;sc.cycles.use_denoising=True
sc.render.threads_mode='FIXED';sc.render.threads=2;sc.render.resolution_x=768;sc.render.resolution_y=768;sc.render.resolution_percentage=100
sc.render.image_settings.file_format='PNG';sc.render.image_settings.color_mode='RGBA';sc.render.image_settings.color_depth='8';sc.render.film_transparent=False
sc.view_settings.view_transform='AgX';sc.view_settings.exposure=.05
sc.render.filepath=str(out/'master.png');sc.render.use_file_extension=True
# Self-contained editable scene, no linked resources or packed purchaser art.
bpy.ops.wm.save_as_mainfile(filepath=str(out/'sorcery-still-life.blend'))
bpy.ops.render.render(write_still=True)
(out/'render-settings.txt').write_text('Blender '+bpy.app.version_string+'\nCPU Cycles; threads2; samples'+str(opt.samples)+'; seed731; denoising; AgX; exposure.05; RGBA8;768x768; opaque contextual ground\nOriginal geometry/procedural materials only. NOT user-approved or painted by a human.\n')
print('SORCERY_PROOF_RENDER_FINISHED',out)
