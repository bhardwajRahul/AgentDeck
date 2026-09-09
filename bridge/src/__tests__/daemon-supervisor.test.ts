/**
 * The autostart unit as a lifecycle peer: who performs a stop/start, and with
 * which commands.
 *
 * The plans are pinned rather than the behaviour of `launchctl` itself, which
 * cannot be driven from CI. What CAN be driven — and is, below — is that the
 * posture parser reads what the three real installers actually write: the
 * cross-format claim is the one a hand-built fixture would agree with forever.
 */
import { describe, it, expect } from 'vitest';
import {
  supervisorStopPlan,
  supervisorStartPlan,
  parseSupervisorPosture,
  oneOffFlagsBlockingSupervisor,
  routeDaemonLifecycle,
  runSupervisorPlan,
  supervisorJobRunning,
  supervisorLivenessProbe,
  describeSupervisor,
  PLIST_LABEL,
  type SupervisorFacts,
} from '../daemon-supervisor.js';
import { buildPlist, daemonPostureArgs } from '../cli.js';
import { buildUnitFile } from '../linux-service.js';
import { buildScheduledTaskXml } from '../windows-service.js';

const LAUNCHD: SupervisorFacts = {
  kind: 'launchd', label: PLIST_LABEL, unitPath: '/Users/x/Library/LaunchAgents/dev.agentdeck.daemon.plist', uid: 501,
};
const SYSTEMD: SupervisorFacts = { kind: 'systemd', label: 'agentdeck-daemon.service', unitPath: '/home/x/.config/systemd/user/agentdeck-daemon.service' };
const SCHTASKS: SupervisorFacts = { kind: 'schtasks', label: 'AgentDeckDaemon' };

describe('stop plans — a stop that stays stopped', () => {
  it('launchd boots the job OUT; `launchctl stop` is not a stop here', () => {
    // Measured 2026-09-09 with a throwaway LaunchAgent carrying the shipped
    // KeepAlive{SuccessfulExit:false}: a self-SIGKILL (which is how the daemon
    // ends /shutdown) was respawned every time, ~7s later. `launchctl stop`
    // only sends SIGTERM, so it produces exactly that death — and a respawn.
    const plan = supervisorStopPlan(LAUNCHD);
    expect(plan.map((c) => c.argv)).toEqual([
      ['launchctl', 'bootout', 'gui/501/dev.agentdeck.daemon'],
    ]);
    expect(plan.some((c) => c.argv[1] === 'stop')).toBe(false);
  });

  it('a job that was never loaded is a legitimate state, not a failure', () => {
    expect(supervisorStopPlan(LAUNCHD)[0].optional).toBe(true);
    expect(supervisorStopPlan(SCHTASKS)[0].optional).toBe(true);
    // systemctl stop exits 0 on an inactive unit, so nothing is excused there.
    expect(supervisorStopPlan(SYSTEMD)[0].optional).toBeUndefined();
  });

  it('systemd and Task Scheduler stop through their own verbs', () => {
    expect(supervisorStopPlan(SYSTEMD).map((c) => c.argv))
      .toEqual([['systemctl', '--user', 'stop', 'agentdeck-daemon.service']]);
    expect(supervisorStopPlan(SCHTASKS).map((c) => c.argv))
      .toEqual([['schtasks', '/End', '/TN', 'AgentDeckDaemon']]);
  });
});

describe('start plans', () => {
  it('launchd bootstraps before it kickstarts', () => {
    // Verified on the same probe: after `bootout`, `kickstart` fails with
    // rc 113 "Could not find service" — the stop removes the job, so the start
    // has to put it back before it can run it. Bootstrap is optional because a
    // still-loaded job is the ordinary case and it fails there.
    const plan = supervisorStartPlan(LAUNCHD);
    expect(plan.map((c) => c.argv)).toEqual([
      ['launchctl', 'bootstrap', 'gui/501', LAUNCHD.unitPath],
      ['launchctl', 'kickstart', 'gui/501/dev.agentdeck.daemon'],
    ]);
    expect(plan[0].optional).toBe(true);
    expect(plan[1].optional).toBeUndefined();
  });

  it('systemd and Task Scheduler start through their own verbs', () => {
    expect(supervisorStartPlan(SYSTEMD).map((c) => c.argv))
      .toEqual([['systemctl', '--user', 'start', 'agentdeck-daemon.service']]);
    expect(supervisorStartPlan(SCHTASKS).map((c) => c.argv))
      .toEqual([['schtasks', '/Run', '/TN', 'AgentDeckDaemon']]);
  });
});

