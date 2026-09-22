import RealityKit

/// Continuous cruising with bounded steering. Residents disturb the school;
/// separation and alignment propagate the disturbance to neighboring fish.
@available(iOS 18.0, macOS 15.0, *)
@MainActor
final class AquariumShoal {
    let root = Entity()
    private var fish: [Entity] = []
    private var tails: [Entity?] = []
    private(set) var positions: [SIMD3<Float>] = []
    private(set) var velocities: [SIMD3<Float>] = []
    private var tailRest: [simd_quatf] = []
    private var tailPhase: [Float] = []
    private var time: Float = 0

    func load(_ habitat: Entity) {
        guard fish.isEmpty else { return }
        func find(_ node: Entity) -> [Entity] {
            if node.name.lowercased().replacingOccurrences(of: "_", with: " ").hasPrefix("fish yaw") { return [node] }
            return node.children.flatMap { find($0) }
        }
        let sources = find(habitat).sorted { $0.name < $1.name }
        guard let source = sources.first else { return }
        let template = source.clone(recursive: true)
        template.stopAllAnimations(recursive: true)
        template.transform = Transform(matrix: source.transformMatrix(relativeTo: nil))
        template.position = .zero
        // Authored first fish faces +Y in Blender, -Z in the Y-up USD scene.
        template.orientation = simd_quatf(angle: -.pi / 2, axis: [0,1,0]) * template.orientation
        for source in sources { source.isEnabled = false }
        for index in 0..<14 {
            let entity = Entity()
            let model = template.clone(recursive: true)
            entity.addChild(model)
            root.addChild(entity)
            fish.append(entity)
            func tail(_ node: Entity) -> Entity? {
                if node.name.lowercased().contains("caudal") { return node }
                return node.children.compactMap { tail($0) }.first
            }
            let fin = tail(model)
            tails.append(fin)
            tailRest.append(fin?.orientation ?? simd_quatf(angle: 0, axis: [0,0,1]))
            tailPhase.append(Float(index))
            let phase = Float(index) * 0.43
            positions.append([cos(phase) * 3.2, 2.3 + sin(phase * 2) * 0.4, sin(phase) * 1.3])
            velocities.append([-sin(phase) * 0.5, 0, cos(phase) * 0.5])
            entity.position = positions[index]
        }
    }

    func step(_ delta: Double, residents: [SIMD3<Float>]) {
        let dt = Float(min(max(delta, 0), 1.0 / 20))
        time += dt
        let oldPositions = positions
        let oldVelocities = velocities
        for i in positions.indices {
            let p = oldPositions[i]
            var force = SIMD3<Float>(-p.z * 0.08, (2.4 - p.y) * 0.18, p.x * 0.035)
            // Soft aquarium bounds turn the school before it reaches the glass.
            force.x -= max(0, abs(p.x) - 3.6) * (p.x > 0 ? 0.9 : -0.9)
            force.z -= max(0, abs(p.z) - 1.8) * (p.z > 0 ? 0.9 : -0.9)
            force += SIMD3(sin(time * 0.39 + Float(i)), sin(time * 0.57 + Float(i) * 0.8) * 0.3, cos(time * 0.31 + Float(i))) * 0.08
            for j in positions.indices where i != j {
                let offset = p - oldPositions[j]
                let distance = simd_length(offset)
                if distance < 0.55 { force += offset / max(0.04, distance * distance) * 0.18 }
                else if distance < 1.4 {
                    force += (oldVelocities[j] - oldVelocities[i]) * 0.055 - offset * 0.012
                }
            }
            for resident in residents {
                let offset = p - resident
                let distance = simd_length(offset)
                if distance < 1.25 {
                    force += offset / max(0.08, distance) * (1.25 - distance) * 1.4
                }
            }
            let acceleration = simd_length(force)
            if acceleration > 0.7 { force *= 0.7 / acceleration }
            var velocity = oldVelocities[i] + force * dt
            let speed = simd_length(velocity)
            if speed > 0.001 { velocity *= min(0.8, max(0.32, speed)) / speed }
            velocities[i] = velocity
            positions[i] += velocity * dt
            fish[i].position = positions[i]
            let yaw = atan2(-velocity.z, velocity.x)
            let pitch = atan2(velocity.y, max(0.01, simd_length(SIMD2(velocity.x, velocity.z))))
            let orientation = simd_quatf(angle: yaw, axis: [0,1,0]) * simd_quatf(angle: pitch, axis: [0,0,1])
            fish[i].orientation = simd_slerp(fish[i].orientation, orientation, 1 - exp(-dt * 5))
            tailPhase[i] += dt * (8 + speed * 6)
            tails[i]?.orientation = tailRest[i] * simd_quatf(angle: sin(tailPhase[i]) * 0.22, axis: [0,0,1])
        }
    }
}
