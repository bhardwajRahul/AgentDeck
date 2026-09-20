/**
 * Z.ai GLM Coding Plan usage — a direct provider-account query, modeled on
 * `usage-api.ts` (the Claude OAuth usage client).
 *
 * The z.ai plan is a PROVIDER account, not a harness feature: one key serves
 * Claude Code (`/api/anthropic`), Codex (`/api/coding/paas/v4`) and any other
 * CLI from one shared quota, so this module never looks at harness state — it
 * asks the provider's monitor endpoint what is left (#348).
 *
 * Key custody (an AgentDeck-owned provider config — never a harness's):
 *   1. `AGENTDECK_ZAI_API_KEY` env,
 *   2. daemon settings `zaiApiKey` (`~/.agentdeck/settings.json`),
 *   3. discovery hint: `~/.claude/settings.json` `env.ANTHROPIC_AUTH_TOKEN`
 *      when that file points `ANTHROPIC_BASE_URL` at a z.ai host — the
 *      official z.ai Claude Code setup stores exactly this pair, so the common
 *      case works with no AgentDeck-side configuration at all.
 *
 * The endpoint is undocumented (same status as Codex's account endpoint):
 * read-only GET, redirects are not followed anywhere the credential could
 * leak, the key is never logged, and the cache file stores numbers + a
 * `fetchedAt` stamp only. A pay-as-you-go key is detected by shape and reports
 * a windowless `{ limitId: "payg" }` block — absence of windows is explicit,
 * never rendered as exhaustion.
 */

import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { join } from 'path';
import { homedir } from 'os';
import { debug, logTagged } from './logger.js';
import { loadDaemonSettings } from './daemon-settings.js';
import {
  zaiKeyLooksPayAsYouGo,
  zaiQuotaFromLimits,
  type ZaiQuotaWindows,
} from '@agentdeck/shared';
import type { ZaiRateLimits } from './types.js';

const DEFAULT_QUOTA_URL = 'https://api.z.ai/api/monitor/usage/quota/limit';
/** See `usage-api.ts` — the TTL is set together with the 60s daemon poll that
 *  reads it, and the slack keeps a poll interval dividing the TTL from
 *  doubling the effective refresh. */
const FILE_CACHE_TTL_MS = 120_000;
const FILE_CACHE_SLACK_MS = 15_000;
const FETCH_TIMEOUT_MS = 10_000;

const AGENTDECK_DIR = process.env.AGENTDECK_DATA_DIR || join(homedir(), '.agentdeck');
export const ZAI_USAGE_CACHE_FILE = join(AGENTDECK_DIR, 'zai-usage-cache.json');

export function zaiCacheExpired(fetchedAt: number, nowMs = Date.now()): boolean {
  return !Number.isFinite(fetchedAt) || fetchedAt > nowMs ||
    (nowMs - fetchedAt) >= (FILE_CACHE_TTL_MS - FILE_CACHE_SLACK_MS);
}

/** A usage reading plus whether it is a LIVE one — same contract as
 *  `UsageFetchResult`: freshness is never folded into the data or a null. */
export interface ZaiUsageFetchResult {
  /** The wire block. Null only when there is nothing at all to say — no key
   *  configured and no cache. A windowless object (`{}`, `{ planType }`,
   *  `{ limitId: "payg" }`) is a legitimate reading with no gauges. */
  data: ZaiRateLimits | null;
  /** True only when `data` came from the network (or a within-TTL cache entry
   *  a network fetch wrote). False when served as a fallback. */
  fresh: boolean;
  /** True when the configured key is pay-as-you-go — not a subscription. */
  payg?: boolean;
}

interface ZaiUsageCacheFile {
  data: ZaiRateLimits;
  fetchedAt: number; // epoch ms
}

let consecutiveFailures = 0;
let lastAttemptAt = 0;
let inFlight: Promise<ZaiUsageFetchResult> | null = null;

function backoffMs(): number {
  if (consecutiveFailures <= 0) return 0;
  const intervals = [45_000, 90_000, 180_000, 300_000];
  return intervals[Math.min(consecutiveFailures - 1, intervals.length - 1)];
}

