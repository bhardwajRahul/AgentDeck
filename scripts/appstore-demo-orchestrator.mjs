#!/usr/bin/env node

// Deterministic, privacy-safe performance harness for launch recordings.
//
// `serve` drives the real AgentDeck WebSocket contract with a looping,
// time-based three-agent scenario. `terminal` replays the matching fictional
// terminal transcript. Neither mode launches a real coding agent or touches a
// user workspace, and this file is never bundled in AgentDeck.app.
//
// The cycle opens on an empty dashboard and introduces one session at a time
// (claude → codex → opencode), so a recording shows the product filling up
// the way it does on a real machine instead of starting mid-story. The whole
// arc fits inside 28s because an App Store App Preview may not exceed 30s.

import process from 'node:process';
import { readFileSync } from 'node:fs';
import { WebSocketServer } from '../bridge/node_modules/ws/wrapper.mjs';

const CYCLE_MS = 30_000;
const DEFAULT_PORT = Number(process.env.AGENTDECK_DEMO_PORT || 9220);
const productVersion = readFileSync(new URL('../VERSION', import.meta.url), 'utf8').trim();

const phases = [
  // Cold open: a connected daemon with no sessions yet. This is the frame the
  // recording starts on, and it is what a new user actually sees before the
  // first agent runs.
  {
    at: 0,
    focus: null,
    sessions: {},
    timeline: null,
  },
  {
    at: 2_000,
    focus: 'demo-claude',
    sessions: {
      claude: ['processing', 'Read', 'Mapping the responsive dashboard'],
    },
    timeline: { agent: 'claude', type: 'chat_start', raw: 'Polish the dashboard for the launch capture' },
  },
  {
    at: 4_500,
    focus: 'demo-claude',
    sessions: {
      claude: ['processing', 'Edit', 'Refining the session cards'],
      codex: ['processing', 'Bash', 'Running the integration test suite'],
    },
    timeline: { agent: 'codex', type: 'chat_start', raw: 'Verify the release candidate' },
    usage: { weeklyPercent: 78 },
  },
  // A second Claude session on a different project — the same agent can run in
  // as many workspaces as you like, and each gets its own creature.
  {
    at: 7_000,
    focus: 'demo-claude-2',
    sessions: {
      claude: ['processing', 'Edit', 'Refining the session cards'],
      codex: ['processing', 'Test', 'Checking protocol and UI tests'],
      claude2: ['processing', 'Grep', 'Auditing the token mirrors'],
    },
    timeline: { agent: 'claude2', type: 'chat_start', raw: 'Audit the design token mirrors' },
  },
  {
    at: 9_500,
    focus: 'demo-opencode',
    sessions: {
      claude: ['idle', undefined, 'Dashboard polish complete'],
      codex: ['processing', 'Test', 'Checking protocol and UI tests'],
      claude2: ['processing', 'Grep', 'Auditing the token mirrors'],
      opencode: ['processing', 'Write', 'Drafting concise release notes'],
    },
    timeline: { agent: 'opencode', type: 'chat_start', raw: 'Draft the launch release notes' },
  },
  {
    at: 11_500,
    focus: 'demo-openclaw',
    gateway: true,
    sessions: {
      claude: ['idle', undefined, 'Dashboard polish complete'],
      codex: ['processing', 'Test', 'Checking protocol and UI tests'],
      claude2: ['processing', 'Grep', 'Auditing the token mirrors'],
      opencode: ['processing', 'Write', 'Drafting concise release notes'],
      openclaw: ['processing', 'Bash', 'Reproducing the reported issue'],
    },
    timeline: { agent: 'openclaw', type: 'chat_start', raw: 'Reproduce the reported pairing issue' },
    usage: { weeklyPercent: 79 },
  },
  // Nothing focused: the timeline drops its per-session filter and every
  // agent's events interleave in one stream. `TimelineStripView` keys this
  // entirely off a missing `focusedSessionId`.
  {
    at: 13_500,
    focus: null,
    gateway: true,
    sessions: {
      claude: ['idle', undefined, 'Dashboard polish complete'],
      codex: ['processing', 'Test', 'Checking protocol and UI tests'],
      claude2: ['processing', 'Edit', 'Fixing two drifted mirrors'],
      opencode: ['processing', 'Write', 'Drafting concise release notes'],
      openclaw: ['processing', 'Bash', 'Reproducing the reported issue'],
    },
    timeline: { agent: 'claude2', type: 'tool_exec', raw: 'Edit · design-tokens.ts', detail: 'input: two drifted mirrors' },
  },
  {
    at: 16_000,
    focus: null,
    gateway: true,
    sessions: {
      claude: ['idle', undefined, 'Dashboard polish complete'],
      codex: ['idle', undefined, 'All release checks passed'],
      claude2: ['processing', 'Edit', 'Fixing two drifted mirrors'],
      opencode: ['idle', undefined, 'Release notes are ready'],
      openclaw: ['processing', 'Bash', 'Reproducing the reported issue'],
    },
    timeline: { agent: 'codex', type: 'chat_response', raw: 'All release checks passed.' },
  },
  {
    at: 18_000,
    focus: 'demo-claude',
    gateway: true,
    sessions: {
      claude: [
        'awaiting_permission',
        'Edit',
        'Waiting for permission to update the interface',
        'Allow the agent to update the dashboard layout?',
      ],
      codex: ['idle', undefined, 'All release checks passed'],
      claude2: ['processing', 'Edit', 'Fixing two drifted mirrors'],
      opencode: ['idle', undefined, 'Release notes are ready'],
      openclaw: ['processing', 'Bash', 'Reproducing the reported issue'],
    },
    timeline: { agent: 'claude', type: 'chat_start', raw: 'Apply the final layout adjustment' },
  },
  {
    at: 21_500,
    focus: 'demo-claude',
    gateway: true,
    sessions: {
      claude: ['processing', 'Edit', 'Applying the approved adjustment'],
      codex: ['idle', undefined, 'All release checks passed'],
      claude2: ['idle', undefined, 'Token mirrors back in sync'],
      opencode: ['idle', undefined, 'Release notes are ready'],
      openclaw: ['processing', 'Bash', 'Reproducing the reported issue'],
    },
    timeline: { agent: 'claude', type: 'tool_exec', raw: 'Edit · DashboardLayout.swift', detail: 'input: final layout adjustment' },
    usage: { weeklyPercent: 80 },
  },
  {
    at: 24_000,
    focus: 'demo-openclaw',
    gateway: true,
    sessions: {
      claude: ['processing', 'Edit', 'Applying the approved adjustment'],
      codex: ['idle', undefined, 'All release checks passed'],
      claude2: ['idle', undefined, 'Token mirrors back in sync'],
      opencode: ['idle', undefined, 'Release notes are ready'],
      openclaw: ['idle', undefined, 'Issue reproduced and captured'],
    },
    timeline: { agent: 'openclaw', type: 'chat_response', raw: 'Reproduced the pairing issue and captured the log.' },
  },
  // Closing frame: every session idle for the last few seconds so the terrarium
  // settles to its rest state before the loop restarts.
  {
    at: 26_000,
    focus: null,
    gateway: true,
    sessions: {
      claude: ['idle', undefined, 'Final adjustment complete'],
      codex: ['idle', undefined, 'All release checks passed'],
      claude2: ['idle', undefined, 'Token mirrors back in sync'],
      opencode: ['idle', undefined, 'Release notes are ready'],
      openclaw: ['idle', undefined, 'Issue reproduced and captured'],
    },
    timeline: { agent: 'claude', type: 'chat_response', raw: 'Final adjustment complete.' },
  },
];