describe('unit posture — read from what the installers write', () => {
  // The point of these three: the parser is asked about the real generated
  // file, not about a string composed to match the parser.
  it('reads the posture back out of the real LaunchAgent plist', () => {
    expect(parseSupervisorPosture('launchd', buildPlist(daemonPostureArgs({ enterprise: true }))))
      .toEqual(['--loopback']);
    expect(parseSupervisorPosture('launchd', buildPlist(daemonPostureArgs({ local: true, enterprise: true }))))
      .toEqual(['--local', '--loopback']);
    expect(parseSupervisorPosture('launchd', buildPlist())).toEqual([]);
  });

  it('reads the posture back out of the real systemd unit (quoted ExecStart words)', () => {
    expect(parseSupervisorPosture('systemd', buildUnitFile({ extraArgs: ['--loopback'] })))
      .toEqual(['--loopback']);
    expect(parseSupervisorPosture('systemd', buildUnitFile({ extraArgs: ['--local'] })))
      .toEqual(['--local']);
    expect(parseSupervisorPosture('systemd', buildUnitFile())).toEqual([]);
  });

  it('reads the posture back out of the real scheduled-task XML (bare argv)', () => {
    expect(parseSupervisorPosture('schtasks', buildScheduledTaskXml({ extraArgs: ['--local', '--loopback'] })))
      .toEqual(['--local', '--loopback']);
    expect(parseSupervisorPosture('schtasks', buildScheduledTaskXml())).toEqual([]);
  });

  it('a longer flag that merely starts with the same letters is not a match', () => {
    expect(parseSupervisorPosture('systemd', 'ExecStart=/n "--loopbackish"')).toEqual([]);
    expect(parseSupervisorPosture('launchd', '<string>--loopbackish</string>')).toEqual([]);
  });

  it('the posture list is comparable as a string — same order as daemonPostureArgs', () => {
    expect(parseSupervisorPosture('launchd', buildPlist(['--loopback', '--local'])))
      .toEqual(daemonPostureArgs({ local: true, loopback: true }));
  });
});

describe('routeDaemonLifecycle', () => {
  const base = { supervisor: LAUNCHD, oneOffFlags: [] as string[] };

  it('an installed unit performs the command', () => {
    expect(routeDaemonLifecycle(base)).toEqual({ via: 'supervisor' });
  });

  it('no unit means the CLI does it, as it always did', () => {
    expect(routeDaemonLifecycle({ supervisor: null, oneOffFlags: [] }))
      .toEqual({ via: 'self', reason: 'no-supervisor' });
  });

  it('a one-off flag the unit cannot carry takes it back', () => {
    // The unit has one fixed argv. `-p 9130` / `--debug` ask for a daemon it
    // cannot produce, so those still fork — and the caller says the result is
    // not the supervised daemon.
    expect(routeDaemonLifecycle({ ...base, oneOffFlags: ['--debug'] }))
      .toEqual({ via: 'self', reason: 'one-off-flags', flags: ['--debug'] });
  });

  it('a posture the unit does not carry takes it back — the enterprise downgrade', () => {
    // Handing a loopback-only daemon's restart to a default-posture unit would
    // rewrite it into an advertising one, silently. That is the exact failure
    // the running daemon's posture is read for in the first place.
    expect(routeDaemonLifecycle({ ...base, unitPosture: [], wantedPosture: ['--loopback'] }))
      .toEqual({ via: 'self', reason: 'posture-mismatch', unitPosture: [], wantedPosture: ['--loopback'] });
    expect(routeDaemonLifecycle({ ...base, unitPosture: ['--loopback'], wantedPosture: ['--loopback'] }))
      .toEqual({ via: 'supervisor' });
  });

  it('an absent posture pair is not a mismatch', () => {
    // `daemon start` has no running daemon to inherit from: the unit's own
    // posture IS the answer there, so the check must not fire.
    expect(routeDaemonLifecycle({ ...base, unitPosture: ['--loopback'] })).toEqual({ via: 'supervisor' });
    expect(routeDaemonLifecycle({ ...base, wantedPosture: ['--loopback'] })).toEqual({ via: 'supervisor' });
  });

  it('flags outrank posture — both are reported one at a time', () => {
    expect(routeDaemonLifecycle({ ...base, oneOffFlags: ['--port'], unitPosture: [], wantedPosture: ['--local'] }))
      .toEqual({ via: 'self', reason: 'one-off-flags', flags: ['--port'] });
  });
});

