"""Portable solid fins and quiet freshwater inhabitants for build-dark-garden.py.
Executed inside the authored scene; no replacement agent characters.
"""
import bpy, math, re
from pathlib import Path
from mathutils import Vector
scene = bpy.context.scene
# Fin membranes need side walls, not merely double-sided rasterization. Rebase
# the caudal mesh to its peduncle: rotation around the body origin tears it away.
for obj in list(scene.objects):
    if obj.type != 'MESH' or 'fin' not in obj.name.lower():
        continue
    obj.data = obj.data.copy()
    if 'caudal' in obj.name.lower():
        hinge = Vector((max(v.co.x for v in obj.data.vertices), 0, 0))
        for v in obj.data.vertices:
            v.co -= hinge
        obj.location += hinge
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new('Membrane volume', 'SOLIDIFY')
    modifier.thickness = 0.045
    modifier.offset = 0
    bpy.ops.object.modifier_apply(modifier=modifier.name)

# Use existing design tokens rather than a second material palette.
css = (Path(__file__).resolve().parents[2] / 'design/tokens.css').read_text()
def material(name, token, roughness):
    value = re.search(r'--' + token + r':\s*#([0-9a-fA-F]{6})', css).group(1)
    srgb = [int(value[i:i+2],16)/255 for i in (0,2,4)]
    rgb = [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in srgb]
    m=bpy.data.materials.new('Fauna '+name);m.diffuse_color=(*rgb,1);m.use_nodes=True
    shader=m.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=(*rgb,1)
    shader.inputs['Roughness'].default_value=roughness
    return m
shell=material('shell','tide-300',.44)
stripe=material('shell bands','ink-700',.58)
flesh=material('foot','kelp-500',.65)
carapace=material('carapace','tide-200',.34)
eye=material('eyes','ink-900',.3)

def root(name, location):
    o=bpy.data.objects.new(name,None);scene.collection.objects.link(o);o.location=location;return o

def ellipsoid(name, parent, location, scale, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,radius=1)
    o=bpy.context.object;o.name=name;o.parent=parent;o.location=location;o.scale=scale
    o.data.materials.append(mat)
    for face in o.data.polygons: face.use_smooth=True
    return o

def strand(name,parent,points,radius,mat):
    curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.bevel_depth=radius;curve.bevel_resolution=2
    spline=curve.splines.new('BEZIER');spline.bezier_points.add(len(points)-1)
    for p,co in zip(spline.bezier_points,points):p.co=co;p.handle_left_type='AUTO';p.handle_right_type='AUTO'
    o=bpy.data.objects.new(name,curve);scene.collection.objects.link(o);o.parent=parent;curve.materials.append(mat)
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.convert(target='MESH')
    return o

# A low foreground grazing snail; its sole stays on the substrate. A spiral
# grows continuously out of the shell rather than floating as a separate ring.
snail=root('Fauna snail',(-3.1,-.02,.93))
ellipsoid('Snail foot',snail,(0,0,.075),(.32,.15,.075),flesh)
ellipsoid('Snail shell',snail,(-.08,0,.24),(.23,.18,.22),shell)
spiral=[]
for i in range(100):
    t=i/99;angle=t*math.tau*2.2;r=.012+.17*t
    spiral.append((-.08+math.cos(angle)*r,-.155-.024*(1-t),.24+math.sin(angle)*r))
strand('Snail shell spiral',snail,spiral,.014,stripe)
for side in (-1,1):
    feeler=root('Snail feeler', (0,0,0));feeler.parent=snail;feeler.location=(.19,side*.075,.11)
    strand('Snail eyestalk',feeler,[(0,0,0),(.06,side*.03,.08),(.09,side*.045,.14)],.012,flesh)
    ellipsoid('Snail eye',feeler,(.09,side*.045,.14),(.018,.018,.018),eye)
    for frame in range(1,722,15):
        feeler.rotation_euler[1]=.12*math.sin((frame-1)/720*math.tau*2+side)
        feeler.keyframe_insert(data_path='rotation_euler',frame=frame)
for frame in range(1,722,15):
    t=(frame-1)/720*math.tau
    sx=-3.1+.17*math.sin(t);sy=-.02+.07*math.cos(t)
    # Match the retained left habitat rock, independent of session platforms.
    sz=.35+.65*math.sqrt(max(0,1-((sx+3.1)/1.5)**2-((sy-.4)/.8)**2))+.02
    snail.location=(sx,sy,sz)
    snail.rotation_euler[2]=.18*math.sin(t)
    snail.keyframe_insert(data_path='location',frame=frame);snail.keyframe_insert(data_path='rotation_euler',frame=frame)

# Two bottom-foraging shrimp: segmented abdomen, tail fan, fine walking legs
# and antennae. Keep the central session stage clear, all parts solid meshes.
for index,(x,y) in enumerate([(-3.85,-.02),(3.3,2.15)]):
    shrimp=root('Fauna shrimp '+str(index),(x,y,-.09))
    shrimp.scale=(.8,.8,.8)
    ellipsoid('Shrimp thorax',shrimp,(.13,0,.16),(.22,.105,.13),carapace)
    for n in range(5):
        ellipsoid('Shrimp abdominal segment',shrimp,(-.08-n*.07,0,.14-n*.012),(.08,.095-n*.012,.095-n*.01),carapace)
    for side in (-1,0,1):
        fan=ellipsoid('Shrimp tail fan',shrimp,(-.43,side*.058,.055),(.10,.047,.018),shell)
        fan.rotation_euler[2]=side*.45
    for side in (-1,1):
        ellipsoid('Shrimp eye',shrimp,(.27,side*.075,.22),(.025,.022,.025),eye)
        strand('Shrimp antenna',shrimp,[(.25,side*.05,.20),(.45,side*.16,.28),(.73,side*.23,.26)],.008,shell)
        for n in range(4):
            leg=root('Shrimp leg',(0,0,0));leg.parent=shrimp;leg.location=(.18-n*.10,side*.06,.14)
            strand('Shrimp walking limb',leg,[(0,0,0),(.015,side*.11,-.04),(.06,side*.16,-.13)],.009,carapace)
            for frame in range(1,722,5):
                phase=(frame-1)/720*math.tau*12+n*1.5+side
                leg.rotation_euler[2]=.20*math.sin(phase)
                leg.keyframe_insert(data_path='rotation_euler',frame=frame)
    for frame in range(1,722,5):
        t=(frame-1)/720*math.tau
        # Small slow foraging circuit rather than constant midwater hovering.
        sx=x+.12*math.sin(t+index);sy=y+.04*math.cos(t+index)
        cx,cy,cz,rx,ry,rz=((-3.1,.4,.35,1.5,.8,.65) if index==0 else (3.3,2.5,.2,.95,.65,.5))
        sz=cz+rz*math.sqrt(max(0,1-((sx-cx)/rx)**2-((sy-cy)/ry)**2))+.01
        shrimp.location=(sx,sy,sz)
        shrimp.rotation_euler[2]=(.4 if index==0 else -.5)+.25*math.sin(t+index)
        shrimp.keyframe_insert(data_path='location',frame=frame);shrimp.keyframe_insert(data_path='rotation_euler',frame=frame)
scene.frame_set(1)
print('Solid hinged fins; one grazing snail and two foraging shrimp authored')
