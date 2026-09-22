import SwiftUI
import RealityKit

/// A presentation projection of canonical live residents, never inferred providers.
struct AquariumResident: Equatable {
    enum Activity: String { case idle = "IDLE", working = "WORKING", waiting = "WAITING", error = "ERROR" }
    let id: String
    let kind: String
    let title: String
    let activity: Activity
    var helpers: Int = 0

    static func project(_ state: TerrariumState) -> [Self] {
        var items: [Self] = state.creatures.map {
            Self(id: $0.id, kind: "claudecode", title: $0.projectName ?? "Claude", activity: $0.state == .asking ? .waiting : $0.state == .working ? .working : .idle, helpers: $0.subagentActivity.activeCount)
        }
        items += state.cloudCreatures.filter { $0.state != .dormant }.map {
            Self(id: $0.id, kind: "codex", title: ($0.projectName ?? "Codex") + ($0.groupSize > 1 ? " ×\($0.groupSize)" : ""), activity: $0.state == .waiting ? .waiting : $0.state == .pulsing ? .working : .idle, helpers: $0.subagentActivity.activeCount)
        }
        items += state.opencodeCreatures.filter { $0.state != .dormant }.map {
            Self(id: $0.id, kind: "opencode", title: $0.projectName ?? "OpenCode", activity: $0.state == .waiting ? .waiting : $0.state == .pulsing ? .working : .idle, helpers: $0.subagentActivity.activeCount)
        }
        items += state.antigravityCreatures.map {
            Self(id: $0.id, kind: "antigravity", title: $0.projectName ?? "Antigravity", activity: $0.state == .asking ? .waiting : $0.state == .working ? .working : .idle, helpers: $0.subagentActivity.activeCount)
        }
        items += state.kiroCreatures.map {
            Self(id: $0.id, kind: "kiro", title: $0.projectName ?? "Kiro", activity: $0.state == .asking ? .waiting : $0.state == .working ? .working : .idle, helpers: $0.subagentActivity.activeCount)
        }
        if state.crayfishVisible {
            items.append(Self(id: "crayfish", kind: "openclaw", title: "OpenClaw", activity: state.crayfishState == .sick ? .error : state.crayfishState == .waiting ? .waiting : state.crayfishState == .routing ? .working : .idle))
        }
        return items.sorted { $0.id < $1.id }
    }
}

@available(iOS 18.0, macOS 15.0, *)
@MainActor
final class AquariumResidents {
    let root = Entity()
    private var templates: [String: Entity] = [:]
    private(set) var residents: [String: Entity] = [:]
    private var descriptors: [AquariumResident] = []
    private var slotOrder: [String] = []
    private var targets: [String: SIMD3<Float>] = [:]
    private struct Motion {
        var phase: Float
        var effort: Float = 0
        var attention: Float = 0
        var fatigue: Float = 0
    }
    private var motions: [String: Motion] = [:]
    private var joints: [String: [Entity]] = [:]
    let shoal = AquariumShoal()
    private var time: Double = 0
    private var size: Float = 0.85
    var animate = true

    func loadTemplates(_ library: Entity) {
        for kind in ["claudecode", "codex", "openclaw", "opencode", "antigravity", "kiro"] {
            if let imported = library.findEntity(named: "resident_" + kind) {
                let template = imported.clone(recursive: true)
                // USD stores Z-up → Y-up on an ancestor. Preserve that transform
                // when extracting a template, otherwise its face lies flat.
                template.transform = Transform(matrix: imported.transformMatrix(relativeTo: nil))
                templates[kind] = template
            }
        }
    }

    var templateCount: Int { templates.count }