describe('oneOffFlagsBlockingSupervisor', () => {
  it('a plain invocation blocks nothing', () => {
    expect(oneOffFlagsBlockingSupervisor({})).toEqual([]);
    // Commander leaves absent options undefined and `--no-*` options as true;
    // only a typed value blocks.
    expect(oneOffFlagsBlockingSupervisor({ port: undefined, debug: false })).toEqual([]);
  });

  it('every flag the unit cannot express is named', () => {
    expect(oneOffFlagsBlockingSupervisor({
      port: '9130', debug: true, local: true, loopback: true, portWindow: '9200-9209', wakeWord: true,
    })).toEqual(['--port', '--debug', '--local', '--loopback', '--port-window', '--wake-word']);
  });
});

describe('runSupervisorPlan', () => {
  const node = process.execPath;

  it('an optional failure is not a failed plan', () => {
    const result = runSupervisorPlan([{ argv: [node, '-e', 'process.exit(3)'], optional: true }]);
    expect(result.ok).toBe(true);
    expect(result.ran[0].ok).toBe(false);
  });

  it('a required failure is', () => {
    const result = runSupervisorPlan([{ argv: [node, '-e', 'process.exit(3)'] }]);
    expect(result.ok).toBe(false);
  });

  it('a missing binary never throws — the machine must not be left daemonless', () => {
    const result = runSupervisorPlan([{ argv: ['agentdeck-no-such-binary-zzz', 'x'], optional: true }]);
    expect(result.ok).toBe(true);
    expect(result.ran[0].ok).toBe(false);
  });

  it('runs the plan in order and reports each command', () => {
    const result = runSupervisorPlan([
      { argv: [node, '-e', ''] },
      { argv: [node, '-e', 'process.exit(1)'], optional: true },
    ]);
    expect(result.ran.map((r) => r.ok)).toEqual([true, false]);
    expect(result.ok).toBe(true);
  });
});

describe('describeSupervisor', () => {
  it('names the thing the user would have to go look at', () => {
    expect(describeSupervisor(LAUNCHD)).toBe('launchd unit dev.agentdeck.daemon');
    expect(describeSupervisor(SYSTEMD)).toBe('systemd --user unit agentdeck-daemon.service');
    expect(describeSupervisor(SCHTASKS)).toBe('scheduled task AgentDeckDaemon');
  });
});

describe('supervisorJobRunning / supervisorLivenessProbe', () => {
  it('answers for the real launchd job on this machine', () => {
    if (process.platform !== 'darwin') return;
    // A job that does not exist is a definite "not running" — the one answer
    // that is allowed to end a wait. (The label is deliberately fictional; the
    // real one may or may not be installed on the machine running this suite.)
    expect(supervisorJobRunning({ kind: 'launchd', label: 'dev.agentdeck.no-such-job', uid: process.getuid?.() ?? 0 }))
      .toBe(false);
  });

  it('an unknown answer keeps the wait alive rather than ending it', () => {
    // schtasks does not exist off Windows, so the lookup throws and returns
    // undefined — which must read as "keep waiting", not "it died". Absence of
    // an answer is not an answer.
    const probe = supervisorLivenessProbe({ kind: 'schtasks', label: 'AgentDeckDaemon' });
    expect(probe()).toBe(true);
  });

  it('caches within the throttle window', () => {
    if (process.platform !== 'darwin') return;
    const probe = supervisorLivenessProbe({ kind: 'launchd', label: 'dev.agentdeck.no-such-job', uid: 501 }, 60_000);
    expect(probe()).toBe(false);
    // Second call must not re-exec; it can only return the cached answer.
    expect(probe()).toBe(false);
  });
});
