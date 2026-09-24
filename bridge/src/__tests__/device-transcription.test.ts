import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
const mocks = vi.hoisted(() => ({ exec: vi.fn(), apple: vi.fn(), read: vi.fn() }));
vi.mock('node:fs/promises', () => ({ readFile: mocks.read }));
vi.mock('node:child_process', () => ({ execFile: mocks.exec }));
vi.mock('../foundation-models-helper.js', () => ({ transcribeWithHelper: mocks.apple }));
import { transcribeDeviceAudio } from '../device-transcription.js';
const local = { transcriber: 'whisper-cpp' as const, whisperCli: '/opt/bin/whisper-cli', whisperModel: '/models/ko.bin', locale: 'ko-KR' };
beforeEach(() => { vi.clearAllMocks(); mocks.read.mockResolvedValue(Buffer.from([1, 2])); });
afterEach(() => { vi.unstubAllGlobals(); });
describe('device speech backend', () => {
  it('preserves Apple as the default', async () => {
    mocks.apple.mockResolvedValue('hello');
    await expect(transcribeDeviceAudio('/tmp/input.wav', { locale: 'ko-KR' })).resolves.toBe('hello');
    expect(mocks.apple).toHaveBeenCalledWith('/tmp/input.wav', 'ko-KR');
  });
  it('runs only the explicitly configured local executable with bounded argument-based IO', async () => {
    mocks.exec.mockImplementation((_exe, _args, _options, cb) => cb(null, { stdout: ' 명령입니다.\n', stderr: '' }));
    await expect(transcribeDeviceAudio('/tmp/audio file.wav', local)).resolves.toBe('명령입니다.');
    expect(mocks.exec).toHaveBeenCalledWith(local.whisperCli,
      ['-m', local.whisperModel, '-f', '/tmp/audio file.wav', '-l', 'ko', '-nt', '-np'],
      expect.objectContaining({ timeout: 60_000, windowsHide: true }), expect.any(Function));
    expect(mocks.apple).not.toHaveBeenCalled();
  });
  it('refuses missing executable/model configuration', async () => {
    await expect(transcribeDeviceAudio('/tmp/a.wav', { ...local, whisperCli: 'whisper-cli' })).rejects.toThrow('paths_required');
    expect(mocks.exec).not.toHaveBeenCalled();
  });
  it('propagates local errors and rejects empty speech without falling back', async () => {
    mocks.exec.mockImplementationOnce((_exe, _args, _options, cb) => cb(new Error('model missing')));
    await expect(transcribeDeviceAudio('/tmp/a.wav', local)).rejects.toThrow('model missing');
    mocks.exec.mockImplementationOnce((_exe, _args, _options, cb) => cb(null, { stdout: ' ', stderr: '' }));
    await expect(transcribeDeviceAudio('/tmp/a.wav', local)).rejects.toThrow('No speech');
    expect(mocks.apple).not.toHaveBeenCalled();
  });
});

describe('warm local speech server', () => {
  const warm = { ...local, whisperServerUrl: 'http://127.0.0.1:19121/inference' };
  it('uploads locally with a deadline and never follows redirects', async () => {
    const fetcher = vi.fn().mockResolvedValue(new Response(JSON.stringify({ text: ' 명령입니다. ' })));
    vi.stubGlobal('fetch', fetcher);
    await expect(transcribeDeviceAudio('/tmp/capture.wav', warm)).resolves.toBe('명령입니다.');
    const [url, request] = fetcher.mock.calls[0];
    expect(url.hostname).toBe('127.0.0.1');
    expect(request.redirect).toBe('error');
    expect(request.signal).toBeInstanceOf(AbortSignal);
    expect(request.body.get('language')).toBe('ko');
    expect(request.body.get('file')).toBeInstanceOf(Blob);
    expect(mocks.exec).not.toHaveBeenCalled();
  });
  it.each(['https://127.0.0.1/inference', 'http://example.com/inference',
    'http://127.0.0.1/other', 'http://user@127.0.0.1/inference'])('refuses %s before sending audio', async (url) => {
    const fetcher = vi.fn(); vi.stubGlobal('fetch', fetcher);
    await expect(transcribeDeviceAudio('/tmp/a.wav', { ...warm, whisperServerUrl: url })).rejects.toThrow('invalid_whisper_server_url');
    expect(fetcher).not.toHaveBeenCalled();
  });
  it.each(['unreachable', 'timeout', 'bad response'])('falls back to configured CLI on %s', async (kind) => {
    vi.stubGlobal('fetch', kind === 'bad response'
      ? vi.fn().mockResolvedValue(new Response('{}'))
      : vi.fn().mockRejectedValue(new Error(kind)));
    mocks.exec.mockImplementation((_exe, _args, _options, cb) => cb(null, { stdout: 'fallback', stderr: '' }));
    await expect(transcribeDeviceAudio('/tmp/a.wav', warm)).resolves.toBe('fallback');
    expect(mocks.exec).toHaveBeenCalledOnce();
  });
  it('does not retry genuinely empty speech', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('{"text":" "}')));
    await expect(transcribeDeviceAudio('/tmp/a.wav', warm)).rejects.toThrow('No speech');
    expect(mocks.exec).not.toHaveBeenCalled();
  });
});
