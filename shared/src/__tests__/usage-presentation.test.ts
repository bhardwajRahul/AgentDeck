import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { USAGE_PRESENTATION, usageLunaActive, selectedLunaReserve, usageSubscriptionProvider } from '../usage-presentation.js';
// @ts-expect-error executable generator has no TypeScript declaration
import { outputs } from '../../../scripts/generate-usage-presentation.mjs';
const window = (usedPercent: number, stale = false) => ({ usedPercent, stale, windowMinutes: 300 });
describe('shared usage display policy', () => {
  it('keeps regular windows until exhausted, then restores them after reset', () => {
    const lunaReserve = { usedPercent: 32 };
    expect(selectedLunaReserve({ primary: window(99), lunaReserve })).toBeUndefined();
    expect(selectedLunaReserve({ secondary: window(100), lunaReserve })).toEqual(lunaReserve);
    expect(selectedLunaReserve({ primary: window(0), lunaReserve })).toBeUndefined();
    expect(selectedLunaReserve({ secondary: window(100, true), lunaReserve })).toBeUndefined();
    expect(selectedLunaReserve({ lunaReserve })).toBeUndefined();
    expect(selectedLunaReserve({ primary: window(100), lunaReserve: { usedPercent: 100, available: false } })).toBeDefined();
  });
  it('expires reserve and regular windows independently', () => {
    const now = Date.parse('2026-09-24T00:00:00Z');
    expect(selectedLunaReserve({ primary: window(100), lunaReserve: { usedPercent: 32, resetsAt: '2026-09-23T23:00:00Z' } }, now)).toBeUndefined();
    expect(selectedLunaReserve({ primary: { ...window(100), resetsAt: '2026-09-23T23:00:00Z' }, lunaReserve: { usedPercent: 32 } }, now)).toBeUndefined();
  });
  it('attributes reported subscription metadata without inventing a provider', () => {
    for (const [name, index] of [['Claude Max',0], ['ChatGPT Pro',1], ['GLM Coding Plan · Lite',2], ['Google AI Pro',3], ['AGY Ultra',3], ['Unknown plan',-1]] as const) {
      expect(usageSubscriptionProvider(name)).toBe(index);
    }
  });
  it('generates the firmware predicate and provider table from the actual source', () => {
    for (const [target, emit] of outputs) expect(readFileSync(new URL(`../../../${target}`, import.meta.url), 'utf8')).toBe(emit(USAGE_PRESENTATION, usageLunaActive.toString()));
  });
});
