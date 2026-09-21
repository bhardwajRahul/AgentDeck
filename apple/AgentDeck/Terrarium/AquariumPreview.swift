import SwiftUI
import RealityKit

/// An opt-in native model trial; the existing live dashboard remains the default.
struct AquariumPreview: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack(alignment: .topTrailing) {
            if #available(iOS 18.0, macOS 15.0, *) {
                LivingAquariumScene()
            } else {
                Text("The 3D aquarium requires iOS 18 or later.")
                    .padding()
            }
            Button("Back to dashboard") { dismiss() }
                .buttonStyle(.borderedProminent)
                .padding()
        }
        .frame(minWidth: 320, minHeight: 300)
    }
}

@available(iOS 18.0, macOS 15.0, *)
struct LivingAquariumScene: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.scenePhase) private var scenePhase
    @State private var controllers: [AnimationPlaybackController] = []
    @State private var failure: String?

    var body: some View {
        RealityView { content in
            content.camera = .virtual
            let camera = PerspectiveCamera()
            camera.camera.fieldOfViewInDegrees = 38
            camera.look(at: [0, 1.65, -0.7], from: [0, 4.8, 14], relativeTo: nil)
            content.add(camera)
            let sun = DirectionalLight()
            sun.light.intensity = 5_000
            sun.look(at: [0, 0, 0], from: [-4, 8, 5], relativeTo: nil)
            content.add(sun)
            let fill = DirectionalLight()
            fill.light.intensity = 900
            fill.look(at: [0, 1, 0], from: [4, 4, -4], relativeTo: nil)
            content.add(fill)
            do {
                guard let url = Bundle.main.url(forResource: "living-aquarium", withExtension: "usdz") else {
                    throw CocoaError(.fileNoSuchFile)
                }
                let root = try await Entity(contentsOf: url)
                applyWaterMaterial(to: root)
                content.add(root)
                // USDZ exposes the same tracks through global and per-node libraries.
                // Playing all of them overlays competing transforms; use one scene clip.
                var playback: [AnimationPlaybackController] = []
                if let animation = root.availableAnimations.first {
                    playback.append(root.playAnimation(animation.repeat(), startsPaused: true))
                }
                controllers = playback
            } catch {
                failure = "The 3D aquarium could not be opened. Your dashboard is still available."
            }
        }
        .overlay {
            if let failure { Text(failure).padding().background(.regularMaterial) }
        }
        // The async loader captures the initial environment. Reconcile playback
        // in the refreshed view so a background → active transition during load
        // cannot leave the newly-created controller paused forever.
        .onChange(of: controllers.count) { _, _ in updatePlayback() }
        .onChange(of: reduceMotion) { _, _ in updatePlayback() }
        .onChange(of: scenePhase) { _, _ in updatePlayback() }
        .onDisappear {
            for controller in controllers { controller.stop() }
            controllers.removeAll()
        }
    }

    /// The water enclosure should recede, not reflect the key light like a wall.
    /// A token-bound unlit material also avoids a bright horizon across imports.
    private func applyWaterMaterial(to entity: Entity) {
        if entity.name.lowercased().contains("garden") && entity.name.lowercased().contains("water"),
           var model = entity.components[ModelComponent.self] {
            #if os(macOS)
            let color = NSColor(TerrariumColors.deepSea)
            #else
            let color = UIColor(TerrariumColors.deepSea)
            #endif
            model.materials = [UnlitMaterial(color: color)]
            entity.components.set(model)
        }
        for child in entity.children { applyWaterMaterial(to: child) }
    }

    private func updatePlayback() {
        for controller in controllers {
            if reduceMotion || scenePhase != .active { controller.pause() }
            else { controller.resume() }
        }
    }
}