const agents = {
  claude: {
    id: 'demo-claude',
    port: 9121,
    projectName: 'Sample Workspace',
    agentType: 'claude-code',
    modelName: 'Claude Sonnet',
    color: '\u001b[38;5;208m',
    label: 'CLAUDE CODE · Sample Workspace',
    appearsAt: 2_000,
  },
  codex: {
    id: 'demo-codex',
    port: 9122,
    projectName: 'API Client',
    agentType: 'codex-cli',
    modelName: 'GPT-5 Codex',
    color: '\u001b[38;5;45m',
    label: 'CODEX · API Client',
    appearsAt: 4_500,
  },
  claude2: {
    id: 'demo-claude-2',
    port: 9124,
    projectName: 'Design System',
    agentType: 'claude-code',
    modelName: 'Claude Sonnet',
    color: '\u001b[38;5;214m',
    label: 'CLAUDE CODE · Design System',
    appearsAt: 7_000,
  },
  opencode: {
    id: 'demo-opencode',
    port: 9123,
    projectName: 'Documentation',
    agentType: 'opencode',
    modelName: 'Qwen Coder',
    color: '\u001b[38;5;141m',
    label: 'OPENCODE · Documentation',
    appearsAt: 9_500,
  },
  openclaw: {
    id: 'demo-openclaw',
    port: 9125,
    projectName: 'OpenClaw',
    agentType: 'openclaw',
    modelName: 'Claude Sonnet',
    color: '\u001b[38;5;203m',
    label: 'OPENCLAW · Issue triage',
    appearsAt: 11_500,
  },
};

