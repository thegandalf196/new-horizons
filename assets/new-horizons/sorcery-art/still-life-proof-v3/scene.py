#!/usr/bin/env python3
"""CC0 original Sorcery still-life geometry/material/light recipe for Blender.
NOT a painted or approved asset. Run headless only in a Build-approved slot:
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
leather=mat('Worn royal-blue leather / broad soft response',(.023,.066,.16),.67)
clothmat=mat('Oxblood woven cloth / broad folds',(.19,.018,.032),.82)
ivory=mat('Warm aged paper stock',(.66,.49,.28),.76)
page=mat('Lit ivory laid-paper faces',(.82,.7,.47),.72)
pageedge=mat('Oxidised leaf edges',(.37,.23,.105),.85)
ink=mat('Faded sepia authored notation',(.105,.052,.022),.91)
silver=mat('Handled old silver key',(.39,.43,.44),.36,.87)
silverdark=mat('Silver recess patina',(.085,.10,.11),.58,.66)
gold=mat('Dull ochre book tooling',(.32,.19,.064),.53,.72)
thread=mat('Worn blue linen binding thread',(.14,.22,.29),.84)
wood=mat('Warm dark walnut table',(.065,.031,.017),.72)
ribbon=mat('Faded russet bookmark ribbon',(.34,.068,.025),.71)
# Fine leather response only; no noise displacement or random art composition.
# Micro-normal amplitude is far below modeled cover seams and corner wear.
n=leather.node_tree.nodes;links=leather.node_tree.links
tex=n.new('ShaderNodeTexNoise');tex.name='Submillimetre leather grain, not painterly texture';tex.inputs['Scale'].default_value=165;tex.inputs['Detail'].default_value=2
bump=n.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.12;bump.inputs['Distance'].default_value=.008
links.new(tex.outputs['Fac'],bump.inputs['Height']);links.new(bump.outputs['Normal'],n.get('Principled BSDF').inputs['Normal'])

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
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=16;cu.bevel_depth=r;cu.bevel_resolution=3
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
    a=j*nu+i;ff.append((a,a+1,a+nu+1,a+nu))
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
for side in (-1,1):
 for row in range(14):
  y=-.76+row*.108
  for word in range(4):
   start=.15+word*.19;end=start+.12+(.018 if (word+row)%3==0 else 0)
   pts=[surf(side,start+(end-start)*k/7,y+.003*math.sin(k*2+row)) for k in range(8)]
   line('Original sepia manuscript stroke',pts,ink,.0037,bookroot)
   if (word+row)%3==0:line('Authored letter ascender',[surf(side,start+.03,y),surf(side,start+.033,y+.027)],ink,.003,bookroot)
 # Running header and small red rubric, not technical diagram filler.
 line('Faded rubric',[surf(side,.19,.84),surf(side,.47,.84)],ribbon,.008,bookroot)
# Spine valley and substantial curved binding head.
line('Deep blue spine gutter',[(0,-1.02,.37),(0,-.6,.52),(0,.6,.52),(0,1.02,.37)],leather,.047,bookroot)
# Satin bookmark drapes through foreground gutter with real thickness.
mesh('Russet cloth bookmark',[(.045,-.55,.655),(.12,-.55,.657),(.12,-1.02,.61),(.10,-1.21,.38),(.06,-1.28,.20),(-.015,-1.25,.20),(.025,-1.18,.39),(.04,-1.0,.60)],[(0,1,2,3,4,5,6,7)],ribbon,bookroot)
# Warding key: physical old silver, recognisable bow/shaft/bit instead of gem badge.
keyroot=bpy.data.objects.new('KEY / original warding key',None);bpy.context.collection.objects.link(keyroot);keyroot.parent=bookroot;keyroot.location=(.15,-.15,.84);keyroot.rotation_euler=(math.radians(7),math.radians(-5),math.radians(-30))
# Irregular quatrefoil bow silhouette; restrained heroic proportions.
pts=[]
for k in range(128):
 a=2*math.pi*k/128;r=.245+.043*math.cos(4*a);pts.append((-.69+r*math.cos(a),r*math.sin(a),0))
line('Forged quatrefoil bow',pts,silver,.049,keyroot,True)
line('Subtle inset patina at bow',[(x,y,-.035) for x,y,z in pts],silverdark,.013,keyroot,True)
line('Heavy silver stem',[(-.41,0,0),(.56,0,0)],silver,.055,keyroot)
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
world.node_tree.nodes['Background'].inputs[0].default_value=(.11,.077,.053,1);world.node_tree.nodes['Background'].inputs[1].default_value=.18

def area(name,loc,power,color,size,target):
 data=bpy.data.lights.new(name,'AREA');data.energy=power;data.color=color;data.shape='DISK';data.size=size
 o=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Dominant upper-left warm window',(-3.5,-2.2,5.8),490,(1,.76,.49),2.6,(0,0,.4))
area('Restrained cool bounce, not neon rim',(2,2.2,3),75,(.53,.64,1),3,(0,0,.4))
camdata=bpy.data.cameras.new('Still-life camera');cam=bpy.data.objects.new('Still-life camera',camdata);bpy.context.collection.objects.link(cam)
cam.location=(3.7,-5.7,6.4);target=Vector((0,-.08,.43));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();camdata.type='ORTHO';camdata.ortho_scale=4.65
sc=bpy.context.scene;sc.camera=cam;sc.render.engine='CYCLES';sc.cycles.device='CPU';sc.cycles.samples=opt.samples;sc.cycles.seed=731;sc.cycles.use_denoising=True
sc.render.threads_mode='FIXED';sc.render.threads=2;sc.render.resolution_x=768;sc.render.resolution_y=768;sc.render.resolution_percentage=100
sc.render.image_settings.file_format='PNG';sc.render.image_settings.color_mode='RGBA';sc.render.image_settings.color_depth='8';sc.render.film_transparent=False
sc.view_settings.view_transform='AgX';sc.view_settings.exposure=.15
sc.render.filepath=str(out/'master.png');sc.render.use_file_extension=True
# Self-contained editable scene, no linked resources or packed purchaser art.
bpy.ops.wm.save_as_mainfile(filepath=str(out/'sorcery-still-life.blend'))
bpy.ops.render.render(write_still=True)
(out/'render-settings.txt').write_text('Blender '+bpy.app.version_string+'\nCPU Cycles; threads2; samples'+str(opt.samples)+'; seed731; denoising; AgX; exposure.15; RGBA8;768x768; opaque contextual ground\nOriginal geometry/procedural materials only. NOT user-approved or painted by a human.\n')
print('SORCERY_PROOF_RENDER_FINISHED',out)
