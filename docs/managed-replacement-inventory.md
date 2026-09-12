# Managed-only capability inventory: launch arguments and terminal affordances

Working measurement for [#273](https://github.com/puritysb/AgentDeck/issues/273), gates
**Custom launch arguments** and **Terminal-only gaps**. Measured against `3f7dc473`.

The other two gates (remote attach, session ordering) are not covered here. Both name a
real-topology or real-user validation as their completion condition, so neither can be
closed from a source reading.

Every row below is either **measured** (a cited source line) or **open** (named as
unmeasured). Nothing here decides a replacement design; it establishes what a replacement
would have to reproduce.

---

## 1. Custom launch arguments

### 1.1 The contract as implemented

| Knob | Where | Exact semantics |
|---|---|---|
| `-c, --command <cmd>` | `bridge/src/cli.ts:859` (claude), `:905` (codex), `:964` (opencode) | Default `claude`/`codex`/`opencode`. Free-form **string**, not an argv array. `monitor` has no `-c` — it spawns no agent. |
| `AGENTDECK_COMMANDER_ARGS` | `applyGlobalEnvArgs`, `cli.ts:705` | Tokenized, spliced into argv at index 3 — immediately after the subcommand. Keyed on `argv[2]` being one of `claude`/`codex`/`opencode`/`monitor`; any other command is a true no-op even when a positional *value* equals a session word. |
| `AGENTDECK_CLAUDE_ARGS` / `AGENTDECK_CODEX_ARGS` / `AGENTDECK_OPENCODE_ARGS` | `weaveAgentCommand`, `cli.ts:741` | Raw string append to the `-c` command (`` `${command} ${extra}`.trim() ``). Weaves onto a user `-c` rather than replacing it. |
| `--no-env-args` | `cli.ts:870`, consumed at `:879` via `opts.envArgs !== false` | Disables **both** layers for the invocation. The commander half is decided pre-parse on raw argv (`cli.ts:729`); the per-agent half in the action. |
| `--weight <n>` | `cli.ts:871` | Not an agent argument, but rides the same env-splice layer — `AGENTDECK_COMMANDER_ARGS="--weight 5"` is a tested path (`cli.test.ts:342`). |

### 1.2 The load-bearing fact: the command string is shell-interpreted

`PtyManager.spawn` (`bridge/src/pty-manager.ts:98-103`) does not exec the command. It execs a shell:

- POSIX: `(process.env.SHELL || '/bin/bash')` with `['-l', '-c', command]`
- Windows: `(process.env.COMSPEC || 'cmd.exe')` with `['/d', '/s', '/c', command]`

Three consequences a replacement inherits, none of them optional:

1. **`-l` is a login shell.** The user's profile is sourced before the agent starts, so
   `PATH`, version managers (nvm/rbenv/mise), and profile-exported credentials are in
   scope. A daemon-first launcher that spawns the agent binary directly reproduces the
   flags but not this environment.
2. **Full shell grammar is in play.** The `-c` string may contain expansion, quoting,
   pipes, `&&`. The per-agent env append is documented as riding this same path
   deliberately (`.claude/rules/managed-sessions.md`).
3. **Two different grammars.** `cmd.exe /d /s /c` quoting is not POSIX quoting, and the
   same `AGENTDECK_CLAUDE_ARGS` value is appended verbatim on both.

By contrast `AGENTDECK_COMMANDER_ARGS` is tokenized in pure JS by `tokenizeArgString`
(`cli.ts:668`) — quote grouping only, **no** expansion, globbing, or escapes. So the two
env layers have deliberately different power, and the rule file says so.

### 1.3 What "resume-command composition" turned out to be

The gate wording implies an AgentDeck-owned resume builder. There is none. A repository
grep for `--resume` finds only: the user's own `-c "claude --resume X"` in docs and tests
(`docs/cli.md:73,90`, `cli.test.ts:200,262`), and Kiro's unrelated `--resume-id` parsing
in `passive-observer.ts:1020,1085`.

So the contract is narrower and easier than the gate suggests: **resume is user-supplied
text, and AgentDeck's only obligation is that the env append does not clobber it.** That
obligation is covered (`cli.test.ts:262`).

### 1.4 Existing regression coverage

`bridge/src/__tests__/cli.test.ts:165-450` already pins: splice position, the `argv[2]`
gate, non-session no-op, empty/unset var, the typed hatch, the **env-smuggled
`--no-env-args`** strip (`:243`), per-agent selection by agent type, whitespace-only
values, last-write scalar override through a real commander parse (`:322`), and both-layer
disable through `parseAsync` (`:429`).

The gate's own ask — *"add regression fixtures from representative
configurations"* — was missing the contract's load-bearing half. Added in this branch:

- `bridge/src/__tests__/pty-manager-launch-contract.test.ts` — POSIX login shell
  (`-l -c`) and its `/bin/bash` fallback, win32 `cmd.exe /d /s /c` and its `COMSPEC`
  fallback, and the command string reaching the shell verbatim. It mocks `node-pty` and
  drives the real `spawn()`, so it covers the combinator, not a pure helper that a later
  edit to `spawn()` could bypass while staying green.
- `cli.test.ts` weave cases — user quoting, `&&`, a Windows path, and a quoted env value
  appended raw rather than tokenized.

Each was mutation-checked rather than trusted for passing: dropping `-l`, JSON-escaping
the command, and quoting the woven value each turn the new cases red.

Still untested, and not reachable from a unit test: that the login shell's **environment**
actually reaches the agent. The tests pin that `-l` is passed, not what sourcing the
profile produces. Closing that needs a real spawn in a controlled profile.

### 1.5 Replacement assessment

| Candidate | Verdict |
|---|---|
| Agent-native config (`~/.claude/settings.json` etc.) | Cannot express per-invocation values, and the vars exist precisely to vary per terminal tab. Fails scalar override. |
| Argument profiles in `daemon.json` | Expressible, but moves the value out of the shell profile, which is where a per-machine `PATH`-dependent value naturally lives. |
| Lightweight non-PTY launch path (`spawn` the same shell, no PTY ownership) | The only candidate that preserves §1.2 in full. AgentDeck would still compose and hand off the command string, but would not own the terminal. **Unmeasured:** whether a handed-off shell process can be hook-attributed to the resulting observed session. |

---

## 2. Terminal-only affordances

### 2.1 Everything `OutputParser` produces, and where it goes

Seventeen distinct events (`bridge/src/output-parser.ts`). The Claude adapter forwards
twelve (`adapters/claude-code.ts:32-70`) under a comment that states the boundary
directly: *"Never add turn lifecycle or tool events here: hooks own state, timeline, and
APME correctness."*

| Parser event | Forwarded as | Observed (hook) equivalent | Verdict |
|---|---|---|---|
| `permission_prompt` | `terminal_ui` | Held `PreToolUse` gate (`observed-steering.ts`) | Partial — different semantics, see §2.2 |
| `option_prompt` | `terminal_ui` | AskUserQuestion gate + terminal injection | Partial |
| `diff_prompt` | `terminal_ui` | the underlying decision yes, the diff no — §2.5 | Partial |
| `status_line` | `terminal_ui` → `usageTracker.setDuration/setOutputTokens` (`state-machine.ts:381`) | tokens yes (§2.3), duration no | Partial |
| `project_name` | `terminal_ui` | bridge-resolved, git-aware; parser scrape is the fallback (`claude-code.ts:98`) | Covered |
| `model_info` | `terminal_ui` | transcript `message.model` (`passive-observer.ts:324`) | Covered |
| `mode_change` | `terminal_ui` | none found | **Managed-only** |
| `suggested_prompt` | `terminal_ui` | none found | **Managed-only** |
| `remote_url` | `terminal_ui` | none found | Managed-only (low value) |
| `cursor_update` | `metadata` | none — no cursor exists to track | **Managed-only by construction** |
| `usage_info` | `metadata` | daemon usage/quota clients (`usage-*.ts`) | Covered by a better source |
| `user_prompt` | `metadata` | `UserPromptSubmit` hook / transcript | Covered |
| `spinner_start` | *not forwarded* | hooks | lifecycle — hook-owned |
| `spinner_stop` | *not forwarded* | hooks | lifecycle — hook-owned |
| `idle` | *not forwarded* | hooks | lifecycle — hook-owned |
| `tool_action` | *not forwarded* | `PreToolUse` | hook-owned |
| `effort_level` | *not forwarded* | — | unused |

Note the five unforwarded events still reach `StateMachine.handleParserEvent` in the
**session-bridge** path (`index.ts:665`), where `spinner_start` transitions to `PROCESSING`
with source `'pty'` and `idle` to `IDLE` (`state-machine.ts:312-364`). That is terminal
parsing authoring lifecycle, and it is exactly what principle 2 of #273 forbids restoring.
It is confined to the managed path; the daemon's other caller of `handleParserEvent`
(`daemon-server.ts:4966`) is the **OpenClaw Gateway** adapter, whose `'parser'` source is
Gateway events, not a terminal.

### 2.2 Command semantics: managed vs observed

`handleObservedClaudeCommand` (`daemon-server.ts:5462`) states the divergence in its own
comment, and the code confirms it:

| Deck command | Managed PTY | Observed |
|---|---|---|
| `interrupt` / `escape` | `\x03` written to the PTY (`pty-manager.ts:204`) — immediate | `requestStop(uuid)` → soft stop, denied at the **next tool call**. Pure text generation runs to completion. |
| `send_prompt` | typed into the PTY | queued, delivered by the `Stop` hook as `{decision:'block'}` |
| `respond` / `select_option` | key injection into the owned PTY | held gate if the daemon owns it; otherwise key injection into the user's terminal via the host ladder (§2.4) |
| `switch_mode` (Shift+Tab) | `\x1b[Z` + 100 ms debounce (`claude-code.ts:81`) | **absent** — not handled anywhere in `handleObservedClaudeCommand`; only `session-focus-relay.ts:48` lists it, and that relays to a managed session |

`switch_mode` is the cleanest managed-only capability in the repository: there is no hook,
no API, and no injection path for it.

It is **not** a dead control on observed rows today, because no observed-facing surface
offers one:

- The live session deck (`buildSessionDeck`, `d200h-layout.ts:874`) emits only `escape`,
  `interrupt`, `permission_decision`, `send_prompt` and `session_command` — no mode command.
- The MODE tile that does emit `{ type: 'mode_toggle' }` (`d200h-layout.ts:651,669`) lives
  in `computeLayout`, the legacy single-page direct-HID grid. `D200HLayoutModel.swift:55-58`
  records that path as superseded by the session-centric deck, and the direct-HID drivers
  are gone.
- The remaining mode button (`index.ts:1231`, gated on state at `:1230`) is built inside
  `startSession` — the session-bridge path, so its session is managed by construction.

The Swift daemon still carries a `mode_toggle` handler (`DaemonServer.swift:4847`) that
routes `switchMode` through the focus relay. No live layout emits that command, so the
handler is unreachable in practice — worth knowing before someone reads it as evidence
that observed mode switching works.

The consequence for #273 is sharper than "partial": today `switch_mode` is reachable
**only** from a managed session's own deck, so removing the managed path removes the
capability outright rather than degrading it.

### 2.3 The telemetry row in #273 is too pessimistic

#273's table records *"Terminal status-line token/cost telemetry — None"* for the
daemon-first replacement. Measured, that is wrong for tokens:

`passive-observer.ts:328-333` accumulates `input_tokens + output_tokens +
cache_read_input_tokens + cache_creation_input_tokens` from the transcript and derives
`contextPercent` (`:379-381`); both reach the wire (`protocol.ts:504`) and are rendered
(`shared/src/d200h-layout.ts:691`). Codex has the parallel path (`:485-536`).

What is genuinely terminal-only:

- **Turn duration** — `usageTracker.setDuration` has exactly one feed, the `status_line`
  parse (`state-machine.ts:386`).
- **The live status-line readout itself**, as text.

And `usage_info` (quota percent, `costSpent`/`costLimit`, reset time — `output-parser.ts:830-864`)
is a scrape of Claude's `/usage` output. The daemon already has first-class clients for
that data, so this is a duplicate source rather than a unique capability.

This row should be corrected in the issue before anyone designs against it.

### 2.4 Observed injection is real, but platform-bounded

`injectObservedSelection` (`observed-inject.ts:373`) is a four-rung ladder: tmux
`send-keys` → iTerm2 → Terminal.app tab select + JXA key post → app-hosted (labelled
button, then key post, then raise). `injectObservedText` (`:436`) does the same for a
dictated line, terminal hosts only, by explicit design.

Rungs 2-4 are `osascript` (`:341`, `:352`) — **macOS only**. Rung 1 needs `tmux`. So on
Windows and on Linux without tmux, an observed session has no injection path at all, and
the file says so in its header: *"Node-daemon only by design: every rung needs a
subprocess."*

This is a platform axis #273's table does not carry. A replacement claiming parity has to
state which platforms it claims it on.

### 2.5 `diff_prompt` is unrendered, not unreachable

The edit-approval prompt looked structurally terminal-only. It is not.

`Write`, `Edit`, `MultiEdit` and `NotebookEdit` are all in the prompting set
(`shared/src/claude-permission-rules.ts:66`), so an edit fires `PreToolUse` and is eligible
for the held device gate like any other tool. The **decision** an observed user would make
at the diff prompt is therefore already reachable daemon-first.

What is lost is the diff itself, and the loss happens on our side of the hook, not at it.
`daemon-server.ts:3677` has the full `toolInput` in hand — for an edit that includes
`old_string` and `new_string` — and passes it to `buildGateQuestion`, which
(`observed-steering.ts:76`) reduces it to `Allow Edit: <file_path>`. Only that one-line
string reaches the overlay; the overlay keeps `question` and `options`
(`awaiting-overlay.ts:29-60`), never the tool input.

So the accurate statement for #273 is: **the diff data arrives at the daemon and is
discarded before anything could render it.** Closing this gap is a rendering decision, not
a protocol or hook limitation. The third terminal affordance — "(V)iew diff" as a
navigable option next to Apply/Deny — has no equivalent, since the deck's gate is
allow/deny.

### 2.6 Hook attribution of a handed-off process is mechanically available

§1.5's replacement candidate — AgentDeck composes and hands off the command but does not
own the PTY — depends on the daemon being able to tell that the observed session which
appears is the one it launched. The machinery for that already exists:

- Every Claude hook posts `X-AgentDeck-Pid: $PPID`, folded into the payload as
  `agentdeck_pid` (`daemon-server.ts:2969-2977`) — described there as *"the only
  consent-free session→process link"*.
- `coordination.registerPid` (`coordination-evidence.ts:264`) already handles a wrapper
  sitting between the hook shell and the agent: it walks up to four levels of `ppid` until
  it finds an agent process (`isAgentProcessCommand`, `:153`), so a shell in the middle
  does not break the link.
- `passiveSessionObserver.processes()` supplies the pid/ppid table both directions.

**Not measured, and the real risk:** whether the ancestry survives. `$SHELL -l -c "claude"`
may `exec` the agent — in which case the pid the daemon spawned *is* the agent pid and the
link is trivial — or may fork and exit, re-parenting the agent away from the daemon's
child. Which happens depends on the shell and the command shape, and it decides whether
attribution needs the ancestry walk at all. This one needs a runtime measurement, not a
reading.

---

## 3. Corrections this measurement suggests for #273

1. Telemetry row: tokens and context percent **are** available daemon-first; scope the row
   to turn duration and the status-line text (§2.3).
2. Add a platform axis to the terminal-affordance rows — observed injection is macOS or
   tmux (§2.4).
3. Split the "terminal UI observation" row: `project_name`, `model_info`, `user_prompt`,
   `usage_info` are already covered or better-sourced; `diff_prompt`, `mode_change`,
   `suggested_prompt`, `cursor_update` are the real remainder (§2.1).
4. "Resume-command composition" is not an AgentDeck feature and needs no replacement
   design — only the no-clobber guarantee it already has (§1.3).
5. `diff_prompt` should not read as managed-only: the decision is already hook-reachable
   and the diff data reaches the daemon before being discarded (§2.5).

## 4. Still unmeasured

- Whether `$SHELL -l -c "<agent>"` execs or forks, which decides whether a handed-off
  process keeps an ancestry link to the daemon's child (§2.6). Runtime measurement.
- Whether the login shell's environment actually reaches the agent (§1.4). Runtime.
- Everything in the remote-attach and session-ordering gates.
