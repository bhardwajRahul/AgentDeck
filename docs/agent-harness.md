---
id: system.agent-harness
title: Agent Harness
description: How each coding agent enters the repository, what it reads, and where skills and workflows are discovered.
category: Engineering
locale: en
canonical: true
status: stable
owner: Repository maintainers
reviewed: 2026-09-11
revision: 2026-09-11
source_of_truth: docs/agent-harness.md
validators: [pnpm design-system:check]
---
# Agent Harness — developing AgentDeck with any coding agent

This repo is built by switching between **Claude Code, Codex, OpenCode, and occasionally Antigravity**. This doc is the canonical map of the *developer-facing harness*: the instruction files, skills, workflows, and discovery surfaces that steer whichever agent is currently editing the code, so an agent can be swapped in without re-learning the project or following stale procedures.

> This is about the **meta-layer that steers the agent doing the work**, not AgentDeck's product features (which *observe* agent sessions). For the product's per-agent session-observation matrix, see [appstore-feature-matrix.md](appstore-feature-matrix.md) and [architecture.md](architecture.md).

## Tier model (read in this order)

1. **`AGENTS.md`** — the entry file every agent reads first (Codex/OpenCode/Antigravity discover it by convention; Claude Code reads `CLAUDE.md` directly). It requires `CLAUDE.md` and points back here. Kept short on purpose: Codex injects the root→cwd `AGENTS.md` chain up to the configured `project_doc_max_bytes` (32 KiB by default). Discovery also supports global guidance, `AGENTS.override.md`, and configured fallback names; see the official sources below.
2. **`CLAUDE.md`** — the shared **map** (automatic in Claude Code, explicitly read by other agents): monorepo layout, build/test, cross-cutting conventions, and the `paths → rule file` index. Reduced from 153 KB to roughly 30 KB on 2026-09-10. Keep it focused on routing and cross-cutting agreements. Tool output budgets vary by harness and invocation; inspect truncation markers and reread missing sections rather than treating a byte count as a fixed token limit.
3. **`.claude/rules/<domain>.md`** — the **SSOT for domain invariants** (APME, OpenClaw, hooks/PERM, usage, ESP32 flash, daemon lifecycle, Swift daemon, wire/devices, managed sessions, design system, App Store/release). Each file carries a `paths:` frontmatter; Claude Code loads it when a matching file is touched, every other agent reads it on demand before editing in that area. Tracked in git (the ignore file lists `.claude/*` and re-includes `.claude/rules/`). Rule bodies are moved verbatim, never paraphrased.
4. **`DEVELOPMENT_LOG.md`** — searchable recent history, and a **generated file**: every entry is one file in `docs/devlog/entries/YYYY-MM-DD-<slug>.md`, and `scripts/devlog-build.mjs` (`pnpm devlog:build`) renders the active log (current + previous month), one `docs/devlog/YYYY-MM.md` per older month, and the index; `pnpm devlog:check` fails CI when an aggregate is stale or an entry malformed. Per-entry files exist because every session used to prepend to the same lines of one file (2026-09-10 measurement: 73 writes, 35 reads, most reads conflict-marker checks). Never read it in full; check the top, then `rg` for keywords/filenames, and grep one archived month at a time.

## Supported-agents matrix

| Agent | Enters repo via | Instruction files it reads | Skill/workflow auto-discovery | Known limits in the harness |
|---|---|---|---|---|
| **Claude Code** | native `claude` (`agentdeck claude` is legacy compatibility) | `CLAUDE.md` (every session); `.claude/rules/*.md` by `paths:`; nested `esp32/CLAUDE.md` on demand | `.claude/skills/<name>` → symlink to `.agents/skills/<name>` | Skill dirs must stay symlinks, never copies |
| **Codex** (`gpt-6-astra`) | native `codex` (`agentdeck codex` is legacy compatibility) | `AGENTS.md`/override chain (root→cwd, configurable 32 KiB default) → `cat CLAUDE.md` → matching `.claude/rules/*.md` by hand; `esp32/AGENTS.md` only when cwd is under `esp32/` | `.agents/skills/` (repo-scoped); skills route to `.agents/workflows/` | No automatic `.claude/rules` glob loading (`.codex/rules` is exec policy). Check actual output truncation. Astra can pause on unclear or conflicting guidance — use canonical rules and current task authorization |
| **OpenCode** | native `opencode` (`agentdeck opencode` is legacy compatibility) | `AGENTS.md` → `CLAUDE.md` | No repo hook/skill auto-discovery | Fully supported as a product session type through the observer plugin; when authoring this repo, point it explicitly at `.agents/workflows/<name>.md` |
| **Antigravity** | manual editing, or native Antigravity CLI/app | `AGENTS.md` → `CLAUDE.md` | Instruction files only; no repo hook/skill auto-discovery | Current product session visibility is CLI-daemon passive discovery only; the App Store app shows usage/credit status, not coding-session observation |

