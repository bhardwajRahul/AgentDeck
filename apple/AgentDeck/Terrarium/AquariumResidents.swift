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
    private var targets: [String: SIMD3<Float>] = [:]
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
        }
        let columns = min(3, max(1, next.count))
        let rows = max(1, (next.count + columns - 1) / columns)
        let width = min(6.2, max(1.6, 5.5 * aspect))
        size = min(0.95, width / Float(columns) * 0.48, 2.9 / Float(rows) * 0.54)
        for (index, item) in next.enumerated() {
            let row = index / columns
            let rowCount = min(columns, next.count - row * columns)
            let x = (Float(index % columns) - Float(rowCount - 1) / 2) * width / Float(columns)
            let y: Float = rows == 1 ? 2.7 : 3.7 - Float(row) * 1.9 / Float(max(1, rows - 1))
            targets[item.id] = [x, y, 1.0]
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
                focus.position.z = -0.17
                resident.addChild(focus)
                resident.position = targets[item.id]!
                root.addChild(resident)
                residents[item.id] = resident
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
        for (index, item) in descriptors.enumerated() {
            guard let entity = residents[item.id], var target = targets[item.id] else { continue }
            let phase = Float(time) * (item.activity == .working ? 0.85 : 0.4) + Float(index) * 1.73
            target.y += sin(phase) * 0.07
            target.z += cos(phase * 0.7) * 0.09
            let blend = Float(1 - exp(-dt * 4))
            entity.position += (target - entity.position) * blend
            if let body = entity.findEntity(named: "body") {
                // All geometry receives the scene light and genuine perspective.
                let orientation = simd_quatf(angle: sin(phase * 0.7) * 0.22, axis: [0,1,0])
                    * simd_quatf(angle: sin(phase) * 0.045, axis: [0,0,1])
                body.orientation = simd_slerp(body.orientation, orientation, blend)
            }
            // Only awaiting attention pulses; other status colors stay steady.
            if let label = entity.findEntity(named: "label") {
                let pulse: Float = item.activity == .waiting ? 1 + sin(Float(time) * 2.5) * 0.035 : 1
                label.scale = .init(repeating: pulse)
            }
        }
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
            label.position = [-bounds.center.x, index == 0 ? 0.83 : 0.64, 0.08]
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
