# Android aquarium habitat

`aquarium-habitat.blend` is the authored Blender 5.2 source for the static habitat.
It was made locally with Blender MCP; no downloaded models or textures are used.
The retained fish study objects are hidden from rendering: live residents, agent
marks, labels and usage data belong to the Android runtime.

Regenerate the Android image from the repository root:

```sh
blender --background --python assets/terrarium/export-habitat.py
```

The 1280×768 PNG lives in Android `drawable-nodpi`. The e-ink layout on LCD and physical readers uses this cached habitat,
desaturated once for monochrome layout, with lightly shaded canonical creature
paths. Physical monochrome readers lift and quantize the background to fixed
16-level gray once at load time. Runtime edge plants and a small grazing snail
add local motion; bubbles are limited to LCD/color panels.
The full-color Android dashboard and other device dashboards are not changed.

Review exported images at the actual dashboard aspect ratio and creature size.
Do not bake UI labels, user data or animated residents into this background.
