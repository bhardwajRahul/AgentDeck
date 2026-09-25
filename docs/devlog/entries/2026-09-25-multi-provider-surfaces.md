# 2026-09-25 — Multi-provider usage surfaces

## Changes

- Stream Deck Classic/XL use their physical bottom-row capacity, with paging only on overflow. Five Claude/Codex/z.ai readings fit Classic together.
- Both SD+ usage LCDs allow explicit provider selection. The selected dial wins a collision; the peer moves to another provider. E2 can return to automatic activity selection with a hold. Provider preferences persist in Stream Deck settings.
- Pixoo's Node and Swift renderers show all three live providers and reserve the corresponding creature-safe area. Seven-day-only Claude readings remain visible.
- TC001 keeps the agent page and rotates live provider usage pages. Official provider marks accompany window labels and percentages; the activity dot stays present on usage pages. Window lengths come from the wire rather than primary/secondary assumptions; MCP retains its quantity label. Rendering uses bounded stack buffers, without new heap allocation. The host simulator uses the same firmware renderer.
- The connected tablet's daemon preference omitted Claude although fresh quota was arriving. Re-enabled Claude through `/dashboard/providers`, preserving the other displayed providers. No Android executable change was needed; an ADB screen capture confirmed all three provider rows and live percentages on the connected Lenovo tablet.

## Validation and delivery scope

- Node build/typecheck and all 307 Vitest files passed: 4,702 tests passed, two skipped.
- macOS Debug build succeeded. TC001 target firmware and host simulator built; provider frames were inspected.
- Protocol regeneration was clean; docs/catalog/token sync checks passed. Design lint retains the existing 89 source findings plus three generated-bundle findings in the built checkout.
- Release remains held. iOS installation is explicitly skipped. Source validation is separate from installation; no store submission, tag, or package publication was performed.