const terminalLines = {
  claude: [
    [2_000, '❯ Polish the dashboard for the launch capture'],
    [2_800, '  Reading MonitorScreen.swift'],
    [4_500, '  Editing responsive session cards…'],
    [9_300, '✓ Dashboard polish complete'],
    [17_800, '❯ Apply the final layout adjustment'],
    [18_400, '  Permission required: update dashboard layout'],
    [21_000, '  Permission granted'],
    [21_500, '  Applying final adjustment…'],
    [25_800, '✓ Final adjustment complete'],
  ],
  codex: [
    [4_500, '› Verify the release candidate'],
    [5_100, '• Running integration test suite'],
    [7_000, '• Checking protocol contract tests'],
    [11_500, '• Checking SwiftUI state projection'],
    [15_800, '✓ 1842 tests passed'],
    [16_000, '✓ Release candidate verified'],
  ],
  claude2: [
    [7_000, '❯ Audit the design token mirrors'],
    [7_800, '  Grepping four token mirrors…'],
    [13_500, '  Editing design-tokens.ts'],
    [21_300, '✓ Token mirrors back in sync'],
  ],
  opencode: [
    [9_500, '❯ Draft the launch release notes'],
    [10_200, '  Reading the release summary…'],
    [12_000, '  Writing concise feature highlights…'],
    [15_900, '✓ Release notes are ready'],
  ],
  openclaw: [
    [11_500, '❯ Reproduce the reported pairing issue'],
    [12_300, '  Replaying the gateway handshake…'],
    [18_000, '  Capturing the failing log window…'],
    [23_800, '✓ Issue reproduced and captured'],
  ],
};

function parseArgs(argv) {
  const [command = 'serve', ...rest] = argv;
  const options = { command };
  for (let index = 0; index < rest.length; index += 1) {
    const arg = rest[index];
    if (arg === '--once') options.once = true;
    else if (arg === '--port') options.port = Number(rest[++index]);
    else if (arg === '--epoch-ms') options.epochMs = Number(rest[++index]);
    else if (arg === '--agent') options.agent = rest[++index];
    else throw new Error(`Unknown argument: ${arg}`);
  }
  return options;
}

function cyclePosition(epochMs, now = Date.now()) {
  const elapsed = Math.max(0, now - epochMs);
  return elapsed % CYCLE_MS;
}

function phaseIndexAt(position) {
  let selected = 0;
  for (let index = 0; index < phases.length; index += 1) {
    if (phases[index].at > position) break;
    selected = index;
  }
  return selected;
}

function sessionInfo(agentKey, tuple) {
  const agent = agents[agentKey];
  const [state, currentTool, activity, question] = tuple;
  return {
    id: agent.id,
    port: agent.port,
    projectName: agent.projectName,
    agentType: agent.agentType,
    alive: true,
    state,
    modelName: agent.modelName,
    ...(currentTool ? { currentTool } : {}),
    activity,
    ...(question ? { question } : {}),
  };
}

function timelineEntry(phase, cycleStartedAt) {
  const agent = agents[phase.timeline.agent];
  return {
    ts: cycleStartedAt + phase.at,
    type: phase.timeline.type,
    raw: phase.timeline.raw,
    agentType: agent.agentType,
    projectName: agent.projectName,
    sessionId: agent.id,
    ...(phase.timeline.detail ? { detail: phase.timeline.detail } : {}),
  };
}

// Codex rolling-window quota, the one provider gauge the sandboxed App Store
// build can produce on its own (the Swift daemon reads ~/.codex directly).
// Claude's 5h/7d fields are deliberately omitted: those depend on OAuth token
// and relay data only the external Node daemon supplies, so `TopologyRail`
// collapses the Claude row. Sending them here would show the App Store app
// doing something it cannot do.
function usageEvent(usage, cycleStartedAt) {
  const isoAfter = (ms) => new Date(cycleStartedAt + ms).toISOString();
  return {
    type: 'usage_update',
    usageStale: false,
    codexPlanType: 'plus',
    codexRateLimits: {
      planType: 'plus',
      // A single 7d window, matching what a real machine reports once the 5h
      // window has reset: the short window drops out and the weekly one is
      // all that remains. The label is derived from `windowMinutes`, not the
      // slot, so 10080 reads as "7d" in whichever slot it lands.
      primary: {
        usedPercent: usage.weeklyPercent,
        windowMinutes: 10_080,
        resetsAt: isoAfter(3.4 * 24 * 60 * 60 * 1000),
        stale: false,
      },
    },
  };
}

