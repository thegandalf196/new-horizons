#!/usr/bin/env python3
"""CC0 original modeled conjuring-hand subject proof, Blender headless only.
No reference pixels/meshes/textures/fonts. NOT a human-painted or approved image.
Run only in a Build-authorized bounded CPU slot. Source preparation is not rendering.
"""
import argparse,math,sys
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('--samples',type=int,default=32)
opt=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
OUT=ROOT/'output';OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def material(name,c,rough=.8,metal=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*c,1);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*c,1);b.inputs['Roughness'].default_value=rough;b.inputs['Metallic'].default_value=metal;b.inputs['Specular IOR Level'].default_value=.18;return m
skin=material('Illustrated warm skin / broad value planes',(.48,.24,.105),.83)
crease=material('Restricted warm palm creases',(.24,.085,.027),.92)
nail=material('Subdued ivory-pink natural nails',(.57,.34,.19),.75)
cloth=material('Deep mulberry velvet sleeve',(.075,.018,.105),.93)
clothlit=material('Woven blue-violet cuff highlights',(.14,.075,.22),.88)
gold=material('Worn ochre cuff embroidery',(.40,.25,.075),.72,.35)
magic=material('Restrained silver-violet magic thread',(.42,.35,.62),.62,.15)
b=magic.node_tree.nodes.get('Principled BSDF');b.inputs['Emission Color'].default_value=(.25,.16,.46,1);b.inputs['Emission Strength'].default_value=.22
spark=material('Small selective ivory silver accents',(.80,.73,.57),.62)
back=material('Warm umber background / no photographic texture',(.056,.028,.019),.95)
# Deliberate soft skin tone grouping in object-space height, not skin noise.
n=skin.node_tree.nodes;l=skin.node_tree.links;co=n.new('ShaderNodeTexCoord');sep=n.new('ShaderNodeSeparateXYZ');r=n.new('ShaderNodeValToRGB');r.color_ramp.interpolation='EASE'
r.color_ramp.elements[0].position=.12;r.color_ramp.elements[0].color=(.22,.072,.028,1);r.color_ramp.elements[1].position=.82;r.color_ramp.elements[1].color=(.64,.37,.17,1)
e=r.color_ramp.elements.new(.48);e.color=(.45,.19,.075,1);l.new(co.outputs['Generated'],sep.inputs[0]);l.new(sep.outputs['Z'],r.inputs[0]);l.new(r.outputs['Color'],n.get('Principled BSDF').inputs['Base Color'])

def ellipsoid(name,loc,scale,m):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=28,ring_count=18,radius=1,location=loc);o=bpy.context.object;o.name=name;o.scale=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(m)
 for f in o.data.polygons:f.use_smooth=True
 return o

def mesh(name,verts,faces,m):
 me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);o.data.materials.append(m)
 for f in me.polygons:f.use_smooth=True
 return o

def curve(name,points,m,radius=.012,closed=False):
 cu=bpy.data.curves.new(name,'CURVE');cu.dimensions='3D';cu.resolution_u=20;cu.bevel_depth=radius;cu.bevel_resolution=4;cu.use_fill_caps=True
 sp=cu.splines.new('BEZIER');sp.bezier_points.add(len(points)-1)
 for p,co in zip(sp.bezier_points,points):p.co=co;p.handle_left_type='AUTO';p.handle_right_type='AUTO'
 sp.use_cyclic_u=closed;o=bpy.data.objects.new(name,cu);bpy.context.collection.objects.link(o);o.data.materials.append(m);return o

