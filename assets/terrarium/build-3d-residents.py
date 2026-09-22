"""Canonical faces on closed, sculpted bodies with articulated appendages."""
from pathlib import Path
import bpy, bmesh, math, re
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
scene=bpy.data.scenes.new('AgentDeck 3D Residents')
bpy.context.window.scene=scene
tokens=(ROOT/'design/tokens.css').read_text()
def brand_color(name):
    value=re.search(r'--brand-'+name+r':\s*#([0-9a-fA-F]{6})',tokens).group(1)
    srgb=[int(value[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4 for c in srgb)
palette={name:brand_color('claude-code' if name=='claudecode' else name)
         for name in ['claudecode','codex','openclaw','opencode','antigravity','kiro']}
for brand,color in palette.items():
    before=set(scene.objects)
    bpy.ops.import_curve.svg(filepath=str(ROOT/'design/brand'/f'{brand}.svg'))
    parts=[o for o in scene.objects if o not in before and o.type=='CURVE']
    bpy.context.view_layer.update()
    bounds=[o.matrix_world@Vector(c) for o in parts for c in o.bound_box]
    low=Vector(tuple(min(p[i] for p in bounds) for i in range(3)))
    high=Vector(tuple(max(p[i] for p in bounds) for i in range(3)))
    center=(low+high)/2; factor=1/max(high.x-low.x,high.y-low.y)
    root=bpy.data.objects.new('resident_'+brand,None);scene.collection.objects.link(root)
    material=bpy.data.materials.new(brand+' ceramic');material.diffuse_color=(*color,1);material.use_nodes=True
    shader=material.node_tree.nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(*color,1)
    shader.inputs['Roughness'].default_value=.32;shader.inputs['Metallic'].default_value=.12
    for o in parts:
        for other in scene.objects:other.select_set(False)
        o.select_set(True);bpy.context.view_layer.objects.active=o
        o.location=(o.location-center)*factor;o.scale*=factor
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        for spline in o.data.splines:
            for point in list(spline.bezier_points) + list(spline.points): point.radius=1
        o.data.dimensions='2D';o.data.fill_mode='BOTH';o.data.extrude=.025
        o.data.bevel_depth=.018;o.data.bevel_resolution=3;o.data.resolution_u=12
        o.data.materials.clear();o.data.materials.append(material)
        bpy.ops.object.convert(target='MESH')
        o=bpy.context.object;o.parent=root
        o.location.z += .24
    if brand != 'codex':
        # The silhouette and rear casing share an outline. A convex, tapered
        # watertight shell bridges the face to the back instead of a logo slab
        # glued onto a smaller sphere with exposed horizontal ledges.
        bm=bmesh.new()
        for part in list(root.children):
            if part.type != 'MESH':continue
            for v in part.data.vertices:
                p=part.matrix_local @ v.co
                for depth,taper in [(0.19,.96),(-.12,.96),(-.37,.72)]:
                    bm.verts.new((p.x*taper,p.y*taper,depth))
        result=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
        bmesh.ops.delete(bm,geom=result['geom_interior'],context='VERTS')
        mesh=bpy.data.meshes.new(brand+' closed casing');bm.to_mesh(mesh);bm.free()
        shell=bpy.data.objects.new(brand+'_shell',mesh);scene.collection.objects.link(shell);shell.parent=root
        shell.data.materials.append(material)
        bpy.ops.object.select_all(action='DESELECT');shell.select_set(True);bpy.context.view_layer.objects.active=shell
        bevel=shell.modifiers.new('Rounded shell edges','BEVEL');bevel.width=.075;bevel.segments=5
        bpy.ops.object.modifier_apply(modifier=bevel.name)
        for polygon in shell.data.polygons:polygon.use_smooth=True
        normal=shell.modifiers.new('Continuous casing normals','WEIGHTED_NORMAL')
        bpy.ops.object.modifier_apply(modifier=normal.name)
    def form(name, position, scale, surface=material):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=1)
        body=bpy.context.object;body.name=brand+'_'+name;body.parent=root
        body.location=position;body.scale=scale;body.data.materials.append(surface)
        for polygon in body.data.polygons: polygon.use_smooth=True
        return body
    def joint(body, name, pivot):
        anchor=bpy.data.objects.new('joint_'+name,None);scene.collection.objects.link(anchor)
        anchor.parent=root;anchor.location=pivot
        body.parent=anchor;body.location-=Vector(pivot)
    # Rounded anatomy behind the canonical mark; it is geometry from every angle.
    if brand=='codex':
        lobes=[]
        for n,(x,y,r) in enumerate([(-.22,.05,.28),(0,.17,.32),(.22,.03,.27),(0,-.19,.29)]):
            lobes.append(form('cloud_lobe_'+str(n),(x,y,-.14),(r,r,.29)))
        # Fuse the cloud volumes into one closed organic back, eliminating seams.
        bpy.ops.object.select_all(action='DESELECT')
        for o in lobes:o.select_set(True)
        bpy.context.view_layer.objects.active=lobes[0];bpy.ops.object.join()
        body=bpy.context.object;body.name='codex_body'
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        remesh=body.modifiers.new('Continuous cloud skin','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.018
        bpy.ops.object.modifier_apply(modifier=remesh.name)
        smooth=body.modifiers.new('Soft cloud contours','SMOOTH');smooth.factor=1.2;smooth.iterations=5
        bpy.ops.object.modifier_apply(modifier=smooth.name)
        for poly in body.data.polygons:poly.use_smooth=True
    elif brand=='claudecode':
        form('body',(0,0,-.12),(.44,.28,.29))
        for i,x in enumerate([-.32,-.13,.13,.32]):
            joint(form('foot',(x,-.28,-.09),(.062,.14,.14)), 'foot_'+str(i), (x,-.20,-.09))
        for i,x in enumerate([-.44,.44]):
            joint(form('arm',(x,-.015,-.08),(.12,.09,.17)), 'arm_'+str(i), (x*.78,0,-.08))
    elif brand=='openclaw':
        form('body',(0,-.02,-.12),(.34,.43,.30))
        for i,x in enumerate([-.43,.43]):
            joint(form('claw',(x,-.04,-.04),(.15,.19,.19)), 'claw_'+str(i), (x*.68,-.12,-.04))
            joint(form('leg',(x*.42,-.43,-.06),(.05,.12,.13)), 'foot_'+str(i), (x*.42,-.34,-.06))
    else:
        form('body',(0,0,-.17),(.36,.38,.27))
    # A tapered dorsal ridge is embedded in the closed body, not an exposed cap.
    # It gives the reverse side a deliberate anatomy and catches grazing light.
    if brand != 'codex':
        form('dorsal_ridge',(0,-.03,-.355),(.085,.23,.09))
        for i in range(3):
            y=.14-i*.14
            form('back_segment_'+str(i),(0,y,-.355),(.25*(1-i*.12),.065,.07))
    if brand in {'opencode','antigravity','kiro','codex'}:
        for i,x in enumerate([-.35,.35]):
            joint(form('fin',(x,-.12,-.19),(.14,.065,.11)), 'fin_'+str(i), (x*.8,-.10,-.16))
    if brand in {'claudecode','openclaw'}:

        eye=bpy.data.materials.new(brand+' eyes');eye.diffuse_color=(.006,.012,.017,1)
        eye.use_nodes=True;eye.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=eye.diffuse_color
        for x in ([-.22,.22] if brand=='claudecode' else [-.13,.13]):
            joint(form('eye',(x,.13 if brand=='claudecode' else .20,.295),(.022,.03,.017),eye), 'eye_'+str(x), (x,.13 if brand=='claudecode' else .20,.295))
    if brand != 'codex':
        surfaces=[o for o in root.children if o.type=='MESH' and any(o.name.startswith(brand+'_'+part) for part in ['body','shell','dorsal','back_segment'])]
        bpy.ops.object.select_all(action='DESELECT')
        for o in surfaces:o.select_set(True)
        bpy.context.view_layer.objects.active=surfaces[0];bpy.ops.object.join()
        body=bpy.context.object;body.name=brand+'_body'
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        remesh=body.modifiers.new('Joined anatomy','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.012
        bpy.ops.object.modifier_apply(modifier=remesh.name)
        smooth=body.modifiers.new('Organic transitions','SMOOTH');smooth.factor=1.3;smooth.iterations=7
        bpy.ops.object.modifier_apply(modifier=smooth.name)
        decimate=body.modifiers.new('Runtime mesh budget','DECIMATE');decimate.ratio=.22
        bpy.ops.object.modifier_apply(modifier=decimate.name)
        for polygon in body.data.polygons:polygon.use_smooth=True
    root.rotation_euler.x=math.pi/2
    root.location.x=list(palette).index(brand)*1.5
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/terrarium/3d-residents.blend'))
# Assets share an origin at runtime, each template is cloned separately.
for o in scene.objects:
    if o.name.startswith('resident_'):o.location.x=0
for o in scene.objects:o.select_set(True)
bpy.ops.wm.usd_export(filepath=str(ROOT/'apple/AgentDeck/Resources/Aquarium/3d-residents.usdz'),
    selected_objects_only=True,export_animation=False,triangulate_meshes=True,
    generate_preview_surface=True,convert_orientation=True,
    export_global_forward_selection='NEGATIVE_Z',export_global_up_selection='Y')
print('Exported six canonical 3D resident templates')