/// Most recent usage snapshot at or before `index`, so a client that connects
/// mid-cycle still sees a populated gauge instead of an empty provider row.
function usageAt(index) {
  for (let i = index; i >= 0; i -= 1) {
    if (phases[i].usage) return phases[i].usage;
  }
  return null;
}

// A full downstream fleet. The rail renders a row per *device entry*, not per
// count: `streamDeck`/`pixoo` are gated on a non-empty `devices` array and
// ignore `deviceCount`/`connectedDeviceCount` entirely, which is why the
// earlier count-only payload produced an empty "Pixel displays" header and no
// Stream Deck section at all. Addresses are fictional — never a real LAN.
const moduleHealth = {
  streamDeck: {
    available: true,
    devices: [
      { id: 'sd-xl', family: 'streamdeckxl', columns: 6, rows: 3 },
      { id: 'sd-plus', family: 'streamdeckplus', columns: 4, rows: 2 },
    ],
  },
  d200h: { connected: true },
  pixoo: {
    configuredDeviceCount: 1,
    hasFrame: true,
    devices: [{ ip: '192.168.0.51', online: true, failures: 0, backedOff: false }],
  },
  timebox: {
    configuredDeviceCount: 1,
    connected: true,
    deviceName: 'Timebox-Mini',
    statusReason: 'connected',
    hasFrame: true,
  },
  idotmatrix: {
    configuredDeviceCount: 1,
    connected: true,
    deviceName: 'IDM-32',
    statusReason: 'connected',
    hasFrame: true,
  },
  serial: {
    connections: [
      {
        connected: true,
        port: '/dev/tty.usbmodem1101',
        deviceInfo: { board: 'ips_10', version: productVersion, wifiConnected: true },
      },
      {
        connected: true,
        port: '/dev/tty.usbserial-0001',
        deviceInfo: { board: 'ulanzi_tc001', version: productVersion, wifiConnected: false },
      },
    ],
  },
  esp32Wifi: {
    available: true,
    devices: [
      { board: 'trmnl_75', ip: '192.168.0.71', version: productVersion, stale: false, serialActive: false },
      { board: 'round_amoled', ip: '192.168.0.72', version: productVersion, stale: false, serialActive: false },
      { board: 'ttgo_t_display', ip: '192.168.0.73', version: productVersion, stale: false, serialActive: false },
    ],
  },
  tuiDashboards: {
    available: true,
    devices: [{ id: 'demo-tui', name: 'workstation' }],
  },
};

function eventsForPhase(index, cycleStartedAt, includeHistory) {
  const phase = phases[index];
  const focusedKey = Object.keys(agents).find((key) => agents[key].id === phase.focus);
  const focused = agents[focusedKey];
  // The cold-open phase has no sessions at all: report a healthy, connected
  // daemon with nothing focused so the app renders its real empty state.
  const focusState = focused
    ? (() => {
        const [state, currentTool, , question] = phase.sessions[focusedKey];
        return {
          state,
          sessionId: focused.id,
          focusedSessionId: focused.id,
          agentType: focused.agentType,
          projectName: focused.projectName,
          modelName: focused.modelName,
          ...(currentTool ? { currentTool } : {}),
          ...(question ? { question } : {}),
        };
      })()
    // An *empty* focusedSessionId is what clears the selection — omitting the
    // field leaves the previous focus in place, so the timeline would stay
    // filtered to whichever session was focused last
    // (`AgentStateHolder.swift`: `if let id = e.focusedSessionId { … isEmpty ? nil : id }`).
    : { state: 'idle', focusedSessionId: '' };

  const events = [
    {
      type: 'state_update',
      ...focusState,
      permissionMode: 'default',
      // OpenClaw sessions only read as connected when the Gateway does; the
      // flag flips on in the phase where the OpenClaw session joins.
      gatewayAvailable: phase.gateway === true,
      gatewayConnected: phase.gateway === true,
      gatewayHasError: false,
      daemonPort: 9120,
      moduleHealth,
    },
    {
      type: 'sessions_list',
      sessions: Object.entries(phase.sessions).map(([key, tuple]) => sessionInfo(key, tuple)),
    },
  ];

  // On (re)connect replay the standing snapshot; mid-cycle only emit on the
  // phases that actually move the gauge.
  const usage = includeHistory ? usageAt(index) : phase.usage;
  if (usage) events.push(usageEvent(usage, cycleStartedAt));

  if (includeHistory) {
    events.push({
      type: 'timeline_history',
      entries: phases
        .slice(0, index + 1)
        .filter((item) => item.timeline)
        .map((item) => timelineEntry(item, cycleStartedAt)),
    });
  } else if (phase.timeline) {
    events.push({ type: 'timeline_event', entry: timelineEntry(phase, cycleStartedAt) });
  }
  return events;
}

