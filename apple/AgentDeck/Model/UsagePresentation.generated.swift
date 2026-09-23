// GENERATED from shared/src/usage-presentation.ts. DO NOT EDIT.
enum UsagePresentation {
    static let heading = "USAGE"
    static func lunaActive(_ primary: Double, _ secondary: Double, _ reserve: Double) -> Bool { return reserve >= 0 && (primary >= 100 || secondary >= 100) }
}
