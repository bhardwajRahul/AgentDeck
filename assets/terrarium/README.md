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
an optional preview sheet (iOS 18+/macOS 15+) and the selectable dashboard below.
The study fish are decorative, not actual agent sessions. Android keeps morph
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

## Dashboard selection

Apple and Android now embed the native habitat beneath their existing live HUD
when selected in Settings → Dashboard type. Selection is device-local and
persistent; it does not replace session state, connection controls or usage data.
The standalone preview entry remains a diagnostic compatibility path.

Apple now renders live agent meshes and their labels in the same RealityKit scene
as the habitat. The regular 2D renderer remains the default and a load-failure
fallback; it is no longer composited into the successful 3D path.

Stable preference IDs are `standard` and `aquarium3d`; Android also offers `paper`
on LCD. Android's existing effective panel classification still chooses the
physical-reader path and its waveform policy. Missing, unknown or unsupported
types resolve to the existing default without rewriting the stored choice.
Apple exposes 3D only on iOS 18+/macOS 15+. Settings remains reachable in 3D even
when the optional settings icon preference was previously hidden.

To add another type, extend `DashboardType` in Android `DisplayPreferences.kt`
and Apple `AppPreferences.swift`: add a stable storage ID, title/description,
capability rule and renderer routing. Keep hardware classification independent
from visual style. Preserve shared HUD/control layers, release native renderer
resources when switching, and verify restart/fallback behavior.

## Dark garden (Apple preview)

The Apple dashboard now bundles `dark-garden.blend` through the existing USDZ
resource name. The original study and Android GLB remain available unchanged.
Rebuild this candidate explicitly after exporting the original study:

```sh
blender --background --python assets/terrarium/build-dark-garden.py
blender --background --python assets/terrarium/export-living-aquarium.py -- --source dark-garden.blend --target apple
```

The deterministic authoring script retains the original swimming hierarchy and
creates asymmetrical stones, broad-leaf planting, branching wood and a winding
sand ribbon. Rooted leaf transform tracks close on the same 24-second loop and
survive the Apple export without requiring morph support. Fish body shape-key
animation remains outside the Apple compatibility claim; existing tail/yaw
transform animation remains. Native water uses the dark palette's unlit material
to avoid a reflective backdrop/horizon seam. Lighting and the HUD scrim are tuned
in the actual macOS scene, not inferred from the Blender render.

Claude's canonical silhouette gains restrained top-left shading. Codex horizontal
drift is bounded by the neighboring home spacing, preserving distinct marks for
three simultaneous processing sessions. This does not promise arbitrary-density
label packing. The native-resident section below supersedes this first hybrid composition.
Sustained GPU/power profiling remains future work. Android and physical e-ink assets were not replaced here.

## Native 3D residents (Apple)

`build-3d-residents.py` imports the six canonical SVGs from `design/brand`, reads
brand colors from `design/tokens.css` and authors beveled geometry with rounded
body parts. It writes editable `3d-residents.blend` and bundled
`apple/AgentDeck/Resources/Aquarium/3d-residents.usdz`:

```sh
blender --background --python assets/terrarium/build-3d-residents.py
```

These are stylized solid mascots, not photorealistic anatomical models. Canonical
marks remain actual geometry; no flat billboard or Canvas draws the residents.
The native template loader preserves the USD ancestor axis conversion when
cloning. Import curve point radii must be reset after unit normalization, before
extrusion, or bevel/extrusion inflates by the SVG scale factor.

`AquariumResidents.swift` projects only the canonical TerrariumState roster,
preserving Codex folding and OpenClaw presence. It reconciles arrivals/departures,
shows live activity and observed active child counts, lays residents out within
the HUD's central region, and animates modest depth/rotation at native scene
updates. Only waiting attention pulses. Native collision targets route to existing
session-focus commands; the empty-water target toggles the HUD. Reduce Motion and
scene visibility pause animation. A load failure retains the live 2D fallback.

Geometry and labels share the camera/depth buffer; information panels remain
SwiftUI for readability and accessibility. The roster remains the accessible
alternative to direct 3D picking. macOS installation/visual picking is verified;
iOS shares the implementation and compiles, but this iteration was not installed
on iPad. Android/e-ink retain their previous renderers. Fish morph deformation and
sustained frame-time/thermal profiling remain separate follow-ups.
