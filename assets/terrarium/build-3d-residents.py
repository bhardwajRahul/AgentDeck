"""Author complete aquatic characters; canonical SVGs are small identity badges.
Local XY is the face plane, +Z faces the viewer. Joint names are a runtime API.
"""
from pathlib import Path
import bpy, math, re
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
scene=bpy.data.scenes.new('AgentDeck 3D Residents')
bpy.context.window.scene=scene
tokens=(ROOT/'design/tokens.css').read_text()
def token(name):
    value=re.search(r'--'+name+r':\s*#([0-9a-fA-F]{6})',tokens).group(1)
    values=[int(value[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4 for c in values)
def material(name,color,rough=.48):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
    shader=m.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value=m.diffuse_color
    shader.inputs['Roughness'].default_value=rough
    shader.inputs['Metallic'].default_value=0
    return m
ink=material('Aquarium ink eyes',token('ink-900'),.24)
sand=material('Soft sand underside',token('tide-50'),.62)
brands=['claudecode','codex','openclaw','opencode','antigravity','kiro']
for brand in brands:
    root=bpy.data.objects.new('resident_'+brand,None);scene.collection.objects.link(root)
    color=token('brand-'+('claude-code' if brand=='claudecode' else brand))
    skin=material(brand+' skin',color)
    finmat=material(brand+' fins',tuple(c*.74 for c in color),.56)
    def ellipsoid(name,position,scale,surface=skin):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=40,ring_count=24,radius=1)
        o=bpy.context.object;o.name=brand+'_'+name;o.parent=root;o.location=position;o.scale=scale
        o.data.materials.append(surface)
        for polygon in o.data.polygons:polygon.use_smooth=True
        return o
    def joint(o,name,pivot):
        anchor=bpy.data.objects.new('joint_'+name,None);scene.collection.objects.link(anchor)
        anchor.parent=root;anchor.location=pivot;o.parent=anchor;o.location-=Vector(pivot)
        return anchor
    def tube(name,points,radii,surface=skin):
        # Tapered continuous appendage, closed at both ends.
        verts=[];faces=[];sides=12
        for i,(p,r) in enumerate(zip(points,radii)):
            tangent=Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])
            tangent.normalize();axis=tangent.cross(Vector((0,0,1)))
            if axis.length<.01:axis=tangent.cross(Vector((0,1,0)))
            axis.normalize();other=tangent.cross(axis)
            for j in range(sides):
                a=j/sides*math.tau;verts.append(Vector(p)+r*(math.cos(a)*axis+math.sin(a)*other))
        for i in range(len(points)-1):
            for j in range(sides):
                a=i*sides+j;b=i*sides+(j+1)%sides;faces.append((a,b,b+sides,a+sides))
        faces.extend([tuple(reversed(range(sides))),tuple((len(points)-1)*sides+j for j in range(sides))])
        mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update()
        o=bpy.data.objects.new(brand+'_'+name,mesh);scene.collection.objects.link(o);o.parent=root;mesh.materials.append(surface)
        for p in mesh.polygons:p.use_smooth=True
        sub=o.modifiers.new('Soft appendage','SUBSURF');sub.levels=1
        bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=sub.name)
        return o
    grounded=brand in {'claudecode','openclaw'}
    if grounded:
        # One smooth domed carapace; no layered logo silhouette, mould seam or rear relief.
        ellipsoid('body',(0,.02,-.07),(.43,.29,.39))
        ellipsoid('underside',(0,-.16,-.01),(.35,.125,.30),sand)
        for side in [-1,1]:
            for n,z in enumerate([.19,-.05,-.27]):
                x=side*.32
                limb=tube('leg',[(x,-.09,z),(side*.47,-.17,z-.035),(side*.54,-.38,z-.02),(side*.61,-.405,z+.04)],[.052,.045,.032,.014],finmat)
                joint(limb,'foot_'+str(n*2+(side>0)),(x,-.09,z))
            arm=ellipsoid('claw' if brand=='openclaw' else 'arm',(side*.48,.025,.23),(.145,.105,.17))
            joint(arm,('claw_' if brand=='openclaw' else 'arm_')+str(int(side>0)),(side*.34,-.015,.12))
        if brand=='openclaw':
            for side in [-1,1]:
                antenna=tube('antenna',[(side*.13,.19,.19),(side*.18,.34,.22),(side*.29,.40,.17)],[.024,.017,.004],finmat)
                joint(antenna,'antenna_'+str(int(side>0)),(side*.13,.19,.19))
    else:
        # A complete cuttlefish-like mantle, subtly different in proportion by species.
        scale={'codex':(.38,.36,.39),'opencode':(.32,.40,.35),'antigravity':(.32,.35,.44),'kiro':(.34,.42,.35)}[brand]
        ellipsoid('body',(0,.035,-.045),scale)
        ellipsoid('underside',(0,-.15,.17),(.245,.17,.17),sand)
        for side in [-1,1]:
            fin=ellipsoid('fin',(side*.34,-.02,-.11),(.21,.035,.28),finmat)
            fin.rotation_euler.y=side*.18
            joint(fin,'fin_'+str(int(side>0)),(side*.24,-.015,-.07))
        for n in range(5):
            x=(n-2)*.09
            limb=tube('tentacle',[(x,-.22,.10),(x*1.1,-.35,.14),(x*1.18,-.48,.24),(x*1.12,-.50,.29)],[.046,.032,.021,.006],finmat)
            joint(limb,'tentacle_'+str(n),(x,-.22,.10))
    # Forward eyes are seated on the body, never pasted onto a rear extrusion.
    for side in [-1,1]:
        x=side*.18;y=.13;z=.275
        socket=ellipsoid('eye_socket',(x,y,z),(.090,.083,.060),sand)
        anchor=joint(socket,'eye_'+str(int(side>0)),(x,y,z))
        for o in [ellipsoid('eye',(x,y+.004,z+.045),(.043,.050,.024),ink),
                  ellipsoid('catchlight',(x-.012,y+.023,z+.068),(.011,.013,.006),sand)]:
            o.parent=anchor;o.location-=anchor.location
    # Keep upstream mark geometry intact as a small chest insignia.
    before=set(scene.objects);bpy.ops.import_curve.svg(filepath=str(ROOT/'design/brand'/f'{brand}.svg'))
    parts=[o for o in scene.objects if o not in before and o.type=='CURVE']
    bpy.context.view_layer.update();bounds=[o.matrix_world@Vector(c) for o in parts for c in o.bound_box]
    low=Vector(tuple(min(p[i] for p in bounds) for i in range(3)));high=Vector(tuple(max(p[i] for p in bounds) for i in range(3)))
    center=(low+high)/2;factor=.18/max(high.x-low.x,high.y-low.y)
    for o in parts:
        bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
        o.location=(o.location-center)*factor;o.scale*=factor
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        for spline in o.data.splines:
            for point in list(spline.bezier_points)+list(spline.points):point.radius=1
        o.data.dimensions='2D';o.data.fill_mode='BOTH';o.data.extrude=.003;o.data.bevel_depth=.002;o.data.bevel_resolution=2
        o.data.materials.clear();o.data.materials.append(ink)
        bpy.ops.object.convert(target='MESH');o=bpy.context.object;o.parent=root;o.name=brand+'_canonical_badge'
        o.location+=Vector((0,-.125,.345 if grounded else .335))
    root.rotation_euler.x=math.pi/2;root.location.x=brands.index(brand)*1.5
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/terrarium/3d-residents.blend'))
for o in scene.objects:
    if o.name.startswith('resident_'):o.location.x=0
for o in scene.objects:o.select_set(True)
bpy.ops.wm.usd_export(filepath=str(ROOT/'apple/AgentDeck/Resources/Aquarium/3d-residents.usdz'),selected_objects_only=True,
    export_animation=False,triangulate_meshes=True,generate_preview_surface=True,convert_orientation=True,
    export_global_forward_selection='NEGATIVE_Z',export_global_up_selection='Y')
print('Exported six complete aquatic characters')
