import { describe, it, expect, vi, beforeEach } from 'vitest';
const mocks = vi.hoisted(() => ({ exec: vi.fn(), apple: vi.fn() }));
vi.mock('node:child_process', () => ({ execFile: mocks.exec }));
vi.mock('../foundation-models-helper.js', () => ({ transcribeWithHelper: mocks.apple }));
import { transcribeDeviceAudio } from '../device-transcription.js';
const local = { transcriber: 'whisper-cpp' as const, whisperCli: '/opt/bin/whisper-cli', whisperModel: '/models/ko.bin', locale: 'ko-KR' };
beforeEach(() => { vi.clearAllMocks(); });
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