    func sync(_ state: TerrariumState, aspect: Float) {
        let next = AquariumResident.project(state)
        let ids = Set(next.map(\.id))
        for id in Array(residents.keys) where !ids.contains(id) {
            residents.removeValue(forKey: id)?.removeFromParent()
            targets.removeValue(forKey: id)
            motions.removeValue(forKey: id)
            joints.removeValue(forKey: id)
        }
        let layout = Self.layout(count: next.count, aspect: aspect)
        size = layout.size
        // Existing residents retain their relative order when sessions arrive or depart.
        let existing = slotOrder.filter(ids.contains)
        slotOrder = existing + next.map(\.id).filter { !existing.contains($0) }
        for item in next {
            let index = slotOrder.firstIndex(of: item.id) ?? 0
            targets[item.id] = layout.positions[index]
            if residents[item.id] == nil, let template = templates[item.kind] {
                let resident = Entity()
                resident.name = "session|" + item.id
                let body = Entity()
                body.addChild(template.clone(recursive: true))
                body.name = "body"
                resident.addChild(body)
                body.generateCollisionShapes(recursive: true)
                resident.components.set(InputTargetComponent())
                let focus = ModelEntity(mesh: .generateSphere(radius: 0.63), materials: [UnlitMaterial(color: nativeColor(TerrariumColors.tetraNeon).withAlphaComponent(0.16))])
                focus.name = "focus"
                focus.scale = [1, 1, 0.05]
                focus.position.z = -0.65
                resident.addChild(focus)
                resident.position = targets[item.id]!
                root.addChild(resident)
                residents[item.id] = resident
                motions[item.id] = Motion(phase: Self.seed(item.id) * 6.28)
                func collect(_ node: Entity) -> [Entity] {
                    (node.name.hasPrefix("joint_") ? [node] : []) + node.children.flatMap { collect($0) }
                }
                joints[item.id] = collect(body)
            }
            guard let resident = residents[item.id] else { continue }
            resident.scale = .init(repeating: size)
            if !animate { resident.position = targets[item.id]! }
            if resident.findEntity(named: "label") == nil || descriptors.first(where: { $0.id == item.id }) != item {
                resident.findEntity(named: "label")?.removeFromParent()
                let label = makeLabel(String(item.title.prefix(22)), activity: item.activity, helpers: item.helpers)
                resident.addChild(label)
            }
            resident.findEntity(named: "focus")?.isEnabled = state.focusedSessionId == item.id || (item.id == "crayfish" && state.focusedSessionId == "openclaw-gateway")
        }
        descriptors = next
    }

    func step(_ delta: Double) {
        guard animate else { return }
        // Bound integration after occlusion/sleep; no wall-clock jump on resume.
        let dt = min(max(delta, 0), 1.0 / 20)
        time += dt
        let blend = Float(1 - exp(-dt * 3))
        for item in descriptors {
            guard let entity = residents[item.id], var target = targets[item.id], var motion = motions[item.id] else { continue }
            motion.effort += ((item.activity == .working ? 1 : 0) - motion.effort) * blend
            motion.attention += ((item.activity == .waiting ? 1 : 0) - motion.attention) * blend
            motion.fatigue += ((item.activity == .error ? 1 : 0) - motion.fatigue) * blend
            // Integrate phase rather than multiplying time by a state-dependent rate.
            // State transitions and changes in the roster must never snap a pose.
            motion.phase += Float(dt) * (0.65 + motion.effort * 1.7)
            let phase = motion.phase
            motions[item.id] = motion
            target.x += sin(phase * 0.37) * size * 0.10
            target.y += (sin(phase) * 0.08 + motion.attention * 0.10 - motion.fatigue * 0.13) * size
            target.z += cos(phase * 0.7) * 0.16
            entity.position += (target - entity.position) * blend
            if let body = entity.findEntity(named: "body") {
                let orientation = simd_quatf(angle: sin(phase * 0.43) * 0.32, axis: [0,1,0])
                    * simd_quatf(angle: motion.fatigue * 0.24 - motion.attention * 0.08, axis: [1,0,0])
                    * simd_quatf(angle: sin(phase) * (0.035 + motion.effort * 0.025), axis: [0,0,1])
                body.orientation = simd_slerp(body.orientation, orientation, blend)
                let breath = sin(phase * 1.3) * 0.018
                body.scale = [1 + breath, 1 - breath * 0.6, 1 + breath]
            }
            for (index, joint) in (joints[item.id] ?? []).enumerated() {
                if joint.name.hasPrefix("joint_eye") {
                    let blink = pow(max(0, cos(phase * 0.62)), 80)
                    joint.scale.y = 1 - blink * 0.90
                } else {
                    let side: Float = joint.name.contains("_0") ? -1 : 1
                    let wave = sin(phase * 2 + Float(index) * 1.8)
                    let lift = motion.attention * 0.48 - motion.fatigue * 0.35
                    joint.orientation = simd_quatf(angle: side * (lift + wave * (0.08 + motion.effort * 0.28)), axis: [0,0,1])
                }
            }
            // Only awaiting attention pulses; other status colors stay steady.
            if let label = entity.findEntity(named: "label") {
                let pulse: Float = item.activity == .waiting ? 1 + sin(Float(time) * 2.5) * 0.035 : 1
                label.scale = .init(repeating: pulse)
            }
        }
        shoal.step(dt, residents: residents.values.map { $0.position })
    }

