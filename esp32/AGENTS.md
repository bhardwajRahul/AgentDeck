# ESP32 — agent entry (Codex / OpenCode / Antigravity)

Codex loads this file only when its working directory is under `esp32/` (root → cwd `AGENTS.md` chain). Read, in order:

1. [../CLAUDE.md](../CLAUDE.md) — repository map and cross-cutting conventions.
2. [CLAUDE.md](CLAUDE.md) — provisioning, WiFi OTA scope, the external-client wire contract, Autonomous Pocket. Claude Code loads this one automatically when it reads files here.
3. [../.claude/rules/esp32-flash.md](../.claude/rules/esp32-flash.md) — board map, merged factory image, flash preflight refusal, post-write reset, serial-suspend lease.
4. The `esp32-heap-discipline` skill (`.agents/skills/esp32-heap-discipline/SKILL.md`) before writing anything that allocates.

Do not restate rules here; add them to the files above.