# Palm, thenar mound and tapered wrist are joined to true articulated finger volumes.
parts=[]
parts.append(ellipsoid('Palm volume',(.12,0,1.62),(.60,.245,.67),skin))
parts.append(ellipsoid('Thenar thumb mound',(-.29,-.13,1.52),(.35,.23,.42),skin))
parts.append(ellipsoid('Tapered wrist',(.13,.025,.94),(.34,.22,.49),skin))
# Each digit is explicitly posed from metacarpal through joints to tip.
# The index and thumb pinch an OPEN arc; other digits curl, not five straight spikes.
digits={
 'thumb':([(-.35,-.02,1.55),(-.68,-.09,1.85),(-.88,-.16,2.15),(-.76,-.20,2.37)],[.205,.18,.145,.105]),
 'index':([(-.25,0,2.04),(-.30,-.015,2.52),(-.40,-.08,2.96),(-.62,-.17,3.12)],[.17,.148,.12,.09]),
 'middle':([(.10,.005,2.12),(.12,.01,2.61),(.17,-.28,2.49),(.18,-.43,2.10)],[.178,.156,.135,.103]),
 'ring':([(.43,.02,2.04),(.48,.01,2.43),(.49,-.27,2.33),(.43,-.43,1.96)],[.16,.14,.12,.093]),
 'little':([(.66,.035,1.92),(.76,-.01,2.20),(.76,-.26,2.08),(.63,-.38,1.78)],[.137,.116,.099,.077])}
for name,(points,radii) in digits.items():
 for i,(p,radius) in enumerate(zip(points,radii)):
  parts.append(ellipsoid(name+' joint '+str(i),p,(radius,radius*.86,radius),skin))
 for i in range(len(points)-1):
  p,q=Vector(points[i]),Vector(points[i+1]);delta=q-p;rad=(radii[i]+radii[i+1])/2
  o=ellipsoid(name+' modeled phalanx '+str(i),(p+q)/2,(rad,rad*.87,delta.length/2+rad*.35),skin);o.rotation_euler=delta.to_track_quat('Z','Y').to_euler();parts.append(o)
# Voxel union is a geometric sculpting operation, not a claim of manual sculpting.
bpy.ops.object.select_all(action='DESELECT')
for o in parts:o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();hand=bpy.context.object;hand.name='Original five-digit posed hand / unified modeled surface'
rem=hand.modifiers.new('Union anatomical volumes','REMESH');rem.mode='VOXEL';rem.voxel_size=.033;rem.use_smooth_shade=True;bpy.ops.object.modifier_apply(modifier=rem.name)
sm=hand.modifiers.new('Soften joined anatomical transitions','SMOOTH');sm.factor=.72;sm.iterations=5;bpy.ops.object.modifier_apply(modifier=sm.name)
# Few restrained palm lines help anatomy; no full outline/photographic skin pores.
curve('Thenar crease',[(-.27,-.335,1.24),(-.39,-.337,1.52),(-.31,-.295,1.79)],crease,.009)
curve('Palm upper flexion line',[(-.22,-.248,1.91),(.09,-.263,1.83),(.40,-.237,1.79)],crease,.008)
curve('Palm central flexion line',[(-.15,-.269,1.53),(.12,-.262,1.61),(.32,-.241,1.62)],crease,.007)
# Visible nails on curled fingertips are subtle, not large white manicure blocks.
for name in ('middle','ring','little'):
 p=Vector(digits[name][0][-1]);rad=digits[name][1][-1]
 ellipsoid(name+' subtle nail',p+Vector((0,-rad*.77,.025)),(rad*.56,.019,rad*.72),nail)
# Sleeve mesh: elongated fold ribs, substantial slanted cuff, no rigid medallion.
verts=[];faces=[];rings=18;around=64
for j in range(rings):
 t=j/(rings-1);z=.05+.99*t
 for i in range(around):
  ang=2*math.pi*i/around
  radius=.60-.24*t+.045*math.cos(7*ang+.45)*(.6+.4*t)
  verts.append((.14+radius*math.cos(ang),.05+radius*.64*math.sin(ang),z+.07*math.cos(ang)*t))
for j in range(rings-1):
 for i in range(around):
  a=j*around+i;b=j*around+(i+1)%around;faces.append((a,b,b+around,a+around))
