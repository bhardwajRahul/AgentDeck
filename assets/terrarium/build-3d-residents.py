"""Extrude canonical upstream SVG silhouettes into rounded native 3D residents."""
from pathlib import Path
import bpy, math, re
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
        o.data.dimensions='2D';o.data.fill_mode='BOTH';o.data.extrude=.105
        o.data.bevel_depth=.018;o.data.bevel_resolution=3;o.data.resolution_u=12
        o.data.materials.clear();o.data.materials.append(material)
        bpy.ops.object.convert(target='MESH')
        o=bpy.context.object;o.parent=root
        o.location.z += .17
    def form(name, position, scale, surface=material):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=1)
        body=bpy.context.object;body.name=brand+'_'+name;body.parent=root
        body.location=position;body.scale=scale;body.data.materials.append(surface)
        for polygon in body.data.polygons: polygon.use_smooth=True
        return body
    # Rounded anatomy behind the canonical mark; it is geometry from every angle.
    if brand=='codex':
        for n,(x,y,r) in enumerate([(-.22,.05,.28),(0,.17,.32),(.22,.03,.27),(0,-.19,.29)]):
            form('cloud_lobe_'+str(n),(x,y,-.14),(r,r,.29))
    elif brand=='claudecode':
        form('body',(0,0,-.12),(.44,.28,.29))
        for x in [-.32,-.13,.13,.32]:form('foot',(x,-.28,-.09),(.062,.14,.14))
        for x in [-.44,.44]:form('arm',(x,-.015,-.08),(.12,.09,.17))
    elif brand=='openclaw':
        form('body',(0,-.02,-.12),(.34,.43,.30))
        for x in [-.43,.43]:
            form('claw',(x,-.04,-.04),(.15,.19,.19))
            form('leg',(x*.42,-.43,-.06),(.05,.12,.13))
    else:
        form('body',(0,0,-.17),(.36,.38,.27))
    if brand in {'claudecode','openclaw'}:
        eye=bpy.data.materials.new(brand+' eyes');eye.diffuse_color=(.006,.012,.017,1)
        eye.use_nodes=True;eye.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=eye.diffuse_color
        for x in ([-.22,.22] if brand=='claudecode' else [-.13,.13]):
            form('eye',(x,.13 if brand=='claudecode' else .20,.295),(.022,.03,.017),eye)
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
