import { execFile } from 'node:child_process';
import { isAbsolute } from 'node:path';
import { readFile } from 'node:fs/promises';
import { promisify } from 'node:util';
import { transcribeWithHelper } from './foundation-models-helper.js';

export interface VoiceTranscriptionSettings {
  locale?: string;
  transcriber?: 'apple' | 'whisper-cpp';
  whisperCli?: string;
  whisperModel?: string;
  /** Explicit opt-in to an operator-managed, loopback-only warm model server. */
  whisperServerUrl?: string;
}
const run = promisify(execFile);

/** Explicit local backend selection: no network ASR or implicit model download. */
export async function transcribeDeviceAudio(wav: string, settings?: VoiceTranscriptionSettings): Promise<string> {
  if (!settings?.transcriber || settings.transcriber === 'apple') {
    return transcribeWithHelper(wav, settings?.locale);
  }
  if (settings.transcriber !== 'whisper-cpp') throw new Error('unknown_voice_transcriber');
  if (!settings.whisperCli || !isAbsolute(settings.whisperCli)
    || !settings.whisperModel || !isAbsolute(settings.whisperModel)) {
    throw new Error('whisper_paths_required');
  }
  const language = settings.locale?.split(/[-_]/)[0].toLowerCase() || 'auto';
  if (!/^(?:[a-z]{2,3}|auto)$/.test(language)) throw new Error('invalid_voice_locale');
  if (settings.whisperServerUrl) {
    const endpoint = new URL(settings.whisperServerUrl);
    if (endpoint.protocol !== 'http:' || endpoint.hostname !== '127.0.0.1'
      || endpoint.username || endpoint.password || endpoint.search || endpoint.hash
      || endpoint.pathname !== '/inference') throw new Error('invalid_whisper_server_url');
    try {
      const form = new FormData();
      form.set('file', new Blob([new Uint8Array(await readFile(wav))], { type: 'audio/wav' }), 'capture.wav');
      form.set('language', language);
      form.set('response_format', 'json');
      // No redirects: captured speech must never leave the configured loopback.
      // Bound a dead/stalled warm worker before falling back to the existing CLI.
      const response = await fetch(endpoint, {
        method: 'POST', body: form, redirect: 'error', signal: AbortSignal.timeout(5_000),
      });
      if (!response.ok) throw new Error('whisper_server_failed');
      const result = await response.json() as { text?: unknown };
      if (typeof result.text !== 'string') throw new Error('invalid_whisper_server_response');
      const text = result.text.trim();
      if (!text) throw new Error('No speech detected');
      return text;
    } catch (error) {
      if (error instanceof Error && error.message === 'No speech detected') throw error;
      // A warm worker is an optimization, never a requirement for dictation.
    }
  }
  const { stdout } = await run(settings.whisperCli, [
    '-m', settings.whisperModel, '-f', wav, '-l', language, '-nt', '-np',
  ], { timeout: 60_000, maxBuffer: 1024 * 1024, windowsHide: true, encoding: 'utf8' });
  const text = stdout.trim();
  if (!text) throw new Error('No speech detected');
  return text;
}
