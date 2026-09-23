import type { CodexRateLimits } from './protocol.js';

/** Cross-surface display policy. Native mirrors are generated, not hand copied. */
export const USAGE_PRESENTATION = {
  heading: 'USAGE',
  providers: [
    { id: 'claude', label: 'Claude', prefixes: ['Claude'] },
    { id: 'codex', label: 'Codex', prefixes: ['ChatGPT', 'Codex'] },
    { id: 'zai', label: 'z.ai', prefixes: ['GLM Coding Plan', 'z.ai'] },
    { id: 'antigravity', label: 'Antigravity', prefixes: ['Google AI', 'Antigravity', 'AGY'] },
  ],
} as const;

export function usageSubscriptionProvider(name: string): number {
  const key = name.trim().toLowerCase();
  return USAGE_PRESENTATION.providers.findIndex(p => p.prefixes.some(prefix => key.startsWith(prefix.toLowerCase())));
}

/** A reported extra pool alone does not mean the account has exhausted quota.
 * Unknown/ended windows are -1, zero is valid. The next account snapshot restores
 * ordinary windows as soon as neither is exhausted. An exhausted reserve is
 * still meaningful (0% left), so availability is not a visibility condition. */
export function usageLunaActive(primary: number, secondary: number, reserve: number): boolean {
  return reserve >= 0 && (primary >= 100 || secondary >= 100);
}

export function selectedLunaReserve(limits?: CodexRateLimits, now = Date.now()) {
  const live = (w: CodexRateLimits['primary']) => w && !w.stale &&
    (!w.resetsAt || !Number.isFinite(Date.parse(w.resetsAt)) || Date.parse(w.resetsAt) > now)
    ? w.usedPercent : -1;
  const reserve = limits?.lunaReserve;
  if (!reserve || (reserve.resetsAt && Date.parse(reserve.resetsAt) <= now)) return undefined;
  return usageLunaActive(live(limits?.primary), live(limits?.secondary), reserve.usedPercent) ? reserve : undefined;
}
