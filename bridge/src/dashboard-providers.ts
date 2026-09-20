import { loadDaemonSettings, updateDaemonSetting } from './daemon-settings.js';

/** Display membership only; independent of observation, quota and connectivity. */
export const DASHBOARD_PROVIDER_IDS = ['claude', 'codex', 'zai', 'openclaw', 'mlx', 'ollama', 'antigravity'] as const;
export function dashboardProviders(update?: Record<string, unknown>): string[] | null {
  let providers = loadDaemonSettings().dashboardProviders;
  if (update) {
    if (!Array.isArray(update.providers) || update.providers.some(p =>
      !(DASHBOARD_PROVIDER_IDS as readonly unknown[]).includes(p))) {
      throw new TypeError('Invalid providers');
    }
    // First registration cannot overwrite a saved choice, including an empty list.
    if (update.initialize !== true || !Array.isArray(providers)) {
      providers = DASHBOARD_PROVIDER_IDS.filter(p => (update.providers as string[]).includes(p));
      updateDaemonSetting('dashboardProviders', providers);
    }
  }
  return Array.isArray(providers) ? providers as string[] : null;
}