Notes:
- **Claude Code & Codex** are the two first-class authoring agents: both get lifecycle hooks (Claude: `~/.claude/settings.json` — written by the CLI installer, or by the App Store opt-in installer against the user-selected file; Codex: `~/.codex/config.toml`) and discover skills.
- **OpenCode** is a fully supported *product session type* through its observer plugin. The legacy `agentdeck opencode` PTY + SSE overlay remains available as a replacement-gated compatibility path. OpenCode still does not auto-discover this repo's skills or hooks as an authoring tool; explicit workflow paths are the supported handoff.
- **Antigravity** reads the repo instruction chain only. AgentDeck does not install or auto-discover Antigravity hooks/skills; the App Store app reads only the user-approved usage/credit database, while coding-session creatures require optional CLI-daemon passive discovery.

## SSOT rules (where each kind of knowledge lives)

| Knowledge | Canonical home | Do **not** |
|---|---|---|
| Repository map, build/test, cross-cutting conventions, rule index | `CLAUDE.md` | re-state rules in `AGENTS.md` beyond a pointer; add a domain rule body here |
| Domain invariants (APME, OpenClaw, hooks/PERM, usage, ESP32 flash, daemon lifecycle, Swift daemon, wire/devices, managed sessions, design system, App Store/release) | `.claude/rules/<domain>.md` (tracked) | paraphrase a rule when moving it; keep a second copy in a topic doc — link the doc for measurements instead |
| Executable **skills** (deploy, diagnose, session-end, workflows index) | `.agents/skills/<name>/SKILL.md` | keep a second copy anywhere — `.claude/skills/<name>` is a symlink to it |
| Human-readable **procedures** (build, start-dev, xcode-debug, …) | `.agents/workflows/*.md` | hand-roll command sequences when a workflow exists |
| Decisions, bugfixes, hardware findings, pitfalls | `docs/devlog/entries/YYYY-MM-DD-<slug>.md` (one file per entry; `DEVELOPMENT_LOG.md` is generated from them) | dump everything into `CLAUDE.md`; edit the generated aggregate |

### Skills are single-source

Canonical skills live under **`.agents/skills/<name>/SKILL.md`** (agent-agnostic, committed to git) and nowhere else. Codex scans `.agents/skills/` natively. Claude Code scans `.claude/skills/<name>/SKILL.md`, so each `.claude/skills/<name>` is a **tracked symlink** to the canonical directory (`.claude/*` is gitignored except `rules/` and `skills/`). The flat pointer files that used to sit there were never discovered — Claude Code 2.1.x requires the `<name>/SKILL.md` shape — and were gitignored besides, so a fresh clone had no `/agentdeck-deploy` at all; a `claude -p` probe on 2026-09-10 confirmed all five symlinked skills are listed. When a procedure changes, edit only the `.agents/skills/` copy. Current skills:

- `agentdeck-deploy` — build/install/launch across Android, Apple, ESP32, Stream Deck, daemon
- `sdc-diagnose` — Stream Deck/PTY sync, cursor, hook-ingestion, and state-machine diagnostics
- `session-end` — cross-agent handoff (below)
- `agentdeck-workflows` — index/router into `.agents/workflows/`
- `esp32-heap-discipline` — firmware allocation decisions and board memory constraints

## Handoff between agents

Before `/clear`, `/new`, switching tasks, or handing work to a different agent, run the **`session-end`** skill (`.agents/skills/session-end/SKILL.md`). It writes a concise handoff (goal, current outcome, changed files, verification, blockers, next action) and updates durable docs only when warranted — separating temporary handoff notes from `CLAUDE.md` / `DEVELOPMENT_LOG.md` / `AGENTS.md`.

## Memory and verification

[CLAUDE.md — Agent working agreements](../CLAUDE.md#agent-working-agreements) owns instruction priority, authorization, memory freshness, and shared-worktree rules. Agent-local memories are optional retrieval aids; essential project rules must remain discoverable from a fresh clone. Store current rules once, link memories to the owner, and label past release or approval conditions as dated evidence.

[CLAUDE.md — Verification scope](../CLAUDE.md#verification-scope) defines local checks by change type. CI and release workflows retain their own gates.

## Official Codex and Astra guidance

- [Codex instruction discovery](https://learn.chatgpt.com/docs/agent-configuration/agents-md): global/project discovery, overrides, fallback filenames, and configurable size limits.
- [GPT-6 Astra guidance](https://developers.openai.com/api/docs/guides/latest-model): clarify instruction priority and task authorization; calibrate verification to the change. Model behavior is guidance, not a guarantee that every conflict causes a pause.
