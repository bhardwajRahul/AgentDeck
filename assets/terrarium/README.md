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

## Living aquarium study and TRMNL gallery

`living-aquarium.blend` is a separate Blender MCP-authored native 3D study. It
contains seven fish, a 24-second swimming circuit, tail motion, body deformation,
ribbon plants and branching driftwood. No third-party models or textures are used.

```sh
blender --background --python assets/terrarium/export-living-aquarium.py
blender --background --python assets/terrarium/export-paper-aquarium.py
```

The first command writes Android's bundled GLB and Apple's bundled USDZ. Android
uses Filament in an optional LCD-only preview activity; Apple uses RealityKit in
an optional preview sheet (iOS 18+/macOS 15+). Neither replaces the live dashboard
or maps these study fish to actual agent sessions yet. Android keeps morph
channels; Apple currently uses triangulated geometry and the global transform
clip. Do not play the USDZ's overlapping per-node animation libraries together.

The paper export writes `paper-aquarium.png` and a 38,400-byte flash-resident C++
bitmap. It uses a dedicated monochrome material pass and fixed spatial dithering,
not temporal dithering. TRMNL KEY1 selects the gallery; KEY2 returns home. The
image stays fixed while routine footer updates coalesce for 60 seconds. Urgent
attention bypasses that interval; the existing periodic panel cleanup remains.

This is a renderer/composition prototype. Art direction, agent-state integration,
sustained frame-time/power measurements and final device approval remain release
gates. The universal Android APK grows substantially with the native renderer.
