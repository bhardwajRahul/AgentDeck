import { execFile } from 'node:child_process';
import { isAbsolute } from 'node:path';
import { promisify } from 'node:util';
import { transcribeWithHelper } from './foundation-models-helper.js';

export interface VoiceTranscriptionSettings {
  locale?: string;
  transcriber?: 'apple' | 'whisper-cpp';
  whisperCli?: string;
  whisperModel?: string;
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
  const { stdout } = await run(settings.whisperCli, [
    '-m', settings.whisperModel, '-f', wav, '-l', language, '-nt', '-np',
  ], { timeout: 60_000, maxBuffer: 1024 * 1024, windowsHide: true, encoding: 'utf8' });
  const text = stdout.trim();
  if (!text) throw new Error('No speech detected');
  return text;
}