function noteFailure(reason: string): void {
  consecutiveFailures++;
  if (consecutiveFailures === 1 || consecutiveFailures % 5 === 0) {
    logTagged(
      'usage',
      `z.ai usage fetch failed (${consecutiveFailures}x): ${reason} — serving cached values, next attempt after ${Math.round(backoffMs() / 1000)}s backoff`,
    );
  }
  debug('ZaiUsage', `Fetch failed (${consecutiveFailures}x): ${reason}`);
}

function readFileCache(): ZaiUsageCacheFile | null {
  try {
    const cache = JSON.parse(readFileSync(ZAI_USAGE_CACHE_FILE, 'utf-8')) as ZaiUsageCacheFile;
    if (cache?.data && typeof cache.fetchedAt === 'number') return cache;
    return null;
  } catch {
    return null;
  }
}

function writeFileCache(data: ZaiRateLimits): void {
  try {
    mkdirSync(AGENTDECK_DIR, { recursive: true });
    const cache: ZaiUsageCacheFile = { data, fetchedAt: Date.now() };
    writeFileSync(ZAI_USAGE_CACHE_FILE, JSON.stringify(cache), 'utf-8');
  } catch (err) {
    debug('ZaiUsage', `Failed to write cache file: ${err}`);
  }
}

// ===== Key resolution =====

export interface ZaiApiKeySource {
  key: string;
  source: 'env' | 'daemon-settings' | 'claude-settings-hint';
}

function readClaudeSettingsHint(): string | null {
  try {
    const path = join(process.env.CLAUDE_CONFIG_DIR || join(homedir(), '.claude'), 'settings.json');
    const env = (JSON.parse(readFileSync(path, 'utf-8')) as { env?: Record<string, string> }).env;
    if (!env) return null;
    const base = env.ANTHROPIC_BASE_URL?.trim().toLowerCase() ?? '';
    // Only when that file demonstrably points Claude Code at z.ai — a token
    // bound to another base is some other account's credential.
    const isZaiBase = base === 'https://api.z.ai/api/anthropic' ||
      (() => { try { return new URL(base).hostname.endsWith('.z.ai'); } catch { return false; } })();
    if (!isZaiBase) return null;
    const token = env.ANTHROPIC_AUTH_TOKEN?.trim();
    return token || null;
  } catch {
    return null;
  }
}

/** The provider key, with its source for diagnostics. Never logged. */
export function resolveZaiApiKey(): ZaiApiKeySource | null {
  const envKey = process.env.AGENTDECK_ZAI_API_KEY?.trim();
  if (envKey) return { key: envKey, source: 'env' };
  const settingsKey = typeof loadDaemonSettings().zaiApiKey === 'string'
    ? (loadDaemonSettings().zaiApiKey as string).trim()
    : '';
  if (settingsKey) return { key: settingsKey, source: 'daemon-settings' };
  const hint = readClaudeSettingsHint();
  if (hint) return { key: hint, source: 'claude-settings-hint' };
  return null;
}

export function zaiUsageConfigured(): boolean {
  return resolveZaiApiKey() !== null;
}

function quotaUrl(): string {
  const base = typeof loadDaemonSettings().zaiApiBase === 'string'
    ? (loadDaemonSettings().zaiApiBase as string).trim().replace(/\/+$/, '')
    : '';
  return base ? `${base}/api/monitor/usage/quota/limit` : DEFAULT_QUOTA_URL;
}

// ===== Fetch =====

/** Trailing-24h measured usage from the provider's model-usage report, or null
 *  on any failure (absent fields, never fabricated). The caller passes the
 *  already-validated key. */
async function fetchModelUsageTotals(key: string): Promise<{ tokens: number; calls: number } | null> {
  if (!key) return null;
  const now = new Date();
  const start = new Date(now.getTime() - 24 * 60 * 60 * 1000);
  // The provider expects "YYYY-MM-DD HH:mm:ss" in UTC.
  const fmt = (d: Date): string => d.toISOString().slice(0, 19).replace('T', ' ');
  const url = `${quotaUrl().replace(/\/quota\/limit$/, '')}/model-usage` +
    `?startTime=${encodeURIComponent(fmt(start))}&endTime=${encodeURIComponent(fmt(now))}`;
  try {
    const res = await fetch(url, {
      method: 'GET',
      headers: { Authorization: key, Accept: 'application/json' },
      redirect: 'manual',
      signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
    });
    if (!res.ok) return null;
    const body = await res.json() as Record<string, any>;
    if (body?.code !== 200 || !body?.data?.totalUsage) return null;
    const tokens = Number(body.data.totalUsage.totalTokensUsage);
    const calls = Number(body.data.totalUsage.totalModelCallCount);
    if (!Number.isFinite(tokens) || !Number.isFinite(calls)) return null;
    return { tokens, calls };
  } catch {
    return null;
  }
}