    /// Slots are separated in camera projection, then unprojected to depth tiers.
    /// Merely changing world Z causes distant rows to overlap in screen space.
    static func layout(count: Int, aspect: Float) -> (positions: [SIMD3<Float>], size: Float) {
        guard count > 0 else { return ([], 0.85) }
        let width = min(6.2, max(1.6, 5.5 * aspect))
        let columns = min(count, max(1, Int(ceil(sqrt(Float(count) * width / 3.5)))))
        let rows = (count + columns - 1) / columns
        let size = min(0.95, width / Float(columns) * 0.43, 2.9 / Float(rows) * 0.46)
        let positions = (0..<count).map { index -> SIMD3<Float> in
            let row = index / columns
            let rowCount = min(columns, count - row * columns)
            let x = (Float(index % columns) - Float(rowCount - 1) / 2) * width / Float(columns)
            let y: Float = rows == 1 ? 2.7 : 3.8 - Float(row) * 2.1 / Float(rows - 1)
            let z = Float((index + row) % 3) * 0.65 - 0.30
            let perspective = (14 - z) / 13
            return [x * perspective, 4.8 + (y - 4.8) * perspective, z]
        }
        return (positions, size)
    }

    private static func seed(_ id: String) -> Float {
        let hash = id.utf8.reduce(UInt32(2166136261)) { ($0 ^ UInt32($1)) &* 16777619 }
        return Float(hash % 10000) / 10000
    }

    static func sessionID(for entity: Entity) -> String? {
        var current: Entity? = entity
        while let node = current {
            if node.name.hasPrefix("session|") { return String(node.name.dropFirst(8)) }
            current = node.parent
        }
        return nil
    }

    private func makeLabel(_ title: String, activity: AquariumResident.Activity, helpers: Int) -> Entity {
        let group = Entity()
        group.name = "label"
        let color: Color = switch activity {
        case .waiting: DesignTokens.Status.awaiting
        case .working: DesignTokens.Status.processing
        case .error: DesignTokens.Status.error
        case .idle: DesignTokens.Status.idle
        }
        for (index, text) in [title, activity.rawValue + (helpers > 0 ? " · \(helpers) agents" : "")].enumerated() {
            let mesh = MeshResource.generateText(text, extrusionDepth: 0.002, font: .systemFont(ofSize: index == 0 ? 0.16 : 0.105))
            let label = ModelEntity(mesh: mesh, materials: [UnlitMaterial(color: nativeColor(index == 0 ? TerrariumColors.hudText : color))])
            let bounds = label.visualBounds(relativeTo: label)
            let fit = min(1, 1.9 / max(0.01, bounds.extents.x))
            label.scale = .init(repeating: fit)
            label.position = [-bounds.center.x * fit, index == 0 ? 0.83 : 0.64, 0.40]
            group.addChild(label)
        }
        return group
    }

    #if os(macOS)
    private func nativeColor(_ color: Color) -> NSColor { NSColor(color) }
    #else
    private func nativeColor(_ color: Color) -> UIColor { UIColor(color) }
    #endif
}