sleeve=mesh('Soft fluted velvet sleeve',verts,faces,cloth);sol=sleeve.modifiers.new('Real sleeve thickness','SOLIDIFY');sol.thickness=.025
for z,m,rr in ((.89,gold,.028),(1.02,gold,.022),(.95,clothlit,.045)):
 pts=[]
 for i in range(48):
  ang=2*math.pi*i/48;pts.append((.14+.375*math.cos(ang),.05+.245*math.sin(ang),z+.07*math.cos(ang)))
 curve('Curved cuff embroidered band',pts,m,rr,True)
for x in (-.06,.13,.32):
 curve('Small stitched cuff chevron',[(x-.035,-.21,.94),(x,-.22,.97),(x+.035,-.21,.94)],gold,.009)
# Magic is a single spatially bending silver-violet thread between controlled tips.
# It is NOT lightning, flame, closed VFX ring, orb or saturated emissive neon.
curve('Actively shaped open silver-violet thread',[(-.64,-.18,3.14),(-1.00,-.20,3.22),(-1.29,-.18,3.03),(-1.22,-.25,2.79),(-.97,-.30,2.77),(-.89,-.26,2.59),(-.78,-.22,2.40)],magic,.027)
curve('Selective lit segment of thread',[(-1.02,-.217,3.22),(-1.20,-.209,3.14),(-1.28,-.19,3.04)],spark,.012)
curve('Small dislocated trailing echo',[(-1.37,-.15,2.84),(-1.41,-.14,2.71),(-1.31,-.17,2.64)],magic,.010)
# Ground and backdrop provide warm contextual mass, no imported scenery.
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,1.1,0));ground=bpy.context.object;ground.name='Warm dark contextual ground';ground.data.materials.append(back)
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,2,2),rotation=(math.pi/2,0,0));bpy.context.object.name='Warm dark background';bpy.context.object.data.materials.append(back)
world=bpy.data.worlds.new('Deep warm ambience');world.use_nodes=True;world.node_tree.nodes['Background'].inputs[0].default_value=(.09,.055,.035,1);world.node_tree.nodes['Background'].inputs[1].default_value=.075;bpy.context.scene.world=world

def area(name,loc,energy,col,size,target):
 d=bpy.data.lights.new(name,'AREA');d.energy=energy;d.color=col;d.shape='DISK';d.size=size;o=bpy.data.objects.new(name,d);bpy.context.collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
area('Dominant warm theatrical hand light',(-3,-4,5.5),460,(1,.75,.49),2,(-.1,0,1.8))
area('Restricted violet arcane bounce',(-1.5,-1,3),24,(.57,.44,1),1.2,(-.4,0,2.2))
area('Subdued cool sleeve fill',(2,0,2),32,(.45,.53,.83),3,(0,0,1))
c=bpy.data.cameras.new('Gesture camera');cam=bpy.data.objects.new('Gesture camera',c);bpy.context.collection.objects.link(cam);cam.location=(.30,-7.8,4.0);target=Vector((-.22,0,1.68));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();c.type='ORTHO';c.ortho_scale=3.85
s=bpy.context.scene;s.camera=cam;s.render.engine='CYCLES';s.cycles.device='CPU';s.cycles.samples=opt.samples;s.cycles.seed=731;s.cycles.use_denoising=True;s.render.threads_mode='FIXED';s.render.threads=2
s.render.resolution_x=768;s.render.resolution_y=768;s.render.resolution_percentage=100;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA';s.render.image_settings.color_depth='8';s.view_settings.view_transform='AgX';s.view_settings.exposure=.05;s.render.filepath=str(OUT/'master.png')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'sorcery-conjuring-hand.blend'));bpy.ops.render.render(write_still=True)
(OUT/'render-settings.txt').write_text('Blender '+bpy.app.version_string+'; CPU2 Cycles; samples'+str(opt.samples)+'; seed731; AgX exposure.05;768RGBA; original modeled hand/cloth/thread. NOT user-approved or manually painted.\n')
print('SORCERY_HAND_PROOF_FINISHED')