export async function fetchZaiQuota(): Promise<ZaiUsageFetchResult> {
  if (inFlight) return inFlight;
  inFlight = fetchZaiQuotaOnce().finally(() => { inFlight = null; });
  return inFlight;
}

async function fetchZaiQuotaOnce(): Promise<ZaiUsageFetchResult> {
  const fileCache = readFileCache();
  const stale = (): ZaiUsageFetchResult =>
    // Freshest reading on disk, not the snapshot this call started with (same
    // rationale as usage-api.ts).
    ({ data: (readFileCache() ?? fileCache)?.data ?? null, fresh: false });

  if (fileCache && zaiCacheExpired(fileCache.fetchedAt) === false) {
    debug('ZaiUsage', `File cache hit (age ${Math.round((Date.now() - fileCache.fetchedAt) / 1000)}s)`);
    consecutiveFailures = 0;
    return { data: fileCache.data, fresh: true };
  }

  const source = resolveZaiApiKey();
  if (!source) return { data: null, fresh: false };

  if (zaiKeyLooksPayAsYouGo(source.key)) {
    // Not a subscription — an explicit windowless block, so any prior plan
    // gauges clear instead of freezing (retain-on-absent).
    if (consecutiveFailures > 0) consecutiveFailures = 0;
    return { data: { limitId: 'payg' }, fresh: true, payg: true };
  }

  const backoff = backoffMs();
  if (lastAttemptAt && Date.now() - lastAttemptAt < backoff) return stale();
  lastAttemptAt = Date.now();

  try {
    // Raw token, no Bearer prefix — matches the provider's own clients; the
    // prefixed form is also accepted, this is simply the canonical spelling.
    const res = await fetch(quotaUrl(), {
      method: 'GET',
      headers: { Authorization: source.key, Accept: 'application/json' },
      redirect: 'manual',
      signal: AbortSignal.timeout(FETCH_TIMEOUT_MS),
    });

    if (res.status === 401 || res.status === 403) {
      noteFailure(`auth error ${res.status} — check the z.ai coding-plan key (${source.source})`);
      return stale();
    }
    if (!res.ok) {
      noteFailure(`API returned ${res.status} ${res.statusText}`);
      return stale();
    }

    const body = await res.json() as Record<string, any>;
    // The envelope answers 200 with `{code:500, msg:"404 NOT_FOUND"}` for a
    // moved path — an HTTP-200 failure is still a failure.
    if (body?.code !== 200 || body?.success !== true || !body?.data) {
      noteFailure(`API error envelope: code=${body?.code} msg=${String(body?.msg).slice(0, 80)}`);
      return stale();
    }

    const windows: ZaiQuotaWindows = zaiQuotaFromLimits(body.data.limits, body.data.level);
    const data: ZaiRateLimits = {
      ...windows,
      capturedAt: new Date().toISOString(),
    };
    // Measured usage (trailing 24h) rides the same block: the quota endpoint
    // only answers percentages, so the actual token volume comes from the
    // model-usage report. Non-fatal — a failed report just omits the fields.
    const measured = await fetchModelUsageTotals(source.key);
    if (measured) {
      data.tokensUsed24h = measured.tokens;
      data.calls24h = measured.calls;
    }
    if (consecutiveFailures > 0) {
      logTagged('usage', `z.ai usage fetch recovered after ${consecutiveFailures} failure(s)`);
    }
    consecutiveFailures = 0;
    writeFileCache(data);
    debug('ZaiUsage', `5h: ${windows.primary?.usedPercent}% (family ${windows.limitId ?? '?'}, plan ${windows.planType ?? '?'})`);
    return { data, fresh: true };
  } catch (err) {
    noteFailure(String(err).slice(0, 160));
    return stale();
  }
}
