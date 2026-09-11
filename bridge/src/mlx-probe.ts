import { loadMlxSettings, resolveSafeMlxModel } from '@agentdeck/shared';

/** This is a resident-model probe, never a downloaded-model picker. */
export async function fetchMlxModels(pin?: string | null): Promise<string[] | null> {
  const settings = loadMlxSettings();
  try { return [await resolveSafeMlxModel(settings.endpoint, pin ?? settings.model)]; }
  catch { return null; }
}
