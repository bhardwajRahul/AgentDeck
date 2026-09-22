"""Give canonical character silhouettes depth without redesigning their anatomy.
No added eyes, shells, fins, tentacles, insignia, or substitute body shapes.
"""
from pathlib import Path
import bpy, bmesh, math, re
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
scene=bpy.data.scenes.new('AgentDeck 3D Residents');bpy.context.window.scene=scene
tokens=(ROOT/'design/tokens.css').read_text()
brands=['claudecode','codex','openclaw','opencode','antigravity','kiro']
def color(name):
    value=re.search(r'--brand-'+name+r':\s*#([0-9a-fA-F]{6})',tokens).group(1)
    rgb=[int(value[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4 for c in rgb)
def convert(o):
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.object.convert(target='MESH');return bpy.context.object
def hinge(o,name,pivot,root):
    anchor=bpy.data.objects.new('joint_'+name,None);scene.collection.objects.link(anchor)
    anchor.parent=root;anchor.location=pivot;o.parent=anchor;o.location-=Vector(pivot)
def clipped(source,name,planes):
    bm=bmesh.new();bm.from_mesh(source.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
    for origin,normal in planes:
        cut=bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=.000001,
            plane_co=origin,plane_no=normal,clear_outer=True,clear_inner=False)
        edges=[e for e in cut['geom_cut'] if isinstance(e,bmesh.types.BMEdge) and e.is_boundary]
        if edges:bmesh.ops.holes_fill(bm,edges=edges,sides=0)
    mesh=bpy.data.meshes.new(name);bm.to_mesh(mesh);bm.free()
    o=bpy.data.objects.new(name,mesh);scene.collection.objects.link(o)
    o.data.materials.append(source.data.materials[0]);return o
for brand in brands:
    root=bpy.data.objects.new('resident_'+brand,None);scene.collection.objects.link(root)
    rgb=color('claude-code' if brand=='claudecode' else brand)
    mat=bpy.data.materials.new(brand+' original');mat.diffuse_color=(*rgb,1);mat.use_nodes=True
    shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=mat.diffuse_color
    shader.inputs['Roughness'].default_value=.46;shader.inputs['Metallic'].default_value=0
    before=set(scene.objects);bpy.ops.import_curve.svg(filepath=str(ROOT/'design/brand'/f'{brand}.svg'))
    parts=[o for o in scene.objects if o not in before and o.type=='CURVE']
    bpy.context.view_layer.update();bounds=[o.matrix_world@Vector(c) for o in parts for c in o.bound_box]
    low=Vector(tuple(min(p[i] for p in bounds) for i in range(3)));high=Vector(tuple(max(p[i] for p in bounds) for i in range(3)))
    center=(low+high)/2;factor=1/max(high.x-low.x,high.y-low.y)
    meshes=[]
    for n,o in enumerate(parts):
        bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
        o.location=(o.location-center)*factor;o.scale*=factor
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        for spline in o.data.splines:
            for p in list(spline.bezier_points)+list(spline.points):p.radius=1
        o.data.dimensions='2D';o.data.fill_mode='BOTH';o.data.extrude=.115
        # Restrained edge softening preserves the source's pixel/curve character.
        o.data.bevel_depth=.006 if brand in {'claudecode','opencode'} else .012
        o.data.bevel_resolution=4;o.data.resolution_u=16
        o.data.materials.clear();o.data.materials.append(mat)
        # Close facial cutouts only at the rear. The back follows the exact
        # outer outline, without a convex hull, dorsal bumps or a second body.
        if len(o.data.splines)>1:
            back=o.copy();back.data=o.data.copy();scene.collection.objects.link(back)
            def area(sp):
                pts=[p.co for p in (sp.bezier_points if sp.type=='BEZIER' else sp.points)]
                return abs(sum(a.x*b.y-b.x*a.y for a,b in zip(pts,pts[1:]+pts[:1])))
            outer=max(back.data.splines,key=area)
            for sp in list(back.data.splines):
                if sp!=outer:back.data.splines.remove(sp)
            back.data.extrude=.025;back.data.bevel_depth=0
            back.location.z=-.089-o.data.bevel_depth
            back=convert(back);back.name=brand+'_rear';back.parent=root;meshes.append(back)
        o=convert(o);o.name=brand+'_canonical_'+str(n);o.parent=root;meshes.append(o)
    if brand=='claudecode':
        # Split only existing limbs from the source; neutral pose is unchanged.
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes:o.select_set(True)
        bpy.context.view_layer.objects.active=meshes[0];bpy.ops.object.join();source=bpy.context.object
        bpy.ops.object.transform_apply(location=True,rotation=False,scale=True)
        middle=[((.375,0,0),(1,0,0)),((-.375,0,0),(-1,0,0))]
        torso=clipped(source,'claudecode_canonical_body',middle+[((0,-.19079,0),(0,-1,0))]);torso.parent=root
        for index,side in enumerate([-1,1]):
            arm=clipped(source,'claudecode_canonical_arm', [((side*.375,0,0),(-side,0,0))])
            hinge(arm,'arm_'+str(index),(side*.375,0,0),root)
        # Original four pixel feet, cut at their existing junction with the body.
        intervals=[(-.31305,-.25),(-.18805,-.125),(.125,.18805),(.25,.31305)]
        for i,(left,right) in enumerate(intervals):
            foot=clipped(source,'claudecode_canonical_foot', [((0,-.19079,0),(0,1,0)),((left,0,0),(-1,0,0)),((right,0,0),(1,0,0))])
            hinge(foot,'foot_'+str(i),((left+right)/2,-.19079,0),root)
        bpy.data.objects.remove(source,do_unlink=True)
    elif brand=='openclaw':
        # SVG paths already separate the two canonical claws from the torso.
        for o in meshes:
            bpy.context.view_layer.update()
            points=[o.matrix_local@Vector(c) for c in o.bound_box]
            cx=sum(p.x for p in points)/8;w=max(p.x for p in points)-min(p.x for p in points)
            if abs(cx)>.30 and w<.30:
                hinge(o,'claw_'+str(int(cx>0)),(cx*.8,0,0),root)
    root.rotation_euler.x=math.pi/2;root.location.x=brands.index(brand)*1.5
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/terrarium/3d-residents.blend'))
for o in scene.objects:
    if o.name.startswith('resident_'):o.location.x=0
for o in scene.objects:o.select_set(True)
bpy.ops.wm.usd_export(filepath=str(ROOT/'apple/AgentDeck/Resources/Aquarium/3d-residents.usdz'),selected_objects_only=True,
    export_animation=False,triangulate_meshes=True,generate_preview_surface=True,convert_orientation=True,
    export_global_forward_selection='NEGATIVE_Z',export_global_up_selection='Y')
print('Exported canonical characters with restrained depth and original anatomy')