function send(socket, payload) {
  if (socket.readyState === socket.OPEN) socket.send(JSON.stringify(payload));
}

async function serve(options) {
  const port = options.port || DEFAULT_PORT;
  const epochMs = options.epochMs || Date.now() + 1_500;
  const wss = new WebSocketServer({ host: '127.0.0.1', port });
  let lastPhase = -1;
  let lastCycle = -1;

  wss.on('connection', (socket) => {
    const now = Date.now();
    const elapsed = Math.max(0, now - epochMs);
    const cycle = Math.floor(elapsed / CYCLE_MS);
    const cycleStartedAt = epochMs + cycle * CYCLE_MS;
    const index = phaseIndexAt(cyclePosition(epochMs, now));
    send(socket, {
      type: 'connection',
      status: 'connected',
      ...(phases[index].focus ? { sessionId: phases[index].focus } : {}),
    });
    for (const event of eventsForPhase(index, cycleStartedAt, true)) send(socket, event);

    socket.on('message', (data) => {
      if (data.toString().includes('ping')) send(socket, { type: 'pong' });
    });
  });

  const timer = setInterval(() => {
    const now = Date.now();
    const elapsed = Math.max(0, now - epochMs);
    const cycle = Math.floor(elapsed / CYCLE_MS);
    const index = phaseIndexAt(cyclePosition(epochMs, now));
    if (index === lastPhase && cycle === lastCycle) return;

    const cycleStartedAt = epochMs + cycle * CYCLE_MS;
    const isNewCycle = cycle !== lastCycle;
    for (const socket of wss.clients) {
      for (const event of eventsForPhase(index, cycleStartedAt, isNewCycle)) send(socket, event);
    }
    lastPhase = index;
    lastCycle = cycle;

    if (options.once && index === phases.length - 1) {
      setTimeout(() => shutdown(), 1_500);
    }
  }, 100);

  const shutdown = () => {
    clearInterval(timer);
    wss.close(() => process.exit(0));
  };
  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);

  console.log(`AgentDeck recording scenario: ws://127.0.0.1:${port}`);
  console.log(`Synchronized epoch: ${epochMs} · cycle: ${CYCLE_MS / 1000}s`);
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function replayTerminal(options) {
  const agentKey = options.agent;
  if (!agents[agentKey]) throw new Error(`Unknown agent: ${agentKey || '(missing)'}`);

  const epochMs = options.epochMs || Date.now() + 1_000;
  const agent = agents[agentKey];
  const reset = '\u001b[0m';
  const faint = '\u001b[2m';

  for (;;) {
    const now = Date.now();
    const elapsed = Math.max(0, now - epochMs);
    const cycle = Math.floor(elapsed / CYCLE_MS);
    const cycleStartedAt = epochMs + cycle * CYCLE_MS;
    const waitForCycle = Math.max(0, cycleStartedAt - now);
    if (waitForCycle > 0) await sleep(waitForCycle);

    process.stdout.write('\u001b[2J\u001b[H');
    // Hold the pane blank until this agent's session appears on the dashboard,
    // so the terminal side of the frame fills in one agent at a time too.
    const headerDelay = Math.max(0, cycleStartedAt + agent.appearsAt - Date.now());
    if (headerDelay > 0) await sleep(headerDelay);
    console.log(`${agent.color}${agent.label}${reset}`);
    console.log(`${faint}deterministic launch rehearsal · no real workspace data${reset}\n`);

    for (const [at, line] of terminalLines[agentKey]) {
      const delay = Math.max(0, cycleStartedAt + at - Date.now());
      if (delay > 0) await sleep(delay);
      console.log(line);
    }

    const cycleEndDelay = Math.max(0, cycleStartedAt + CYCLE_MS - Date.now());
    if (options.once) break;
    if (cycleEndDelay > 0) await sleep(cycleEndDelay);
  }
}

const options = parseArgs(process.argv.slice(2));
if (options.command === 'serve') await serve(options);
else if (options.command === 'terminal') await replayTerminal(options);
else throw new Error(`Unknown command: ${options.command}`);
